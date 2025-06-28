#include "buffer_manager.h"

int initialize_audio_buffers(struct audio_buffers *buffers, uint32_t required_size)
{
  // Initialize buffers on first call or when size changes
  if (buffers->buffer_size < required_size)
  {
    buffers->buffer_size = required_size * 2; // Extra space for future growth

    buffers->silence_buffer = realloc(buffers->silence_buffer,
                                      buffers->buffer_size * sizeof(float));
    buffers->temp_buffer = realloc(buffers->temp_buffer,
                                   buffers->buffer_size * sizeof(float));

    if (!buffers->silence_buffer || !buffers->temp_buffer)
    {
      pw_log_error("Failed to allocate audio buffers");
      return -1;
    }

    // Pre-fill silence buffer once
    memset(buffers->silence_buffer, 0, buffers->buffer_size * sizeof(float));
  }
  return 0;
}

void cleanup_audio_buffers(struct audio_buffers *buffers)
{
  if (buffers->silence_buffer)
  {
    free(buffers->silence_buffer);
    buffers->silence_buffer = NULL;
  }
  if (buffers->temp_buffer)
  {
    free(buffers->temp_buffer);
    buffers->temp_buffer = NULL;
  }
  buffers->buffer_size = 0;
}

float calculate_rms(float *buffer, uint32_t n_samples)
{
  if (!buffer || n_samples == 0)
    return 0.0f;

  float rms = 0.0f;
  for (uint32_t i = 0; i < n_samples; i++)
  {
    rms += buffer[i] * buffer[i];
  }
  return sqrtf(rms / n_samples);
}
