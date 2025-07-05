#include "uphonor.h"
#include "rt_nonrt_bridge.h"
#include "audio_processing_rt.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

// Get the memory loop for a specific MIDI note
struct memory_loop *get_loop_by_note(struct data *data, uint8_t midi_note)
{
  if (midi_note > 127)
  {
    pw_log_warn("Invalid MIDI note: %d", midi_note);
    return NULL;
  }
  return &data->memory_loops[midi_note];
}

// Stop all recordings (emergency function)
void stop_all_recordings(struct data *data)
{
  for (int i = 0; i < 128; i++)
  {
    struct memory_loop *loop = &data->memory_loops[i];
    if (loop->current_state == LOOP_STATE_RECORDING)
    {
      pw_log_info("Emergency stop recording for note %d", i);
      loop->current_state = LOOP_STATE_STOPPED;
      loop->recording_to_memory = false;
    }
  }
  data->currently_recording_note = 255; // 255 = no note recording
}

// Stop all playback (utility function)
void stop_all_playback(struct data *data)
{
  for (int i = 0; i < 128; i++)
  {
    struct memory_loop *loop = &data->memory_loops[i];
    if (loop->is_playing)
    {
      pw_log_info("Stopping playback for note %d", i);
      loop->is_playing = false;
      loop->current_state = LOOP_STATE_STOPPED;
    }
  }
}

// Initialize all memory loops
int init_all_memory_loops(struct data *data, uint32_t max_seconds, uint32_t sample_rate)
{
  pw_log_info("Initializing %d memory loops for MIDI notes 0-127", 128);

  data->active_loop_count = 0;
  data->currently_recording_note = 255;               // 255 = no note recording
  data->current_playback_mode = PLAYBACK_MODE_NORMAL; // Default to normal mode

  // Initialize sync mode fields
  data->sync_mode_enabled = true; // Sync mode enabled by default
  data->pulse_loop_note = 255;    // No pulse loop set
  data->pulse_loop_duration = 0;
  data->waiting_for_pulse_reset = false;
  data->longest_loop_duration = 0;
  data->sync_cutoff_percentage = 0.5f; // Default to 50% cutoff

  for (int i = 0; i < 128; i++)
  {
    struct memory_loop *loop = &data->memory_loops[i];

    // Initialize the loop structure
    memset(loop, 0, sizeof(struct memory_loop));
    loop->midi_note = i;
    loop->current_state = LOOP_STATE_IDLE;
    loop->sample_rate = sample_rate;
    loop->volume = 1.0f;          // Default volume
    loop->pending_record = false; // Not waiting to record
    loop->pending_stop = false;   // Not waiting to stop recording
    loop->pending_start = false;  // Not waiting to start playing

    // Allocate buffer (60 seconds worth of audio)
    loop->buffer_size = max_seconds * sample_rate; // frames (mono)
    loop->buffer = calloc(loop->buffer_size, sizeof(float));
    if (!loop->buffer)
    {
      pw_log_error("Failed to allocate memory loop buffer for note %d", i);
      // Clean up previously allocated loops
      for (int j = 0; j < i; j++)
      {
        free(data->memory_loops[j].buffer);
        data->memory_loops[j].buffer = NULL;
      }
      return -1;
    }

    pw_log_debug("Initialized memory loop for note %d: %u frames", i, loop->buffer_size);
  }

  pw_log_info("Successfully initialized all %d memory loops", 128);
  return 0;
}

// Cleanup all memory loops
void cleanup_all_memory_loops(struct data *data)
{
  pw_log_info("Cleaning up all memory loops");

  for (int i = 0; i < 128; i++)
  {
    struct memory_loop *loop = &data->memory_loops[i];
    if (loop->buffer)
    {
      free(loop->buffer);
      loop->buffer = NULL;
    }
  }

  data->active_loop_count = 0;
  data->currently_recording_note = 255;

  pw_log_info("All memory loops cleaned up");
}

