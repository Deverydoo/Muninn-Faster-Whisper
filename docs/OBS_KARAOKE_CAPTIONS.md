# OBS Karaoke Captions Plugin Guide

A sketch of an OBS filter plugin that shows live captions with the current word highlighted, using Muninn's streaming API. It is not a finished plugin: the OBS calls are outlined, not tested. `examples/karaoke_captions.cpp` is the same idea as a complete program that runs in a terminal.

What it gives you:
- Captions about one window (2 s by default) plus the decode time after the speech
- The current word highlighted, from streaming's word timestamps
- The language detected on the first speech (`language = "auto"`)
- SRT or VTT export of the session through `SubtitleExporter`

The plugin must be built the way README_BINARIES.txt says (MSVC 2022 17.10 or newer, Release, /MD) and should call `muninn::check_version()` once when it loads.

## Architecture

```
OBS Audio Source
       ↓
   Audio Filter (captures PCM)
       ↓
StreamingTranscriber (Muninn)
       ↓
Word Timestamps + Text
       ↓
OBS Text Source (GDI+/FreeType)
```

## Implementation Steps

### 1. Project Setup

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(obs-muninn-captions)

# Find OBS
find_package(libobs REQUIRED)

# Muninn: headers from include/, import library lib/muninn.lib
target_include_directories(obs-muninn-captions PRIVATE ${MUNINN_DIR}/include)
target_link_libraries(obs-muninn-captions
    PRIVATE
        OBS::libobs
        ${MUNINN_DIR}/lib/muninn.lib
)
```

### 2. Plugin Structure

```cpp
// obs_muninn_captions.cpp

#include <obs-module.h>
#include <muninn/streaming_transcriber.h>
#include <mutex>
#include <deque>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-muninn-captions", "en-US")

struct caption_word {
    std::string text;
    float start;
    float end;
    float probability;
};

struct caption_segment {
    std::string text;
    std::vector<caption_word> words;
    float start;
    float end;
};

struct muninn_caption_data {
    obs_source_t* source;           // This filter instance
    obs_source_t* text_source;      // Target text source for captions

    // Muninn transcriber
    std::unique_ptr<muninn::StreamingTranscriber> transcriber;

    // Settings
    std::string model_path;
    bool enabled;
    bool word_highlighting;
    float latency_s;

    // Caption state
    std::mutex caption_mutex;
    std::deque<caption_segment> segments;
    std::atomic<float> stream_time;  // written by the audio thread, read by the worker
    int sample_rate;                 // audio_output_get_sample_rate(obs_get_audio())

    // Worker thread
    std::thread worker;
    std::atomic<bool> stop_worker;
};
```

### 3. Filter Creation

```cpp
static void* caption_filter_create(obs_data_t* settings, obs_source_t* source) {
    auto* data = new muninn_caption_data;
    data->source = source;
    data->enabled = true;
    data->word_highlighting = true;
    data->latency_s = 1.5f;
    data->stream_time = 0.0f;
    data->sample_rate = static_cast<int>(audio_output_get_sample_rate(obs_get_audio()));
    data->stop_worker = false;

    // Get settings
    const char* model_path = obs_data_get_string(settings, "model_path");
    const char* text_source_name = obs_data_get_string(settings, "text_source");

    // Find text source
    data->text_source = obs_get_source_by_name(text_source_name);

    // Initialize Muninn
    try {
        data->transcriber = std::make_unique<muninn::StreamingTranscriber>(
            model_path, "cuda", "float16"
        );

        // Configure for low latency
        muninn::StreamingOptions opts;
        opts.language = "auto";
        opts.chunk_length_s = data->latency_s;
        opts.overlap_s = 0.3f;
        opts.enable_vad = true;
        opts.word_timestamps = true;  // ENABLE WORD TIMESTAMPS

        // Start with callback
        data->transcriber->start(opts, [data](const muninn::Segment& seg) {
            // Convert to caption_segment
            caption_segment cap;
            cap.text = seg.text;
            cap.start = seg.start;
            cap.end = seg.end;

            for (const auto& word : seg.words) {
                caption_word w;
                w.text = word.word;
                w.start = word.start;
                w.end = word.end;
                w.probability = word.probability;
                cap.words.push_back(w);
            }

            // Add to queue
            std::lock_guard<std::mutex> lock(data->caption_mutex);
            data->segments.push_back(cap);

            // Keep only last 5 segments
            if (data->segments.size() > 5) {
                data->segments.pop_front();
            }

            return true;  // Continue
        });

        blog(LOG_INFO, "[Muninn] Streaming captions started");

    } catch (const std::exception& e) {
        blog(LOG_ERROR, "[Muninn] Failed to initialize: %s", e.what());
    }

    // Start render worker
    data->worker = std::thread([data]() {
        while (!data->stop_worker) {
            update_text_source(data);
            std::this_thread::sleep_for(std::chrono::milliseconds(33));  // 30 FPS
        }
    });

    return data;
}

