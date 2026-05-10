# Thread Safety Audit Report

**Audit Date:** December 2024
**Auditor:** Claude Code
**Project:** Muninn Faster-Whisper

## Executive Summary

Muninn provides a mixed thread-safety model:
- **Cancellation API**: Thread-safe via `std::atomic<bool>` with proper memory ordering
- **StreamingTranscriber**: Thread-safe for producer/consumer pattern
- **Transcriber/Translator instances**: NOT thread-safe for concurrent operations on the same instance
- **Static methods**: Thread-safe

---

## Class-by-Class Analysis

### 1. Transcriber (`src/transcriber.cpp`)

#### Thread-Safe Components

| Component | Mechanism | Notes |
|-----------|-----------|-------|
| `cancel()` | `std::atomic<bool>` with `memory_order_release` | Safe to call from any thread |
| `reset_cancel()` | `std::atomic<bool>` with `memory_order_release` | Safe to call from any thread |
| `is_cancelled()` | `std::atomic<bool>` with `memory_order_acquire` | Safe to call from any thread |
| `get_audio_info()` | Static method | Thread-safe (no shared state) |

#### NOT Thread-Safe

| Component | Issue | Recommendation |
|-----------|-------|----------------|
| `transcribe()` | No mutex protection on `pimpl_->model`, mel converter, or VAD instances | Do NOT call concurrently on same instance |
| `pimpl_->model` | CTranslate2 Whisper model is not thread-safe | One transcription at a time per instance |
| `pimpl_->mel_converter` | MelSpectrogram instance has no internal synchronization | One transcription at a time per instance |

#### Cancellation Implementation (CORRECT)

```cpp
// In Impl class:
std::atomic<bool> cancelled{false};

// In transcribe():
pimpl_->cancelled.store(false, std::memory_order_release);  // Auto-reset at start

// Check before each batch:
if (pimpl_->cancelled.load(std::memory_order_acquire)) {
    result.was_cancelled = true;
    return result;
}

// Public cancel():
void Transcriber::cancel() {
    pimpl_->cancelled.store(true, std::memory_order_release);
}
```

**Verdict:** The cancellation implementation is correct and safe. The `release/acquire` memory ordering ensures visibility across threads.

---

### 2. Translator (`src/translator.cpp`)

#### Thread-Safe Components

| Component | Mechanism | Notes |
|-----------|-----------|-------|
| `cancel()` | `std::atomic<bool>` with `memory_order_release` | Safe to call from any thread |
| `reset_cancel()` | `std::atomic<bool>` with `memory_order_release` | Safe to call from any thread |
| `is_cancelled()` | `std::atomic<bool>` with `memory_order_acquire` | Safe to call from any thread |
| `shutdown()` | `std::atomic<bool>` exchange for one-time execution | Safe shutdown pattern |
| Static methods | `to_nllb_code()`, `from_nllb_code()`, `supported_languages()` | Thread-safe |

#### NOT Thread-Safe

| Component | Issue | Recommendation |
|-----------|-------|----------------|
| `translate()` / `translate_batch()` | No mutex on CTranslate2 model | One translation at a time per instance |
| `pimpl_->model` | CTranslate2 Translator is not thread-safe | Create separate instances for parallel translation |

#### Static Variable Safety

```cpp
// Thread-safe initialization of reverse mapping
static std::unordered_map<std::string, std::string> NLLB_TO_CODE;
static std::once_flag reverse_mapping_once;

static void init_reverse_mapping() {
    std::call_once(reverse_mapping_once, []() {
        for (const auto& pair : CODE_TO_NLLB) {
            NLLB_TO_CODE[pair.second] = pair.first;
        }
    });
}
```

**Verdict:** Static variable initialization is correctly protected by `std::call_once`.

---

### 3. StreamingTranscriber (`src/streaming_transcriber.cpp`)

#### Thread-Safe Components

| Component | Mechanism | Notes |
|-----------|-----------|-------|
| `push_audio()` | `std::mutex` on all buffer access | **Thread-safe** - can call from audio thread |
| `poll_segment()` | `std::mutex` on segment queue | **Thread-safe** - can call from main thread |
| `get_all_segments()` | `std::mutex` on completed segments | Thread-safe |
| `get_current_text()` | `std::mutex` on completed segments | Thread-safe |
| `get_stats()` | `std::mutex` on stats access | Thread-safe |
| `reset()` | `std::mutex` on all state | Thread-safe |
| `is_active()` | `std::atomic<bool>` | Thread-safe |
| Stop signaling | `std::atomic<bool> stop_requested_` | Thread-safe |

#### Implementation Pattern (Producer/Consumer)

```cpp
class StreamingTranscriber::Impl {
    mutable std::mutex mutex_;
    std::atomic<bool> active_;
    std::atomic<bool> stop_requested_;
    std::thread worker_;

    // Protected by mutex_:
    std::vector<float> audio_buffer_;
    std::vector<Segment> completed_segments_;
    std::deque<Segment> pending_segments_;
};
```

#### Thread Communication Flow

```
Audio Thread                    Worker Thread                 UI Thread
     │                               │                            │
     │ push_audio()                  │                            │
     │───[mutex]──►audio_buffer_     │                            │
     │                               │                            │
     │                 ┌─[mutex]─────┤                            │
     │                 │ process_buffered_audio()                 │
     │                 │     │                                    │
     │                 │     ▼                                    │
     │                 │ pending_segments_                        │
     │                 └─────────────┤                            │
     │                               │                            │
     │                               │              poll_segment()│
     │                               │◄──────[mutex]──────────────┤
     │                               │                            │
     │                               │      stop_requested_ = true│
     │                               │◄──────[atomic]─────────────┤
```

**Verdict:** StreamingTranscriber is properly thread-safe for the producer/consumer pattern.

