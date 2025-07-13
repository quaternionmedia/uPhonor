# Makefile for uPhonor Python CFFI bindings

.PHONY: all build clean test install install-dev deps check help

# Default target
all: build

# Build the CFFI extension
build:
	@echo "Building uPhonor Python CFFI bindings..."
	python3 build.py

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf build/
	rm -rf __pycache__/
	rm -rf *.egg-info/
	rm -f _uphonor_cffi*.so
	rm -f _uphonor_cffi*.c
	rm -f __init__.py
	rm -f setup.cfg

# Install system dependencies (Ubuntu/Debian)
deps-ubuntu:
	@echo "Installing system dependencies (Ubuntu/Debian)..."
	sudo apt update
	sudo apt install -y libpipewire-0.3-dev libsndfile1-dev libasound2-dev \
		librubberband-dev libcjson-dev pkg-config python3-dev python3-pip

# Install system dependencies (Fedora)
deps-fedora:
	@echo "Installing system dependencies (Fedora)..."
	sudo dnf install -y pipewire-devel libsndfile-devel alsa-lib-devel \
		rubberband-devel libcjson-devel pkgconfig python3-devel python3-pip

# Install Python dependencies
deps-python:
	@echo "Installing Python dependencies..."
	pip3 install cffi setuptools wheel

# Install all dependencies (Ubuntu/Debian)
deps: deps-ubuntu deps-python

# Run tests
test: build
	@echo "Running tests..."
	python3 test_bindings.py

# Run examples
examples: build
	@echo "Running examples..."
	python3 examples.py

# Install in development mode
install-dev: build
	@echo "Installing in development mode..."
	pip3 install -e .

# Install normally
install: build
	@echo "Installing uPhonor Python bindings..."
	pip3 install .

# Check if dependencies are available
check:
	@echo "Checking system dependencies..."
	@echo -n "pkg-config: "; which pkg-config >/dev/null 2>&1 && echo "✓" || echo "✗"
	@echo -n "pipewire: "; pkg-config --exists libpipewire-0.3 && echo "✓" || echo "✗"
	@echo -n "sndfile: "; pkg-config --exists sndfile && echo "✓" || echo "✗"
	@echo -n "alsa: "; pkg-config --exists alsa && echo "✓" || echo "✗"
	@echo -n "rubberband: "; pkg-config --exists rubberband && echo "✓" || echo "✗"
	@echo -n "cjson: "; pkg-config --exists libcjson && echo "✓" || echo "✗"
	@echo "Python dependencies:"
	@echo -n "cffi: "; python3 -c "import cffi" 2>/dev/null && echo "✓" || echo "✗"
	@echo -n "setuptools: "; python3 -c "import setuptools" 2>/dev/null && echo "✓" || echo "✗"

# Help
help:
	@echo "uPhonor Python CFFI Bindings - Available targets:"
	@echo ""
	@echo "  build        - Build the CFFI extension"
	@echo "  clean        - Clean build artifacts"
	@echo "  test         - Run the test suite"
	@echo "  examples     - Run usage examples"
	@echo "  install      - Install the bindings"
	@echo "  install-dev  - Install in development mode"
	@echo "  check        - Check if dependencies are available"
	@echo ""
	@echo "  deps         - Install all dependencies (Ubuntu/Debian)"
	@echo "  deps-ubuntu  - Install system deps (Ubuntu/Debian)"
	@echo "  deps-fedora  - Install system deps (Fedora)"
	@echo "  deps-python  - Install Python dependencies"
	@echo ""
	@echo "Quick start:"
	@echo "  make deps     # Install dependencies"
	@echo "  make build    # Build the extension"
	@echo "  make test     # Run tests"
	@echo "  make examples # See it in action"
