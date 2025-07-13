#!/usr/bin/env python3
"""
Build script for uPhonor Python CFFI bindings using uv
Run this to compile the C extension
"""

import subprocess
import sys
import os
from pathlib import Path


def run_command(cmd, cwd=None):
    """Run a command and return success status"""
    try:
        result = subprocess.run(
            cmd, shell=True, check=True, cwd=cwd, capture_output=True, text=True
        )
        print(f"✓ {cmd}")
        if result.stdout:
            print(f"  {result.stdout.strip()}")
        return True
    except subprocess.CalledProcessError as e:
        print(f"✗ {cmd}")
        print(f"  Error: {e}")
        if e.stdout:
            print(f"  stdout: {e.stdout}")
        if e.stderr:
            print(f"  stderr: {e.stderr}")
        return False


def check_uv_installed():
    """Check if uv is installed"""
    try:
        result = subprocess.run(
            ["uv", "--version"], capture_output=True, text=True, check=True
        )
        print(f"✓ uv found: {result.stdout.strip()}")
        return True
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("❌ uv not found. Please install uv first:")
        print("   curl -LsSf https://astral.sh/uv/install.sh | sh")
        print("   Or visit: https://docs.astral.sh/uv/getting-started/installation/")
        return False


def check_pdm_installed():
    """Check if PDM is installed"""
    try:
        result = subprocess.run(
            ["pdm", "--version"], capture_output=True, text=True, check=True
        )
        print(f"✓ PDM found: {result.stdout.strip()}")
        return True
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("❌ PDM not found. Please install PDM first:")
        print("   curl -sSL https://pdm.fming.dev/install-pdm.py | python3 -")
        print("   Or visit: https://pdm.fming.dev/latest/")
        return False


def check_dependencies():
    """Check if required system dependencies are available"""
    print("Checking system dependencies...")

    # Check for pkg-config
    if not run_command("pkg-config --version"):
        print("pkg-config is required but not found")
        return False

    # Check for required libraries
    required_libs = ['libpipewire-0.3', 'sndfile', 'alsa', 'rubberband', 'libcjson']

    for lib in required_libs:
        if not run_command(f"pkg-config --exists {lib}"):
            print(f"Required library {lib} not found")
            return False

    print("✓ All system dependencies found")
    return True


def setup_project():
    """Setup project with PDM or uv"""
    print("Setting up project...")

    # Try PDM first, then fall back to uv
    if check_pdm_installed():
        print("Using PDM for project management...")
        if not run_command("pdm install"):
            print("Failed to install dependencies with PDM")
            return False
        print("✓ PDM project setup complete")
        return True
    elif check_uv_installed():
        print("Using uv for project management...")
        if not run_command("uv sync"):
            print("Failed to sync uv project")
            return False
        print("✓ uv project synced")
        return True
    else:
        print("❌ Neither PDM nor uv found. Please install one of them.")
        return False


def build_cffi_extension():
    """Build the CFFI extension"""
    print("Building CFFI extension...")

    # Try PDM first, then fall back to uv
    if check_pdm_installed():
        if not run_command("pdm run python build_cffi.py"):
            print("Failed to build CFFI extension with PDM")
            return False
    elif check_uv_installed():
        if not run_command("uv run python build_cffi.py"):
            print("Failed to build CFFI extension with uv")
            return False
    else:
        # Fall back to direct python execution
        if not run_command("python build_cffi.py"):
            print("Failed to build CFFI extension")
            return False

    print("✓ CFFI extension built successfully")
    return True


def test_import():
    """Test if the built extension can be imported"""
    print("Testing import...")

    try:
        # Determine which runner to use
        runner_cmd = []
        if check_pdm_installed():
            runner_cmd = ["pdm", "run", "python"]
        elif check_uv_installed():
            runner_cmd = ["uv", "run", "python"]
        else:
            runner_cmd = ["python"]

        # Try to import the built extension
        result = subprocess.run(
            runner_cmd + ["-c", "import _uphonor_cffi; print('CFFI extension OK')"],
            capture_output=True,
            text=True,
            check=True,
        )
        print("✓ CFFI extension imports successfully")

        # Try to import the Python wrapper
        result = subprocess.run(
            runner_cmd
            + ["-c", "import uphonor.uphonor_python; print('Python wrapper OK')"],
            capture_output=True,
            text=True,
            check=True,
        )
        print("✓ Python wrapper imports successfully")

        return True
    except subprocess.CalledProcessError as e:
        print("✓ Python wrapper imports successfully")

        return True
    except subprocess.CalledProcessError as e:
        print(f"✗ Import failed: {e}")
        if e.stderr:
            print(f"  stderr: {e.stderr}")
        return False


def create_package_info():
    """Create package information files"""
    print("Creating package info...")

    # Create __init__.py file
    init_content = '''"""
uPhonor Python Bindings

Real-time audio looping system with Python CFFI bindings.
"""

from .uphonor_python import *

__version__ = "0.1.0"
__author__ = "Quaternion Media"
'''

    with open('__init__.py', 'w') as f:
        f.write(init_content)

    print("✓ Package info created")
    return True


def create_readme():
    """Create README for the Python bindings"""
    readme_content = '''# uPhonor Python Bindings

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
'''

    with open('README.md', 'w') as f:
        f.write(readme_content)

    print("✓ README created")
    return True


def main():
    """Main build process"""
    print("uPhonor Python Bindings Build Script (PDM/uv)")
    print("=" * 40)

    # Change to script directory
    script_dir = Path(__file__).parent
    os.chdir(script_dir)

    steps = [
        (
            "Checking package managers",
            lambda: check_pdm_installed() or check_uv_installed(),
        ),
        ("Checking system dependencies", check_dependencies),
        ("Setting up project", setup_project),
        ("Building CFFI extension", build_cffi_extension),
        ("Testing import", test_import),
        ("Creating package info", create_package_info),
        ("Creating README", create_readme),
    ]

    for step_name, step_func in steps:
        print(f"\n{step_name}...")
        if not step_func():
            print(f"\n✗ Build failed at: {step_name}")
            return 1

    print("\n" + "=" * 40)
    print("✓ Build completed successfully!")
    print("\nNext steps:")
    print(
        "1. Run 'pdm run python examples.py' or 'uv run python examples.py' to test the bindings"
    )
    print(
        "2. Run 'pdm run python test_bindings.py' or 'uv run python test_bindings.py' to run tests"
    )
    print(
        "3. Run 'pdm install -e .' or 'uv pip install -e .' to install in development mode"
    )
    print("4. Import with 'from uphonor import UPhonor'")

    return 0


if __name__ == "__main__":
    sys.exit(main())
