# Cancellation API

Muninn provides a thread-safe cancellation API for both `Transcriber` and `Translator` classes, allowing GUI applications to cancel long-running operations without blocking or hanging.

## Quick Start

```cpp
#include <muninn/transcriber.h>
#include <thread>

muninn::Transcriber transcriber("models/faster-whisper-large-v3-turbo");

// Start transcription in worker thread
std::thread worker([&]() {
    auto result = transcriber.transcribe("long_audio.mp4", options);

    if (result.was_cancelled) {
        std::cout << "Transcription was cancelled\n";
        // Handle partial results in result.segments
    }
});

// Cancel from UI thread (e.g., button click)
transcriber.cancel();

worker.join();
```

## API Reference

### Transcriber

```cpp
class Transcriber {
public:
    // Request cancellation (thread-safe, can be called from any thread)
    void cancel();

    // Reset cancellation flag (auto-called at start of transcribe())
    void reset_cancel();

    // Check if cancellation was requested
    bool is_cancelled() const;
};
```

### Translator

```cpp
class Translator {
public:
    // Request cancellation (thread-safe)
    void cancel();

    // Reset cancellation flag
    void reset_cancel();

    // Check if cancellation was requested
    bool is_cancelled() const;
};
```

### TranscribeResult

```cpp
struct TranscribeResult {
    std::vector<Segment> segments;  // Partial results if cancelled
    bool was_cancelled;             // True if transcription was cancelled
    // ... other fields
};
```

## How It Works

### Thread Safety

The cancellation flag uses `std::atomic<bool>` with proper memory ordering:
- `cancel()` uses `memory_order_release` to ensure visibility
- Checks use `memory_order_acquire` to see the latest value

This guarantees that when you call `cancel()` from the UI thread, the worker thread will see it at the next checkpoint.

### Cooperative Cancellation

Cancellation is **cooperative**, meaning it checks at safe points rather than forcibly terminating:

1. **Before each batch** - Checked before starting GPU inference
2. **After each batch** - Checked after processing results
3. **Between tracks** - Checked when processing multi-track files
4. **Between translation chunks** - Checked every 8 segments

This ensures:
- No GPU resources are left in an inconsistent state
- Partial results are valid and usable
- No memory leaks or crashes

### Checkpoint Locations

```
Transcription Flow:
┌─────────────────┐
│  Load Audio     │
└────────┬────────┘
         │
┌────────▼────────┐
│  VAD Processing │
└────────┬────────┘
         │
┌────────▼────────┐
│  Split Chunks   │
└────────┬────────┘
         │
    ┌────▼────┐
    │  Batch  │◄─── ✓ Cancellation check
    │   1     │
    └────┬────┘
         │
    ┌────▼────┐
    │  Batch  │◄─── ✓ Cancellation check
    │   2     │
    └────┬────┘
         │
        ...
```

## Usage Patterns

### Pattern 1: Simple Cancel Button

```cpp
// Member variables
muninn::Transcriber* m_transcriber;
std::thread m_worker;

void onStartClicked() {
    m_worker = std::thread([this]() {
        auto result = m_transcriber->transcribe(m_audioPath, m_options);
        emit transcriptionComplete(result);
    });
}

void onCancelClicked() {
    m_transcriber->cancel();  // Safe to call from UI thread
}
```

### Pattern 2: Progress Callback with Cancel

The progress callback can also trigger cancellation by returning `false`:

```cpp
auto result = transcriber.transcribe(audio_path, options,
    [this](int track, int total, float progress, const std::string& msg) {
        updateProgressBar(progress);
        return !m_cancelRequested;  // Return false to cancel
    });
```

### Pattern 3: Cancelling Both Transcription and Translation

```cpp
void onCancelClicked() {
    m_transcriber->cancel();
    m_translator->cancel();
}
```

### Pattern 4: Checking Partial Results

```cpp
auto result = transcriber.transcribe(audio_path, options);

if (result.was_cancelled) {
    // Still have valid partial results!
    std::cout << "Got " << result.segments.size() << " segments before cancel\n";

    for (const auto& seg : result.segments) {
        std::cout << seg.text << "\n";  // Valid transcribed text
    }
}
```

## Best Practices

### DO:
- Call `cancel()` from your UI thread - it's thread-safe
- Check `result.was_cancelled` to know if cancellation occurred
- Use partial results if needed - they're valid

### DON'T:
- Don't call `reset_cancel()` before `transcribe()` - it's automatic
- Don't try to force-terminate threads - use cooperative cancellation
- Don't assume immediate cancellation - it happens at checkpoints

## Latency

Cancellation latency depends on where the operation is in its processing:

| Operation | Typical Latency |
|-----------|-----------------|
| Between batches | < 100ms |
| During batch inference | Up to batch duration (1-5s) |
| During VAD | < 500ms |
| During audio extraction | < 1s |

For very long audio files with large batches, worst-case cancellation latency is the time to complete one batch (typically 1-5 seconds on GPU).

## Error Handling

Cancellation is not an error - it's a normal control flow:

```cpp
try {
    auto result = transcriber.transcribe(path, options);

    if (result.was_cancelled) {
        // Normal cancellation - not an exception
        handlePartialResults(result);
    } else {
        handleCompleteResults(result);
    }
} catch (const std::exception& e) {
    // Actual errors (file not found, GPU error, etc.)
    handleError(e.what());
}
```

## Implementation Notes

### Memory Ordering

```cpp
// In cancel():
cancelled.store(true, std::memory_order_release);

// In transcribe loop:
if (cancelled.load(std::memory_order_acquire)) {
    // Guaranteed to see the store from cancel()
}
```

### Auto-Reset Behavior

`Transcriber::transcribe()` automatically resets the cancellation flag at the start:

```cpp
TranscribeResult Transcriber::transcribe(...) {
    pimpl_->cancelled.store(false, std::memory_order_release);
    // ... rest of transcription
}
```

This means you don't need to call `reset_cancel()` between transcriptions - each call starts fresh.

For `Translator`, you should call `reset_cancel()` explicitly if reusing after a cancellation.