static void caption_filter_destroy(void* vdata) {
    auto* data = static_cast<muninn_caption_data*>(vdata);

    data->stop_worker = true;
    if (data->worker.joinable()) {
        data->worker.join();
    }

    if (data->transcriber) {
        data->transcriber->stop();
    }

    if (data->text_source) {
        obs_source_release(data->text_source);
    }

    delete data;
}
```

### 4. Audio Callback

```cpp
static struct obs_audio_data* caption_filter_audio(
    void* vdata,
    struct obs_audio_data* audio
) {
    auto* data = static_cast<muninn_caption_data*>(vdata);

    if (!data || !data->enabled || !audio || !data->transcriber) {
        return audio;
    }

    // Get mono float32 audio
    const uint8_t* chan0 = audio->data[0];
    if (!chan0) {
        return audio;
    }

    const float* samples = reinterpret_cast<const float*>(chan0);
    size_t frames = audio->frames;

    // OBS audio is planar float; this pushes the first channel. Use OBS's
    // real rate: audio_output_get_sample_rate(obs_get_audio()).
    data->transcriber->push_audio(samples, frames, data->sample_rate, 1);

    // Seconds pushed: the same clock as the segment timestamps
    data->stream_time = data->stream_time + static_cast<float>(frames) / data->sample_rate;

    return audio;  // Pass through
}
```

### 5. Text Rendering with Word Highlighting

```cpp
void update_text_source(muninn_caption_data* data) {
    if (!data->text_source) {
        return;
    }

    std::lock_guard<std::mutex> lock(data->caption_mutex);

    if (data->segments.empty()) {
        return;
    }

    // Get last segment for display
    const auto& seg = data->segments.back();

    if (!data->word_highlighting || seg.words.empty()) {
        // Simple mode - just show text
        obs_data_t* settings = obs_data_create();
        obs_data_set_string(settings, "text", seg.text.c_str());
        obs_source_update(data->text_source, settings);
        obs_data_release(settings);
        return;
    }

    // Karaoke mode. A segment arrives about one window after its words were
    // spoken, so the display clock runs that far behind the audio clock;
    // otherwise every word is already past when it appears.
    const float display_time = data->stream_time - (data->latency_s + 1.0f);
    std::string formatted_text = build_karaoke_text(seg, display_time);

    obs_data_t* settings = obs_data_create();
    obs_data_set_string(settings, "text", formatted_text.c_str());
    obs_source_update(data->text_source, settings);
    obs_data_release(settings);
}