// This function processes the loops based on the current state for a specific MIDI note
void process_loops(struct data *data, struct spa_io_position *position, uint8_t midi_note, float volume)
{
  // Validate inputs
  if (midi_note > 127)
  {
    pw_log_warn("Invalid MIDI note: %d, ignoring", midi_note);
    return;
  }

  // Ensure volume is within valid range
  if (volume < 0.0f || volume > 1.0f)
  {
    pw_log_warn("Invalid volume level: %.2f, clamping to [0.0, 1.0]", volume);
    volume = (volume < 0.0f) ? 0.0f : 1.0f;
  }

  struct memory_loop *loop = get_loop_by_note(data, midi_note);
  if (!loop)
  {
    pw_log_error("Failed to get loop for note %d", midi_note);
    return;
  }

  // Set the volume for this specific loop
  loop->volume = volume;

  // Log the current state and volume
  pw_log_info("Processing loop for note %d in state %d with volume %.2f",
              midi_note, loop->current_state, volume);

  switch (loop->current_state)
  {
  case LOOP_STATE_IDLE:
    // Check sync mode constraints
    if (data->sync_mode_enabled && data->pulse_loop_note != 255)
    {
      // In sync mode with active pulse loop, don't start recording immediately
      // This should be handled by sync timing logic elsewhere
      pw_log_info("Sync mode active - recording for note %d will wait for pulse loop sync", midi_note);

      // Only mark as pending if not already pending
      if (!loop->pending_record)
      {
        loop->pending_record = true;
        pw_log_info("Marking note %d as pending for sync recording", midi_note);
      }
      else
      {
        pw_log_info("Note %d already pending for sync recording", midi_note);
      }

      // Also check if we can start recording immediately (if pulse loop just reset)
      check_sync_pending_recordings(data);
      return;
    }

    // If another loop is recording, stop it first
    if (data->currently_recording_note != 255 && data->currently_recording_note != midi_note)
    {
      struct memory_loop *recording_loop = get_loop_by_note(data, data->currently_recording_note);
      if (recording_loop && recording_loop->current_state == LOOP_STATE_RECORDING)
      {
        pw_log_info("Stopping recording for note %d to start recording note %d",
                    data->currently_recording_note, midi_note);
        stop_loop_recording_rt(data, data->currently_recording_note);
        recording_loop->current_state = LOOP_STATE_PLAYING;
        recording_loop->is_playing = true;
      }
    }

    // Set as pulse loop if none exists
    if (data->pulse_loop_note == 255)
    {
      data->pulse_loop_note = midi_note;
      pw_log_info("Setting note %d as pulse loop", midi_note);
    }

    pw_log_info("Starting memory loop recording for note %d", midi_note);
    /* Generate timestamp-based filename for the loop */
    {
      time_t now;
      time(&now);
      struct tm *tm_info = localtime(&now);
      snprintf(loop->loop_filename, sizeof(loop->loop_filename),
               "loop_note_%03d_%04d-%02d-%02d_%02d-%02d-%02d.wav",
               midi_note,
               tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
               tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);

      start_loop_recording_rt(data, midi_note, loop->loop_filename);
    }
    loop->current_state = LOOP_STATE_RECORDING;
    data->currently_recording_note = midi_note;
    data->active_loop_count++;
    break;

  case LOOP_STATE_RECORDING:
    // In sync mode, don't stop recording immediately - mark as pending stop
    if (data->sync_mode_enabled && data->pulse_loop_note != 255)
    {
      // Special case: if this is the pulse loop itself, allow it to stop normally
      // to establish the pulse duration
      if (midi_note != data->pulse_loop_note)
      {
        if (!loop->pending_stop)
        {
          loop->pending_stop = true;
          pw_log_info("SYNC mode: Marking recording for note %d to stop at next pulse reset", midi_note);
        }
        else
        {
          pw_log_info("SYNC mode: Note %d already marked to stop at next pulse reset", midi_note);
        }
        return; // Don't stop immediately for non-pulse loops
      }
      // Fall through for pulse loop to stop normally and set duration
    }

    pw_log_info("Stopping memory loop recording for note %d", midi_note);
    stop_loop_recording_rt(data, midi_note);

    /* Give the system a moment to process the stop command */
    usleep(1000); // 1ms

    loop->current_state = LOOP_STATE_PLAYING;
    data->currently_recording_note = 255; // No longer recording

    // In sync mode, if this is the pulse loop, ensure it's playing and set duration
    if (data->sync_mode_enabled && midi_note == data->pulse_loop_note)
    {
      // Pulse loop always starts playing immediately
      loop->is_playing = true;
      data->pulse_loop_duration = loop->recorded_frames;
      pw_log_info("SYNC mode: Pulse loop (note %d) recorded with %u frames, now playing",
                  midi_note, data->pulse_loop_duration);
      // Check for any pending recordings that can now start
      check_sync_pending_recordings(data);
    }
    else if (data->sync_mode_enabled && data->pulse_loop_note != 255)
    {
      // This is a non-pulse loop in sync mode - decide whether to start immediately or wait
      struct memory_loop *pulse_loop = &data->memory_loops[data->pulse_loop_note];
      if (pulse_loop->is_playing && data->pulse_loop_duration > 0)
      {
        // Calculate current pulse position and cutoff point
        uint32_t pulse_position = pulse_loop->playback_position;
        uint32_t cutoff_position = (uint32_t)(data->sync_cutoff_percentage * data->pulse_loop_duration);

        // Decide whether to sync to current pulse or wait for next
        if (pulse_position <= cutoff_position)
        {
          // Before cutoff - sync to current pulse position and start playing
          if (pulse_position < loop->recorded_frames)
          {
            // Pulse position fits within this loop - start there
            loop->playback_position = pulse_position;
          }
          else
          {
            // Pulse position is beyond this loop's length - use modulo
            loop->playback_position = pulse_position % loop->recorded_frames;
          }

          loop->is_playing = true;
          loop->pending_start = false;

          pw_log_info("SYNC mode: Starting recorded loop %d at current pulse position %u (pulse at %u, cutoff at %u)",
                      midi_note, loop->playback_position, pulse_position, cutoff_position);
        }
        else
        {
          // After cutoff - mark as pending start and wait for next pulse cycle
          loop->playback_position = 0;
          loop->is_playing = false;
          loop->pending_start = true;

          pw_log_info("SYNC mode: Loop %d marked as pending start - waiting for next pulse cycle (pulse at %u, cutoff at %u)",
                      midi_note, pulse_position, cutoff_position);
        }
      }
      else
      {
        // No pulse loop or pulse not playing - start immediately
        loop->is_playing = true;
        loop->pending_start = false;
      }
    }
    else
    {
      // Not in sync mode - start playing immediately
      loop->is_playing = true;
      loop->pending_start = false;
    }

    pw_log_info("Starting playback from memory loop for note %d", midi_note);
    break;

  case LOOP_STATE_PLAYING:
    pw_log_info("Stopping playback for note %d", midi_note);
    loop->current_state = LOOP_STATE_STOPPED;
    loop->is_playing = false;
    loop->pending_start = false; // Clear pending start if stopping manually
    break;

  case LOOP_STATE_STOPPED:
    pw_log_info("Restarting playback for note %d", midi_note);
    loop->current_state = LOOP_STATE_PLAYING;
    loop->pending_start = false; // Clear pending start when manually starting
    loop->is_playing = true;
    /* Reset memory loop playback position */
    reset_memory_loop_playback_rt(data, midi_note);
    break;

  default:
    pw_log_warn("Unknown state %d for note %d, ignoring",
                loop->current_state, midi_note);
    break;
  }

  pw_log_info("Loop state changed for note %d: state=%d, playing=%s",
              midi_note, loop->current_state, loop->is_playing ? "yes" : "no");

  // Reset audio only when sync mode is disabled or when this is a new recording
  if (!data->sync_mode_enabled || loop->current_state == LOOP_STATE_RECORDING)
  {
    data->reset_audio = true;
  }
}

