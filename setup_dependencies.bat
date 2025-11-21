@echo off
echo ============================================
echo Muninn Faster-Whisper - Dependency Setup
echo ============================================
echo.

REM Check for admin rights
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: This script requires Administrator privileges
    echo Right-click and select "Run as Administrator"
    pause
    exit /b 1
)

cd /d "%~dp0"

REM 1. Create third_party directory
if not exist third_party mkdir third_party
cd third_party

echo [1/3] Setting up CTranslate2 symlink...
if exist CTranslate2 (
    echo     CTranslate2 already exists, skipping
) else (
    mklink /D CTranslate2 "D:\machine_learning\Loki-Studio\CTranslate2"
    if errorlevel 1 (
        echo     ERROR: Failed to create CTranslate2 symlink
        pause
        exit /b 1
    )
    echo     ✓ CTranslate2 symlink created
)

echo.
echo [2/3] Copying Heimdall audio decoder...
if not exist heimdall mkdir heimdall
if not exist heimdall\include mkdir heimdall\include

copy /Y "D:\machine_learning\Loki-Studio\cpp_heimdall_waveform_engine\build\Release\heimdall.dll" heimdall\ >nul
copy /Y "D:\machine_learning\Loki-Studio\cpp_heimdall_waveform_engine\build\Release\heimdall.lib" heimdall\ >nul
copy /Y "D:\machine_learning\Loki-Studio\cpp_heimdall_waveform_engine\src\audio_decoder.h" heimdall\include\ >nul
copy /Y "D:\machine_learning\Loki-Studio\cpp_heimdall_waveform_engine\src\heimdall.h" heimdall\include\ >nul

if errorlevel 1 (
    echo     ERROR: Failed to copy Heimdall files
    echo     Make sure Heimdall is built: cpp_heimdall_waveform_engine\build\Release\heimdall.dll
    pause
    exit /b 1
)
echo     ✓ Heimdall DLL and headers copied

echo.
echo [3/3] Verifying setup...
if not exist CTranslate2\include (
    echo     ERROR: CTranslate2 headers not found
    pause
    exit /b 1
)
if not exist heimdall\heimdall.dll (
    echo     ERROR: Heimdall DLL not found
    pause
    exit /b 1
)
echo     ✓ All dependencies verified

echo.
echo ============================================
echo Dependencies setup complete!
echo ============================================
echo.
echo Next steps:
echo   1. Configure: cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
echo   2. Build:     cmake --build build --config Release -j
echo   3. Test:      build\Release\muninn_test_app.exe test_audio.mp3
echo.
pause
