#include "uphonor.h"
#include "process-midi.c"
#include "record.c"

/* Simple in->out passthrough */
void on_process(void *userdata, struct spa_io_position *position)
{
  struct data *data = userdata;
  float *in, *out;
  uint32_t n_samples = position->clock.duration;

  pw_log_trace("do process %d", n_samples);

  in = pw_filter_get_dsp_buffer(data->audio_in, n_samples);
  out = pw_filter_get_dsp_buffer(data->audio_out, n_samples);

  if (in == NULL || out == NULL)
    return;

  pw_log_trace("Processing %d samples", n_samples);

  memcpy(out, in, n_samples * sizeof(float));
}

/* Play a file - optimized version */
void play_file(void *userdata, struct spa_io_position *position)
{
  struct data *data = userdata;
  struct pw_buffer *b;
  uint32_t n_samples = position->clock.duration;

  // Pre-allocated static buffers to avoid malloc/free in real-time
  static float *silence_buffer = NULL;
  static float *temp_buffer = NULL;
  static uint32_t buffer_size = 0;
  static int sync_counter = 0;
  static int rms_skip_counter = 0;

  float *in, *out;

  pw_log_trace("play file %d", n_samples);

  process_midi(userdata, position);

  // Initialize buffers on first call or when size changes
  if (buffer_size < n_samples * data->fileinfo.channels)
  {
    buffer_size = n_samples * data->fileinfo.channels * 2; // Extra space for future growth

    silence_buffer = realloc(silence_buffer, buffer_size * sizeof(float));
    temp_buffer = realloc(temp_buffer, buffer_size * sizeof(float));

    if (!silence_buffer || !temp_buffer)
    {
      pw_log_error("Failed to allocate audio buffers");
      return;
    }

    // Pre-fill silence buffer once
    memset(silence_buffer, 0, buffer_size * sizeof(float));
  }

  // Get input buffer for recording
  in = pw_filter_get_dsp_buffer(data->audio_in, n_samples);

  // Only calculate RMS occasionally to reduce CPU load
  if (in != NULL && ++rms_skip_counter >= 10)
  {
    float rms = 0.0f;
    for (uint32_t i = 0; i < n_samples; i++)
    {
      rms += in[i] * in[i];
    }
    rms = sqrtf(rms / n_samples);

    if (rms > 0.001f)
    {
      pw_log_info("Input audio detected: RMS = %f", rms);
    }
    rms_skip_counter = 0;
  }

  // Handle audio input recording - optimized
  if (data->recording_enabled && data->record_file)
  {
    sf_count_t frames_written;

    if (in == NULL)
    {
      // Use pre-allocated silence buffer
      frames_written = sf_writef_float(data->record_file, silence_buffer, n_samples);
    }
    else
    {
      // Direct write of input data
      frames_written = sf_writef_float(data->record_file, in, n_samples);
    }

    if (frames_written != n_samples)
    {
      pw_log_error("Could not write all frames: wrote %ld of %d",
                   frames_written, n_samples);
    }

    // Sync to disk less frequently (every 500 callbacks ~= 10 seconds at 48kHz/1024)
    if (++sync_counter >= 500)
    {
      sf_write_sync(data->record_file);
      sync_counter = 0;
    }
  }

  // Get output buffer
  if ((b = pw_filter_dequeue_buffer(data->audio_out)) == NULL)
  {
    pw_log_trace("Out of buffers");
    return;
  }

  float *buf = b->buffer->datas[0].data;
  if (buf == NULL)
  {
    pw_log_warn("buffer data is NULL");
    pw_filter_queue_buffer(data->audio_out, b);
    return;
  }

  uint32_t stride = sizeof(float);

  if (b->requested)
  {
    n_samples = SPA_MIN(n_samples, b->requested);
  }

  // Handle audio reset
  if (data->reset_audio)
  {
    pw_log_info("Resetting audio playback position");
    sf_seek(data->file, 0, SEEK_SET);
    data->reset_audio = false;
  }

  // Read audio data - optimized for mono output
  sf_count_t frames_read;

  if (data->fileinfo.channels == 1)
  {
    // Direct read for mono files
    frames_read = sf_readf_float(data->file, buf, n_samples);
  }
  else
  {
    // Read multi-channel data and extract first channel
    frames_read = sf_readf_float(data->file, temp_buffer, n_samples);

    // Optimized channel extraction using pointer arithmetic
    float *src = temp_buffer;
    for (uint32_t i = 0; i < frames_read; i++)
    {
      buf[i] = *src;
      src += data->fileinfo.channels;
    }
  }

  // Handle end of file with single seek operation
  if (frames_read < n_samples)
  {
    sf_seek(data->file, 0, SEEK_SET);

    sf_count_t remaining = n_samples - frames_read;
    if (data->fileinfo.channels == 1)
    {
      sf_count_t additional = sf_readf_float(data->file, &buf[frames_read], remaining);
      frames_read += additional;
    }
    else
    {
      sf_count_t additional = sf_readf_float(data->file, temp_buffer, remaining);
      float *src = temp_buffer;
      for (uint32_t i = 0; i < additional; i++)
      {
        buf[frames_read + i] = *src;
        src += data->fileinfo.channels;
      }
      frames_read += additional;
    }
  }

  // Apply volume in-place (vectorizable by compiler)
  if (data->volume != 1.0f)
  {
    for (uint32_t i = 0; i < frames_read; i++)
    {
      buf[i] *= data->volume;
    }
  }

  b->buffer->datas[0].chunk->offset = 0;
  b->buffer->datas[0].chunk->stride = stride;
  b->buffer->datas[0].chunk->size = frames_read * stride;

  pw_filter_queue_buffer(data->audio_out, b);
}