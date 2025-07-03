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

    /* Read audio from this loop using RT-optimized buffering */
    float temp_buf[n_samples];
    sf_count_t frames_read = audio_buffer_rt_read(&loop->audio_buffer,
                                                  loop->file,
                                                  &loop->fileinfo,
                                                  temp_buf,
                                                  n_samples);

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

  /* Update rubberband parameters if they changed - but be gentle about it */
  static float last_speed = 1.0f;
  static float last_pitch = 0.0f;
  static int stable_counter = 0;
  bool params_changed = false;

  /* Only update if values have actually changed */
  if (fabsf(data->playback_speed - last_speed) > 0.001f)
  {
    /* Set time ratio for speed changes (inverse of playback speed) */
    double time_ratio = 1.0 / data->playback_speed;
    rubberband_set_time_ratio(data->rubberband_state, time_ratio);
    params_changed = true;
    last_speed = data->playback_speed;
    stable_counter = 0;
  }

  if (fabsf(data->pitch_shift - last_pitch) > 0.01f)
  {
    /* Set pitch scale for pitch changes */
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
    stable_counter = 0;
  }

  /* Only reset if we have major parameter changes and the system has been unstable */
  if (params_changed && stable_counter == 0)
  {
    /* Use a much gentler approach - study the output for a few frames first */
    stable_counter = 1; /* Start stability counter */
  }
  else if (!params_changed)
  {
    stable_counter++;
  }

  /* Process the input through rubberband */
  const float *input_ptr = buffer;
  rubberband_process(data->rubberband_state, &input_ptr, n_samples, 0);

  /* Get available output */
  int available = rubberband_available(data->rubberband_state);
  
  /* In case we don't have enough output, try to get some by processing a bit more */
  if (available < (int)(n_samples * 0.8f) && available < (int)n_samples)
  {
    /* Feed a bit more silence to encourage output */
    static float silence_chunk[64] = {0};
    const float *silence_ptr = silence_chunk;
    rubberband_process(data->rubberband_state, &silence_ptr, 64, 0);
    available = rubberband_available(data->rubberband_state);
  }

  if (available > 0)
  {
    /* Calculate how much to retrieve, but be conservative */
    uint32_t to_retrieve = (uint32_t)available;
    
    /* Don't retrieve more than we can handle */
    if (to_retrieve > n_samples)
    {
      to_retrieve = n_samples;
    }
    
    if (to_retrieve > data->rubberband_buffer_size)
    {
      to_retrieve = data->rubberband_buffer_size;
    }

    /* Retrieve processed samples */
    float *output_ptr = data->rubberband_output_buffer;
    size_t retrieved = rubberband_retrieve(data->rubberband_state, &output_ptr, to_retrieve);

    if (retrieved > 0)
    {
      /* Copy processed samples back to the original buffer */
      memcpy(buffer, data->rubberband_output_buffer, retrieved * sizeof(float));
      
      /* If we got less than expected, pad with silence rather than leaving garbage */
      if (retrieved < n_samples)
      {
        memset(&buffer[retrieved], 0, (n_samples - retrieved) * sizeof(float));
      }
      
      return (uint32_t)retrieved;
    }
  }

  /* If we get here, rubberband didn't produce output - return original samples */
  return n_samples;
}
