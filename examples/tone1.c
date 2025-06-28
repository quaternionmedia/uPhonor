#include "uphonor.h"

/* do_quit gets called on SIGINT and SIGTERM, upon which we ask the
   event loop to quit. */
void do_quit(void *userdata, int signal_number)
{
  struct data *data = userdata;
  pw_main_loop_quit(data->loop);
}

static void on_param_changed(void *userdata, uint32_t id, const struct spa_pod *param)
{
  struct data *data = userdata;

  if (param == NULL || id != SPA_PARAM_Format)
    return;

  struct spa_audio_info info = {0};
  if (spa_format_parse(param, &info.media_type, &info.media_subtype) < 0)
    return;

  if (info.media_type != SPA_MEDIA_TYPE_audio ||
      info.media_subtype != SPA_MEDIA_SUBTYPE_raw)
    return;

  spa_format_audio_raw_parse(param, &info.info.raw);

  pw_log_info("Negotiated format:");
  // pw_log_info("  Format: %s", spa_debug_type_find_name(spa_audio_format, info.info.raw.format));
  pw_log_info("  Channels: %u", info.info.raw.channels);
  pw_log_info("  Rate: %u", info.info.raw.rate);
}

int main(int argc, char **argv)
{
  struct data data = {
      0,
  };

  /* Enable more logging */
  // pw_log_set_level(SPA_LOG_LEVEL_INFO);

  /* For sine wave test, set up fake file info with system defaults */
  memset(&data.fileinfo, 0, sizeof(data.fileinfo));
  data.fileinfo.samplerate = 48000;
  data.fileinfo.channels = 1; // Let's try mono to match typical system setup
  data.file = NULL;

  /* We initialise libpipewire. */
  pw_init(NULL, NULL);

  /* Create the event loop. */
  data.loop = pw_main_loop_new(NULL);

  /* Create the context. */
  struct pw_context *context = pw_context_new(
      pw_main_loop_get_loop(data.loop),
      pw_properties_new(
          PW_KEY_CONFIG_NAME, "client.conf",
          NULL),
      0);
  if (context == NULL)
  {
    perror("pw_context_new() failed");
    return 1;
  }

  /* Connect the context. */
  data.core = pw_context_connect(context, NULL, 0);
  if (data.core == NULL)
  {
    perror("pw_context_connect() failed");
    return 1;
  }

  /* Add signal listeners. */
  pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGINT,
                     do_quit, &data);
  pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGTERM,
                     do_quit, &data);

  const struct pw_filter_events filter_events = {
      PW_VERSION_FILTER_EVENTS,
      .state_changed = state_changed,
      .process = simple_process,
      .param_changed = on_param_changed, // Add this to see negotiated format
  };

  /* Create filter with AUTOCONNECT like a stream */
  data.filter = pw_filter_new(
      data.core,
      "uPhonor-Test",
      pw_properties_new(
          PW_KEY_MEDIA_TYPE, "Audio",
          PW_KEY_MEDIA_CATEGORY, "Playback",
          PW_KEY_MEDIA_ROLE, "Music",
          PW_KEY_NODE_NAME, "uPhonor-Test",
          PW_KEY_NODE_LATENCY, "1024/48000",
          NULL));

  /* Add event listener */
  struct spa_hook listener;
  pw_filter_add_listener(data.filter, &listener, &filter_events, &data);

  /* Create the audio output port */
  data.audio_out = pw_filter_add_port(data.filter,
                                      PW_DIRECTION_OUTPUT,
                                      PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                                      sizeof(struct port),
                                      pw_properties_new(
                                          PW_KEY_FORMAT_DSP, "32 bit float mono audio",
                                          PW_KEY_PORT_NAME, "audio_output",
                                          NULL),
                                      NULL, 0);

  /* Set up buffer parameters with MULTIPLE format options */
  uint8_t buffer[1024];
  struct spa_pod_builder builder;
  const struct spa_pod *params[3];

  spa_pod_builder_init(&builder, buffer, sizeof(buffer));

  // Reset builder for next param
  spa_pod_builder_init(&builder, buffer, sizeof(buffer));

  params[0] = spa_format_audio_raw_build(&builder,
                                         SPA_PARAM_EnumFormat,
                                         &SPA_AUDIO_INFO_RAW_INIT(
                                                 .format = SPA_AUDIO_FORMAT_F32_LE,
                                                 .channels = 1,
                                                 .rate = 48000));

  pw_filter_update_params(data.filter, data.audio_out, params, 1);

  if (pw_filter_connect(data.filter,
                        PW_FILTER_FLAG_RT_PROCESS,
                        NULL, 0) < 0)
  {
    fprintf(stderr, "can't connect\n");
    return -1;
  }

  /* Start the event loop. */
  pw_main_loop_run(data.loop);

  /* Cleanup */
  spa_hook_remove(&listener);
  pw_filter_destroy(data.filter);
  pw_context_destroy(context);
  pw_main_loop_destroy(data.loop);
  pw_deinit();

  return 0;
}

