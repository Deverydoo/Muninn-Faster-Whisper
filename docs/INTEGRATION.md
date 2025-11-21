# Muninn Integration Guide

This guide shows how to integrate Muninn Faster-Whisper into your C++ application.

## What's New: Self-Contained Architecture

Muninn now includes **built-in audio extraction** powered by FFmpeg:

✅ **No external audio DLLs** - Everything you need is in muninn.dll
✅ **Multi-format support** - MP3, WAV, M4A, FLAC, MP4, MOV, MKV, etc.
✅ **Multi-track support** - Process videos with multiple audio tracks
✅ **Fast extraction** - Multi-threaded FFmpeg decoder
✅ **Energy VAD by default** - No ONNX Runtime needed for basic usage

**Deployment is simple**: Just 2 DLLs (muninn.dll + ctranslate2.dll) + model files.

## Quick Start

### 1. Add to Your Project

**Minimum Required Files:**

```
lib/
  muninn.dll          # Main library (includes built-in audio extraction)
  muninn.lib          # Import library (for linking)
  ctranslate2.dll     # CTranslate2 inference engine

include/muninn/
  transcriber.h       # Main API
  types.h             # Options and result types
  export.h            # DLL export macros
  audio_extractor.h   # Audio extraction API (optional)
  mel_spectrogram.h   # MEL conversion (optional)
  vad.h               # VAD interfaces (optional)
```

**Optional Files (for Silero VAD):**

```
lib/
  onnxruntime.dll     # ONNX Runtime (only if using Silero VAD)
```

**Note:** Muninn now has **built-in audio extraction** - no separate Heimdall DLL needed!

### 2. Link in CMake

```cmake
target_link_libraries(your_app PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/muninn.lib
)

target_include_directories(your_app PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
```

### 3. Basic Usage

```cpp
#include <muninn/transcriber.h>
#include <iostream>

int main() {
    // Initialize transcriber
    muninn::Transcriber transcriber(
        "path/to/faster-whisper-large-v3-turbo",  // Model path
        "cuda",                                    // Device: "cuda", "cpu", "auto"
        "float16"                                  // Precision: "float16", "int8", "float32"
    );

    // Transcribe a file
    auto result = transcriber.transcribe("audio.mp3");

    // Print results
    std::cout << "Language: " << result.language << "\n";
    std::cout << "Duration: " << result.duration << "s\n\n";

    for (const auto& segment : result.segments) {
        std::cout << "[" << segment.start << "s] " << segment.text << "\n";
    }

    return 0;
}
```

## API Reference

### Transcriber Class

```cpp
namespace muninn {

class Transcriber {
public:
    // Constructor with ModelOptions (recommended)
    explicit Transcriber(const ModelOptions& options);

    // Convenience constructor
    Transcriber(const std::string& model_path,
                const std::string& device = "cuda",
                const std::string& compute_type = "float16");

    // Transcribe from file (supports MP3, WAV, MP4, MOV, etc.)
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

    // Get model metadata
    ModelInfo get_model_info() const;
};

}
```

### TranscribeOptions

```cpp
muninn::TranscribeOptions options;

// Language
options.language = "auto";           // Auto-detect, or "en", "es", "ja", etc.
options.task = "transcribe";         // "transcribe" or "translate" (to English)

// Decoding
options.beam_size = 5;               // Beam search width (1-10)
options.temperature = 0.0f;          // 0 = greedy, >0 = sampling

// Voice Activity Detection
options.vad_filter = true;           // Enable VAD (uses Energy VAD by default)
options.vad_type = muninn::VADType::Energy;  // Energy (default, fast, no dependencies)
// options.vad_type = muninn::VADType::Silero;  // Optional: Silero (more accurate, requires ONNX)
// options.silero_model_path = "models/silero_vad.onnx";

// Quality filtering
options.compression_ratio_threshold = 2.4f;  // Filter hallucinations
options.no_speech_threshold = 0.4f;          // Skip silent segments

// Timestamps
options.word_timestamps = false;     // Enable word-level timing

// Performance
options.batch_size = 4;              // GPU batch size
```

### TranscribeResult

