#!/usr/bin/env python3
"""
CFFI Extension Builder for uPhonor

This script builds the C extension using CFFI for Python bindings.
It uses a minimal, self-contained approach that doesn't depend on
the full PipeWire integration - perfect for testing and demos.
"""

from cffi import FFI
import os
import sys
import subprocess
from pathlib import Path


def get_pkg_config_flags(packages):
    """Get compiler flags from pkg-config"""
    try:
        result = subprocess.run(
            ["pkg-config", "--cflags", "--libs"] + packages,
            capture_output=True,
            text=True,
            check=True,
        )

        flags = result.stdout.strip().split()
        cflags = []
        ldflags = []
        include_dirs = []
        library_dirs = []
        libraries = []

        for flag in flags:
            if flag.startswith('-I'):
                include_dirs.append(flag[2:])
            elif flag.startswith('-L'):
                library_dirs.append(flag[2:])
            elif flag.startswith('-l'):
                libraries.append(flag[2:])
            elif (
                flag.startswith('-D') or flag.startswith('-W') or flag.startswith('-f')
            ):
                cflags.append(flag)
            else:
                cflags.append(flag)

        return {
            'include_dirs': include_dirs,
            'library_dirs': library_dirs,
            'libraries': libraries,
            'extra_compile_args': cflags,
        }
    except (subprocess.CalledProcessError, FileNotFoundError):
        print(f"Warning: pkg-config failed for packages: {packages}")
        return {
            'include_dirs': [],
            'library_dirs': [],
            'libraries': [],
            'extra_compile_args': [],
        }


