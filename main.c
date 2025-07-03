#include "uphonor.h"
#include "cli_rubberband.c"
#include "pipe.c"
#include "process.c"
#include "audio_processing_rt.h"
#include "audio_buffer_rt.h"

struct pw_filter_events filter_events = {
    PW_VERSION_FILTER_EVENTS,
    .state_changed = state_changed,
    .param_changed = on_param_changed,

    .process = on_process,
    //  .process = tone,
};

int main(int argc, char *argv[])
{
  struct data data = {
      0,
  };

  // Initialize recording fields
  data.recording_enabled = false;
  data.record_file = NULL;
  data.record_filename = NULL;

  data.volume = 1.0f;         // Default volume level
  data.playback_speed = 1.0f; // Default normal speed
  data.sample_position = 0.0; // Initialize fractional sample position

  data.current_state = HOLO_STATE_IDLE;
  // Initialize performance buffers (add after data initialization)
  data.max_buffer_size = 2048 * 8; // Support up to 8 channels at 2048 samples
  data.silence_buffer = calloc(data.max_buffer_size, sizeof(float));
  data.temp_audio_buffer = malloc(data.max_buffer_size * sizeof(float));

  // Initialize RT/Non-RT bridge for performance-critical operations
  if (rt_nonrt_bridge_init(&data.rt_bridge,
                           65536, // 64K sample ring buffer (~1.3 seconds at 48kHz)
                           256    // 256 message queue slots
                           ) < 0)
  {
    fprintf(stderr, "Failed to initialize RT/Non-RT bridge\n");
    free(data.silence_buffer);
    free(data.temp_audio_buffer);
    return -1;
  }

  // Initialize audio buffer system for RT-optimized file reading
  if (audio_buffer_rt_init(&data.audio_buffer, 8) < 0) // Support up to 8 channels
  {
    fprintf(stderr, "Failed to initialize audio buffer system\n");
    rt_nonrt_bridge_destroy(&data.rt_bridge);
    free(data.silence_buffer);
    free(data.temp_audio_buffer);
    return -1;
  }

  // Initialize memory loop system (60 seconds max loop at 48kHz)
  if (init_memory_loop(&data, 60, 48000) < 0)
  {
    fprintf(stderr, "Failed to initialize memory loop system\n");
    audio_buffer_rt_cleanup(&data.audio_buffer);
    rt_nonrt_bridge_destroy(&data.rt_bridge);
    free(data.silence_buffer);
    free(data.temp_audio_buffer);
    return -1;
  }

  // Create recordings directory if it doesn't exist
  struct stat st = {0};
  if (stat("recordings", &st) == -1)
  {
    mkdir("recordings", 0755);
  }

  if (!data.silence_buffer || !data.temp_audio_buffer)
  {
    fprintf(stderr, "Failed to allocate audio buffers\n");
    return -1;
  }

  /* Initialize rubberband after we have format information */
  /* Note: We'll initialize this later when we have proper format info */
  data.rubberband_state = NULL;
  data.pitch_shift = 0.0f;
  data.rubberband_enabled = true;
  data.rubberband_input_buffer = NULL;
  data.rubberband_output_buffer = NULL;
  data.rubberband_buffer_size = 0;
  /* Set up buffer parameters for audio */
  const struct spa_pod *params[1];
  uint8_t buffer[1024];
  struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

  /* We initialise libpipewire. This mainly reads some
     environment variables and initialises logging. */
  pw_init(NULL, NULL);

  /* Create the event loop. */
  data.loop = pw_main_loop_new(NULL);
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

  /* Connect the context, which returns us a proxy to the core
     object. */
  data.core = pw_context_connect(context, NULL, 0);
  if (data.core == NULL)
  {
    perror("pw_context_connect() failed");
    return 1;
  }
  /* Add signal listeners to cleanly close the event loop and
     process when requested. */
  pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGINT,
                     do_quit, &data);
  pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGTERM,
                     do_quit, &data);
  char rate_str[64];
  snprintf(rate_str, sizeof(rate_str), "1/%u",
           data.fileinfo.samplerate);
  /* Create a single filter that handles both audio and MIDI */
  data.filter = pw_filter_new_simple(
      pw_main_loop_get_loop(data.loop),
      "uPhonor",
      pw_properties_new(
          /* Set as a general media node that can handle both audio and MIDI */
          PW_KEY_MEDIA_TYPE, "Audio",
          PW_KEY_MEDIA_CATEGORY, "Duplex",
          PW_KEY_MEDIA_CLASS, "Audio/Duplex",
          PW_KEY_MEDIA_ROLE, "DSP",
          PW_KEY_NODE_NAME, "uPhonor",
          //   PW_KEY_NODE_LATENCY, "1024/48000",
          //   PW_KEY_NODE_FORCE_QUANTUM, "1024",
          //   PW_KEY_NODE_RATE, "1/44100",
          //   PW_KEY_NODE_RATE, rate_str,
          //   PW_KEY_NODE_FORCE_RATE, "true",
          PW_KEY_NODE_NICK, "uPhonor",
          PW_KEY_NODE_DESCRIPTION, "Micro-phonor Loop manager",
          //   PW_KEY_NODE_AUTOCONNECT, "true",
          NULL),
      &filter_events,
      &data);

  /* Create the pw_stream. This does not add it to the graph. */
  data.audio_out = pw_filter_add_port(data.filter,
                                      PW_DIRECTION_OUTPUT,
                                      PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                                      sizeof(struct port),
                                      pw_properties_new(
                                          PW_KEY_FORMAT_DSP, "32 bit float mono audio",
                                          PW_KEY_PORT_NAME, "audio_output",
                                          //  PW_KEY_AUDIO_CHANNELS, "1",
                                          //  PW_KEY_NODE_CHANNELNAMES, "left",
                                          NULL),
                                      NULL, 0);
  data.audio_in = pw_filter_add_port(data.filter,
                                     PW_DIRECTION_INPUT,
                                     PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                                     sizeof(struct port),
                                     pw_properties_new(
                                         PW_KEY_FORMAT_DSP, "32 bit float mono audio",
                                         PW_KEY_PORT_NAME, "audio_input",
                                         NULL),
                                     NULL, 0);

  /* Add MIDI ports */
  data.midi_out = pw_filter_add_port(data.filter,
                                     PW_DIRECTION_OUTPUT,
                                     PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                                     sizeof(struct port),
                                     pw_properties_new(
                                         PW_KEY_FORMAT_DSP, "8 bit raw midi",
                                         PW_KEY_PORT_NAME, "midi_output",
                                         NULL),
                                     NULL, 0);

  data.midi_in = pw_filter_add_port(data.filter,
                                    PW_DIRECTION_INPUT,
                                    PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                                    sizeof(struct port),
                                    pw_properties_new(
                                        PW_KEY_FORMAT_DSP, "8 bit raw midi",
                                        PW_KEY_PORT_NAME, "midi_input",
                                        NULL),
                                    NULL, 0);

  // params[0] = spa_process_latency_build(&builder,
  //                                       SPA_PARAM_ProcessLatency,
  //                                       &SPA_PROCESS_LATENCY_INFO_INIT(
  //                                               .ns = 10 * SPA_NSEC_PER_MSEC));

  if (pw_filter_connect(data.filter,
                        PW_FILTER_FLAG_RT_PROCESS,
                        NULL,
                        0) < 0)
  {
    fprintf(stderr, "can't connect\n");
    return -1;
  }

  int cli_status = cli(argc, argv, &data);
  if (cli_status != 0)
  {
    if (cli_status == 1)
    {
      // Help was shown, exit normally
      return 0;
    }
    fprintf(stderr, "Error in command line interface: %d\n", cli_status);
    return cli_status;
  }

  pw_main_loop_run(data.loop);

  pw_filter_destroy(data.filter);
  pw_main_loop_destroy(data.loop);
  pw_deinit();
  sf_close(data.file);

  // Clean up recording resources
  if (data.recording_enabled)
  {
    stop_recording(&data);
  }

  // Free allocated filename string
  if (data.record_filename)
  {
    free(data.record_filename);
  }

  // Destroy RT/Non-RT bridge
  rt_nonrt_bridge_destroy(&data.rt_bridge);

  // Cleanup audio buffer system
  audio_buffer_rt_cleanup(&data.audio_buffer);

  // Cleanup memory loop system
  cleanup_memory_loop(&data);

  // Free performance buffers
  free(data.silence_buffer);
  free(data.temp_audio_buffer);

  // Clean up rubberband
  cleanup_rubberband(&data);

  return 0;
}

void state_changed(void *userdata, enum pw_filter_state old,
                   enum pw_filter_state state, const char *error)
{
  struct data *data = userdata;

  switch (state)
  {
  case PW_FILTER_STATE_STREAMING:
    /* reset playback position */
    pw_log_info("start playback");
    data->clock_id = SPA_ID_INVALID;
    data->offset = 0;
    data->position = 0;

    // Add some debugging for the ports
    pw_log_info("Filter is now streaming - audio_in: %p, audio_out: %p",
                data->audio_in, data->audio_out);
    break;
  default:
    pw_log_info("filter state changed from %d to %d: %s",
                old, state, error ? error : "no error");
    break;
  }
}

/* do_quit gets called on SIGINT and SIGTERM, upon which we ask the
   event loop to quit. */
void do_quit(void *userdata, int signal_number)
{
  struct data *data = userdata;
  pw_main_loop_quit(data->loop);
}