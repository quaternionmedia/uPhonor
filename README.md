# uPhonor Python Bindings

Python CFFI bindings for uPhonor, a real-time audio looping system built on PipeWire.

## Features

- Real-time audio processing and looping
- MIDI control interface
- Multi-loop recording and playback
- Pitch shifting and time stretching with Rubberband
- Sync mode for synchronized loop operations
- Configuration save/load functionality
- Full Python API with type hints

## Installation

### Prerequisites

Make sure you have the required system dependencies:

```bash
# On Ubuntu/Debian:
sudo apt install libpipewire-0.3-dev libsndfile1-dev libasound2-dev librubberband-dev libcjson-dev pkg-config

# On Fedora:
sudo dnf install pipewire-devel libsndfile-devel alsa-lib-devel rubberband-devel libcjson-devel pkgconfig
```

### Build and Install with uv

```bash
# Install uv if you haven't already
curl -LsSf https://astral.sh/uv/install.sh | sh

# Build the CFFI extension
uv run python build.py

# Run examples
uv run python examples.py

# Install in development mode
uv pip install -e .
```

## Quick Start

```python
from uphonor_python import uphonor_session

# Use context manager for automatic cleanup
with uphonor_session() as uphonor:
    # Start the audio system
    uphonor.start()
    
    # Set volume and playback parameters
    uphonor.set_volume(0.8)
    uphonor.set_playback_speed(1.2)
    
    # Handle MIDI input
    uphonor.handle_note_on(0, 60, 100)  # Start recording/playing loop on middle C
    uphonor.handle_note_off(0, 60, 0)   # Stop
    
    # Save session
    uphonor.save_state("my_session.json")
```

## Examples

See `examples.py` for comprehensive usage examples including:

- Basic usage and setup
- Recording and playback
- Loop management
- MIDI handling
- Configuration management
- Error handling

Run examples with: `uv run python examples.py`

## Testing

Run the test suite with: `uv run python test_bindings.py`

## API Reference

### Main Classes

- `UPhonor`: Main interface class
- `MemoryLoop`: Individual loop management
- `HoloState`, `LoopState`, `PlaybackMode`: State enumerations

### Key Methods

- Audio control: `set_volume()`, `set_playback_speed()`, `set_pitch_shift()`
- Loop management: `get_loop_by_note()`, `stop_all_recordings()`
- MIDI handling: `handle_note_on()`, `handle_note_off()`, `handle_control_change()`
- Configuration: `save_state()`, `load_state()`, `reset_to_defaults()`

## Development

```bash
# Install development dependencies
uv sync --group dev

# Format code
uv run black .
uv run ruff --fix .

# Type checking
uv run mypy uphonor_python.py

# Run tests
uv run pytest test_bindings.py
```

## License

MIT License - see LICENSE file for details.