def build_cffi_extension():
    """Build the CFFI extension"""

    # Create CFFI builder
    ffibuilder = FFI()

    # Define the C declarations that we want to expose to Python
    ffibuilder.cdef(
        """
        // Enum constants (CFFI approach for inline enum definitions)
        #define HOLO_STATE_IDLE 0
        #define HOLO_STATE_PLAYING 1
        #define HOLO_STATE_STOPPED 2
        
        #define LOOP_STATE_IDLE 0
        #define LOOP_STATE_RECORDING 1
        #define LOOP_STATE_PLAYING 2
        #define LOOP_STATE_STOPPED 3
        
        #define PLAYBACK_MODE_NORMAL 0
        #define PLAYBACK_MODE_TRIGGER 1

        // Memory loop structure (simplified for Python access)
        struct memory_loop {
            float *buffer;
            uint32_t buffer_size;
            uint32_t recorded_frames;
            uint32_t playback_position;
            bool loop_ready;
            bool recording_to_memory;
            bool is_playing;
            bool pending_record;
            bool pending_stop;
            bool pending_start;
            uint32_t sample_rate;
            char loop_filename[512];
            uint8_t midi_note;
            float volume;
            int current_state;  // Use int for inline enum
        };
        
        // Main data structure (simplified for Python access)
        struct data {
            // Global state
            int current_state;  // Use int for inline enum
            bool recording_enabled;
            char *record_filename;
            
            // Audio parameters
            float volume;
            float playback_speed;
            double sample_position;
            
            // Rubberband parameters
            float pitch_shift;
            bool rubberband_enabled;
            
            // Loop management
            struct memory_loop memory_loops[128];
            uint8_t active_loop_count;
            uint8_t currently_recording_note;
            
            // Playback mode
            int current_playback_mode;  // Use int for inline enum
            
            // Sync mode
            bool sync_mode_enabled;
            uint8_t pulse_loop_note;
            uint32_t pulse_loop_duration;
            bool waiting_for_pulse_reset;
            uint32_t longest_loop_duration;
            float sync_cutoff_percentage;
            
            // Additional fields for demo functionality
            float sync_recording_cutoff_percentage;
            uint32_t max_buffer_size;
            float *silence_buffer;
            float *temp_audio_buffer;
        };

        // Core functions
        struct data* uphonor_init(void);
        void uphonor_cleanup(struct data* data);

        // Audio control functions
        void uphonor_start_recording(struct data* data, uint8_t note);
        void uphonor_stop_recording(struct data* data, uint8_t note);
        void uphonor_start_playback(struct data* data, uint8_t note);
        void uphonor_stop_playback(struct data* data, uint8_t note);

        // Parameter setting functions
        void uphonor_set_volume(struct data* data, float volume);
        void uphonor_set_loop_volume(struct data* data, uint8_t note, float volume);
        void uphonor_set_pitch_shift(struct data* data, float semitones);
        void uphonor_set_playback_speed(struct data* data, float speed);
        void uphonor_enable_rubberband(struct data* data, bool enable);
        void uphonor_set_playback_mode(struct data* data, int mode);
        void uphonor_enable_sync_mode(struct data* data, bool enable);
        void uphonor_set_pulse_loop(struct data* data, uint8_t note);

        // MIDI note on/off handlers
        void uphonor_note_on(struct data* data, uint8_t note, uint8_t velocity);
        void uphonor_note_off(struct data* data, uint8_t note, uint8_t velocity);
        """
    )

    # Don't get system dependencies - we're going self-contained
    required_packages = []  # No external dependencies for minimal version
    pkg_flags = {
        'include_dirs': [],
        'library_dirs': [],
        'libraries': [],
        'extra_compile_args': [],
    }

    # Set the source code that implements the C declarations
    ffibuilder.set_source(
        "_uphonor_cffi",
        """
        // Minimal includes for basic types
        #define _GNU_SOURCE
        #include <sys/types.h>
        #include <unistd.h>
        #include <locale.h>
        #include <stdint.h>
        #include <stdbool.h>
        #include <stdio.h>
        #include <stdlib.h>
        #include <string.h>
        
        // Enum constants
        #define HOLO_STATE_IDLE 0
        #define HOLO_STATE_PLAYING 1
        #define HOLO_STATE_STOPPED 2
        
        #define LOOP_STATE_IDLE 0
        #define LOOP_STATE_RECORDING 1
        #define LOOP_STATE_PLAYING 2
        #define LOOP_STATE_STOPPED 3
        
        #define PLAYBACK_MODE_NORMAL 0
        #define PLAYBACK_MODE_TRIGGER 1
        
        // Memory loop structure (simplified for Python access)
        struct memory_loop {
            float *buffer;
            uint32_t buffer_size;
            uint32_t recorded_frames;
            uint32_t playback_position;
            bool loop_ready;
            bool recording_to_memory;
            bool is_playing;
            bool pending_record;
            bool pending_stop;
            bool pending_start;
            uint32_t sample_rate;
            char loop_filename[512];
            uint8_t midi_note;
            float volume;
            int current_state;  // Use int for inline enum
        };
        
        // Main data structure (simplified for Python access)
        struct data {
            // Global state
            int current_state;  // Use int for inline enum
            bool recording_enabled;
            char *record_filename;
            
            // Audio parameters
            float volume;
            float playback_speed;
            double sample_position;
            
            // Rubberband parameters
            float pitch_shift;
            bool rubberband_enabled;
            
            // Loop management
            struct memory_loop memory_loops[128];
            uint8_t active_loop_count;
            uint8_t currently_recording_note;
            
            // Playback mode
            int current_playback_mode;  // Use int for inline enum
            
            // Sync mode
            bool sync_mode_enabled;
            uint8_t pulse_loop_note;
            uint32_t pulse_loop_duration;
            bool waiting_for_pulse_reset;
            uint32_t longest_loop_duration;
            float sync_cutoff_percentage;
            
            // Additional fields for demo functionality
            float sync_recording_cutoff_percentage;
            uint32_t max_buffer_size;
            float *silence_buffer;
            float *temp_audio_buffer;
        };
        
        // Global instance for demo/testing
        static struct data* global_data = NULL;
        
        struct data* uphonor_init(void) {
            if (global_data != NULL) {
                return global_data;  // Already initialized
            }
            
            global_data = calloc(1, sizeof(struct data));
            if (!global_data) {
                return NULL;
            }
            
            // Initialize basic fields
            global_data->volume = 1.0f;
            global_data->playback_speed = 1.0f;
            global_data->sample_position = 0.0;
            global_data->recording_enabled = false;
            global_data->record_filename = NULL;
            global_data->current_state = HOLO_STATE_IDLE;
            global_data->pitch_shift = 0.0f;
            global_data->rubberband_enabled = true;
            global_data->current_playback_mode = PLAYBACK_MODE_TRIGGER;
            global_data->sync_mode_enabled = false;
            global_data->pulse_loop_note = 255;  // No pulse loop
            global_data->sync_cutoff_percentage = 0.5f;
            global_data->sync_recording_cutoff_percentage = 0.5f;
            
            // Initialize performance buffers
            global_data->max_buffer_size = 2048 * 8;
            global_data->silence_buffer = calloc(global_data->max_buffer_size, sizeof(float));
            global_data->temp_audio_buffer = malloc(global_data->max_buffer_size * sizeof(float));
            
            // Initialize memory loops
            for (int i = 0; i < 128; i++) {
                global_data->memory_loops[i].buffer = NULL;
                global_data->memory_loops[i].buffer_size = 0;
                global_data->memory_loops[i].recorded_frames = 0;
                global_data->memory_loops[i].playback_position = 0;
                global_data->memory_loops[i].loop_ready = false;
                global_data->memory_loops[i].recording_to_memory = false;
                global_data->memory_loops[i].is_playing = false;
                global_data->memory_loops[i].pending_record = false;
                global_data->memory_loops[i].pending_stop = false;
                global_data->memory_loops[i].pending_start = false;
                global_data->memory_loops[i].sample_rate = 48000;
                global_data->memory_loops[i].loop_filename[0] = '\\0';
                global_data->memory_loops[i].midi_note = i;
                global_data->memory_loops[i].volume = 1.0f;
                global_data->memory_loops[i].current_state = LOOP_STATE_IDLE;
            }
            
            global_data->active_loop_count = 0;
            global_data->currently_recording_note = 255;  // No recording
            
            return global_data;
        }
        
        void uphonor_cleanup(struct data* data) {
            if (data == NULL) return;
            
            // Clean up memory loops
            for (int i = 0; i < 128; i++) {
                if (data->memory_loops[i].buffer) {
                    free(data->memory_loops[i].buffer);
                    data->memory_loops[i].buffer = NULL;
                }
            }
            
            // Clean up performance buffers
            if (data->silence_buffer) {
                free(data->silence_buffer);
                data->silence_buffer = NULL;
            }
            
            if (data->temp_audio_buffer) {
                free(data->temp_audio_buffer);
                data->temp_audio_buffer = NULL;
            }
            
            if (data->record_filename) {
                free(data->record_filename);
                data->record_filename = NULL;
            }
            
            free(data);
            if (data == global_data) {
                global_data = NULL;
            }
        }
        
        // Simplified stub functions for demo/testing
        void uphonor_start_recording(struct data* data, uint8_t note) {
            if (data && note < 128) {
                data->memory_loops[note].current_state = LOOP_STATE_RECORDING;
                data->currently_recording_note = note;
                printf("Demo: Started recording loop %d\\n", note);
            }
        }
        
        void uphonor_stop_recording(struct data* data, uint8_t note) {
            if (data && note < 128) {
                data->memory_loops[note].current_state = LOOP_STATE_IDLE;
                if (data->currently_recording_note == note) {
                    data->currently_recording_note = 255;
                }
                printf("Demo: Stopped recording loop %d\\n", note);
            }
        }
        
        void uphonor_start_playback(struct data* data, uint8_t note) {
            if (data && note < 128) {
                data->memory_loops[note].current_state = LOOP_STATE_PLAYING;
                printf("Demo: Started playback loop %d\\n", note);
            }
        }
        
        void uphonor_stop_playback(struct data* data, uint8_t note) {
            if (data && note < 128) {
                data->memory_loops[note].current_state = LOOP_STATE_STOPPED;
                printf("Demo: Stopped playback loop %d\\n", note);
            }
        }
        
        void uphonor_set_volume(struct data* data, float volume) {
            if (data) {
                data->volume = volume;
                printf("Demo: Set global volume to %.2f\\n", volume);
            }
        }
        
        void uphonor_set_loop_volume(struct data* data, uint8_t note, float volume) {
            if (data && note < 128) {
                data->memory_loops[note].volume = volume;
                printf("Demo: Set loop %d volume to %.2f\\n", note, volume);
            }
        }
        
        void uphonor_set_pitch_shift(struct data* data, float semitones) {
            if (data) {
                data->pitch_shift = semitones;
                printf("Demo: Set pitch shift to %.2f semitones\\n", semitones);
            }
        }
        
        void uphonor_set_playback_speed(struct data* data, float speed) {
            if (data) {
                data->playback_speed = speed;
                printf("Demo: Set playback speed to %.2f\\n", speed);
            }
        }
        
        void uphonor_enable_rubberband(struct data* data, bool enable) {
            if (data) {
                data->rubberband_enabled = enable;
                printf("Demo: %s Rubberband processing\\n", enable ? "Enabled" : "Disabled");
            }
        }
        
        void uphonor_set_playback_mode(struct data* data, int mode) {
            if (data) {
                data->current_playback_mode = mode;
                printf("Demo: Set playback mode to %s\\n", 
                       mode == PLAYBACK_MODE_NORMAL ? "Normal" : "Trigger");
            }
        }
        
        void uphonor_enable_sync_mode(struct data* data, bool enable) {
            if (data) {
                data->sync_mode_enabled = enable;
                printf("Demo: %s sync mode\\n", enable ? "Enabled" : "Disabled");
            }
        }
        
        void uphonor_set_pulse_loop(struct data* data, uint8_t note) {
            if (data) {
                data->pulse_loop_note = note;
                printf("Demo: Set pulse loop to note %d\\n", note);
            }
        }
        
        // MIDI note on/off handlers
        void uphonor_note_on(struct data* data, uint8_t note, uint8_t velocity) {
            if (data && note < 128) {
                printf("Demo: Note ON - note=%d, velocity=%d\\n", note, velocity);
                // In trigger mode, start recording or playback
                if (data->current_playback_mode == PLAYBACK_MODE_TRIGGER) {
                    if (!data->memory_loops[note].loop_ready) {
                        uphonor_start_recording(data, note);
                    } else {
                        uphonor_start_playback(data, note);
                    }
                }
            }
        }
        
        void uphonor_note_off(struct data* data, uint8_t note, uint8_t velocity) {
            if (data && note < 128) {
                printf("Demo: Note OFF - note=%d, velocity=%d\\n", note, velocity);
                // In trigger mode, stop recording or playback
                if (data->current_playback_mode == PLAYBACK_MODE_TRIGGER) {
                    if (data->memory_loops[note].current_state == LOOP_STATE_RECORDING) {
                        uphonor_stop_recording(data, note);
                        data->memory_loops[note].loop_ready = true;
                    } else if (data->memory_loops[note].current_state == LOOP_STATE_PLAYING) {
                        uphonor_stop_playback(data, note);
                    }
                }
            }
        }
        """,
        # No source dependencies - self-contained CFFI wrapper
        sources=[],
        **pkg_flags,
    )

    return ffibuilder


def main():
    """Main build function"""
    print("Building uPhonor CFFI extension...")

    # Build the extension
    ffibuilder = build_cffi_extension()

    # Compile with verbose output for debugging
    ffibuilder.compile(verbose=True)

    print("✓ CFFI extension built successfully!")


if __name__ == "__main__":
    main()
