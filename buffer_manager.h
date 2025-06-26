#ifndef BUFFER_MANAGER_H
#define BUFFER_MANAGER_H

#include "uphonor.h"

/* Buffer management structure */
struct audio_buffers
{
  float *silence_buffer;
  float *temp_buffer;
  uint32_t buffer_size;
  int sync_counter;
  int rms_skip_counter;
};

/* Buffer management function declarations */
int initialize_audio_buffers(struct audio_buffers *buffers, uint32_t required_size);
void cleanup_audio_buffers(struct audio_buffers *buffers);
float calculate_rms(float *buffer, uint32_t n_samples);

#endif /* BUFFER_MANAGER_H */
