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
  sf_count_t frames_read = read_audio_frames_variable_speed_rt(data, buf, n_samples);

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

  if (data->pitch_shift <= 0.0f || data->pitch_shift > 10.0f)
  {
    /* Invalid pitch, use normal pitch as fallback */
    data->pitch_shift = 1.0f;
  }

  if (data->playback_speed == 1.0f && data->pitch_shift == 1.0f)
  {
    /* Normal speed and pitch - use the optimized path */
    return read_audio_frames_rt(data, buf, n_samples);
  }

  /* Independent speed and pitch control */
  return read_audio_frames_variable_speed_pitch_rt(data, buf, n_samples);
}

sf_count_t read_audio_frames_variable_speed_pitch_rt(struct data *data, float *buf, uint32_t n_samples)
{
  sf_count_t total_frames = data->fileinfo.frames;
  
  /* STEP 1: Fill the buffer based on speed (tempo) control only
   * This affects how fast we read through the file but not the pitch */
  uint32_t speed_frames = 0;
  static double speed_position_tracker = 0.0; /* Separate position tracker for speed */
  
  /* Initialize speed position if needed */
  if (speed_position_tracker == 0.0)
  {
    speed_position_tracker = data->sample_position;
  }
  
  while (speed_frames < n_samples)
  {
    sf_count_t sample_index = (sf_count_t)speed_position_tracker;
    double frac = speed_position_tracker - sample_index;

    /* Handle looping */
    if (sample_index >= total_frames)
    {
      speed_position_tracker = fmod(speed_position_tracker, total_frames);
      sample_index = (sf_count_t)speed_position_tracker;
      frac = speed_position_tracker - sample_index;
    }

    /* Seek to the current position */
    if (sf_seek(data->file, sample_index, SEEK_SET) != sample_index)
    {
      buf[speed_frames] = 0.0f;
      speed_frames++;
      speed_position_tracker += data->playback_speed;
      continue;
    }

    /* Read current and next sample for interpolation */
    static float temp_buffer[8];
    sf_count_t frames_to_read = SPA_MIN(2, total_frames - sample_index);
    sf_count_t frames_read;
    
    if (data->fileinfo.channels == 1)
    {
      frames_read = sf_readf_float(data->file, temp_buffer, frames_to_read);
    }
    else
    {
      /* Multi-channel file - read and extract first channel */
      static float temp_multichannel[16];
      sf_count_t temp_frames = sf_readf_float(data->file, temp_multichannel, frames_to_read);
      
      /* Extract first channel */
      for (sf_count_t j = 0; j < temp_frames; j++)
      {
        temp_buffer[j] = temp_multichannel[j * data->fileinfo.channels];
      }
      frames_read = temp_frames;
    }

    if (frames_read <= 0)
    {
      buf[speed_frames] = 0.0f;
      speed_frames++;
      speed_position_tracker += data->playback_speed;
      continue;
    }

    /* Interpolate between samples for smooth playback */
    float sample = temp_buffer[0];
    if (frames_read > 1 && frac > 0.0)
    {
      sample = temp_buffer[0] + (temp_buffer[1] - temp_buffer[0]) * frac;
    }

    buf[speed_frames] = sample;
    speed_frames++;

    /* Advance position by speed rate (controls tempo only) */
    speed_position_tracker += data->playback_speed;
  }
  
  /* Update main position tracker */
  data->sample_position = speed_position_tracker;

  /* STEP 2: Apply pitch shifting WITHOUT affecting tempo
   * This is independent time-stretching/pitch-shifting */
  if (data->pitch_shift != 1.0f)
  {
    static float pitch_buffer[4096];
    static double pitch_read_position = 0.0;
    
    /* Copy speed-adjusted samples to temp buffer */
    for (uint32_t i = 0; i < speed_frames && i < 4096; i++)
    {
      pitch_buffer[i] = buf[i];
    }
    
    /* Apply pitch shift by resampling the speed-adjusted audio
     * This changes frequency without affecting the playback rate */
    for (uint32_t i = 0; i < n_samples; i++)
    {
      /* Calculate source position in the pitch buffer */
      double src_pos = pitch_read_position;
      sf_count_t idx = (sf_count_t)src_pos;
      double frac_pitch = src_pos - idx;
      
      if (idx < speed_frames - 1)
      {
        /* Linear interpolation for pitch shifting */
        buf[i] = pitch_buffer[idx] + 
                 (pitch_buffer[idx + 1] - pitch_buffer[idx]) * frac_pitch;
      }
      else if (idx < speed_frames)
      {
        buf[i] = pitch_buffer[idx];
      }
      else
      {
        /* Beyond available data, fill with silence */
        buf[i] = 0.0f;
      }
      
      /* Advance pitch read position
       * pitch_shift > 1.0 = higher pitch (read faster through buffer)
       * pitch_shift < 1.0 = lower pitch (read slower through buffer) */
      pitch_read_position += data->pitch_shift;
      
      /* Reset pitch position when we've read all available data */
      if (pitch_read_position >= speed_frames)
      {
        pitch_read_position = fmod(pitch_read_position, speed_frames);
      }
    }
    
    return n_samples;
  }

  /* No pitch shifting - return speed-adjusted frames */
  return speed_frames;
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
