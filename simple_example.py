#!/usr/bin/env python3
"""
Simple example usage of uPhonor Python bindings
This demonstrates the core functionality with just the implemented functions.
"""

import time

# Handle both standalone and package imports
try:
    from _uphonor_cffi import ffi, lib
except ImportError:
    print("Error: CFFI extension not available. Run 'python build_cffi.py' first.")
    exit(1)


def main():
    """Simple example using direct CFFI calls"""
    print("uPhonor Python Bindings - Simple Example")
    print("=" * 50)

    # Initialize uPhonor
    print("Initializing uPhonor...")
    data = lib.uphonor_init()
    if data == ffi.NULL:
        print("✗ Failed to initialize uPhonor")
        return
    print("✓ uPhonor initialized successfully")

    try:
        # Test basic functionality
        print("\n=== Basic Functionality Tests ===")

        # Test volume control
        print("Setting volume to 0.8...")
        lib.uphonor_set_volume(data, 0.8)
        print(f"Current volume: {data.volume}")

        # Test playback speed
        print("Setting playback speed to 1.5...")
        lib.uphonor_set_playback_speed(data, 1.5)
        print(f"Current playback speed: {data.playback_speed}")

        # Test pitch shift
        print("Setting pitch shift to +2 semitones...")
        lib.uphonor_set_pitch_shift(data, 2.0)
        print(f"Current pitch shift: {data.pitch_shift} semitones")

        # Test playback mode
        print("Setting playback mode to Normal...")
        lib.uphonor_set_playback_mode(data, 0)  # PLAYBACK_MODE_NORMAL
        print(f"Current playback mode: {data.current_playback_mode}")

        print("\n=== Loop Recording/Playback Tests ===")

        # Test MIDI note 60 (Middle C)
        note = 60
        velocity = 100

        print(f"Simulating Note ON for MIDI note {note}...")
        lib.uphonor_note_on(data, note, velocity)

        # Check loop state
        loop = data.memory_loops[note]
        print(f"Loop {note} state: {loop.current_state}")
        print(f"Loop {note} ready: {loop.loop_ready}")

        # Wait a bit
        time.sleep(1)

        print(f"Simulating Note OFF for MIDI note {note}...")
        lib.uphonor_note_off(data, note, velocity)

        # Check updated state
        print(f"Loop {note} state: {loop.current_state}")
        print(f"Loop {note} ready: {loop.loop_ready}")

        print("\n=== Testing Multiple Loops ===")

        # Test multiple notes
        notes = [60, 62, 64, 67]  # C major chord

        for note in notes:
            print(f"Testing loop {note}...")
            lib.uphonor_note_on(data, note, velocity)
            time.sleep(0.5)
            lib.uphonor_note_off(data, note, velocity)

            loop = data.memory_loops[note]
            print(f"  Loop {note} ready: {loop.loop_ready}")

        print("\n=== Sync Mode Test ===")

        print("Enabling sync mode...")
        lib.uphonor_enable_sync_mode(data, True)
        print(f"Sync mode enabled: {data.sync_mode_enabled}")

        print("Setting pulse loop to note 36 (kick drum)...")
        lib.uphonor_set_pulse_loop(data, 36)
        print(f"Pulse loop note: {data.pulse_loop_note}")

        print("\n=== Rubberband Test ===")

        print("Enabling Rubberband processing...")
        lib.uphonor_enable_rubberband(data, True)
        print(f"Rubberband enabled: {data.rubberband_enabled}")

        print("Disabling Rubberband processing...")
        lib.uphonor_enable_rubberband(data, False)
        print(f"Rubberband enabled: {data.rubberband_enabled}")

        print("\n=== Loop Volume Tests ===")

        for i, note in enumerate(notes):
            volume = 0.5 + (i * 0.2)  # Different volumes
            print(f"Setting loop {note} volume to {volume:.1f}...")
            lib.uphonor_set_loop_volume(data, note, volume)
            print(f"  Loop {note} volume: {data.memory_loops[note].volume}")

        print("\n✓ All tests completed successfully!")

    except Exception as e:
        print(f"✗ Error during testing: {e}")

    finally:
        # Cleanup
        print("\nCleaning up...")
        lib.uphonor_cleanup(data)
        print("✓ Cleanup complete")


if __name__ == "__main__":
    main()