void state_changed(void *userdata, enum pw_filter_state old,
                   enum pw_filter_state state, const char *error)
{
  struct data *data = userdata;

  switch (state)
  {
  case PW_FILTER_STATE_STREAMING:
    pw_log_info("Filter started streaming");
    data->clock_id = SPA_ID_INVALID;
    data->offset = 0;
    data->position = 0;
    break;
  case PW_FILTER_STATE_ERROR:
    pw_log_error("Filter error: %s", error ? error : "unknown");
    break;
  default:
    pw_log_info("Filter state changed to %d", state);
    break;
  }
}

static double phase = 0.0;
static uint32_t actual_channels = 0;
static uint32_t actual_rate = 0;

void simple_process(void *userdata, struct spa_io_position *position)
{
  struct data *data = userdata;
  struct pw_buffer *b;

  if ((b = pw_filter_dequeue_buffer(data->audio_out)) == NULL)
    return;

  float *buf = b->buffer->datas[0].data;
  if (buf == NULL)
  {
    pw_filter_queue_buffer(data->audio_out, b);
    return;
  }

  /* Try to detect the actual format from the buffer */
  if (actual_channels == 0)
  {
    // Make an educated guess based on buffer size
    uint32_t total_samples = b->buffer->datas[0].maxsize / sizeof(float);

    actual_channels = 1;

    pw_log_info("Detected buffer: %u total samples, assuming %u channels",
                total_samples, actual_channels);
  }

  /* Get sample rate from position */
  if (position && position->clock.rate.denom > 0)
  {
    uint32_t detected_rate = position->clock.rate.denom / position->clock.rate.num;
    if (actual_rate != detected_rate)
    {
      actual_rate = detected_rate;
      pw_log_info("Sample rate: %u Hz", actual_rate);
    }
  }
  else
  {
    actual_rate = 48000; // fallback
  }

  uint32_t stride = sizeof(float) * actual_channels;
  uint32_t n_frames = b->buffer->datas[0].maxsize / stride;
  if (b->requested)
    n_frames = SPA_MIN(n_frames, b->requested);

  /* Generate clean 440Hz sine wave */
  double freq = 440.0;
  double sample_rate = (double)actual_rate;

  for (uint32_t i = 0; i < n_frames; i++)
  {
    float sample = (float)(sin(phase) * 0.3);

    /* Fill all channels */
    for (uint32_t ch = 0; ch < actual_channels; ch++)
    {
      buf[i * actual_channels] = sample;
    }

    phase += 2.0 * M_PI * freq / sample_rate;
    if (phase >= 2.0 * M_PI)
      phase -= 2.0 * M_PI;
  }

  /* Set buffer metadata */
  b->buffer->datas[0].chunk->offset = 0;
  b->buffer->datas[0].chunk->stride = stride;
  b->buffer->datas[0].chunk->size = n_frames * stride;

  pw_filter_queue_buffer(data->audio_out, b);
}