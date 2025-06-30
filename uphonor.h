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
#include <sys/stat.h>
#include <rubberband/rubberband-c.h>
#include "rt_nonrt_bridge.h"

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

  uint32_t max_buffer_size; // Maximum expected buffer size
  float *silence_buffer;    // Pre-allocated silence buffer
  float *temp_audio_buffer; // Pre-allocated temp buffer for multi-channel

  /* libsndfile stuff used to read samples from the input audio
     file. */
  SNDFILE *file;
  SF_INFO fileinfo;

  /* Recording related fields */
  SNDFILE *record_file;
  SF_INFO record_fileinfo;
  bool recording_enabled;
  char *record_filename;

  /* Holophonor loop states */
  enum holo_state
  {
    HOLO_STATE_IDLE,
    HOLO_STATE_RECORDING,
    HOLO_STATE_PLAYING,
    HOLO_STATE_STOPPED
  } current_state;

  /* Flag to reset audio playback on loop sync */
  bool reset_audio;
  /* Volume level */
  float volume;
  /* Playback speed multiplier (1.0 = normal speed, 0.5 = half speed, 2.0 = double speed) */
  float playback_speed;
  /* Fractional sample position for variable speed playback */
  double sample_position;

  /* Rubberband time-stretching and pitch-shifting */
  RubberBandState rubberband_state;
  float pitch_shift;               /* Pitch shift in semitones (12 = one octave up, -12 = one octave down) */
  bool rubberband_enabled;         /* Whether to use rubberband processing */
  float *rubberband_input_buffer;  /* Input buffer for rubberband */
  float *rubberband_output_buffer; /* Output buffer for rubberband */
  uint32_t rubberband_buffer_size; /* Size of rubberband buffers */

  /* RT/Non-RT bridge for performance-critical operations */
  struct rt_nonrt_bridge rt_bridge;
};

/* Function declarations */
void on_process(void *userdata, struct spa_io_position *position);

/* Include modular headers */
#include "audio_processing.h"
#include "midi_processing.h"
#include "buffer_manager.h"
#include "rt_nonrt_bridge.h"

void process_loops(struct data *data, struct spa_io_position *position, float volume);

/* Utility functions */
void set_volume(struct data *data, float new_volume);
void set_playback_speed(struct data *data, float new_speed);
void set_pitch_shift(struct data *data, float semitones);
void set_rubberband_enabled(struct data *data, bool enabled);
float linear_to_db_volume(float linear_volume);

/* Rubberband functions */
int init_rubberband(struct data *data);
void cleanup_rubberband(struct data *data);
void rubberband_reset_data(struct data *data);

void state_changed(void *userdata, enum pw_filter_state old,
                   enum pw_filter_state state, const char *error);

// Command line interface function to parse arguments
int cli(int argc, char **argv, struct data *data);

/* External stream events structure */
void state_changed(void *userdata, enum pw_filter_state old,
                   enum pw_filter_state state, const char *error);

void do_quit(void *userdata, int signal_number);

#endif /* UPHONOR_H */
