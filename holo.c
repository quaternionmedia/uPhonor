#include "uphonor.h"
#include "rt_nonrt_bridge.h"
#include "audio_processing_rt.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

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
    pw_log_info("Starting memory loop recording");
    /* Generate timestamp-based filename for the loop */
    {
      time_t now;
      time(&now);
      struct tm *tm_info = localtime(&now);
      char loop_filename[256];
      snprintf(loop_filename, sizeof(loop_filename),
               "loop_%04d-%02d-%02d_%02d-%02d-%02d.wav",
               tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
               tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);

      start_loop_recording_rt(data, loop_filename);
    }
    data->current_state = HOLO_STATE_RECORDING;
    break;

  case HOLO_STATE_RECORDING:
    pw_log_info("Stopping memory loop recording");
    stop_loop_recording_rt(data);

    /* Give the system a moment to process the stop command */
    usleep(1000); // 1ms

    data->current_state = HOLO_STATE_PLAYING;
    pw_log_info("Starting playback from memory loop");
    break;

  case HOLO_STATE_PLAYING:
    pw_log_info("Stopping playback");
    data->current_state = HOLO_STATE_STOPPED;
    break;

  case HOLO_STATE_STOPPED:
    pw_log_info("Restarting playback");
    data->current_state = HOLO_STATE_PLAYING;
    /* Reset memory loop playback position */
    reset_memory_loop_playback_rt(data);
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
