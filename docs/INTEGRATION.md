# Muninn Integration Guide

This guide shows how to integrate Muninn Faster-Whisper into your C++ application.

## What's New: Self-Contained Architecture

Muninn now includes **built-in audio extraction** powered by FFmpeg:

✅ **No external audio DLLs** - Everything you need is in muninn.dll
✅ **Multi-format support** - MP3, WAV, M4A, FLAC, MP4, MOV, MKV, etc.
✅ **Multi-track support** - Process videos with multiple audio tracks
✅ **Fast extraction** - Multi-threaded FFmpeg decoder
✅ **Smart VAD auto-detection** - Automatically selects best algorithm per track (NEW!)
✅ **Zero configuration** - Works perfectly out-of-box with sensible defaults

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

// Voice Activity Detection (Auto-detection is DEFAULT)
options.vad_type = muninn::VADType::Auto;    // Smart auto-selection (DEFAULT)
// options.vad_type = muninn::VADType::Energy;  // Force Energy VAD (music + speech)
// options.vad_type = muninn::VADType::Silero;  // Force Silero VAD (clean speech only)
// options.vad_type = muninn::VADType::None;    // Disable VAD
options.silero_model_path = "models/silero_vad.onnx";  // Needed if Auto chooses Silero

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

## Streaming API

Muninn provides a real-time streaming API for live transcription with word-level timestamps - perfect for live captions, OBS plugins, and karaoke-style word highlighting.

### StreamingTranscriber Class

```cpp
namespace muninn {

class StreamingTranscriber {
public:
    // Constructor
    StreamingTranscriber(const std::string& model_path,
                         const std::string& device = "cuda",
                         const std::string& compute_type = "float16");

    // Start streaming with callback
    void start(const StreamingOptions& options, StreamingCallback callback);

    // Push audio (thread-safe, call from audio thread)
    void push_audio(const float* samples, size_t count,
                    int sample_rate = 16000, int channels = 1);

    // Poll for new segments (alternative to callback)
    bool poll_segment(Segment& out_segment);

    // Get current transcript text
    std::string get_current_text(bool include_partial = true) const;

    // Get all segments so far
    std::vector<Segment> get_all_segments() const;

    // Stop and get final results
    std::vector<Segment> stop();

    // Runtime stats
    Stats get_stats() const;

    // Control
    void reset();  // Clear buffers, start fresh
    bool is_active() const;
};

}
```

### StreamingOptions

```cpp
muninn::StreamingOptions options;

// Language
options.language = "auto";          // Auto-detect or specify ("en", "es", etc.)

// Latency control
options.chunk_length_s = 1.5f;      // Process every 1.5 seconds (500ms-3s range)
options.overlap_s = 0.3f;           // Context overlap between chunks

// Word timestamps (KARAOKE FEATURE)
options.word_timestamps = true;     // Enable word-level timing for highlighting

// Voice Activity Detection
options.enable_vad = true;          // Skip silence automatically
options.vad_type = muninn::VADType::Energy;  // Energy (fast) or Silero (accurate)

// Quality
options.beam_size = 5;              // Beam search width
options.filter_hallucinations = true;  // Skip low-quality segments

// Performance
options.batch_size = 1;             // Usually 1 for streaming (low latency)
```

### Basic Streaming Usage

```cpp
#include <muninn/streaming_transcriber.h>

// Initialize
muninn::StreamingTranscriber stream("models/faster-whisper-large-v3-turbo");

muninn::StreamingOptions options;
options.chunk_length_s = 1.5f;      // 1.5s latency
options.word_timestamps = true;     // Enable word timing

// Start with callback
stream.start(options, [](const muninn::Segment& seg) {
    std::cout << seg.text << "\n";

    // Word-level timing
    for (const auto& word : seg.words) {
        std::cout << "  " << word.word << " ("
                  << word.start << "s - " << word.end << "s)\n";
    }

    return true;  // Continue streaming
});

// Push audio from microphone/OBS/etc (thread-safe)
stream.push_audio(audio_samples, 1024, 48000, 1);

// Stop when done
auto final_segments = stream.stop();
```

