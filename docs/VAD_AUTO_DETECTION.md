# VAD auto-detection

With `vad_type = VADType::Auto` (the default), `Transcriber` picks a detector
for each track separately. The rules are in `auto_detect_vad_type()`
(`src/vad.cpp`), checked in this order:

1. **A track hint, if given** (`TranscribeOptions::track_hints`):
   - `SystemAudio`, `GameAudio`, `MovieAudio`, `Music`: no VAD. Whisper
     segments the audio itself.
   - `Microphone`, `VoiceChat`: Silero.
   - `Unknown` (or no hint): the rules below.
2. **Track 0 of a file with several tracks:** no VAD. In OBS-style
   recordings track 0 is usually the desktop or game mix.
3. **The track's levels**, from `analyze_audio_characteristics()` (every
   1000th sample; noise floor = 10th percentile of the absolute amplitude,
   speech level = 90th percentile, dynamic range = the difference):
   - silent (peak below 0.0001): Energy, which then skips it;
   - noise floor below 0.0001 and dynamic range above 0.01 (a noise-gated
     mic): Silero;
   - noise floor below 0.01 and dynamic range above 0.15 (clean speech):
     Silero;
   - anything else: Energy.

When the rules pick Silero but `silero_model_path` is empty, or Silero can't
be loaded, the track uses Energy instead (logged as a warning). Set
`silero_model_path` whenever you use `Auto`.

With `skip_silent_tracks` on (the default), a silent track is skipped before
any of this runs.

```cpp
muninn::TranscribeOptions options;            // vad_type = Auto
options.silero_model_path = "models/silero_vad.onnx";
options.track_hints = {
    {0, muninn::TrackType::GameAudio},
    {1, muninn::TrackType::Microphone},
};
```

The log shows each decision, for example
`[Auto-VAD] Track 1: Very clean speech (noise gate) → Silero VAD`.

## Why game audio gets no VAD

Silero is trained on speech and drops speech that sits under music and sound
effects. An early test on a 243-second gameplay recording (December 2024)
had forced Silero keep 5.5 s of the game track and miss most of the dialogue.
Energy VAD kept the dialogue then; since March 2026 the game track gets no VAD
at all, so nothing is cut and Whisper's own segmentation handles the silence.

## Streaming

`StreamingTranscriber` doesn't use these rules: there `Auto` means Silero
when `StreamingOptions::vad_model_path` is set, and Energy otherwise.

Option details are in [VAD_API.md](VAD_API.md).
