#include "uphonor.h"
#include "process-midi.c"
/* do_quit gets called on SIGINT and SIGTERM, upon which we ask the
   event loop to quit. */
void do_quit(void *userdata, int signal_number)
{
   struct data *data = userdata;
   pw_main_loop_quit(data->loop);
}

int main(int argc, char **argv)
{
   struct data data = {
       0,
   };

   /* A single argument: path to the audio file. */
   if (argc < 2)
   {
      fprintf(stderr,
              "expected an argument: the file to open\n");
      return 1;
   }
   /* If a second argument is given, it is the volume level. */
   if (argc == 3)
   {
      if (sscanf(argv[2], "%f", &data.volume) != 1)
      {
         fprintf(stderr, "invalid volume level: %s\n", argv[2]);
         return 1;
      }
   }
   else
   {
      /* Default volume level is 1.0 */
      data.volume = 1.0f;
   }

   /* We initialise libsndfile, the library we'll use to convert
      the audio file's content into interlaced float samples. */
   memset(&data.fileinfo, 0, sizeof(data.fileinfo));
   data.file = sf_open(argv[1], SFM_READ, &data.fileinfo);
   if (data.file == NULL)
   {
      fprintf(stderr, "file opening error: %s\n",
              sf_strerror(NULL));
      return 1;
   }

   /* We initialise libpipewire. This mainly reads some
      environment variables and initialises logging. */
   pw_init(NULL, NULL);

   /* Create the event loop. */
   data.loop = pw_main_loop_new(NULL);

   /* Create the context. This is the main interface we'll use to
      interact with PipeWire. It parses the appropriate
      configuration file, loads PipeWire modules declared in the
      config and registers event sources to the event loop. */
   struct pw_context *context = pw_context_new(
       pw_main_loop_get_loop(data.loop),
       pw_properties_new(
           /* Explicity ask for the realtime configuration. */
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
   struct pw_filter_events filter_events = {
       .state_changed = state_changed,
       .process = on_process,
   };

   /* Create a single filter that handles both audio and MIDI */
   data.filter = pw_filter_new_simple(
       pw_main_loop_get_loop(data.loop),
       "uPhonor",
       pw_properties_new(
           /* Set as a general media node that can handle both audio and MIDI */
           PW_KEY_MEDIA_TYPE, "Audio",
           PW_KEY_MEDIA_CATEGORY, "Playback",
           PW_KEY_MEDIA_ROLE, "Music",
           PW_KEY_NODE_NAME, "uPhonor",
           NULL),
       &filter_events,
       &data);
   /* Initialise a string that will be used as a property to the
      stream. We request a specific sample rate, the one found in
      the opened file. Note that the sample rate will not be
      enforced: see PW_KEY_NODE_FORCE_RATE for that. */
   char rate_str[64];
   snprintf(rate_str, sizeof(rate_str), "1/%u",
            data.fileinfo.samplerate);

   /* Create the pw_stream. This does not add it to the graph. */
   data.audio_out = pw_filter_add_port(data.filter,
                                       PW_DIRECTION_OUTPUT,
                                       PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                                       sizeof(struct port),
                                       pw_properties_new(
                                           PW_KEY_FORMAT_DSP, "32 bit float mono audio",
                                           PW_KEY_PORT_NAME, "audio_output",
                                           PW_KEY_AUDIO_CHANNELS, "2", // or however many channels you need
                                           PW_KEY_AUDIO_RATE, rate_str,
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

   /* Set up buffer parameters for audio */
   uint8_t buffer[1024];
   struct spa_pod_builder builder;
   const struct spa_pod *params[2];

   spa_pod_builder_init(&builder, buffer, sizeof(buffer));

   /* Audio format parameters */
   params[0] = spa_format_audio_raw_build(&builder,
                                          SPA_PARAM_EnumFormat,
                                          &SPA_AUDIO_INFO_RAW_INIT(
                                                  .format = SPA_AUDIO_FORMAT_F32,
                                                  .channels = data.fileinfo.channels,
                                                  .rate = data.fileinfo.samplerate));

   pw_filter_update_params(data.filter, data.audio_out,
                           (const struct spa_pod **)params, 1);

   /* MIDI buffer parameters */
   // spa_pod_builder_init(&builder, buffer, sizeof(buffer));
   // params[0] = spa_pod_builder_add_object(&builder,
   //                                        SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
   //                                        SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(1, 1, 32),
   //                                        SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
   //                                        SPA_PARAM_BUFFERS_size, SPA_POD_CHOICE_RANGE_Int(4096, 4096, INT32_MAX),
   //                                        SPA_PARAM_BUFFERS_stride, SPA_POD_Int(1));

   // pw_filter_update_params(data.filter, data.midi_out,
   //                         (const struct spa_pod **)params, 1);
   // pw_filter_update_params(data.filter, data.midi_in,
   //                         (const struct spa_pod **)params, 1);

   /* Now connect this filter. We ask that our process function is
    * called in a realtime thread. */
   if (pw_filter_connect(data.filter,
                         PW_FILTER_FLAG_RT_PROCESS,
                         NULL, 0) < 0)
   {
      fprintf(stderr, "can't connect\n");
      return -1;
   }

   /* We start the event loop. Underlying to this is an epoll call
      that listens on an eventfd. In this example, the process
      gets woken up regularly to evaluate the process event
      handler. */
   pw_main_loop_run(data.loop);

   /* pw_main_loop_run returns when the event loop has been asked
      to quit, using pw_main_loop_quit. */
   pw_filter_destroy(data.filter);
   // pw_stream_destroy(data.audio_out);
   // spa_hook_remove(&event_listener);
   pw_context_destroy(context);
   pw_main_loop_destroy(data.loop);
   pw_deinit();
   sf_close(data.file);

   return 0;
}