### Karaoke-Style Word Highlighting

The killer feature for live captions - words light up as they're spoken:

```cpp
void render_karaoke(const muninn::Segment& seg, float current_time) {
    for (const auto& word : seg.words) {
        if (current_time >= word.start && current_time < word.end) {
            // CURRENT WORD - HIGHLIGHT!
            std::cout << "\033[1;33m" << word.word << "\033[0m ";
        } else if (current_time > word.end) {
            // Past word - dimmed
            std::cout << "\033[2m" << word.word << "\033[0m ";
        } else {
            // Future word - normal
            std::cout << word.word << " ";
        }
    }
}

// Update display at 30 FPS for smooth highlighting
while (running) {
    muninn::Segment seg;
    if (stream.poll_segment(seg)) {
        float time = get_current_playback_time();
        render_karaoke(seg, time);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(33));
}
```

### Callback vs Polling

**Callback Mode** (recommended for OBS plugins):

```cpp
stream.start(options, [&](const muninn::Segment& seg) {
    // Process segment immediately
    update_caption_display(seg);
    return true;  // Continue
});

// Push audio from OBS audio callback
stream.push_audio(obs_audio, frames, 48000, 1);
```

**Polling Mode** (recommended for GUI apps):

```cpp
stream.start(options, nullptr);  // No callback

// Worker thread polls for new segments
while (running) {
    muninn::Segment seg;
    if (stream.poll_segment(seg)) {
        // New segment available
        ui_thread->update_caption(seg);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
```

### OBS Plugin Integration

Complete example for OBS audio filter plugin:

```cpp
// Audio callback (runs on audio thread)
obs_audio_data* filter_audio(void* data, obs_audio_data* audio) {
    auto* plugin = static_cast<CaptionPlugin*>(data);

    const float* samples = reinterpret_cast<const float*>(audio->data[0]);
    plugin->stream->push_audio(samples, audio->frames, 48000, 1);

    return audio;  // Pass through
}

// Worker thread updates OBS text source
void worker_thread(CaptionPlugin* plugin) {
    while (!plugin->stop) {
        muninn::Segment seg;
        if (plugin->stream->poll_segment(seg)) {
            // Build HTML with word highlighting
            std::string html = build_karaoke_html(seg, current_time);

            // Update OBS text source (GDI+/FreeType)
            obs_data_t* settings = obs_data_create();
            obs_data_set_string(settings, "text", html.c_str());
            obs_source_update(plugin->text_source, settings);
            obs_data_release(settings);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(33));  // 30 FPS
    }
}

std::string build_karaoke_html(const muninn::Segment& seg, float time) {
    std::ostringstream html;

    for (const auto& word : seg.words) {
        if (time >= word.start && time < word.end) {
            // Current word - yellow and bold
            html << "<font color='#FFFF00'><b>" << word.word << "</b></font> ";
        } else if (time > word.end) {
            // Past word - gray
            html << "<font color='#888888'>" << word.word << "</font> ";
        } else {
            // Future word - white
            html << "<font color='#FFFFFF'>" << word.word << "</font> ";
        }
    }

    return html.str();
}
```

### Automatic Audio Resampling

StreamingTranscriber handles any sample rate/channel count automatically:

```cpp
// All of these work - automatically resampled to 16kHz mono
stream.push_audio(samples, 1024, 48000, 2);   // 48kHz stereo (OBS)
stream.push_audio(samples, 1024, 44100, 1);   // 44.1kHz mono (microphone)
stream.push_audio(samples, 1024, 16000, 1);   // 16kHz mono (optimal, no resampling)
```

### Performance Tuning

**Low Latency (Gaming/Live Chat)**:
```cpp
options.chunk_length_s = 0.8f;   // 800ms latency
options.overlap_s = 0.2f;
options.enable_vad = true;       // Skip silence for faster response
```

**High Quality (Podcasts/Interviews)**:
```cpp
options.chunk_length_s = 2.5f;   // More context = better quality
options.overlap_s = 0.5f;
options.beam_size = 10;          // Higher beam = better accuracy
```

