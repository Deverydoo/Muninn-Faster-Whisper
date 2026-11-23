# Muninn Integration Guide

How to use muninn.dll from a C++ program on Windows.

## 1. Files

From a release zip (or your own build):

```
bin/      muninn.dll, ctranslate2.dll, onnxruntime.dll,
          avformat-63.dll, avcodec-63.dll, avutil-61.dll, swresample-7.dll  (FFmpeg 9.0.2, LGPL)
lib/      muninn.lib  (import library)
include/  muninn/*.h
models/   silero_vad.onnx (for Silero VAD)
```

muninn.dll loads all of the DLLs in `bin/` at start-up, so ship them all,
next to your executable (not only on PATH: Windows has an older
onnxruntime.dll in System32 that would be found first). For the GPU you also
need the CUDA 13 runtime and cuDNN 9 DLLs, which are not included;
[packaging/README_BINARIES.txt](../packaging/README_BINARIES.txt) lists them,
with the driver and GPU requirements. Without a GPU, `device = "cpu"` works,
much more slowly.

Your program must be built the way muninn.dll is, because the API passes
`std::string`, `std::vector`, `std::function` and exceptions across the DLL:
C++17, MSVC 2022 17.10 or newer, x64, Release with `/MD`. Call
`muninn::check_version()` once at start-up; it throws if the DLL and the
headers come from different releases.

## 2. CMake

There is no CMake package to `find_package()`. Point at the files:

```cmake
target_include_directories(your_app PRIVATE ${MUNINN_DIR}/include)
target_link_libraries(your_app PRIVATE ${MUNINN_DIR}/lib/muninn.lib)
```

## 3. Basic use

```cpp
#include <muninn/transcriber.h>
#include <muninn/version.h>
#include <iostream>

int main() {
    muninn::check_version();

    muninn::Transcriber transcriber(
        "models/faster-whisper-large-v3-turbo",  // CTranslate2 model folder
        "cuda",                                  // "cuda", "cpu" or "auto"
        "float16");                              // float16 becomes float32 on the CPU

    auto result = transcriber.transcribe("video.mp4");

    std::cout << "Language: " << result.language << "\n";
    for (const auto& seg : result.segments) {
        std::cout << "[" << seg.track_id << "] " << seg.start << "s " << seg.text << "\n";
    }
}
```

## API reference

### Transcriber

```cpp
namespace muninn {

class Transcriber {
public:
    explicit Transcriber(const ModelOptions& options);
    Transcriber(const std::string& model_path,
                const std::string& device = "cuda",
                const std::string& compute_type = "float16");

    // Every audio track of a file (MP3, WAV, M4A, FLAC, MP4, MOV, MKV, ...)
    TranscribeResult transcribe(const std::string& audio_path,
                                const TranscribeOptions& options = {},
                                ProgressCallback progress_callback = nullptr);

    // One track already in memory (mono float at 16 kHz; other rates throw)
    TranscribeResult transcribe(const std::vector<float>& audio_samples,
                                int sample_rate = 16000,
                                const TranscribeOptions& options = {},
                                int track_id = 0,
                                int total_tracks = 1,
                                ProgressCallback progress_callback = nullptr);

    static AudioInfo get_audio_info(const std::string& audio_path);  // always throws in this build; use AudioExtractor
    ModelInfo get_model_info() const;
    DeviceInfo get_device_info() const;

    void cancel();            // see CANCELLATION_API.md
    void reset_cancel();
    bool is_cancelled() const;

    void unload_model(int timeout_ms = 3000);  // waits for a running transcribe()
    void clear_cache();
    std::vector<size_t> encode(const std::string& text) const;
};

}
```

### TranscribeOptions

The full list with defaults is in `include/muninn/types.h`. The ones most
programs set:

```cpp
muninn::TranscribeOptions options;

options.language = "auto";            // or "en", "es", "ja", ...; an unknown code throws
options.task = "transcribe";          // or "translate" (into English)

options.beam_size = 5;
options.temperature_fallback = {0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f};
options.best_of = 5;                  // samples per window above temperature 0

options.vad_type = muninn::VADType::Auto;              // see VAD_API.md
options.silero_model_path = "models/silero_vad.onnx";  // lets Auto use Silero

options.word_timestamps = true;       // Segment::words
options.initial_prompt = "Names: Icarus, Tusker";
options.clip_start = 60.0f;           // seconds; clip_end = -1 for the rest
options.skip_tracks = {2};
options.track_hints[1] = muninn::TrackType::Microphone;
```

