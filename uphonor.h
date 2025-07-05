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
#include "audio_buffer_rt.h"

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

  /* Global file playback state (separate from per-loop states) */
  enum holo_state
  {
    HOLO_STATE_IDLE,
    HOLO_STATE_PLAYING,
    HOLO_STATE_STOPPED
  } current_state;

  /* Holophonor system state - removed individual loop states, now per-loop */

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

  /* RT-optimized audio buffering system */
  struct audio_buffer_rt audio_buffer;

  /* In-memory loop recording and playback - one loop per MIDI note */
  struct memory_loop
  {
    float *buffer;              /* In-memory audio buffer for recorded loop */
    uint32_t buffer_size;       /* Total allocated size of buffer */
    uint32_t recorded_frames;   /* Number of frames currently recorded */
    uint32_t playback_position; /* Current playback position in the loop */
    bool loop_ready;            /* Whether loop is ready for playback */
    bool recording_to_memory;   /* Whether we're currently recording to memory */
    bool is_playing;            /* Whether this loop is currently playing */
    bool pending_record;        /* Whether this loop is waiting to start recording in sync mode */
    bool pending_stop;          /* Whether this loop is waiting to stop recording at next pulse reset in sync mode */
    bool pending_start;         /* Whether this loop is waiting to start playing at next pulse reset in sync mode */
    uint32_t sample_rate;       /* Sample rate for the recorded loop */
    char loop_filename[512];    /* Filename for eventual file write */
    uint8_t midi_note;          /* MIDI note number (0-127) that controls this loop */
    float volume;               /* Individual volume for this loop (from note velocity) */

    /* Per-loop state management */
    enum loop_state
    {
      LOOP_STATE_IDLE,
      LOOP_STATE_RECORDING,
      LOOP_STATE_PLAYING,
      LOOP_STATE_STOPPED
    } current_state;
  } memory_loops[128]; /* One loop for each MIDI note (0-127) */

  /* Global loop management */
  uint8_t active_loop_count;        /* Number of loops that have been used */
  uint8_t currently_recording_note; /* MIDI note currently being recorded (-1 if none) */

  /* Playback mode control */
  enum playback_mode
  {
    PLAYBACK_MODE_NORMAL, /* Note On toggles play/stop, Note Off ignored */
    PLAYBACK_MODE_TRIGGER /* Note On starts, Note Off stops (current behavior) */
  } current_playback_mode;

  /* Sync mode control (independent of playback mode) */
  bool sync_mode_enabled;         /* Whether sync mode is active */
  uint8_t pulse_loop_note;        /* MIDI note of the pulse/master loop (255 if none) */
  uint32_t pulse_loop_duration;   /* Duration in frames of the pulse loop */
  bool waiting_for_pulse_reset;   /* Whether we're waiting for pulse loop to reset before allowing new recordings */
  uint32_t longest_loop_duration; /* Duration of the longest currently playing loop */
  float sync_cutoff_percentage;   /* Cutoff point for sync decisions (0.0-1.0, default 0.5) */
};

/* Function declarations */
void on_process(void *userdata, struct spa_io_position *position);

/* Include modular headers */
#include "audio_processing.h"
#include "midi_processing.h"
#include "buffer_manager.h"

void process_loops(struct data *data, struct spa_io_position *position, uint8_t midi_note, float volume);

/* Utility functions */
void set_volume(struct data *data, float new_volume);
void set_playback_speed(struct data *data, float new_speed);
void set_record_player_mode(struct data *data, float speed_pitch_factor);
void set_pitch_shift(struct data *data, float semitones);
void set_rubberband_enabled(struct data *data, bool enabled);
float linear_to_db_volume(float linear_volume);

/* Multi-loop management functions */
int init_all_memory_loops(struct data *data, uint32_t max_seconds, uint32_t sample_rate);
void cleanup_all_memory_loops(struct data *data);
struct memory_loop *get_loop_by_note(struct data *data, uint8_t midi_note);
void stop_all_recordings(struct data *data);
void stop_all_playback(struct data *data);

/* Playback mode functions */
void set_playback_mode_normal(struct data *data);
void set_playback_mode_trigger(struct data *data);
void toggle_playback_mode(struct data *data);
const char *get_playback_mode_name(struct data *data);

/* Sync mode functions */
void enable_sync_mode(struct data *data);
void disable_sync_mode(struct data *data);
void toggle_sync_mode(struct data *data);
bool is_sync_mode_enabled(struct data *data);
void check_sync_pending_recordings(struct data *data);
void start_sync_pending_recordings_on_pulse_reset(struct data *data);
void stop_sync_pending_recordings_on_pulse_reset(struct data *data);
void start_sync_pending_playback_on_pulse_reset(struct data *data);
void check_sync_recording_target_length(struct data *data, uint8_t midi_note);

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
