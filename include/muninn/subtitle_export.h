#pragma once

#include "export.h"
#include "types.h"
#include <string>
#include <vector>
#include <map>
#include <filesystem>

namespace muninn {

/**
 * @brief Subtitle format types
 */
enum class SubtitleFormat {
    SRT,        // SubRip (.srt) - universal compatibility
    VTT,        // WebVTT (.vtt) - web standard with styling support
    ASS         // Advanced SubStation Alpha (.ass) - advanced styling (future)
};

/**
 * @brief Subtitle export configuration
 */
struct SubtitleExportOptions {
    // ═══════════════════════════════════════════════════════════
    // Format Options
    // ═══════════════════════════════════════════════════════════
    SubtitleFormat format = SubtitleFormat::SRT;

    // ═══════════════════════════════════════════════════════════
    // Text Formatting
    // ═══════════════════════════════════════════════════════════
    int max_chars_per_line = 42;           // Max characters per line
    int max_lines = 2;                     // Max lines per subtitle
    bool auto_split_long_text = true;      // Auto-split at punctuation/spaces

    // ═══════════════════════════════════════════════════════════
    // Speaker Labels
    // ═══════════════════════════════════════════════════════════
    bool include_speakers = false;         // Include speaker labels
    std::string speaker_format = "[{label}] {text}";  // Format: {label}, {text}, {id}
    std::map<int, std::string> speaker_names;  // Custom speaker names (speaker_id -> name)

    // ═══════════════════════════════════════════════════════════
    // Timing
    // ═══════════════════════════════════════════════════════════
    float min_duration = 0.3f;             // Minimum subtitle duration (seconds)
    float max_duration = 7.0f;             // Maximum subtitle duration (seconds)
    float gap_threshold = 0.1f;            // Merge segments closer than this (seconds)

    // ═══════════════════════════════════════════════════════════
    // VTT-specific (WebVTT with styling)
    // ═══════════════════════════════════════════════════════════
    bool vtt_include_word_timestamps = false;  // Word-level <v> tags
    bool vtt_include_speaker_colors = false;   // Speaker-specific colors
    std::map<int, std::string> vtt_speaker_colors;  // speaker_id -> hex color

    // VTT styling options
    bool vtt_include_confidence_styling = false;  // Color by confidence
    bool vtt_include_intensity_styling = false;   // Bold/size by intensity

    // ═══════════════════════════════════════════════════════════
    // Output
    // ═══════════════════════════════════════════════════════════
    std::string output_path;               // Output file path (empty = auto-generate)
    bool overwrite_existing = true;        // Overwrite if file exists
};

/**
 * @brief Subtitle entry
 */
struct SubtitleEntry {
    int index;                             // Subtitle number (1-based)
    float start;                           // Start time (seconds)
    float end;                             // End time (seconds)
    std::string text;                      // Subtitle text

    // Optional metadata
    int speaker_id = -1;                   // Speaker ID (-1 = no speaker)
    std::string speaker_label;             // Speaker label
    std::vector<Word> words;               // Word-level timing (for VTT)

    SubtitleEntry() : index(0), start(0.0f), end(0.0f) {}
};

/**
 * @brief Subtitle Exporter
 *
 * Export transcription results to standard subtitle formats (SRT, VTT, ASS).
 *
 * Features:
 * - SRT: Universal compatibility (all video players)
 * - VTT: Web standard with styling, word-level timing, speaker colors
 * - Auto-splitting long text to fit subtitle guidelines
 * - Speaker label integration
 * - Automatic output path generation (same directory as video)
 *
 * Example usage:
 * @code
 * // Export to SRT (basic)
 * muninn::SubtitleExporter exporter;
 * exporter.export_srt(segments, "video.mp4");  // Creates video.srt
 *
 * // Export to VTT with speakers
 * muninn::SubtitleExportOptions options;
 * options.format = muninn::SubtitleFormat::VTT;
 * options.include_speakers = true;
 * options.vtt_include_speaker_colors = true;
 * exporter.export_subtitles(segments, "video.mp4", options);
 * @endcode
 */
class MUNINN_API SubtitleExporter {
public:
    SubtitleExporter() = default;
    ~SubtitleExporter() = default;

    // ═══════════════════════════════════════════════════════════
    // High-level Export (Auto-detect format from extension)
    // ═══════════════════════════════════════════════════════════

    /**
     * @brief Export subtitles with custom options
     *
     * Output path auto-generated from video_path if not specified in options.
     *
     * @param segments Transcription segments
     * @param video_path Original video file path (for auto-naming output)
     * @param options Export configuration
     * @return Output file path
     */
    std::string export_subtitles(const std::vector<Segment>& segments,
                                 const std::string& video_path,
                                 const SubtitleExportOptions& options = SubtitleExportOptions());

