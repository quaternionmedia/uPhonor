#!/usr/bin/env python3
"""
PDM build script for uPhonor CFFI extension
"""

import os
import sys
import subprocess
from pathlib import Path


def build_cffi_extension():
    """Build the CFFI extension for PDM"""
    print("Building CFFI extension for PDM...")

    # Run the CFFI builder
    try:
        # Import and run the CFFI builder
        from build_cffi import build_cffi_extension

        ffibuilder = build_cffi_extension()
        ffibuilder.compile(verbose=True)
        print("✓ CFFI extension built successfully")
        return True
    except Exception as e:
        print(f"✗ Failed to build CFFI extension: {e}")
        return False


if __name__ == "__main__":
    success = build_cffi_extension()
    sys.exit(0 if success else 1)