std::string build_karaoke_text(const caption_segment& seg, float current_time) {
    std::ostringstream html;

    // Find current word
    int current_word_idx = -1;
    for (size_t i = 0; i < seg.words.size(); ++i) {
        const auto& w = seg.words[i];
        if (current_time >= w.start && current_time < w.end) {
            current_word_idx = static_cast<int>(i);
            break;
        }
    }

    // Build HTML with color coding (escape the text with
    // muninn::HTMLFormatter::escape if it can hold &, < or >)
    for (size_t i = 0; i < seg.words.size(); ++i) {
        if (static_cast<int>(i) < current_word_idx) {
            // Past words - gray
            html << "<font color='#888888'>" << seg.words[i].text << "</font> ";
        } else if (static_cast<int>(i) == current_word_idx) {
            // Current word - yellow and bold
            html << "<font color='#FFFF00'><b>" << seg.words[i].text << "</b></font> ";
        } else {
            // Future words - white
            html << "<font color='#FFFFFF'>" << seg.words[i].text << "</font> ";
        }
    }

    return html.str();
}
```

### 6. Settings UI

```cpp
static obs_properties_t* caption_filter_properties(void* vdata) {
    obs_properties_t* props = obs_properties_create();

    // Model path
    obs_properties_add_path(
        props,
        "model_path",
        "Whisper Model Path",
        OBS_PATH_DIRECTORY,
        nullptr,
        nullptr
    );

    // Text source selector
    obs_property_t* text_source = obs_properties_add_list(
        props,
        "text_source",
        "Caption Text Source",
        OBS_COMBO_TYPE_LIST,
        OBS_COMBO_FORMAT_STRING
    );

    // Enumerate text sources
    obs_enum_sources([](void* param, obs_source_t* source) {
        auto* prop = static_cast<obs_property_t*>(param);
        const char* id = obs_source_get_id(source);

        if (strcmp(id, "text_gdiplus") == 0 || strcmp(id, "text_ft2_source") == 0) {
            const char* name = obs_source_get_name(source);
            obs_property_list_add_string(prop, name, name);
        }

        return true;
    }, text_source);

    // Enable/disable
    obs_properties_add_bool(props, "enabled", "Enable Captions");

    // Word highlighting
    obs_properties_add_bool(props, "word_highlighting", "Word Highlighting (Karaoke Mode)");

    // Latency slider
    obs_properties_add_float_slider(
        props,
        "latency",
        "Latency (seconds)",
        0.5, 3.0, 0.5
    );

    // Language
    obs_properties_add_list(
        props,
        "language",
        "Language",
        OBS_COMBO_TYPE_LIST,
        OBS_COMBO_FORMAT_STRING
    );
    // Add languages: "auto", "en", "es", "ja", etc.

    // Export button: its callback asks for a path and calls
    // export_subtitles() from step 7
    obs_properties_add_button(
        props,
        "export_subtitles",
        "Export Subtitles",
        export_button_clicked
    );

    return props;
}
```

### 7. Export Subtitles

`SubtitleExporter` writes the session's segments as SRT or VTT. SRT has no word timing; VTT can carry it as per-word tags (`vtt_include_word_timestamps`). How you ask the user for a path depends on your UI; `path` below is that choice.

```cpp
#include <muninn/subtitle_export.h>

static void export_subtitles(muninn_caption_data* data, const std::string& path) {
    const auto segments = data->transcriber->get_all_segments();

    muninn::SubtitleExportOptions opts;
    opts.output_path = path;                    // ends in .vtt here
    opts.format = muninn::SubtitleFormat::VTT;  // the format comes from this, not the extension
    opts.vtt_include_word_timestamps = true;

    muninn::SubtitleExporter exporter;
    exporter.export_subtitles(segments, "", opts);  // video path unused when output_path is set
}
```

## Styling Options

`word_styling.h` has ready-made formatters. Muninn doesn't measure how loud a word is, so the styles go by timing and confidence. Word text is HTML-escaped by `format_word`.

### Option 1: Karaoke Highlighting

Past words dimmed, the current word highlighted in bold yellow:

```cpp
#include <muninn/word_styling.h>

