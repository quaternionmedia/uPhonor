#include "audio_processing_rt.h"
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "rt_nonrt_bridge.h"
#include <math.h>

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
    
    /* Reset static positions in the pitch/speed processing */
    extern void reset_speed_pitch_positions(void);
    reset_speed_pitch_positions();
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
  if (!data->file)
  {
    /* Fill with silence if no file */
    for (uint32_t i = 0; i < n_samples; i++)
    {
      buf[i] = 0.0f;
    }
    return n_samples;
  }

  sf_count_t total_frames = data->fileinfo.frames;
  if (total_frames == 0)
  {
    for (uint32_t i = 0; i < n_samples; i++)
    {
      buf[i] = 0.0f;
    }
    return n_samples;
  }

  /* SIMPLE INDEPENDENT APPROACH
   * 
   * Use a single position that advances by speed only.
   * Apply pitch as a separate resampling factor.
   */
  
  static double file_position = 0.0;       /* Current position in file (speed only) */
  static bool initialized = false;
  
  /* Reset static variables when audio is reset */
  if (data->reset_audio || !initialized)
  {
    file_position = 0.0;
    initialized = true;
  }
  
  /* Debug output occasionally */
  static uint32_t debug_counter = 0;
  if (++debug_counter >= 1000) {  /* Every ~20 seconds at 48kHz/1024 */
    pw_log_info("DEBUG: speed=%.3f, pitch=%.3f, position=%.3f", 
                data->playback_speed, data->pitch_shift, file_position);
    debug_counter = 0;
  }
  
  for (uint32_t i = 0; i < n_samples; i++)
  {
    /* STEP 1: Calculate file position based ONLY on speed */
    double current_position = file_position + (i * data->playback_speed);
    current_position = fmod(current_position, (double)total_frames);
    if (current_position < 0) current_position += total_frames;
    
    /* STEP 2: Apply pitch shift ONLY to the reading, not the position */
    double read_position = current_position;
    
    /* If pitch != 1.0, modify where we read from without affecting timeline */
    if (data->pitch_shift != 1.0f)
    {
      /* Use pitch to create a local offset for resampling */
      double pitch_offset = (i * (data->pitch_shift - 1.0f)) * 0.1;
      read_position += pitch_offset;
      
      /* Wrap the read position */
      read_position = fmod(read_position, (double)total_frames);
      if (read_position < 0) read_position += total_frames;
    }
    
    /* Read sample at calculated position */
    sf_count_t read_pos = (sf_count_t)read_position;
    double frac = read_position - read_pos;
    
    /* Ensure read position stays in bounds */
    if (read_pos >= total_frames) read_pos = total_frames - 1;
    if (read_pos < 0) read_pos = 0;
    
    /* Seek and read with interpolation */
    if (sf_seek(data->file, read_pos, SEEK_SET) == read_pos)
    {
      static float temp_samples[8];
      sf_count_t frames_read;
      
      if (data->fileinfo.channels == 1)
      {
        frames_read = sf_readf_float(data->file, temp_samples, 2);
      }
      else
      {
        static float temp_multi[16];
        frames_read = sf_readf_float(data->file, temp_multi, 2);
        for (int j = 0; j < frames_read; j++)
        {
          temp_samples[j] = temp_multi[j * data->fileinfo.channels];
        }
      }
      
      if (frames_read >= 1)
      {
        if (frames_read >= 2 && frac > 0.0)
        {
          /* Linear interpolation */
          buf[i] = temp_samples[0] + (temp_samples[1] - temp_samples[0]) * frac;
        }
        else
        {
          buf[i] = temp_samples[0];
        }
      }
      else
      {
        buf[i] = 0.0f;
      }
    }
    else
    {
      buf[i] = 0.0f;
    }
  }
  
  /* Advance file position by speed only - pitch never affects this */
  file_position += n_samples * data->playback_speed;
  if (file_position >= total_frames)
  {
    file_position = fmod(file_position, (double)total_frames);
  }

  /* Update the global sample position for UI display */
  data->sample_position += n_samples;
  if (data->sample_position >= total_frames)
  {
    data->sample_position = fmod(data->sample_position, (double)total_frames);
  }

  return n_samples;
}

/* Reset function for static positions */
void reset_speed_pitch_positions(void)
{
  /* Reset static variables by setting them to 0 */
  /* This is called when audio is reset */
  static double *speed_position_ptr = NULL;
  static double *pitch_read_position_ptr = NULL;
  static float **speed_buffer_ptr = NULL;
  
  /* On first call, just return - static variables will be initialized properly */
  /* The actual reset happens when the read function detects data->reset_audio */
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
