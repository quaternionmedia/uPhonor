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
/* Play a file */
void play_file(void *userdata, struct spa_io_position *position)
{
  struct data *data = userdata;
  struct pw_buffer *b;
  uint32_t n_samples = position->clock.duration;

  float *in, *out;

  pw_log_trace("play file %d", n_samples);

  process_midi(userdata, position);

  // Always test if we can get input data, regardless of recording state
  in = pw_filter_get_dsp_buffer(data->audio_in, n_samples);
  if (in != NULL)
  {
    // Calculate RMS to see if there's actual audio data
    float rms = 0.0f;
    for (uint32_t i = 0; i < n_samples; i++)
    {
      rms += in[i] * in[i];
    }
    rms = sqrtf(rms / n_samples);

    if (rms > 0.001f) // Only log if there's significant audio
    {
      pw_log_info("Input audio detected: RMS = %f", rms);
    }
  }
  else
  {
    pw_log_trace("No input audio buffer available");
  }

  // Handle audio input recording
  if (data->recording_enabled && data->record_file)
  {
    if (in == NULL)
    {
      // No input connected - record silence
      pw_log_trace("No input for recording, writing silence");
      float *silence = calloc(n_samples, sizeof(float));
      if (silence)
      {
        sf_count_t frames_written = sf_writef_float(data->record_file, silence, n_samples);
        if (frames_written != n_samples)
        {
          pw_log_error("Could not write silence frames: wrote %ld of %d",
                       frames_written, n_samples);
        }
        free(silence);
      }
    }
    else
    {
      // We have input data - record it
      pw_log_trace("Recording %d samples from input", n_samples);

      sf_count_t frames_written = sf_writef_float(data->record_file, in, n_samples);
      if (frames_written != n_samples)
      {
        pw_log_error("Could not write all input frames: wrote %ld of %d. Error: %s",
                     frames_written, n_samples, sf_strerror(data->record_file));
      }
    }

    // Periodically sync to disk
    static int sync_counter = 0;
    if (++sync_counter >= 100)
    {
      pw_log_debug("Syncing recording file to disk");
      sf_write_sync(data->record_file);
      sync_counter = 0;
    }
  }

  // Rest of the function remains the same...
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
  pw_log_trace("File info: channels=%d, samplerate=%d",
               data->fileinfo.channels, data->fileinfo.samplerate);
  pw_log_debug("Buffer: maxsize=%d, stride=%d, calculated n_samples=%d",
               b->buffer->datas[0].maxsize, stride, n_samples);
  if (b->requested)
  {
    n_samples = SPA_MIN(n_samples, b->requested);
    pw_log_warn("Adjusted n_samples to %d based on requested=%d", n_samples, b->requested);
  }

  // Temporary buffer for multi-channel data
  float *temp_buf = malloc(n_samples * data->fileinfo.channels * sizeof(float));
  if (!temp_buf)
  {
    pw_log_error("Failed to allocate temporary buffer");
    pw_filter_queue_buffer(data->audio_out, b);
    return;
  }

  if (data->reset_audio)
  {
    pw_log_info("Resetting audio playback position");
    sf_seek(data->file, 0, SEEK_SET);
    data->reset_audio = false;
  }

  sf_count_t frames_read = sf_readf_float(data->file, temp_buf, n_samples);
  pw_log_debug("read %d frames from file", frames_read);

  if (frames_read < n_samples)
  {
    pw_log_debug("not enough frames read, seeking to start and reading again");
    sf_seek(data->file, 0, SEEK_SET);
    sf_count_t additional = sf_readf_float(data->file,
                                           &temp_buf[frames_read * data->fileinfo.channels],
                                           n_samples - frames_read);
    frames_read += additional;
  }

  // Extract only the first channel
  for (uint32_t i = 0; i < frames_read; i++)
  {
    buf[i] = temp_buf[i * data->fileinfo.channels] * data->volume;
  }

  free(temp_buf);

  b->buffer->datas[0].chunk->offset = 0;
  b->buffer->datas[0].chunk->stride = stride;
  b->buffer->datas[0].chunk->size = frames_read * stride;

  pw_filter_queue_buffer(data->audio_out, b);
}