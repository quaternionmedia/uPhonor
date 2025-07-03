#include "loop_manager.h"
#include "uphonor.h"
#include "audio_processing_rt.h"
#include <string.h>
#include <math.h>
#include <pipewire/pipewire.h>
#include <rubberband/rubberband-c.h>

// Forward declaration for rubberband processing function
uint32_t apply_rubberband_to_buffer(struct data *data, float *buffer, uint32_t n_samples);
void process_multiple_loops_audio_output(struct data *data, struct spa_io_position *position)
{
  if (!data || !data->loop_mgr || !position)
    return;

  struct pw_buffer *b;
  uint32_t n_samples = position->clock.duration;
  float *output_buf;

  /* Get output buffer */
  if ((b = pw_filter_dequeue_buffer(data->audio_out)) == NULL)
  {
    return;
  }

  if (b->buffer->n_datas < 1)
  {
    pw_filter_queue_buffer(data->audio_out, b);
    return;
  }

  struct spa_data *d = &b->buffer->datas[0];
  uint32_t stride = sizeof(float);
  output_buf = (float *)d->data;

  if (!output_buf)
  {
    pw_filter_queue_buffer(data->audio_out, b);
    return;
  }

  /* Clear output buffer */
  memset(output_buf, 0, n_samples * stride);

  /* Mix all active playing loops */
  int active_loops = 0;
  for (int i = 0; i < MAX_LOOPS; i++)
  {
    struct loop_slot *loop = &data->loop_mgr->loops[i];

    if (!loop->active || loop->state != HOLO_STATE_PLAYING || !loop->file)
    {
      continue;
    }

    /* Reset audio if needed for this loop */
    if (loop->reset_audio)
    {
      sf_seek(loop->file, 0, SEEK_SET);
      loop->sample_position = 0.0;
      loop->reset_audio = false;
      audio_buffer_rt_reset(&loop->audio_buffer);
      audio_buffer_rt_fill(&loop->audio_buffer, loop->file, &loop->fileinfo);
    }

    /* Read audio from this loop */
    float temp_buf[n_samples];
    sf_count_t frames_read;

    if (data->rubberband_enabled)
    {
      /* When rubberband is enabled, read normally and apply rubberband to mixed output */
      frames_read = audio_buffer_rt_read(&loop->audio_buffer,
                                         loop->file,
                                         &loop->fileinfo,
                                         temp_buf,
                                         n_samples);
    }
    else
    {
      /* When rubberband is disabled (record player mode), apply speed directly to this loop */
      frames_read = read_loop_with_variable_speed(loop, temp_buf, n_samples, data->playback_speed);
    }

    /* Apply volume and mix into output */
    float loop_volume = loop->volume * data->loop_mgr->master_volume;
    for (uint32_t j = 0; j < frames_read; j++)
    {
      output_buf[j] += temp_buf[j] * loop_volume;
    }

    active_loops++;
  }

  /* Normalize output if multiple loops are playing */
  if (active_loops > 1)
  {
    float norm_factor = 1.0f / sqrtf((float)active_loops);
    for (uint32_t i = 0; i < n_samples; i++)
    {
      output_buf[i] *= norm_factor;
    }
  }

  /* Apply rubberband processing to the mixed output if enabled */
  uint32_t final_samples = n_samples;
  if (data->rubberband_enabled && data->rubberband_state && active_loops > 0)
  {
    final_samples = apply_rubberband_to_buffer(data, output_buf, n_samples);
  }

  /* Apply master volume */
  if (data->volume != 1.0f)
  {
    for (uint32_t i = 0; i < final_samples; i++)
    {
      output_buf[i] *= data->volume;
    }
  }

  /* Set buffer metadata */
  b->buffer->datas[0].chunk->offset = 0;
  b->buffer->datas[0].chunk->stride = stride;
  b->buffer->datas[0].chunk->size = final_samples * stride;

  pw_filter_queue_buffer(data->audio_out, b);
}

