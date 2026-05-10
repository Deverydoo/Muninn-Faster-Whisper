# Muninn Streaming API

Real-time transcription API for live captions, subtitles, and voice commands.

## Overview

`StreamingTranscriber` processes audio incrementally with low latency (~500ms-2s), suitable for:

- **Live captions** in OBS/streaming software
- **Real-time subtitles** for videos
- **Voice commands** and live transcription
- **Meeting transcription** with live updates

## Quick Start

```cpp
#include <muninn/streaming_transcriber.h>

// Initialize
muninn::StreamingTranscriber stream("models/faster-whisper-large-v3-turbo");

// Start with callback
stream.start({}, [](const muninn::Segment& seg) {
    std::cout << "[" << seg.start << "s] " << seg.text << std::endl;
    return true;  // Continue
});

// Push audio chunks (from mic/OBS)
while (recording) {
    float* mic_buffer = get_mic_audio();  // Your audio source
    stream.push_audio(mic_buffer, 1024, 48000, 1);
}

// Stop and get final segments
auto all_segments = stream.stop();
```

## API Reference

### StreamingTranscriber

```cpp
class StreamingTranscriber {
public:
    // Initialize with model
    StreamingTranscriber(const std::string& model_path,
                         const std::string& device = "cuda",
                         const std::string& compute_type = "float16");

    // Start streaming session
    void start(const StreamingOptions& options = {},
               StreamingCallback callback = nullptr);

    // Stop and flush
    std::vector<Segment> stop();

    // Push audio (thread-safe)
    void push_audio(const float* samples,
                    size_t sample_count,
                    int sample_rate = 16000,
                    int channels = 1);

    // Poll for results (thread-safe, non-blocking)
    bool poll_segment(Segment& out_segment);

    // Get current text (last 5 segments)
    std::string get_current_text(bool include_partial = true) const;

    // Get all segments
    std::vector<Segment> get_all_segments() const;

    // Reset state
    void reset();

    // Get stats
    Stats get_stats() const;
};
```

### StreamingOptions

```cpp
struct StreamingOptions {
    std::string language = "auto";         // Language code or "auto"
    int beam_size = 5;                     // Beam search width
    float temperature = 0.0f;              // Sampling temperature

    float chunk_length_s = 2.0f;           // Process audio every N seconds
    float overlap_s = 0.5f;                // Overlap for context
    bool enable_vad = true;                // Use VAD
    VADType vad_type = VADType::Energy;    // VAD algorithm

    float compression_ratio_threshold = 2.4f;  // Hallucination filter
    float no_speech_threshold = 0.4f;          // Silence threshold
    bool word_timestamps = true;               // Enable word timing
};
```

### StreamingCallback

```cpp
using StreamingCallback = std::function<bool(const Segment& segment)>;

// Example:
stream.start({}, [](const Segment& seg) {
    // Process completed segment
    std::cout << seg.text << std::endl;

    // Return false to stop, true to continue
    return !user_stopped;
});
```

## Usage Patterns

### Pattern 1: Callback-Based (Recommended for GUIs)

```cpp
muninn::StreamingTranscriber stream("models/whisper-turbo");

stream.start({}, [](const muninn::Segment& seg) {
    // Update UI with new segment
    update_caption_display(seg.text);
    return true;
});

// Audio callback thread
void on_audio_data(float* samples, size_t count, int rate) {
    stream.push_audio(samples, count, rate);
}

// Later...
stream.stop();
```

### Pattern 2: Polling (Recommended for Main Loops)

```cpp
muninn::StreamingTranscriber stream("models/whisper-turbo");
stream.start();

while (app_running) {
    // Push audio from source
    stream.push_audio(mic_samples, 1024, 48000);

    // Poll for new segments
    muninn::Segment seg;
    if (stream.poll_segment(seg)) {
        std::cout << seg.text << std::endl;
    }

    // Update UI
    std::string current = stream.get_current_text();
    update_display(current);
}

stream.stop();
```

### Pattern 3: OBS Integration

