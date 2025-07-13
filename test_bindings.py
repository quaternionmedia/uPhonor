#!/usr/bin/env python3
"""
Test suite for uPhonor Python CFFI bindings

Run with: uv run python test_bindings.py
"""

import unittest
import tempfile
import os
from pathlib import Path

# Import our bindings
try:
    from uphonor_python import (
        UPhonor,
        MemoryLoop,
        uphonor_session,
        create_uphonor,
        HoloState,
        LoopState,
        PlaybackMode,
        ConfigResult,
    )

    BINDINGS_AVAILABLE = True
except ImportError as e:
    print(f"Warning: uPhonor bindings not available: {e}")
    BINDINGS_AVAILABLE = False


@unittest.skipUnless(BINDINGS_AVAILABLE, "uPhonor bindings not available")
class TestUPhonoBasics(unittest.TestCase):
    """Test basic uPhonor functionality"""

    def setUp(self):
        """Set up test case"""
        self.uphonor = UPhonor()
        self.assertTrue(self.uphonor.initialize(), "Failed to initialize uPhonor")

    def tearDown(self):
        """Clean up test case"""
        if hasattr(self, 'uphonor'):
            self.uphonor.cleanup()

    def test_initialization(self):
        """Test uPhonor initialization and cleanup"""
        # Should be initialized from setUp
        self.assertEqual(self.uphonor.get_state(), HoloState.IDLE)
        self.assertEqual(self.uphonor.get_volume(), 1.0)
        self.assertEqual(self.uphonor.get_playback_speed(), 1.0)
        self.assertEqual(self.uphonor.get_pitch_shift(), 0.0)

    def test_volume_control(self):
        """Test volume control"""
        # Test setting valid volumes
        test_volumes = [0.0, 0.5, 0.8, 1.0]
        for vol in test_volumes:
            self.uphonor.set_volume(vol)
            self.assertAlmostEqual(self.uphonor.get_volume(), vol, places=6)

    def test_playback_speed_control(self):
        """Test playback speed control"""
        test_speeds = [0.5, 1.0, 1.5, 2.0]
        for speed in test_speeds:
            self.uphonor.set_playback_speed(speed)
            self.assertAlmostEqual(self.uphonor.get_playback_speed(), speed, places=6)

    def test_pitch_shift_control(self):
        """Test pitch shift control"""
        test_pitches = [-12.0, -5.0, 0.0, 3.0, 12.0]
        for pitch in test_pitches:
            self.uphonor.set_pitch_shift(pitch)
            self.assertAlmostEqual(self.uphonor.get_pitch_shift(), pitch, places=6)

    def test_rubberband_control(self):
        """Test rubberband enable/disable"""
        # Should start enabled
        self.assertTrue(self.uphonor.is_rubberband_enabled())

        # Disable and check
        self.uphonor.set_rubberband_enabled(False)
        self.assertFalse(self.uphonor.is_rubberband_enabled())

        # Re-enable and check
        self.uphonor.set_rubberband_enabled(True)
        self.assertTrue(self.uphonor.is_rubberband_enabled())


@unittest.skipUnless(BINDINGS_AVAILABLE, "uPhonor bindings not available")
class TestPlaybackModes(unittest.TestCase):
    """Test playback mode functionality"""

    def setUp(self):
        self.uphonor = UPhonor()
        self.assertTrue(self.uphonor.initialize())

    def tearDown(self):
        self.uphonor.cleanup()

    def test_playback_mode_switching(self):
        """Test switching between playback modes"""
        # Should start in trigger mode
        self.assertEqual(self.uphonor.get_playback_mode(), PlaybackMode.TRIGGER)
        self.assertEqual(self.uphonor.get_playback_mode_name(), "trigger")

        # Switch to normal mode
        self.uphonor.set_playback_mode_normal()
        self.assertEqual(self.uphonor.get_playback_mode(), PlaybackMode.NORMAL)
        self.assertEqual(self.uphonor.get_playback_mode_name(), "normal")

        # Switch back to trigger mode
        self.uphonor.set_playback_mode_trigger()
        self.assertEqual(self.uphonor.get_playback_mode(), PlaybackMode.TRIGGER)

        # Test toggle function
        self.uphonor.toggle_playback_mode()
        self.assertEqual(self.uphonor.get_playback_mode(), PlaybackMode.NORMAL)

        self.uphonor.toggle_playback_mode()
        self.assertEqual(self.uphonor.get_playback_mode(), PlaybackMode.TRIGGER)


