# MIDI CC 79 Sync Cutoff Implementation

## Overview
Added MIDI CC 79 control for sync cutoff percentage, allowing users to control when recorded loops sync to the current pulse position vs waiting for the next pulse cycle.

## Implementation Details

### 1. MIDI CC 79 Definition
- Added `#define SYNC_CUTOFF_CC_NUMBER 79` in `midi_processing.c`
- Maps MIDI values 0-127 to percentage 0.0-1.0

### 2. Data Structure
- Added `float sync_cutoff_percentage` field to main data structure in `uphonor.h`
- Initialized to 0.5f (50%) as default in `holo.c`

### 3. MIDI Handler
- Added CC 79 case in `handle_control_change()` function
- Converts MIDI value to percentage: `data->sync_cutoff_percentage = cc_value / 127.0f`
- Logs the new cutoff percentage for user feedback

### 4. Sync Logic Integration
Modified sync decision logic in three locations:

#### A. `midi_processing.c` - Note On Sync Decision
```c
if (data->sync_mode_enabled && pulse_loop && pulse_loop->current_state == MEMORY_LOOP_PLAYING) {
    uint32_t cutoff_position = (uint32_t)(data->sync_cutoff_percentage * pulse_loop->recorded_frames);
    if (pulse_loop->playback_position <= cutoff_position) {
        // Sync to current pulse position
    } else {
        // Wait for next pulse cycle
    }
}
```

#### B. `audio_processing_rt.c` - Loop Restart Sync Decision
Similar cutoff logic for when loops are restarted during playback.

#### C. `holo.c` - Recorded Loop Playback Start
When recorded loops finish and start playback, uses cutoff to decide sync timing.

## Usage
- **MIDI CC 79 = 0**: Always wait for next pulse cycle (0% cutoff)
- **MIDI CC 79 = 64**: 50% cutoff point (default)
- **MIDI CC 79 = 127**: Always sync to current position (100% cutoff)

## Musical Benefits
- **Low values (0-30%)**: Tight synchronization, always wait for clean pulse starts
- **Medium values (40-60%)**: Balanced - sync if early in pulse, wait if late
- **High values (70-100%)**: Immediate response - sync to current position whenever possible

This gives users real-time control over sync timing behavior based on their musical needs.
