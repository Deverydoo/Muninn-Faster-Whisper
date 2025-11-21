@echo off
REM Quick build script for Heimdall module

echo ========================================
echo Building Heimdall - The Vigilant Guardian
echo ========================================
echo.

REM Create build directory
if not exist build mkdir build
cd build

REM Configure
echo [1/3] Configuring CMake...
cmake -G "Visual Studio 17 2022" -A x64 ..
if errorlevel 1 (
    echo ERROR: CMake configuration failed!
    pause
    exit /b 1
)

REM Build
echo.
echo [2/3] Building Release...
cmake --build . --config Release
if errorlevel 1 (
    echo ERROR: Build failed!
    pause
    exit /b 1
)

REM Install
echo.
echo [3/3] Installing to project root...
cmake --install . --config Release
if errorlevel 1 (
    echo ERROR: Installation failed!
    pause
    exit /b 1
)

cd ..

echo.
echo ========================================
echo BUILD SUCCESSFUL!
echo ========================================
echo Output: heimdall.cp312-win_amd64.pyd
echo.
echo Test with:
echo   python -c "import heimdall; print(heimdall.__version__)"
echo.
pause
