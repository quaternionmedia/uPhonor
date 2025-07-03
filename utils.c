/* Utility functions */

#include "uphonor.h"

void set_volume(struct data *data, float new_volume)
{
  // Clamp volume to reasonable range
  if (new_volume < 0.0f)
    new_volume = 0.0f;
  if (new_volume > 2.0f)
    new_volume = 2.0f; // Allow up to 200% amplification

  data->volume = new_volume;
  pw_log_info("Volume set to %.2f", new_volume);
}

// Convert linear volume (0.0-1.0) to logarithmic
float linear_to_db_volume(float linear_volume)
{
  if (linear_volume <= 0.0f)
    return 0.0f;
  return powf(linear_volume, 2.0f); // Square for more natural feel
}

void set_playback_speed(struct data *data, float new_speed)
{
  // Clamp playback speed to reasonable range
  if (new_speed < 0.1f)
    new_speed = 0.1f; // Minimum 0.1x speed
  if (new_speed > 8.0f)
    new_speed = 8.0f; // Maximum 8x speed

  data->playback_speed = new_speed;

  /* Update rubberband time ratio if enabled */
  if (data->rubberband_enabled && data->rubberband_state)
  {
    rubberband_set_time_ratio(data->rubberband_state, new_speed);
  }

  pw_log_info("Playback speed set to %.2fx", new_speed);
}

void set_record_player_mode(struct data *data, float speed_pitch_factor)
{
  // Clamp speed/pitch factor to reasonable range
  if (speed_pitch_factor < 0.1f)
    speed_pitch_factor = 0.1f; // Minimum 0.1x
  if (speed_pitch_factor > 8.0f)
    speed_pitch_factor = 8.0f; // Maximum 8x

  /* Disable rubberband processing */
  set_rubberband_enabled(data, false);

  /* Set playback speed (this will affect both speed and pitch in record player mode) */
  data->playback_speed = speed_pitch_factor;

  /* Reset pitch shift since it's controlled by speed in record player mode */
  data->pitch_shift = 0.0f;

  pw_log_info("Record player mode: Speed/pitch set to %.2fx (rubberband disabled)", speed_pitch_factor);
}
