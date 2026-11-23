# Muninn Faster-Whisper

A C++ port of [faster-whisper](https://github.com/SYSTRAN/faster-whisper): Whisper speech-to-text on [CTranslate2](https://github.com/OpenNMT/CTranslate2), with audio decoding, voice activity detection, word timestamps, a streaming API, NLLB translation and subtitle export. Windows x64, MSVC.

[![CI](https://github.com/Deverydoo/Muninn-Faster-Whisper/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/Deverydoo/Muninn-Faster-Whisper/actions/workflows/ci.yml)
[![License: BSL 1.1](https://img.shields.io/badge/License-BSL%201.1-yellow.svg)](LICENSE)

Version 0.6.0. Muninn is the transcription engine of [Loki Studio](https://www.lokistudio.ai); its name is one of Odin's ravens.

## Features

- **Transcription** of any file FFmpeg can read. Every audio track is transcribed separately (`Segment::track_id`) and keeps its place on the video's timeline.
- **faster-whisper's decoding**: 30 s windows with its seek and context rules, temperature fallback with `best_of` sampling, the initial prompt tokenized with the model's BPE, and the same quality thresholds. The mel features match faster-whisper's feature extractor. `tests/accuracy` compares transcripts and word timings against faster-whisper.
- **Word timestamps** from CTranslate2's alignment, with faster-whisper's word splitting (also for scripts without spaces) and timing rules.
- **Voice activity detection**: Energy (no model) or Silero (ONNX), picked per track by `VADType::Auto` from track hints and the audio. Timestamps are mapped back to the original timeline.
- **Failures are reported**: windows and tracks that fail are counted in `TranscribeResult`, and `transcribe()` throws when nothing succeeded. `cancel()` from any thread returns what was done so far.
- **Language detection** over several windows, and an error for a language the model doesn't know.
- **GPU or CPU**: CUDA 13 on RTX 20 series and newer; the CPU works through CTranslate2's Ruy backend (much slower).
- **Streaming** (`StreamingTranscriber`): push audio at any rate and channel count from any thread, get segments with word timestamps by callback or polling. Language detection on the first speech, translate-to-English with multilingual models.
- **Translation** (`Translator`): NLLB-200 through CTranslate2 and SentencePiece. A failed item comes back as `""`, never as the untranslated text.
- **Subtitles** (`SubtitleExporter`): SRT, WebVTT (with optional word timestamp tags) and ASS; line splitting by characters for many languages, never dropping text, no overlapping cues. A metadata JSON writer and reader.
- **Speaker diarization** (`Diarizer`): pyannote embeddings (ONNX, CPU) and average-linkage clustering.

Not implemented: TEN and WebRTC VAD (`VADType::TEN` runs Silero, `WebRTC` runs Energy), hotwords, `Transcriber::get_audio_info()` (it throws; use `AudioExtractor`).

## What ships

The release zip (`Muninn-0.6.0-win-x64-cuda13.zip`) has:

- `muninn.dll` and `muninn.lib`, and the public headers
- `ctranslate2.dll` (CTranslate2 4.9.1 with Muninn's patch, CUDA 13.2)
- `onnxruntime.dll` (ONNX Runtime 1.31.0, CPU) for Silero VAD and diarization
- FFmpeg 9.0.2's `avformat`, `avcodec`, `avutil` and `swresample` DLLs (LGPL build; you can replace them)
- `silero_vad.onnx`

For the GPU you also need the CUDA 13 runtime and cuDNN 9 DLLs (not included), an RTX 20 series or newer GPU and driver 570+. Whisper models aren't included: use a CTranslate2 conversion such as `Systran/faster-whisper-large-v3`.

`packaging/README_BINARIES.txt` lists the files and what a program using the DLL must match. Muninn passes C++ standard-library types across the DLL boundary, so your program must use MSVC 2022 17.10+, C++17, Release with /MD, and Visual C++ runtime 14.40+. Call `muninn::check_version()` (`<muninn/version.h>`) once at startup: it throws if the DLL isn't from the same release as your headers.

## Building

See [SETUP.md](SETUP.md). In short:

- MSVC 2022, /MD, CMake 3.20+, vcpkg as the toolchain (`C:/vcpkg` unless you pass `CMAKE_TOOLCHAIN_FILE`), CUDA 13.2 for the GPU build
- CTranslate2 4.9.1 with the local patch: `scripts/build_ctranslate2.ps1`, or the steps in [patches/README.md](patches/README.md)
- SentencePiece (static) for the translator
- ONNX Runtime 1.31.0 for Silero and diarization
- FFmpeg 9 as shared DLLs, through `FFMPEG_DIR`

Only Windows x64 is supported. There is no CMake package to `find_package`; add `include/` and link `muninn.lib`.

## Transcribing a file

```cpp
#include <muninn/transcriber.h>
#include <muninn/version.h>
#include <cstdio>

int main() {
    muninn::check_version();
    muninn::Transcriber transcriber("models/faster-whisper-large-v3-turbo", "cuda", "float16");

    muninn::TranscribeOptions options;      // language "auto", VAD Auto, beam 5
    options.word_timestamps = true;

    auto result = transcriber.transcribe("video.mp4", options);
    for (const auto& seg : result.segments) {
        std::printf("[track %d] [%.2f -> %.2f] %s\n", seg.track_id, seg.start, seg.end, seg.text.c_str());
    }
    if (result.failed_windows || result.failed_tracks) {
        std::printf("Some audio failed: %s\n", result.error.c_str());
    }
}
```

`transcribe()` also takes 16 kHz mono samples (`std::vector<float>`) and an optional progress callback that can cancel by returning false. `examples/basic_transcription.cpp` is a complete program.

Defaults worth knowing (`include/muninn/types.h`):

| Option | Default | |
|---|---|---|
| `language` | `"auto"` | |
| `beam_size`, `patience`, `best_of` | 5, 1.0, 5 | |
| `temperature_fallback` | 0.0, 0.2 ... 1.0 | |
| `vad_type`, `vad_filter` | `VADType::Auto`, true | Silero needs `silero_model_path` |
| `vad_threshold` | 0.02 | Energy's RMS floor; Silero uses it only above 0.1 (else 0.25) |
| `vad_min_silence_duration_ms`, `vad_speech_pad_ms` | 500, 300 | |
| `no_speech_threshold`, `log_prob_threshold`, `compression_ratio_threshold` | 0.4, -1.0, 2.4 | |
| `hallucination_silence_threshold` | 0 (off) | fraction of a segment that must be speech |
| `word_timestamps` | false | |
| `sequential` | true | false = the older batched path (`batch_size` 4) |
| `condition_on_previous` | true | |

`ModelOptions` (for the other constructor) defaults to device `Auto` and `Float16`; on the CPU float16 becomes float32. `intra_threads` 0 uses about one thread per physical core.

## Streaming

```cpp
#include <muninn/streaming_transcriber.h>

muninn::StreamingTranscriber stream("models/faster-whisper-large-v3-turbo", "auto");
muninn::StreamingOptions options;           // 2 s windows, 0.5 s overlap, energy VAD
stream.start(options, [](const muninn::Segment& seg) {
    std::printf("[%.2f] %s\n", seg.start, seg.text.c_str());
    return true;
});
stream.push_audio(samples, frame_count * 2, 48000, 2);   // any thread, any rate
auto segments = stream.stop();              // decodes what's left
```

See [docs/STREAMING_API.md](docs/STREAMING_API.md), and `examples/karaoke_captions.cpp` and `examples/translation_streaming.cpp`.

## Translation

```cpp
#include <muninn/translator.h>

muninn::Translator translator("models/nllb-200-distilled-600M", "cuda");
auto batch = translator.translate_batch_result(texts, "en", "es");
// batch.texts[i] is "" for an item that failed; batch.cancelled, batch.failed_count, batch.error
```

See [docs/TRANSLATION.md](docs/TRANSLATION.md) and `examples/translation_example.cpp`.

## Subtitles

```cpp
#include <muninn/subtitle_export.h>

muninn::SubtitleExporter exporter;
exporter.export_srt(result.segments, "video.mp4");    // writes video.srt

muninn::SubtitleExportOptions vtt;
vtt.format = muninn::SubtitleFormat::VTT;
vtt.vtt_include_word_timestamps = true;
exporter.export_subtitles(result.segments, "video.mp4", vtt);   // writes video.vtt
```

See [docs/SUBTITLE_EXPORT.md](docs/SUBTITLE_EXPORT.md) and `examples/subtitle_export.cpp`.

## Documentation

| | |
|---|---|
| [SETUP.md](SETUP.md) | Building |
| [docs/INTEGRATION.md](docs/INTEGRATION.md) | Using the DLL in an application |
| [docs/VAD_API.md](docs/VAD_API.md), [docs/VAD_AUTO_DETECTION.md](docs/VAD_AUTO_DETECTION.md) | Voice activity detection |
| [docs/CANCELLATION_API.md](docs/CANCELLATION_API.md) | Cancelling transcription and translation |
| [docs/STREAMING_API.md](docs/STREAMING_API.md), [docs/REALTIME_TRANSLATION.md](docs/REALTIME_TRANSLATION.md) | Streaming |
| [docs/TRANSLATION.md](docs/TRANSLATION.md) | NLLB translation |
| [docs/SUBTITLE_EXPORT.md](docs/SUBTITLE_EXPORT.md), [docs/WORD_STYLING.md](docs/WORD_STYLING.md), [docs/OBS_KARAOKE_CAPTIONS.md](docs/OBS_KARAOKE_CAPTIONS.md) | Subtitles and captions |
| [docs/SPEAKER_DIARIZATION.md](docs/SPEAKER_DIARIZATION.md), [docs/DIARIZATION_SETUP.md](docs/DIARIZATION_SETUP.md) | Diarization |
| [docs/CTRANSLATE2_CHANGES.md](docs/CTRANSLATE2_CHANGES.md) | Muninn's CTranslate2 patch |
| [docs/CODE_AUDIT_2026-11.md](docs/CODE_AUDIT_2026-11.md) | The November 2026 audit behind 0.6.0 |

## License

Muninn is under the [Business Source License 1.1](LICENSE), with an additional use grant: you may use it for any non-commercial purpose (personal projects, research, education). Commercial use needs a license from the licensor (contact details in [LICENSE](LICENSE)). Each release changes to the MIT License four years after it is published. The third-party parts keep their own licenses (`packaging/THIRD_PARTY_NOTICES.txt`).

## Acknowledgments

- [faster-whisper](https://github.com/SYSTRAN/faster-whisper) by SYSTRAN, which Muninn follows
- [CTranslate2](https://github.com/OpenNMT/CTranslate2) by OpenNMT
- [OpenAI Whisper](https://github.com/openai/whisper)
- [Silero VAD](https://github.com/snakers4/silero-vad), [NLLB-200](https://github.com/facebookresearch/fairseq/tree/nllb), [pyannote](https://github.com/pyannote/pyannote-audio), [FFmpeg](https://ffmpeg.org/)

Issues: [GitHub Issues](https://github.com/Deverydoo/Muninn-Faster-Whisper/issues)
