project('uPhonor', 'c', version : '0.1',
  default_options : ['warning_level=3'])

# libraries
cc = meson.get_compiler('c')
math = cc.find_library('m')

# dependencies
pipewire = dependency('libpipewire-0.3')
sndfile = dependency('sndfile')
alsa = dependency('alsa')
threads = dependency('threads')
rubberband = dependency('rubberband')
cjson = dependency('libcjson')

# sources
uphonor_sources = [
  'uphonor.h', 
  'main.c',
  'audio_processing.c',
  'audio_processing_rt.c',
  'audio_buffer_rt.c',
  'rt_nonrt_bridge.c',
  'midi_processing.c', 
  'buffer_manager.c',
  'record.c',
  'rubberband_processing.c',
  'utils.c',
  'multi_loop_functions.c',
  'holo.c',
  'config.c',
  'config_utils.c',
  'config_file_loader.c',
]

# exes
executable('uphonor', uphonor_sources, dependencies : [pipewire, sndfile, alsa, math, threads, rubberband, cjson], install : true)

# examples
# executable('midi', 'examples/midi.c', dependencies : [pipewire, alsa], install : true)
# executable('stream', 'examples/stream.c', dependencies: [pipewire, sndfile])
# executable('play', 'examples/play-file.c', dependencies: [pipewire, sndfile])
# executable('filter', 'examples/filter.c', dependencies: [pipewire, math])
