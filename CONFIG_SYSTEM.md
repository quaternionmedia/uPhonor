# uPhonor Configuration System

The uPhonor configuration system allows you to save and restore the state of your audio looping session, including loop metadata, playback states, volume levels, sync settings, and global parameters.

## Important Note: Audio Data vs Configuration

**The configuration system saves settings and metadata, NOT the actual recorded audio data.**

- ✅ **Saves**: Volume levels, sync settings, loop states, filenames, playback positions
- ❌ **Does NOT save**: Actual recorded audio content in the loop buffers

This means when you load a configuration:
1. All your settings will be restored (volumes, sync mode, etc.)
2. Loop metadata will be loaded (which notes were used, their settings)
3. But you'll need to record new audio content to actually hear the loops

This design keeps configuration files small and fast to load/save.

## Features

- **Complete Settings Persistence**: Save all loop metadata, volumes, sync settings, and global parameters
- **JSON Format**: Human-readable configuration files that can be manually edited if needed
- **Automatic Backup**: Creates backups before loading new configurations
- **Active Loops Only**: Option to save only loops with recorded content metadata
- **Validation**: Validates configuration files before loading
- **Command Line Interface**: Easy-to-use CLI commands for session management

## Quick Start

### Save Current Session
```bash
# Save with default name (uphonor_session.json)
./uphonor --save

# Save with custom name
./uphonor --save mysession

# The .json extension is added automatically if not provided
```

### Load Session
```bash
# Load default session
./uphonor --load

# Load specific session
./uphonor --load mysession

# Automatically creates backup before loading
```

### Save Active Loops Only
```bash
# Save only loops with recorded content
./uphonor --save-active jam

# Creates jam_active.json with only the loops that have audio
```

### Other Commands
```bash
# List available sessions
./uphonor --list-sessions

# Show current configuration status
./uphonor --status

# Reset everything to defaults (with backup)
./uphonor --reset

# Show help
./uphonor --help
```

## Configuration File Structure

The configuration files use JSON format with the following structure:

```json
{
  "global_state": {
    "version": "1.0",
    "volume": 0.8,
    "playback_speed": 1.0,
    "pitch_shift": 0.0,
    "rubberband_enabled": false,
    "current_state": "IDLE",
    "playback_mode": "TRIGGER",
    "sync_mode_enabled": true,
    "pulse_loop_note": 60,
    "pulse_loop_duration": 48000,
    "sync_cutoff_percentage": 0.5,
    "sync_recording_cutoff_percentage": 0.5,
    "active_loop_count": 3,
    "currently_recording_note": -1
  },
  "memory_loops": [
    {
      "midi_note": 60,
      "state": "PLAYING",
      "volume": 0.9,
      "filename": "loop_note_060_2025-07-08_14-30-15.wav",
      "recorded_frames": 48000,
      "playback_position": 12000,
      "buffer_size": 2880000,
      "sample_rate": 48000,
      "loop_ready": true,
      "recording_to_memory": false,
      "is_playing": true,
      "pending_record": false,
      "pending_stop": false,
      "pending_start": false
    }
  ],
  "saved_at": "2025-07-08 14:45:30"
}
```

### Global State Fields

- **version**: Configuration format version
- **volume**: Master volume (0.0 - 1.0+)
- **playback_speed**: Speed multiplier (1.0 = normal)
- **pitch_shift**: Pitch shift in semitones
- **rubberband_enabled**: Whether time-stretching is enabled
- **current_state**: Overall system state (IDLE/PLAYING/STOPPED)
- **playback_mode**: NORMAL (toggle) or TRIGGER (momentary)
- **sync_mode_enabled**: Whether sync mode is active
- **pulse_loop_note**: MIDI note of the master timing loop
- **pulse_loop_duration**: Length of pulse loop in frames
- **sync_cutoff_percentage**: Sync timing threshold
- **active_loop_count**: Number of loops in use
- **currently_recording_note**: Which loop is recording (-1 if none)

### Memory Loop Fields

- **midi_note**: MIDI note number (0-127)
- **state**: Loop state (IDLE/RECORDING/PLAYING/STOPPED)
- **volume**: Individual loop volume
- **filename**: Associated audio file
- **recorded_frames**: Length of recorded audio
- **playback_position**: Current playback position
- **sample_rate**: Audio sample rate
- **loop_ready**: Whether loop has recorded content
- **is_playing**: Whether loop is currently playing
- **pending_***: Sync mode flags for deferred operations

## File Locations

- **Default location**: Current working directory
- **Config directory**: `~/.config/uphonor/` (for backups and organized storage)
- **Backup files**: Automatically timestamped (e.g., `uphonor_backup_20250708_143015.json`)

## Integration with Your Code

### Low-Level API

```c
#include "config.h"

// Save current state
config_result_t result = config_save_state(data, "session.json");
if (result != CONFIG_SUCCESS) {
    printf("Error: %s\n", config_get_error_message(result));
}

// Load state
result = config_load_state(data, "session.json");
if (result != CONFIG_SUCCESS) {
    printf("Error: %s\n", config_get_error_message(result));
}

// Validate file before loading
result = config_validate_file("session.json");
if (result == CONFIG_SUCCESS) {
    // File is valid, safe to load
}
```

### High-Level API

```c
#include "config_utils.h"

// User-friendly functions with automatic feedback
if (save_current_session(data, "mysession") == 0) {
    printf("Session saved successfully!\n");
}

if (load_session(data, "mysession") == 0) {
    printf("Session loaded successfully!\n");
}

// Print current status
print_config_status(data);
```

## Best Practices

1. **Regular Saves**: Save your session frequently, especially before experimenting
2. **Descriptive Names**: Use meaningful session names (e.g., "jazz_practice", "experimental_loops")
3. **Active Loops Only**: Use `--save-active` for performance sessions to avoid clutter
4. **Backup Awareness**: The system creates automatic backups, but keep important sessions safe
5. **File Management**: Organize your session files in the config directory

## Troubleshooting

### Common Issues

**"Configuration file not found"**
- Check that the file exists and has the correct name
- Try with the full path or .json extension

**"Failed to parse configuration file"**
- The JSON syntax may be invalid
- Use `--validate` to check file integrity

**"Invalid configuration data"**
- The file structure doesn't match expected format
- Check version compatibility

**"Memory allocation error"**
- Insufficient system memory
- Try closing other applications

### Recovery

If something goes wrong:

1. **Check backups**: Look in `~/.config/uphonor/` for automatic backups
2. **Use --reset**: Reset to known good defaults
3. **Validate files**: Use `config_validate_file()` to check file integrity
4. **Manual editing**: JSON files can be carefully edited by hand if needed

## Development Notes

- **Thread Safety**: Configuration operations are not thread-safe - call from main thread only
- **RT Safety**: Do not call config functions from real-time audio callbacks
- **Memory**: The system handles all JSON memory management automatically
- **Dependencies**: Requires libcjson for JSON parsing

## Examples

See `example_session.json` for a sample configuration file showing a typical looping session with multiple active loops and sync mode enabled.
