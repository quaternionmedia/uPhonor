#!/usr/bin/env python3
"""
Meson helper script for checking Python dependencies and package managers
"""

import sys
import subprocess
import importlib.util


def check_python_package(package_name):
    """Check if a Python package is available"""
    try:
        spec = importlib.util.find_spec(package_name)
        return spec is not None
    except ImportError:
        return False


def check_command(command):
    """Check if a command is available"""
    try:
        result = subprocess.run(
            [command, '--version'], capture_output=True, text=True, check=True
        )
        return True, result.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False, None


def main():
    """Main check function"""
    if len(sys.argv) < 2:
        print("Usage: check_deps.py <check_type>")
        sys.exit(1)

    check_type = sys.argv[1]

    if check_type == "cffi":
        # Check if CFFI is available
        if check_python_package("cffi"):
            print("CFFI available")
            sys.exit(0)
        else:
            print("CFFI not available")
            sys.exit(1)

    elif check_type == "pdm":
        # Check if PDM is available
        available, version = check_command("pdm")
        if available:
            print(f"PDM available: {version}")
            sys.exit(0)
        else:
            print("PDM not available")
            sys.exit(1)

    elif check_type == "uv":
        # Check if uv is available
        available, version = check_command("uv")
        if available:
            print(f"uv available: {version}")
            sys.exit(0)
        else:
            print("uv not available")
            sys.exit(1)

    elif check_type == "package_manager":
        # Check which package manager is available (PDM preferred)
        pdm_available, pdm_version = check_command("pdm")
        uv_available, uv_version = check_command("uv")

        if pdm_available:
            print("pdm")
        elif uv_available:
            print("uv")
        else:
            print("pip")
        sys.exit(0)

    elif check_type == "all":
        # Check all dependencies and print status
        print("Python dependency check:")

        # Check CFFI
        cffi_ok = check_python_package("cffi")
        print(f"  CFFI: {'✓' if cffi_ok else '✗'}")

        # Check package managers
        pdm_ok, pdm_version = check_command("pdm")
        uv_ok, uv_version = check_command("uv")
        print(
            f"  PDM: {'✓' if pdm_ok else '✗'}" + (f" ({pdm_version})" if pdm_ok else "")
        )
        print(f"  uv: {'✓' if uv_ok else '✗'}" + (f" ({uv_version})" if uv_ok else ""))

        if not cffi_ok:
            print("\nTo install CFFI:")
            if pdm_ok:
                print("  pdm add cffi")
            elif uv_ok:
                print("  uv add cffi")
            else:
                print("  pip install cffi")

        if not (pdm_ok or uv_ok):
            print("\nTo install package managers:")
            print("  PDM: curl -sSL https://pdm.fming.dev/install-pdm.py | python3 -")
            print("  uv: curl -LsSf https://astral.sh/uv/install.sh | sh")

        sys.exit(0 if cffi_ok else 1)

    else:
        print(f"Unknown check type: {check_type}")
        sys.exit(1)


if __name__ == "__main__":
    main()