`sequential = true` (the default) decodes 30-second windows one after another
with context, as faster-whisper does. `batch_size` only applies to the older
batched path (`sequential = false`). `hotwords` is not used yet.

### TranscribeResult

```cpp
struct TranscribeResult {
    std::vector<Segment> segments;
    std::string language;          // detected or given
    float language_probability;
    float duration;                // seconds
    bool was_cancelled;
    int failed_windows;            // windows whose decode threw (e.g. CUDA out of memory)
    int failed_tracks;             // tracks that couldn't be extracted or decoded
    std::string error;             // the last failure, "" when none
};

struct Segment {
    int id;
    int track_id;
    float start, end;              // seconds on the original timeline
    std::string text;
    std::vector<Word> words;       // with word_timestamps
    std::string language;
    float language_probability;
    int speaker_id;                // -1 unless diarization assigned one
    std::string speaker_label;
    float speaker_confidence;
    float temperature, avg_logprob, compression_ratio, no_speech_prob;
    std::string translated_text, translation_target;   // filled by the caller
};
```

`transcribe()` throws when nothing could be transcribed. When only some
windows or tracks fail, it returns what worked and reports the rest in
`failed_windows`, `failed_tracks` and `error`.

## Streaming API

`StreamingTranscriber` transcribes audio as it arrives, with word timestamps, for live captions. [STREAMING_API.md](STREAMING_API.md) has the details (windows, latency, thread rules).

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

    // Push audio (any thread; never waits for a decode). count is the
    // number of floats (frames x channels). False when nothing was taken.
    bool push_audio(const float* samples, size_t count,
                    int sample_rate = 16000, int channels = 1);

    // Poll for new segments (alternative to callback)
    bool poll_segment(Segment& out_segment);

    // Get current transcript text (include_partial adds held-back words)
    std::string get_current_text(bool include_partial = true) const;

    // Get all segments so far
    std::vector<Segment> get_all_segments() const;

    // Stop and get final results
    std::vector<Segment> stop();

    // Runtime stats
    Stats get_stats() const;

    // Errors on the worker thread (a window that failed is skipped)
    void set_error_callback(StreamingErrorCallback callback);
    std::string last_error() const;

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

