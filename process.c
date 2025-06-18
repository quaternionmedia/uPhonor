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