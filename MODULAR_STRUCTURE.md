# uPhonor Modular Structure

This document describes the refactored modular structure of the uPhonor codebase for improved readability and maintainability.

## Overview

The main processing functionality has been split into focused modules to improve code organization and readability.

## Module Structure

### 1. Buffer Manager (`buffer_manager.h` / `buffer_manager.c`)
Handles audio buffer allocation, management, and utility functions:
- `initialize_audio_buffers()` - Allocates and initializes audio buffers
- `cleanup_audio_buffers()` - Cleans up allocated buffers
- `calculate_rms()` - Calculates RMS value of audio buffer

### 2. Audio Processing (`audio_processing.h` / `audio_processing.c`)
Handles all audio-related processing:
- `handle_audio_input()` - Processes input audio for recording
- `process_audio_output()` - Handles audio playback output
- `read_audio_frames()` - Reads audio data from files
- `handle_end_of_file()` - Manages audio looping
- `apply_volume()` - Applies volume changes to audio

### 3. MIDI Processing (`midi_processing.h` / `midi_processing.c`)
Handles all MIDI-related functionality:
- `process_midi_input()` - Processes incoming MIDI data
- `process_midi_output()` - Generates MIDI output
- `handle_midi_message()` - Parses and handles MIDI messages
- `handle_note_on()` / `handle_note_off()` - Specific note event handlers
- `parse_midi_sequence()` - Parses MIDI sequence data

### 4. Main Process (`process.c`)
Simplified main processing function that orchestrates the modules:
- `on_process()` - Main processing callback that calls modular functions

## Benefits of This Structure

1. **Improved Readability**: Each file focuses on a specific area of functionality
2. **Better Maintainability**: Changes to audio processing don't affect MIDI code and vice versa
3. **Easier Testing**: Individual modules can be tested independently
4. **Reduced Complexity**: The main process function is now much simpler and easier to understand
5. **Reusability**: Modules can be reused in other parts of the application

## Migration Notes

- Buffer management code has been extracted to `buffer_manager.c`
- Audio processing code has been extracted to `audio_processing.c`

## Build System

The `meson.build` file has been updated to include all the new modular source files.

## Future Improvements

Consider further modularization:
- Extract recording functionality to a separate `recording.c` module
- Create a `playback.c` module for playback-specific functionality
- Add unit tests for each module