@unittest.skipUnless(BINDINGS_AVAILABLE, "uPhonor bindings not available")
class TestSyncMode(unittest.TestCase):
    """Test sync mode functionality"""

    def setUp(self):
        self.uphonor = UPhonor()
        self.assertTrue(self.uphonor.initialize())

    def tearDown(self):
        self.uphonor.cleanup()

    def test_sync_mode_control(self):
        """Test sync mode enable/disable"""
        # Should start disabled
        self.assertFalse(self.uphonor.is_sync_mode_enabled())

        # Enable sync mode
        self.uphonor.enable_sync_mode()
        self.assertTrue(self.uphonor.is_sync_mode_enabled())

        # Disable sync mode
        self.uphonor.disable_sync_mode()
        self.assertFalse(self.uphonor.is_sync_mode_enabled())

        # Test toggle
        self.uphonor.toggle_sync_mode()
        self.assertTrue(self.uphonor.is_sync_mode_enabled())

        self.uphonor.toggle_sync_mode()
        self.assertFalse(self.uphonor.is_sync_mode_enabled())

    def test_pulse_loop_note(self):
        """Test pulse loop note management"""
        # Should start with no pulse loop
        self.assertIsNone(self.uphonor.get_pulse_loop_note())


@unittest.skipUnless(BINDINGS_AVAILABLE, "uPhonor bindings not available")
class TestMIDIHandling(unittest.TestCase):
    """Test MIDI message handling"""

    def setUp(self):
        self.uphonor = UPhonor()
        self.assertTrue(self.uphonor.initialize())
        self.uphonor.start()

    def tearDown(self):
        self.uphonor.stop()
        self.uphonor.cleanup()

    def test_note_on_off(self):
        """Test MIDI Note On/Off handling"""
        # These should not crash
        self.uphonor.handle_note_on(0, 60, 100)
        self.uphonor.handle_note_off(0, 60, 0)

        # Test with different notes and velocities
        for note in [36, 60, 84, 127]:
            for velocity in [1, 64, 127]:
                self.uphonor.handle_note_on(0, note, velocity)
                self.uphonor.handle_note_off(0, note, 0)

    def test_control_change(self):
        """Test MIDI Control Change handling"""
        # Test various control changes
        test_controls = [
            (7, 100),  # Volume
            (10, 64),  # Pan
            (1, 50),  # Modulation
            (64, 127),  # Sustain pedal
        ]

        for controller, value in test_controls:
            # Should not crash
            self.uphonor.handle_control_change(0, controller, value)


@unittest.skipUnless(BINDINGS_AVAILABLE, "uPhonor bindings not available")
class TestLoopManagement(unittest.TestCase):
    """Test loop management functionality"""

    def setUp(self):
        self.uphonor = UPhonor()
        self.assertTrue(self.uphonor.initialize())
        self.uphonor.start()

    def tearDown(self):
        self.uphonor.stop()
        self.uphonor.cleanup()

    def test_get_loop_by_note(self):
        """Test getting loops by MIDI note"""
        # Test valid notes
        for note in [0, 60, 127]:
            loop = self.uphonor.get_loop_by_note(note)
            self.assertIsInstance(loop, MemoryLoop)
            self.assertEqual(loop.midi_note, note)

        # Test invalid notes
        invalid_notes = [-1, 128, 200]
        for note in invalid_notes:
            loop = self.uphonor.get_loop_by_note(note)
            self.assertIsNone(loop)

    def test_loop_properties(self):
        """Test memory loop properties"""
        loop = self.uphonor.get_loop_by_note(60)
        self.assertIsNotNone(loop)

        # Test property access
        self.assertIsInstance(loop.buffer_size, int)
        self.assertIsInstance(loop.recorded_frames, int)
        self.assertIsInstance(loop.playback_position, int)
        self.assertIsInstance(loop.loop_ready, bool)
        self.assertIsInstance(loop.recording_to_memory, bool)
        self.assertIsInstance(loop.is_playing, bool)
        self.assertIsInstance(loop.midi_note, int)
        self.assertIsInstance(loop.volume, float)
        self.assertIsInstance(loop.current_state, LoopState)
        self.assertIsInstance(loop.loop_filename, str)
        self.assertIsInstance(loop.sample_rate, int)

    def test_stop_functions(self):
        """Test stop all recordings and playback"""
        # These should not crash
        self.uphonor.stop_all_recordings()
        self.uphonor.stop_all_playback()

    def test_active_loop_count(self):
        """Test active loop count"""
        count = self.uphonor.get_active_loop_count()
        self.assertIsInstance(count, int)
        self.assertGreaterEqual(count, 0)

    def test_currently_recording_note(self):
        """Test currently recording note"""
        note = self.uphonor.get_currently_recording_note()
        # Should be None or a valid MIDI note
        if note is not None:
            self.assertIsInstance(note, int)
            self.assertGreaterEqual(note, 0)
            self.assertLessEqual(note, 127)

    def test_get_all_loops(self):
        """Test getting all loops"""
        all_loops = self.uphonor.get_all_loops()
        self.assertIsInstance(all_loops, dict)

        # All keys should be valid MIDI notes
        for note in all_loops.keys():
            self.assertIsInstance(note, int)
            self.assertGreaterEqual(note, 0)
            self.assertLessEqual(note, 127)

        # All values should be MemoryLoop instances
        for loop in all_loops.values():
            self.assertIsInstance(loop, MemoryLoop)


