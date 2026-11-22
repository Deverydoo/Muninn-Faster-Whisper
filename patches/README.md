# Local CTranslate2 changes

`ctranslate2-v4.8.3-local.patch` applies to CTranslate2 v4.8.3 (used since
October 2026). It adds:

- `Model::release_gpu_memory()` / `ReplicaPool::release_gpu_memory()`: free the
  model's VRAM without destroying the pool (Loki Studio's worker releases VRAM
  this way, then exits).
- A shutdown flag in `ReplicaWorker` so `idle()` stops calling
  `synchronize_stream()` once shutdown starts (it could block on Windows during
  CUDA teardown). Upstream 4.8's `destroy_context()` in `finalize()` is kept.
- Bounds checks in the DTW backtrace (word alignment).
- CUDA 13 changes in `src/cuda/primitives.cu`, and thread-pool changes in
  `thread_pool.{h,cc}`.

`ctranslate2-v4.7.1-local.patch` is the same set against 4.7.1 plus the
BS::thread_pool 5.1 upgrade, which upstream made itself in 4.8.

## Build (Windows, CUDA 13.2)

```
git clone --depth 1 --branch v4.8.3 --recurse-submodules --shallow-submodules ^
    https://github.com/OpenNMT/CTranslate2.git third_party/CTranslate2-4.8.3
cd third_party/CTranslate2-4.8.3
git apply ../../patches/ctranslate2-v4.8.3-local.patch
cmake -B build_cuda13 -G "Visual Studio 17 2022" -A x64 -DBUILD_SHARED_LIBS=ON ^
    -DBUILD_CLI=ON -DBUILD_TESTS=OFF -DWITH_CUDA=ON -DWITH_CUDNN=ON ^
    -DCUDA_DYNAMIC_LOADING=OFF -DWITH_MKL=OFF -DWITH_DNNL=OFF -DWITH_OPENBLAS=OFF ^
    -DWITH_RUY=ON -DWITH_FLASH_ATTN=OFF -DWITH_TENSOR_PARALLEL=OFF ^
    -DENABLE_CPU_DISPATCH=ON -DOPENMP_RUNTIME=COMP ^
    "-DCUDA_ARCH_LIST=7.5;8.0;8.6;8.9;9.0" ^
    "-DCUDA_NVCC_FLAGS=-gencode;arch=compute_120,code=sm_120" ^
    "-DCUDAToolkit_ROOT=C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2"
cmake --build build_cuda13 --config Release --target ctranslate2
```

The CPU backend is Ruy, which is bundled with CTranslate2 (Apache 2.0) and does
float32 and int8 GEMM, with AVX2/AVX-512 kernels picked at run time. OpenMP is
MSVC's (`VCOMP140.DLL`, part of the Visual C++ runtime) for the other CPU ops.
Without a CPU backend (`WITH_RUY=OFF`, as until November 2026) `device="cpu"`
throws "No SGEMM backend on CPU", so PCs without an RTX 20 series or newer GPU
couldn't transcribe at all. MKL would be faster on Intel CPUs but means
installing oneAPI and shipping Intel's OpenMP; Ruy needs nothing extra.

sm_120 (RTX 50) goes in through `CUDA_NVCC_FLAGS` because CMake's FindCUDA
module, which CTranslate2 still uses, has no name for 12.0. Check the result
with `cuobjdump --list-elf ctranslate2.dll`: sm_75, 80, 86, 89, 90 and 120.
