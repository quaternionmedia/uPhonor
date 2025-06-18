#include "uphonor.h"

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

void play_file(void *userdata, struct spa_io_position *position)
{
  struct data *data = userdata;
  struct pw_buffer *b;
  uint32_t n_samples = position->clock.duration;

  float *in, *out;

  pw_log_trace("play file %d", n_samples);

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

  uint32_t stride = sizeof(float) * data->fileinfo.channels * 8;
  uint32_t n_frames = b->buffer->datas[0].maxsize / stride;
  pw_log_debug("requested %d frames, maxsize %d, stride %d",
               n_frames, b->buffer->datas[0].maxsize, stride);
  if (b->requested)
  {
    n_frames = SPA_MIN(n_frames, b->requested);
    pw_log_debug("adjusted n_frames to %d based on requested size", n_frames);
  }
  sf_count_t frames_read = sf_readf_float(data->file, buf, n_frames);
  pw_log_debug("read %d frames from file", frames_read);

  if (frames_read < n_frames)
  {
    pw_log_debug("not enough frames read, seeking to start and reading again");
    sf_seek(data->file, 0, SEEK_SET);
    sf_count_t additional = sf_readf_float(data->file,
                                           &buf[frames_read * data->fileinfo.channels],
                                           n_frames - frames_read);

    frames_read += additional;
  }

  b->buffer->datas[0].chunk->offset = 0;
  b->buffer->datas[0].chunk->stride = stride;
  b->buffer->datas[0].chunk->size = frames_read * stride;

  pw_filter_queue_buffer(data->audio_out, b);
}