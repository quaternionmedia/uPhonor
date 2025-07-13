#!/usr/bin/env python3
"""
Setup script for uPhonor Python CFFI bindings
"""

from setuptools import setup, find_packages
from cffi import FFI

# Create CFFI builder
ffibuilder = FFI()

# Define the C declarations that we want to expose to Python
ffibuilder.cdef("""
    // Basic data types and enums
    typedef enum {
        HOLO_STATE_IDLE,
        HOLO_STATE_PLAYING,
        HOLO_STATE_STOPPED
    } holo_state;
    
    typedef enum {
        LOOP_STATE_IDLE,
        LOOP_STATE_RECORDING,
        LOOP_STATE_PLAYING,
        LOOP_STATE_STOPPED
    } loop_state;
    
    typedef enum {
        PLAYBACK_MODE_NORMAL,
        PLAYBACK_MODE_TRIGGER
    } playback_mode;
    
    typedef enum {
        CONFIG_SUCCESS = 0,
        CONFIG_ERROR_FILE_NOT_FOUND = -1,
        CONFIG_ERROR_PARSE_FAILED = -2,
        CONFIG_ERROR_WRITE_FAILED = -3,
        CONFIG_ERROR_INVALID_VERSION = -4,
        CONFIG_ERROR_MEMORY = -5,
        CONFIG_ERROR_INVALID_DATA = -6
    } config_result_t;
    
    // Forward declarations for opaque types
    typedef struct data data_t;
    typedef struct memory_loop memory_loop_t;
    
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
        enum loop_state current_state;
    };
    
    // Main data structure (simplified for Python access)
    struct data {
        // Global state
        enum holo_state current_state;
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
        enum playback_mode current_playback_mode;
        
        // Sync mode
        bool sync_mode_enabled;
        uint8_t pulse_loop_note;
        uint32_t pulse_loop_duration;
        bool waiting_for_pulse_reset;
        uint32_t longest_loop_duration;
        float sync_cutoff_percentage;
        float sync_recording_cutoff_percentage;
        
        // Timeline tracking
        uint64_t pulse_timeline_start_frame;
        uint64_t current_sample_frame;
        uint32_t previous_pulse_position;
        
        // Backfill buffer
        uint32_t backfill_buffer_size;
        uint32_t backfill_write_position;
        uint32_t backfill_available_frames;
    };
    
    // Core uPhonor functions
    data_t* uphonor_init(void);
    void uphonor_cleanup(data_t* data);
    int uphonor_start(data_t* data);
    void uphonor_stop(data_t* data);
    
    // Volume and playback control
    void set_volume(data_t* data, float new_volume);
    void set_playback_speed(data_t* data, float new_speed);
    void set_record_player_mode(data_t* data, float speed_pitch_factor);
    void set_pitch_shift(data_t* data, float semitones);
    void set_rubberband_enabled(data_t* data, bool enabled);
    float linear_to_db_volume(float linear_volume);
    
    // Loop management functions
    struct memory_loop* get_loop_by_note(data_t* data, uint8_t midi_note);
    void stop_all_recordings(data_t* data);
    void stop_all_playback(data_t* data);
    
    // Playback mode functions
    void set_playback_mode_normal(data_t* data);
    void set_playback_mode_trigger(data_t* data);
    void toggle_playback_mode(data_t* data);
    const char* get_playback_mode_name(data_t* data);
    
    // Sync mode functions
    void enable_sync_mode(data_t* data);
    void disable_sync_mode(data_t* data);
    void toggle_sync_mode(data_t* data);
    bool is_sync_mode_enabled(data_t* data);
    
    // MIDI handling functions
    void handle_note_on(data_t* data, uint8_t channel, uint8_t note, uint8_t velocity);
    void handle_note_off(data_t* data, uint8_t channel, uint8_t note, uint8_t velocity);
    void handle_control_change(data_t* data, uint8_t channel, uint8_t controller, uint8_t value);
    
    // Audio file operations
    int start_recording(data_t* data, const char* filename);
    int stop_recording(data_t* data);
    int start_playing(data_t* data, const char* filename);
    
    // Configuration functions
    config_result_t config_save_state(data_t* data, const char* filename);
    config_result_t config_load_state(data_t* data, const char* filename);
    config_result_t config_save_active_loops_only(data_t* data, const char* filename);
    config_result_t config_validate_file(const char* filename);
    const char* config_get_error_message(config_result_t result);
    void config_reset_to_defaults(data_t* data);
    
    // Memory management
    void* malloc(size_t size);
    void free(void* ptr);
""")

# Set the source code that implements the C declarations
ffibuilder.set_source(
    "_uphonor_cffi",
    """
    #include "uphonor.h"
    #include "audio_processing.h"
    #include "midi_processing.h"
    #include "config.h"
    
    // Wrapper functions for Python CFFI integration
    
    // These wrapper functions provide a simplified interface for Python
    // while hiding the complexity of PipeWire integration
    
    static struct data* global_data = NULL;
    
    data_t* uphonor_init(void) {
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
        global_data->record_file = NULL;
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
        
        if (!global_data->silence_buffer || !global_data->temp_audio_buffer) {
            uphonor_cleanup(global_data);
            return NULL;
        }
        
        // Initialize multi-loop memory system (60 seconds max loop at 48kHz)
        if (init_all_memory_loops(global_data, 60, 48000) < 0) {
            uphonor_cleanup(global_data);
            return NULL;
        }
        
        return global_data;
    }
    
    void uphonor_cleanup(data_t* data) {
        if (data == NULL) return;
        
        // Clean up recording resources
        if (data->recording_enabled) {
            stop_recording(data);
        }
        
        if (data->record_filename) {
            free(data->record_filename);
        }
        
        // Cleanup multi-loop memory system
        cleanup_all_memory_loops(data);
        
        // Free performance buffers
        if (data->silence_buffer) {
            free(data->silence_buffer);
        }
        if (data->temp_audio_buffer) {
            free(data->temp_audio_buffer);
        }
        
        // Clean up rubberband
        cleanup_rubberband(data);
        
        if (data == global_data) {
            global_data = NULL;
        }
        
        free(data);
    }
    
    int uphonor_start(data_t* data) {
        // This would normally start the PipeWire processing
        // For now, just mark as playing
        if (data) {
            data->current_state = HOLO_STATE_PLAYING;
            return 0;
        }
        return -1;
    }
    
    void uphonor_stop(data_t* data) {
        if (data) {
            data->current_state = HOLO_STATE_STOPPED;
            stop_all_recordings(data);
            stop_all_playback(data);
        }
    }
    """,
    sources=[
        'audio_processing.c',
        'audio_processing_rt.c', 
        'audio_buffer_rt.c',
        'rt_nonrt_bridge.c',
        'midi_processing.c',
        'buffer_manager.c',
        'record.c',
        'rubberband_processing.c',
        'utils.c',
        'multi_loop_functions.c',
        'holo.c',
        'config.c',
        'config_utils.c',
        'config_file_loader.c'
    ],
    libraries=['pipewire-0.3', 'sndfile', 'alsa', 'm', 'pthread', 'rubberband', 'cjson'],
    include_dirs=['.'],
    library_dirs=[],
    extra_compile_args=['-std=c99'],
    define_macros=[]
)

if __name__ == "__main__":
    ffibuilder.compile(verbose=True)
