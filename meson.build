project('uPhonor', 'c', version : '0.9.0',
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

# Python dependencies
python = import('python').find_installation('python3')
python_dep = python.dependency()

# Check for Python packages using helper script
check_deps_script = files('scripts/check_deps.py')

# Check CFFI availability
cffi_check = run_command(python, check_deps_script, 'cffi', check: false)
cffi_available = cffi_check.returncode() == 0

# Determine which package manager to use
package_manager_check = run_command(python, check_deps_script, 'package_manager', check: false)
preferred_package_manager = package_manager_check.stdout().strip()

# Override with user option if specified
package_manager_option = get_option('python_package_manager')
if package_manager_option != 'auto'
  preferred_package_manager = package_manager_option
endif

# Set up package manager commands
if preferred_package_manager == 'pdm'
  pdm_prog = find_program('pdm', required: true)
  python_deps_cmd = [pdm_prog, 'install']
  python_run_prefix = [pdm_prog.full_path(), 'run', 'python']
  message('Using PDM for Python package management')
elif preferred_package_manager == 'uv' 
  uv_prog = find_program('uv', required: true)
  python_deps_cmd = [uv_prog, 'sync']
  python_run_prefix = [uv_prog.full_path(), 'run', 'python']
  message('Using uv for Python package management')
else
  python_deps_cmd = [python, '-m', 'pip', 'install', '-e', '.']
  python_run_prefix = [python.full_path()]
  warning('Using pip directly - consider installing PDM or uv for better dependency management')
endif

# Python bindings option check
build_python_bindings = get_option('python_bindings')
if build_python_bindings and not cffi_available
  error('Python bindings requested but CFFI not available. Install with: ' + preferred_package_manager + ' add cffi')
endif

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

# main executable
executable('uphonor', uphonor_sources, dependencies : [pipewire, sndfile, alsa, math, threads, rubberband, cjson], install : true)

# Python CFFI bindings
if build_python_bindings and cffi_available
  message('Building Python CFFI bindings with ' + preferred_package_manager)
  
  # Python dependencies are already set up above
  # The commands use the python_run_prefix defined above
endif

# examples
# executable('midi', 'examples/midi.c', dependencies : [pipewire, alsa], install : true)
# executable('stream', 'examples/stream.c', dependencies: [pipewire, sndfile])
# executable('play', 'examples/play-file.c', dependencies: [pipewire, sndfile])
# executable('filter', 'examples/filter.c', dependencies: [pipewire, math])

# Custom targets for Python development workflow
if cffi_available
  # Python dependencies target
  python_deps_target = custom_target('python-deps',
    output: 'python-deps-installed',
    command: python_deps_cmd,
    console: true,
    build_always_stale: true,
    build_by_default: false
  )

  # Build CFFI extension
  cffi_build_target = custom_target('cffi-build',
    output: 'cffi-built',
    input: [files('build_cffi.py'), files('build.py')],
    command: ['sh', '-c', 'cd @0@ && @1@'.format(meson.project_source_root(), ' '.join(python_run_prefix + ['build.py']))],
    depends: python_deps_target,
    console: true,
    build_always_stale: true,
    build_by_default: false
  )

  # Test target
  test_target = custom_target('python-test',
    output: 'python-tests-run',
    input: files('test_bindings.py'),
    command: ['sh', '-c', 'cd @0@ && @1@'.format(meson.project_source_root(), ' '.join(python_run_prefix + ['test_bindings.py']))],
    depends: cffi_build_target,
    console: true,
    build_always_stale: true,
    build_by_default: false
  )

  # Examples target
  examples_target = custom_target('python-examples',
    output: 'python-examples-run',
    input: files('examples.py'),
    command: ['sh', '-c', 'cd @0@ && @1@'.format(meson.project_source_root(), ' '.join(python_run_prefix + ['examples.py']))],
    depends: cffi_build_target,
    console: true,
    build_always_stale: true,
    build_by_default: false
  )

  # Integration test target
  integration_target = custom_target('python-integration',
    output: 'python-integration-run',
    input: files('holophono_integration.py'),
    command: ['sh', '-c', 'cd @0@ && @1@'.format(meson.project_source_root(), ' '.join(python_run_prefix + ['holophono_integration.py', 'test']))],
    depends: cffi_build_target,
    console: true,
    build_always_stale: true,
    build_by_default: false
  )

  # Development install target
  install_dev_target = custom_target('python-install-dev',
    output: 'python-dev-installed',
    command: ['sh', '-c', 'cd @0@ && @1@'.format(meson.project_source_root(), ' '.join(python_run_prefix + ['-m', 'pip', 'install', '-e', '.']))],
    depends: cffi_build_target,
    console: true,
    build_always_stale: true,
    build_by_default: false
  )

  # Quality assurance targets
  black_target = custom_target('python-black',
    output: 'python-black-done',
    command: ['sh', '-c', 'cd @0@ && @1@'.format(meson.project_source_root(), ' '.join(python_run_prefix + ['-m', 'black', '.']))],
    depends: python_deps_target,
    console: true,
    build_always_stale: true,
    build_by_default: false
  )

  ruff_target = custom_target('python-ruff',
    output: 'python-ruff-done', 
    command: ['sh', '-c', 'cd @0@ && @1@'.format(meson.project_source_root(), ' '.join(python_run_prefix + ['-m', 'ruff', '--fix', '.']))],
    depends: python_deps_target,
    console: true,
    build_always_stale: true,
    build_by_default: false
  )

  typecheck_target = custom_target('python-typecheck',
    output: 'python-typechecked',
    input: files('uphonor/uphonor_python.py'),
    command: ['sh', '-c', 'cd @0@ && @1@'.format(meson.project_source_root(), ' '.join(python_run_prefix + ['-m', 'mypy', 'uphonor/uphonor_python.py']))],
    depends: python_deps_target,
    console: true,
    build_always_stale: true,
    build_by_default: false
  )

  # Composite targets
  alias_target('python-build', cffi_build_target)
  alias_target('python-format', [black_target, ruff_target])
  alias_target('python-qa', [black_target, ruff_target, typecheck_target, test_target])
  alias_target('python-dev-setup', [python_deps_target, cffi_build_target])
  alias_target('python-all', [python_deps_target, cffi_build_target, test_target, examples_target])
endif

# Clean target (handled by meson automatically, but we can add custom clean)
run_target('clean-python',
  command: [
    'rm', '-rf', 
    'build/',
    '__pycache__/',
    '*.egg-info/',
    '.uv-cache/',
    '_uphonor_cffi*.so',
    '_uphonor_cffi*.c',
    '.python-version'
  ]
)
