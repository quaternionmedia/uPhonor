#include "uphonor.h"
#include "audio_processing.h"
#include "audio_processing_rt.h"
#include "midi_processing.h"

void on_process(void *userdata, struct spa_io_position *position)
{
  struct data *data = userdata;
  uint32_t n_samples = position->clock.duration;

  // Update pulse timeline for sync mode
  if (data->sync_mode_enabled && data->pulse_loop_duration > 0)
  {
    update_pulse_timeline(data, position->clock.position);
    check_theoretical_pulse_reset(data);
  }

  // Process MIDI input and output (always needed for control)
  process_midi_input(data, position);

  // Check for sync mode playback reset
  check_sync_playback_reset(data);

  // Handle audio input (recording) - RT-optimized
  // Only process if recording is enabled or if we need to monitor levels
  handle_audio_input_rt(data, n_samples);

  // Process audio output (playback) - RT-optimized
  // Only process if we're in playing state
  process_audio_output_rt(data, position);
}
