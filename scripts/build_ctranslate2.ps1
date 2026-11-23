<#
.SYNOPSIS
  Builds CTranslate2 for Muninn: the pinned version, Muninn's local patch,
  CUDA 13.2 for RTX 20 series and newer, and the Ruy CPU backend.

.DESCRIPTION
  The same steps as patches/README.md:
    1. clone the tag (with submodules) into third_party/CTranslate2-<version>
    2. apply patches/ctranslate2-v<version>-local.patch
    3. configure and build build_cuda13 (Release, ctranslate2 target)
    4. check the GPU architectures with cuobjdump (sm_75, 80, 86, 89, 90, 120;
       no PTX) and that the CPU backend's VCOMP140.DLL is imported.

  Then rebuild Muninn against it (see patches/README.md for the cache entries
  to clear when switching versions).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts/build_ctranslate2.ps1
#>
param(
    [string] $Version = "4.9.1",
    [string] $CudaRoot = "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $root "third_party/CTranslate2-$Version"
$patch = Join-Path $root "patches/ctranslate2-v$Version-local.patch"

if (-not (Test-Path $patch)) { throw "No patch for $Version at $patch" }

if (-not (Test-Path $src)) {
    git clone --depth 1 --branch "v$Version" --recurse-submodules --shallow-submodules `
        https://github.com/OpenNMT/CTranslate2.git $src
    if ($LASTEXITCODE) { throw "clone failed" }
    git -C $src apply $patch
    if ($LASTEXITCODE) { throw "the patch doesn't apply to $Version" }
} else {
    Write-Host "Using the existing $src (delete it to start over)"
}

$archs = "75-real;80-real;86-real;89-real;90-real;120-real"
cmake -S $src -B "$src/build_cuda13" -G "Visual Studio 17 2022" -A x64 `
    -T "cuda=$CudaRoot" `
    -DBUILD_SHARED_LIBS=ON -DBUILD_CLI=ON -DBUILD_TESTS=OFF -DWITH_CUDA=ON -DWITH_CUDNN=ON `
    -DCUDA_DYNAMIC_LOADING=OFF -DWITH_MKL=OFF -DWITH_DNNL=OFF -DWITH_OPENBLAS=OFF `
    -DWITH_RUY=ON -DWITH_FLASH_ATTN=OFF -DWITH_TENSOR_PARALLEL=OFF `
    -DENABLE_CPU_DISPATCH=ON -DOPENMP_RUNTIME=COMP `
    "-DCMAKE_CUDA_ARCHITECTURES=$archs" -DCMAKE_CUDA_RUNTIME_LIBRARY=Shared `
    "-DCUDAToolkit_ROOT=$CudaRoot"
if ($LASTEXITCODE) { throw "configure failed" }

cmake --build "$src/build_cuda13" --config Release --target ctranslate2 --parallel
if ($LASTEXITCODE) { throw "build failed" }

$dll = Join-Path $src "build_cuda13/Release/ctranslate2.dll"
$elf = & (Join-Path $CudaRoot "bin/cuobjdump.exe") --list-elf $dll 2>$null
$found = ($elf | Select-String -Pattern "sm_\d+" -AllMatches).Matches.Value | Sort-Object -Unique
$want = "sm_120","sm_75","sm_80","sm_86","sm_89","sm_90"
$missing = $want | Where-Object { $_ -notin $found }
if ($missing) { throw "GPU architectures missing from the build: $($missing -join ', ')" }
$ptx = & (Join-Path $CudaRoot "bin/cuobjdump.exe") --list-ptx $dll 2>&1
if ($ptx -match "ptx") { Write-Warning "The DLL carries PTX (not expected with -real architectures)" }
Write-Host "OK: $dll ($($found -join ' '))"
