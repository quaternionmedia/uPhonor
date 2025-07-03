#include "uphonor.h"
#include "audio_processing_rt.h"
#include <stdbool.h>

/* Mix all active memory loops into output buffer */
sf_count_t mix_all_active_loops_rt(struct data *data, float *buf, uint32_t n_samples)
{
  /* Initialize output buffer to silence */
  for (uint32_t i = 0; i < n_samples; i++)
  {
    buf[i] = 0.0f;
  }

  bool any_playing = false;
  float temp_buffer[n_samples]; /* Buffer for individual loop */

  /* Mix all active loops */
  for (int note = 0; note < 128; note++)
  {
    struct memory_loop *loop = &data->memory_loops[note];
    
    if (!loop->is_playing || !loop->loop_ready || loop->recorded_frames == 0)
      continue;

    any_playing = true;

    /* Read from this loop into temp buffer */
    sf_count_t frames_read = read_audio_frames_from_memory_loop_basic_rt(loop, temp_buffer, n_samples);

    /* Mix this loop into output buffer with volume control */
    for (uint32_t i = 0; i < frames_read && i < n_samples; i++)
    {
      buf[i] += temp_buffer[i] * loop->volume;
    }
  }

  return any_playing ? n_samples : 0;
}

/* Basic memory loop reading for individual loops */
sf_count_t read_audio_frames_from_memory_loop_basic_rt(struct memory_loop *loop, float *buf, uint32_t n_samples)
{
  if (!loop || !loop->buffer || !loop->loop_ready || !buf)
    return 0;

  if (loop->recorded_frames == 0)
    return 0;

  uint32_t frames_copied = 0;
  uint32_t total_frames = loop->recorded_frames;

  for (uint32_t i = 0; i < n_samples; i++)
  {
    if (loop->playback_position >= total_frames)
    {
      loop->playback_position = 0; /* Loop back to beginning */
    }

    buf[i] = loop->buffer[loop->playback_position];
    loop->playback_position++;
    frames_copied++;
  }

  return frames_copied;
}
