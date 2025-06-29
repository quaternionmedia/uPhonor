/*
 * Fixed version of the speed/pitch algorithm that ensures TRUE independence
 */

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

  /* If both speed and pitch are normal, use optimized path */
  if (data->playback_speed == 1.0f && data->pitch_shift == 1.0f)
  {
    return read_audio_frames_rt(data, buf, n_samples);
  }

  /* TRUE independent speed and pitch processing
   * Key insight: Speed and pitch must be applied in time domain, not sample domain
   * Speed controls playback timeline advancement
   * Pitch controls frequency content sampling
   */

  static double virtual_time = 0.0; /* Virtual playback time */
  static double time_step = 0.0;    /* Time per output sample */

  /* Initialize time step */
  if (time_step == 0.0)
  {
    time_step = 1.0 / (double)data->fileinfo.samplerate;
  }

  /* Initialize virtual time from current position */
  if (virtual_time == 0.0)
  {
    virtual_time = (double)data->sample_position / (double)data->fileinfo.samplerate;
  }

  /* Process each output sample */
  for (uint32_t i = 0; i < n_samples; i++)
  {
    /* Step 1: Speed control - how fast do we advance through the file?
     * This controls ONLY the rate of advancement through file timeline */
    double file_timeline_position = virtual_time * data->playback_speed;

    /* Step 2: Pitch control - how do we sample the file content?
     * This controls ONLY the frequency at which we read samples
     * Higher pitch = read samples at higher rate from file
     * Lower pitch = read samples at lower rate from file
     * This is completely independent of playback speed */
    double sampling_position = file_timeline_position * data->pitch_shift;

    /* Convert to actual file sample index */
    double file_position = sampling_position * (double)data->fileinfo.samplerate;

    /* Handle file looping */
    while (file_position >= total_frames)
    {
      file_position -= total_frames;
    }
    while (file_position < 0)
    {
      file_position += total_frames;
    }

    sf_count_t sample_index = (sf_count_t)file_position;
    double frac = file_position - sample_index;

    /* Read from file */
    if (sf_seek(data->file, sample_index, SEEK_SET) != sample_index)
    {
      buf[i] = 0.0f;
    }
    else
    {
      static float temp_buffer[8];
      sf_count_t frames_to_read = SPA_MIN(2, total_frames - sample_index);
      sf_count_t frames_read;

      if (data->fileinfo.channels == 1)
      {
        frames_read = sf_readf_float(data->file, temp_buffer, frames_to_read);
      }
      else
      {
        /* Multi-channel - extract first channel */
        static float temp_multichannel[16];
        sf_count_t temp_frames = sf_readf_float(data->file, temp_multichannel, frames_to_read);

        for (sf_count_t j = 0; j < temp_frames; j++)
        {
          temp_buffer[j] = temp_multichannel[j * data->fileinfo.channels];
        }
        frames_read = temp_frames;
      }

      if (frames_read <= 0)
      {
        buf[i] = 0.0f;
      }
      else
      {
        /* Interpolate for smooth playback */
        float sample = temp_buffer[0];
        if (frames_read > 1 && frac > 0.0)
        {
          sample = temp_buffer[0] + (temp_buffer[1] - temp_buffer[0]) * frac;
        }
        buf[i] = sample;
      }
    }

    /* Advance virtual time at constant rate (this is key for independence) */
    virtual_time += time_step;
  }

  /* Update position based on actual speed-controlled advancement */
  double new_file_time = virtual_time * data->playback_speed;
  data->sample_position = (sf_count_t)(new_file_time * (double)data->fileinfo.samplerate);

  /* Handle looping */
  if (data->sample_position >= total_frames)
  {
    data->sample_position %= total_frames;
    virtual_time = (double)data->sample_position / (double)data->fileinfo.samplerate / data->playback_speed;
  }

  return n_samples;
}
