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
  static float mini_buffer[256] = {0}; /* Very small buffer for smoothing */
  static uint32_t buffer_fill = 0;

  bool speed_changed = fabsf(data->playback_speed - last_speed) > 0.005f;    /* More sensitive */
  bool pitch_changed = fabsf(data->pitch_shift - last_pitch) > 0.05f;        /* More sensitive */
  bool major_speed_change = fabsf(data->playback_speed - last_speed) > 0.3f; /* Less aggressive reset */
  bool major_pitch_change = fabsf(data->pitch_shift - last_pitch) > 3.0f;    /* Less aggressive reset */

  /* Reset if this is the first time or if parameters changed dramatically */
  if (!rubberband_initialized || major_speed_change || major_pitch_change)
  {
    rubberband_reset(data->rubberband_state);
    rubberband_initialized = true;
    buffer_fill = 0; /* Clear mini buffer */
    pw_log_debug("Rubberband: Reset due to major parameter change");
  }

  if (speed_changed)
  {
    /* Set time ratio - rubberband uses inverse ratio: lower values = faster playback */
    double time_ratio = 1.0 / data->playback_speed;
    rubberband_set_time_ratio(data->rubberband_state, time_ratio);
    pw_log_debug("Rubberband: Set time ratio to %.2f (speed %.2fx)", time_ratio, data->playback_speed);
  }

  if (pitch_changed)
  {
    float pitch_scale = powf(2.0f, data->pitch_shift / 12.0f);
    rubberband_set_pitch_scale(data->rubberband_state, pitch_scale);
    pw_log_debug("Rubberband: Set pitch shift to %.1f semitones", data->pitch_shift);
  }

  /* Update our tracking variables after applying changes */
  last_speed = data->playback_speed;
  last_pitch = data->pitch_shift;

  /* Debug logging for buffer levels */
  static int debug_counter = 0;
  bool should_debug = (++debug_counter % 200 == 0); /* Log every ~4 seconds at 48kHz/1024 */

  /* Process input and accumulate output in mini buffer */
  const float *input_ptr = buffer;

  /* Feed input based on speed - balance input feeding for optimal rubberband processing */
  if (data->playback_speed < 0.5f)
  {
    /* Very slow speeds - feed more input as they consume more to produce less output */
    rubberband_process(data->rubberband_state, &input_ptr, n_samples, 0);
    rubberband_process(data->rubberband_state, &input_ptr, n_samples, 0);
  }
  else if (data->playback_speed < 0.8f)
  {
    /* Moderately slow speeds - feed extra input */
    rubberband_process(data->rubberband_state, &input_ptr, n_samples, 0);
    rubberband_process(data->rubberband_state, &input_ptr, n_samples, 0);
  }
  else if (data->playback_speed > 1.5f)
  {
    /* Fast speeds - single input feed as they produce more output per input */
    rubberband_process(data->rubberband_state, &input_ptr, n_samples, 0);
  }
  else
  {
    /* Normal speeds around 1x - standard single feed */
    rubberband_process(data->rubberband_state, &input_ptr, n_samples, 0);
  }

  /* Try to get output and add to mini buffer */
  int available = rubberband_available(data->rubberband_state);
  if (available > 0)
  {
    uint32_t space_left = 256 - buffer_fill;
    uint32_t to_retrieve = (uint32_t)available;
    if (to_retrieve > space_left)
    {
      to_retrieve = space_left;
    }
    if (to_retrieve > data->rubberband_buffer_size)
    {
      to_retrieve = data->rubberband_buffer_size;
    }

    if (to_retrieve > 0)
    {
      float *output_ptr = data->rubberband_output_buffer;
      size_t retrieved = rubberband_retrieve(data->rubberband_state, &output_ptr, to_retrieve);

      if (retrieved > 0 && buffer_fill + retrieved <= 256)
      {
        memcpy(&mini_buffer[buffer_fill], data->rubberband_output_buffer, retrieved * sizeof(float));
        buffer_fill += (uint32_t)retrieved;
      }
    }
  }

  /* For any speed, try multiple iterations if we don't have enough output, but be conservative */
  int max_tries = (data->playback_speed < 0.7f) ? 6 : 4; /* More tries for very slow speeds only */
  for (int tries = 0; tries < max_tries && buffer_fill < n_samples; tries++)
  {
    /* Feed input conservatively to avoid overfeeding distortion */
    if (tries > 0)
    {
      if (data->playback_speed < 0.6f)
      {
        /* For very slow speeds, feed additional input */
        rubberband_process(data->rubberband_state, &input_ptr, n_samples, 0);
      }
      else
      {
        /* For normal and fast speeds, single input feed per retry */
        rubberband_process(data->rubberband_state, &input_ptr, n_samples, 0);
      }
    }

    available = rubberband_available(data->rubberband_state);
    if (available > 0)
    {
      uint32_t space_left = 256 - buffer_fill;
      uint32_t to_retrieve = (uint32_t)available;
      if (to_retrieve > space_left)
      {
        to_retrieve = space_left;
      }
      if (to_retrieve > data->rubberband_buffer_size)
      {
        to_retrieve = data->rubberband_buffer_size;
      }

      if (to_retrieve > 0)
      {
        float *output_ptr = data->rubberband_output_buffer;
        size_t retrieved = rubberband_retrieve(data->rubberband_state, &output_ptr, to_retrieve);

        if (retrieved > 0 && buffer_fill + retrieved <= 256)
        {
          memcpy(&mini_buffer[buffer_fill], data->rubberband_output_buffer, retrieved * sizeof(float));
          buffer_fill += (uint32_t)retrieved;
        }
      }
    }

    /* If we have enough samples, break early */
    if (buffer_fill >= n_samples)
    {
      break;
    }
  }

  /* Debug logging */
  if (should_debug)
  {
    pw_log_debug("Rubberband: speed=%.2f, pitch=%.1f, buffer_fill=%u, needed=%u, available=%d",
                 data->playback_speed, data->pitch_shift, buffer_fill, n_samples, available);
  }

  /* Check if buffer is running low and we need additional processing */
  if (buffer_fill < n_samples)
  {
    if (data->playback_speed < 0.6f)
    {
      /* Buffer is low for very slow speed - feed more input conservatively */
      int extra_feeds = 2; /* Conservative feeding to avoid distortion */

      for (int feed = 0; feed < extra_feeds; feed++)
      {
        rubberband_process(data->rubberband_state, &input_ptr, n_samples, 0);

        available = rubberband_available(data->rubberband_state);
        if (available > 0)
        {
          uint32_t space_left = 256 - buffer_fill;
          uint32_t to_retrieve = (uint32_t)available;
          if (to_retrieve > space_left)
            to_retrieve = space_left;
          if (to_retrieve > data->rubberband_buffer_size)
            to_retrieve = data->rubberband_buffer_size;

          if (to_retrieve > 0)
          {
            float *output_ptr = data->rubberband_output_buffer;
            size_t retrieved = rubberband_retrieve(data->rubberband_state, &output_ptr, to_retrieve);

            if (retrieved > 0 && buffer_fill + retrieved <= 256)
            {
              memcpy(&mini_buffer[buffer_fill], data->rubberband_output_buffer, retrieved * sizeof(float));
              buffer_fill += (uint32_t)retrieved;
            }
          }
        }

        /* Stop if we have enough now */
        if (buffer_fill >= n_samples)
          break;
      }
    }
    else
    {
      /* For normal and fast speeds - try conservative output retrieval */
      for (int retrieval_attempt = 0; retrieval_attempt < 2 && buffer_fill < n_samples; retrieval_attempt++)
      {
        available = rubberband_available(data->rubberband_state);
        if (available > 0)
        {
          uint32_t space_left = 256 - buffer_fill;
          uint32_t to_retrieve = (uint32_t)available;
          if (to_retrieve > space_left)
            to_retrieve = space_left;
          if (to_retrieve > data->rubberband_buffer_size)
            to_retrieve = data->rubberband_buffer_size;

          if (to_retrieve > 0)
          {
            float *output_ptr = data->rubberband_output_buffer;
            size_t retrieved = rubberband_retrieve(data->rubberband_state, &output_ptr, to_retrieve);

            if (retrieved > 0 && buffer_fill + retrieved <= 256)
            {
              memcpy(&mini_buffer[buffer_fill], data->rubberband_output_buffer, retrieved * sizeof(float));
              buffer_fill += (uint32_t)retrieved;
            }
          }
        }
        else
        {
          /* No more output available, feed a single input and try again */
          rubberband_process(data->rubberband_state, &input_ptr, n_samples, 0);
        }
      }
    }
  }

  /* Output from mini buffer with better handling */
  if (buffer_fill >= n_samples)
  {
    /* We have enough - use what we need */
    memcpy(buffer, mini_buffer, n_samples * sizeof(float));

    /* Shift remaining data down */
    buffer_fill -= n_samples;
    if (buffer_fill > 0)
    {
      memmove(mini_buffer, &mini_buffer[n_samples], buffer_fill * sizeof(float));
    }

    return n_samples;
  }
  else if (buffer_fill > 0)
  {
    /* Use what we have and pad with previous sample instead of silence */
    memcpy(buffer, mini_buffer, buffer_fill * sizeof(float));

    /* Fill remaining with last valid sample to avoid clicks */
    float last_sample = (buffer_fill > 0) ? mini_buffer[buffer_fill - 1] : 0.0f;
    for (uint32_t i = buffer_fill; i < n_samples; i++)
    {
      buffer[i] = last_sample * 0.95f; /* Gentle fade to avoid artifacts */
    }

    buffer_fill = 0;
    return n_samples;
  }
  else
  {
    /* No output available - return silence but log this for debugging */
    static int silence_count = 0;
    if (++silence_count % 100 == 0) /* Log every 100th occurrence */
    {
      pw_log_debug("Rubberband: No output available (speed=%.2f, pitch=%.1f) - count %d",
                   data->playback_speed, data->pitch_shift, silence_count);
    }
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
  uint32_t samples_needed = (uint32_t)(n_samples * playback_speed) + 1;
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
    float src_index = i * playback_speed;
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