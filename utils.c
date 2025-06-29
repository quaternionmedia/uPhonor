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
  pw_log_info("Playback speed set to %.2fx", new_speed);
}

void set_pitch_shift(struct data *data, float new_pitch)
{
  // Clamp pitch shift to reasonable range
  if (new_pitch < 0.25f)
    new_pitch = 0.25f; // Minimum 0.25x pitch (2 octaves down)
  if (new_pitch > 4.0f)
    new_pitch = 4.0f; // Maximum 4x pitch (2 octaves up)

  data->pitch_shift = new_pitch;
  pw_log_info("Pitch shift set to %.2fx", new_pitch);
}