---

### 4. VAD Components

#### Energy VAD (`src/vad.cpp`)

**Thread Safety:** NOT thread-safe

- No mutex protection
- Instance variable `silence_removed_` is modified during processing
- Safe if: One VAD instance per thread, or external synchronization

#### Silero VAD (`src/silero_vad.cpp`)

**Thread Safety:** NOT thread-safe

- ONNX Runtime session is not thread-safe
- Internal state vectors (`state_`, `context_`) are modified during inference
- Safe if: One SileroVAD instance per thread

#### Recommendation

VAD instances are created locally within `Transcriber::transcribe()`, so they are thread-safe by virtue of being stack-local to each transcription call.

---

### 5. MelSpectrogram (`src/mel_spectrogram.cpp`)

**Thread Safety:** Conditionally safe

- Constructor creates immutable lookup tables (`hann_window_`, `mel_filters_`)
- `compute()` only reads from these tables
- **Thread-safe for concurrent reads** after construction

However, the `Transcriber::Impl::mel_converter` member is shared, so concurrent `transcribe()` calls on the same Transcriber instance would have undefined behavior if MelSpectrogram were modified (currently safe since `compute()` is read-only).

---

## Shared/Static State Analysis

### Safe Static Variables

| Variable | Location | Protection |
|----------|----------|------------|
| `CODE_TO_NLLB` | translator.cpp | `const` (immutable) |
| `SUPPORTED_LANGUAGES` | translator.cpp | `const` (immutable) |
| `NLLB_TO_CODE` | translator.cpp | `std::call_once` |

### Potential Issues

1. **Global Logger**: If `Logger::info()` / `Logger::error()` write to shared state (file/stream), they need internal synchronization.

2. **CUDA Context**: CTranslate2 manages CUDA context internally. Creating multiple Transcriber/Translator instances on different threads for the same GPU may require CTranslate2's built-in synchronization.

---

## Usage Patterns

### SAFE: UI Cancel Pattern

```cpp
// Worker thread
std::thread worker([&transcriber, &audio_path, &options]() {
    auto result = transcriber.transcribe(audio_path, options);
    if (result.was_cancelled) {
        // Handle partial results
    }
});

// UI thread (e.g., button click)
void onCancelClicked() {
    transcriber.cancel();  // Thread-safe!
}
```

### SAFE: Streaming Pattern

```cpp
// Audio callback thread (from OS/driver)
void onAudioData(const float* samples, size_t count) {
    streamer.push_audio(samples, count, 48000, 2);  // Thread-safe!
}

// Main/UI thread
void pollForCaptions() {
    Segment seg;
    while (streamer.poll_segment(seg)) {  // Thread-safe!
        displayCaption(seg.text);
    }
}
```

### UNSAFE: Concurrent Transcription

```cpp
// DON'T DO THIS - Same instance, multiple threads
std::thread t1([&]() { transcriber.transcribe("file1.mp3", opts); });
std::thread t2([&]() { transcriber.transcribe("file2.mp3", opts); });  // RACE CONDITION!
```

### SAFE: Parallel Transcription (Separate Instances)

```cpp
// DO THIS - Separate instances
Transcriber t1("models/whisper");
Transcriber t2("models/whisper");

std::thread thread1([&]() { t1.transcribe("file1.mp3", opts); });
std::thread thread2([&]() { t2.transcribe("file2.mp3", opts); });  // SAFE
```

---

## Recommendations

### 1. Document Thread Safety in Headers

Add thread-safety annotations to public headers:

```cpp
/**
 * @brief Request cancellation of ongoing transcription
 *
 * Thread Safety: SAFE - Can be called from any thread.
 */
void cancel();

/**
 * @brief Transcribe audio from file
 *
 * Thread Safety: NOT safe for concurrent calls on the same instance.
 * Create separate Transcriber instances for parallel transcription.
 */
TranscribeResult transcribe(...);
```

### 2. Consider Adding Instance-Level Mutex (Optional)

If users need concurrent `transcribe()` calls on the same instance (unlikely but possible), add:

```cpp
class Transcriber::Impl {
    std::mutex transcribe_mutex_;  // Serialize transcribe() calls
};

TranscribeResult Transcriber::transcribe(...) {
    std::lock_guard<std::mutex> lock(pimpl_->transcribe_mutex_);
    // ... existing implementation
}
```

**Trade-off:** This would queue concurrent calls rather than reject them. Consider if this is the desired behavior.

### 3. Validate Logger Thread Safety

Ensure `Logger::info()` and `Logger::error()` either:
- Use `std::mutex` internally, OR
- Use `std::osyncstream` (C++20), OR
- Write to thread-local buffers

---

## Summary Table

| Class | Concurrent Instances | Same Instance | Cancel API |
|-------|---------------------|---------------|------------|
| Transcriber | SAFE | NOT SAFE | SAFE |
| Translator | SAFE | NOT SAFE | SAFE |
| StreamingTranscriber | SAFE | SAFE (designed for it) | N/A (uses callback) |
| VAD | SAFE | NOT SAFE | N/A |
| SileroVAD | SAFE | NOT SAFE | N/A |
| MelSpectrogram | SAFE | SAFE (read-only) | N/A |

---

## Conclusion

Muninn's thread-safety model is appropriate for its use case:

1. **The cancellation API is correctly implemented** with proper atomic operations and memory ordering.

2. **StreamingTranscriber is fully thread-safe** for the producer/consumer pattern it's designed for.

3. **Transcriber and Translator are single-threaded per instance**, which is a reasonable design choice given that GPU resources are typically the bottleneck.

4. **For parallel transcription**, users should create separate instances, which is well-documented and works correctly.

The implementation follows best practices for cooperative cancellation and doesn't introduce any thread-safety bugs in its current form.
