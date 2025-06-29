#!/usr/bin/env python3
"""
Test MIDI controls for uPhonor pitch and speed controls.
"""

import time
import subprocess
import sys

def send_midi_cc(channel, controller, value):
    """Send a MIDI Control Change message using amidi."""
    # Convert to hex format for amidi
    status_byte = 0xB0 + (channel - 1)  # Control Change + channel (0-based)
    hex_msg = f"{status_byte:02x} {controller:02x} {value:02x}"
    
    try:
        # Send to first available MIDI output
        cmd = f'echo "{hex_msg}" | xxd -r -p | amidi -p hw:1,0 -s'
        subprocess.run(cmd, shell=True, check=True)
        print(f"Sent CC {controller} = {value} on channel {channel}")
        return True
    except subprocess.CalledProcessError:
        try:
            # Try alternative method
            cmd = f'amidi -p hw:0,0 -S "{hex_msg}"'
            subprocess.run(cmd, shell=True, check=True)
            print(f"Sent CC {controller} = {value} on channel {channel}")
            return True
        except subprocess.CalledProcessError:
            print(f"Failed to send MIDI CC {controller} = {value}")
            return False

def test_speed_control():
    """Test CC 74 (speed control) at different values."""
    print("\n=== Testing Speed Control (CC 74) ===")
    
    # Test different speed values
    test_values = [0, 32, 64, 96, 127]  # Quarter speed to 4x speed
    
    for value in test_values:
        speed_factor = 0.25 + (value / 127.0) * 3.75  # 0.25x to 4x
        print(f"Setting speed to {speed_factor:.2f}x (CC 74 = {value})")
        send_midi_cc(1, 74, value)
        time.sleep(2)  # Give time for processing

def test_pitch_control():
    """Test CC 75 (pitch control) at different values."""
    print("\n=== Testing Pitch Control (CC 75) ===")
    
    # Test different pitch values
    test_values = [0, 32, 64, 96, 127]  # Half pitch to 4x pitch
    
    for value in test_values:
        pitch_factor = 0.25 + (value / 127.0) * 3.75  # 0.25x to 4x
        print(f"Setting pitch to {pitch_factor:.2f}x (CC 75 = {value})")
        send_midi_cc(1, 75, value)
        time.sleep(2)  # Give time for processing

def test_combined_controls():
    """Test speed and pitch controls independently."""
    print("\n=== Testing Independent Controls ===")
    
    # Reset both to center (1.0x)
    print("Resetting both controls to center (1.0x)")
    send_midi_cc(1, 74, 64)  # Speed = 1.0x
    send_midi_cc(1, 75, 64)  # Pitch = 1.0x
    time.sleep(2)
    
    # Change only speed
    print("Changing only speed to 2.0x, pitch should remain 1.0x")
    send_midi_cc(1, 74, 96)  # Speed = 2.0x
    time.sleep(3)
    
    # Change only pitch  
    print("Changing only pitch to 0.5x, speed should remain 2.0x")
    send_midi_cc(1, 75, 32)  # Pitch = 0.5x
    time.sleep(3)
    
    # Reset both
    print("Resetting both to center")
    send_midi_cc(1, 74, 64)
    send_midi_cc(1, 75, 64)

def main():
    print("uPhonor MIDI Control Test")
    print("Make sure uPhonor is running and MIDI is connected!")
    print("Watch the console output for control value changes...")
    
    try:
        test_speed_control()
        test_pitch_control()
        test_combined_controls()
        
        print("\nTest completed! Check uPhonor console for logged control changes.")
        
    except KeyboardInterrupt:
        print("\nTest interrupted by user")
    except Exception as e:
        print(f"Error during test: {e}")

if __name__ == "__main__":
    main()
