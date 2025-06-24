project('uPhonor', 'c', version : '0.1',
  default_options : ['warning_level=3'])

# libraries
cc = meson.get_compiler('c')
math = cc.find_library('m')

# dependencies
pipewire = dependency('libpipewire-0.3')
sndfile = dependency('sndfile')
alsa = dependency('alsa')

# exe
executable('uphonor', 'uphonor.h', 'main.c', dependencies : [pipewire, sndfile, alsa, math], install : true)
# executable('midi', 'midi.c', dependencies : [pipewire, alsa], install : true)
executable('stream', 'stream.c', dependencies: [pipewire, sndfile])
executable('play', 'play-file.c', dependencies: [pipewire, sndfile])
executable('filter', 'filter.c', dependencies: [pipewire, math])
