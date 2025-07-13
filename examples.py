"""
Example usage of uPhonor Python bindings

Run with: uv run python examples.py
"""

import time
import threading

# Handle both standalone and package imports
try:
    from uphonor_python import (
        UPhonor,
        uphonor_session,
        HoloState,
        PlaybackMode,
        ConfigResult,
    )
except ImportError:
    from uphonor.uphonor_python import (
        UPhonor,
        uphonor_session,
        HoloState,
        PlaybackMode,
        ConfigResult,
    )


def basic_usage_example():
    """Basic usage example of uPhonor Python bindings"""
    print("=== Basic uPhonor Usage Example ===")

    # Method 1: Using context manager (recommended)
    with uphonor_session() as uphonor:
        print(f"uPhonor initialized successfully")
        print(f"Current state: {uphonor.get_state()}")
        print(f"Current volume: {uphonor.get_volume()}")
        print(f"Current playback mode: {uphonor.get_playback_mode_name()}")

        # Start the system
        if uphonor.start():
            print("uPhonor started successfully")

        # Adjust some parameters
        uphonor.set_volume(0.8)
        uphonor.set_playback_speed(1.2)  # 20% faster
        uphonor.set_pitch_shift(2.0)  # 2 semitones up

        print(f"Volume set to: {uphonor.get_volume()}")
        print(f"Playback speed: {uphonor.get_playback_speed()}")
        print(f"Pitch shift: {uphonor.get_pitch_shift()} semitones")

        # Enable sync mode
        uphonor.enable_sync_mode()
        print(f"Sync mode enabled: {uphonor.is_sync_mode_enabled()}")

        # Simulate some MIDI input
        print("\nSimulating MIDI Note On for note 60 (Middle C)")
        uphonor.handle_note_on(0, 60, 100)  # Channel 0, Note 60, Velocity 100

        # Check loop status
        loop = uphonor.get_loop_by_note(60)
        if loop:
            print(
                f"Loop for note 60 - State: {loop.current_state}, Ready: {loop.loop_ready}"
            )

        # Wait a bit then send Note Off
        time.sleep(0.1)
        print("Sending MIDI Note Off for note 60")
        uphonor.handle_note_off(0, 60, 0)

        # Stop the system
        uphonor.stop()
        print("uPhonor stopped")


def recording_example():
    """Example of recording functionality"""
    print("\n=== Recording Example ===")

    with uphonor_session() as uphonor:
        if not uphonor.start():
            print("Failed to start uPhonor")
            return

        # Start recording to a file
        recording_file = "test_recording.wav"
        if uphonor.start_recording(recording_file):
            print(f"Started recording to {recording_file}")
            print(f"Recording enabled: {uphonor.is_recording_enabled()}")

            # Simulate some activity
            time.sleep(0.5)

            # Stop recording
            if uphonor.stop_recording():
                print("Recording stopped successfully")
            else:
                print("Failed to stop recording")
        else:
            print("Failed to start recording")


def playback_mode_example():
    """Example of different playback modes"""
    print("\n=== Playback Mode Example ===")

    with uphonor_session() as uphonor:
        uphonor.start()

        print(f"Initial playback mode: {uphonor.get_playback_mode_name()}")

        # Switch to normal mode
        uphonor.set_playback_mode_normal()
        print(f"Changed to: {uphonor.get_playback_mode_name()}")

        # Switch to trigger mode
        uphonor.set_playback_mode_trigger()
        print(f"Changed to: {uphonor.get_playback_mode_name()}")

        # Toggle mode
        uphonor.toggle_playback_mode()
        print(f"Toggled to: {uphonor.get_playback_mode_name()}")


def loop_management_example():
    """Example of loop management"""
    print("\n=== Loop Management Example ===")

    with uphonor_session() as uphonor:
        uphonor.start()

        # Simulate recording several loops
        notes_to_record = [60, 64, 67, 72]  # C, E, G, high C

        for note in notes_to_record:
            print(f"Simulating recording for MIDI note {note}")
            uphonor.handle_note_on(0, note, 100)
            time.sleep(0.1)  # Simulate brief recording
            uphonor.handle_note_off(0, note, 0)

        print(f"Active loop count: {uphonor.get_active_loop_count()}")

        # Get information about all loops
        all_loops = uphonor.get_all_loops()
        print(f"Found {len(all_loops)} ready loops:")

        for note, loop in all_loops.items():
            print(
                f"  Note {note}: {loop.recorded_frames} frames, "
                f"state: {loop.current_state}, volume: {loop.volume}"
            )

        # Stop all playback
        uphonor.stop_all_playback()
        print("Stopped all playback")


