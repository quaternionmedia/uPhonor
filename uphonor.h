#ifndef UPHONOR_H
#define UPHONOR_H

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <sys/time.h>
#include <string.h>

#include <pipewire/pipewire.h>
#include <pipewire/filter.h>

#include <spa/pod/builder.h>
#include <spa/control/control.h>
#include <sndfile.h>
#include <spa/param/audio/format-utils.h>

struct port
{
};

/* A common pattern for PipeWire is to provide a user data void
   pointer that can be used to pass data around, so that we have a
   reference to our memory structures when in callbacks. The norm
   is therefore to store all the state required by the client in a
   struct declared in main, and passed to PipeWire as a pointer.
   struct data is just that. */
struct data
{
  /* Keep some references to PipeWire objects. */
  struct pw_main_loop *loop;
  struct pw_core *core;
  struct pw_stream *stream;

  struct pw_filter *filter;
  struct port *midi_in;
  struct port *midi_out;
  uint32_t clock_id;
  int64_t offset;
  uint64_t position;

  /* libsndfile stuff used to read samples from the input audio
     file. */
  SNDFILE *file;
  SF_INFO fileinfo;

  /* Flag to reset audio playback on loop sync */
  bool reset_audio;
  /* Volume level */
  float volume;
};

/* Function declarations */
void on_process(void *userdata);
void process_midi(void *userdata, struct spa_io_position *position);

void do_quit(void *userdata, int signal_number);

/* External stream events structure */
extern const struct pw_stream_events stream_events;

#endif /* UPHONOR_H */