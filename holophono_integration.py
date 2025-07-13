#!/usr/bin/env python3
"""
Integration example showing how to use uPhonor CFFI bindings
with the Holophonor Python project

Run with: uv run python holophono_integration.py
"""

import sys
import os
import time
import threading
from pathlib import Path

# Add the uPhonor bindings to path
sys.path.insert(0, '/home/harpo/uPhonor')

try:
    from uphonor_python import UPhonor, uphonor_session, HoloState, PlaybackMode

    UPHONOR_AVAILABLE = True
except ImportError as e:
    print(f"uPhonor bindings not available: {e}")
    print("Run: uv run python build.py")
    UPHONOR_AVAILABLE = False

# Try to import from the existing Holophonor project
try:
    sys.path.insert(0, '/home/harpo/holophonor')
    import holophonor

    HOLOPHONOR_AVAILABLE = True
except ImportError:
    HOLOPHONOR_AVAILABLE = False
    print("Holophonor project not available - running standalone example")


class HoloPhonoIntegration:
    """
    Integration class that bridges Holophonor Python control
    with uPhonor C audio processing
    """

    def __init__(self):
        self.uphonor = None
        self.running = False
        self.midi_thread = None

    def initialize(self) -> bool:
        """Initialize both systems"""
        if not UPHONOR_AVAILABLE:
            print("❌ uPhonor bindings not available")
            return False

        # Initialize uPhonor
        self.uphonor = UPhonor()
        if not self.uphonor.initialize():
            print("❌ Failed to initialize uPhonor")
            return False

        print("✓ uPhonor initialized")

        # Configure for live performance
        self.uphonor.set_volume(0.8)
        self.uphonor.set_playback_mode_trigger()  # Note On starts, Note Off stops
        self.uphonor.enable_sync_mode()

        # Start the audio processing
        if not self.uphonor.start():
            print("❌ Failed to start uPhonor audio processing")
            return False

        print("✓ uPhonor audio processing started")
        return True

    def cleanup(self):
        """Clean up resources"""
        self.running = False

        if self.midi_thread and self.midi_thread.is_alive():
            self.midi_thread.join(timeout=1.0)

        if self.uphonor:
            self.uphonor.stop()
            self.uphonor.cleanup()
            print("✓ uPhonor cleaned up")

    def handle_midi_note_on(self, channel: int, note: int, velocity: int):
        """Handle incoming MIDI Note On"""
        if self.uphonor:
            self.uphonor.handle_note_on(channel, note, velocity)

            # Get loop info for feedback
            loop = self.uphonor.get_loop_by_note(note)
            if loop:
                print(
                    f"🎵 Note {note}: {loop.current_state.name}, "
                    f"Recording: {loop.recording_to_memory}, "
                    f"Playing: {loop.is_playing}"
                )

    def handle_midi_note_off(self, channel: int, note: int, velocity: int):
        """Handle incoming MIDI Note Off"""
        if self.uphonor:
            self.uphonor.handle_note_off(channel, note, velocity)

            loop = self.uphonor.get_loop_by_note(note)
            if loop:
                print(f"🎵 Note {note} OFF: {loop.current_state.name}")

    def handle_midi_control_change(self, channel: int, controller: int, value: int):
        """Handle MIDI Control Change messages"""
        if not self.uphonor:
            return

        # Map some common controllers to uPhonor functions
        if controller == 7:  # Volume
            volume = value / 127.0
            self.uphonor.set_volume(volume)
            print(f"🎛️  Volume: {volume:.2f}")

        elif controller == 1:  # Modulation -> Pitch shift
            # Map 0-127 to -12 to +12 semitones
            pitch = ((value - 64) / 64.0) * 12.0
            self.uphonor.set_pitch_shift(pitch)
            print(f"🎛️  Pitch shift: {pitch:.1f} semitones")

        elif controller == 74:  # Filter cutoff -> Playback speed
            # Map 0-127 to 0.5 to 2.0 speed
            speed = 0.5 + (value / 127.0) * 1.5
            self.uphonor.set_playback_speed(speed)
            print(f"🎛️  Playback speed: {speed:.2f}x")

        elif controller == 64:  # Sustain pedal -> Sync mode toggle
            if value >= 64:  # Pedal pressed
                self.uphonor.toggle_sync_mode()
                sync_enabled = self.uphonor.is_sync_mode_enabled()
                print(f"🎛️  Sync mode: {'ON' if sync_enabled else 'OFF'}")

        # Pass through to uPhonor for any additional processing
        self.uphonor.handle_control_change(channel, controller, value)

    def start_midi_simulation(self):
        """Start a thread that simulates MIDI input for demo"""
        if not self.uphonor:
            return

        def midi_simulation():
            """Simulate some MIDI activity"""
            notes = [60, 64, 67, 72]  # C major chord

            print("🎹 Starting MIDI simulation...")
            time.sleep(1)

            # Simulate recording some loops
            for i, note in enumerate(notes):
                if not self.running:
                    break

                print(f"🎹 Recording loop {i+1}/4 on note {note}")
                self.handle_midi_note_on(0, note, 100)
                time.sleep(2.0)  # Record for 2 seconds
                self.handle_midi_note_off(0, note, 0)
                time.sleep(0.5)  # Brief pause

            time.sleep(1)

            # Play back the loops
            print("🎹 Playing back loops...")
            for note in notes:
                if not self.running:
                    break

                self.handle_midi_note_on(0, note, 80)
                time.sleep(0.1)

            time.sleep(3)

            # Stop playback
            print("🎹 Stopping playback...")
            for note in notes:
                self.handle_midi_note_off(0, note, 0)

            # Test some control changes
            print("🎹 Testing control changes...")
            for cc_val in [32, 64, 96, 64]:  # Volume changes
                if not self.running:
                    break
                self.handle_midi_control_change(0, 7, cc_val)
                time.sleep(0.5)

            print("🎹 MIDI simulation completed")

        self.running = True
        self.midi_thread = threading.Thread(target=midi_simulation, daemon=True)
        self.midi_thread.start()

    def print_status(self):
        """Print current system status"""
        if not self.uphonor:
            return

        print("\n" + "=" * 50)
        print("🎵 HoloPhono Integration Status")
        print("=" * 50)
        print(f"State: {self.uphonor.get_state().name}")
        print(f"Volume: {self.uphonor.get_volume():.2f}")
        print(f"Playback Speed: {self.uphonor.get_playback_speed():.2f}x")
        print(f"Pitch Shift: {self.uphonor.get_pitch_shift():.1f} semitones")
        print(f"Playback Mode: {self.uphonor.get_playback_mode_name()}")
        print(f"Sync Mode: {'ON' if self.uphonor.is_sync_mode_enabled() else 'OFF'}")
        print(f"Active Loops: {self.uphonor.get_active_loop_count()}")

        # Show active loops
        all_loops = self.uphonor.get_all_loops()
        if all_loops:
            print(f"\nActive Loops:")
            for note, loop in all_loops.items():
                print(
                    f"  Note {note:3d}: {loop.current_state.name:10s} "
                    f"({loop.recorded_frames:6d} frames)"
                )

        print("=" * 50)

    def save_session(self, filename: str = None):
        """Save current session"""
        if self.uphonor:
            result = self.uphonor.save_state(filename)
            if result.value == 0:  # SUCCESS
                print(f"✓ Session saved to {filename or 'default file'}")
            else:
                print(
                    f"❌ Failed to save session: {UPhonor.get_config_error_message(result)}"
                )

    def load_session(self, filename: str):
        """Load a session"""
        if self.uphonor:
            result = self.uphonor.load_state(filename)
            if result.value == 0:  # SUCCESS
                print(f"✓ Session loaded from {filename}")
            else:
                print(
                    f"❌ Failed to load session: {UPhonor.get_config_error_message(result)}"
                )