// Playback mode management functions
void set_playback_mode_normal(struct data *data)
{
  data->current_playback_mode = PLAYBACK_MODE_NORMAL;
  pw_log_info("Playback mode set to NORMAL (Note On toggles play/stop, Note Off ignored)");
}

void set_playback_mode_trigger(struct data *data)
{
  data->current_playback_mode = PLAYBACK_MODE_TRIGGER;
  pw_log_info("Playback mode set to TRIGGER (Note On starts, Note Off stops)");
}

void toggle_playback_mode(struct data *data)
{
  if (data->current_playback_mode == PLAYBACK_MODE_NORMAL)
  {
    set_playback_mode_trigger(data);
  }
  else
  {
    set_playback_mode_normal(data);
  }
}

const char *get_playback_mode_name(struct data *data)
{
  switch (data->current_playback_mode)
  {
  case PLAYBACK_MODE_NORMAL:
    return "NORMAL";
  case PLAYBACK_MODE_TRIGGER:
    return "TRIGGER";
  default:
    return "UNKNOWN";
  }
}

/* Sync mode functions (independent of playback mode) */
void enable_sync_mode(struct data *data)
{
  data->sync_mode_enabled = true;
  init_sync_mode(data);
  pw_log_info("Sync mode ENABLED - waiting for first loop to set pulse");
}

