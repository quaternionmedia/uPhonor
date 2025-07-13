# Meson Build System for uPhonor Python Bindings

The uPhonor project now uses Meson as the primary build system for both the C program and Python CFFI bindings. This replaces the previous Makefile-based approach.

## Features

- **Unified Build System**: Both C executable and Python bindings through Meson
- **Package Manager Detection**: Automatically detects and uses PDM, uv, or pip
- **Configurable Options**: Control Python binding builds through Meson options
- **Dependency Checking**: Comprehensive dependency validation
- **Quality Assurance**: Built-in formatting, type checking, and testing targets

## Configuration Options

Configure the build using Meson options in `meson_options.txt`:

```bash
# Configure build options
meson setup builddir -Dpython_bindings=true -Dpython_package_manager=pdm

# Available options:
# -Dpython_bindings=true/false          # Build Python bindings (default: true)
# -Dpython_package_manager=auto/pdm/uv/pip  # Package manager choice (default: auto)
# -Dinstall_python_deps=true/false     # Auto-install Python deps (default: false)
# -Drun_python_tests=true/false        # Run tests after build (default: false)
```

## Build Commands

### Initial Setup
```bash
# Configure build directory
meson setup builddir

# Or with specific options
meson setup builddir -Dpython_bindings=true -Dpython_package_manager=pdm
```

### Building

```bash
# Build C executable only
meson compile -C builddir

# Build C executable 
meson compile -C builddir uphonor

# Build Python CFFI extension
meson compile -C builddir python-build

# Build everything (if auto-enabled)
meson compile -C builddir python-all
```

### Development Workflow

```bash
# Setup Python environment and install dependencies
meson compile -C builddir python-deps

# Build CFFI extension
meson compile -C builddir python-build

# Run tests
meson compile -C builddir python-test

# Run examples
meson compile -C builddir python-examples

# Run integration tests
meson compile -C builddir python-integration

# Install in development mode
meson compile -C builddir python-install-dev

# Quality assurance (format + typecheck + test)
meson compile -C builddir python-qa

# Complete development setup
meson compile -C builddir python-dev-setup
```

### Available Targets

| Target | Description |
|--------|-------------|
| `uphonor` | Build C executable |
| `python-deps` | Install Python dependencies |
| `python-build` | Build CFFI extension |
| `python-test` | Run Python tests |
| `python-examples` | Run usage examples |
| `python-integration` | Run integration tests |
| `python-install-dev` | Install in development mode |
| `python-format` | Format code with black/ruff |
| `python-typecheck` | Type check with mypy |
| `python-qa` | Run all quality checks |
| `python-dev-setup` | Complete dev environment setup |
| `python-all` | Build and test everything |
| `clean-python` | Clean Python build artifacts |

## Package Manager Support

The system automatically detects and uses the best available Python package manager:

1. **PDM** (preferred) - Modern Python package manager
2. **uv** (fallback) - Fast Python package installer
3. **pip** (last resort) - Standard Python package installer

### Manual Package Manager Selection

```bash
# Force PDM usage
meson setup builddir -Dpython_package_manager=pdm

# Force uv usage  
meson setup builddir -Dpython_package_manager=uv

# Force pip usage
meson setup builddir -Dpython_package_manager=pip
```

## Dependency Checking

Check your development environment:

```bash
# Check all dependencies
python scripts/check_deps.py all

# Check specific components
python scripts/check_deps.py cffi
python scripts/check_deps.py pdm  
python scripts/check_deps.py uv
```

## Migration from Makefile

The previous Makefile commands have been replaced with Meson targets:

| Old Makefile | New Meson Command |
|--------------|-------------------|
| `make build` | `meson compile -C builddir python-build` |
| `make test` | `meson compile -C builddir python-test` |
| `make examples` | `meson compile -C builddir python-examples` |
| `make install-dev` | `meson compile -C builddir python-install-dev` |
| `make clean` | `meson compile -C builddir clean-python` |
| `make deps` | `meson compile -C builddir python-deps` |
| `make qa` | `meson compile -C builddir python-qa` |
| `make dev-setup` | `meson compile -C builddir python-dev-setup` |

## Example Workflow

```bash
# 1. Initial setup
meson setup builddir -Dpython_bindings=true

# 2. Install dependencies and build
meson compile -C builddir python-dev-setup

# 3. Run tests and examples
meson compile -C builddir python-test
meson compile -C builddir python-examples

# 4. Development cycle
meson compile -C builddir python-qa    # Check code quality
meson compile -C builddir python-build # Rebuild after changes

# 5. Install for use
meson compile -C builddir python-install-dev
```

## Installation

```bash
# Install C executable
meson install -C builddir

# Python bindings are installed via the python-install-dev target
meson compile -C builddir python-install-dev
```

This Meson-based approach provides a more robust and standardized build system while maintaining all the functionality of the previous Makefile-based workflow.
