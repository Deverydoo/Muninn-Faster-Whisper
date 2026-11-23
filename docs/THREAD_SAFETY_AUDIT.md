# Thread Safety Audit Report (December 2024)

> **Historical report.** This is the thread-safety review of December 2024.
> It is superseded by [CODE_AUDIT_2026-11.md](CODE_AUDIT_2026-11.md). The
> sections below are kept as written except where a claim is no longer true;
> those are corrected and listed here.
>
> What changed since:
> - **Transcriber cancel flag:** cleared when `transcribe()` returns, not at
>   its start, so a `cancel()` sent just before a run counts. Partial results
>   of a cancelled run get the same timestamp restore as a full run.
> - **Transcriber unload:** `transcribe()` holds a shared lock on the model
>   for the whole run and `unload_model()` takes it exclusively, so unloading
>   waits for a running transcription instead of freeing the model under it
>   (its `timeout_ms` is currently not used). `clear_cache()` checks for a
>   null implementation before locking.
> - **Translator:** `translate_batch()` holds a shared lock for the whole
>   decode; `unload_model()` and `shutdown()` take it exclusively.
>   `unload_model(timeout_ms)` asks a running batch to stop at its next chunk
>   and returns `false` if it can't get the lock in time. The cancel flag
>   stays set until `reset_cancel()`.
> - **Logger:** thread-safe. The callback is swapped under a mutex and called
>   outside the lock (so it may log itself); without a callback every level
>   goes to stderr, one whole line at a time.
> - **StreamingTranscriber:** rewritten in November 2026. Audio goes into a
>   buffer with its own short lock; `push_audio()` never waits for a decode;
>   callbacks run with no lock held; `start()`/`stop()` throw
>   `std::logic_error` when called from a callback.
> - **Language tables** (translator) are function-local statics, initialised
>   once by the compiler; the `std::call_once` code quoted below is gone.

## Executive Summary

- **Cancellation API:** thread-safe (`std::atomic<bool>`, release/acquire).
- **StreamingTranscriber:** thread-safe for one producer pushing audio and
  other threads polling or reading results.
- **Transcriber / Translator:** one operation at a time per instance. Calling
  `unload_model()` from another thread during a run is safe (see above).
- **Static methods:** thread-safe.

---

## Class-by-Class Analysis

### 1. Transcriber (`src/transcriber.cpp`)

#### Thread-safe

| Component | Mechanism | Notes |
|-----------|-----------|-------|
| `cancel()`, `reset_cancel()`, `is_cancelled()` | `std::atomic<bool>` | Any thread |
| `unload_model()` | exclusive lock on the inference mutex | Waits for a running `transcribe()` |
| `get_audio_info()` | static, no shared state | |

#### Not thread-safe

| Component | Issue | Recommendation |
|-----------|-------|----------------|
| `transcribe()` | Concurrent calls on one instance share the mel converter and per-run state (the inference lock is shared, so it doesn't serialise them) | One transcription at a time per instance |

The cancellation implementation uses `memory_order_release` in `cancel()` and
`memory_order_acquire` at each check, which is correct.

---

### 2. Translator (`src/translator.cpp`)

#### Thread-safe

| Component | Mechanism | Notes |
|-----------|-----------|-------|
| `cancel()`, `reset_cancel()`, `is_cancelled()` | `std::atomic<bool>` | Any thread |
| `unload_model()`, `shutdown()` | exclusive lock on the model | Stop and wait for a running batch |
| `to_nllb_code()`, `from_nllb_code()`, `supported_languages()` | static, read-only tables | |

#### Not thread-safe

| Component | Recommendation |
|-----------|----------------|
| `translate()` / `translate_batch()` from several threads on one instance | Not tested; use one at a time, or separate instances |
| Destroying the Translator while another thread is inside one of its methods | Not allowed |

---

### 3. StreamingTranscriber (`src/streaming_transcriber.cpp`)

| Component | Notes |
|-----------|-------|
| `push_audio()` | Any thread; calls from several threads are serialised; never waits for a decode |
| `poll_segment()`, `get_all_segments()`, `get_current_text()`, `get_stats()`, `last_error()` | Any thread, also from the callbacks |
| `reset()` | Any thread, also from a callback |
| `start()`, `stop()` | Not from a callback (they throw `std::logic_error`) |
| Segment and error callbacks | Run on the worker thread with no lock held |

Details are in [STREAMING_API.md](STREAMING_API.md).

---

### 4. VAD Components

`VAD` (energy) and `SileroVAD` keep state between calls (Silero's recurrent
state, `silence_removed_`), so one instance must not be used from two threads
at once. The transcriber creates them per track, so this only matters when you
use them directly.

---

### 5. MelSpectrogram (`src/mel_spectrogram.cpp`)

The window and filterbank are built in the constructor and only read by
`compute()`. The transcriber's converter is a member shared by every run of
that instance, which is one more reason not to run two transcriptions on one
instance at once.

---

## Shared/Static State

| State | Protection |
|-------|------------|
| Translator language tables | `const` / function-local statics |
| `Logger` callback and file | mutex (since November 2026) |
| `CaptionRouter` engine list | `std::shared_mutex` (since November 2026) |

CTranslate2 manages the CUDA context itself. Several instances on one GPU
work, but each loads its own copy of the model.

---

## Usage Patterns

### Safe: cancel from the UI

```cpp
std::thread worker([&] {
    auto result = transcriber.transcribe(audio_path, options);
    if (result.was_cancelled) { /* partial results */ }
});

void onCancelClicked() { transcriber.cancel(); }
```

### Safe: streaming

```cpp
void onAudioData(const float* samples, size_t count) {
    streamer.push_audio(samples, count, 48000, 2);   // count = frames x channels
}

void pollForCaptions() {
    muninn::Segment seg;
    while (streamer.poll_segment(seg)) displayCaption(seg.text);
}
```

### Unsafe: two transcriptions on one instance

```cpp
std::thread t1([&] { transcriber.transcribe("file1.mp3", opts); });
std::thread t2([&] { transcriber.transcribe("file2.mp3", opts); });  // don't
```

### Safe: separate instances

```cpp
muninn::Transcriber a("models/whisper"), b("models/whisper");
std::thread t1([&] { a.transcribe("file1.mp3", opts); });
std::thread t2([&] { b.transcribe("file2.mp3", opts); });
```

---

## Summary Table

| Class | Separate instances | Same instance | Cancel |
|-------|--------------------|---------------|--------|
| Transcriber | Safe | One run at a time; `unload_model()` during a run is safe | Safe |
| Translator | Safe | One batch at a time; `unload_model()`/`cancel()` during a batch are safe | Safe |
| StreamingTranscriber | Safe | Safe (designed for it) | n/a |
| VAD, SileroVAD | Safe | Not safe | n/a |
| MelSpectrogram | Safe | Safe (`compute()` is read-only) | n/a |
