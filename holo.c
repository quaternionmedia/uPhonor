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
  if (midi_note > 127) {
    pw_log_warn("Invalid MIDI note: %d", midi_note);
    return NULL;
  }
  return &data->memory_loops[midi_note];
}

// Stop all recordings (emergency function)
void stop_all_recordings(struct data *data)
{
  for (int i = 0; i < 128; i++) {
    struct memory_loop *loop = &data->memory_loops[i];
    if (loop->current_state == LOOP_STATE_RECORDING) {
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
  for (int i = 0; i < 128; i++) {
    struct memory_loop *loop = &data->memory_loops[i];
    if (loop->is_playing) {
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
  data->currently_recording_note = 255; // 255 = no note recording
  
  for (int i = 0; i < 128; i++) {
    struct memory_loop *loop = &data->memory_loops[i];
    
    // Initialize the loop structure
    memset(loop, 0, sizeof(struct memory_loop));
    loop->midi_note = i;
    loop->current_state = LOOP_STATE_IDLE;
    loop->sample_rate = sample_rate;
    loop->volume = 1.0f; // Default volume
    
    // Allocate buffer (60 seconds worth of audio)
    loop->buffer_size = max_seconds * sample_rate; // frames (mono)
    loop->buffer = calloc(loop->buffer_size, sizeof(float));
    if (!loop->buffer) {
      pw_log_error("Failed to allocate memory loop buffer for note %d", i);
      // Clean up previously allocated loops
      for (int j = 0; j < i; j++) {
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
  
  for (int i = 0; i < 128; i++) {
    struct memory_loop *loop = &data->memory_loops[i];
    if (loop->buffer) {
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
  if (midi_note > 127) {
    pw_log_warn("Invalid MIDI note: %d, ignoring", midi_note);
    return;
  }
  
  // Ensure volume is within valid range
  if (volume < 0.0f || volume > 1.0f) {
    pw_log_warn("Invalid volume level: %.2f, clamping to [0.0, 1.0]", volume);
    volume = (volume < 0.0f) ? 0.0f : 1.0f;
  }

  struct memory_loop *loop = get_loop_by_note(data, midi_note);
  if (!loop) {
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
    // If another loop is recording, stop it first
    if (data->currently_recording_note != 255 && data->currently_recording_note != midi_note) {
      struct memory_loop *recording_loop = get_loop_by_note(data, data->currently_recording_note);
      if (recording_loop && recording_loop->current_state == LOOP_STATE_RECORDING) {
        pw_log_info("Stopping recording for note %d to start recording note %d", 
                   data->currently_recording_note, midi_note);
        stop_loop_recording_rt(data, data->currently_recording_note);
        recording_loop->current_state = LOOP_STATE_PLAYING;
        recording_loop->is_playing = true;
      }
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
    pw_log_info("Stopping memory loop recording for note %d", midi_note);
    stop_loop_recording_rt(data, midi_note);

    /* Give the system a moment to process the stop command */
    usleep(1000); // 1ms

    loop->current_state = LOOP_STATE_PLAYING;
    loop->is_playing = true;
    data->currently_recording_note = 255; // No longer recording
    pw_log_info("Starting playback from memory loop for note %d", midi_note);
    break;

  case LOOP_STATE_PLAYING:
    pw_log_info("Stopping playback for note %d", midi_note);
    loop->current_state = LOOP_STATE_STOPPED;
    loop->is_playing = false;
    break;

  case LOOP_STATE_STOPPED:
    pw_log_info("Restarting playback for note %d", midi_note);
    loop->current_state = LOOP_STATE_PLAYING;
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
  data->reset_audio = true;
}
