#ifndef UPHONOR_H
#define UPHONOR_H

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <sys/time.h>
#include <string.h>

#include <pipewire/pipewire.h>
#include <sndfile.h>
#include <spa/param/audio/format-utils.h>

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

  /* libsndfile stuff used to read samples from the input audio
     file. */
  SNDFILE *file;
  SF_INFO fileinfo;
};

/* Function declarations */
void on_process(void *userdata);
void do_quit(void *userdata, int signal_number);

/* External stream events structure */
extern const struct pw_stream_events stream_events;

#endif /* PLAY_H */