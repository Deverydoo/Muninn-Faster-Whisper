# Subtitle Export (SRT/VTT)

Muninn provides automatic subtitle generation from transcription results, with support for SRT, VTT, and metadata JSON formats.

## Overview

Export transcriptions to standard subtitle formats:

```
Video transcription → Automatic subtitle files
video.mp4 → video.srt (universal)
         → video.vtt (web/streaming)
         → video_metadata.json (Loki Studio)
```

**Features:**
- SRT format (universal compatibility - all video players)
- VTT format (web standard with styling, speaker colors)
- Metadata JSON (Loki Studio format with full data preservation)
- Speaker label integration
- Automatic text splitting (proper line lengths)
- Multi-format export (SRT + VTT + JSON in one call)

## Quick Start

### Basic SRT Export

```cpp
#include <muninn/transcriber.h>
#include <muninn/subtitle_export.h>

// Transcribe video
muninn::Transcriber transcriber("models/faster-whisper-large-v3-turbo");
auto result = transcriber.transcribe("video.mp4");

// Export to SRT (automatic path: video.mp4 → video.srt)
muninn::SubtitleExporter exporter;
exporter.export_srt(result.segments, "video.mp4");
```

**Output:** `video.srt` in same directory as `video.mp4`

```srt
1
00:00:02,320 --> 00:00:06,720
All right, well, that was one hell of a party.

2
00:00:07,760 --> 00:00:08,680
Oh, boy.
```

## Use Cases

### 1. Video Editing (Premiere, DaVinci Resolve)

Export SRT for video editing software:

```cpp
muninn::SubtitleExportOptions options;
options.max_chars_per_line = 42;     // Standard subtitle width
options.max_lines = 2;               // Max 2 lines per subtitle
options.auto_split_long_text = true; // Auto-split at word boundaries

exporter.export_srt(result.segments, "video.mp4", options);
```

### 2. Gaming Commentary

Separate player commentary from game audio:

```cpp
// Transcribe multi-track audio
auto result = transcriber.transcribe("gameplay.mp4");

// Assign track labels
for (auto& seg : result.segments) {
    if (seg.track_id == 0) {
        seg.speaker_id = 0;
        seg.speaker_label = "Game Audio";
    } else {
        seg.speaker_id = 1;
        seg.speaker_label = "Player";
    }
}

// Export with speaker labels
muninn::SubtitleExportOptions options;
options.include_speakers = true;
options.speaker_format = "[{label}] {text}";

exporter.export_srt(result.segments, "gameplay.mp4", options);
```

**Output:**
```srt
1
00:00:15,020 --> 00:01:35,870
[Game Audio] Jump Gate, Unknown Sector.

2
00:01:21,320 --> 00:01:21,980
[Player] Hello there.
```

### 3. Podcast/Interview Transcripts

Multi-speaker subtitles with speaker labels:

```cpp
// Transcribe + diarize
auto result = transcriber.transcribe("podcast.mp3");

muninn::Diarizer diarizer("models/pyannote-embedding.onnx");
auto diarization = diarizer.diarize(audio_data, num_samples, 16000);

muninn::Diarizer::assign_speakers_to_segments(result.segments, diarization);

// Custom speaker names
std::map<int, std::string> names = {{0, "Host"}, {1, "Guest"}};
muninn::Diarizer::set_speaker_labels(diarization, names);

// Export with speaker labels
muninn::SubtitleExportOptions options;
options.include_speakers = true;
options.speaker_format = "[{label}] {text}";

exporter.export_srt(result.segments, "podcast.mp3", options);
```

**Output:**
```srt
1
00:00:15,020 --> 00:00:20,000
[Host] Welcome to the show!

2
00:00:20,500 --> 00:00:23,000
[Guest] Thanks for having me!
```

### 4. YouTube/Streaming (VTT with Colors)

Export VTT with speaker-specific colors:

```cpp
muninn::SubtitleExportOptions options;
options.format = muninn::SubtitleFormat::VTT;
options.vtt_include_speaker_colors = true;

// Define speaker colors
options.vtt_speaker_colors = {
    {0, "#00D9FF"},  // Cyan for host
    {1, "#FF6B9D"},  // Pink for guest
    {2, "#C9F04D"}   // Lime for guest 2
};

exporter.export_vtt(result.segments, "video.mp4", options);
```

