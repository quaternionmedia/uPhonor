#include "audio_processing_rt.h"
#include "rt_nonrt_bridge.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

void handle_audio_input_rt(struct data *data, uint32_t n_samples)
{
  /* Get input buffer first - we need to process it even if not recording */
  float *in = pw_filter_get_dsp_buffer(data->audio_in, n_samples);

  if (!in)
  {
    /* Use silence if no input buffer available */
    if (data->rt_bridge.rt_recording_enabled)
    {
      /* Push silence to recording buffer */
      rt_bridge_push_audio(&data->rt_bridge, data->silence_buffer, n_samples);
    }
    return;
  }

  /* Always process the input buffer to prevent "out of buffers" messages */
  /* Calculate RMS occasionally for level monitoring (RT-safe) */
  static uint32_t rms_skip_counter = 0;
  if (++rms_skip_counter >= 200)
  { /* Reduced frequency - Every ~4 seconds at 48kHz/1024 for better RT performance */
    float rms = calculate_rms_rt(in, n_samples);

    if (rms > 0.001f)
    {
      /* Send level information to non-RT thread */
      struct rt_message msg = {
          .type = RT_MSG_AUDIO_LEVEL,
          .data.audio_level.rms_level = rms};
      rt_bridge_send_message(&data->rt_bridge, &msg);
    }
    rms_skip_counter = 0;
  }

  /* Push audio to ring buffer for recording (RT-safe) */
  if (data->rt_bridge.rt_recording_enabled)
  {
    if (!rt_bridge_push_audio(&data->rt_bridge, in, n_samples))
    {
      /* Buffer overrun - could send error message but don't block */
      static uint32_t overrun_counter = 0;
      if (++overrun_counter >= 2000)
      { /* Throttle error messages more aggressively for RT safety */
        struct rt_message msg = {
            .type = RT_MSG_ERROR,
        };
        /* Use a static error message to avoid string operations in RT thread */
        const char *error_msg = "Audio buffer overrun";
        memcpy(msg.data.error.message, error_msg, strlen(error_msg) + 1);
        rt_bridge_send_message(&data->rt_bridge, &msg);
        overrun_counter = 0;
      }
    }
  }

  /* Store audio in memory loop if any loop recording is active (RT-safe) */
  if (data->currently_recording_note < 128)
  {
    struct memory_loop *loop = &data->memory_loops[data->currently_recording_note];
    if (loop->recording_to_memory)
    {
      if (!store_audio_in_memory_loop_rt(data, data->currently_recording_note, in, n_samples))
      {
        /* Memory loop buffer is full - could send notification but don't block */
        static uint32_t loop_full_counter = 0;
        if (++loop_full_counter >= 2000)
        { /* Throttle error messages */
          struct rt_message msg = {
              .type = RT_MSG_ERROR,
          };
          const char *error_msg = "Memory loop buffer full";
          memcpy(msg.data.error.message, error_msg, strlen(error_msg) + 1);
          rt_bridge_send_message(&data->rt_bridge, &msg);
          loop_full_counter = 0;
        }
      }
      else
      {
        /* Check if sync recording should stop due to reaching target length */
        check_sync_recording_target_length(data, data->currently_recording_note);
      }
    }
  }
}

void process_audio_output_rt(struct data *data, struct spa_io_position *position)
{
  struct pw_buffer *b;
  uint32_t n_samples = position->clock.duration;

  /* Check if any loops are ready for playback */
  bool any_loops_playing = false;
  for (int i = 0; i < 128; i++)
  {
    if (data->memory_loops[i].is_playing && data->memory_loops[i].loop_ready)
    {
      any_loops_playing = true;
      break;
    }
  }

  if (!any_loops_playing)
  {
    return; /* Skip processing if no loops are playing */
  }

  /* Get output buffer */
  if ((b = pw_filter_dequeue_buffer(data->audio_out)) == NULL)
  {
    return; /* No buffers available - this is normal */
  }

  float *buf = b->buffer->datas[0].data;
  if (buf == NULL)
  {
    pw_filter_queue_buffer(data->audio_out, b);
    return;
  }

  uint32_t stride = sizeof(float);

  if (b->requested)
  {
    n_samples = SPA_MIN(n_samples, b->requested);
  }

  /* Handle audio reset (RT-safe file operations) */
  if (data->reset_audio)
  {
    /* In sync mode, don't reset all loops - they should maintain their positions */
    if (!data->sync_mode_enabled)
    {
      /* Reset all active memory loops */
      for (int i = 0; i < 128; i++)
      {
        if (data->memory_loops[i].loop_ready)
        {
          reset_memory_loop_playback_rt(data, i);
        }
      }
    }

    /* Reset file playback position */
    sf_seek(data->file, 0, SEEK_SET);
    data->sample_position = 0.0; /* Reset fractional position for variable speed */
    data->reset_audio = false;
  }

  /* Mix all active memory loops */
  sf_count_t frames_read = mix_all_active_loops_rt(data, buf, n_samples);

  /* Apply global volume (RT-optimized) */
  apply_volume_rt(buf, frames_read, data->volume);

  b->buffer->datas[0].chunk->offset = 0;
  b->buffer->datas[0].chunk->stride = stride;
  b->buffer->datas[0].chunk->size = frames_read * stride;

  pw_filter_queue_buffer(data->audio_out, b);
}

