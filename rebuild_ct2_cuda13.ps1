# Rebuild CTranslate2 with CUDA 13.2 for Blackwell GPU support (RTX 50-series)
# This creates a separate build in build_cuda13/ to avoid clobbering the CUDA 12 build

$ErrorActionPreference = "Stop"

$CT2_DIR = "D:\Vibe_Projects\Muninn-Faster-Whisper\third_party\CTranslate2"
$BUILD_DIR = "$CT2_DIR\build_cuda13"
$CUDA_PATH = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2"

Write-Host "=== Rebuilding CTranslate2 with CUDA 13.2 ===" -ForegroundColor Cyan
Write-Host "CUDA Toolkit: $CUDA_PATH"
Write-Host "Build Dir: $BUILD_DIR"

# Verify CUDA 13.2 exists
if (-not (Test-Path "$CUDA_PATH\bin\nvcc.exe")) {
    Write-Host "ERROR: CUDA 13.2 not found at $CUDA_PATH" -ForegroundColor Red
    exit 1
}

# Create build directory
if (-not (Test-Path $BUILD_DIR)) {
    New-Item -ItemType Directory -Path $BUILD_DIR | Out-Null
}

Set-Location $BUILD_DIR

# Configure with CUDA 13.2 supported architectures
# sm_75 = Turing (RTX 20xx) - minimum for CUDA 13
# sm_80 = Ampere datacenter (A100)
# sm_86 = Ampere consumer (RTX 30xx)
# sm_89 = Ada Lovelace (RTX 40xx)
# sm_90 = Hopper (H100)
# sm_120 = Blackwell (RTX 50xx) - the reason for this build
# NOTE: sm_60/61/70 dropped in CUDA 13

# Use forward slashes to avoid CMake backslash escape issues
$CUDA_PATH_CMAKE = $CUDA_PATH -replace '\\', '/'
$env:CUDA_PATH = $CUDA_PATH
$env:CUDAToolkit_ROOT = $CUDA_PATH
$env:PATH = "$CUDA_PATH\bin\x64;$CUDA_PATH\bin;$env:PATH"

Write-Host "`nConfiguring CMake..." -ForegroundColor Yellow

# CTranslate2 uses legacy FindCUDA which was removed in CMake 3.31
# CMP0146=OLD restores the module; CMP0104=OLD allows CUDA_ARCH_LIST
cmake .. -G "Visual Studio 17 2022" -A x64 `
    -DWITH_CUDA=ON `
    -DWITH_CUDNN=ON `
    -DWITH_MKL=OFF `
    -DOPENMP_RUNTIME=NONE `
    -DCUDA_ARCH_LIST="7.5;8.0;8.6;8.9;9.0" `
    -DCUDA_NVCC_FLAGS="-gencode;arch=compute_120,code=sm_120" `
    -DCMAKE_BUILD_TYPE=Release `
    -DBUILD_SHARED_LIBS=ON `
    -DCUDAToolkit_ROOT="$CUDA_PATH_CMAKE" `
    -DCUDA_TOOLKIT_ROOT_DIR="$CUDA_PATH_CMAKE" `
    "-DCMAKE_POLICY_DEFAULT_CMP0146=OLD" `
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: CMake configuration failed" -ForegroundColor Red
    exit 1
}

Write-Host "`nBuilding..." -ForegroundColor Yellow

cmake --build . --config Release --parallel

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Build failed" -ForegroundColor Red
    exit 1
}

Write-Host "`n=== CTranslate2 CUDA 13 Build Complete ===" -ForegroundColor Green
Write-Host "Output: $BUILD_DIR\Release\ctranslate2.dll"
Write-Host ""
Write-Host "Next step: Rebuild Muninn against this CTranslate2:"
Write-Host "  .\rebuild_muninn_cuda13.ps1"
