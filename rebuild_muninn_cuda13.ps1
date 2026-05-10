# Rebuild Muninn DLL against CTranslate2 CUDA 13 build
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

# Use FFmpeg from Loki Studio's cpp_modules (no vcpkg dependency)
$FFMPEG_DIR = "D:/Vibe_Projects/Loki-Studio/cpp_modules/ffmpeg"

cmake $MUNINN_DIR -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_BUILD_TYPE=Release `
    -DWITH_CUDA=ON `
    -DCUDAToolkit_ROOT="$CUDA_PATH_CMAKE" `
    -DCTRANSLATE2_LIB="$CT2_BUILD_CMAKE/Release/ctranslate2.lib" `
    -DCTranslate2_DIR="$($MUNINN_DIR -replace '\\','/')/third_party/CTranslate2" `
    -DFFMPEG_DIR="$FFMPEG_DIR"

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
