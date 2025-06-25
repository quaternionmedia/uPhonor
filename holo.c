#include "uphonor.h"

// This function processes the loops based on the current state
void process_loops(struct data *data, struct spa_io_position *position, float volume)
{
  // Ensure volume is within valid range
  if (volume < 0.0f || volume > 1.0f)
  {
    pw_log_warn("Invalid volume level: %.2f, resetting to default (1.0)", volume);
    volume = 1.0f;
  }

  // Log the current state and volume
  pw_log_trace("Processing loops in state %d with volume %.2f", data->current_state, volume);

  switch (data->current_state)
  {
  case HOLO_STATE_IDLE:
    pw_log_info("Starting recording");
    start_recording(data, NULL);
    data->current_state = HOLO_STATE_RECORDING;
    break;
  case HOLO_STATE_RECORDING:
    pw_log_info("Stopping recording");
    stop_recording(data);
    data->current_state = HOLO_STATE_PLAYING;
    if (!data->file)
    {
      pw_log_info("No audio file preloaded. Loading recorded file.");
      start_playing(data, data->record_filename);
    }
    break;
  case HOLO_STATE_PLAYING:
    pw_log_info("Stopping playback");
    data->current_state = HOLO_STATE_STOPPED;
    break;
  case HOLO_STATE_STOPPED:
    pw_log_info("Restarting playback");
    data->current_state = HOLO_STATE_PLAYING;
    // Reset playback position
    data->offset = 0;
    data->position = 0;
    break;

  default:
    pw_log_warn("Unknown state %d, ignoring Note On message",
                data->current_state);
    break;
  }
  pw_log_info("Resetting audio playback due to Note On message");
  data->reset_audio = true;
  // set the volume from the Note On message velocity
  pw_log_info("Setting volume to %.2f from Note On velocity %d", volume);
  data->volume = volume;
}
