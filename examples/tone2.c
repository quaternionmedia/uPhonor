#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <pipewire/pipewire.h>
#include <pipewire/filter.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>

struct data
{
  struct pw_main_loop *loop;
  struct pw_core *core;
  struct pw_filter *filter;
  struct pw_filter_port *audio_out;

  double phase;
  uint32_t sample_rate;
  uint32_t channels;
  bool format_negotiated;
  uint64_t process_count;
};

/* Signal handler to quit cleanly */
static void do_quit(void *userdata, int signal_number)
{
  struct data *data = userdata;
  pw_main_loop_quit(data->loop);
}

/* Called when parameters change - important for format negotiation */
static void on_param_changed(void *userdata, uint32_t id, const struct spa_pod *param)
{
  struct data *data = userdata;

  printf("Parameter changed: id=%u\n", id);

  if (param == NULL || id != SPA_PARAM_Format)
    return;

  struct spa_audio_info info = {0};
  if (spa_format_parse(param, &info.media_type, &info.media_subtype) < 0)
  {
    printf("Failed to parse format\n");
    return;
  }

  printf("Media type: %u, subtype: %u\n", info.media_type, info.media_subtype);

  if (info.media_type != SPA_MEDIA_TYPE_audio ||
      info.media_subtype != SPA_MEDIA_SUBTYPE_raw)
  {
    printf("Not raw audio format\n");
    return;
  }

  spa_format_audio_raw_parse(param, &info.info.raw);

  data->sample_rate = info.info.raw.rate;
  data->channels = info.info.raw.channels;
  data->format_negotiated = true;

  printf("Format negotiated: %u Hz, %u channels, format: %u\n",
         data->sample_rate, data->channels, info.info.raw.format);
}

/* Called when filter state changes */
static void on_state_changed(void *userdata, enum pw_filter_state old,
                             enum pw_filter_state state, const char *error)
{
  struct data *data = userdata;
  printf("State changed from %d to %d\n", old, state);

  switch (state)
  {
  case PW_FILTER_STATE_STREAMING:
    printf("Filter started streaming\n");
    data->process_count = 0;
    /* Force format negotiation if it hasn't happened yet */
    if (!data->format_negotiated)
    {
      printf("Format not negotiated, using defaults\n");
      data->sample_rate = 48000;
      data->channels = 1; /* Default to mono */
      data->format_negotiated = true;
    }
    break;
  case PW_FILTER_STATE_ERROR:
    printf("Filter error: %s\n", error ? error : "unknown");
    break;
  case PW_FILTER_STATE_PAUSED:
    printf("Filter paused\n");
    break;
  case PW_FILTER_STATE_CONNECTING:
    printf("Filter connecting\n");
    break;
  default:
    printf("Filter state: %d\n", state);
    break;
  }
}

/* Audio processing callback - generates 440Hz sine wave */
static void on_process(void *userdata, struct spa_io_position *position)
{
  struct data *data = userdata;
  struct pw_buffer *b;
  float *samples;
  uint32_t n_frames;

  data->process_count++;

  /* Log every 1000 process calls to verify we're being called */
  if (data->process_count % 1000 == 0)
  {
    printf("Process callback #%lu, format_negotiated: %s\n",
           data->process_count, data->format_negotiated ? "yes" : "no");
  }

  /* Get the next buffer */
  if ((b = pw_filter_dequeue_buffer(data->audio_out)) == NULL)
  {
    if (data->process_count % 1000 == 0)
    {
      printf("No buffer available\n");
    }
    return;
  }

  /* Get pointer to the audio data */
  samples = b->buffer->datas[0].data;
  if (samples == NULL)
  {
    printf("No sample data in buffer\n");
    pw_filter_queue_buffer(data->audio_out, b);
    return;
  }

  /* Try to detect format from buffer if not negotiated */
  if (!data->format_negotiated)
  {
    /* Make reasonable assumptions */
    data->sample_rate = 48000;
    data->channels = 1;
    data->format_negotiated = true;
    printf("Auto-detected format: %u Hz, %u channels\n",
           data->sample_rate, data->channels);
  }

  /* Calculate number of frames correctly */
  uint32_t stride = sizeof(float) * data->channels;
  n_frames = b->buffer->datas[0].maxsize / stride;

  /* Respect requested buffer size if specified */
  if (b->requested && b->requested < n_frames)
  {
    n_frames = b->requested;
  }

  if (data->process_count % 1000 == 0)
  {
    printf("Processing %u frames, stride: %u, maxsize: %u\n",
           n_frames, stride, b->buffer->datas[0].maxsize);
  }

  /* Generate 440Hz sine wave */
  const double freq = 440.0;
  const double amplitude = 0.5; /* 50% amplitude */
  const double sample_rate = (double)data->sample_rate;
  const double phase_increment = 2.0 * M_PI * freq / sample_rate;

  for (uint32_t i = 0; i < n_frames; i++)
  {
    float sample = (float)(sin(data->phase) * amplitude);

    /* Fill all channels with the same sample */
    for (uint32_t ch = 0; ch < data->channels; ch++)
    {
      samples[i * data->channels + ch] = sample;
    }

    /* Increment phase */
    data->phase += phase_increment;

    /* Keep phase in reasonable range to avoid precision loss */
    if (data->phase >= 2.0 * M_PI)
    {
      data->phase -= 2.0 * M_PI;
    }
  }

  /* Set buffer metadata correctly */
  b->buffer->datas[0].chunk->offset = 0;
  b->buffer->datas[0].chunk->stride = stride;
  b->buffer->datas[0].chunk->size = n_frames * stride;

  /* Queue the buffer back */
  pw_filter_queue_buffer(data->audio_out, b);
}