**Balanced (Default)**:
```cpp
options.chunk_length_s = 1.5f;   // 1.5s latency
options.overlap_s = 0.3f;
options.beam_size = 5;
```

### Word Timestamp Accuracy

Muninn uses CTranslate2's attention alignment data for accurate word timestamps:

- **With alignment**: 95%+ accuracy using cross-attention peaks
- **Fallback**: Even distribution if alignment unavailable (still 80%+ accurate)
- **Automatic**: Enabled via `options.word_timestamps = true`

### Thread Safety

- ✅ `push_audio()` - Thread-safe, call from audio thread
- ✅ `poll_segment()` - Thread-safe, call from any thread
- ✅ `get_current_text()` - Thread-safe
- ⚠️ Callback runs on worker thread - use mutex for UI updates

### Statistics

```cpp
auto stats = stream.get_stats();
std::cout << "Latency: " << stats.avg_latency_ms << "ms\n";
std::cout << "Buffer fill: " << stats.buffer_fill_ratio << "\n";
std::cout << "Throughput: " << stats.throughput_ratio << "x real-time\n";
```

### Complete Example

See [examples/karaoke_captions.cpp](../examples/karaoke_captions.cpp) for a complete standalone example with ANSI color terminal output.

For OBS plugin implementation, see [docs/OBS_KARAOKE_CAPTIONS.md](OBS_KARAOKE_CAPTIONS.md).

---

## Voice Activity Detection (VAD)

### Overview

Muninn includes intelligent Voice Activity Detection that automatically selects the best algorithm for your audio. This is **critical for performance and quality** - VAD can reduce processing time by 2-3x while improving transcription accuracy by filtering silence.

### Quick Start: Auto-Detection (Recommended)

```cpp
muninn::TranscribeOptions options;
options.vad_type = muninn::VADType::Auto;  // DEFAULT - smart auto-selection
options.silero_model_path = "models/silero_vad.onnx";  // Needed if Auto chooses Silero

auto result = transcriber.transcribe("audio.mp3", options);
```

**That's it!** Muninn will automatically:
- Analyze audio characteristics (noise floor, dynamic range)
- Select the best VAD algorithm per track
- Filter silence intelligently

### VAD Options

| VAD Type | Best For | Dependencies | Speed |
|----------|----------|--------------|-------|
| **Auto** (default) | Multi-track recordings, gaming streams, mixed content | None (Silero optional) | Smart |
| **None** | Pre-cleaned audio, or when VAD causes issues | None | N/A |
| **Energy** | Game audio with music, noisy environments, mixed content | None | ~1ms/chunk |
| **Silero** | Clean speech, podcasts, studio recordings, noise-gated mics | ONNX Runtime | ~0.5ms/chunk |

### Auto-Detection Heuristics

When `VADType::Auto` is selected (default), Muninn analyzes each track:

#### 1. Multi-track Track 0 → Energy VAD
**Reasoning:** Desktop/game audio typically contains music + speech. Silero over-filters music.

```cpp
// Example: OBS recording with 3 tracks
// Track 0: Desktop/game audio → Auto-detects Energy VAD
// Track 1: Microphone → Auto-detects Silero or Energy based on noise
// Track 2: Silent → Auto-detects Energy VAD (fast skip)
```

#### 2. Very Clean Speech → Silero VAD
**Condition:** Noise floor < 0.0001, Dynamic range > 0.01
**Reasoning:** Noise gates and studio mics produce extremely low noise floors. Silero excels here.

```cpp
// Example: Gaming commentary with noise gate
[Auto-VAD] Track 1: Noise=3.48e-05, Speech=0.0193, Range=0.0192
[Auto-VAD] Track 1: Very clean speech (noise gate) → Silero VAD
```

#### 3. Clean Speech → Silero VAD
**Condition:** Noise floor < 0.01, Dynamic range > 0.15
**Reasoning:** High signal-to-noise ratio benefits from neural precision.

#### 4. Mixed/Noisy Content → Energy VAD
**Default:** All other cases
**Reasoning:** Energy VAD is robust to background noise and music.

### Real-World Test Results

Tested on 243-second gaming video (OBS multi-track):

