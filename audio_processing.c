#include "audio_processing.h"
#include <time.h>
#include "buffer_manager.h"
#include "play.c"

void handle_audio_input(struct data *data, uint32_t n_samples)
{
  static struct audio_buffers buffers = {0};

  // Get input buffer for recording
  float *in = pw_filter_get_dsp_buffer(data->audio_in, n_samples);

  // Initialize buffers if needed
  uint32_t required_size = n_samples * data->fileinfo.channels;
  if (initialize_audio_buffers(&buffers, required_size) < 0)
    return;

  // Only calculate RMS occasionally to reduce CPU load
  if (in != NULL && ++buffers.rms_skip_counter >= 10)
  {
    float rms = calculate_rms(in, n_samples);
    if (rms > 0.001f)
    {
      pw_log_info("Input audio detected: RMS = %f", rms);
    }
    buffers.rms_skip_counter = 0;
  }

  // Handle audio input recording - optimized
  if (data->recording_enabled && data->record_file)
  {
    sf_count_t frames_written;

    if (in == NULL)
    {
      // Use pre-allocated silence buffer
      frames_written = sf_writef_float(data->record_file,
                                       buffers.silence_buffer, n_samples);
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
    if (++buffers.sync_counter >= 500)
    {
      sf_write_sync(data->record_file);
      buffers.sync_counter = 0;
    }
  }
}

sf_count_t read_audio_frames(struct data *data, float *buf, uint32_t n_samples,
                             float *temp_buffer)
{
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

  return frames_read;
}

void handle_end_of_file(struct data *data, float *buf, sf_count_t frames_read,
                        uint32_t n_samples, float *temp_buffer)
{
  if (frames_read >= n_samples)
    return; // No need to handle EOF

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
  }
}

void apply_volume(float *buf, uint32_t frames, float volume)
{
  if (volume == 1.0f)
    return; // No change needed

  // Apply volume in-place (vectorizable by compiler)
  for (uint32_t i = 0; i < frames; i++)
  {
    buf[i] *= volume;
  }
}

void process_audio_output(struct data *data, struct spa_io_position *position)
{
  static struct audio_buffers buffers = {0};
  struct pw_buffer *b;
  uint32_t n_samples = position->clock.duration;

  if (data->current_state != HOLO_STATE_PLAYING)
  {
    return; // Skip processing if not in playing state
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

  // Initialize buffers if needed
  uint32_t required_size = n_samples * data->fileinfo.channels;
  if (initialize_audio_buffers(&buffers, required_size) < 0)
  {
    pw_filter_queue_buffer(data->audio_out, b);
    return;
  }

  // Read audio data - optimized for mono output
  sf_count_t frames_read = read_audio_frames(data, buf, n_samples, buffers.temp_buffer);

  // Handle end of file with single seek operation
  handle_end_of_file(data, buf, frames_read, n_samples, buffers.temp_buffer);

  // Apply volume
  apply_volume(buf, frames_read, data->volume);

  b->buffer->datas[0].chunk->offset = 0;
  b->buffer->datas[0].chunk->stride = stride;
  b->buffer->datas[0].chunk->size = frames_read * stride;

  pw_filter_queue_buffer(data->audio_out, b);
}
