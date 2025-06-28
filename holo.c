#include "uphonor.h"
#include "rt_nonrt_bridge.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

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

    /* Give the RT bridge a moment to finish processing */
    usleep(10000); // 10ms

    data->current_state = HOLO_STATE_PLAYING;
    if (!data->file)
    {
      pw_log_info("No audio file preloaded. Loading recorded file.");

      /* Get the actual filename that was used for recording */
      const char *recorded_filename = rt_bridge_get_current_filename(&data->rt_bridge);
      pw_log_info("RT bridge reports filename: %s", recorded_filename ? recorded_filename : "NULL");

      if (recorded_filename)
      {
        /* Update our record_filename pointer to the recorded file */
        if (data->record_filename)
        {
          free(data->record_filename);
        }
        data->record_filename = strdup(recorded_filename);
        pw_log_info("Starting playback of: %s", data->record_filename);
        start_playing(data, data->record_filename);
      }
      else
      {
        pw_log_error("No recorded file found to play back");
        data->current_state = HOLO_STATE_IDLE;
      }
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