def configuration_example():
    """Example of configuration save/load"""
    print("\n=== Configuration Example ===")

    with uphonor_session() as uphonor:
        uphonor.start()

        # Configure some settings
        uphonor.set_volume(0.75)
        uphonor.set_playback_speed(0.9)
        uphonor.set_pitch_shift(3.0)
        uphonor.enable_sync_mode()

        # Save configuration
        config_file = "uphonor_test_config.json"
        result = uphonor.save_state(config_file)

        if result == ConfigResult.SUCCESS:
            print(f"Configuration saved to {config_file}")
        else:
            print(
                f"Failed to save configuration: {UPhonor.get_config_error_message(result)}"
            )

        # Reset to defaults
        uphonor.reset_to_defaults()
        print(
            f"After reset - Volume: {uphonor.get_volume()}, "
            f"Speed: {uphonor.get_playback_speed()}"
        )

        # Load configuration back
        result = uphonor.load_state(config_file)

        if result == ConfigResult.SUCCESS:
            print(f"Configuration loaded from {config_file}")
            print(
                f"After load - Volume: {uphonor.get_volume()}, "
                f"Speed: {uphonor.get_playback_speed()}, "
                f"Pitch: {uphonor.get_pitch_shift()}"
            )
        else:
            print(
                f"Failed to load configuration: {UPhonor.get_config_error_message(result)}"
            )

        # Validate config file
        validation_result = UPhonor.validate_config_file(config_file)
        if validation_result == ConfigResult.SUCCESS:
            print("Configuration file is valid")
        else:
            print(
                f"Configuration file validation failed: {UPhonor.get_config_error_message(validation_result)}"
            )


def advanced_control_example():
    """Example of advanced control features"""
    print("\n=== Advanced Control Example ===")

    with uphonor_session() as uphonor:
        uphonor.start()

        # Test rubberband controls
        print(f"Rubberband enabled: {uphonor.is_rubberband_enabled()}")

        uphonor.set_rubberband_enabled(False)
        print(f"Rubberband disabled: {not uphonor.is_rubberband_enabled()}")

        uphonor.set_rubberband_enabled(True)
        print(f"Rubberband re-enabled: {uphonor.is_rubberband_enabled()}")

        # Test volume conversion
        linear_vol = 0.5
        db_vol = UPhonor.linear_to_db_volume(linear_vol)
        print(f"Linear volume {linear_vol} = {db_vol:.2f} dB")

        # Test control change handling
        print("Sending MIDI Control Change messages")
        uphonor.handle_control_change(0, 7, 100)  # Volume control
        uphonor.handle_control_change(0, 10, 64)  # Pan control

        # Test sync mode features
        uphonor.toggle_sync_mode()
        print(f"Sync mode toggled to: {uphonor.is_sync_mode_enabled()}")

        pulse_loop_note = uphonor.get_pulse_loop_note()
        if pulse_loop_note is not None:
            print(f"Pulse loop note: {pulse_loop_note}")
        else:
            print("No pulse loop set")


def error_handling_example():
    """Example of error handling"""
    print("\n=== Error Handling Example ===")

    # Test initialization failure handling
    uphonor = UPhonor()

    # Try operations before initialization
    print(f"Volume before init: {uphonor.get_volume()}")  # Should return 0.0
    print(f"State before init: {uphonor.get_state()}")  # Should return IDLE

    # Initialize properly
    if uphonor.initialize():
        print("uPhonor initialized successfully")

        # Test invalid MIDI note handling
        invalid_loop = uphonor.get_loop_by_note(200)  # Invalid note (>127)
        print(f"Invalid loop request returned: {invalid_loop}")  # Should be None

        # Test invalid file operations
        success = uphonor.start_recording("/invalid/path/test.wav")
        print(f"Invalid recording path result: {success}")  # Should be False

        # Clean up
        uphonor.cleanup()
        print("uPhonor cleaned up")
    else:
        print("Failed to initialize uPhonor")


def main():
    """Run all examples"""
    print("uPhonor Python Bindings Examples")
    print("=" * 40)

    try:
        basic_usage_example()
        recording_example()
        playback_mode_example()
        loop_management_example()
        configuration_example()
        advanced_control_example()
        error_handling_example()

        print("\n=== All Examples Completed ===")

    except Exception as e:
        print(f"Error running examples: {e}")
        import traceback

        traceback.print_exc()


if __name__ == "__main__":
    main()