float calculate_rms_rt(const float *buffer, uint32_t n_samples)
{
  if (!buffer || n_samples == 0)
    return 0.0f;

  /* Highly optimized RMS calculation for RT thread */
  float sum = 0.0f;
  uint32_t i;

  /* Process 8 samples at a time for better SIMD potential and cache usage */
  const uint32_t unroll_count = 8;
  const uint32_t vectorized_samples = n_samples & ~(unroll_count - 1);

  for (i = 0; i < vectorized_samples; i += unroll_count)
  {
    float s0 = buffer[i];
    float s1 = buffer[i + 1];
    float s2 = buffer[i + 2];
    float s3 = buffer[i + 3];
    float s4 = buffer[i + 4];
    float s5 = buffer[i + 5];
    float s6 = buffer[i + 6];
    float s7 = buffer[i + 7];

    sum += s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3 +
           s4 * s4 + s5 * s5 + s6 * s6 + s7 * s7;
  }

  /* Handle remaining samples */
  for (; i < n_samples; i++)
  {
    float s = buffer[i];
    sum += s * s;
  }

  return sqrtf(sum / n_samples);
}

void apply_volume_rt(float *buf, uint32_t frames, float volume)
{
  if (volume == 1.0f)
    return; /* No change needed - avoid unnecessary operations */

  /* Highly optimized volume application for RT thread */
  uint32_t i;

  /* Process 8 samples at a time for better SIMD potential */
  const uint32_t unroll_count = 8;
  const uint32_t vectorized_frames = frames & ~(unroll_count - 1);

  for (i = 0; i < vectorized_frames; i += unroll_count)
  {
    buf[i] *= volume;
    buf[i + 1] *= volume;
    buf[i + 2] *= volume;
    buf[i + 3] *= volume;
    buf[i + 4] *= volume;
    buf[i + 5] *= volume;
    buf[i + 6] *= volume;
    buf[i + 7] *= volume;
  }

  /* Handle remaining samples */
  for (; i < frames; i++)
  {
    buf[i] *= volume;
  }
}

sf_count_t read_audio_frames_rt(struct data *data, float *buf, uint32_t n_samples)
{
  sf_count_t frames_read;

  if (data->fileinfo.channels == 1)
  {
    /* Direct read for mono files - most efficient */
    frames_read = sf_readf_float(data->file, buf, n_samples);
  }
  else
  {
    /* Multi-channel read with channel extraction */
    /* Use pre-allocated temp buffer to avoid memory allocation */
    sf_count_t temp_frames = sf_readf_float(data->file, data->temp_audio_buffer, n_samples);

    /* Extract first channel using optimized pointer arithmetic */
    float *src = data->temp_audio_buffer;
    uint32_t channels = data->fileinfo.channels;

    for (uint32_t i = 0; i < temp_frames; i++)
    {
      buf[i] = src[i * channels]; /* First channel only */
    }

    frames_read = temp_frames;
  }

  /* Handle end of file with single seek operation */
  if (frames_read < n_samples)
  {
    sf_seek(data->file, 0, SEEK_SET);

    /* Read remaining samples */
    sf_count_t remaining = n_samples - frames_read;
    sf_count_t additional;

    if (data->fileinfo.channels == 1)
    {
      additional = sf_readf_float(data->file, &buf[frames_read], remaining);
    }
    else
    {
      additional = sf_readf_float(data->file, data->temp_audio_buffer, remaining);
      float *src = data->temp_audio_buffer;
      uint32_t channels = data->fileinfo.channels;

      for (uint32_t i = 0; i < additional; i++)
      {
        buf[frames_read + i] = src[i * channels];
      }
    }

    frames_read += additional;
  }

  return frames_read;
}

