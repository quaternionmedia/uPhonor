#include "uphonor.h"

#define M_PI_M2 (M_PI + M_PI)

#define DEFAULT_RATE 44100
#define DEFAULT_FREQ 440
#define DEFAULT_VOLUME 0.7

/* Play a simple tone */
static void tone(void *userdata, struct spa_io_position *position)
{
  struct data *data = userdata;
  float *out;
  struct port *audio_out = data->audio_out;
  uint32_t i, n_samples = position->clock.duration;

  pw_log_debug("do process %d", n_samples);

  out = pw_filter_get_dsp_buffer(audio_out, n_samples);
  if (out == NULL)
    return;

  for (i = 0; i < n_samples; i++)
  {
    audio_out->accumulator += M_PI_M2 * DEFAULT_FREQ / DEFAULT_RATE;
    if (audio_out->accumulator >= M_PI_M2)
      audio_out->accumulator -= M_PI_M2;

    *out++ = sin(audio_out->accumulator) * DEFAULT_VOLUME;
  }
}