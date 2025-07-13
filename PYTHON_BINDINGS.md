# uPhonor Python CFFI Bindings

This directory contains Python CFFI bindings for the uPhonor real-time audio looping system.

## Files

- `setup.py` - CFFI build configuration and C extension setup
- `uphonor_python.py` - High-level Python API wrapper
- `examples.py` - Comprehensive usage examples
- `build.py` - Build script with dependency checking
- `test_bindings.py` - Test suite for the bindings

## Prerequisites

Before building, ensure you have the uPhonor C library dependencies:

```bash
# Ubuntu/Debian
sudo apt install libpipewire-0.3-dev libsndfile1-dev libasound2-dev librubberband-dev libcjson-dev

# Fedora
sudo dnf install pipewire-devel libsndfile-devel alsa-lib-devel rubberband-devel libcjson-devel
```

## Building

1. Install uv package manager:
```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
```

2. Install system dependencies:
```bash
# Ubuntu/Debian
sudo apt install libpipewire-0.3-dev libsndfile1-dev libasound2-dev librubberband-dev libcjson-dev

# Fedora  
sudo dnf install pipewire-devel libsndfile-devel alsa-lib-devel rubberband-devel libcjson-devel
```

3. Build the extension:
```bash
uv run python build.py
# Or use make: make build
```

4. Test the bindings:
```bash
uv run python examples.py
# Or use make: make examples
```

## Usage

```python
# Simple usage with uv
# uv run python -c "from uphonor_python import UPhonor, uphonor_session; ..."

from uphonor_python import UPhonor, uphonor_session

# Simple usage with context manager
with uphonor_session() as uphonor:
    uphonor.start()
    uphonor.set_volume(0.8)
    # ... use uphonor
```

See `examples.py` for comprehensive usage examples.

## Architecture

The bindings use CFFI to interface with the uPhonor C library:

- **Low-level CFFI layer**: Direct C function and structure access
- **High-level Python wrapper**: Object-oriented API with proper error handling
- **Type safety**: Full type hints and enum definitions
- **Memory management**: Automatic cleanup with context managers

## API Overview

### Core Classes

- `UPhonor`: Main interface for audio system control
- `MemoryLoop`: Individual loop management and status
- State enums: `HoloState`, `LoopState`, `PlaybackMode`, `ConfigResult`

### Key Features

- Real-time audio processing control
- MIDI input handling
- Multi-loop recording and playback
- Pitch shifting and time stretching
- Sync mode for synchronized operations
- Configuration persistence
- Comprehensive error handling

## Integration with Holophonor

These bindings are designed to integrate with the existing Holophonor Python project, providing a bridge between Python control logic and the high-performance C audio processing core.
