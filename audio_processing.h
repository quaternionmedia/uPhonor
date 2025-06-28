#ifndef AUDIO_PROCESSING_H
#define AUDIO_PROCESSING_H

#include "uphonor.h"

/* Audio processing function declarations */
void process_audio_output(struct data *data, struct spa_io_position *position);
void handle_audio_input(struct data *data, uint32_t n_samples);
void apply_volume(float *buf, uint32_t frames, float volume);
void handle_end_of_file(struct data *data, float *buf, sf_count_t frames_read,
                        uint32_t n_samples, float *temp_buffer);

/* Audio file operations */
sf_count_t read_audio_frames(struct data *data, float *buf, uint32_t n_samples,
                             float *temp_buffer);

/* Recording and playback functions */
int start_recording(struct data *data, const char *filename);
int stop_recording(struct data *data);
int start_playing(struct data *data, const char *filename);

#endif /* AUDIO_PROCESSING_H */