def main():
    """Main demo function"""
    print("🎵 HoloPhono Integration Demo")
    print("Bridging Holophonor Python control with uPhonor C audio processing")
    print("-" * 60)

    if not UPHONOR_AVAILABLE:
        print("❌ uPhonor bindings not available.")
        print("   Run 'cd /home/harpo/uPhonor && python build.py' to build them.")
        return 1

    integration = HoloPhonoIntegration()

    try:
        # Initialize the system
        if not integration.initialize():
            return 1

        # Print initial status
        integration.print_status()

        # Start MIDI simulation
        integration.start_midi_simulation()

        # Let it run for a while
        print("\n🎵 Demo running... (15 seconds)")
        for i in range(15):
            time.sleep(1)
            if i % 5 == 4:  # Every 5 seconds
                integration.print_status()

        # Save session
        session_file = "/tmp/holophono_demo_session.json"
        integration.save_session(session_file)

        print("\n🎵 Demo completed successfully!")

    except KeyboardInterrupt:
        print("\n🛑 Demo interrupted by user")

    except Exception as e:
        print(f"\n❌ Demo failed: {e}")
        import traceback

        traceback.print_exc()
        return 1

    finally:
        integration.cleanup()

    return 0


def test_integration():
    """Test the integration without running the full demo"""
    print("🧪 Testing HoloPhono Integration...")

    if not UPHONOR_AVAILABLE:
        print("❌ uPhonor bindings not available")
        return False

    try:
        with uphonor_session() as uphonor:
            print("✓ uPhonor session created")

            # Test basic operations
            uphonor.set_volume(0.7)
            uphonor.set_playback_speed(1.1)
            uphonor.enable_sync_mode()

            # Test MIDI handling
            uphonor.handle_note_on(0, 60, 100)
            uphonor.handle_note_off(0, 60, 0)
            uphonor.handle_control_change(0, 7, 100)

            print("✓ MIDI handling works")

            # Test configuration
            import tempfile

            with tempfile.NamedTemporaryFile(suffix='.json', delete=False) as f:
                config_file = f.name

            try:
                result = uphonor.save_state(config_file)
                if result.value == 0:
                    print("✓ Configuration save works")
                else:
                    print("❌ Configuration save failed")
                    return False

                result = uphonor.load_state(config_file)
                if result.value == 0:
                    print("✓ Configuration load works")
                else:
                    print("❌ Configuration load failed")
                    return False

            finally:
                os.unlink(config_file)

            print("✓ All integration tests passed!")
            return True

    except Exception as e:
        print(f"❌ Integration test failed: {e}")
        return False


if __name__ == "__main__":
    import sys

    if len(sys.argv) > 1 and sys.argv[1] == "test":
        success = test_integration()
        sys.exit(0 if success else 1)
    else:
        sys.exit(main())