@unittest.skipUnless(BINDINGS_AVAILABLE, "uPhonor bindings not available")
class TestConfiguration(unittest.TestCase):
    """Test configuration save/load functionality"""

    def setUp(self):
        self.uphonor = UPhonor()
        self.assertTrue(self.uphonor.initialize())
        self.test_dir = tempfile.mkdtemp()

    def tearDown(self):
        self.uphonor.cleanup()
        # Clean up test files
        import shutil

        shutil.rmtree(self.test_dir, ignore_errors=True)

    def test_save_load_state(self):
        """Test configuration save and load"""
        config_file = os.path.join(self.test_dir, "test_config.json")

        # Configure some settings
        self.uphonor.set_volume(0.75)
        self.uphonor.set_playback_speed(1.25)
        self.uphonor.set_pitch_shift(3.0)
        self.uphonor.enable_sync_mode()

        # Save configuration
        result = self.uphonor.save_state(config_file)
        self.assertEqual(result, ConfigResult.SUCCESS)
        self.assertTrue(os.path.exists(config_file))

        # Reset to defaults
        self.uphonor.reset_to_defaults()
        self.assertAlmostEqual(self.uphonor.get_volume(), 1.0)

        # Load configuration
        result = self.uphonor.load_state(config_file)
        self.assertEqual(result, ConfigResult.SUCCESS)

        # Check if settings were restored
        self.assertAlmostEqual(self.uphonor.get_volume(), 0.75, places=5)
        self.assertAlmostEqual(self.uphonor.get_playback_speed(), 1.25, places=5)
        self.assertAlmostEqual(self.uphonor.get_pitch_shift(), 3.0, places=5)
        self.assertTrue(self.uphonor.is_sync_mode_enabled())

    def test_save_active_loops_only(self):
        """Test saving only active loops"""
        config_file = os.path.join(self.test_dir, "test_loops.json")

        result = self.uphonor.save_active_loops_only(config_file)
        # Should succeed even with no active loops
        self.assertEqual(result, ConfigResult.SUCCESS)

    def test_validate_config_file(self):
        """Test config file validation"""
        config_file = os.path.join(self.test_dir, "test_config.json")

        # Create a config file first
        result = self.uphonor.save_state(config_file)
        self.assertEqual(result, ConfigResult.SUCCESS)

        # Validate it
        result = UPhonor.validate_config_file(config_file)
        self.assertEqual(result, ConfigResult.SUCCESS)

        # Test with non-existent file
        result = UPhonor.validate_config_file("/nonexistent/file.json")
        self.assertEqual(result, ConfigResult.ERROR_FILE_NOT_FOUND)

    def test_config_error_messages(self):
        """Test config error message retrieval"""
        for result in ConfigResult:
            message = UPhonor.get_config_error_message(result)
            self.assertIsInstance(message, str)
            self.assertGreater(len(message), 0)


