# Heimdall 2.0 - The Vigilant Guardian

<p align="center">
  <em>Audio Extraction Engine + Waveform Generator</em><br>
  <em>Named after the Norse god with incredible hearing who guards Bifröst</em>
</p>

## 🎵 Overview

Heimdall is a high-performance C++ module for **audio extraction** and **waveform visualization**. Version 2.0 adds unified audio extraction for transcription engines (CTranslate2/Whisper) while maintaining the original waveform generation capabilities.

**Performance**:
- Audio extraction: **380x real-time** (4-min video extracted in 0.6s)
- Waveform generation: **10-20x faster** than pure Python

## ✨ Features

- **🎤 Audio Extraction** - Extract audio tracks for transcription (16kHz mono float32)
- **🔊 Multi-Track Preservation** - Each audio track kept separate (no mixing)
- **⚡ SIMD-Optimized** - AVX2/SSE instructions for peak detection
- **🎬 Multi-Track Support** - Process multiple audio streams simultaneously
- **🚀 Hardware Acceleration** - FFmpeg's optimized decoders
- **📊 Configurable Quality** - Full quality for transcription, fast mode for waveforms
- **🐍 Python Integration** - Seamless pybind11 bindings
- **📦 Static Linking** - Standalone DLL with no external dependencies

## 📁 Output Files

| File | Size | Location | Purpose |
|------|------|----------|---------|
| `heimdall.dll` | ~16 MB | `lib/Release/` | C++ integration |
| `heimdall.pyd` | ~17 MB | `Release/` | Python extension |

## 🔧 Build

### Prerequisites

- Visual Studio 2022 with C++ tools
- CMake 3.15+
- Python 3.12 (conda environment)
- pybind11: `pip install pybind11`
- vcpkg with FFmpeg (x64-windows-static)

### Build Commands

```cmd
cd cpp_heimdall_waveform_engine
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

## 📖 Python API

### Audio Extraction (for Transcription)

```python
import heimdall

h = heimdall.Heimdall()

# Extract all audio tracks at 16kHz (Whisper-ready)
tracks = h.extract_audio("video.mp4")
# Returns: {0: [samples...], 1: [samples...], 2: [samples...]}

# Each track is:
# - Mono float32 samples
# - Resampled to 16kHz
# - Full quality (no packet skipping)
# - Ready for CTranslate2/Whisper

# Extract specific streams only
tracks = h.extract_audio("video.mp4", 16000, [0, 2])

# Feed to Whisper
for stream_idx, samples in tracks.items():
    result = whisper_model.transcribe(samples)
```

### Waveform Generation

```python
import heimdall

h = heimdall.Heimdall()

# Get audio metadata
info = h.get_audio_info("video.mp4")
print(f"Duration: {info.duration_ms}ms")
print(f"Streams: {info.stream_count}")

# Generate waveform for single track
peaks = h.generate_peaks(
    audio_file="video.mp4",
    stream_index=0,
    width=800,
    height=60,
    samples_per_pixel=512,
    normalize=True
)
# peaks: [min0, max0, min1, max1, ...] (length = width * 2)

# Batch process multiple streams (single file read)
all_peaks = h.generate_batch(
    audio_file="video.mp4",
    stream_indices=[0, 1, 2],
    width=800,
    height=60,
    target_sample_rate=48000,
    packet_quality=10  # 10% for speed
)
# Returns: {0: [peaks...], 1: [peaks...], 2: [peaks...]}
```

## 📚 API Reference

### `heimdall.Heimdall` Class

#### `extract_audio(audio_file, sample_rate=16000, stream_indices=[], quality=100)`

Extract audio from streams for transcription or processing.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `audio_file` | str | required | Path to audio/video file |
| `sample_rate` | int | 16000 | Target sample rate (16000 for Whisper) |
| `stream_indices` | list[int] | [] | Streams to extract (empty = all) |
| `quality` | int | 100 | Decode quality 1-100 (100=full, 10=fast) |

**Returns**: `dict[int, list[float]]` - Map of stream_index → mono float32 samples

**Examples**:
```python
# Transcription (full quality, 16kHz, all streams)
tracks = h.extract_audio("video.mp4")

