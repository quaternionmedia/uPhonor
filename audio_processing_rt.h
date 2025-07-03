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

/* In-memory loop recording functions (RT-safe) */
int start_loop_recording_rt(struct data *data, const char *filename);
int stop_loop_recording_rt(struct data *data);
sf_count_t read_audio_frames_from_memory_loop_rt(struct data *data, float *buf, uint32_t n_samples);
sf_count_t read_audio_frames_from_memory_loop_variable_speed_rt(struct data *data, float *buf, uint32_t n_samples);
uint32_t read_audio_frames_memory_loop_rubberband_rt(struct data *data, float *buf, uint32_t n_samples);
bool store_audio_in_memory_loop_rt(struct data *data, const float *input, uint32_t n_samples);

/* Memory loop management */
int init_memory_loop(struct data *data, uint32_t max_seconds, uint32_t sample_rate);
void cleanup_memory_loop(struct data *data);
void reset_memory_loop_playback_rt(struct data *data);

#endif /* AUDIO_PROCESSING_RT_H */