// Limits
options.max_buffered_s = 60.0f;     // audio waiting beyond this is dropped, oldest first
```

See [STREAMING_API.md](STREAMING_API.md) for the rest.

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

Highlight each word while it is spoken:

```cpp
void render_karaoke(const muninn::Segment& seg, float current_time) {
    for (const auto& word : seg.words) {
        if (current_time >= word.start && current_time < word.end) {
            // current word: bold yellow
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

**Defaults**:
```cpp
options.chunk_length_s = 2.0f;
options.overlap_s = 0.5f;        // at most half a chunk
options.beam_size = 5;
```

Every window is padded to 30 seconds before decoding, so short chunks cost
nearly as much GPU time each as long ones.

### Word Timestamp Accuracy

Words are timed from CTranslate2's cross-attention alignment, with
faster-whisper's word splitting and punctuation merging, as in the offline
path. They are always aligned internally (that is how windows are joined);
`options.word_timestamps` (default `true`) says whether they are returned.

### Thread Safety

- `push_audio()`: any thread, including an audio callback; never waits for a decode.
- `poll_segment()`, `get_all_segments()`, `get_current_text()`, `get_stats()`: any thread.
- The segment and error callbacks run on the worker thread with no lock held.
  They may call `reset()` and the getters, but not `start()` or `stop()`
  (those throw `std::logic_error` there). Use your own lock for UI updates.

### Statistics

```cpp
auto stats = stream.get_stats();
std::cout << "Decode time per window: " << stats.average_latency_ms << " ms\n";
std::cout << "Audio waiting: " << stats.buffer_fill_ratio << " chunks\n";
std::cout << "Failed windows: " << stats.chunks_failed
          << ", audio dropped: " << stats.samples_dropped << " samples\n";
```

### Complete Example

[examples/karaoke_captions.cpp](../examples/karaoke_captions.cpp) plays an audio file into the transcriber in real time and draws the captions with the current word highlighted.

For OBS plugin implementation, see [docs/OBS_KARAOKE_CAPTIONS.md](OBS_KARAOKE_CAPTIONS.md).

---

## Voice Activity Detection (VAD)

By default (`VADType::Auto`) the transcriber picks a detector per track: no
VAD for game, system and music tracks and for track 0 of a multi-track file,
Silero for microphone tracks and clean speech, Energy otherwise.

```cpp
muninn::TranscribeOptions options;
options.silero_model_path = "models/silero_vad.onnx";  // without it Silero falls back to Energy
options.track_hints = {
    {0, muninn::TrackType::GameAudio},
    {1, muninn::TrackType::Microphone},
};

// Or force one detector:
options.vad_type = muninn::VADType::Energy;   // vad_threshold <= 0.1 is an RMS level
options.vad_type = muninn::VADType::Silero;   // vad_threshold > 0.1 is a speech probability
options.vad_type = muninn::VADType::None;
```

`VADType::TEN` currently runs Silero. Options, defaults and fallbacks:
[VAD_API.md](VAD_API.md). The auto rules: [VAD_AUTO_DETECTION.md](VAD_AUTO_DETECTION.md).

## Advanced usage

### Progress and cancellation

```cpp
auto result = transcriber.transcribe("video.mp4", options,
    [&](int track, int total_tracks, float progress, const std::string& message) {
        std::cout << "Track " << (track + 1) << "/" << total_tracks << " "
                  << int(progress * 100) << "% " << message << "\n";
        return !user_cancelled;   // false cancels
    });
```

`transcriber.cancel()` from another thread does the same. See
[CANCELLATION_API.md](CANCELLATION_API.md).

### Multi-track files

Each audio track is extracted and transcribed separately; segments carry
`track_id`.

```cpp
auto info = muninn::Transcriber::get_audio_info("video.mp4");
std::cout << info.num_tracks << " track(s), " << info.duration << " s\n";

options.skip_tracks = {2};          // don't transcribe track 2
options.skip_silent_tracks = true;  // default: skip tracks with no signal
auto result = transcriber.transcribe("video.mp4", options);
```

### ModelOptions

```cpp
muninn::ModelOptions m;
m.model_path = "models/faster-whisper-large-v3-turbo";
m.device = muninn::DeviceType::Auto;          // CUDA when a compatible GPU is there, else CPU
m.compute_type = muninn::ComputeType::Float16; // float32 on the CPU
m.device_index = 0;                            // GPU index
m.intra_threads = 0;                           // CPU threads; 0 = half the hardware threads
muninn::Transcriber transcriber(m);

auto dev = transcriber.get_device_info();      // what was actually used
```

### Temperature fallback

A window is decoded again at the next temperature when its compression ratio
is above `compression_ratio_threshold` (2.4) or its average log probability is
below `log_prob_threshold` (-1.0). Above temperature 0 it takes `best_of`
samples with beam 1 and keeps the best, as faster-whisper does.

## Models

Muninn reads CTranslate2-format Whisper models, for example
`Systran/faster-whisper-large-v3` or `deepdml/faster-whisper-large-v3-turbo-ct2`
on Hugging Face. large-v3-turbo is the usual choice: close to large-v3 in
accuracy and much faster. It can't translate (task "translate"); use
large-v3 or a smaller multilingual model for that.

## Logging

Muninn logs through `muninn::Logger` (`muninn/logging.h`). Without a callback
every line goes to stderr, never stdout. Set a callback to send it to your own
log:

```cpp
muninn::Logger::set_callback([](muninn::LogLevel level, const std::string& msg) {
    my_log(level, msg);
});
```

## Errors

```cpp
try {
    muninn::Transcriber transcriber("models/whisper");
    auto result = transcriber.transcribe("audio.mp3");
    if (result.failed_windows || result.failed_tracks)
        std::cerr << "Part of the audio failed: " << result.error << "\n";
} catch (const std::exception& e) {
    std::cerr << "Transcription failed: " << e.what() << "\n";
}
```

The constructor throws when the model can't be loaded. `transcribe()` throws
for a file that can't be opened, a language the model doesn't know, or a run
in which nothing succeeded.

## License

Business Source License 1.1 with an additional grant for any non-commercial
use. Each release becomes MIT-licensed four years after it is published. For
commercial use, see [LICENSE](../LICENSE).
