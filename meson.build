project('uPhonor', 'c', version : '0.1',
  default_options : ['warning_level=3'])

# dependencies
pipewire = dependency('libpipewire-0.3')
sndfile = dependency('sndfile')
alsa = dependency('alsa')


# exe
executable('play', 'uphonor.h', 'main.c', 'process.c', dependencies : [pipewire, sndfile, alsa], install : true)
executable('midi', 'midi.c', dependencies : [pipewire, alsa], install : true)
