# Muninn Faster-Whisper

**High-performance Pure C++ Whisper transcription library - Production-ready alternative to Python faster-whisper**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![CTranslate2](https://img.shields.io/badge/CTranslate2-Optimized-green.svg)](https://github.com/OpenNMT/CTranslate2)

> **Muninn** - In Norse mythology, Muninn ("memory" or "mind") is one of Odin's ravens that flies across the world gathering information.

## Why Muninn?

**A pure C++ faster-whisper has been one of the most requested features in the transcription community.** Python's faster-whisper is excellent, but many applications need:

- Native C++ integration without Python dependencies
- Lower latency for real-time applications
- Smaller deployment footprint
- Direct integration with C++ audio/video pipelines

Muninn delivers all of this while maintaining **feature parity with Python faster-whisper**.

## Features

### Core Transcription
- **Automatic Language Detection** - Detects 99+ languages automatically
- **Multi-track Audio Support** - Process video files with multiple audio tracks separately
- **Word-level Timestamps** - Precise timing for each word (optional)
- **Beam Search Decoding** - High-quality output with configurable beam size
- **Temperature Fallback** - Automatic retry with higher temperatures on difficult segments

### Voice Activity Detection (VAD)
- **Silero VAD** - Neural network-based speech detection (most accurate)
- **Energy VAD** - Fast energy-based fallback (no dependencies)
- **Automatic Silence Skipping** - Skip non-speech regions for faster processing
- **VAD-aware Timestamp Remapping** - Accurate timestamps on original timeline

### Quality & Hallucination Filtering
- **Compression Ratio Checks** - Detect repetitive hallucinations
- **Log Probability Thresholds** - Filter low-confidence outputs
- **No-speech Probability Filtering** - Skip segments with no actual speech
- **Repetition Detection** - Catch phrase/word repetition patterns
- **Cross-chunk Hallucination Detection** - Detect repeated phrases across chunks
- **Silence Region Filtering** - Filter segments that fall in silent regions

### Performance
- **CUDA GPU Acceleration** - Full NVIDIA GPU utilization
- **Batched Inference** - Process multiple chunks in parallel on GPU
- **Optimized CTranslate2** - Custom-built for maximum speed
- **2-4x Faster than Python** - No interpreter overhead
- **~40% Lower Memory** - Efficient C++ memory management

### Advanced Options
- **Clip Timestamps** - Process only specific time ranges
- **Initial Prompt** - Condition model on domain-specific vocabulary
- **Condition on Previous Text** - Better consistency across segments
- **Token Suppression** - Control which tokens can be generated
- **Skip Tracks** - Skip specific audio tracks in multi-track files

## Quick Start

```cpp
#include <muninn/transcriber.h>

using namespace muninn;

int main() {
    // Initialize transcriber with GPU
    Transcriber transcriber("models/faster-whisper-large-v3-turbo",
                            "cuda", "float16");

    // Configure transcription
    TranscribeOptions options;
    options.language = "auto";           // Auto-detect language
    options.vad_filter = true;           // Enable VAD
    options.vad_type = VADType::Silero;  // Use neural VAD
    options.silero_model_path = "models/silero_vad.onnx";
    options.beam_size = 5;
    options.word_timestamps = false;     // Set true for word-level timing

    // Transcribe audio/video file
    auto result = transcriber.transcribe("video.mp4", options);

    // Print results
    std::cout << "Language: " << result.language << std::endl;
    std::cout << "Duration: " << result.duration << "s" << std::endl;

    for (const auto& segment : result.segments) {
        printf("[Track %d] [%.2f -> %.2f] %s\n",
               segment.track_id, segment.start, segment.end,
               segment.text.c_str());
    }

    return 0;
}
```

## Feature Parity with faster-whisper

| Feature | faster-whisper (Python) | Muninn (C++) |
|---------|------------------------|--------------|
| Silero VAD | ✅ | ✅ |
| Beam Search | ✅ | ✅ |
| Temperature Fallback | ✅ | ✅ |
| Condition on Previous | ✅ | ✅ |
| Language Detection | ✅ | ✅ |
| Word Timestamps | ✅ | ✅ |
| Hallucination Filtering | ✅ | ✅ |
| Batched Processing | ✅ | ✅ |
| Initial Prompt | ✅ | ✅ |
| Clip Timestamps | ✅ | ✅ |
| Multi-track Audio | ❌ | ✅ |
| No Python Required | ❌ | ✅ |

## Performance Benchmarks

Tested on RTX 4090, large-v3-turbo model:

| Video Length | Python faster-whisper | Muninn C++ | Speedup |
|-------------|----------------------|------------|---------|
| 5 minutes   | 45s                  | 18s        | **2.5x**|
| 30 minutes  | 4m 30s               | 1m 50s     | **2.5x**|
| 60 minutes  | 9m 10s               | 3m 45s     | **2.4x**|

Memory usage: **~40% lower** than Python implementation

## Installation

### Requirements
- C++17 compiler (MSVC 2022, GCC 9+, Clang 10+)
- CMake 3.20+
- CUDA Toolkit 11.8+ (for GPU acceleration)
- CTranslate2 (included or provide your own build)
- Heimdall 2.0+ (required, for audio extraction - included in `third_party/heimdall/`)
- ONNX Runtime (optional, for Silero VAD)

### Build from Source

```bash
git clone --recursive https://github.com/nordiq-ai/muninn-faster-whisper.git
cd muninn-faster-whisper

# Configure with all features
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DWITH_CUDA=ON \
      -DWITH_SILERO_VAD=ON \
      -DWITH_HEIMDALL=ON

# Build
cmake --build build --config Release -j

# Install
cmake --install build --prefix /usr/local
```

### CMake Integration

```cmake
find_package(Muninn REQUIRED)
target_link_libraries(your_app PRIVATE Muninn::faster-whisper)
```

## API Reference

### Transcriber Class

```cpp
class Transcriber {
public:
    // Constructor with ModelOptions (recommended)
    explicit Transcriber(const ModelOptions& options);

    // Constructor with strings (convenience)
    Transcriber(const std::string& model_path,
                const std::string& device = "cuda",
                const std::string& compute_type = "float16");

    // Transcribe from file (supports MP3, WAV, M4A, FLAC, MP4, MOV, etc.)
    TranscribeResult transcribe(
        const std::string& audio_path,
        const TranscribeOptions& options = {},
        ProgressCallback progress_callback = nullptr
    );

    // Transcribe from memory
    TranscribeResult transcribe(
        const std::vector<float>& audio_samples,
        int sample_rate = 16000,
        const TranscribeOptions& options = {},
        int track_id = 0
    );

    // Get audio file info without transcribing
    static AudioInfo get_audio_info(const std::string& audio_path);

    // Get model information
    ModelInfo get_model_info() const;
};
```

### TranscribeOptions

```cpp
struct TranscribeOptions {
    // Language and Task
    std::string language = "auto";         // "en", "es", "auto", etc.
    std::string task = "transcribe";       // "transcribe" or "translate"

    // Decoding Parameters
    int beam_size = 5;                     // Beam search width (1-10)
    float temperature = 0.0f;              // Sampling temperature (0 = greedy)
    std::vector<float> temperature_fallback = {0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f};
    float patience = 1.0f;                 // Beam search patience
    float length_penalty = 1.0f;           // Length penalty factor
    float repetition_penalty = 1.0f;       // Repetition penalty
    int no_repeat_ngram_size = 0;          // Prevent n-gram repetitions

    // Voice Activity Detection (VAD)
    VADType vad_type = VADType::Energy;    // Energy, Silero, or None
    bool vad_filter = true;                // Enable VAD filtering
    float vad_threshold = 0.02f;           // VAD threshold
    int vad_min_speech_duration_ms = 250;  // Minimum speech duration
    int vad_max_speech_duration_s = 30;    // Maximum segment duration
    int vad_min_silence_duration_ms = 500; // Minimum silence for split
    int vad_speech_pad_ms = 100;           // Padding around speech
    std::string silero_model_path;         // Path to silero_vad.onnx

    // Hallucination Filtering
    float compression_ratio_threshold = 2.4f;
    float log_prob_threshold = -1.0f;
    float no_speech_threshold = 0.4f;
    float hallucination_silence_threshold = 0.0f;  // 0 = disabled

    // Timestamps
    bool word_timestamps = false;          // Extract word-level timing
    float clip_start = 0.0f;               // Start time for clip
    float clip_end = -1.0f;                // End time (-1 = full audio)

    // Token Suppression
    bool suppress_blank = true;
    std::vector<int> suppress_tokens = {-1};

    // Multi-Track Processing
    std::set<int> skip_tracks;             // Track indices to skip
    bool skip_silent_tracks = true;        // Auto-skip silent tracks

    // Performance Tuning
    int batch_size = 4;                    // Batch size for GPU
    int max_length = 448;                  // Max tokens per segment

    // Prompt / Context
    std::string initial_prompt;            // Domain-specific vocabulary
    std::vector<std::string> hotwords;     // Words to boost (planned)
    bool condition_on_previous = true;     // Use previous text as context
    float prompt_reset_on_temperature = 0.5f;  // Reset context threshold
};
```

### TranscribeResult

```cpp
struct Word {
    float start;             // Start time in seconds
    float end;               // End time in seconds
    std::string word;        // The word text
    float probability;       // Confidence score
};

struct Segment {
    int id;                  // Segment index
    int track_id;            // Audio track index
    float start;             // Start time in seconds
    float end;               // End time in seconds
    std::string text;        // Transcribed text
    std::vector<Word> words; // Word timestamps (if enabled)
    float temperature;       // Temperature used
    float avg_logprob;       // Average log probability
    float compression_ratio; // Text compression ratio
    float no_speech_prob;    // No speech probability
};

struct TranscribeResult {
    std::vector<Segment> segments;
    std::string language;
    float language_probability;
    float duration;

    // Range-based for loop support
    auto begin() { return segments.begin(); }
    auto end() { return segments.end(); }
};
```

## Examples

### Basic Transcription

```cpp
#include <muninn/transcriber.h>

int main() {
    muninn::Transcriber transcriber("models/whisper-large-v3-turbo");
    auto result = transcriber.transcribe("audio.mp3");

    for (const auto& seg : result) {
        std::cout << "[" << seg.start << "s] " << seg.text << std::endl;
    }
}
```

### With Silero VAD

```cpp
muninn::TranscribeOptions options;
options.vad_filter = true;
options.vad_type = muninn::VADType::Silero;
options.silero_model_path = "models/silero_vad.onnx";

auto result = transcriber.transcribe("audio.mp3", options);
```

### Word-level Timestamps

```cpp
muninn::TranscribeOptions options;
options.word_timestamps = true;

auto result = transcriber.transcribe("audio.mp3", options);

for (const auto& seg : result) {
    for (const auto& word : seg.words) {
        printf("[%.2fs] %s\n", word.start, word.word.c_str());
    }
}
```

### Process Specific Time Range

```cpp
muninn::TranscribeOptions options;
options.clip_start = 60.0f;   // Start at 1 minute
options.clip_end = 120.0f;    // End at 2 minutes

auto result = transcriber.transcribe("long_video.mp4", options);
```

### Multi-track Video

```cpp
// Get audio info first
auto info = muninn::Transcriber::get_audio_info("video.mkv");
std::cout << "Tracks: " << info.num_tracks << std::endl;

// Transcribe all tracks
muninn::TranscribeOptions options;
// options.skip_tracks = {0};  // Skip track 0 if needed

auto result = transcriber.transcribe("video.mkv", options);

// Results are grouped by track_id
for (const auto& seg : result) {
    std::cout << "[Track " << seg.track_id << "] " << seg.text << std::endl;
}
```

### Progress Callback (GUI Integration)

```cpp
auto result = transcriber.transcribe("video.mp4", options,
    [](int track, int total_tracks, float progress, const std::string& msg) {
        printf("\rTrack %d/%d: %.1f%% - %s",
               track + 1, total_tracks, progress * 100, msg.c_str());
        return true;  // Return false to cancel
    });
```

## Dependencies

| Dependency | Required | Purpose |
|------------|----------|---------|
| CTranslate2 | Yes | Whisper inference engine |
| Heimdall 2.0 | Yes | Audio/video file loading (380x real-time extraction) |
| CUDA | Recommended | GPU acceleration |
| ONNX Runtime | Optional | Silero VAD support |

### Heimdall Audio Engine

Muninn uses **Heimdall 2.0** for audio extraction. Heimdall's v2 API provides:

- **16kHz mono float32 output** - Optimized for CTranslate2/Whisper requirements
- **Multi-track support** - Extract each audio track separately (no mixing)
- **380x real-time** performance (4-min video extracted in 0.6s)
- **Static linking** - Standalone DLL with no external dependencies

Heimdall is included in `third_party/heimdall/`. See [Heimdall README](third_party/heimdall/README.md) for build instructions.

## Project Status

**Current Version**: 0.5.0-alpha

### Implemented
- [x] CTranslate2 Whisper integration
- [x] Mel-spectrogram conversion (128-bin)
- [x] Silero VAD (neural)
- [x] Energy VAD (fallback)
- [x] VAD timestamp remapping
- [x] Temperature fallback strategy
- [x] Condition on previous text
- [x] Language auto-detection
- [x] Hallucination filtering (comprehensive)
- [x] Word-level timestamps
- [x] Batched GPU inference
- [x] Multi-track audio support
- [x] Clip timestamps
- [x] Initial prompt support
- [x] Token suppression options
- [x] Progress callbacks

### Planned
- [ ] Hotwords/prefix support (logit bias)
- [ ] Streaming transcription
- [ ] Python bindings
- [ ] Pre-built binaries

## Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## License

MIT License - see [LICENSE](LICENSE) for details.

## Acknowledgments

- Built on [CTranslate2](https://github.com/OpenNMT/CTranslate2) by OpenNMT
- Inspired by [faster-whisper](https://github.com/SYSTRAN/faster-whisper)
- Based on [OpenAI Whisper](https://github.com/openai/whisper)
- Silero VAD from [snakers4/silero-vad](https://github.com/snakers4/silero-vad)

## Support

- **Issues**: [GitHub Issues](https://github.com/nordiq-ai/muninn-faster-whisper/issues)
- **Discussions**: [GitHub Discussions](https://github.com/nordiq-ai/muninn-faster-whisper/discussions)

---

**Made with care for the C++ community**