sf_count_t read_audio_frames_variable_speed_rt(struct data *data, float *buf, uint32_t n_samples)
{
  if (data->playback_speed <= 0.0f || data->playback_speed > 10.0f)
  {
    /* Invalid speed, use normal speed as fallback */
    data->playback_speed = 1.0f;
  }

  if (data->playback_speed == 1.0f)
  {
    /* Normal speed - use the optimized path */
    return read_audio_frames_rt(data, buf, n_samples);
  }

  /* Variable speed playback using linear interpolation */
  sf_count_t total_frames = data->fileinfo.frames;
  uint32_t output_frames = 0;

  for (uint32_t i = 0; i < n_samples && output_frames < n_samples; i++)
  {
    /* Get integer and fractional parts of sample position */
    sf_count_t sample_index = (sf_count_t)data->sample_position;
    double frac = data->sample_position - sample_index;

    /* Handle looping */
    if (sample_index >= total_frames)
    {
      data->sample_position = fmod(data->sample_position, total_frames);
      sample_index = (sf_count_t)data->sample_position;
      frac = data->sample_position - sample_index;
      sf_seek(data->file, sample_index, SEEK_SET);
    }

    /* Read current and next samples for interpolation */
    float current_sample = 0.0f;
    float next_sample = 0.0f;

    if (data->fileinfo.channels == 1)
    {
      /* Mono file - direct read */
      sf_seek(data->file, sample_index, SEEK_SET);
      sf_readf_float(data->file, &current_sample, 1);

      if (sample_index + 1 < total_frames)
      {
        sf_readf_float(data->file, &next_sample, 1);
      }
      else
      {
        /* End of file - use first sample for looping */
        sf_seek(data->file, 0, SEEK_SET);
        sf_readf_float(data->file, &next_sample, 1);
      }
    }
    else
    {
      /* Multi-channel file - extract first channel */
      float temp_buffer[data->fileinfo.channels];

      sf_seek(data->file, sample_index, SEEK_SET);
      if (sf_readf_float(data->file, temp_buffer, 1) == 1)
      {
        current_sample = temp_buffer[0];
      }

      if (sample_index + 1 < total_frames)
      {
        if (sf_readf_float(data->file, temp_buffer, 1) == 1)
        {
          next_sample = temp_buffer[0];
        }
      }
      else
      {
        /* End of file - use first sample for looping */
        sf_seek(data->file, 0, SEEK_SET);
        if (sf_readf_float(data->file, temp_buffer, 1) == 1)
        {
          next_sample = temp_buffer[0];
        }
      }
    }

    /* Linear interpolation */
    buf[output_frames] = current_sample + (next_sample - current_sample) * frac;
    output_frames++;

    /* Advance sample position by speed multiplier */
    data->sample_position += data->playback_speed;
  }

  /* Fill remaining buffer with silence if needed */
  for (uint32_t i = output_frames; i < n_samples; i++)
  {
    buf[i] = 0.0f;
  }

  return output_frames;
}

int start_recording_rt(struct data *data, const char *filename)
{
  /* Send message to non-RT thread to start recording */
  struct rt_message msg = {
      .type = RT_MSG_START_RECORDING,
      .data.recording = {
          .sample_rate = data->format.info.raw.rate > 0 ? data->format.info.raw.rate : 48000,
          .channels = 1}};

  /* Copy filename if provided */
  if (filename)
  {
    /* Use memcpy instead of strncpy for RT safety and ensure null termination */
    size_t len = strlen(filename);
    if (len >= sizeof(msg.data.recording.filename))
    {
      len = sizeof(msg.data.recording.filename) - 1;
    }
    memcpy(msg.data.recording.filename, filename, len);
    msg.data.recording.filename[len] = '\0';
  }
  else
  {
    msg.data.recording.filename[0] = '\0';
  }

  if (!rt_bridge_send_message(&data->rt_bridge, &msg))
  {
    return -1; /* Message queue full */
  }

  /* Enable RT recording flag */
  rt_bridge_set_recording_enabled(&data->rt_bridge, true);
  data->recording_enabled = true;

  return 0;
}

int stop_recording_rt(struct data *data)
{
  /* Disable RT recording flag first */
  rt_bridge_set_recording_enabled(&data->rt_bridge, false);
  data->recording_enabled = false;

  /* Send message to non-RT thread to stop recording */
  struct rt_message msg = {
      .type = RT_MSG_STOP_RECORDING};

  if (!rt_bridge_send_message(&data->rt_bridge, &msg))
  {
    return -1; /* Message queue full */
  }

  return 0;
}

