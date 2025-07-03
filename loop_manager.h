#ifndef LOOP_MANAGER_H
#define LOOP_MANAGER_H

#include "common_types.h"
#include "audio_buffer_rt.h"
#include <sndfile.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_LOOPS 128 /* Support all MIDI notes (0-127) */

/* Forward declarations */
struct data;
struct spa_io_position;

/* Individual loop state structure */
struct loop_slot
{
  /* State for this specific loop */
  enum holo_state state;

  /* Audio file info for this loop */
  SNDFILE *file;
  SF_INFO fileinfo;
  char *filename;

  /* Recording file info for this loop */
  SNDFILE *record_file;
  SF_INFO record_fileinfo;
  char *record_filename;
  bool recording_enabled;

  /* Playback parameters for this loop */
  float volume;
  float playback_speed;
  double sample_position;
  bool reset_audio;

  /* RT-optimized audio buffering for this loop */
  struct audio_buffer_rt audio_buffer;

  /* MIDI note that controls this loop */
  uint8_t midi_note;

  /* Whether this loop slot is active/allocated */
  bool active;
};

/* Loop manager structure */
struct loop_manager
{
  struct loop_slot loops[MAX_LOOPS];
  int num_active_loops;

  /* Master volume and settings that apply to all loops */
  float master_volume;

  /* Current active loop for display/UI purposes */
  int current_loop_index;
};

/* Function declarations */
int loop_manager_init(struct loop_manager *mgr);
void loop_manager_cleanup(struct loop_manager *mgr);

/* Loop management functions */
struct loop_slot *loop_manager_get_loop_by_note(struct loop_manager *mgr, uint8_t midi_note);
struct loop_slot *loop_manager_allocate_loop(struct loop_manager *mgr, uint8_t midi_note);
void loop_manager_free_loop(struct loop_manager *mgr, uint8_t midi_note);

/* Process loops function that handles multiple loops */
void process_multiple_loops(struct data *data, uint8_t midi_note, float volume);

/* Audio processing for multiple loops */
void process_multiple_loops_audio_output(struct data *data, struct spa_io_position *position);
void handle_multiple_loops_audio_input(struct data *data, uint32_t n_samples);

/* Rubberband processing for mixed loop output */
uint32_t apply_rubberband_to_buffer(struct data *data, float *buffer, uint32_t n_samples);

/* Loop file management */
char *generate_loop_filename(uint8_t midi_note);
int start_loop_recording(struct loop_slot *loop, const char *filename);
int stop_loop_recording(struct loop_slot *loop);
int start_loop_playing(struct loop_slot *loop, const char *filename);

#endif /* LOOP_MANAGER_H */