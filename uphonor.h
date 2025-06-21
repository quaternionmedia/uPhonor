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
#include <spa/param/latency-utils.h>
#include <spa/param/format.h>
#include <sndfile.h>
#include <spa/param/audio/format-utils.h>

struct port
{
   double accumulator;
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
   struct pw_filter_port *audio_in;
   struct pw_filter_port *audio_out;
   struct pw_filter_port *midi_in;
   struct pw_filter_port *midi_out;
   struct spa_audio_info format;
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
void on_process(void *userdata, struct spa_io_position *position);
void process_midi(void *userdata, struct spa_io_position *position);
void simple_process(void *userdata, struct spa_io_position *position);
void stream_process(void *userdata);

/* Utility functions */
void set_volume(struct data *data, float new_volume);
float linear_to_db_volume(float linear_volume);

void state_changed(void *userdata, enum pw_filter_state old,
                   enum pw_filter_state state, const char *error);

// Command line interface function to parse arguments
int cli(int argc, char **argv, struct data *data);

/* External stream events structure */
void state_changed(void *userdata, enum pw_filter_state old,
                   enum pw_filter_state state, const char *error);

void do_quit(void *userdata, int signal_number);

#endif /* UPHONOR_H */