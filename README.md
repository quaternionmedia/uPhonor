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

## Development
### Prerequisites

Before building uPhonor, ensure you have the following installed:

- [Meson](https://mesonbuild.com/)
- [PipeWire](https://pipewire.org/)
- [libsndfile](https://libsndfile.github.io/libsndfile/)


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