void disable_sync_mode(struct data *data)
{
  data->sync_mode_enabled = false;
  data->pulse_loop_note = 255; // Clear pulse loop
  data->pulse_loop_duration = 0;
  data->waiting_for_pulse_reset = false;
  data->longest_loop_duration = 0;

  // Clear any pending recordings and allow them to start immediately
  for (int i = 0; i < 128; i++)
  {
    struct memory_loop *loop = &data->memory_loops[i];
    if (loop->pending_record)
    {
      loop->pending_record = false;
      pw_log_info("Clearing pending recording for note %d due to sync mode disable", i);
    }
    if (loop->pending_stop)
    {
      loop->pending_stop = false;
      pw_log_info("Clearing pending stop for note %d due to sync mode disable", i);
    }
    if (loop->pending_start)
    {
      // If a loop was pending start, make it start playing immediately
      loop->pending_start = false;
      if (loop->current_state == LOOP_STATE_PLAYING && loop->loop_ready)
      {
        loop->is_playing = true;
        pw_log_info("Starting pending loop %d due to sync mode disable", i);
      }
    }
  }

  pw_log_info("Sync mode DISABLED - all loops now independent");
}

void toggle_sync_mode(struct data *data)
{
  if (data->sync_mode_enabled)
  {
    disable_sync_mode(data);
  }
  else
  {
    enable_sync_mode(data);
  }
}

bool is_sync_mode_enabled(struct data *data)
{
  return data->sync_mode_enabled;
}

void init_sync_mode(struct data *data)
{
  data->pulse_loop_note = 255; // No pulse loop set
  data->pulse_loop_duration = 0;
  data->waiting_for_pulse_reset = false;
  data->longest_loop_duration = 0;
  pw_log_info("Sync mode initialized - waiting for first loop to set pulse");
}

bool can_start_recording_sync(struct data *data, uint8_t midi_note)
{
  // If no pulse loop is set, this will become the pulse loop
  if (data->pulse_loop_note == 255)
  {
    return true;
  }

  // If this is the pulse loop, check if we're waiting for its reset
  if (midi_note == data->pulse_loop_note)
  {
    return !data->waiting_for_pulse_reset;
  }

  // For other loops, only allow recording when pulse loop resets
  return !data->waiting_for_pulse_reset;
}

void set_pulse_loop(struct data *data, uint8_t midi_note)
{
  if (data->pulse_loop_note == 255)
  {
    data->pulse_loop_note = midi_note;
    pw_log_info("SYNC mode: Note %d set as pulse loop", midi_note);
  }
}