/* Filter event callbacks */
static const struct pw_filter_events filter_events = {
    PW_VERSION_FILTER_EVENTS,
    .state_changed = on_state_changed,
    .process = on_process,
    .param_changed = on_param_changed,
};

int main(int argc, char **argv)
{
  struct data data = {0};
  data.phase = 0.0;
  data.sample_rate = 48000; /* Default, will be updated during negotiation */
  data.channels = 1;        /* Default stereo */
  data.format_negotiated = false;
  data.process_count = 0;

  /* Enable more verbose logging */
  pw_log_set_level(SPA_LOG_LEVEL_INFO);

  /* Initialize PipeWire */
  pw_init(NULL, NULL);

  /* Create main loop */
  data.loop = pw_main_loop_new(NULL);
  if (data.loop == NULL)
  {
    fprintf(stderr, "Failed to create main loop\n");
    return 1;
  }

  /* Create context */
  struct pw_context *context = pw_context_new(
      pw_main_loop_get_loop(data.loop),
      pw_properties_new(
          PW_KEY_CONFIG_NAME, "client.conf",
          NULL),
      0);
  if (context == NULL)
  {
    fprintf(stderr, "Failed to create context\n");
    return 1;
  }

  /* Connect to PipeWire */
  data.core = pw_context_connect(context, NULL, 0);
  if (data.core == NULL)
  {
    fprintf(stderr, "Failed to connect to PipeWire\n");
    return 1;
  }

  /* Add signal handlers */
  pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGINT, do_quit, &data);
  pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGTERM, do_quit, &data);

  /* Create filter */
  data.filter = pw_filter_new(
      data.core,
      "Simple Tone Generator",
      pw_properties_new(
          PW_KEY_MEDIA_TYPE, "Audio",
          PW_KEY_MEDIA_CATEGORY, "Playback",
          PW_KEY_MEDIA_ROLE, "Music",
          PW_KEY_NODE_NAME, "simple-tone-generator",
          // PW_KEY_NODE_LATENCY, "1024/48000",
          PW_KEY_NODE_FORCE_QUANTUM, "1024",
          PW_KEY_NODE_RATE, "1/48000",
          PW_KEY_NODE_FORCE_RATE, "true",
          NULL));
  if (data.filter == NULL)
  {
    fprintf(stderr, "Failed to create filter\n");
    return 1;
  }

  /* Add event listener */
  struct spa_hook listener;
  pw_filter_add_listener(data.filter, &listener, &filter_events, &data);

  /* Create audio output port */
  data.audio_out = pw_filter_add_port(data.filter,
                                      PW_DIRECTION_OUTPUT,
                                      PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                                      0,
                                      pw_properties_new(
                                          PW_KEY_FORMAT_DSP, "32 bit float mono audio",
                                          PW_KEY_PORT_NAME, "output",
                                          NULL),
                                      NULL, 0);
  if (data.audio_out == NULL)
  {
    fprintf(stderr, "Failed to create output port\n");
    return 1;
  }

  /* Set up audio format parameters - use a simpler approach */
  uint8_t buffer[1024];
  struct spa_pod_builder builder;
  const struct spa_pod *params[1];

  spa_pod_builder_init(&builder, buffer, sizeof(buffer));

  /* Just offer stereo format to keep it simple */
  params[0] = spa_format_audio_raw_build(&builder,
                                         SPA_PARAM_EnumFormat,
                                         &SPA_AUDIO_INFO_RAW_INIT(
                                                 .format = SPA_AUDIO_FORMAT_F32_LE,
                                                 .channels = 1,
                                                 .rate = 48000));

  printf("Setting up format parameters\n");

  /* Update port parameters */
  pw_filter_update_params(data.filter, data.audio_out, params, 1);

  printf("Connecting filter\n");

  /* Connect the filter */
  if (pw_filter_connect(data.filter, PW_FILTER_FLAG_RT_PROCESS, NULL, 0) < 0)
  {
    fprintf(stderr, "Failed to connect filter\n");
    return 1;
  }

  printf("Playing 440Hz tone. Press Ctrl+C to stop.\n");
  printf("Connect the 'simple-tone-generator' output to a sink to hear audio.\n");

  /* Run the main loop */
  pw_main_loop_run(data.loop);

  /* Cleanup */
  spa_hook_remove(&listener);
  pw_filter_destroy(data.filter);
  pw_context_destroy(context);
  pw_main_loop_destroy(data.loop);
  pw_deinit();

  printf("Goodbye! Processed %lu audio buffers.\n", data.process_count);
  return 0;
}