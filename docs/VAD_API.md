# Voice Activity Detection (VAD) API

## Overview

Muninn provides intelligent Voice Activity Detection to filter silence and improve transcription quality. The VAD system automatically selects the best algorithm based on audio characteristics.

## API Options

### `VADType` Enum

| Value    | Description | Best For |
|----------|-------------|----------|
| **Auto** (default) | Smart auto-detection per track | Multi-track recordings, gaming streams, mixed content |
| **None** | No VAD - process all audio | Pre-cleaned audio, or when VAD causes issues |
| **Energy** | RMS energy-based detection | Game audio with music, mixed content, noisy environments |
| **Silero** | Neural VAD (ONNX Runtime) | Clean speech, podcasts, studio recordings, noise-gated mics |

## Auto-Detection Heuristics

When `VADType::Auto` is selected (default), Muninn analyzes each audio track and selects the optimal VAD:

### 1. Multi-track Track 0 → Energy VAD
- **Reason:** Desktop/game audio typically contains music + speech
- **Example:** OBS recordings with separate game audio and microphone tracks

### 2. Very Clean Speech → Silero VAD
- **Condition:** Noise floor < 0.0001, Dynamic range > 0.01
- **Reason:** Noise gates and studio mics produce extremely low noise floors
- **Example:** Gaming commentary with noise gate filters

### 3. Clean Speech → Silero VAD
- **Condition:** Noise floor < 0.01, Dynamic range > 0.15
- **Reason:** High signal-to-noise ratio benefits from neural precision
- **Example:** Podcasts, interviews, clean recordings

### 4. Mixed/Noisy Content → Energy VAD
- **Default:** All other cases
- **Reason:** Energy VAD is robust to background noise and music
- **Example:** Field recordings, noisy environments

## Usage Examples

### C++ API

```cpp
#include <muninn/transcriber.h>

// Auto-detection (recommended)
muninn::TranscribeOptions options;
options.vad_type = muninn::VADType::Auto;  // Default
transcriber.transcribe(audio_samples, 16000, options);

// Force Energy VAD
options.vad_type = muninn::VADType::Energy;

// Force Silero VAD (requires ONNX Runtime)
options.vad_type = muninn::VADType::Silero;
options.silero_model_path = "path/to/silero_vad.onnx";

// Disable VAD
options.vad_type = muninn::VADType::None;
```

### Qt GUI Integration

```cpp
// Dropdown options
QComboBox* vadCombo = new QComboBox();
vadCombo->addItem("Auto (Recommended)", static_cast<int>(muninn::VADType::Auto));
vadCombo->addItem("Off", static_cast<int>(muninn::VADType::None));
vadCombo->addItem("Energy VAD", static_cast<int>(muninn::VADType::Energy));
vadCombo->addItem("Silero VAD", static_cast<int>(muninn::VADType::Silero));
vadCombo->setCurrentIndex(0);  // Auto by default

// Set option
muninn::TranscribeOptions options;
options.vad_type = static_cast<muninn::VADType>(vadCombo->currentData().toInt());
```

## Performance Characteristics

| VAD Type | Speed | Accuracy | Dependencies | Best Use Case |
|----------|-------|----------|--------------|---------------|
| Auto | Mixed | Best overall | Silero ONNX (optional) | Multi-track, gaming streams |
| None | Fastest | N/A | None | Pre-cleaned audio |
| Energy | ~1ms/chunk | Good | None | Mixed audio, music + speech |
| Silero | ~0.5ms/chunk | Excellent | ONNX Runtime (~2MB) | Clean speech only |

## Test Results

Tested on 243-second gaming video with OBS multi-track audio:

### Track 0: Game Audio (Desktop)
- **Auto-detected:** Energy VAD
- **Result:** 21 segments, 173.7s captured (28% filtered)
- **Quality:** Captured all game dialogue including quiet NPCs

### Track 1: Microphone (Noise Gate)
- **Auto-detected:** Silero VAD (noise floor: 0.000035)
- **Result:** 53 segments, 71.3s captured (70% silence filtered)
- **Quality:** Accurate transcription including "Shift M", license plate "ZHW 327"

### Track 2: Silent
- **Auto-detected:** Energy VAD
- **Result:** 0 segments (empty track)

## Troubleshooting

### Issue: VAD filters too aggressively
**Solution:** Use `VADType::None` or adjust `vad_threshold` (lower = less aggressive)

```cpp
options.vad_type = muninn::VADType::None;  // Disable VAD
// Or
options.vad_threshold = 0.01f;  // Lower threshold (default: 0.02)
```

### Issue: Silero VAD missing speech in noisy audio
**Solution:** Use `VADType::Energy` or `VADType::Auto` (will auto-select Energy)

```cpp
options.vad_type = muninn::VADType::Energy;  // Force Energy for noisy content
```

### Issue: Energy VAD not filtering enough silence
**Solution:** Increase `vad_threshold` or use `VADType::Silero` for clean speech

```cpp
options.vad_threshold = 0.05f;  // Higher threshold = more aggressive
```

## Advanced: Manual Analysis

You can manually analyze audio characteristics before transcription:

```cpp
#include <muninn/vad.h>

// Analyze audio
auto characteristics = muninn::analyze_audio_characteristics(audio_samples);

std::cout << "Noise floor: " << characteristics.noise_floor << "\n";
std::cout << "Speech level: " << characteristics.speech_level << "\n";
std::cout << "Dynamic range: " << characteristics.dynamic_range << "\n";

// Get recommendation
auto vad_type = muninn::auto_detect_vad_type(audio_samples, track_id, total_tracks);
```

## See Also

- [Streaming API](STREAMING_API.md) - Real-time transcription with VAD
- [Multi-track Audio](INTEGRATION.md#multi-track-audio) - Working with OBS recordings
- [Performance Tuning](../README.md#performance) - Optimization guide