void check_sync_playback_reset(struct data *data)
{
  if (!data->sync_mode_enabled)
    return;

  // Find the longest currently playing loop
  uint32_t longest_duration = 0;
  bool any_playing = false;

  for (int i = 0; i < 128; i++)
  {
    struct memory_loop *loop = &data->memory_loops[i];
    if (loop->is_playing && loop->recorded_frames > 0)
    {
      any_playing = true;
      if (loop->recorded_frames > longest_duration)
      {
        longest_duration = loop->recorded_frames;
      }
    }
  }

  if (!any_playing)
    return;

  // Check if any loop has reached the end of the longest loop
  for (int i = 0; i < 128; i++)
  {
    struct memory_loop *loop = &data->memory_loops[i];
    if (loop->is_playing && loop->playback_position >= longest_duration)
    {
      // Reset all playing loops to the beginning
      for (int j = 0; j < 128; j++)
      {
        struct memory_loop *reset_loop = &data->memory_loops[j];
        if (reset_loop->is_playing)
        {
          reset_loop->playback_position = 0;
        }
      }

      // Allow new recordings to start
      data->waiting_for_pulse_reset = false;
      pw_log_info("SYNC mode: All loops reset to beginning, new recordings allowed");

      // Handle pending stops first (recordings that should end at pulse boundary)
      stop_sync_pending_recordings_on_pulse_reset(data);

      // Then start any pending recordings
      start_sync_pending_recordings_on_pulse_reset(data);

      // Finally start any pending playback
      start_sync_pending_playback_on_pulse_reset(data);
      break;
    }
  }
}

uint32_t get_longest_loop_duration(struct data *data)
{
  uint32_t longest = 0;
  for (int i = 0; i < 128; i++)
  {
    struct memory_loop *loop = &data->memory_loops[i];
    if (loop->recorded_frames > longest)
    {
      longest = loop->recorded_frames;
    }
  }
  return longest;
}

/* Check for pending recordings and start them if sync conditions are met */
void check_sync_pending_recordings(struct data *data)
{
  if (!data->sync_mode_enabled || data->pulse_loop_note == 255)
  {
    return;
  }

  // This function just checks status - it doesn't start recordings
  // Recordings are only started by start_sync_pending_recordings_on_pulse_reset()

  // Check if pulse loop is actually playing
  struct memory_loop *pulse_loop = get_loop_by_note(data, data->pulse_loop_note);
  if (!pulse_loop || !pulse_loop->is_playing)
  {
    pw_log_debug("SYNC check: Pulse loop (note %d) not playing - cannot sync",
                 data->pulse_loop_note);
    return;
  }

  // Count pending recordings
  int pending_count = 0;
  for (int i = 0; i < 128; i++)
  {
    struct memory_loop *loop = &data->memory_loops[i];
    if (loop->pending_record && loop->current_state == LOOP_STATE_IDLE)
    {
      pending_count++;
    }
  }

  if (pending_count > 0)
  {
    pw_log_debug("SYNC check: %d recordings pending for next pulse reset", pending_count);
  }
}

