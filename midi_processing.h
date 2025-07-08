#ifndef MIDI_PROCESSING_H
#define MIDI_PROCESSING_H

#include "uphonor.h"

/* MIDI processing function declarations */
void process_midi_input(struct data *data, struct spa_io_position *position);
void process_midi_output(struct data *data, struct spa_io_position *position);
void handle_midi_message(struct data *data, uint8_t *midi_data);
void handle_note_on(struct data *data, uint8_t channel, uint8_t note, uint8_t velocity);
void handle_note_off(struct data *data, uint8_t channel, uint8_t note, uint8_t velocity);
void handle_control_change(struct data *data, uint8_t channel, uint8_t controller, uint8_t value);

/* Pulse timeline functions */
uint32_t get_theoretical_pulse_position(struct data *data);
void update_pulse_timeline(struct data *data, uint64_t current_frame);
void check_theoretical_pulse_reset(struct data *data);

/* MIDI utility functions */
void parse_midi_sequence(struct data *data, struct spa_pod_sequence *seq);

#endif /* MIDI_PROCESSING_H */
