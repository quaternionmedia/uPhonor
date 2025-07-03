#include "audio_buffer_rt.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

int audio_buffer_rt_init(struct audio_buffer_rt *ab, uint32_t channels)
{
  ab->size = AUDIO_BUFFER_SIZE;
  ab->buffer = malloc(ab->size * sizeof(float));
  if (!ab->buffer)
    return -1;
    
  ab->temp_size = ab->size * channels;
  ab->temp_multichannel = malloc(ab->temp_size * sizeof(float));
  if (!ab->temp_multichannel)
  {
    free(ab->buffer);
    return -1;
  }
  
  ab->valid_samples = 0;
  ab->read_position = 0;
  ab->file_position = 0;
  ab->loop_mode = true;
  
  return 0;
}

void audio_buffer_rt_cleanup(struct audio_buffer_rt *ab)
{
  if (ab->buffer)
  {
    free(ab->buffer);
    ab->buffer = NULL;
  }
  if (ab->temp_multichannel)
  {
    free(ab->temp_multichannel);
    ab->temp_multichannel = NULL;
  }
}

int audio_buffer_rt_fill(struct audio_buffer_rt *ab, SNDFILE *file, SF_INFO *fileinfo)
{
  if (!ab->buffer || !file)
    return -1;
    
  /* Seek to the current file position */
  sf_seek(file, ab->file_position, SEEK_SET);
  
  sf_count_t frames_read;
  
  if (fileinfo->channels == 1)
  {
    /* Direct read for mono files */
    frames_read = sf_readf_float(file, ab->buffer, ab->size);
  }
  else
  {
    /* Multi-channel read with first channel extraction */
    frames_read = sf_readf_float(file, ab->temp_multichannel, ab->size);
    
    /* Extract first channel */
    for (sf_count_t i = 0; i < frames_read; i++)
    {
      ab->buffer[i] = ab->temp_multichannel[i * fileinfo->channels];
    }
  }
  
  ab->valid_samples = (uint32_t)frames_read;
  ab->read_position = 0;
  ab->file_position += frames_read;
  
  /* Handle end of file */
  if (frames_read < ab->size && ab->loop_mode)
  {
    /* We hit end of file, reset for looping */
    ab->file_position = 0;
  }
  
  return (int)frames_read;
}

sf_count_t audio_buffer_rt_read(struct audio_buffer_rt *ab, SNDFILE *file, SF_INFO *fileinfo,
                                float *output, uint32_t n_samples)
{
  uint32_t samples_copied = 0;
  
  while (samples_copied < n_samples)
  {
    /* Check if we need more data in buffer */
    if (ab->read_position >= ab->valid_samples)
    {
      /* Buffer is empty, try to refill */
      int filled = audio_buffer_rt_fill(ab, file, fileinfo);
      if (filled <= 0)
      {
        /* No more data available, fill with silence */
        memset(&output[samples_copied], 0, (n_samples - samples_copied) * sizeof(float));
        break;
      }
    }
    
    /* Copy available samples from buffer */
    uint32_t available = ab->valid_samples - ab->read_position;
    uint32_t to_copy = n_samples - samples_copied;
    if (to_copy > available)
      to_copy = available;
      
    memcpy(&output[samples_copied], &ab->buffer[ab->read_position], to_copy * sizeof(float));
    
    ab->read_position += to_copy;
    samples_copied += to_copy;
  }
  
  return samples_copied;
}

void audio_buffer_rt_reset(struct audio_buffer_rt *ab)
{
  ab->read_position = 0;
  ab->file_position = 0;
  ab->valid_samples = 0;
}

bool audio_buffer_rt_needs_refill(const struct audio_buffer_rt *ab)
{
  /* Consider buffer needing refill when less than 25% remains */
  return (ab->read_position >= (ab->valid_samples * 3) / 4);
}