uint32_t read_audio_frames_rubberband_rt(struct data *data, float *buf, uint32_t n_samples)
{
  if (!data->rubberband_enabled || !data->rubberband_state)
  {
    /* Fallback to variable speed if rubberband is disabled */
    return read_audio_frames_variable_speed_rt(data, buf, n_samples);
  }

  /* Update rubberband parameters if playback speed changed */
  static float last_speed = 1.0f;
  static float last_pitch = 0.0f;
  bool params_changed = false;

  if (data->playback_speed != last_speed)
  {
    /* Set time ratio for speed changes (inverse of playback speed) */
    double time_ratio = 1.0 / data->playback_speed;
    rubberband_set_time_ratio(data->rubberband_state, time_ratio);
    params_changed = true;
    last_speed = data->playback_speed;
  }

  if (data->pitch_shift != last_pitch)
  {
    /* Set pitch scale for pitch changes, ensuring 1.0 means no pitch change */
    if (data->pitch_shift == 0.0f)
    {
      rubberband_set_pitch_scale(data->rubberband_state, 1.0);
    }
    else
    {
      float pitch_scale = powf(2.0f, data->pitch_shift / 12.0f);
      rubberband_set_pitch_scale(data->rubberband_state, pitch_scale);
    }
    params_changed = true;
    last_pitch = data->pitch_shift;
  }

  /* If parameters changed, use different strategies based on change type */
  if (params_changed)
  {
    /* Determine what type of change occurred */
    bool pitch_changed = (data->pitch_shift != last_pitch);
    bool speed_changed = (data->playback_speed != last_speed);

    if (pitch_changed)
    {
      /* Pitch changes benefit from reset for immediate response */
      rubberband_reset(data->rubberband_state);
    }
    else if (speed_changed)
    {
      /* For speed-only changes, use gentle flushing to avoid speedup artifacts */
      uint32_t flush_size = 32; /* Very small chunks to minimize artifacts */
      if (flush_size > data->rubberband_buffer_size)
      {
        flush_size = data->rubberband_buffer_size;
      }

      sf_count_t frames_read = read_audio_frames_buffered_rt(data, data->rubberband_input_buffer, flush_size);
      if (frames_read > 0)
      {
        const float *input_ptr = data->rubberband_input_buffer;
        rubberband_process(data->rubberband_state, &input_ptr, (size_t)frames_read, 0);
      }
    }
  }

  uint32_t total_output = 0;
  uint32_t remaining = n_samples;

  /* Prime the rubberband with some initial data if it's empty */
  int available = rubberband_available(data->rubberband_state);
  if (available == 0)
  {
    /* Feed some initial samples to get rubberband started */
    uint32_t prime_size = 256; /* Small prime buffer */
    if (prime_size > data->rubberband_buffer_size)
    {
      prime_size = data->rubberband_buffer_size;
    }

    sf_count_t frames_read = read_audio_frames_buffered_rt(data, data->rubberband_input_buffer, prime_size);
    if (frames_read > 0)
    {
      const float *input_ptr = data->rubberband_input_buffer;
      rubberband_process(data->rubberband_state, &input_ptr, (size_t)frames_read, 0);
    }
  }

  /* Add a safety counter to prevent infinite loops */
  int iteration_count = 0;
  const int max_iterations = params_changed ? 25 : 15; /* Extra iterations when params changed for faster transition */

  while (remaining > 0 && total_output < n_samples && iteration_count < max_iterations)
  {
    iteration_count++;
    bool made_progress = false;

    /* Check how many samples rubberband needs */
    int required = rubberband_get_samples_required(data->rubberband_state);

    if (required > 0)
    {
      /* Use smaller chunks when parameters changed for faster response */
      uint32_t to_read = (uint32_t)required;
      uint32_t chunk_limit = params_changed ? 64 : 128; /* Smaller chunks for parameter changes */
      if (to_read > chunk_limit)
      {
        to_read = chunk_limit;
      }
      if (to_read > data->rubberband_buffer_size)
      {
        to_read = data->rubberband_buffer_size;
      }

      sf_count_t frames_read = read_audio_frames_buffered_rt(data, data->rubberband_input_buffer, to_read);

      if (frames_read > 0)
      {
        /* Feed samples to rubberband */
        const float *input_ptr = data->rubberband_input_buffer;
        rubberband_process(data->rubberband_state, &input_ptr, (size_t)frames_read, 0);
        made_progress = true;
      }
    }

    /* Try to retrieve processed samples */
    available = rubberband_available(data->rubberband_state);
    if (available > 0)
    {
      uint32_t to_retrieve = (uint32_t)available;
      if (to_retrieve > remaining)
      {
        to_retrieve = remaining;
      }
      if (to_retrieve > data->rubberband_buffer_size)
      {
        to_retrieve = data->rubberband_buffer_size;
      }

      float *output_ptr = data->rubberband_output_buffer;
      size_t retrieved = rubberband_retrieve(data->rubberband_state, &output_ptr, to_retrieve);

      /* Copy to output buffer */
      for (size_t i = 0; i < retrieved && total_output < n_samples; i++)
      {
        buf[total_output++] = data->rubberband_output_buffer[i];
      }

      remaining = n_samples - total_output;
      made_progress = true;
    }

    /* If no progress was made in this iteration, break to avoid infinite loop */
    if (!made_progress)
    {
      break;
    }
  }

  /* Fill remaining buffer with silence if needed */
  for (uint32_t i = total_output; i < n_samples; i++)
  {
    buf[i] = 0.0f;
  }

  return total_output;
}