# Specific streams only
tracks = h.extract_audio("video.mp4", 16000, [0, 2])

# Fast waveform data (48kHz, 10% quality)
tracks = h.extract_audio("video.mp4", 48000, [], 10)
```

---

#### `generate_peaks(audio_file, stream_index=0, width=800, height=60, samples_per_pixel=512, normalize=True)`

Generate waveform peaks for visualization.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `audio_file` | str | required | Path to audio/video file |
| `stream_index` | int | 0 | Audio stream index |
| `width` | int | 800 | Output width in pixels |
| `height` | int | 60 | Output height (reserved) |
| `samples_per_pixel` | int | 512 | Downsampling factor |
| `normalize` | bool | True | Normalize to 0.0-1.0 |

**Returns**: `list[float]` - Peak values (length = width × 2 for min/max pairs)

---

#### `generate_batch(audio_file, stream_indices, width=800, height=60, target_sample_rate=48000, packet_quality=10)`

Generate waveforms for multiple streams in one pass.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `audio_file` | str | required | Path to audio/video file |
| `stream_indices` | list[int] | required | Streams to process |
| `width` | int | 800 | Output width in pixels |
| `height` | int | 60 | Output height (reserved) |
| `target_sample_rate` | int | 48000 | Sample rate for extraction |
| `packet_quality` | int | 10 | Quality % (10=fast, 100=full) |

**Returns**: `dict[int, list[float]]` - Map of stream_index → peak values

---

#### `get_audio_info(audio_file)`

Get audio file metadata (fast, no decoding).

**Returns**: `AudioInfo` object:
- `duration_ms` (int): Duration in milliseconds
- `sample_rate` (int): Sample rate in Hz
- `channels` (int): Channels per stream
- `stream_count` (int): Total audio streams

## 🏆 Performance

Test file: 4-minute video, 3 audio tracks

| Operation | Time | Notes |
|-----------|------|-------|
| Full extraction (16kHz) | 0.635s | 380x real-time |
| Fast waveform (48kHz, 10%) | 0.281s | 2.3x faster |
| File info | <1ms | Metadata only |

## 🛠️ Architecture

```
Heimdall 2.0
├── extract_audio()          # Core extraction (unified)
│   └── AudioDecoder
│       ├── FFmpeg decoding
│       ├── Resampling (16-48kHz)
│       └── Quality control (packet skip)
│
├── generate_batch()         # Waveform generation
│   ├── extract_audio()      # Uses core extraction
│   └── PeakDetector
│       └── AVX2/SSE SIMD
│
└── generate_peaks()         # Single stream waveform
    └── (legacy, uses full decode)
```

## 🔌 Integration with CTranslate2/Whisper

```python
import heimdall
import numpy as np
from faster_whisper import WhisperModel

# Extract audio
h = heimdall.Heimdall()
tracks = h.extract_audio("video.mp4", 16000)

# Convert to numpy (Whisper expects numpy array)
model = WhisperModel("large-v3", device="cuda")

for stream_idx, samples in tracks.items():
    audio_np = np.array(samples, dtype=np.float32)
    segments, info = model.transcribe(audio_np)

    print(f"Stream {stream_idx}:")
    for segment in segments:
        print(f"  [{segment.start:.2f}s -> {segment.end:.2f}s] {segment.text}")
```

## 🐛 Troubleshooting

### "Unable to import heimdall"
- Check `heimdall.pyd` exists in Python path
- Verify Python 3.12
- Static build has no DLL dependencies

### Build Errors
- Verify Visual Studio 2022 installed
- Check CMake 3.15+
- Ensure vcpkg FFmpeg: `vcpkg install ffmpeg:x64-windows-static`

## 📜 Version History

| Version | Changes |
|---------|---------|
| 2.0.0 | Added `extract_audio()` for transcription, unified extraction core, static linking |
| 1.0.0 | Initial waveform generation |

## 📜 License

Part of Loki Studio - Copyright (c) 2025 NordIQ AI

---

**"Ever-vigilant, the guardian watches."** 🛡️
