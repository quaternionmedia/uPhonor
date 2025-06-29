#!/usr/bin/env python3

def test_midi_mapping():
    """Test the MIDI CC to speed/pitch mapping"""
    
    def map_cc_to_value(cc_value):
        """Map MIDI CC value (0-127) to speed/pitch (0.25x to 4.0x)"""
        if cc_value <= 64:
            # Map 0-64 to 0.25-1.0
            return 0.25 + (cc_value / 64.0) * 0.75
        else:
            # Map 65-127 to 1.0-4.0
            return 1.0 + ((cc_value - 64.0) / 63.0) * 3.0
    
    print("MIDI CC to Speed/Pitch Mapping Test:")
    print("CC Value -> Output Value")
    print("-" * 25)
    
    # Test key values
    test_values = [0, 1, 32, 63, 64, 65, 96, 126, 127]
    
    for cc in test_values:
        result = map_cc_to_value(cc)
        print(f"CC {cc:3d} -> {result:.3f}x")
    
    print("\nChecking for problematic ranges:")
    
    # Check if value 0 gives minimum (0.25)
    val_0 = map_cc_to_value(0)
    print(f"CC 0: {val_0:.3f} (should be 0.25)")
    
    # Check if value 64 gives center (1.0)
    val_64 = map_cc_to_value(64)
    print(f"CC 64: {val_64:.3f} (should be 1.0)")
    
    # Check if value 127 gives maximum (4.0)
    val_127 = map_cc_to_value(127)
    print(f"CC 127: {val_127:.3f} (should be 4.0)")
    
    # Check values just below 64
    for cc in range(60, 65):
        result = map_cc_to_value(cc)
        print(f"CC {cc}: {result:.3f}x")

if __name__ == "__main__":
    test_midi_mapping()
