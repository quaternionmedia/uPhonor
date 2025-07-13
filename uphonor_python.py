"""
Python CFFI bindings for uPhonor - A real-time audio looping system
"""

# Handle both standalone and package imports
try:
    from ._uphonor_cffi import ffi, lib
except ImportError:
    try:
        from _uphonor_cffi import ffi, lib
    except ImportError:
        print("Error: CFFI extension not available. Run 'python build_cffi.py' first.")
        raise

import os
import sys
from typing import Optional, List, Dict, Any
from enum import IntEnum
from contextlib import contextmanager


class HoloState(IntEnum):
    """Holophonor system states"""

    IDLE = 0
    PLAYING = 1
    STOPPED = 2


class LoopState(IntEnum):
    """Individual loop states"""

    IDLE = 0
    RECORDING = 1
    PLAYING = 2
    STOPPED = 3


class PlaybackMode(IntEnum):
    """Playback mode types"""

    NORMAL = 0  # Note On toggles play/stop, Note Off ignored
    TRIGGER = 1  # Note On starts, Note Off stops


class ConfigResult(IntEnum):
    """Configuration operation results"""

    SUCCESS = 0
    ERROR_FILE_NOT_FOUND = -1
    ERROR_PARSE_FAILED = -2
    ERROR_WRITE_FAILED = -3
    ERROR_INVALID_VERSION = -4
    ERROR_MEMORY = -5
    ERROR_INVALID_DATA = -6


class MemoryLoop:
    """Wrapper for memory_loop structure"""

    def __init__(self, cffi_loop):
        self._cffi_loop = cffi_loop

    @property
    def buffer_size(self) -> int:
        return self._cffi_loop.buffer_size

    @property
    def recorded_frames(self) -> int:
        return self._cffi_loop.recorded_frames

    @property
    def playback_position(self) -> int:
        return self._cffi_loop.playback_position

    @property
    def loop_ready(self) -> bool:
        return self._cffi_loop.loop_ready

    @property
    def recording_to_memory(self) -> bool:
        return self._cffi_loop.recording_to_memory

    @property
    def is_playing(self) -> bool:
        return self._cffi_loop.is_playing

    @property
    def midi_note(self) -> int:
        return self._cffi_loop.midi_note

    @property
    def volume(self) -> float:
        return self._cffi_loop.volume

    @property
    def current_state(self) -> LoopState:
        return LoopState(self._cffi_loop.current_state)

    @property
    def loop_filename(self) -> str:
        return ffi.string(self._cffi_loop.loop_filename).decode('utf-8')

    @property
    def sample_rate(self) -> int:
        return self._cffi_loop.sample_rate