/* Optimized buffered audio reading functions - minimize file I/O in RT thread */

sf_count_t read_audio_frames_buffered_rt(struct data *data, float *buf, uint32_t n_samples)
{
  /* Use the buffered system to minimize file I/O operations */
  return audio_buffer_rt_read(&data->audio_buffer, data->file, &data->fileinfo, buf, n_samples);
}

sf_count_t read_audio_frames_variable_speed_buffered_rt(struct data *data, float *buf, uint32_t n_samples)
{
  if (data->playback_speed <= 0.0f || data->playback_speed > 10.0f)
  {
    /* Invalid speed, use normal speed as fallback */
    data->playback_speed = 1.0f;
  }

  if (data->playback_speed == 1.0f)
  {
    /* Normal speed - use the optimized buffered path */
    return read_audio_frames_buffered_rt(data, buf, n_samples);
  }

  /* Variable speed playback using linear interpolation with minimal file I/O */
  sf_count_t total_frames = data->fileinfo.frames;
  uint32_t output_frames = 0;

  /* Pre-allocate a small working buffer to reduce individual sample reads */
  const uint32_t work_buffer_size = 256;
  static float work_buffer[256];
  static sf_count_t work_buffer_start = -1;
  static uint32_t work_buffer_valid = 0;

  for (uint32_t i = 0; i < n_samples && output_frames < n_samples; i++)
  {
    /* Get integer and fractional parts of sample position */
    sf_count_t sample_index = (sf_count_t)data->sample_position;
    double frac = data->sample_position - sample_index;

    /* Handle looping */
    if (sample_index >= total_frames)
    {
      data->sample_position = fmod(data->sample_position, total_frames);
      sample_index = (sf_count_t)data->sample_position;
      frac = data->sample_position - sample_index;
      audio_buffer_rt_reset(&data->audio_buffer);
      work_buffer_start = -1; /* Invalidate work buffer */
    }

    /* Check if we need to refresh the work buffer */
    if (work_buffer_start == -1 ||
        sample_index < work_buffer_start ||
        sample_index >= (work_buffer_start + work_buffer_valid))
    {
      /* Refresh work buffer from the main audio buffer */
      work_buffer_start = sample_index;
      work_buffer_valid = audio_buffer_rt_read(&data->audio_buffer, data->file, &data->fileinfo,
                                               work_buffer, work_buffer_size);
    }

    /* Get samples for interpolation from work buffer */
    float current_sample = 0.0f;
    float next_sample = 0.0f;

    sf_count_t local_index = sample_index - work_buffer_start;
    if (local_index < work_buffer_valid)
    {
      current_sample = work_buffer[local_index];

      if (local_index + 1 < work_buffer_valid)
      {
        next_sample = work_buffer[local_index + 1];
      }
      else if (sample_index + 1 < total_frames)
      {
        /* Need next sample, but it's beyond our work buffer */
        sf_count_t next_pos = sample_index + 1;
        audio_buffer_rt_read(&data->audio_buffer, data->file, &data->fileinfo, &next_sample, 1);
      }
      else
      {
        /* End of file - use first sample for looping */
        audio_buffer_rt_reset(&data->audio_buffer);
        audio_buffer_rt_read(&data->audio_buffer, data->file, &data->fileinfo, &next_sample, 1);
      }
    }

    /* Linear interpolation */
    buf[output_frames] = current_sample + (next_sample - current_sample) * frac;
    output_frames++;

    /* Advance sample position by speed multiplier */
    data->sample_position += data->playback_speed;
  }

  /* Fill remaining buffer with silence if needed */
  for (uint32_t i = output_frames; i < n_samples; i++)
  {
    buf[i] = 0.0f;
  }

  return output_frames;
}