std::string build_karaoke_html(const muninn::Segment& seg, float time) {
    return muninn::HTMLFormatter::build_karaoke_html(seg, time);
}
```

### Option 2: Confidence-Based Coloring

Show transcription confidence with colors:

```cpp
std::string build_confidence_html(const muninn::Segment& seg, float time) {
    // Color by confidence:
    // - Low confidence (< 60%): red
    // - Medium confidence (60-80%): yellow
    // - High confidence (> 80%): green
    return muninn::HTMLFormatter::build_confidence_html(seg, time);
}
```

### Option 3: Custom Manual Styling

For full control, use the WordStyle API:

```cpp
std::string build_custom_html(const muninn::Segment& seg, float time) {
    std::ostringstream html;

    for (const auto& word : seg.words) {
        bool is_current = (time >= word.start && time < word.end);

        muninn::WordStyle style{};

        if (is_current) {
            style.color = "#FFFF00";                                  // Yellow for the current word
            style.bold = true;
            style.italic = word.probability < 0.6f;                   // Italic when unsure
        } else if (time > word.end) {
            style.color = "#888888";  // Dimmed for past words
        } else {
            style.color = "#FFFFFF";  // Normal for future words
        }

        html << muninn::HTMLFormatter::format_word(word.word, style) << " ";
    }

    return html.str();
}
```

### Option 4: Simple Classic Karaoke

```cpp
// Classic 3-color scheme (escape word.word with HTMLFormatter::escape if it can hold &, < or >)
for (const auto& word : seg.words) {
    if (time > word.end) {
        html << "<font color='#888888'>" << word.word << "</font> ";  // Past
    } else if (time >= word.start && time < word.end) {
        html << "<font color='#FFFF00'><b>" << word.word << "</b></font> ";  // Current
    } else {
        html << "<font color='#FFFFFF'>" << word.word << "</font> ";  // Future
    }
}
```

## Performance Tips

1. **Use a GPU** if you have one; the CPU works but is much slower.
2. **chunk_length_s** - Shorter windows mean lower latency but more decoding: every window is padded to 30 s before the encoder runs, so halving the window roughly doubles the work.
3. **Energy VAD** (the default) needs no model file; Silero needs `vad_model_path`.
4. **Keep only the last few segments** for display.

## Configuration Presets

### Low Latency (Gaming/Live Chat)

```cpp
muninn::StreamingOptions opts;
opts.chunk_length_s = 0.8f;   // 800ms chunks
opts.overlap_s = 0.2f;         // 200ms overlap
opts.enable_vad = true;        // Skip silence
opts.word_timestamps = true;
```

### High Quality (Podcasts/Interviews)

```cpp
muninn::StreamingOptions opts;
opts.chunk_length_s = 2.5f;   // 2.5s chunks (more context)
opts.overlap_s = 0.5f;         // 500ms overlap
opts.beam_size = 10;           // Higher beam = better quality
opts.word_timestamps = true;
```

### Defaults

```cpp
muninn::StreamingOptions opts;   // chunk_length_s 2.0, overlap_s 0.5,
                                 // Energy VAD on, word timestamps on
```

## Installing

Muninn is Windows-only. Copy the plugin DLL to OBS's `obs-plugins4bit` folder, and make Muninn's `bin\` DLLs (muninn, ctranslate2, onnxruntime, FFmpeg) plus the CUDA 13 and cuDNN 9 DLLs loadable from there. Windows has an older `onnxruntime.dll` of its own in System32, so don't rely on PATH for that one (see README_BINARIES.txt).

## Troubleshooting

| Issue | Solution |
|-------|----------|
| No captions appearing | Check model path, ensure text source exists |
| High latency | Reduce `chunk_length_s`, use faster model (turbo) |
| Words not highlighting | Ensure `word_timestamps = true` |
| Errors during a stream | Set an error callback (`set_error_callback`): failed windows are reported there and skipped |
| No GPU | Use device `"auto"`: it falls back to the CPU when CUDA isn't there or fails |

## Full Example

See `examples/karaoke_captions.cpp` for a complete standalone example.

## License

Business Source License 1.1, free for non-commercial use. Commercial use: craig@nordiqai.io
