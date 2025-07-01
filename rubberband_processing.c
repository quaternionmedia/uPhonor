#include "uphonor.h"
#include <rubberband/rubberband-c.h>

int init_rubberband(struct data *data)
{
  if (!data)
  {
    return -1;
  }

  /* Get sample rate from format info */
  uint32_t sample_rate = data->format.info.raw.rate > 0 ? data->format.info.raw.rate : 48000;

  /* Create rubberband state for realtime processing with ultra-low latency and maximum responsiveness */
  data->rubberband_state = rubberband_new(
      sample_rate,                          /* sample rate */
      1,                                    /* channels (mono) */
      RubberBandOptionProcessRealTime |     /* realtime processing */
          RubberBandOptionTransientsCrisp | /* crisp transients for quick response */
          RubberBandOptionThreadingNever |  /* no threading in RT context */
          RubberBandOptionWindowShort |     /* shorter analysis window for lower latency */
          RubberBandOptionFormantShifted |  /* allow formant shifting for better responsiveness */
          RubberBandOptionSmoothingOff |    /* disable smoothing for lower latency */
          RubberBandOptionPhaseIndependent | /* reduce phase artifacts that can cause latency */
          RubberBandOptionPitchHighSpeed |  /* fast pitch processing for immediate response */
          RubberBandOptionEngineFaster |    /* use faster engine for lower latency */
          RubberBandOptionDetectorPercussive, /* percussive detector for quick parameter changes */
      1.0,                                  /* initial time ratio (no speed change) */
      1.0                                   /* initial pitch scale (no pitch change) */
  );

  if (!data->rubberband_state)
  {
    return -1;
  }

  /* Set maximum process size to very small chunks for immediate responsiveness */
  rubberband_set_max_process_size(data->rubberband_state, 256);

  /* Set up buffer sizes - use very small buffers for maximum responsiveness */
  /* Use minimal buffer size for rubberband to reduce latency to absolute minimum */
  data->rubberband_buffer_size = 256; /* Even smaller buffer size for ultra-low latency */
  if (data->max_buffer_size > 0 && data->max_buffer_size < 256)
  {
    data->rubberband_buffer_size = data->max_buffer_size;
  }

  /* Allocate input and output buffers */
  data->rubberband_input_buffer = malloc(data->rubberband_buffer_size * sizeof(float));
  data->rubberband_output_buffer = malloc(data->rubberband_buffer_size * sizeof(float));

  if (!data->rubberband_input_buffer || !data->rubberband_output_buffer)
  {
    cleanup_rubberband(data);
    return -1;
  }

  /* Initialize default values */
  data->pitch_shift = 0.0f; /* No pitch shift initially */
  /* Note: Do NOT reset rubberband_enabled here - it should be preserved from CLI settings */

  return 0;
}

void cleanup_rubberband(struct data *data)
{
  if (!data)
  {
    return;
  }

  if (data->rubberband_state)
  {
    rubberband_delete(data->rubberband_state);
    data->rubberband_state = NULL;
  }

  if (data->rubberband_input_buffer)
  {
    free(data->rubberband_input_buffer);
    data->rubberband_input_buffer = NULL;
  }

  if (data->rubberband_output_buffer)
  {
    free(data->rubberband_output_buffer);
    data->rubberband_output_buffer = NULL;
  }
}

void rubberband_reset_data(struct data *data)
{
  if (!data || !data->rubberband_state)
  {
    return;
  }

  rubberband_reset(data->rubberband_state);
}

void set_pitch_shift(struct data *data, float semitones)
{
  if (!data)
  {
    return;
  }

  data->pitch_shift = semitones;

  if (data->rubberband_state)
  {
    if (semitones == 0.0f)
    {
      /* Explicitly set pitch scale to 1.0 for no pitch shift */
      rubberband_set_pitch_scale(data->rubberband_state, 1.0);
    }
    else
    {
      /* Convert semitones to pitch scale (2^(semitones/12)) */
      float pitch_scale = powf(2.0f, semitones / 12.0f);
      rubberband_set_pitch_scale(data->rubberband_state, pitch_scale);
    }
  }
}

void set_rubberband_enabled(struct data *data, bool enabled)
{
  if (!data)
  {
    return;
  }

  bool was_enabled = data->rubberband_enabled;
  data->rubberband_enabled = enabled;

  /* If we're enabling rubberband, reset it and configure parameters */
  if (enabled && !was_enabled && data->rubberband_state)
  {
    rubberband_reset(data->rubberband_state);

    /* Set time ratio for speed changes (inverse of playback speed) */
    rubberband_set_time_ratio(data->rubberband_state, 1.0 / data->playback_speed);

    /* Set pitch scale for pitch shifts (separate from speed) */
    float pitch_scale = powf(2.0f, data->pitch_shift / 12.0f);
    rubberband_set_pitch_scale(data->rubberband_state, pitch_scale);

    /* For speed-only changes, ensure pitch scale is exactly 1.0 to preserve pitch */
    if (data->pitch_shift == 0.0f && data->playback_speed != 1.0f)
    {
      rubberband_set_pitch_scale(data->rubberband_state, 1.0);
    }
  }
}