#### Track 0: Game Audio (Desktop)
```
[Auto-VAD] Track 0: Multi-track desktop/game audio → Energy VAD
[VAD] Detected 21 speech segments
[VAD] Removed 69.3s of silence (28% filtered)
```

**Quality:** ✅ Captured all game dialogue including quiet NPCs
- "Accelerator, boa" (opening line)
- "You go now" (quiet NPC during music)
- "Scan found nothing. You go now."

**Comparison with Silero (forced):**
- Silero: 5 segments, 5.5s captured (97% filtered) ❌ Missed most dialogue
- Energy: 21 segments, 173.7s captured (28% filtered) ✅ Perfect capture

#### Track 1: Microphone (Noise Gate + Filters)
```
[Auto-VAD] Track 1: Noise=3.48e-05, Speech=0.0193, Range=0.0192
[Auto-VAD] Track 1: Very clean speech (noise gate) → Silero VAD
[SileroVAD] Detected 53 speech segments
[SileroVAD] Removed 171.8s silence (70% filtered)
```

**Quality:** ✅ Perfect transcription including:
- Small details: "Shift M" (keyboard callout)
- License plate reading: "ZHW 327"
- Natural sentence segmentation

### Manual VAD Selection

Force specific VAD if auto-detection doesn't fit your use case:

```cpp
muninn::TranscribeOptions options;

// Force Energy VAD (music + speech, noisy environments)
options.vad_type = muninn::VADType::Energy;
options.vad_threshold = 0.02f;  // RMS energy threshold (0.0-1.0)

// Force Silero VAD (clean speech only)
options.vad_type = muninn::VADType::Silero;
options.silero_model_path = "models/silero_vad.onnx";
options.vad_threshold = 0.5f;  // Speech probability threshold (0.0-1.0)

// Disable VAD (process all audio)
options.vad_type = muninn::VADType::None;
```

### VAD Fine-Tuning

```cpp
muninn::TranscribeOptions options;
options.vad_type = muninn::VADType::Auto;  // or Energy/Silero

// Energy VAD settings
options.vad_threshold = 0.02f;              // Lower = more sensitive (0.01-0.05)
options.vad_min_speech_duration_ms = 250;   // Minimum speech to keep
options.vad_min_silence_duration_ms = 500;  // Minimum silence to split

// Silero VAD settings (if using Silero)
options.silero_model_path = "models/silero_vad.onnx";
options.vad_threshold = 0.5f;               // Lower = more sensitive (0.3-0.7)
options.silero_min_speech_duration_ms = 250;
options.silero_min_silence_duration_ms = 100;  // Silero is more precise
options.silero_speech_pad_ms = 100;            // Padding around speech
```

### When to Use Each VAD Type

#### Use Auto (Default)
✅ Multi-track recordings (OBS, video editing)
✅ Mixed content (music + speech)
✅ Unknown audio characteristics
✅ "Just make it work" scenarios

#### Use Energy VAD
✅ Game audio with background music
✅ Noisy environments
✅ Field recordings
✅ When you don't want ONNX Runtime dependency

#### Use Silero VAD
✅ Podcasts and interviews
✅ Studio recordings
✅ Clean microphone with noise gate
✅ When you need maximum precision

#### Use None (Disable VAD)
✅ Pre-cleaned audio (already silence-removed)
✅ When VAD incorrectly filters speech
✅ Short audio clips (< 30s)
✅ Debugging transcription issues

### Performance Impact

**Energy VAD:**
- Overhead: ~1ms per chunk (negligible)
- Speedup: 2-3x on typical content (30-50% silence)
- Dependencies: None

**Silero VAD:**
- Overhead: ~0.5ms per chunk (faster than Energy!)
- Speedup: 3-5x on clean speech (50-70% silence)
- Dependencies: ONNX Runtime (~14 MB DLL)
- Best for: Clean speech only

**Auto VAD:**
- Overhead: ~0.1ms audio analysis (one-time per track)
- Speedup: Best of both worlds
- Dependencies: ONNX Runtime optional (graceful fallback to Energy)

### Multi-Track Intelligence

Auto-detection analyzes each track independently:

