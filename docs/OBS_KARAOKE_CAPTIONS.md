# OBS Karaoke Captions Plugin Guide

Build an OBS plugin with word-by-word highlighting using Muninn's streaming API.

## Features

✅ **Real-time captions** - 500ms-2s latency
✅ **Word highlighting** - Karaoke-style word-by-word emphasis
✅ **Customizable styles** - Colors, fonts, sizes, positions
✅ **Auto language detection** - 99+ languages supported
✅ **Export SRT** - Save captions with word timing

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

# Add Muninn
target_link_libraries(obs-muninn-captions
    PRIVATE
        OBS::libobs
        muninn.lib
        ctranslate2.lib
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
    float stream_time;

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

    // Push to Muninn (thread-safe)
    data->transcriber->push_audio(samples, frames, 48000, 1);

    // Update stream time
    data->stream_time += static_cast<float>(frames) / 48000.0f;

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

    // Karaoke mode - build HTML/Rich Text with word highlighting
    std::string formatted_text = build_karaoke_text(seg, data->stream_time);

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

    // Build HTML with color coding
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

    // Export button
    obs_properties_add_button(
        props,
        "export_srt",
        "Export SRT Subtitles",
        export_srt_callback
    );

    return props;
}
```

### 7. Export SRT with Word Timestamps

```cpp
static bool export_srt_callback(obs_properties_t* props, obs_property_t* property, void* vdata) {
    auto* data = static_cast<muninn_caption_data*>(vdata);

    // Get save path from user
    const char* path = obs_frontend_get_save_path("srt", "SRT Files (*.srt)");
    if (!path) {
        return false;
    }

    // Get all segments
    auto all_segments = data->transcriber->get_all_segments();

    // Export to SRT
    std::ofstream srt(path);
    int index = 1;

    for (const auto& seg : all_segments) {
        srt << index++ << "\n";
        srt << format_srt_time(seg.start) << " --> " << format_srt_time(seg.end) << "\n";
        srt << seg.text << "\n\n";

        // Optionally: export word-level SRT (one word per subtitle)
        if (!seg.words.empty()) {
            for (const auto& word : seg.words) {
                srt << index++ << "\n";
                srt << format_srt_time(word.start) << " --> " << format_srt_time(word.end) << "\n";
                srt << word.word << "\n\n";
            }
        }
    }

    blog(LOG_INFO, "[Muninn] Exported SRT to: %s", path);
    return true;
}
```

## Styling Options

Muninn provides built-in styling presets with `word_styling.h` for dynamic, intensity-based word highlighting.

### Option 1: Intensity-Based Coloring (Recommended)

Words change color based on volume/emphasis - whispered words are dim, shouted words are bright red:

```cpp
#include <muninn/word_styling.h>

std::string build_karaoke_html(const muninn::Segment& seg, float time) {
    // Built-in formatter with intensity-based colors:
    // - Whispered (< 20% volume): dim gray
    // - Quiet (20-40%): light gray
    // - Normal (40-70%): white/yellow
    // - Emphasized (70-90%): bright yellow
    // - Shouted (> 90%): bright red
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

### Option 3: Combined Intensity + Confidence

```cpp
std::string build_combined_html(const muninn::Segment& seg, float time) {
    // Uses intensity for color, confidence for styling effects
    // Low confidence words are italicized
    return muninn::HTMLFormatter::build_combined_html(seg, time);
}
```

### Option 4: Custom Manual Styling

For full control, use the WordStyle API:

```cpp
std::string build_custom_html(const muninn::Segment& seg, float time) {
    std::ostringstream html;

    for (const auto& word : seg.words) {
        bool is_current = (time >= word.start && time < word.end);

        muninn::WordStyle style;

        if (is_current) {
            // Custom style for current word based on emphasis
            switch (word.emphasis) {
                case muninn::EmphasisLevel::VeryHigh:
                    style.color = "#FF0000";      // Red for shouted
                    style.bold = true;
                    style.size_offset = 2;
                    break;
                case muninn::EmphasisLevel::High:
                    style.color = "#FFFF00";      // Yellow for emphasized
                    style.bold = true;
                    style.size_offset = 1;
                    break;
                default:
                    style.color = "#FFFFFF";      // White for normal
                    style.bold = true;
                    break;
            }
        } else if (time > word.end) {
            style.color = "#888888";  // Dimmed for past words
            style.bold = false;
        } else {
            style.color = "#FFFFFF";  // Normal for future words
            style.bold = false;
        }

        html << muninn::HTMLFormatter::format_word(word.word, style) << " ";
    }

    return html.str();
}
```

### Option 5: Simple Classic Karaoke

```cpp
// Classic 3-color scheme (no intensity)
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

1. **Use GPU** - CUDA gives 10-20x speedup
2. **Adjust chunk_length_s** - Lower = lower latency, higher CPU
3. **Use Energy VAD** - Fast, no ONNX dependency
4. **Limit visible segments** - Keep only last 3-5 for display
5. **30 FPS rendering** - Smooth word transitions without overhead

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

### Balanced (Default)

```cpp
muninn::StreamingOptions opts;
opts.chunk_length_s = 1.5f;   // 1.5s latency
opts.overlap_s = 0.3f;
opts.enable_vad = true;
opts.word_timestamps = true;
```

## Testing

```bash
# Build plugin
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/obs-studio
make

# Install to OBS
cp obs-muninn-captions.dll ~/.config/obs-studio/plugins/

# Run OBS and test
obs --verbose --log-level debug
```

## Troubleshooting

| Issue | Solution |
|-------|----------|
| No captions appearing | Check model path, ensure text source exists |
| High latency | Reduce `chunk_length_s`, use faster model (turbo) |
| Words not highlighting | Ensure `word_timestamps = true` |
| Audio choppy | Push smaller chunks (1024 samples) |
| CUDA errors | Fallback to CPU (`device = "cpu"`) |

## Full Example

See `examples/karaoke_captions.cpp` for a complete standalone example.

## License

Business Source License 1.1
Commercial use: craig@nordiqai.io
