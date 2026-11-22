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

The header, `include/muninn/streaming_transcriber.h`, is the full reference.
In short:

```cpp
class StreamingTranscriber {
public:
    // device: "cuda", "cpu" or "auto" (CUDA when it works, else CPU).
    // On the CPU, float16 becomes float32 and int8_float16 becomes int8.
    StreamingTranscriber(const std::string& model_path,
                         const std::string& device = "cuda",
                         const std::string& compute_type = "float16");
    // Uses device, compute_type, device_index and intra_threads.
    explicit StreamingTranscriber(const ModelOptions& options);

    void start(const StreamingOptions& options = {},
               StreamingCallback callback = nullptr);
    std::vector<Segment> stop();          // decodes what's buffered; returns every segment
    bool is_active() const;

    // Never waits for a decode. Any channel count (averaged to mono), any
    // sample rate > 0. Returns false when nothing was taken.
    bool push_audio(const float* samples, size_t sample_count,
                    int sample_rate = 16000, int channels = 1);

    bool poll_segment(Segment& out_segment);
    std::vector<Segment> get_all_segments() const;
    // include_partial adds the words decoded but held back for the next window.
    std::string get_current_text(bool include_partial = true) const;
    void reset();                         // clears everything; timeline back to 0

    void set_error_callback(StreamingErrorCallback callback);
    std::string last_error() const;
    Stats get_stats() const;
};
```

### StreamingOptions

```cpp
struct StreamingOptions {
    std::string language = "auto";     // detected on the first speech, or a code the model knows
    std::string task = "transcribe";   // or "translate" (to English)
    int beam_size = 5;
    float temperature = 0.0f;

    float chunk_length_s = 2.0f;       // window length (at most 30 s)
    float overlap_s = 0.5f;            // windows overlap by this (at most half a window)
    bool enable_vad = true;
    VADType vad_type = VADType::Energy;
    std::string vad_model_path;        // silero_vad.onnx, for VADType::Silero
    bool vad_fallback_to_energy = true;

    float compression_ratio_threshold = 2.4f;  // zlib ratio above this = decoding loop
    float no_speech_threshold = 0.4f;          // silence = no_speech above this
    float log_prob_threshold = -1.0f;          //   AND avg log-prob below this
    bool word_timestamps = true;               // Segment::words, from CTranslate2's alignment
    bool filter_hallucinations = true;         // drop windows that look like a loop
    bool condition_on_previous_text = true;    // prompt each window with the text before it

    float max_buffered_s = 60.0f;              // audio waiting beyond this is dropped, oldest first
    size_t max_pending_segments = 1000;        // poll queue cap, oldest dropped first
};
```

### How windows are joined

Each window starts `overlap_s` before the previous one ended. Every word gets a
time from CTranslate2's alignment, and a word is emitted only by the window in
which it sits furthest from an edge. Nothing is emitted twice, and a word cut by
one window's edge is taken from the next window, which hears it whole. The cost
is latency: the last `overlap_s / 2` seconds of a window wait for the next one.

Timestamps are seconds of audio pushed since `start()` (or `reset()`). They stay
right when decoding falls behind, and when audio is dropped because the buffer
was full.

### Callbacks

```cpp
using StreamingCallback = std::function<bool(const Segment& segment)>;
using StreamingErrorCallback = std::function<void(const std::string& message)>;
```

Both run on the worker thread with no lock held, so they may call any method
except `start()` and `stop()`, which throw `std::logic_error` there (`stop()`
would be waiting for itself). Return false from the segment callback to end the
session, then call `stop()` from your own thread.

A window that fails to decode (out of GPU memory, for example) is skipped and
reported to the error callback and `last_error()`; streaming carries on. An
exception from your segment callback is caught and reported the same way.

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

### Any Channel Count

Interleaved audio with any number of channels is averaged to mono.
`sample_count` is the number of floats (frames x channels).

```cpp
float stereo_buffer[2048];  // L,R,L,R,...
stream.push_audio(stereo_buffer, 2048, 48000, 2);   // 1024 frames
stream.push_audio(surround, 6 * 480, 48000, 6);     // 5.1, 10 ms
```

The resampler keeps its position between calls, so audio pushed in small
blocks comes out exactly as if it were pushed in one piece.

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
- **Memory**: ~500MB model + buffers
- **CPU Usage**: Minimal (most work on GPU)

### Tuning Latency

Every window is padded to 30 s before decoding, as Whisper expects, so a short
window costs a full encoder pass: halving `chunk_length_s` roughly doubles the
GPU work.

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

- `push_audio()`: any thread, including audio callbacks. It never waits for a
  decode; calls from several threads at once are serialised.
- `poll_segment()`, `get_all_segments()`, `get_current_text()`, `get_stats()`,
  `last_error()`, `reset()`, `set_error_callback()`: any thread, and from the
  callbacks.
- `start()` and `stop()`: any thread except the callbacks.

## Comparison: Streaming vs Batch

| Feature | StreamingTranscriber | Transcriber (Batch) |
|---------|---------------------|-------------------|
| Latency | 500ms-2s | Processes full file |
| Use Case | Live captions, real-time | Post-production, archival |
| Memory | Low (buffers chunks) | High (full audio) |
| Word timestamps | Yes | Yes |
| Language detection | First speech | Per file |
| VAD | Yes | Yes |
| Multi-track | No | Yes |
| Quality | Good | Excellent |

## Example: Export SRT from Stream

```cpp
#include <muninn/subtitle_export.h>

stream.start(opts);
// ... push audio ...
auto segments = stream.stop();   // every segment of the session

muninn::SubtitleExporter exporter;
exporter.export_srt(segments, "recording.mp4");   // writes recording.srt beside it
```

## Limitations

1. **One audio stream.** Mix tracks before pushing, or run one transcriber per track.
2. **The language is detected once**, on the first window with speech, and kept.
3. **No temperature fallback.** A window that looks like a decoding loop is dropped rather than retried.
4. **Latency** is about one window plus half the overlap, plus the decode time.

## Next Steps

- See [OBS Integration Guide](OBS_KARAOKE_CAPTIONS.md)
- See [INTEGRATION.md](INTEGRATION.md) for batch API
- Examples: [karaoke_captions.cpp](../examples/karaoke_captions.cpp),
  [translation_streaming.cpp](../examples/translation_streaming.cpp),
  [streaming_vad_selection.cpp](../examples/streaming_vad_selection.cpp)

## License

Business Source License 1.1 - Free for personal/academic use

Commercial licensing: craig@nordiqai.io
