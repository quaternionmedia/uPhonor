#ifndef AUDIO_PROCESSING_RT_H
#define AUDIO_PROCESSING_RT_H

#include "uphonor.h"

/* RT-optimized audio processing functions */
void handle_audio_input_rt(struct data *data, uint32_t n_samples);
void process_audio_output_rt(struct data *data, struct spa_io_position *position);

/* RT-safe utility functions */
float calculate_rms_rt(const float *buffer, uint32_t n_samples);
void apply_volume_rt(float *buf, uint32_t frames, float volume);
sf_count_t read_audio_frames_rt(struct data *data, float *buf, uint32_t n_samples);
sf_count_t read_audio_frames_variable_speed_rt(struct data *data, float *buf, uint32_t n_samples);
uint32_t read_audio_frames_rubberband_rt(struct data *data, float *buf, uint32_t n_samples);

/* Optimized buffered audio reading functions */
sf_count_t read_audio_frames_buffered_rt(struct data *data, float *buf, uint32_t n_samples);
sf_count_t read_audio_frames_variable_speed_buffered_rt(struct data *data, float *buf, uint32_t n_samples);

/* Recording control functions (RT-safe) */
int start_recording_rt(struct data *data, const char *filename);
int stop_recording_rt(struct data *data);

/* In-memory loop recording functions (RT-safe) - Multi-loop support */
int start_loop_recording_rt(struct data *data, uint8_t midi_note, const char *filename);
int stop_loop_recording_rt(struct data *data, uint8_t midi_note);
bool store_audio_in_memory_loop_rt(struct data *data, uint8_t midi_note, const float *input, uint32_t n_samples);

/* Multi-loop mixing functions */
sf_count_t mix_all_active_loops_rt(struct data *data, float *buf, uint32_t n_samples);
sf_count_t read_audio_frames_from_memory_loop_basic_rt(struct memory_loop *loop, float *buf, uint32_t n_samples);
void reset_memory_loop_playback_rt(struct data *data, uint8_t midi_note);

#endif /* AUDIO_PROCESSING_RT_H */
