# CTranslate2 Modifications for Muninn

This document tracks all modifications made to the CTranslate2 library in `third_party/CTranslate2/` for Muninn-Faster-Whisper.

## Summary of Changes

| File | Change Type | Description |
|------|-------------|-------------|
| `CMakeLists.txt` | Addition | Added median_filter_gpu.cu to CUDA sources |
| `include/ctranslate2/ops/median_filter.h` | Enhancement | Added GPU compute template |
| `src/ops/median_filter.cc` | Enhancement | Added device dispatch for CPU/GPU |
| `src/ops/median_filter_gpu.cu` | New File | GPU implementation of MedianFilter |
| `include/ctranslate2/replica_pool.h` | Bug Fix | Fixed shutdown hang on Windows |
| `src/thread_pool.cc` | Bug Fix | Fixed thread pool shutdown race condition |

---

## 1. GPU MedianFilter Implementation

### Problem
The `MedianFilter` operation was CPU-only, creating a performance bottleneck during Whisper word-level alignment on GPU-accelerated transcription.

### Solution
Implemented CUDA kernel for MedianFilter with optimizations for small window sizes (3-15).

### Files Modified

#### `CMakeLists.txt`
```diff
+ src/ops/median_filter_gpu.cu
```
Added the new CUDA source file to the build.

#### `include/ctranslate2/ops/median_filter.h`
```cpp
// Added compute template for device-specific implementations
template <Device D>
void compute(const StorageView& input,
             const dim_t batch_size,
             const dim_t depth,
             StorageView& output) const;
```

#### `src/ops/median_filter.cc`
```cpp
// Changed from direct CPU call to device dispatch
#include "device_dispatch.h"

void MedianFilter::operator()(const StorageView& input, StorageView& output) const {
  // ... validation code ...
  DEVICE_DISPATCH(input.device(), (compute<D>(input, batch_size, depth, output)));
}
```

#### `src/ops/median_filter_gpu.cu` (New File)
```cpp
// CUDA kernel using insertion sort (efficient for small windows)
template <typename T, int Width>
__global__ void median_filter_kernel(const T* __restrict__ input,
                                     T* __restrict__ output,
                                     const cuda::index_t batch_size,
                                     const cuda::index_t depth) {
  // Each thread processes one output element
  // Uses register-based window storage
  // Insertion sort for median finding
}

// Template dispatch for widths 3, 5, 7, 9, 11, 13, 15
template <typename T>
void launch_median_filter(const T* input, T* output,
                          dim_t batch_size, dim_t depth,
                          dim_t width, cudaStream_t stream);
```

### Technical Details
- Uses insertion sort algorithm (O(n²) but efficient for n ≤ 15)
- Each CUDA thread processes one output element
- Window data stored in registers for maximum performance
- Boundary reflection for edge handling matches CPU implementation
- Compile-time template specialization for common window sizes

---

## 2. Windows Shutdown Hang Fix

### Problem
On Windows, CTranslate2-based applications would hang indefinitely during shutdown. The issue occurred when `cudaStreamSynchronize()` was called during CUDA resource teardown, blocking forever.

### Root Cause
The thread pool's `idle()` callback called `synchronize_stream()` which executed `cudaStreamSynchronize()`. During shutdown, this could block indefinitely if CUDA resources were being destroyed.

### Solution
Added shutdown detection flags to prevent synchronization calls during teardown.

### Files Modified

#### `include/ctranslate2/replica_pool.h`
```cpp
class ReplicaWorker : public Worker {
  // ... existing code ...

protected:
  void idle() override {
    // Skip sync during shutdown to prevent blocking on cudaStreamSynchronize
    if (_shutting_down)
      return;
    synchronize_stream(_device);
  }

  void finalize() override {
    // Mark as shutting down to prevent idle() from blocking
    _shutting_down = true;
    // Don't call synchronize_stream during shutdown
    _replica.reset();
  }

private:
  bool _shutting_down;  // Added flag
};
```

#### `src/thread_pool.cc`
```cpp
std::unique_ptr<Job> JobQueue::get(const std::function<void()>& before_wait) {
  std::unique_lock<std::mutex> lock(_mutex);

  while (!can_get_job()) {
    // Only call before_wait() if we're not shutting down.
    // This is critical because before_wait() may call cudaStreamSynchronize()
    // which can block indefinitely if there's pending work.
    if (before_wait && !_request_end) {
      lock.unlock();
      before_wait();
      lock.lock();
      continue;  // Re-check condition after re-acquiring lock
    }
    _can_get_job.wait(lock, [this]{ return can_get_job(); });
  }
  // ... rest of method ...
}
```

### Technical Details
- `_shutting_down` flag in `ReplicaWorker` prevents `idle()` from calling `synchronize_stream()` during shutdown
- `_request_end` check in `JobQueue::get()` prevents `before_wait()` callback during queue closure
- Proper lock release/re-acquire pattern to avoid mutex deadlocks
- CUDA cleanup handled through normal destruction sequence instead of explicit synchronization

---

## Build Instructions

After modifying CTranslate2, rebuild with:

```bash
cmake --build third_party/CTranslate2/build --config Release
```

Then copy the updated DLL to Muninn's build directory:

```bash
copy third_party\CTranslate2\build\Release\ctranslate2.dll build\Release\Release\
```

---

## Testing

The modifications have been tested with:
1. Word-level alignment using Whisper transcription
2. Multi-file batch transcription
3. Application shutdown (previously hung on Windows)
4. GPU memory management under load

---

## Upstream Contribution Notes

These changes could be contributed back to the CTranslate2 project:

1. **GPU MedianFilter**: A pure performance enhancement with no API changes. Should be straightforward to upstream.

2. **Shutdown Fix**: Platform-specific bug fix for Windows. Would need testing on Linux/macOS to ensure no regressions.

When submitting PRs:
- Reference this documentation
- Include test cases demonstrating the fixes
- Note Windows-specific behavior for the shutdown fix
