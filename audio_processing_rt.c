#include "audio_processing_rt.h"
#include "rt_nonrt_bridge.h"

void handle_audio_input_rt(struct data *data, uint32_t n_samples)
{
  /* Get input buffer */
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

  /* Calculate RMS occasionally for level monitoring (RT-safe) */
  static uint32_t rms_skip_counter = 0;
  if (++rms_skip_counter >= 100)
  { /* Every ~2 seconds at 48kHz/1024 */
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
      if (++overrun_counter >= 1000)
      { /* Throttle error messages */
        struct rt_message msg = {
            .type = RT_MSG_ERROR,
        };
        strncpy(msg.data.error.message, "Audio buffer overrun",
                sizeof(msg.data.error.message) - 1);
        rt_bridge_send_message(&data->rt_bridge, &msg);
        overrun_counter = 0;
      }
    }
  }
}

void process_audio_output_rt(struct data *data, struct spa_io_position *position)
{
  struct pw_buffer *b;
  uint32_t n_samples = position->clock.duration;

  if (data->current_state != HOLO_STATE_PLAYING)
  {
    return; /* Skip processing if not in playing state */
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
    sf_seek(data->file, 0, SEEK_SET);
    data->sample_position = 0.0;  /* Reset fractional position for variable speed */
    data->reset_audio = false;
  }

  /* Read audio data with variable speed playback */
  sf_count_t frames_read;
  
  /* Debug: Check which audio processing path is taken */
  static bool debug_path_logged = false;
  if (!debug_path_logged) {
    pw_log_info("DEBUG: Audio processing path - rubberband_enabled: %s, rubberband_state: %s", 
           data->rubberband_enabled ? "true" : "false",
           data->rubberband_state ? "valid" : "NULL");
    debug_path_logged = true;
  }
  
  if (data->rubberband_enabled && data->rubberband_state) {
    frames_read = read_audio_frames_rubberband_rt(data, buf, n_samples);
  } else {
    frames_read = read_audio_frames_variable_speed_rt(data, buf, n_samples);
  }

  /* Apply volume (RT-optimized) */
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

  /* Optimized RMS calculation - unroll loop for better performance */
  float sum = 0.0f;
  uint32_t i;

  /* Process 4 samples at a time for better cache usage */
  for (i = 0; i < (n_samples & ~3); i += 4)
  {
    float s0 = buffer[i];
    float s1 = buffer[i + 1];
    float s2 = buffer[i + 2];
    float s3 = buffer[i + 3];
    sum += s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3;
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

  /* Vectorizable loop for volume application */
  for (uint32_t i = 0; i < frames; i++)
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
    strncpy(msg.data.recording.filename, filename,
            sizeof(msg.data.recording.filename) - 1);
    msg.data.recording.filename[sizeof(msg.data.recording.filename) - 1] = '\0';
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
  /* Debug: One-time state check at first call */
  static bool first_call = true;
  if (first_call) {
    pw_log_info("DEBUG: First rubberband call - enabled: %s, state: %s", 
           data->rubberband_enabled ? "true" : "false",
           data->rubberband_state ? "valid" : "NULL");
    first_call = false;
  }

  if (!data->rubberband_enabled || !data->rubberband_state) {
    /* Debug: Print why we're falling back */
    static int debug_count = 0;
    if (debug_count < 5) {  /* Limit debug output to avoid spam */
      pw_log_info("DEBUG: Rubberband fallback - enabled: %s, state: %s", 
             data->rubberband_enabled ? "true" : "false",
             data->rubberband_state ? "valid" : "NULL");
      debug_count++;
    }
    /* Fallback to variable speed if rubberband is disabled */
    return read_audio_frames_variable_speed_rt(data, buf, n_samples);
  }

  /* Update rubberband parameters if playback speed changed */
  static float last_speed = 1.0f;
  static float last_pitch = 0.0f;
  static int param_debug_count = 0;
  
  if (data->playback_speed != last_speed) {
    /* Set time ratio for speed changes (inverse of playback speed) */
    double time_ratio = 1.0 / data->playback_speed;
    rubberband_set_time_ratio(data->rubberband_state, time_ratio);
    if (param_debug_count < 3) {
      pw_log_info("DEBUG: Set time ratio to %.3f (speed %.2f)", time_ratio, data->playback_speed);
      param_debug_count++;
    }
    last_speed = data->playback_speed;
  }
  
  if (data->pitch_shift != last_pitch) {
    /* Set pitch scale for pitch changes, ensuring 1.0 means no pitch change */
    if (data->pitch_shift == 0.0f) {
      rubberband_set_pitch_scale(data->rubberband_state, 1.0);
      if (param_debug_count < 3) {
        pw_log_info("DEBUG: Set pitch scale to 1.0 (no pitch shift)");
      }
    } else {
      float pitch_scale = powf(2.0f, data->pitch_shift / 12.0f);
      rubberband_set_pitch_scale(data->rubberband_state, pitch_scale);
      if (param_debug_count < 3) {
        pw_log_info("DEBUG: Set pitch scale to %.3f (%.2f semitones)", pitch_scale, data->pitch_shift);
      }
    }
    last_pitch = data->pitch_shift;
  }

  uint32_t total_output = 0;
  uint32_t remaining = n_samples;

  while (remaining > 0 && total_output < n_samples) {
    /* Check how many samples rubberband needs */
    int required = rubberband_get_samples_required(data->rubberband_state);
    
    if (required > 0) {
      /* Read input samples from file */
      uint32_t to_read = (uint32_t)required;
      if (to_read > data->rubberband_buffer_size) {
        to_read = data->rubberband_buffer_size;
      }

      sf_count_t frames_read = read_audio_frames_rt(data, data->rubberband_input_buffer, to_read);
      
      if (frames_read > 0) {
        /* Feed samples to rubberband */
        const float *input_ptr = data->rubberband_input_buffer;
        rubberband_process(data->rubberband_state, &input_ptr, (size_t)frames_read, 0);
      }
    }

    /* Try to retrieve processed samples */
    int available = rubberband_available(data->rubberband_state);
    if (available > 0) {
      uint32_t to_retrieve = (uint32_t)available;
      if (to_retrieve > remaining) {
        to_retrieve = remaining;
      }
      if (to_retrieve > data->rubberband_buffer_size) {
        to_retrieve = data->rubberband_buffer_size;
      }

      float *output_ptr = data->rubberband_output_buffer;
      size_t retrieved = rubberband_retrieve(data->rubberband_state, &output_ptr, to_retrieve);
      
      /* Copy to output buffer */
      for (size_t i = 0; i < retrieved && total_output < n_samples; i++) {
        buf[total_output++] = data->rubberband_output_buffer[i];
      }
      
      remaining = n_samples - total_output;
    } else {
      /* No samples available, break to avoid infinite loop */
      break;
    }
  }

  /* Fill remaining buffer with silence if needed */
  for (uint32_t i = total_output; i < n_samples; i++) {
    buf[i] = 0.0f;
  }

  return total_output;
}