void reset_memory_loop_playback_rt(struct data *data, uint8_t midi_note)
{
  if (!data || midi_note >= 128)
    return;

  struct memory_loop *loop = &data->memory_loops[midi_note];

  // Calculate synchronized start position in sync mode
  if (data->sync_mode_enabled && data->pulse_loop_note != 255 && midi_note != data->pulse_loop_note)
  {
    // Get the pulse loop to sync with
    struct memory_loop *pulse_loop = &data->memory_loops[data->pulse_loop_note];
    if (pulse_loop->is_playing && data->pulse_loop_duration > 0 && loop->recorded_frames > 0)
    {
      // Calculate current pulse position and cutoff point
      uint32_t pulse_position = pulse_loop->playback_position;
      uint32_t cutoff_position = (uint32_t)(data->sync_cutoff_percentage * data->pulse_loop_duration);

      // Decide whether to sync to current pulse or wait for next
      if (pulse_position <= cutoff_position)
      {
        // Before cutoff - sync to current pulse position
        if (pulse_position < loop->recorded_frames)
        {
          // Pulse position fits within this loop - start there
          loop->playback_position = pulse_position;
        }
        else
        {
          // Pulse position is beyond this loop's length - use modulo
          loop->playback_position = pulse_position % loop->recorded_frames;
        }

        return; // Early return - don't reset to 0
      }
      // If after cutoff, fall through to reset to 0 (wait for next pulse)
    }
  }

  // Default behavior - reset to beginning
  data->memory_loops[midi_note].playback_position = 0;
}

