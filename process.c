#include "uphonor.h"
#include "process-midi.c"

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

  // in = pw_filter_get_dsp_buffer(data->audio_in, n_samples);
  // out = pw_filter_get_dsp_buffer(data->audio_out, n_samples);

  // if (in == NULL)
  //   return;

  // memcpy(out, in, n_samples * sizeof(float));

  if ((b = pw_filter_dequeue_buffer(data->audio_out)) == NULL)
  {
    pw_log_warn("out of buffers");
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
  pw_log_info("File info: channels=%d, samplerate=%d",
              data->fileinfo.channels, data->fileinfo.samplerate);
  pw_log_info("Buffer: maxsize=%d, stride=%d, calculated n_samples=%d",
              b->buffer->datas[0].maxsize, stride, n_samples);
  if (b->requested)
  {
    n_samples = SPA_MIN(n_samples, b->requested);
    pw_log_info("Adjusted n_samples to %d based on requested=%d", n_samples, b->requested);
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