void handle_multiple_loops_audio_input(struct data *data, uint32_t n_samples)
{
  if (!data || !data->loop_mgr)
    return;

  /* Get input buffer */
  float *input_buf = pw_filter_get_dsp_buffer(data->audio_in, n_samples);

  /* Handle recording for all loops that are in recording state */
  for (int i = 0; i < MAX_LOOPS; i++)
  {
    struct loop_slot *loop = &data->loop_mgr->loops[i];

    if (!loop->active || loop->state != HOLO_STATE_RECORDING || !loop->record_file)
    {
      continue;
    }

    /* Write input to this loop's recording file */
    sf_count_t frames_written;
    if (input_buf == NULL)
    {
      /* Use silence if no input */
      frames_written = sf_writef_float(loop->record_file,
                                       data->silence_buffer,
                                       n_samples);
    }
    else
    {
      /* Write input data */
      frames_written = sf_writef_float(loop->record_file, input_buf, n_samples);
    }

    if (frames_written != n_samples)
    {
      pw_log_warn("Could not write all frames for loop %d: wrote %ld of %d",
                  i, frames_written, n_samples);
    }

    /* Sync to disk periodically */
    static int sync_counter = 0;
    if (++sync_counter >= 500)
    { /* ~10 seconds at 48kHz/1024 */
      sf_write_sync(loop->record_file);
      sync_counter = 0;
    }
  }
}

uint32_t apply_rubberband_to_buffer(struct data *data, float *buffer, uint32_t n_samples)
{
  if (!data->rubberband_enabled || !data->rubberband_state || n_samples == 0)
  {
    return n_samples; /* No processing, return original sample count */
  }

  /* Check if we need any processing at all */
  bool needs_processing = (fabsf(data->playback_speed - 1.0f) > 0.001f) ||
                          (fabsf(data->pitch_shift) > 0.01f);

  if (!needs_processing)
  {
    /* No processing needed - return original audio unchanged */
    return n_samples;
  }

  /* Update rubberband parameters only when they actually change significantly */
  static float last_speed = 1.0f;
  static float last_pitch = 0.0f;
  static bool rubberband_initialized = false;
  static float rubberband_buffer[512] = {0}; /* Smaller buffer for lower latency */
  static uint32_t buffer_fill = 0; /* How much is in our buffer */

  bool speed_changed = fabsf(data->playback_speed - last_speed) > 0.01f;
  bool pitch_changed = fabsf(data->pitch_shift - last_pitch) > 0.1f;
  bool major_speed_change = fabsf(data->playback_speed - last_speed) > 0.5f;
  bool major_pitch_change = fabsf(data->pitch_shift - last_pitch) > 5.0f;

  /* Reset if this is the first time or if parameters changed dramatically */
  if (!rubberband_initialized || major_speed_change || major_pitch_change)
  {
    rubberband_reset(data->rubberband_state);
    rubberband_initialized = true;
    buffer_fill = 0; /* Clear our buffer too */
    pw_log_debug("Rubberband: Reset due to major parameter change");
  }

  if (speed_changed)
  {
    /* Set time ratio directly - higher values = faster playback */
    double time_ratio = data->playback_speed;
    rubberband_set_time_ratio(data->rubberband_state, time_ratio);
    pw_log_debug("Rubberband: Set time ratio to %.2f (speed %.2fx)", time_ratio, data->playback_speed);
    
    /* Force immediate processing by clearing buffer on parameter changes */
    buffer_fill = 0;
  }

  if (pitch_changed)
  {
    float pitch_scale = powf(2.0f, data->pitch_shift / 12.0f);
    rubberband_set_pitch_scale(data->rubberband_state, pitch_scale);
    pw_log_debug("Rubberband: Set pitch shift to %.1f semitones", data->pitch_shift);
    
    /* Force immediate processing by clearing buffer on parameter changes */
    buffer_fill = 0;
  }

  /* Update our tracking variables after applying changes */
  last_speed = data->playback_speed;
  last_pitch = data->pitch_shift;

  /* Process the input */
  const float *input_ptr = buffer;
  rubberband_process(data->rubberband_state, &input_ptr, n_samples, 0);

  /* Try to get output and add it to our buffer */
  int available = rubberband_available(data->rubberband_state);
  if (available > 0)
  {
    uint32_t can_store = 512 - buffer_fill; /* How much room we have */
    uint32_t to_retrieve = (uint32_t)available;
    if (to_retrieve > can_store)
    {
      to_retrieve = can_store;
    }
    if (to_retrieve > data->rubberband_buffer_size)
    {
      to_retrieve = data->rubberband_buffer_size;
    }

    if (to_retrieve > 0)
    {
      float *output_ptr = data->rubberband_output_buffer;
      size_t retrieved = rubberband_retrieve(data->rubberband_state, &output_ptr, to_retrieve);
      
      if (retrieved > 0)
      {
        /* Add to our accumulation buffer */
        memcpy(&rubberband_buffer[buffer_fill], data->rubberband_output_buffer, 
               retrieved * sizeof(float));
        buffer_fill += (uint32_t)retrieved;
      }
    }
  }

  /* Now provide output from our buffer - be more aggressive about outputting */
  if (buffer_fill >= n_samples)
  {
    /* We have enough - copy what we need */
    memcpy(buffer, rubberband_buffer, n_samples * sizeof(float));
    
    /* Shift the remaining data down */
    buffer_fill -= n_samples;
    if (buffer_fill > 0)
    {
      memmove(rubberband_buffer, &rubberband_buffer[n_samples], 
              buffer_fill * sizeof(float));
    }
    
    return n_samples;
  }
  else if (buffer_fill > 0)
  {
    /* We have some but not enough - use what we have and pad with silence */
    memcpy(buffer, rubberband_buffer, buffer_fill * sizeof(float));
    memset(&buffer[buffer_fill], 0, (n_samples - buffer_fill) * sizeof(float));
    buffer_fill = 0;
    
    return n_samples;
  }
  else
  {
    /* No output available yet - try to get some immediately */
    const float *input_ptr = buffer;
    rubberband_process(data->rubberband_state, &input_ptr, n_samples, 0);
    
    /* Try again to get output */
    available = rubberband_available(data->rubberband_state);
    if (available > 0)
    {
      uint32_t to_get = (available > n_samples) ? n_samples : (uint32_t)available;
      if (to_get > data->rubberband_buffer_size)
      {
        to_get = data->rubberband_buffer_size;
      }
      
      float *output_ptr = data->rubberband_output_buffer;
      size_t retrieved = rubberband_retrieve(data->rubberband_state, &output_ptr, to_get);
      
      if (retrieved > 0)
      {
        memcpy(buffer, data->rubberband_output_buffer, retrieved * sizeof(float));
        if (retrieved < n_samples)
        {
          memset(&buffer[retrieved], 0, (n_samples - retrieved) * sizeof(float));
        }
        return n_samples;
      }
    }
    
    /* Still no output - return silence */
    memset(buffer, 0, n_samples * sizeof(float));
    return n_samples;
  }
}