void start_sync_pending_recordings_on_pulse_reset(struct data *data)
{
  if (!data->sync_mode_enabled || data->pulse_loop_note == 255)
  {
    return;
  }

  // Only start one pending recording at a time
  if (data->currently_recording_note != 255)
  {
    pw_log_debug("SYNC pulse reset: Currently recording note %d - cannot start new recording",
                 data->currently_recording_note);
    return;
  }

  // This function is only called when pulse loop resets, so we know it's playing
  pw_log_debug("SYNC pulse reset detected: checking for pending recordings");

  // Find the first pending recording and start it
  for (int i = 0; i < 128; i++)
  {
    struct memory_loop *loop = &data->memory_loops[i];
    if (loop->pending_record && loop->current_state == LOOP_STATE_IDLE)
    {
      pw_log_info("SYNC PULSE RESET: Starting sync'd recording for note %d", i);

      // Generate timestamp-based filename
      time_t now;
      time(&now);
      struct tm *tm_info = localtime(&now);
      snprintf(loop->loop_filename, sizeof(loop->loop_filename),
               "loop_note_%03d_%04d-%02d-%02d_%02d-%02d-%02d.wav",
               i,
               tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
               tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);

      start_loop_recording_rt(data, i, loop->loop_filename);
      loop->current_state = LOOP_STATE_RECORDING;
      loop->pending_record = false;
      data->currently_recording_note = i;
      data->active_loop_count++;

      // Only start one recording per sync cycle
      pw_log_info("SYNC PULSE RESET: Started recording for note %d, now recording note %d", i, data->currently_recording_note);
      break;
    }
    else if (loop->pending_record)
    {
      pw_log_debug("SYNC pulse reset: Note %d pending but state=%d (not IDLE)", i, loop->current_state);
    }
  }
}

void stop_sync_pending_recordings_on_pulse_reset(struct data *data)
{
  if (!data->sync_mode_enabled || data->pulse_loop_note == 255)
  {
    return;
  }

  pw_log_debug("SYNC pulse reset detected: checking for pending stops");

  // Stop all recordings that are marked as pending stop
  for (int i = 0; i < 128; i++)
  {
    struct memory_loop *loop = &data->memory_loops[i];
    if (loop->pending_stop && loop->current_state == LOOP_STATE_RECORDING)
    {
      pw_log_info("SYNC PULSE RESET: Stopping sync'd recording for note %d (extending to pulse boundary)", i);

      // Calculate the target duration (multiple of pulse loop duration)
      uint32_t target_duration = loop->recorded_frames;
      if (data->pulse_loop_duration > 0)
      {
        // Round up to next multiple of pulse duration
        uint32_t multiple = (loop->recorded_frames + data->pulse_loop_duration - 1) / data->pulse_loop_duration;
        if (multiple == 0)
          multiple = 1;
        target_duration = multiple * data->pulse_loop_duration;
        pw_log_info("SYNC mode: Extending recording to %u frames (%ux pulse loop)", target_duration, multiple);
      }

      // Stop the recording
      stop_loop_recording_rt(data, i);
      usleep(1000); // 1ms

      // Set the final duration to be a multiple of pulse duration
      loop->recorded_frames = target_duration;

      loop->current_state = LOOP_STATE_PLAYING;
      loop->is_playing = true;
      loop->pending_stop = false;

      // Clear the currently recording note if this was it
      if (data->currently_recording_note == i)
      {
        data->currently_recording_note = 255;
      }

      pw_log_info("SYNC PULSE RESET: Recording for note %d stopped and set to %u frames, now playing", i, target_duration);
    }
    else if (loop->pending_stop)
    {
      pw_log_debug("SYNC pulse reset: Note %d pending stop but state=%d (not RECORDING)", i, loop->current_state);
    }
  }
}

void start_sync_pending_playback_on_pulse_reset(struct data *data)
{
  if (!data->sync_mode_enabled)
    return;

  pw_log_debug("SYNC pulse reset detected: checking for pending playback starts");

  // Start any loops that were waiting for the next pulse cycle
  for (int i = 0; i < 128; i++)
  {
    struct memory_loop *loop = &data->memory_loops[i];

    if (loop->pending_start && loop->current_state == LOOP_STATE_PLAYING && loop->loop_ready)
    {
      // Start playing the loop
      loop->is_playing = true;
      loop->pending_start = false;
      loop->playback_position = 0; // Start from beginning

      pw_log_info("SYNC PULSE RESET: Starting pending playback for loop %d", i);
    }
    else if (loop->pending_start)
    {
      pw_log_debug("SYNC pulse reset: Note %d pending start but state=%d or loop_ready=%s",
                   i, loop->current_state, loop->loop_ready ? "true" : "false");
    }
  }
}
