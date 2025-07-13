#!/usr/bin/env python3
"""
Build script for uPhonor Python CFFI bindings
Run this to compile the C extension
"""

import subprocess
import sys
import os
from pathlib import Path

def run_command(cmd, cwd=None):
    """Run a command and return success status"""
    try:
        result = subprocess.run(cmd, shell=True, check=True, cwd=cwd, 
                              capture_output=True, text=True)
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

def check_dependencies():
    """Check if required system dependencies are available"""
    print("Checking system dependencies...")
    
    # Check for pkg-config
    if not run_command("pkg-config --version"):
        print("pkg-config is required but not found")
        return False
    
    # Check for required libraries
    required_libs = [
        'libpipewire-0.3',
        'sndfile', 
        'alsa',
        'rubberband',
        'libcjson'
    ]
    
    for lib in required_libs:
        if not run_command(f"pkg-config --exists {lib}"):
            print(f"Required library {lib} not found")
            return False
    
    print("✓ All system dependencies found")
    return True

def install_python_dependencies():
    """Install required Python packages"""
    print("Installing Python dependencies...")
    
    python_deps = [
        'cffi>=1.14.0',
        'setuptools',
        'wheel'
    ]
    
    for dep in python_deps:
        if not run_command(f"{sys.executable} -m pip install {dep}"):
            print(f"Failed to install {dep}")
            return False
    
    print("✓ Python dependencies installed")
    return True

def build_cffi_extension():
    """Build the CFFI extension"""
    print("Building CFFI extension...")
    
    # Run the setup.py build
    if not run_command(f"{sys.executable} setup.py build_ext --inplace"):
        print("Failed to build CFFI extension")
        return False
    
    print("✓ CFFI extension built successfully")
    return True

def test_import():
    """Test if the built extension can be imported"""
    print("Testing import...")
    
    try:
        # Try to import the built extension
        import _uphonor_cffi
        print("✓ CFFI extension imports successfully")
        
        # Try to import the Python wrapper
        import uphonor_python
        print("✓ Python wrapper imports successfully")
        
        return True
    except ImportError as e:
        print(f"✗ Import failed: {e}")
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
    
    # Create setup.cfg for installation
    setup_cfg_content = '''[metadata]
name = uphonor-python
version = 0.1.0
description = Python CFFI bindings for uPhonor real-time audio looping system
long_description = file: README.md
long_description_content_type = text/markdown
author = Quaternion Media
author_email = holophonor@quaternion.media
license = MIT
classifiers =
    Development Status :: 3 - Alpha
    Intended Audience :: Developers
    License :: OSI Approved :: MIT License
    Programming Language :: Python :: 3
    Programming Language :: Python :: 3.8
    Programming Language :: Python :: 3.9
    Programming Language :: Python :: 3.10
    Programming Language :: Python :: 3.11
    Topic :: Multimedia :: Sound/Audio

[options]
python_requires = >=3.8
install_requires =
    cffi>=1.14.0

[options.packages.find]
where = .
include = uphonor*
'''
    
    with open('setup.cfg', 'w') as f:
        f.write(setup_cfg_content)
    
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

### Build and Install

```bash
# Build the CFFI extension
python build.py

# Install in development mode
pip install -e .

# Or install normally
pip install .
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

## License

MIT License - see LICENSE file for details.
'''
    
    with open('README.md', 'w') as f:
        f.write(readme_content)
    
    print("✓ README created")
    return True

def main():
    """Main build process"""
    print("uPhonor Python Bindings Build Script")
    print("=" * 40)
    
    # Change to script directory
    script_dir = Path(__file__).parent
    os.chdir(script_dir)
    
    steps = [
        ("Checking dependencies", check_dependencies),
        ("Installing Python dependencies", install_python_dependencies),
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
    print("1. Run 'python examples.py' to test the bindings")
    print("2. Run 'pip install -e .' to install in development mode")
    print("3. Import with 'from uphonor_python import UPhonor'")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