    // ═══════════════════════════════════════════════════════════
    // Format-specific Export
    // ═══════════════════════════════════════════════════════════

    /**
     * @brief Export to SRT format (SubRip)
     *
     * Creates <video_name>.srt in same directory as video_path.
     *
     * @param segments Transcription segments
     * @param video_path Video file path
     * @param options Export options
     * @return Output SRT file path
     */
    std::string export_srt(const std::vector<Segment>& segments,
                          const std::string& video_path,
                          const SubtitleExportOptions& options = SubtitleExportOptions());

    /**
     * @brief Export to VTT format (WebVTT)
     *
     * Creates <video_name>.vtt with optional styling and word-level timing.
     *
     * @param segments Transcription segments
     * @param video_path Video file path
     * @param options Export options
     * @return Output VTT file path
     */
    std::string export_vtt(const std::vector<Segment>& segments,
                          const std::string& video_path,
                          const SubtitleExportOptions& options = SubtitleExportOptions());

    // ═══════════════════════════════════════════════════════════
    // Low-level Formatting
    // ═══════════════════════════════════════════════════════════

    /**
     * @brief Convert segments to subtitle entries
     *
     * Applies text splitting, timing adjustments, speaker formatting.
     *
     * @param segments Transcription segments
     * @param options Export options
     * @return Formatted subtitle entries
     */
    static std::vector<SubtitleEntry> segments_to_entries(
        const std::vector<Segment>& segments,
        const SubtitleExportOptions& options);

    /**
     * @brief Format subtitle entry to SRT format
     *
     * @param entry Subtitle entry
     * @return SRT-formatted string
     */
    static std::string format_srt_entry(const SubtitleEntry& entry);

    /**
     * @brief Format subtitle entry to VTT format
     *
     * @param entry Subtitle entry
     * @param options Export options (for styling)
     * @return VTT-formatted string (cue block)
     */
    static std::string format_vtt_entry(const SubtitleEntry& entry,
                                       const SubtitleExportOptions& options);

    // ═══════════════════════════════════════════════════════════
    // Utilities
    // ═══════════════════════════════════════════════════════════

    /**
     * @brief Generate output path from video path
     *
     * @param video_path Video file path
     * @param format Subtitle format
     * @return Output subtitle file path (same directory, different extension)
     */
    static std::string generate_output_path(const std::string& video_path,
                                           SubtitleFormat format);

    /**
     * @brief Split long text into multiple lines
     *
     * @param text Text to split
     * @param max_chars_per_line Max characters per line
     * @param max_lines Max number of lines
     * @return Split text with newlines
     */
    static std::string split_text(const std::string& text,
                                  int max_chars_per_line,
                                  int max_lines = 2);

    /**
     * @brief Format time for SRT (HH:MM:SS,mmm)
     *
     * @param seconds Time in seconds
     * @return SRT-formatted time string
     */
    static std::string format_srt_timestamp(float seconds);

    /**
     * @brief Format time for VTT (HH:MM:SS.mmm)
     *
     * @param seconds Time in seconds
     * @return VTT-formatted time string
     */
    static std::string format_vtt_timestamp(float seconds);

    /**
     * @brief Apply speaker format string
     *
     * Replaces {label}, {id}, {text} in format string.
     *
     * @param format_string Format template
     * @param speaker_id Speaker ID
     * @param speaker_label Speaker label
     * @param text Segment text
     * @return Formatted string
     */
    static std::string apply_speaker_format(const std::string& format_string,
                                           int speaker_id,
                                           const std::string& speaker_label,
                                           const std::string& text);
};

/**
 * @brief Helper functions for subtitle metadata generation
 */
namespace SubtitleMetadata {
    /**
     * @brief Generate JSON metadata (Loki Studio format)
     *
     * Creates <video_name>_metadata.json with transcription data.
     *
     * @param segments Transcription segments
     * @param video_path Video file path
     * @param whisper_model Model name used
     * @param language Detected language
     * @param duration Total audio duration
     * @return Metadata JSON file path
     */
    MUNINN_API std::string generate_metadata_json(
        const std::vector<Segment>& segments,
        const std::string& video_path,
        const std::string& whisper_model,
        const std::string& language,
        float duration);

    /**
     * @brief Load segments from metadata JSON
     *
     * @param metadata_json_path Path to metadata JSON file
     * @return Segments loaded from JSON
     */
    MUNINN_API std::vector<Segment> load_from_metadata_json(
        const std::string& metadata_json_path);
}

} // namespace muninn