/* Simplified variable speed reading for individual loops */
sf_count_t read_loop_with_variable_speed(struct loop_slot *loop, float *buf, uint32_t n_samples, float playback_speed)
{
  if (!loop || !loop->file || playback_speed <= 0.0f)
  {
    /* Fill with silence if invalid */
    memset(buf, 0, n_samples * sizeof(float));
    return n_samples;
  }

  if (playback_speed == 1.0f)
  {
    /* Normal speed - use regular buffered reading */
    return audio_buffer_rt_read(&loop->audio_buffer,
                                loop->file,
                                &loop->fileinfo,
                                buf,
                                n_samples);
  }

  /* For variable speed, read more or fewer samples and resample */
  uint32_t samples_needed = (uint32_t)(n_samples / playback_speed) + 1;
  if (samples_needed > 4096)
    samples_needed = 4096; /* Limit buffer size */

  static float temp_buffer[4096];
  sf_count_t frames_read = audio_buffer_rt_read(&loop->audio_buffer,
                                                loop->file,
                                                &loop->fileinfo,
                                                temp_buffer,
                                                samples_needed);

  if (frames_read == 0)
  {
    memset(buf, 0, n_samples * sizeof(float));
    return n_samples;
  }

  /* Simple resampling using linear interpolation */
  for (uint32_t i = 0; i < n_samples; i++)
  {
    float src_index = i / playback_speed;
    uint32_t src_int = (uint32_t)src_index;
    float src_frac = src_index - src_int;

    if (src_int < frames_read - 1)
    {
      /* Linear interpolation */
      buf[i] = temp_buffer[src_int] + src_frac * (temp_buffer[src_int + 1] - temp_buffer[src_int]);
    }
    else if (src_int < frames_read)
    {
      /* Use last sample */
      buf[i] = temp_buffer[src_int];
    }
    else
    {
      /* Silence */
      buf[i] = 0.0f;
    }
  }

  return n_samples;
}