```cpp
struct obs_whisper_data {
    muninn::StreamingTranscriber* stream;
    obs_source_t* text_source;
};

// Filter audio callback
obs_audio_data* filter_audio(void* data, obs_audio_data* audio) {
    auto* filter = static_cast<obs_whisper_data*>(data);

    // Push audio to Muninn
    const float* samples = reinterpret_cast<const float*>(audio->data[0]);
    filter->stream->push_audio(samples, audio->frames, 48000, 1);

    return audio;  // Pass through
}

// Worker thread polls for results
void worker_thread(obs_whisper_data* data) {
    muninn::Segment seg;
    while (running) {
        if (data->stream->poll_segment(seg)) {
            // Update OBS text source
            obs_data_t* settings = obs_data_create();
            obs_data_set_string(settings, "text", seg.text.c_str());
            obs_source_update(data->text_source, settings);
            obs_data_release(settings);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
```

## Features

### Automatic Resampling

Audio is automatically resampled to 16kHz:

```cpp
// Any sample rate works
stream.push_audio(samples_44100, count, 44100);  // 44.1kHz
stream.push_audio(samples_48000, count, 48000);  // 48kHz
stream.push_audio(samples_16000, count, 16000);  // 16kHz (optimal)
```

### Stereo to Mono Conversion

```cpp
// Stereo audio (interleaved)
float stereo_buffer[2048];  // L,R,L,R,...
stream.push_audio(stereo_buffer, 2048, 48000, 2);  // channels=2
```

### Voice Activity Detection

```cpp
muninn::StreamingOptions opts;
opts.enable_vad = true;
opts.vad_type = muninn::VADType::Energy;  // Fast, no dependencies

// Or use Silero for better accuracy
// opts.vad_type = muninn::VADType::Silero;

stream.start(opts);
```

### Hallucination Filtering

Built-in quality checks automatically filter out hallucinations:

```cpp
muninn::StreamingOptions opts;
opts.compression_ratio_threshold = 2.4f;  // Skip repetitive text
opts.no_speech_threshold = 0.4f;          // Skip silence
stream.start(opts);
```

### Statistics

```cpp
auto stats = stream.get_stats();
std::cout << "Processed: " << stats.total_audio_duration_s << "s\n";
std::cout << "Segments: " << stats.segments_completed << "\n";
std::cout << "Latency: " << stats.average_latency_ms << "ms\n";
std::cout << "Buffer: " << (stats.buffer_fill_ratio * 100) << "%\n";
```

## Performance

- **Latency**: 500ms-2s depending on `chunk_length_s`
- **Throughput**: 22x real-time on GPU (RTX 4090)
- **Memory**: ~500MB model + buffers
- **CPU Usage**: Minimal (most work on GPU)

### Tuning Latency

```cpp
muninn::StreamingOptions opts;

// Lower latency (more CPU, less context)
opts.chunk_length_s = 0.5f;   // Process every 500ms
opts.overlap_s = 0.1f;         // Minimal overlap

// Higher quality (more context, higher latency)
opts.chunk_length_s = 3.0f;    // Process every 3s
opts.overlap_s = 1.0f;          // 1s overlap for context
```

## Thread Safety

- ✅ `push_audio()` - Thread-safe, can call from audio callback
- ✅ `poll_segment()` - Thread-safe, can call from main thread
- ✅ `get_current_text()` - Thread-safe
- ✅ `get_stats()` - Thread-safe
- ❌ `start()`/`stop()` - Must call from same thread

## Comparison: Streaming vs Batch

| Feature | StreamingTranscriber | Transcriber (Batch) |
|---------|---------------------|-------------------|
| Latency | 500ms-2s | Processes full file |
| Use Case | Live captions, real-time | Post-production, archival |
| Memory | Low (buffers chunks) | High (full audio) |
| Word timestamps | Yes | Yes |
| VAD | Yes | Yes |
| Multi-track | No | Yes |
| Quality | Good | Excellent |

## Example: Export SRT from Stream

```cpp
// Collect segments during stream
std::vector<muninn::Segment> all_segments;

stream.start({}, [&](const muninn::Segment& seg) {
    all_segments.push_back(seg);
    return true;
});

// ... record audio ...

stream.stop();

// Export to SRT
export_srt(all_segments, "output.srt");
```

## Limitations

1. **No multi-track support** - Single audio stream only
2. **No file-based input** - Memory/stream only (use `Transcriber` for files)
3. **Limited context** - Only `overlap_s` seconds of context
4. **English bias** - Best for English; other languages may have higher latency

## Next Steps

- See [OBS Integration Guide](obs_whisper_captions_design.md)
- See [INTEGRATION.md](INTEGRATION.md) for batch API
- Check [examples/streaming_captions.cpp](../examples/streaming_captions.cpp)

## License

Business Source License 1.1 - Free for personal/academic use

Commercial licensing: craig@nordiqai.io
