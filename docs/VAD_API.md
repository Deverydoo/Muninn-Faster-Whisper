# Voice Activity Detection (VAD)

VAD finds the parts of a track that contain speech. Muninn decodes only those
parts, joined together, and maps the timestamps back to the original timeline
afterwards (words by the region their midpoint falls in, as faster-whisper
does). This applies to `Transcriber`; streaming has its own, simpler VAD
handling (see [STREAMING_API.md](STREAMING_API.md)).

## `VADType`

| Value | What happens |
|---|---|
| `Auto` (default) | Chosen per track; see [VAD_AUTO_DETECTION.md](VAD_AUTO_DETECTION.md). |
| `None` | No VAD: the whole track is decoded. Same as `vad_filter = false`. |
| `Energy` | RMS energy detector with an adaptive noise floor. No model needed. |
| `Silero` | Silero VAD (ONNX Runtime, CPU). Needs `silero_model_path`. |
| `TEN` | Not implemented yet: the transcriber logs a warning and uses Silero. |
| `WebRTC` | Not implemented: falls back to Energy. |

Silero falls back to Energy, with a warning in the log, when Muninn was built
without ONNX Runtime, when `silero_model_path` is empty, or when the model
fails to load or run. The fallback uses the energy detector's own threshold.

## Options (`TranscribeOptions`)

| Option | Default | Used by |
|---|---|---|
| `vad_type` | `Auto` | |
| `vad_filter` | `true` | `false` turns VAD off whatever `vad_type` says |
| `vad_threshold` | `0.02` | Energy uses it when it is 0.1 or less (an RMS level). Silero uses it when it is above 0.1 (a speech probability); otherwise Silero uses 0.25. |
| `vad_min_speech_duration_ms` | `250` | Energy, Silero |
| `vad_min_silence_duration_ms` | `500` | Energy, Silero: the silence needed to end a speech region |
| `vad_speech_pad_ms` | `300` | Energy, Silero: padding on each side of a region |
| `vad_max_speech_duration_s` | `30` | Silero: a longer region is split at its last pause of about 100 ms |
| `vad_audio_preamble_ms` | `2000` | Only read by the TEN path, so currently unused (see below) |
| `silero_model_path` | empty | Silero (also needed when `Auto` picks Silero) |
| `ten_vad_model_path` | empty | Unused while TEN is replaced by Silero |
| `skip_silent_tracks` | `true` | A track whose peak stays below 0.0001 is skipped before VAD |
| `track_hints` | empty | `Auto` only: track index to `TrackType` |
| `hallucination_silence_threshold` | `0` | 0 = off. Otherwise the fraction (0 to 1, clamped) of a segment that must fall inside detected speech, counted word by word when there are word timestamps; segments below it are dropped. Only applies when VAD found speech regions. |

Both detectors have a fixed 2-second preamble: speech that starts within the
first 2 s is pulled back to 0. The preamble never becomes a segment on its
own, so a track with no detected speech gives no speech regions.

When a detector finds no speech in a track that does have signal (peak of
0.001 or more), the whole track is decoded as if VAD were off. A track below
that level is skipped.

## Examples

```cpp
#include <muninn/transcriber.h>

muninn::TranscribeOptions options;          // Auto by default
options.silero_model_path = "models/silero_vad.onnx";  // lets Auto use Silero

// Tell Auto what each track is (Loki Studio does this from track titles)
options.track_hints[0] = muninn::TrackType::GameAudio;   // no VAD
options.track_hints[1] = muninn::TrackType::Microphone;  // Silero

// Or pick one detector for every track
options.vad_type = muninn::VADType::Silero;
options.vad_threshold = 0.35f;              // Silero speech probability

options.vad_type = muninn::VADType::Energy;
options.vad_threshold = 0.01f;              // lower RMS level = keeps quieter speech

options.vad_type = muninn::VADType::None;   // decode everything
```

## Using the detectors directly

`muninn/vad.h` and `muninn/silero_vad.h` expose the detectors on their own:

```cpp
#include <muninn/vad.h>
#include <muninn/silero_vad.h>

muninn::VAD energy;                         // VADOptions defaults
auto regions = energy.detect_speech(samples, 16000);

muninn::SileroVADOptions so;
so.model_path = "models/silero_vad.onnx";
muninn::SileroVAD silero(so);               // throws if the model can't load
std::vector<muninn::SpeechSegment> speech;
auto speech_only = silero.filter_silence(samples, 16000, speech);

auto c = muninn::analyze_audio_characteristics(samples);
auto pick = muninn::auto_detect_vad_type(samples, track_id, total_tracks,
                                         muninn::TrackType::Unknown);
```

Note that `VADOptions` and `SileroVADOptions` have their own defaults
(`speech_pad_ms` 100 and 300, `min_silence_duration_ms` 500 and 100, Silero
`threshold` 0.5); the transcriber overrides them from `TranscribeOptions` as
listed above.

A `VAD` or `SileroVAD` object must not be used from two threads at once. The
transcriber creates its own for each track.

## Troubleshooting

- **Speech is cut:** lower `vad_threshold` for Energy, set a Silero
  probability such as 0.2, raise `vad_speech_pad_ms`, or use `None`.
- **Too much silence is decoded:** raise the Energy threshold, or use Silero
  on clean microphone tracks.
- **Silero isn't used:** check the log for the fallback warning. Usually
  `silero_model_path` is empty or wrong.
