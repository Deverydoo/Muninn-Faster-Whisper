# Cancellation

`Transcriber` and `Translator` can be cancelled from another thread (a UI
thread, for example). Cancellation is cooperative: the running call stops at
its next check and returns what it has.

## Transcriber

```cpp
#include <muninn/transcriber.h>
#include <thread>

muninn::Transcriber transcriber("models/faster-whisper-large-v3-turbo");

std::thread worker([&] {
    auto result = transcriber.transcribe("long_video.mp4", options);
    if (result.was_cancelled) {
        // result.segments holds what was finished, with normal timestamps
    }
});

transcriber.cancel();   // from any thread
worker.join();
```

| Method | |
|---|---|
| `cancel()` | Sets the flag. Safe from any thread. |
| `reset_cancel()` | Clears the flag. Only needed to undo a `cancel()` sent while nothing was running. |
| `is_cancelled()` | Whether the flag is set. |

How the flag behaves:
- It is cleared when `transcribe()` returns, not when it starts. A
  `cancel()` sent just before `transcribe()` is called cancels that run.
- If nothing is running, the flag stays set until the next `transcribe()`
  (which it then cancels) or `reset_cancel()`.

Where it is checked:
- before each track of a file;
- after VAD, before the mel spectrogram is computed;
- before each 30-second window (sequential decoding, the default), or before
  each batch (`sequential = false`).

Audio extraction, VAD itself, the mel spectrogram and the window being
decoded are not interrupted, so a cancel can take as long as the current step.

A progress callback that returns `false` cancels the same way:

```cpp
auto result = transcriber.transcribe(path, options,
    [&](int track, int total, float progress, const std::string& msg) {
        update_progress(track, total, progress, msg);
        return !cancel_requested;
    });
```

### Partial results

`result.segments` keeps every segment finished before the cancel, from all
tracks done so far. Their timestamps go through the same steps as a complete
run (mapped back from the VAD-joined audio, plus the clip offset), so they are
on the original timeline. `failed_windows`, `failed_tracks` and `error` are
filled in as usual. A cancelled run doesn't throw, and speaker diarization
(if enabled) still runs on what was kept.

One gap: when the cancel lands between VAD and the mel spectrogram of the
only (or last) track, the track comes back with no segments and
`was_cancelled` can be `false`.

## Translator

```cpp
translator.cancel();          // from any thread
translator.reset_cancel();    // before the next batch
```

- A running `translate_batch()` stops before its next chunk of 128
  sentences. Items it didn't finish come back `""`.
- The flag stays set until `reset_cancel()`: every batch started meanwhile
  returns at once with all items `""`.
- `translate_batch()` can't tell a cancelled item from a failed one; use
  `translate_batch_result()`, which sets `cancelled` and counts
  `failed_count`.
- `unload_model()` also stops a running batch at its next chunk (and
  reports it as cancelled), then waits for it up to `timeout_ms`.

## Errors

Cancelling is not an error. `transcribe()` throws for real failures: a model
that isn't loaded, a file that can't be opened, an unknown language, or a run
in which no track succeeded.

```cpp
try {
    auto result = transcriber.transcribe(path, options);
    if (result.was_cancelled) { /* partial */ }
    if (result.failed_windows || result.failed_tracks) { /* some audio was lost: result.error */ }
} catch (const std::exception& e) {
    // nothing could be transcribed
}
```
