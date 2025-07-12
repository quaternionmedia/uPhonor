# Save Configuration Function

## Overview
You can now save the current session state while uPhonor is running using MIDI control change messages.

## How to Use

### MIDI Save Function
- **MIDI CC Number**: 81
- **Trigger**: Send any value > 0 to trigger a save
- **Filename**: Automatically generated with timestamp format: `uphonor_session_YYYYMMDD_HHMMSS.json`

### Example MIDI Message
- Send CC 81 with value 127 (or any value > 0) to save the current state
- The file will be saved in the current working directory
- You'll see a log message indicating success or failure

### What Gets Saved
- All global settings (volume, playback speed, pitch shift, etc.)
- **Only active loops** (excludes loops in IDLE state)
- Loop filenames and audio file references for active loops
- Sync mode settings
- Current playback states for active loops

**Note**: Loops in IDLE state (never used or fully reset) are automatically excluded from saved configurations to keep files clean and focused on actual content.

### Loading Saved Configurations
You can load saved configurations using the CLI:
```bash
./build/uphonor --load uphonor_session_20251208_143022
```

### Complete MIDI CC Reference
- CC 74: Playback speed control
- CC 75: Pitch shift control  
- CC 76: Record player mode
- CC 7: Volume control
- CC 77: Playback mode (normal/trigger)
- CC 78: Sync mode on/off
- CC 79: Sync playback cutoff
- CC 80: Sync recording cutoff
- **CC 81: Save configuration** (NEW)

## Notes
- The save function only saves configuration metadata, not the actual audio data
- Audio files are loaded automatically when loading a configuration (if they exist)
- Timestamp-based filenames prevent accidental overwrites
- Check the console/log output for save confirmation messages