```cpp
// OBS recording with 3 tracks
auto result = transcriber.transcribe("gameplay.mp4", options);

// Muninn automatically:
// Track 0 (Desktop/Game): Energy VAD → Captures dialogue through music
// Track 1 (Microphone):   Silero VAD → Clean speech with noise gate
// Track 2 (Silent):       Energy VAD → Fast skip (0 processing)
```

### Troubleshooting

#### Issue: VAD filters too aggressively (missing speech)

**Solution 1:** Disable VAD
```cpp
options.vad_type = muninn::VADType::None;
```

**Solution 2:** Lower threshold
```cpp
options.vad_threshold = 0.01f;  // More sensitive (default: 0.02)
```

**Solution 3:** Force Energy VAD
```cpp
options.vad_type = muninn::VADType::Energy;  // Better for noisy content
```

#### Issue: Silero VAD missing speech in game audio

**Expected behavior!** Silero is designed for clean speech only. It will over-filter music + speech.

**Solution:** Use Energy VAD or Auto (will auto-select Energy)
```cpp
options.vad_type = muninn::VADType::Auto;  // Will choose Energy for mixed content
```

#### Issue: Energy VAD not filtering enough silence

**Solution 1:** Increase threshold
```cpp
options.vad_threshold = 0.05f;  // Less sensitive (default: 0.02)
```

**Solution 2:** Use Silero VAD for clean speech
```cpp
options.vad_type = muninn::VADType::Silero;
options.silero_model_path = "models/silero_vad.onnx";
```

#### Issue: "Silero model not found" error with Auto VAD

**Explanation:** Auto-detection tried to use Silero but model path not set.

**Solution:** Set Silero model path even when using Auto
```cpp
options.vad_type = muninn::VADType::Auto;
options.silero_model_path = "models/silero_vad.onnx";  // Needed for Auto
```

Or force Energy if you don't have Silero:
```cpp
options.vad_type = muninn::VADType::Energy;  // No external dependencies
```

### Streaming API VAD

Same options apply to streaming transcription:

```cpp
muninn::StreamingOptions options;
options.vad_type = muninn::VADType::Auto;   // Smart selection
options.enable_vad = true;                  // Enable VAD
options.vad_threshold = 0.02f;              // Threshold

muninn::StreamingTranscriber stream("models/faster-whisper-large-v3-turbo");
stream.start(options, callback);
```

### Advanced: Manual Audio Analysis

Analyze audio characteristics before transcription:

```cpp
#include <muninn/vad.h>

// Analyze audio
auto characteristics = muninn::analyze_audio_characteristics(audio_samples);

std::cout << "Noise floor: " << characteristics.noise_floor << "\n";
std::cout << "Speech level: " << characteristics.speech_level << "\n";
std::cout << "Dynamic range: " << characteristics.dynamic_range << "\n";
std::cout << "Is silent: " << characteristics.is_silent << "\n";

// Get VAD recommendation
auto vad_type = muninn::auto_detect_vad_type(audio_samples, track_id, total_tracks);

if (vad_type == muninn::VADType::Silero) {
    std::cout << "Recommended: Silero VAD (clean speech detected)\n";
} else {
    std::cout << "Recommended: Energy VAD (mixed/noisy content)\n";
}
```

### See Also

- **[VAD API Reference](VAD_API.md)** - Complete API documentation
- **[VAD Auto-Detection](VAD_AUTO_DETECTION.md)** - Feature details and test results
- **[Streaming API](STREAMING_API.md)** - Real-time VAD usage

---

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
3. **Use Auto VAD (default)**: Automatically selects best algorithm per track - 2-3x speedup with zero configuration
4. **Batch processing**: Set `batch_size = 4-8` for long audio
5. **Use float16**: Good balance of speed and quality on GPU
6. **Built-in audio extraction**: Multi-threaded FFmpeg decoder uses all CPU cores for fast extraction
7. **Multi-track optimization**: Auto-VAD analyzes each track independently for optimal filtering

## License

Business Source License 1.1

- Free for personal and academic use
- Commercial use requires a license

Contact: craig@nordiqai.io
