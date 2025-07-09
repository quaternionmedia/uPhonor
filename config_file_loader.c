#include "config.h"
#include <stdio.h>
#include <string.h>
#include <sndfile.h>
#include <unistd.h>

/**
 * Load an audio file into a memory loop buffer
 * Returns true on success, false on failure
 */
bool load_audio_file_into_loop(struct memory_loop *loop, const char *filename, uint32_t sample_rate)
{
  if (!loop || !filename || !loop->buffer)
  {
    return false;
  }

  /* Check if file exists */
  if (access(filename, R_OK) != 0)
  {
    printf("Audio file not found: %s\n", filename);
    return false;
  }

  /* Open audio file */
  SF_INFO fileinfo;
  memset(&fileinfo, 0, sizeof(fileinfo));

  SNDFILE *file = sf_open(filename, SFM_READ, &fileinfo);
  if (!file)
  {
    printf("Failed to open audio file: %s - %s\n", filename, sf_strerror(NULL));
    return false;
  }

  /* Validate sample rate compatibility */
  if (fileinfo.samplerate != (int)sample_rate)
  {
    printf("Warning: Audio file sample rate (%d Hz) differs from system (%d Hz)\n",
           fileinfo.samplerate, sample_rate);
    /* Continue anyway - we'll load what we can */
  }

  /* Calculate how many frames we can load */
  uint32_t frames_to_load = (uint32_t)fileinfo.frames;
  if (frames_to_load > loop->buffer_size)
  {
    frames_to_load = loop->buffer_size;
    printf("Warning: Audio file too large, truncating to %u frames\n", frames_to_load);
  }

  /* Load audio data */
  sf_count_t frames_read;
  if (fileinfo.channels == 1)
  {
    /* Direct read for mono files */
    frames_read = sf_readf_float(file, loop->buffer, frames_to_load);
  }
  else
  {
    /* Multi-channel file - extract first channel only */
    float *temp_buffer = malloc(frames_to_load * fileinfo.channels * sizeof(float));
    if (!temp_buffer)
    {
      sf_close(file);
      printf("Failed to allocate temporary buffer for multi-channel file\n");
      return false;
    }

    frames_read = sf_readf_float(file, temp_buffer, frames_to_load);

    /* Extract first channel */
    for (sf_count_t i = 0; i < frames_read; i++)
    {
      loop->buffer[i] = temp_buffer[i * fileinfo.channels];
    }

    free(temp_buffer);
  }

  sf_close(file);

  if (frames_read <= 0)
  {
    printf("Failed to read audio data from file: %s\n", filename);
    return false;
  }

  /* Update loop state */
  loop->recorded_frames = (uint32_t)frames_read;
  loop->playback_position = 0;
  loop->loop_ready = true;
  loop->recording_to_memory = false;
  loop->current_state = LOOP_STATE_IDLE; /* Start idle, user can trigger playback */

  printf("Loaded audio file: %s (%u frames, %.2f seconds)\n",
         filename, loop->recorded_frames,
         (float)loop->recorded_frames / sample_rate);

  return true;
}

/**
 * Load all audio files referenced in the configuration
 * This should be called after config_load_state to actually load the audio data
 */
int config_load_audio_files(struct data *data)
{
  if (!data)
    return -1;

  int files_loaded = 0;
  int files_failed = 0;

  printf("Loading audio files for configured loops...\n");

  for (int i = 0; i < 128; i++)
  {
    struct memory_loop *loop = &data->memory_loops[i];

    /* Skip loops that have no filename */
    if (strlen(loop->loop_filename) == 0)
      continue;

    /* Try to load from recordings directory first */
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "recordings/%s", loop->loop_filename);

    if (load_audio_file_into_loop(loop, full_path, loop->sample_rate))
    {
      files_loaded++;
    }
    else
    {
      /* Try loading from current directory */
      if (load_audio_file_into_loop(loop, loop->loop_filename, loop->sample_rate))
      {
        files_loaded++;
      }
      else
      {
        files_failed++;
        printf("Failed to load audio for loop %d: %s\n", i, loop->loop_filename);
      }
    }
  }

  printf("Audio loading complete: %d files loaded, %d failed\n", files_loaded, files_failed);

  return files_loaded;
}
