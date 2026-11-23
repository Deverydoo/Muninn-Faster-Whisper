# CTranslate2 changes for Muninn

Muninn builds against CTranslate2 **4.9.1** with a local patch,
`patches/ctranslate2-v4.9.1-local.patch`. The build steps are in
[patches/README.md](../patches/README.md) (or run `scripts/build_ctranslate2.ps1`,
which follows them). The tree lives in `third_party/CTranslate2-4.9.1`, which is
not tracked in git.

The 4.9.1 patch is the 4.8.3 patch rebased (only `replica_pool.h` needed changes),
minus one hunk: the older patches also sent float32 GEMM through `cublasGemmEx`
with `CUBLAS_GEMM_DEFAULT_TENSOR_OP` in `src/cuda/primitives.cu`, which lets
cuBLAS round float32 inputs down. The accuracy clips came out byte-identical
without it (audit item C2), so CTranslate2's own float32 GEMM is used. The
patches for 4.8.3 and 4.7.1 are kept beside it for reference.

## What the patch changes

| File | Change |
|------|--------|
| `include/ctranslate2/models/model.h`, `src/models/model.cc` | `Model::release_gpu_memory()`: frees the model's weights (and so its VRAM) without destroying the object. The destructor calls it and does nothing if it already ran. |
| `include/ctranslate2/replica_pool.h` | `ReplicaPool::release_gpu_memory()`. `detach_models()` also calls `prepare_shutdown()` on each worker. `ReplicaWorker` gets an atomic `_shutting_down` flag: `idle()` returns early once it is set, and `prepare_shutdown()` and `finalize()` set it. Upstream's `synchronize_stream_nothrow` in `idle()` and `destroy_context()` in `finalize()` are kept. |
| `include/ctranslate2/thread_pool.h`, `src/thread_pool.cc` | `Worker::join(int timeout_ms = 5000)`: if the worker thread doesn't finish in time it is detached, with a warning on stderr. A virtual `Worker::prepare_shutdown()`, called on every worker by `~ThreadPool()` before the queue is closed. `JobQueue::get()` doesn't call `before_wait()` (the idle stream sync) once the queue is closing, and releases the lock while it runs. |
| `src/dtw.cc` | Bounds checks in the DTW backtrace used for word alignment: an out-of-range index throws instead of reading past the trace, and no negative pairs are emitted. |

## Why

- **Releasing VRAM without destroying the model.** Destroying a CTranslate2
  model on Windows can hang during CUDA teardown, and destroying the thread
  pool could leave the CUDA context unusable for other GPU work in the same
  process. Muninn's `Transcriber::unload_model()` and `Translator::unload_model()`
  detach the models, call `release_gpu_memory()` on each, clear the CUDA
  allocator cache, and leave the empty model object alive. Loki Studio's worker
  then exits, which is what finally releases everything.
- **Shutdown hangs.** A worker's `idle()` synchronises the CUDA stream. During
  shutdown that call could block forever on Windows, so the flag and the
  `before_wait` check keep it from running once shutdown has started, and the
  timed join stops a stuck worker from hanging the process.
- **DTW bounds.** Word alignment crashed on some inputs where the backtrace
  indices ran past the trace.

## Build settings

See [patches/README.md](../patches/README.md) for the full command. In short:
CUDA 13.2 (`-T cuda=...`), cuDNN on, `CMAKE_CUDA_ARCHITECTURES` 75/80/86/89/90/120
(`-real`, no PTX), a shared CUDA runtime, and the Ruy CPU backend with MSVC's
OpenMP, so `device="cpu"` works. 4.9 dropped FindCUDA, so the old
`CUDA_ARCH_LIST` / `CUDA_NVCC_FLAGS` options are ignored; check each build with
`cuobjdump --list-elf ctranslate2.dll`.

When switching CTranslate2 versions, rebuild muninn.dll too (4.9 changed the
`Model` vtable) and clear the cached paths: `-DCTranslate2_DIR=<new tree>
-U CTRANSLATE2_LIB -U CTRANSLATE2_DLL`.
