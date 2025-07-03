#include "uphonor.h"
#include "audio_processing.h"
#include "audio_processing_rt.h"
#include "midi_processing.h"
#include "loop_manager.h"

void on_process(void *userdata, struct spa_io_position *position)
{
  struct data *data = userdata;
  uint32_t n_samples = position->clock.duration;

  // Process MIDI input and output (always needed for control)
  process_midi_input(data, position);

  // Handle audio processing based on whether we have loop manager
  if (data->loop_mgr) {
    // Multi-loop processing
    handle_multiple_loops_audio_input(data, n_samples);
    process_multiple_loops_audio_output(data, position);
  } else {
    // Fallback to single loop processing
    handle_audio_input_rt(data, n_samples);
    process_audio_output_rt(data, position);
  }
}
