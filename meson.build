project('uPhonor', 'c',
  version : '0.1',
  default_options : ['warning_level=3'])

executable('play',
           'play.c',
           dependencies : [
              dependency('libpipewire-0.3'),
              dependency('sndfile')],
           install : true)