```cpp
struct TranscribeResult {
    std::vector<Segment> segments;   // Transcription segments
    std::string language;            // Detected language code
    float language_probability;      // Detection confidence
    float duration;                  // Audio duration in seconds
};

struct Segment {
    int id;                   // Segment index
    int track_id;             // Audio track (for multi-track files)
    float start;              // Start time (seconds)
    float end;                // End time (seconds)
    std::string text;         // Transcribed text
    std::vector<Word> words;  // Word timestamps (if enabled)

    // Quality metrics
    float temperature;
    float avg_logprob;
    float compression_ratio;
    float no_speech_prob;
};
```

## Advanced Usage

### Progress Callback (GUI Integration)

```cpp
auto result = transcriber.transcribe("video.mp4", options,
    [](int track, int total_tracks, float progress, const std::string& message) -> bool {
        std::cout << "Track " << (track + 1) << "/" << total_tracks
                  << " - " << (progress * 100) << "% - " << message << "\n";

        // Return false to cancel transcription
        return !user_cancelled;
    }
);
```

### Multi-Track Video Files

Muninn automatically detects and processes multiple audio tracks:

```cpp
// Get track info first
auto info = muninn::Transcriber::get_audio_info("video.mp4");
std::cout << "Found " << info.num_tracks << " audio track(s)\n";

// Skip specific tracks
options.skip_tracks = {2};  // Skip track index 2
options.skip_silent_tracks = true;  // Auto-skip silent tracks

auto result = transcriber.transcribe("video.mp4", options);

// Results include track_id for each segment
for (const auto& segment : result.segments) {
    std::cout << "[Track " << segment.track_id << "] "
              << segment.text << "\n";
}
```

### Using ModelOptions

```cpp
muninn::ModelOptions model_opts;
model_opts.model_path = "models/faster-whisper-large-v3-turbo";
model_opts.device = muninn::DeviceType::CUDA;
model_opts.compute_type = muninn::ComputeType::Float16;
model_opts.device_index = 0;      // GPU index
model_opts.intra_threads = 4;     // CPU threads

muninn::Transcriber transcriber(model_opts);
```

### Temperature Fallback

Muninn automatically retries with higher temperatures if quality thresholds fail:

```cpp
options.temperature = 0.0f;  // Start with greedy decoding
options.temperature_fallback = {0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f};
options.compression_ratio_threshold = 2.4f;  // Retry if exceeded
options.log_prob_threshold = -1.0f;          // Retry if below
```

## Models

Download CTranslate2-converted Whisper models:

| Model | Size | Speed | Accuracy |
|-------|------|-------|----------|
| tiny | 75 MB | Fastest | Basic |
| base | 150 MB | Fast | Good |
| small | 500 MB | Medium | Better |
| medium | 1.5 GB | Slower | Great |
| large-v3 | 3 GB | Slow | Best |
| **large-v3-turbo** | 1.6 GB | Fast | Excellent |

Recommended: `faster-whisper-large-v3-turbo` for best speed/quality balance.

Models available at: https://huggingface.co/Systran

## Required Runtime Files

Distribute these DLLs with your application:

| File | Size | Required | Purpose |
|------|------|----------|---------|
| **muninn.dll** | 17 MB | Always | Main library with built-in audio extraction |
| **ctranslate2.dll** | 20 MB | Always | Whisper inference engine |
| **onnxruntime.dll** | 14 MB | Optional | Only needed for Silero VAD |

**No separate audio extraction DLL needed** - FFmpeg is statically linked into muninn.dll.

Plus CUDA runtime DLLs if using GPU acceleration (cudart64_*.dll, cublas64_*.dll, etc.).

## Error Handling

```cpp
try {
    muninn::Transcriber transcriber("models/whisper");
    auto result = transcriber.transcribe("audio.mp3");
} catch (const std::runtime_error& e) {
    std::cerr << "Transcription failed: " << e.what() << "\n";
}
```

Common errors:
- Model not found
- Invalid audio file
- CUDA out of memory (reduce batch_size)
- VAD model not found (check silero_model_path)

## Performance Tips

1. **Use GPU**: CUDA is 10-20x faster than CPU
2. **Use large-v3-turbo**: Best speed/quality ratio
3. **Enable VAD**: Energy VAD (default) provides good speedup; Silero VAD is more accurate but requires ONNX Runtime
4. **Batch processing**: Set `batch_size = 4-8` for long audio
5. **Use float16**: Good balance of speed and quality
6. **Built-in audio extraction**: Multi-threaded FFmpeg decoder uses all CPU cores for fast extraction

## License

Business Source License 1.1

- Free for personal and academic use
- Commercial use requires a license

Contact: craig@nordiqai.io
