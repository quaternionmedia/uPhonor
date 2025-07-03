#include "loop_manager.h"
#include "uphonor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <pipewire/pipewire.h>

int loop_manager_init(struct loop_manager *mgr)
{
    if (!mgr) return -1;
    
    memset(mgr, 0, sizeof(struct loop_manager));
    mgr->master_volume = 1.0f;
    mgr->current_loop_index = -1;
    
    /* Initialize all loop slots as inactive */
    for (int i = 0; i < MAX_LOOPS; i++) {
        struct loop_slot *loop = &mgr->loops[i];
        loop->state = HOLO_STATE_IDLE;
        loop->volume = 1.0f;
        loop->playback_speed = 1.0f;
        loop->sample_position = 0.0;
        loop->active = false;
        loop->recording_enabled = false;
        loop->reset_audio = false;
        loop->midi_note = i; /* Note number corresponds to array index */
        
        /* Initialize audio buffer for this loop (mono) */
        if (audio_buffer_rt_init(&loop->audio_buffer, 1) < 0) {
            pw_log_error("Failed to initialize audio buffer for loop %d", i);
            /* Clean up previously initialized loops */
            for (int j = 0; j < i; j++) {
                audio_buffer_rt_cleanup(&mgr->loops[j].audio_buffer);
            }
            return -1;
        }
    }
    
    pw_log_info("Loop manager initialized with support for %d loops", MAX_LOOPS);
    return 0;
}

void loop_manager_cleanup(struct loop_manager *mgr)
{
    if (!mgr) return;
    
    for (int i = 0; i < MAX_LOOPS; i++) {
        struct loop_slot *loop = &mgr->loops[i];
        
        /* Close any open files */
        if (loop->file) {
            sf_close(loop->file);
            loop->file = NULL;
        }
        
        if (loop->record_file) {
            sf_close(loop->record_file);
            loop->record_file = NULL;
        }
        
        /* Free filename strings */
        if (loop->filename) {
            free(loop->filename);
            loop->filename = NULL;
        }
        
        if (loop->record_filename) {
            free(loop->record_filename);
            loop->record_filename = NULL;
        }
        
        /* Cleanup audio buffer */
        audio_buffer_rt_cleanup(&loop->audio_buffer);
        
        loop->active = false;
    }
    
    mgr->num_active_loops = 0;
    pw_log_info("Loop manager cleaned up");
}

struct loop_slot* loop_manager_get_loop_by_note(struct loop_manager *mgr, uint8_t midi_note)
{
    if (!mgr || midi_note >= MAX_LOOPS) return NULL;
    
    return &mgr->loops[midi_note];
}

struct loop_slot* loop_manager_allocate_loop(struct loop_manager *mgr, uint8_t midi_note)
{
    if (!mgr || midi_note >= MAX_LOOPS) return NULL;
    
    struct loop_slot *loop = &mgr->loops[midi_note];
    
    if (!loop->active) {
        loop->active = true;
        mgr->num_active_loops++;
        mgr->current_loop_index = midi_note;
        pw_log_info("Allocated loop for MIDI note %d", midi_note);
    }
    
    return loop;
}

void loop_manager_free_loop(struct loop_manager *mgr, uint8_t midi_note)
{
    if (!mgr || midi_note >= MAX_LOOPS) return;
    
    struct loop_slot *loop = &mgr->loops[midi_note];
    
    if (loop->active) {
        /* Stop any recording or playback */
        if (loop->record_file) {
            sf_close(loop->record_file);
            loop->record_file = NULL;
        }
        
        if (loop->file) {
            sf_close(loop->file);
            loop->file = NULL;
        }
        
        /* Reset state */
        loop->state = HOLO_STATE_IDLE;
        loop->active = false;
        loop->recording_enabled = false;
        loop->reset_audio = false;
        loop->sample_position = 0.0;
        
        mgr->num_active_loops--;
        pw_log_info("Freed loop for MIDI note %d", midi_note);
        
        /* Update current loop index if this was the current one */
        if (mgr->current_loop_index == midi_note) {
            mgr->current_loop_index = -1;
            /* Find another active loop to be current */
            for (int i = 0; i < MAX_LOOPS; i++) {
                if (mgr->loops[i].active) {
                    mgr->current_loop_index = i;
                    break;
                }
            }
        }
    }
}

char* generate_loop_filename(uint8_t midi_note)
{
    /* Generate timestamp-based filename with MIDI note number */
    time_t rawtime;
    struct tm *timeinfo;
    char timestamp[64];
    char *filename;
    
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", timeinfo);
    
    /* Allocate filename string */
    filename = malloc(256);
    if (!filename) return NULL;
    
    snprintf(filename, 256, "recordings/loop-note%03d-%s.wav", midi_note, timestamp);
    
    return filename;
}