**Output:** `video.vtt`
```vtt
WEBVTT

STYLE
::cue(v[voice="Speaker0"]) {
  color: #00D9FF;
}
::cue(v[voice="Speaker1"]) {
  color: #FF6B9D;
}

1
00:00:15.020 --> 00:00:20.000
<v Speaker0>Welcome to the show!</v>

2
00:00:20.500 --> 00:00:23.000
<v Speaker1>Thanks for having me!</v>
```

### 5. Loki Studio Metadata JSON

Preserve full transcription data for later editing:

```cpp
// Generate metadata JSON (same format as your existing workflow)
muninn::SubtitleMetadata::generate_metadata_json(
    result.segments,
    "video.mp4",
    "large-v3-turbo",
    result.language,
    result.duration
);
```

**Output:** `video_metadata.json`
```json
{
  "video_file": "video.mp4",
  "processed_date": 1763433895,
  "whisper_model": "large-v3-turbo",
  "engine": "muninn-faster-whisper",
  "device": "cuda",
  "transcriptions": {
    "channel_1": {
      "text": "Full transcript text...",
      "segments": [
        {
          "start": 2.32,
          "end": 6.72,
          "text": "All right, well, that was one hell of a party.",
          "words": [...]
        }
      ],
      "language": "en",
      "duration": 2760.21
    }
  }
}
```

## Configuration Options

### SubtitleExportOptions

```cpp
struct SubtitleExportOptions {
    // Format
    SubtitleFormat format = SubtitleFormat::SRT;  // SRT or VTT

    // Text formatting
    int max_chars_per_line = 42;           // Max characters per line
    int max_lines = 2;                     // Max lines per subtitle
    bool auto_split_long_text = true;      // Auto-split at spaces/punctuation

    // Speaker labels
    bool include_speakers = false;         // Include speaker labels
    std::string speaker_format = "[{label}] {text}";  // Format template

    // Timing
    float min_duration = 0.3f;             // Min subtitle duration (seconds)
    float max_duration = 7.0f;             // Max subtitle duration
    float gap_threshold = 0.1f;            // Merge segments closer than this

    // VTT-specific
    bool vtt_include_word_timestamps = false;       // Word-level <v> tags
    bool vtt_include_speaker_colors = false;        // Speaker colors
    std::map<int, std::string> vtt_speaker_colors;  // speaker_id → hex color

    // Output
    std::string output_path;               // Custom output path (optional)
    bool overwrite_existing = true;        // Overwrite if exists
};
```

### Speaker Format Templates

Control how speaker labels appear:

```cpp
// Format: [Speaker] text
options.speaker_format = "[{label}] {text}";

// Format: Speaker: text
options.speaker_format = "{label}: {text}";

// Format: (Speaker ID 0) text
options.speaker_format = "({label} ID {id}) {text}";

// Format: Just text with speaker prefix
options.speaker_format = "{label} - {text}";
```

**Variables:**
- `{label}` - Speaker label ("Host", "Speaker 0", etc.)
- `{id}` - Speaker ID number (0, 1, 2, ...)
- `{text}` - Segment text

## API Reference

### SubtitleExporter Class

```cpp
class SubtitleExporter {
    // High-level export
    std::string export_subtitles(const std::vector<Segment>& segments,
                                 const std::string& video_path,
                                 const SubtitleExportOptions& options);

    // Format-specific
    std::string export_srt(const std::vector<Segment>& segments,
                          const std::string& video_path,
                          const SubtitleExportOptions& options = {});

    std::string export_vtt(const std::vector<Segment>& segments,
                          const std::string& video_path,
                          const SubtitleExportOptions& options = {});

    // Utilities
    static std::string generate_output_path(const std::string& video_path,
                                           SubtitleFormat format);
    static std::string split_text(const std::string& text, int max_chars, int max_lines);
    static std::vector<SubtitleEntry> segments_to_entries(
        const std::vector<Segment>& segments,
        const SubtitleExportOptions& options);
};
```

### SubtitleMetadata Namespace

```cpp
namespace SubtitleMetadata {
    // Generate metadata JSON (Loki Studio format)
    std::string generate_metadata_json(
        const std::vector<Segment>& segments,
        const std::string& video_path,
        const std::string& whisper_model,
        const std::string& language,
        float duration);

    // Load segments from metadata JSON
    std::vector<Segment> load_from_metadata_json(const std::string& json_path);
}
```

## Advanced Usage

### Multi-Format Export

Export all formats at once:

