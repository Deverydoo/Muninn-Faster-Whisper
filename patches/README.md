# Local CTranslate2 changes

`ctranslate2-v4.9.1-local.patch` applies to CTranslate2 v4.9.1 (used since
November 2026; the transcripts and word timings of the accuracy clips came out
identical to 4.8.3). It is the 4.8.3 patch rebased: only `replica_pool.h`
changed, where upstream added a `_factory` member and now calls
`synchronize_stream_nothrow` in `idle()` (kept, with the shutdown check in
front). It adds:

- `Model::release_gpu_memory()` / `ReplicaPool::release_gpu_memory()`: free the
  model's VRAM without destroying the pool (Loki Studio's worker releases VRAM
  this way, then exits).
- A shutdown flag in `ReplicaWorker` so `idle()` stops calling
  `synchronize_stream()` once shutdown starts (it could block on Windows during
  CUDA teardown). Upstream 4.8's `destroy_context()` in `finalize()` is kept.
- Bounds checks in the DTW backtrace (word alignment).
- `src/cuda/primitives.cu`: float32 GEMM goes through `cublasGemmEx` with
  `CUBLAS_GEMM_DEFAULT_TENSOR_OP`. Older notes called this "CUDA 13 changes";
  it is a precision setting (cuBLAS may round inputs down for tensor cores),
  measured separately (see the audit, C2).
- Thread-pool changes in `thread_pool.{h,cc}`.

`ctranslate2-v4.8.3-local.patch` and `ctranslate2-v4.7.1-local.patch` are the
same set against the older versions (4.7.1 also carried the BS::thread_pool 5.1
upgrade, which upstream made itself in 4.8).

## Build (Windows, CUDA 13.2)

```
git clone --depth 1 --branch v4.9.1 --recurse-submodules --shallow-submodules ^
    https://github.com/OpenNMT/CTranslate2.git third_party/CTranslate2-4.9.1
cd third_party/CTranslate2-4.9.1
git apply ../../patches/ctranslate2-v4.9.1-local.patch
cmake -B build_cuda13 -G "Visual Studio 17 2022" -A x64 ^
    -T "cuda=C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2" ^
    -DBUILD_SHARED_LIBS=ON -DBUILD_CLI=ON -DBUILD_TESTS=OFF -DWITH_CUDA=ON -DWITH_CUDNN=ON ^
    -DCUDA_DYNAMIC_LOADING=OFF -DWITH_MKL=OFF -DWITH_DNNL=OFF -DWITH_OPENBLAS=OFF ^
    -DWITH_RUY=ON -DWITH_FLASH_ATTN=OFF -DWITH_TENSOR_PARALLEL=OFF ^
    -DENABLE_CPU_DISPATCH=ON -DOPENMP_RUNTIME=COMP ^
    "-DCMAKE_CUDA_ARCHITECTURES=75-real;80-real;86-real;89-real;90-real;120-real" ^
    -DCMAKE_CUDA_RUNTIME_LIBRARY=Shared ^
    "-DCUDAToolkit_ROOT=C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2"
cmake --build build_cuda13 --config Release --target ctranslate2
```

4.9 dropped FindCUDA: the old `CUDA_ARCH_LIST` / `CUDA_NVCC_FLAGS` options are
ignored without a word, and the build then covers one GPU generation. Check
every build with `cuobjdump --list-elf ctranslate2.dll` (sm_75, 80, 86, 89, 90,
120) and `--list-ptx` (none). `-T cuda=` picks CUDA 13.2 when an older CUDA's
Visual Studio integration is also installed.

After switching CTranslate2, reconfigure Muninn's build with
`-DCTranslate2_DIR=<new tree> -U CTRANSLATE2_LIB -U CTRANSLATE2_DLL`: the old
paths are cached, and the old DLL with the new headers crashes (4.9 changed the
`Model` vtable).

```
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