int start_loop_recording(struct loop_slot *loop, const char *filename)
{
    if (!loop) return -1;
    
    /* Generate filename if not provided */
    if (!filename) {
        loop->record_filename = generate_loop_filename(loop->midi_note);
        if (!loop->record_filename) {
            pw_log_error("Failed to generate filename for loop %d", loop->midi_note);
            return -1;
        }
    } else {
        loop->record_filename = strdup(filename);
        if (!loop->record_filename) {
            pw_log_error("Failed to copy filename for loop %d", loop->midi_note);
            return -1;
        }
    }
    
    /* Set up recording file info */
    loop->record_fileinfo.samplerate = 48000; /* Default sample rate */
    loop->record_fileinfo.channels = 1;       /* Mono recording */
    loop->record_fileinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    
    /* Open file for recording */
    loop->record_file = sf_open(loop->record_filename, SFM_WRITE, &loop->record_fileinfo);
    if (!loop->record_file) {
        pw_log_error("Failed to open recording file for loop %d: %s", 
                    loop->midi_note, sf_strerror(NULL));
        free(loop->record_filename);
        loop->record_filename = NULL;
        return -1;
    }
    
    loop->recording_enabled = true;
    pw_log_info("Started recording for loop %d: %s", loop->midi_note, loop->record_filename);
    
    return 0;
}

int stop_loop_recording(struct loop_slot *loop)
{
    if (!loop || !loop->record_file) return -1;
    
    /* Close recording file */
    sf_close(loop->record_file);
    loop->record_file = NULL;
    loop->recording_enabled = false;
    
    pw_log_info("Stopped recording for loop %d: %s", loop->midi_note, 
                loop->record_filename ? loop->record_filename : "unknown");
    
    return 0;
}

int start_loop_playing(struct loop_slot *loop, const char *filename)
{
    if (!loop || !filename) return -1;
    
    /* Close any existing file */
    if (loop->file) {
        sf_close(loop->file);
        loop->file = NULL;
    }
    
    /* Open file for playback */
    memset(&loop->fileinfo, 0, sizeof(loop->fileinfo));
    loop->file = sf_open(filename, SFM_READ, &loop->fileinfo);
    if (!loop->file) {
        pw_log_error("Failed to open playback file for loop %d: %s", 
                    loop->midi_note, sf_strerror(NULL));
        return -1;
    }
    
    /* Store filename */
    if (loop->filename) {
        free(loop->filename);
    }
    loop->filename = strdup(filename);
    
    /* Reset playback position */
    loop->sample_position = 0.0;
    loop->reset_audio = true;
    
    /* Reset and fill audio buffer for this loop */
    audio_buffer_rt_reset(&loop->audio_buffer);
    audio_buffer_rt_fill(&loop->audio_buffer, loop->file, &loop->fileinfo);
    
    pw_log_info("Started playback for loop %d: %s", loop->midi_note, filename);
    
    return 0;
}

void process_multiple_loops(struct data *data, uint8_t midi_note, float volume)
{
    if (!data || !data->loop_mgr) {
        pw_log_error("Data or loop manager is NULL");
        return;
    }
    
    /* Ensure volume is within valid range */
    if (volume < 0.0f || volume > 1.0f) {
        pw_log_warn("Invalid volume level: %.2f, clamping to 0.0-1.0", volume);
        volume = volume < 0.0f ? 0.0f : 1.0f;
    }
    
    /* Get or allocate loop for this MIDI note */
    struct loop_slot *loop = loop_manager_get_loop_by_note(data->loop_mgr, midi_note);
    if (!loop) {
        pw_log_error("Failed to get loop for MIDI note %d", midi_note);
        return;
    }
    
    /* Allocate loop if not active */
    if (!loop->active) {
        loop_manager_allocate_loop(data->loop_mgr, midi_note);
    }
    
    /* Set volume for this loop */
    loop->volume = volume;
    
    pw_log_info("Processing loop %d (note %d) in state %d with volume %.2f", 
                midi_note, midi_note, loop->state, volume);
    
    /* State machine for this specific loop */
    switch (loop->state) {
    case HOLO_STATE_IDLE:
        pw_log_info("Starting recording for loop %d", midi_note);
        if (start_loop_recording(loop, NULL) == 0) {
            loop->state = HOLO_STATE_RECORDING;
        }
        break;
        
    case HOLO_STATE_RECORDING:
        pw_log_info("Stopping recording for loop %d", midi_note);
        stop_loop_recording(loop);
        
        loop->state = HOLO_STATE_PLAYING;
        
        /* Start playback of the recorded file */
        if (loop->record_filename) {
            pw_log_info("Starting playback of recorded file for loop %d: %s", 
                       midi_note, loop->record_filename);
            if (start_loop_playing(loop, loop->record_filename) != 0) {
                pw_log_error("Failed to start playback for loop %d", midi_note);
                loop->state = HOLO_STATE_IDLE;
            }
        } else {
            pw_log_error("No recorded file found for loop %d", midi_note);
            loop->state = HOLO_STATE_IDLE;
        }
        break;
        
    case HOLO_STATE_PLAYING:
        pw_log_info("Stopping playback for loop %d", midi_note);
        loop->state = HOLO_STATE_STOPPED;
        break;
        
    case HOLO_STATE_STOPPED:
        pw_log_info("Restarting playback for loop %d", midi_note);
        loop->state = HOLO_STATE_PLAYING;
        /* Reset playback position */
        loop->sample_position = 0.0;
        loop->reset_audio = true;
        break;
        
    default:
        pw_log_warn("Unknown state %d for loop %d, resetting to idle", 
                   loop->state, midi_note);
        loop->state = HOLO_STATE_IDLE;
        break;
    }
}
