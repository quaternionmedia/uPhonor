# uPhonor
A Micro-Holophonor implementation.

#### Pronunciation
"micro-phone-or"

## Installation
Download the latest release from the [Releases page](https://github.com/quaternionmedia/uPhonor/releases).

Move the downloaded file to a directory in your PATH (e.g., `/usr/local/bin`)

Alternatively, specify the path to run the file locally (e.g., `./uphonor`)

## Usage
```sh
uphonor
```

If no argument is specified, uPhonor will start and be ready for audio and MIDI input.

#### Start with input file

```sh
uphonor [<input_file>] [volume]
```

Where `<input_file>` is the path to the audio file you want to play, and `[volume]` is an optional volume level (default is 1.0).

## Python Bindings

uPhonor now includes Python CFFI bindings for integration with Python applications. This allows you to control uPhonor from Python scripts and integrate it with other Python audio tools.

### Quick Start with Python Bindings

```python
from uphonor_python import uphonor_session

# Use context manager for automatic cleanup
with uphonor_session() as uphonor:
    # Start the audio system
    uphonor.start()
    
    # Set audio parameters
    uphonor.set_volume(0.8)
    uphonor.set_playback_speed(1.2)
    uphonor.set_pitch_shift(3.0)  # 3 semitones up
    
    # Handle MIDI input
    uphonor.handle_note_on(0, 60, 100)  # Start loop on middle C
    uphonor.handle_note_off(0, 60, 0)   # Stop loop
    
    # Save session
    uphonor.save_state("my_session.json")
```

### Building Python Bindings

```sh
cd uPhonor
# Install uv if you haven't already
curl -LsSf https://astral.sh/uv/install.sh | sh

# Install system dependencies (Ubuntu/Debian)
make deps-ubuntu

# Build and test
make build       # Build the CFFI extension  
make test        # Run tests
make examples    # See usage examples
```

For other package managers:
```sh
# Traditional pip workflow
pip install cffi
python build.py
```

See [PYTHON_BINDINGS.md](PYTHON_BINDINGS.md) for detailed documentation.

## Development
### Prerequisites

Before building uPhonor, ensure you have the following installed:

- [Meson](https://mesonbuild.com/)
- [PipeWire](https://pipewire.org/)
- [libsndfile](https://libsndfile.github.io/libsndfile/)

Ubuntu / Debian / Mint users should be able to install the required packages using the following:
```sh
sudo apt-get install -y build-essential meson libsndfile-dev libasound2-dev libdbus-1-dev libgtk2.0-dev librubberband-dev
```

### Setup
```sh
git clone https://github.com/quaternionmedia/uPhonor.git
cd uPhonor
mkdir build && cd build
meson setup
```

### Compile
You should now be able to build uPhonor using the following command:
```sh
meson compile
```

### Run
If all went well, you should have a `uphonor` binary in the `build` directory. You can run it using:
```sh
./uphonor
```
