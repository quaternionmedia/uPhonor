#include "uphonor.h"
#include "cli.c"
#include "process.c"

struct pw_filter_events filter_events = {
    PW_VERSION_FILTER_EVENTS,
    //  .state_changed = state_changed,
    .process = on_process,
};
int main(int argc, char *argv[])
{
   struct data data = {
       0,
   };
   /* Set up buffer parameters for audio */
   const struct spa_pod *params[1];
   uint8_t buffer[1024];
   struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

   int cli_status = cli(argc, argv, &data);
   if (cli_status != 0)
   {
      fprintf(stderr, "Error in command line interface: %d\n", cli_status);
      return cli_status;
   }

   /* We initialise libpipewire. This mainly reads some
      environment variables and initialises logging. */
   pw_init(NULL, NULL);

   /* Create the event loop. */
   data.loop = pw_main_loop_new(NULL);

   /* Add signal listeners to cleanly close the event loop and
      process when requested. */
   pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGINT,
                      do_quit, &data);
   pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGTERM,
                      do_quit, &data);

   /* Create a single filter that handles both audio and MIDI */
   data.filter = pw_filter_new_simple(
       pw_main_loop_get_loop(data.loop),
       "uPhonor",
       pw_properties_new(
           /* Set as a general media node that can handle both audio and MIDI */
           PW_KEY_MEDIA_TYPE, "Audio",
           PW_KEY_MEDIA_CATEGORY, "Duplex",
           PW_KEY_MEDIA_CLASS, "Audio/Duplex",
           PW_KEY_MEDIA_ROLE, "Music",
           PW_KEY_NODE_NAME, "uPhonor",
           //   PW_KEY_NODE_LATENCY, "1024/48000",
           //   PW_KEY_NODE_FORCE_QUANTUM, "1024",
           //   PW_KEY_NODE_RATE, "1/44100",
           //   PW_KEY_NODE_FORCE_RATE, "true",
           PW_KEY_NODE_NICK, "uPhonor",
           PW_KEY_NODE_DESCRIPTION, "Micro-phonor Loop manager",
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

   params[0] = spa_process_latency_build(&builder,
                                         SPA_PARAM_ProcessLatency,
                                         &SPA_PROCESS_LATENCY_INFO_INIT(
                                                 .ns = 10 * SPA_NSEC_PER_MSEC));

   if (pw_filter_connect(data.filter,
                         PW_FILTER_FLAG_RT_PROCESS,
                         params, 1) < 0)
   {
      fprintf(stderr, "can't connect\n");
      return -1;
   }

   pw_main_loop_run(data.loop);

   pw_filter_destroy(data.filter);
   pw_main_loop_destroy(data.loop);
   pw_deinit();
   // sf_close(data.file);

   return 0;
}

// void state_changed(void *userdata, enum pw_filter_state old,
//                    enum pw_filter_state state, const char *error)
// {
//    struct data *data = userdata;

//    switch (state)
//    {
//    case PW_FILTER_STATE_STREAMING:
//       /* reset playback position */
//       pw_log_info("start playback");
//       data->clock_id = SPA_ID_INVALID;
//       data->offset = 0;
//       data->position = 0;
//       break;
//    default:
//       break;
//    }
// }

/* do_quit gets called on SIGINT and SIGTERM, upon which we ask the
   event loop to quit. */
void do_quit(void *userdata, int signal_number)
{
   struct data *data = userdata;
   pw_main_loop_quit(data->loop);
}