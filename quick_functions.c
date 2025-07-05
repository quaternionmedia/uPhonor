/* Quick stub to get compilation working */
#include <stdint.h>
#include <sndfile.h>
#include "uphonor.h"

sf_count_t mix_all_active_loops_rt(struct data *data, float *buf, uint32_t n_samples)
{
  /* Initialize output buffer to silence */
  for (uint32_t i = 0; i < n_samples; i++)
  {
    buf[i] = 0.0f;
  }

  bool any_playing = false;
  bool pulse_loop_reset = false;

  /* Track pulse loop position for sync timing */
  uint32_t pulse_loop_old_position = 0;
  uint32_t pulse_loop_new_position = 0;
  bool pulse_loop_is_playing = false;

  if (data->sync_mode_enabled && data->pulse_loop_note != 255)
  {
    struct memory_loop *pulse_loop = &data->memory_loops[data->pulse_loop_note];
    if (pulse_loop->is_playing && pulse_loop->loop_ready && pulse_loop->recorded_frames > 0)
    {
      pulse_loop_old_position = pulse_loop->playback_position;
      pulse_loop_is_playing = true;
    }
  }

  /* Mix all active loops */
  for (int note = 0; note < 128; note++)
  {
    struct memory_loop *loop = &data->memory_loops[note];

    if (!loop->is_playing || !loop->loop_ready || loop->recorded_frames == 0)
      continue;

    any_playing = true;

    /* Simple loop playback */
    for (uint32_t i = 0; i < n_samples; i++)
    {
      if (loop->playback_position >= loop->recorded_frames)
      {
        loop->playback_position = 0; /* Loop back to beginning */

        // Check if this is the pulse loop resetting
        if (data->sync_mode_enabled && note == data->pulse_loop_note)
        {
          pulse_loop_reset = true;
        }
      }

      buf[i] += loop->buffer[loop->playback_position] * loop->volume;
      loop->playback_position++;
    }
  }

  /* Additional pulse reset detection: check if pulse loop completed a cycle during this buffer */
  if (pulse_loop_is_playing && !pulse_loop_reset && data->pulse_loop_note != 255)
  {
    struct memory_loop *pulse_loop = &data->memory_loops[data->pulse_loop_note];
    pulse_loop_new_position = pulse_loop->playback_position;

    // If pulse loop position wrapped around during this buffer (position decreased)
    if (pulse_loop_new_position < pulse_loop_old_position)
    {
      pulse_loop_reset = true;
    }
  }

  // Handle pulse loop reset events for sync coordination
  if (pulse_loop_reset)
  {
    // Stop any recordings that are pending stop
    stop_sync_pending_recordings_on_pulse_reset(data);
    // Start any recordings that are pending start
    start_sync_pending_recordings_on_pulse_reset(data);
    // Start any playback that is pending
    start_sync_pending_playback_on_pulse_reset(data);
  }

  return any_playing ? n_samples : 0;
}

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
