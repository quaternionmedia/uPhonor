#include "uphonor.h"
#include "audio_processing.h"
#include "audio_processing_rt.h"
#include "midi_processing.h"

void on_process(void *userdata, struct spa_io_position *position)
{
  struct data *data = userdata;
  uint32_t n_samples = position->clock.duration;

  // Process MIDI input and output
  process_midi_input(data, position);

  // Handle audio input (recording) - RT-optimized
  handle_audio_input_rt(data, n_samples);

  // Process audio output (playback) - RT-optimized
  process_audio_output_rt(data, position);
}