```cpp
muninn::SubtitleExporter exporter;

// SRT (universal)
exporter.export_srt(result.segments, "video.mp4");

// VTT (web/streaming)
muninn::SubtitleExportOptions vtt_opts;
vtt_opts.format = muninn::SubtitleFormat::VTT;
exporter.export_vtt(result.segments, "video.mp4", vtt_opts);

// Metadata JSON (archival)
muninn::SubtitleMetadata::generate_metadata_json(
    result.segments, "video.mp4",
    "large-v3-turbo", result.language, result.duration);
```

**Result:**
- `video.srt` - Universal compatibility
- `video.vtt` - Web/streaming
- `video_metadata.json` - Full data preservation

### Load and Re-export

Load from metadata JSON and re-export with different options:

```cpp
// Load previous transcription
auto segments = muninn::SubtitleMetadata::load_from_metadata_json(
    "video_metadata.json");

// Re-export with new formatting
muninn::SubtitleExportOptions options;
options.include_speakers = true;
options.speaker_format = "{label}: {text}";
options.max_chars_per_line = 35;  // Shorter lines

muninn::SubtitleExporter exporter;
exporter.export_srt(segments, "video.mp4", options);
```

### Custom Text Splitting

Control how long text is split across lines:

```cpp
muninn::SubtitleExportOptions options;
options.max_chars_per_line = 35;       // Mobile-friendly
options.max_lines = 2;                 // Max 2 lines
options.auto_split_long_text = true;   // Auto-split at word boundaries
options.gap_threshold = 0.5f;          // Merge segments < 0.5s apart
```

**Before splitting:**
```
This is a very long sentence that exceeds the maximum character limit.
```

**After splitting:**
```
This is a very long sentence
that exceeds the maximum character limit.
```

## Subtitle Format Comparison

| Feature | SRT | VTT |
|---------|-----|-----|
| **Compatibility** | Universal (all players) | Web/modern players |
| **Styling** | ❌ | ✅ (CSS-like) |
| **Speaker colors** | ❌ | ✅ |
| **Word-level timing** | ❌ | ✅ |
| **File size** | Smaller | Slightly larger |
| **YouTube** | ✅ | ✅ |
| **Video editors** | ✅ | Limited |
| **Streaming** | ✅ | ✅ (preferred) |

**Recommendation:**
- **SRT**: Video editing, YouTube upload, universal compatibility
- **VTT**: Live streaming, web players, speaker differentiation
- **Both**: Export both for maximum flexibility

## Loki Studio Integration

### Automatic Workflow

Integrate subtitle export into Loki Studio transcription:

```cpp
// In Loki Studio transcription worker:
void process_video(const std::string& video_path) {
    // 1. Transcribe
    muninn::Transcriber transcriber("models/faster-whisper-large-v3-turbo");

    muninn::TranscribeOptions options;
    options.word_timestamps = true;

    auto result = transcriber.transcribe(video_path, options);

    // 2. Export metadata JSON (existing Loki Studio format)
    muninn::SubtitleMetadata::generate_metadata_json(
        result.segments,
        video_path,
        "large-v3-turbo",
        result.language,
        result.duration
    );
    // Creates: video_metadata.json

    // 3. Export SRT (new!)
    muninn::SubtitleExporter exporter;
    exporter.export_srt(result.segments, video_path);
    // Creates: video.srt

    // Done! Both files in same directory as video
}
```

**Result:**
- `video.mp4` - Original video
- `video_metadata.json` - Full transcription data (for Loki Studio)
- `video.srt` - Ready-to-use subtitles (for video editing)

### Metadata Window Integration

Use metadata JSON in Loki Studio metadata creator:

```cpp
// Load existing metadata
auto segments = muninn::SubtitleMetadata::load_from_metadata_json(
    "video_metadata.json");

// User edits segments in GUI...
// segments[0].text = "Edited text...";

// Re-export with edits
muninn::SubtitleExporter exporter;
exporter.export_srt(segments, "video.mp4");
```

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Subtitles too long | Decrease `max_chars_per_line` (try 35) |
| Too many subtitle breaks | Increase `gap_threshold` (try 0.5) |
| Speaker labels wrong | Re-run diarization, adjust `clustering_threshold` |
| VTT not showing colors | Ensure `vtt_include_speaker_colors = true` |
| Metadata JSON format wrong | Use `SubtitleMetadata::generate_metadata_json()` |

## See Also

- [Streaming API Guide](STREAMING_API.md)
- [Speaker Diarization](SPEAKER_DIARIZATION.md)
- [OBS Integration](OBS_KARAOKE_CAPTIONS.md)
- [Integration Guide](INTEGRATION.md)

---

**Professional subtitle export made easy!** Generate publication-ready SRT/VTT files directly from transcription results.
