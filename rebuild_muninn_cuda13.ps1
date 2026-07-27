# Rebuild Muninn DLL against CTranslate2 CUDA 13 build
#
# READ THIS FIRST IF YOU CHANGED MUNINN SOURCE FOR LOKI STUDIO:
# Loki Studio does NOT deploy the muninn.dll this script produces. Its
# CMakeLists post-build step copies from build_static/Release/Release/, so
# building only build_cuda13/ leaves Loki running the previous DLL — and the
# next Loki build will silently overwrite anything you hand-copied into
# build/Release. To get source changes into Loki:
#
#   cmake --build D:\Vibe_Projects\Muninn-Faster-Whisper\build_static --config Release --parallel
#   cmake --build D:\Vibe_Projects\Loki-Studio\loki_studio-cpp\build --config Release
#
# build_static/ is already configured with the same static-FFmpeg + CUDA 13
# CTranslate2 settings this script uses. This script remains useful for the
# CUDA 13 test apps and for pairing checks against the CT2 build.
$ErrorActionPreference = "Stop"

$MUNINN_DIR = "D:\Vibe_Projects\Muninn-Faster-Whisper"
$BUILD_DIR = "$MUNINN_DIR\build_cuda13"
$CT2_BUILD = "$MUNINN_DIR\third_party\CTranslate2\build_cuda13"
$CUDA_PATH = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2"
$CUDA_PATH_CMAKE = $CUDA_PATH -replace '\\', '/'

Write-Host "=== Rebuilding Muninn with CUDA 13.2 ===" -ForegroundColor Cyan

# Verify CTranslate2 CUDA 13 build exists
if (-not (Test-Path "$CT2_BUILD\Release\ctranslate2.dll")) {
    Write-Host "ERROR: CTranslate2 CUDA 13 build not found. Run rebuild_ct2_cuda13.ps1 first." -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $BUILD_DIR)) {
    New-Item -ItemType Directory -Path $BUILD_DIR | Out-Null
}

Set-Location $BUILD_DIR

$env:CUDA_PATH = $CUDA_PATH
$env:CUDAToolkit_ROOT = $CUDA_PATH
$env:PATH = "$CUDA_PATH\bin\x64;$CUDA_PATH\bin;$env:PATH"

Write-Host "Configuring..." -ForegroundColor Yellow

# Point CTranslate2 search to the CUDA 13 build output
# Override CTRANSLATE2_LIB directly to avoid finding the old CUDA 12 lib
$CT2_BUILD_CMAKE = $CT2_BUILD -replace '\\','/'

# Use FFmpeg from Loki Studio's cpp_modules (no vcpkg dependency).
#
# This MUST be the static install (lib/libav*.lib) with FFMPEG_STATIC=ON — the
# Muninn Pattern links FFmpeg inside muninn.dll so it stays isolated from Qt's
# dynamically-loaded CRT. Pointing at cpp_modules/ffmpeg without the flag takes
# the dynamic path instead: the build still succeeds, but produces a ~1.4 MB
# muninn.dll that imports avcodec-62.dll / avutil-60.dll at runtime rather than
# the ~19 MB self-contained one that ships. Verify with:
#   dumpbin /DEPENDENTS muninn.dll   -> must list NO av*.dll
$FFMPEG_DIR = "D:/Vibe_Projects/Loki-Studio/cpp_modules/ffmpeg-8.0-dev/install_msvc"

cmake $MUNINN_DIR -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_BUILD_TYPE=Release `
    -DWITH_CUDA=ON `
    -DCUDAToolkit_ROOT="$CUDA_PATH_CMAKE" `
    -DCTRANSLATE2_LIB="$CT2_BUILD_CMAKE/Release/ctranslate2.lib" `
    -DCTranslate2_DIR="$($MUNINN_DIR -replace '\\','/')/third_party/CTranslate2" `
    -DFFMPEG_DIR="$FFMPEG_DIR" `
    -DFFMPEG_STATIC=ON

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: CMake configuration failed" -ForegroundColor Red
    exit 1
}

Write-Host "Building..." -ForegroundColor Yellow

cmake --build . --config Release --parallel

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Build failed" -ForegroundColor Red
    exit 1
}

Write-Host "`n=== Muninn CUDA 13 Build Complete ===" -ForegroundColor Green
Write-Host "Output: $BUILD_DIR\Release\muninn.dll"
Write-Host ""
Write-Host "To deploy to Loki Studio:"
Write-Host "  Copy $BUILD_DIR\Release\muninn.dll -> muninn_cu13.dll"
Write-Host "  Copy $CT2_BUILD\Release\ctranslate2.dll -> ctranslate2_cu13.dll"
Write-Host "  Copy CUDA 13 runtime DLLs (cublas64_13.dll, etc.)"
