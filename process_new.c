#include "uphonor.h"
#include "audio_processing.h"
#include "midi_processing.h"

void on_process(void *userdata, struct spa_io_position *position)
{
  struct data *data = userdata;
  uint32_t n_samples = position->clock.duration;

  // Process MIDI input and output
  process_midi_input(data, position);

  // Handle audio input (recording)
  handle_audio_input(data, n_samples);

  // Process audio output (playback)
  process_audio_output(data, position);
}