int start_loop_recording_rt(struct data *data, uint8_t midi_note, const char *filename)
{
  if (!data || midi_note >= 128)
    return -1;

  struct memory_loop *loop = &data->memory_loops[midi_note];

  if (!loop->buffer)
    return -1;

  /* Reset loop state */
  loop->recorded_frames = 0;
  loop->playback_position = 0;
  loop->loop_ready = false;
  loop->recording_to_memory = true;

  /* Store filename for later file write */
  if (filename)
  {
    size_t len = strlen(filename);
    if (len >= sizeof(loop->loop_filename))
    {
      len = sizeof(loop->loop_filename) - 1;
    }
    memcpy(loop->loop_filename, filename, len);
    loop->loop_filename[len] = '\0';
  }
  else
  {
    /* Generate timestamp-based filename */
    time_t now;
    time(&now);
    struct tm *tm_info = localtime(&now);
    snprintf(loop->loop_filename, sizeof(loop->loop_filename),
             "loop_note%d_%04d-%02d-%02d_%02d-%02d-%02d.wav", midi_note,
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
  }

  /* Also start regular recording for backup */
  start_recording_rt(data, loop->loop_filename);

  return 0;
}

int stop_loop_recording_rt(struct data *data, uint8_t midi_note)
{
  if (!data || midi_note >= 128)
    return -1;

  struct memory_loop *loop = &data->memory_loops[midi_note];

  if (!loop->recording_to_memory)
    return -1;

  /* Stop memory recording */
  loop->recording_to_memory = false;

  /* If we recorded something, mark loop as ready for playback */
  if (loop->recorded_frames > 0)
  {
    loop->loop_ready = true;
    loop->playback_position = 0;

    /* Send message to non-RT thread to write loop to file */
    struct rt_message msg = {
        .type = RT_MSG_WRITE_LOOP_TO_FILE,
        .data.loop_write = {
            .audio_data = loop->buffer,
            .num_frames = loop->recorded_frames,
            .sample_rate = loop->sample_rate}};

    /* Copy filename */
    memcpy(msg.data.loop_write.filename, loop->loop_filename,
           sizeof(msg.data.loop_write.filename));

    rt_bridge_send_message(&data->rt_bridge, &msg);
  }

  /* Also stop regular recording */
  stop_recording_rt(data);

  return 0;
}

bool store_audio_in_memory_loop_rt(struct data *data, uint8_t midi_note, const float *input, uint32_t n_samples)
{
  if (midi_note >= 128 || !data || !input)
    return false;

  struct memory_loop *loop = &data->memory_loops[midi_note];

  if (!loop->buffer || !loop->recording_to_memory)
    return false;

  /* Check if we have space in the buffer */
  uint32_t space_available = loop->buffer_size - loop->recorded_frames;
  uint32_t samples_to_store = n_samples;

  if (samples_to_store > space_available)
  {
    samples_to_store = space_available;
  }

  /* Copy audio data to memory buffer */
  float *dest = loop->buffer + loop->recorded_frames;
  memcpy(dest, input, samples_to_store * sizeof(float));

  loop->recorded_frames += samples_to_store;

  /* Return true if we stored all samples, false if buffer is full */
  return (samples_to_store == n_samples);
}

sf_count_t read_audio_frames_from_memory_loop_rt(struct data *data, float *buf, uint32_t n_samples)
{
  // TEMPORARY: Use first loop - this function needs proper multi-loop implementation
  if (!data || !data->memory_loops[0].buffer || !data->memory_loops[0].loop_ready || !buf)
    return 0;

  if (data->memory_loops[0].recorded_frames == 0)
    return 0;

  uint32_t frames_copied = 0;

  for (uint32_t i = 0; i < n_samples; i++)
  {
    /* Handle looping */
    if (data->memory_loops[0].playback_position >= data->memory_loops[0].recorded_frames)
    {
      data->memory_loops[0].playback_position = 0;
    }

    /* Copy sample from memory loop */
    buf[i] = data->memory_loops[0].buffer[data->memory_loops[0].playback_position];
    data->memory_loops[0].playback_position++;
    frames_copied++;
  }

  return frames_copied;
}

sf_count_t read_audio_frames_from_memory_loop_variable_speed_rt(struct data *data, float *buf, uint32_t n_samples)
{
  // TEMPORARY: Use first loop - this function needs proper multi-loop implementation
  if (!data || !data->memory_loops[0].buffer || !data->memory_loops[0].loop_ready || !buf)
    return 0;

  if (data->memory_loops[0].recorded_frames == 0)
    return 0;

  if (data->playback_speed <= 0.0f || data->playback_speed > 10.0f)
  {
    data->playback_speed = 1.0f;
  }

  if (data->playback_speed == 1.0f)
  {
    /* Normal speed - use the optimized path */
    return read_audio_frames_from_memory_loop_rt(data, buf, n_samples);
  }

  /* Variable speed playback using linear interpolation on memory loop */
  uint32_t total_frames = data->memory_loops[0].recorded_frames;
  uint32_t output_frames = 0;
  static double loop_sample_position = 0.0; /* Separate position for memory loop */

  for (uint32_t i = 0; i < n_samples && output_frames < n_samples; i++)
  {
    /* Get integer and fractional parts of sample position */
    uint32_t sample_index = (uint32_t)loop_sample_position;
    double frac = loop_sample_position - sample_index;

    /* Handle looping */
    if (sample_index >= total_frames)
    {
      loop_sample_position = fmod(loop_sample_position, total_frames);
      sample_index = (uint32_t)loop_sample_position;
      frac = loop_sample_position - sample_index;
    }

    /* Get samples for interpolation */
    float current_sample = data->memory_loops[0].buffer[sample_index];
    float next_sample;

    if (sample_index + 1 < total_frames)
    {
      next_sample = data->memory_loops[0].buffer[sample_index + 1];
    }
    else
    {
      /* End of loop - use first sample for seamless looping */
      next_sample = data->memory_loops[0].buffer[0];
    }

    /* Linear interpolation */
    buf[output_frames] = current_sample + (next_sample - current_sample) * frac;
    output_frames++;

    /* Advance sample position by speed multiplier */
    loop_sample_position += data->playback_speed;
  }

  /* Fill remaining buffer with silence if needed */
  for (uint32_t i = output_frames; i < n_samples; i++)
  {
    buf[i] = 0.0f;
  }

  return output_frames;
}

uint32_t read_audio_frames_memory_loop_rubberband_rt(struct data *data, float *buf, uint32_t n_samples)
{
  pw_log_debug("Memory loop rubberband called: speed=%.2f, pitch=%.2f, enabled=%d, state=%p",
               data->playback_speed, data->pitch_shift, data->rubberband_enabled, data->rubberband_state);

  if (!data->rubberband_enabled || !data->rubberband_state)
  {
    pw_log_debug("Rubberband not available, falling back to variable speed");
    /* Fallback to variable speed if rubberband is disabled */
    return read_audio_frames_from_memory_loop_variable_speed_rt(data, buf, n_samples);
  }

  if (!data->memory_loops[0].loop_ready || data->memory_loops[0].recorded_frames == 0)
  {
    /* No loop available, fill with silence */
    for (uint32_t i = 0; i < n_samples; i++)
    {
      buf[i] = 0.0f;
    }
    return n_samples;
  }

  /* Update rubberband parameters if playback speed or pitch changed */
  static float last_speed = 1.0f;
  static float last_pitch = 0.0f;
  bool params_changed = false;
  bool pitch_changed = false;
  bool speed_changed = false;

  if (data->playback_speed != last_speed)
  {
    /* Set time ratio for speed changes (inverse of playback speed) */
    double time_ratio = 1.0 / data->playback_speed;
    rubberband_set_time_ratio(data->rubberband_state, time_ratio);
    params_changed = true;
    speed_changed = true;
    last_speed = data->playback_speed;
    pw_log_debug("Speed changed to %.2f, time_ratio=%.2f", data->playback_speed, time_ratio);
  }

  if (data->pitch_shift != last_pitch)
  {
    /* Set pitch scale for pitch changes, ensuring 1.0 means no pitch change */
    if (data->pitch_shift == 0.0f)
    {
      rubberband_set_pitch_scale(data->rubberband_state, 1.0);
      pw_log_debug("Pitch reset to 0.0, pitch_scale=1.0");
    }
    else
    {
      float pitch_scale = powf(2.0f, data->pitch_shift / 12.0f);
      rubberband_set_pitch_scale(data->rubberband_state, pitch_scale);
      pw_log_debug("Pitch changed to %.2f semitones, pitch_scale=%.2f", data->pitch_shift, pitch_scale);
    }
    params_changed = true;
    pitch_changed = true;
    last_pitch = data->pitch_shift;
  }

  /* If parameters changed, use gentle flushing to avoid timing artifacts */
  if (params_changed)
  {
    /* Use gentle flushing for both speed and pitch changes to avoid speedup artifacts */
    uint32_t flush_size = 32; /* Very small chunks to minimize artifacts */
    if (flush_size > data->rubberband_buffer_size)
    {
      flush_size = data->rubberband_buffer_size;
    }

    sf_count_t frames_read = read_audio_frames_from_memory_loop_rt(data, data->rubberband_input_buffer, flush_size);
    if (frames_read > 0)
    {
      const float *input_ptr = data->rubberband_input_buffer;
      rubberband_process(data->rubberband_state, &input_ptr, (size_t)frames_read, 0);
    }
  }

  uint32_t total_output = 0;
  uint32_t remaining = n_samples;

  /* Prime the rubberband with some initial data if it's empty */
  int available = rubberband_available(data->rubberband_state);
  if (available == 0)
  {
    /* Feed some initial samples to get rubberband started */
    uint32_t prime_size = 256; /* Small prime buffer */
    if (prime_size > data->rubberband_buffer_size)
    {
      prime_size = data->rubberband_buffer_size;
    }

    sf_count_t frames_read = read_audio_frames_from_memory_loop_rt(data, data->rubberband_input_buffer, prime_size);
    if (frames_read > 0)
    {
      const float *input_ptr = data->rubberband_input_buffer;
      rubberband_process(data->rubberband_state, &input_ptr, (size_t)frames_read, 0);
    }
  }

  /* Add a safety counter to prevent infinite loops */
  int iteration_count = 0;
  const int max_iterations = params_changed ? 25 : 15; /* Extra iterations when params changed for faster transition */

  while (remaining > 0 && total_output < n_samples && iteration_count < max_iterations)
  {
    iteration_count++;
    bool made_progress = false;

    /* Check how many samples rubberband needs */
    int required = rubberband_get_samples_required(data->rubberband_state);

    if (required > 0)
    {
      /* Use smaller chunks when parameters changed for faster response */
      uint32_t to_read = (uint32_t)required;
      uint32_t chunk_limit = params_changed ? 64 : 128; /* Smaller chunks for parameter changes */
      if (to_read > chunk_limit)
      {
        to_read = chunk_limit;
      }
      if (to_read > data->rubberband_buffer_size)
      {
        to_read = data->rubberband_buffer_size;
      }

      sf_count_t frames_read = read_audio_frames_from_memory_loop_rt(data, data->rubberband_input_buffer, to_read);

      if (frames_read > 0)
      {
        /* Feed samples to rubberband */
        const float *input_ptr = data->rubberband_input_buffer;
        rubberband_process(data->rubberband_state, &input_ptr, (size_t)frames_read, 0);
        made_progress = true;
      }
    }

    /* Try to retrieve processed samples */
    available = rubberband_available(data->rubberband_state);
    if (available > 0)
    {
      uint32_t to_retrieve = (uint32_t)available;
      if (to_retrieve > remaining)
      {
        to_retrieve = remaining;
      }
      if (to_retrieve > data->rubberband_buffer_size)
      {
        to_retrieve = data->rubberband_buffer_size;
      }

      float *output_ptr = data->rubberband_output_buffer;
      size_t retrieved = rubberband_retrieve(data->rubberband_state, &output_ptr, to_retrieve);

      /* Copy to output buffer */
      for (size_t i = 0; i < retrieved && total_output < n_samples; i++)
      {
        buf[total_output++] = data->rubberband_output_buffer[i];
      }

      remaining = n_samples - total_output;
      made_progress = true;
    }

    /* If no progress was made in this iteration, break to avoid infinite loop */
    if (!made_progress)
    {
      break;
    }
  }

  /* Fill remaining buffer with silence if needed */
  for (uint32_t i = total_output; i < n_samples; i++)
  {
    buf[i] = 0.0f;
  }

  return total_output;
}
