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
    -DWITH_RUY=OFF -DWITH_FLASH_ATTN=OFF -DWITH_TENSOR_PARALLEL=OFF ^
    -DENABLE_CPU_DISPATCH=ON -DOPENMP_RUNTIME=NONE ^
    "-DCUDA_ARCH_LIST=7.5;8.0;8.6;8.9;9.0" ^
    "-DCUDA_NVCC_FLAGS=-gencode;arch=compute_120,code=sm_120" ^
    "-DCUDAToolkit_ROOT=C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2"
cmake --build build_cuda13 --config Release --target ctranslate2
```

sm_120 (RTX 50) goes in through `CUDA_NVCC_FLAGS` because CMake's FindCUDA
module, which CTranslate2 still uses, has no name for 12.0. Check the result
with `cuobjdump --list-elf ctranslate2.dll`: sm_75, 80, 86, 89, 90 and 120.
