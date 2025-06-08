#include "uphonor.h"
#include "process-midi.c"

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
          PW_KEY_CONFIG_NAME, "client-rt.conf",
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

  /* Initialise a string that will be used as a property to the
     stream. We request a specific sample rate, the one found in
     the opened file. Note that the sample rate will not be
     enforced: see PW_KEY_NODE_FORCE_RATE for that. */
  char rate_str[64];
  snprintf(rate_str, sizeof(rate_str), "1/%u",
           data.fileinfo.samplerate);

  /* Create the pw_stream. This does not add it to the graph. */
  data.stream = pw_stream_new(
      data.core, /* Core proxy. */
      argv[1],   /* Media name associated with the stream, which
                    is different to the node name. */
      pw_properties_new(
          /* Those describe the node type and are required to
             allow the session manager to auto-connect us to a
             sink node. */
          PW_KEY_MEDIA_TYPE, "Audio",
          PW_KEY_MEDIA_CATEGORY, "Playback",
          PW_KEY_MEDIA_ROLE, "Music",

          /* Our node name. */
          PW_KEY_NODE_NAME, "Audio source",

          PW_KEY_NODE_RATE, rate_str,
          NULL));

  /* Register event callbacks. stream_events is a struct with
     function pointers to the callbacks. The most important one
     is `process`, which is called to generate samples. We'll
     see its implementation later on. */
  struct spa_hook event_listener;
  pw_stream_add_listener(data.stream, &event_listener,
                         &stream_events, &data);

  /* This is the stream's mechanism to define the list of
     supported formats. A format is specified by the samples
     format(32-bit floats, unsigned 8-bit integers, etc.), the
     sample rate, the channel number and their positions. Here,
     we define a single format that matches what we read from
     from the file for the sample rate and the channel number,
     and we use a float format for samples, regardless of what
     the file contains. */
  const struct spa_pod *params[1];
  uint8_t buffer[1024];
  struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer,
                                                        sizeof(buffer));
  params[0] = spa_format_audio_raw_build(&builder,
                                         SPA_PARAM_EnumFormat,
                                         &SPA_AUDIO_INFO_RAW_INIT(
                                                 .format = SPA_AUDIO_FORMAT_F32,
                                                 .channels = data.fileinfo.channels,
                                                 .rate = data.fileinfo.samplerate));

  /* This starts by calling pw_context_connect if it wasn't
     called, then it creates the node object, exports it and
     creates its ports. This makes the node appear in the graph,
     and it can then be detected by the session manager that is
     responsible for establishing the links from this node's
     output ports to input ports elsewhere in the graph (if it
     can).

     The third parameter indicates a target node identifier.

     The fourth parameter is a list of flags:
      - we ask the session manager to auto-connect us to a sink;
      - we want to automatically memory-map the memfd buffers;
      - we want to run the process event callback in the realtime
        thread rather than in the main thread.
     */
  pw_stream_connect(data.stream,
                    PW_DIRECTION_OUTPUT,
                    PW_ID_ANY,
                    PW_STREAM_FLAG_AUTOCONNECT |
                        PW_STREAM_FLAG_MAP_BUFFERS |
                        PW_STREAM_FLAG_RT_PROCESS,
                    params, 1);

  /* Create a simple filter, the simple filter manages the core and remote
   * objects for you if you don't need to deal with them.
   *
   * Pass your events and a user_data pointer as the last arguments. This
   * will inform you about the filter state. The most important event
   * you need to listen to is the process event where you need to process
   * the data.
   */
  data.filter = pw_filter_new_simple(
      pw_main_loop_get_loop(data.loop),
      "midiphonor",
      pw_properties_new(
          PW_KEY_MEDIA_TYPE, "Midi",
          PW_KEY_MEDIA_CATEGORY, "Playback",
          PW_KEY_MEDIA_CLASS, "Midi/Source",
          NULL),
      &filter_events,
      &data);

  /* Make a midi output port */
  data.midi_out = pw_filter_add_port(data.filter,
                                     PW_DIRECTION_OUTPUT,
                                     PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                                     sizeof(struct port),
                                     pw_properties_new(
                                         PW_KEY_FORMAT_DSP, "8 bit raw midi",
                                         PW_KEY_PORT_NAME, "output",
                                         NULL),
                                     NULL, 0);
  data.midi_in = pw_filter_add_port(data.filter,
                                    PW_DIRECTION_INPUT,
                                    PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                                    sizeof(struct port),
                                    pw_properties_new(
                                        PW_KEY_FORMAT_DSP, "8 bit raw midi",
                                        PW_KEY_PORT_NAME, "input",
                                        NULL),
                                    NULL, 0);

  /* Update SPA_PARAM_Buffers to request a specific sizes and counts.
   * This is not mandatory: if you skip this, you'll get default sized
   * buffers, usually 4k or 32k bytes or so.
   *
   * We'll here ask for 4096 bytes as that's enough.
   */
  spa_pod_builder_init(&builder, buffer, sizeof(buffer));

  params[0] = spa_pod_builder_add_object(&builder,
                                         /* POD Object for the buffer parameter */
                                         SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
                                         /* Default 1 buffer, minimum of 1, max of 32 buffers.
                                          * We can do with 1 buffer as we dequeue and queue in the same
                                          * cycle.
                                          */
                                         SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(1, 1, 32),
                                         /* MIDI buffers always have 1 data block */
                                         SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
                                         /* Buffer size: request default 4096 bytes, min 4096, no maximum */
                                         SPA_PARAM_BUFFERS_size, SPA_POD_CHOICE_RANGE_Int(4096, 4096, INT32_MAX),
                                         /* MIDI buffers have stride 1 */
                                         SPA_PARAM_BUFFERS_stride, SPA_POD_Int(1));

  pw_filter_update_params(data.filter, data.midi_out,
                          (const struct spa_pod **)params, SPA_N_ELEMENTS(params));

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
  pw_stream_destroy(data.stream);
  spa_hook_remove(&event_listener);
  pw_context_destroy(context);
  pw_main_loop_destroy(data.loop);
  pw_deinit();
  sf_close(data.file);

  return 0;
}

/* do_quit gets called on SIGINT and SIGTERM, upon which we ask the
   event loop to quit. */
void do_quit(void *userdata, int signal_number)
{
  struct data *data = userdata;
  pw_main_loop_quit(data->loop);
}

/* This is a structure containing function pointers to event
   handlers. It is a common pattern in PipeWire: when something
   allows event listeners, a function _add_listener is available
   that takes a structure of function pointers, one for each
   event. Those APIs are versioned using the first field which is
   an integer version number, associated with a constant declared
   in the header file.

   Not all event listeners need to be implemented; the only
   required one for a stream or filter is `process`. */
const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process,
};