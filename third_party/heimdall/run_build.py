#!/usr/bin/env python3
"""
Build script for Heimdall C++ module
Runs CMake configuration and build process
"""
import subprocess
import sys
import os
from pathlib import Path

def run_command(cmd, cwd=None):
    """Run a command and print output in real-time"""
    print(f"\n{'='*60}")
    print(f"Running: {' '.join(cmd) if isinstance(cmd, list) else cmd}")
    print(f"{'='*60}\n")

    result = subprocess.run(
        cmd,
        cwd=cwd,
        capture_output=True,
        text=True,
        shell=isinstance(cmd, str)
    )

    if result.stdout:
        print(result.stdout)
    if result.stderr:
        print(result.stderr, file=sys.stderr)

    return result.returncode

def main():
    # Get project root
    project_root = Path(__file__).parent
    build_dir = project_root / "build"

    print("\n" + "="*60)
    print("Building Heimdall - The Vigilant Guardian")
    print("="*60)

    # Clean and create build directory
    print("\n[1/4] Preparing build directory...")
    if build_dir.exists():
        import shutil
        shutil.rmtree(build_dir)
    build_dir.mkdir()
    print(f"[OK] Build directory created: {build_dir}")

    # Configure with CMake
    print("\n[2/4] Configuring with CMake...")
    python_exe = sys.executable.replace('\\', '/')
    ret = run_command(
        ['cmake', '-G', 'Visual Studio 17 2022', '-A', 'x64',
         f'-DPython_EXECUTABLE={python_exe}',
         '..'],
        cwd=build_dir
    )
    if ret != 0:
        print("ERROR: CMake configuration failed!")
        return 1
    print("[OK] CMake configuration successful")

    # Build Release
    print("\n[3/4] Building Release...")
    ret = run_command(
        ['cmake', '--build', '.', '--config', 'Release'],
        cwd=build_dir
    )
    if ret != 0:
        print("ERROR: Build failed!")
        return 1
    print("[OK] Build successful")

    # Install to project root
    print("\n[4/4] Installing to project root...")
    ret = run_command(
        ['cmake', '--install', '.', '--config', 'Release'],
        cwd=build_dir
    )
    if ret != 0:
        print("ERROR: Installation failed!")
        return 1
    print("[OK] Installation successful")

    print("\n" + "="*60)
    print("BUILD SUCCESSFUL!")
    print("="*60)
    print(f"\nOutput: heimdall.cp312-win_amd64.pyd")
    print(f"\nTest with:")
    print(f'  python -c "import heimdall; print(heimdall.__version__)"')

    return 0

if __name__ == '__main__':
    sys.exit(main())