@unittest.skipUnless(BINDINGS_AVAILABLE, "uPhonor bindings not available")
class TestUtilities(unittest.TestCase):
    """Test utility functions"""

    def test_linear_to_db_volume(self):
        """Test linear to dB volume conversion"""
        # Test some known conversions
        test_cases = [
            (0.0, float('-inf')),  # 0 linear = -inf dB
            (1.0, 0.0),  # 1 linear = 0 dB
            (0.5, -6.0),  # 0.5 linear ≈ -6 dB (approximately)
        ]

        for linear, expected_db in test_cases:
            result = UPhonor.linear_to_db_volume(linear)
            if expected_db == float('-inf'):
                self.assertTrue(result < -100)  # Very negative for 0 input
            else:
                self.assertAlmostEqual(result, expected_db, delta=0.5)


@unittest.skipUnless(BINDINGS_AVAILABLE, "uPhonor bindings not available")
class TestContextManager(unittest.TestCase):
    """Test context manager functionality"""

    def test_uphonor_session(self):
        """Test uphonor_session context manager"""
        with uphonor_session() as uphonor:
            self.assertIsInstance(uphonor, UPhonor)
            self.assertEqual(uphonor.get_state(), HoloState.IDLE)

            # Test basic operations
            uphonor.set_volume(0.5)
            self.assertAlmostEqual(uphonor.get_volume(), 0.5)

        # Context manager should clean up automatically

    def test_create_uphonor(self):
        """Test create_uphonor convenience function"""
        uphonor = create_uphonor()
        self.assertIsInstance(uphonor, UPhonor)

        # Initialize and test
        self.assertTrue(uphonor.initialize())
        self.assertEqual(uphonor.get_state(), HoloState.IDLE)

        # Clean up
        uphonor.cleanup()


@unittest.skipUnless(BINDINGS_AVAILABLE, "uPhonor bindings not available")
class TestErrorHandling(unittest.TestCase):
    """Test error handling and edge cases"""

    def test_uninitialized_access(self):
        """Test accessing uninitialized uPhonor"""
        uphonor = UPhonor()

        # These should return safe default values
        self.assertEqual(uphonor.get_volume(), 0.0)
        self.assertEqual(uphonor.get_state(), HoloState.IDLE)
        self.assertEqual(uphonor.get_playback_speed(), 1.0)
        self.assertFalse(uphonor.is_sync_mode_enabled())

        # These should return None/empty
        self.assertIsNone(uphonor.get_loop_by_note(60))
        self.assertEqual(uphonor.get_all_loops(), {})

        # Operations should fail gracefully
        self.assertFalse(uphonor.start())
        self.assertEqual(uphonor.save_state(), ConfigResult.ERROR_MEMORY)

    def test_invalid_midi_notes(self):
        """Test handling of invalid MIDI notes"""
        uphonor = UPhonor()
        uphonor.initialize()

        try:
            # Invalid notes should return None
            invalid_notes = [-1, 128, 255, 1000]
            for note in invalid_notes:
                loop = uphonor.get_loop_by_note(note)
                self.assertIsNone(loop)

            # MIDI functions should handle invalid notes gracefully
            uphonor.handle_note_on(0, -1, 100)  # Should not crash
            uphonor.handle_note_on(0, 128, 100)  # Should not crash
        finally:
            uphonor.cleanup()


def run_tests():
    """Run all tests"""
    if not BINDINGS_AVAILABLE:
        print("Skipping tests - uPhonor bindings not available")
        print("Run 'python build.py' first to build the bindings")
        return

    # Create test suite
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    # Add all test classes
    test_classes = [
        TestUPhonoBasics,
        TestPlaybackModes,
        TestSyncMode,
        TestMIDIHandling,
        TestLoopManagement,
        TestConfiguration,
        TestUtilities,
        TestContextManager,
        TestErrorHandling,
    ]

    for test_class in test_classes:
        tests = loader.loadTestsFromTestCase(test_class)
        suite.addTests(tests)

    # Run tests
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    # Print summary
    if result.wasSuccessful():
        print(f"\n✓ All {result.testsRun} tests passed!")
    else:
        print(f"\n✗ {len(result.failures)} failures, {len(result.errors)} errors")
        return 1

    return 0


if __name__ == "__main__":
    import sys

    sys.exit(run_tests())
