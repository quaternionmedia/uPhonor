# Makefile for uPhonor Python CFFI bindings with uv

.PHONY: all build clean test install install-dev deps check help uv-install

# Default target
all: build

# Install uv if not present
uv-install:
	@echo "Checking for uv..."
	@which uv >/dev/null 2>&1 || (echo "Installing uv..." && curl -LsSf https://astral.sh/uv/install.sh | sh)

# Build the CFFI extension
build: uv-install
	@echo "Building uPhonor Python CFFI bindings with uv..."
	uv run python build.py

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf build/
	rm -rf __pycache__/
	rm -rf *.egg-info/
	rm -rf .uv-cache/
	rm -f _uphonor_cffi*.so
	rm -f _uphonor_cffi*.c
	rm -f __init__.py
	rm -f .python-version

# Install system dependencies (Ubuntu/Debian)
deps-ubuntu:
	@echo "Installing system dependencies (Ubuntu/Debian)..."
	sudo apt update
	sudo apt install -y libpipewire-0.3-dev libsndfile1-dev libasound2-dev \
		librubberband-dev libcjson-dev pkg-config python3-dev

# Install system dependencies (Fedora)
deps-fedora:
	@echo "Installing system dependencies (Fedora)..."
	sudo dnf install -y pipewire-devel libsndfile-devel alsa-lib-devel \
		rubberband-devel libcjson-devel pkgconfig python3-devel

# Setup uv project and sync dependencies
deps-python: uv-install
	@echo "Setting up uv project and syncing dependencies..."
	uv sync

# Install all dependencies (Ubuntu/Debian)
deps: deps-ubuntu deps-python

# Run tests
test: build
	@echo "Running tests with uv..."
	uv run python test_bindings.py

# Run examples
examples: build
	@echo "Running examples with uv..."
	uv run python examples.py

# Run integration test
integration: build
	@echo "Running integration test with uv..."
	uv run python holophono_integration.py test

# Install in development mode
install-dev: build
	@echo "Installing in development mode with uv..."
	uv pip install -e .

# Install normally
install: build
	@echo "Installing uPhonor Python bindings with uv..."
	uv pip install .

# Check if dependencies are available
check: uv-install
	@echo "Checking system dependencies..."
	@echo -n "uv: "; which uv >/dev/null 2>&1 && echo "✓" || echo "✗"
	@echo -n "pkg-config: "; which pkg-config >/dev/null 2>&1 && echo "✓" || echo "✗"
	@echo -n "pipewire: "; pkg-config --exists libpipewire-0.3 && echo "✓" || echo "✗"
	@echo -n "sndfile: "; pkg-config --exists sndfile && echo "✓" || echo "✗"
	@echo -n "alsa: "; pkg-config --exists alsa && echo "✓" || echo "✗"
	@echo -n "rubberband: "; pkg-config --exists rubberband && echo "✓" || echo "✗"
	@echo -n "cjson: "; pkg-config --exists libcjson && echo "✓" || echo "✗"
	@echo "Python dependencies:"
	@echo -n "cffi: "; uv run python -c "import cffi" 2>/dev/null && echo "✓" || echo "✗"

# Format code
format: uv-install
	@echo "Formatting code with black and ruff..."
	uv run black .
	uv run ruff --fix .

# Type checking
typecheck: uv-install
	@echo "Running type checking with mypy..."
	uv run mypy uphonor_python.py

# Run all quality checks
qa: format typecheck test
	@echo "All quality checks completed!"

# Development setup
dev-setup: deps build
	@echo "Development environment setup complete!"
	@echo "Try: make test && make examples"

# Help
help:
	@echo "uPhonor Python CFFI Bindings (uv) - Available targets:"
	@echo ""
	@echo "  build        - Build the CFFI extension"
	@echo "  clean        - Clean build artifacts"
	@echo "  test         - Run the test suite"
	@echo "  examples     - Run usage examples"
	@echo "  integration  - Run integration tests"
	@echo "  install      - Install the bindings"
	@echo "  install-dev  - Install in development mode"
	@echo "  check        - Check if dependencies are available"
	@echo ""
	@echo "  deps         - Install all dependencies (Ubuntu/Debian)"
	@echo "  deps-ubuntu  - Install system deps (Ubuntu/Debian)"
	@echo "  deps-fedora  - Install system deps (Fedora)"
	@echo "  deps-python  - Setup uv project and sync deps"
	@echo ""
	@echo "  format       - Format code with black and ruff"
	@echo "  typecheck    - Run type checking with mypy"
	@echo "  qa           - Run all quality checks"
	@echo "  dev-setup    - Complete development setup"
	@echo ""
	@echo "  uv-install   - Install uv package manager"
	@echo ""
	@echo "Quick start:"
	@echo "  make deps     # Install dependencies"
	@echo "  make build    # Build the extension"
	@echo "  make test     # Run tests"
	@echo "  make examples # See it in action"
	@echo ""
	@echo "Development:"
	@echo "  make dev-setup # Complete dev environment setup"
	@echo "  make qa        # Run all quality checks"