class UPhonor:
    """Main uPhonor interface class"""

    def __init__(self):
        self._data = None
        self._initialized = False

    def __enter__(self):
        self.initialize()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.cleanup()

    def initialize(self) -> bool:
        """Initialize the uPhonor system"""
        if self._initialized:
            return True

        self._data = lib.uphonor_init()
        if self._data == ffi.NULL:
            return False

        self._initialized = True
        return True

    def cleanup(self):
        """Clean up uPhonor resources"""
        if self._initialized and self._data != ffi.NULL:
            lib.uphonor_cleanup(self._data)
            self._data = ffi.NULL
            self._initialized = False

    def start(self) -> bool:
        """Start the uPhonor processing"""
        if not self._initialized:
            return False
        return lib.uphonor_start(self._data) == 0

    def stop(self):
        """Stop the uPhonor processing"""
        if self._initialized:
            lib.uphonor_stop(self._data)

    # Audio control methods

    def set_volume(self, volume: float):
        """Set the global volume (0.0 to 1.0)"""
        if self._initialized:
            lib.set_volume(self._data, volume)

    def get_volume(self) -> float:
        """Get the current global volume"""
        if self._initialized:
            return self._data.volume
        return 0.0

    def set_playback_speed(self, speed: float):
        """Set playback speed multiplier (1.0 = normal, 0.5 = half, 2.0 = double)"""
        if self._initialized:
            lib.set_playback_speed(self._data, speed)

    def get_playback_speed(self) -> float:
        """Get current playback speed"""
        if self._initialized:
            return self._data.playback_speed
        return 1.0

    def set_pitch_shift(self, semitones: float):
        """Set pitch shift in semitones (12 = one octave up, -12 = one octave down)"""
        if self._initialized:
            lib.set_pitch_shift(self._data, semitones)

    def get_pitch_shift(self) -> float:
        """Get current pitch shift in semitones"""
        if self._initialized:
            return self._data.pitch_shift
        return 0.0

    def set_rubberband_enabled(self, enabled: bool):
        """Enable/disable rubberband time stretching"""
        if self._initialized:
            lib.set_rubberband_enabled(self._data, enabled)

    def is_rubberband_enabled(self) -> bool:
        """Check if rubberband is enabled"""
        if self._initialized:
            return self._data.rubberband_enabled
        return False

    # Loop management methods

    def get_loop_by_note(self, midi_note: int) -> Optional[MemoryLoop]:
        """Get memory loop for a specific MIDI note (0-127)"""
        if not self._initialized or midi_note < 0 or midi_note > 127:
            return None

        cffi_loop = lib.get_loop_by_note(self._data, midi_note)
        if cffi_loop == ffi.NULL:
            return None

        return MemoryLoop(cffi_loop)

    def stop_all_recordings(self):
        """Stop all active recordings"""
        if self._initialized:
            lib.stop_all_recordings(self._data)

    def stop_all_playback(self):
        """Stop all loop playback"""
        if self._initialized:
            lib.stop_all_playback(self._data)

    def get_active_loop_count(self) -> int:
        """Get number of active loops"""
        if self._initialized:
            return self._data.active_loop_count
        return 0

    def get_currently_recording_note(self) -> Optional[int]:
        """Get MIDI note currently being recorded (None if none)"""
        if self._initialized:
            note = self._data.currently_recording_note
            return note if note != 255 else None
        return None

    # Playback mode methods

    def set_playback_mode_normal(self):
        """Set playback mode to normal (Note On toggles play/stop)"""
        if self._initialized:
            lib.set_playback_mode_normal(self._data)

    def set_playback_mode_trigger(self):
        """Set playback mode to trigger (Note On starts, Note Off stops)"""
        if self._initialized:
            lib.set_playback_mode_trigger(self._data)

    def toggle_playback_mode(self):
        """Toggle between normal and trigger playback modes"""
        if self._initialized:
            lib.toggle_playback_mode(self._data)

    def get_playback_mode(self) -> PlaybackMode:
        """Get current playback mode"""
        if self._initialized:
            return PlaybackMode(self._data.current_playback_mode)
        return PlaybackMode.TRIGGER

    def get_playback_mode_name(self) -> str:
        """Get current playback mode name as string"""
        if self._initialized:
            name = lib.get_playback_mode_name(self._data)
            return ffi.string(name).decode('utf-8')
        return "unknown"

    # Sync mode methods

    def enable_sync_mode(self):
        """Enable sync mode for synchronized loop operations"""
        if self._initialized:
            lib.enable_sync_mode(self._data)

    def disable_sync_mode(self):
        """Disable sync mode"""
        if self._initialized:
            lib.disable_sync_mode(self._data)

    def toggle_sync_mode(self):
        """Toggle sync mode on/off"""
        if self._initialized:
            lib.toggle_sync_mode(self._data)

    def is_sync_mode_enabled(self) -> bool:
        """Check if sync mode is enabled"""
        if self._initialized:
            return lib.is_sync_mode_enabled(self._data)
        return False

    def get_pulse_loop_note(self) -> Optional[int]:
        """Get MIDI note of the pulse/master loop (None if no pulse loop)"""
        if self._initialized:
            note = self._data.pulse_loop_note
            return note if note != 255 else None
        return None

    # MIDI handling methods

    def handle_note_on(self, channel: int, note: int, velocity: int):
        """Handle MIDI Note On message"""
        if self._initialized and 0 <= note <= 127 and 0 <= velocity <= 127:
            lib.handle_note_on(self._data, channel, note, velocity)

    def handle_note_off(self, channel: int, note: int, velocity: int):
        """Handle MIDI Note Off message"""
        if self._initialized and 0 <= note <= 127 and 0 <= velocity <= 127:
            lib.handle_note_off(self._data, channel, note, velocity)

    def handle_control_change(self, channel: int, controller: int, value: int):
        """Handle MIDI Control Change message"""
        if self._initialized and 0 <= controller <= 127 and 0 <= value <= 127:
            lib.handle_control_change(self._data, channel, controller, value)

    # Audio file operations

    def start_recording(self, filename: str) -> bool:
        """Start recording to file"""
        if self._initialized:
            filename_bytes = filename.encode('utf-8')
            return lib.start_recording(self._data, filename_bytes) == 0
        return False

    def stop_recording(self) -> bool:
        """Stop current recording"""
        if self._initialized:
            return lib.stop_recording(self._data) == 0
        return False

    def start_playing(self, filename: str) -> bool:
        """Start playing audio file"""
        if self._initialized:
            filename_bytes = filename.encode('utf-8')
            return lib.start_playing(self._data, filename_bytes) == 0
        return False

    def is_recording_enabled(self) -> bool:
        """Check if recording is currently enabled"""
        if self._initialized:
            return self._data.recording_enabled
        return False

    # Configuration methods

    def save_state(self, filename: Optional[str] = None) -> ConfigResult:
        """Save current state to configuration file"""
        if not self._initialized:
            return ConfigResult.ERROR_MEMORY

        filename_ptr = ffi.NULL
        if filename:
            filename_bytes = filename.encode('utf-8')
            filename_ptr = filename_bytes

        result = lib.config_save_state(self._data, filename_ptr)
        return ConfigResult(result)

    def load_state(self, filename: Optional[str] = None) -> ConfigResult:
        """Load state from configuration file"""
        if not self._initialized:
            return ConfigResult.ERROR_MEMORY

        filename_ptr = ffi.NULL
        if filename:
            filename_bytes = filename.encode('utf-8')
            filename_ptr = filename_bytes

        result = lib.config_load_state(self._data, filename_ptr)
        return ConfigResult(result)

    def save_active_loops_only(self, filename: Optional[str] = None) -> ConfigResult:
        """Save only active loops to configuration file"""
        if not self._initialized:
            return ConfigResult.ERROR_MEMORY

        filename_ptr = ffi.NULL
        if filename:
            filename_bytes = filename.encode('utf-8')
            filename_ptr = filename_bytes

        result = lib.config_save_active_loops_only(self._data, filename_ptr)
        return ConfigResult(result)

    def reset_to_defaults(self):
        """Reset configuration to default values"""
        if self._initialized:
            lib.config_reset_to_defaults(self._data)

    @staticmethod
    def validate_config_file(filename: str) -> ConfigResult:
        """Validate a configuration file without loading it"""
        filename_bytes = filename.encode('utf-8')
        result = lib.config_validate_file(filename_bytes)
        return ConfigResult(result)

    @staticmethod
    def get_config_error_message(result: ConfigResult) -> str:
        """Get human-readable error message for config result"""
        error_msg = lib.config_get_error_message(result.value)
        return ffi.string(error_msg).decode('utf-8')

    # Utility methods

    @staticmethod
    def linear_to_db_volume(linear_volume: float) -> float:
        """Convert linear volume to decibel volume"""
        return lib.linear_to_db_volume(linear_volume)

    def get_state(self) -> HoloState:
        """Get current system state"""
        if self._initialized:
            return HoloState(self._data.current_state)
        return HoloState.IDLE

    def get_all_loops(self) -> Dict[int, MemoryLoop]:
        """Get all memory loops as a dictionary indexed by MIDI note"""
        loops = {}
        if self._initialized:
            for i in range(128):
                loop = self.get_loop_by_note(i)
                if loop and loop.loop_ready:
                    loops[i] = loop
        return loops


# Convenience context manager
@contextmanager
def uphonor_session():
    """Context manager for uPhonor sessions"""
    uphonor = UPhonor()
    try:
        if uphonor.initialize():
            yield uphonor
        else:
            raise RuntimeError("Failed to initialize uPhonor")
    finally:
        uphonor.cleanup()


# Module-level convenience functions
def create_uphonor() -> UPhonor:
    """Create a new UPhonor instance"""
    return UPhonor()


__all__ = [
    'UPhonor',
    'MemoryLoop',
    'HoloState',
    'LoopState',
    'PlaybackMode',
    'ConfigResult',
    'uphonor_session',
    'create_uphonor',
]
