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
