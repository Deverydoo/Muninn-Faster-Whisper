#include "muninn/subtitle_export.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <chrono>

namespace muninn {

// ═══════════════════════════════════════════════════════════
// High-level Export
// ═══════════════════════════════════════════════════════════

std::string SubtitleExporter::export_subtitles(const std::vector<Segment>& segments,
                                              const std::string& video_path,
                                              const SubtitleExportOptions& options) {
    switch (options.format) {
        case SubtitleFormat::SRT:
            return export_srt(segments, video_path, options);
        case SubtitleFormat::VTT:
            return export_vtt(segments, video_path, options);
        default:
            throw std::runtime_error("Unsupported subtitle format");
    }
}

std::string SubtitleExporter::export_srt(const std::vector<Segment>& segments,
                                        const std::string& video_path,
                                        const SubtitleExportOptions& options) {
    // Generate output path
    std::string output_path = options.output_path.empty() ?
        generate_output_path(video_path, SubtitleFormat::SRT) :
        options.output_path;

    // Convert segments to entries
    // Use specialized modes for better timing when enabled
    std::vector<SubtitleEntry> entries;
    if (options.enable_karaoke_mode) {
        // AAA English karaoke-style captioning
        entries = build_karaoke_captions(segments, options);
    } else if (options.enable_thai_mode) {
        // Thai-specific segmentation (no spaces, like Japanese)
        entries = segment_thai(segments, options);
    } else if (options.enable_vietnamese_mode) {
        // Vietnamese-specific segmentation
        entries = segment_vietnamese(segments, options);
    } else if (options.enable_indonesian_mode) {
        // Indonesian-specific segmentation
        entries = segment_indonesian(segments, options);
    } else if (options.enable_hindi_mode) {
        // Hindi/Devanagari-specific segmentation
        entries = segment_hindi(segments, options);
    } else if (options.enable_arabic_mode) {
        // Arabic-specific segmentation (RTL, ligature-safe)
        entries = segment_arabic(segments, options);
    } else if (options.enable_spanish_mode) {
        // Spanish/Portuguese-specific segmentation
        entries = segment_spanish(segments, options);
    } else if (options.enable_russian_mode) {
        // Russian-specific segmentation with CPS control and preposition rules
        entries = segment_russian(segments, options);
    } else if (options.enable_chinese_mode) {
        // Chinese-specific segmentation with CPS control
        entries = segment_chinese(segments, options);
    } else if (options.enable_korean_mode) {
        // Korean-specific segmentation with geumchik rules
        entries = segment_korean(segments, options);
    } else if (options.enable_japanese_mode) {
        // Japanese-specific segmentation
        entries = segment_japanese(segments, options);
    } else {
        // Standard segment-based subtitles
        entries = segments_to_entries(segments, options);
    }

    // Write SRT file
    std::ofstream file(output_path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to create SRT file: " + output_path);
    }

    for (const auto& entry : entries) {
        file << format_srt_entry(entry);
        file << "\n";  // Blank line between entries
    }

    file.close();
    return output_path;
}

std::string SubtitleExporter::export_vtt(const std::vector<Segment>& segments,
                                        const std::string& video_path,
                                        const SubtitleExportOptions& options) {
    // Generate output path
    std::string output_path = options.output_path.empty() ?
        generate_output_path(video_path, SubtitleFormat::VTT) :
        options.output_path;

    // Convert segments to entries
    // Use specialized modes for better timing when enabled
    std::vector<SubtitleEntry> entries;
    if (options.enable_karaoke_mode) {
        // AAA English karaoke-style captioning
        entries = build_karaoke_captions(segments, options);
    } else if (options.enable_thai_mode) {
        // Thai-specific segmentation (no spaces, like Japanese)
        entries = segment_thai(segments, options);
    } else if (options.enable_vietnamese_mode) {
        // Vietnamese-specific segmentation
        entries = segment_vietnamese(segments, options);
    } else if (options.enable_indonesian_mode) {
        // Indonesian-specific segmentation
        entries = segment_indonesian(segments, options);
    } else if (options.enable_hindi_mode) {
        // Hindi/Devanagari-specific segmentation
        entries = segment_hindi(segments, options);
    } else if (options.enable_arabic_mode) {
        // Arabic-specific segmentation (RTL, ligature-safe)
        entries = segment_arabic(segments, options);
    } else if (options.enable_spanish_mode) {
        // Spanish/Portuguese-specific segmentation
        entries = segment_spanish(segments, options);
    } else if (options.enable_russian_mode) {
        // Russian-specific segmentation with CPS control and preposition rules
        entries = segment_russian(segments, options);
    } else if (options.enable_chinese_mode) {
        // Chinese-specific segmentation with CPS control
        entries = segment_chinese(segments, options);
    } else if (options.enable_korean_mode) {
        // Korean-specific segmentation with geumchik rules
        entries = segment_korean(segments, options);
    } else if (options.enable_japanese_mode) {
        // Japanese-specific segmentation
        entries = segment_japanese(segments, options);
    } else {
        // Standard segment-based subtitles
        entries = segments_to_entries(segments, options);
    }

    // Write VTT file
    std::ofstream file(output_path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to create VTT file: " + output_path);
    }

    // VTT header
    file << "WEBVTT\n\n";

    // Optional: Add styling section
    if (options.vtt_include_speaker_colors && !options.vtt_speaker_colors.empty()) {
        file << "STYLE\n";
        file << "::cue {\n";
        file << "  background-color: rgba(0, 0, 0, 0.8);\n";
        file << "}\n\n";

        for (const auto& [speaker_id, color] : options.vtt_speaker_colors) {
            file << "::cue(v[voice=\"Speaker" << speaker_id << "\"]) {\n";
            file << "  color: " << color << ";\n";
            file << "}\n\n";
        }
    }

    // Write cues
    for (const auto& entry : entries) {
        file << format_vtt_entry(entry, options);
        file << "\n";  // Blank line between cues
    }

    file.close();
    return output_path;
}

// ═══════════════════════════════════════════════════════════
// Segment Conversion
// ═══════════════════════════════════════════════════════════

std::vector<SubtitleEntry> SubtitleExporter::segments_to_entries(
    const std::vector<Segment>& segments,
    const SubtitleExportOptions& options) {

    std::vector<SubtitleEntry> entries;
    int entry_index = 1;

    for (const auto& seg : segments) {
        SubtitleEntry entry;
        entry.index = entry_index++;
        entry.start = seg.start;
        entry.end = seg.end;
        entry.speaker_id = seg.speaker_id;
        entry.speaker_label = seg.speaker_label;
        entry.words = seg.words;

        // Apply speaker formatting
        if (options.include_speakers && seg.speaker_id >= 0) {
            std::string speaker_label = seg.speaker_label.empty() ?
                ("Speaker " + std::to_string(seg.speaker_id)) : seg.speaker_label;

            entry.text = apply_speaker_format(
                options.speaker_format,
                seg.speaker_id,
                speaker_label,
                seg.text
            );
        } else {
            entry.text = seg.text;
        }

        // Split long text if needed
        if (options.auto_split_long_text) {
            entry.text = split_text(entry.text,
                                   options.max_chars_per_line,
                                   options.max_lines);
        }

        // Apply timing constraints
        float duration = entry.end - entry.start;
        if (duration < options.min_duration) {
            entry.end = entry.start + options.min_duration;
        }
        if (duration > options.max_duration) {
            entry.end = entry.start + options.max_duration;
        }

        entries.push_back(entry);
    }

    // Merge close segments if requested
    if (options.gap_threshold > 0.0f && entries.size() > 1) {
        std::vector<SubtitleEntry> merged;
        merged.push_back(entries[0]);

        for (size_t i = 1; i < entries.size(); ++i) {
            auto& last = merged.back();
            const auto& curr = entries[i];

            float gap = curr.start - last.end;

            // Merge if gap is small and same speaker
            if (gap < options.gap_threshold &&
                curr.speaker_id == last.speaker_id) {
                last.end = curr.end;
                last.text += " " + curr.text;

                // Re-split if needed
                if (options.auto_split_long_text) {
                    last.text = split_text(last.text,
                                          options.max_chars_per_line,
                                          options.max_lines);
                }
            } else {
                merged.push_back(curr);
            }
        }

        // Re-index
        for (size_t i = 0; i < merged.size(); ++i) {
            merged[i].index = i + 1;
        }

        entries = merged;
    }

    return entries;
}

// ═══════════════════════════════════════════════════════════
// SRT Formatting
// ═══════════════════════════════════════════════════════════

std::string SubtitleExporter::format_srt_entry(const SubtitleEntry& entry) {
    std::ostringstream oss;

    // Index
    oss << entry.index << "\n";

    // Timestamps
    oss << format_srt_timestamp(entry.start) << " --> "
        << format_srt_timestamp(entry.end) << "\n";

    // Text
    oss << entry.text << "\n";

    return oss.str();
}

std::string SubtitleExporter::format_srt_timestamp(float seconds) {
    int hours = static_cast<int>(seconds / 3600);
    int minutes = static_cast<int>((seconds - hours * 3600) / 60);
    int secs = static_cast<int>(seconds - hours * 3600 - minutes * 60);
    int millis = static_cast<int>((seconds - static_cast<int>(seconds)) * 1000);

    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(2) << hours << ":"
        << std::setw(2) << minutes << ":"
        << std::setw(2) << secs << ","
        << std::setw(3) << millis;

    return oss.str();
}

// ═══════════════════════════════════════════════════════════
// VTT Formatting
// ═══════════════════════════════════════════════════════════

std::string SubtitleExporter::format_vtt_entry(const SubtitleEntry& entry,
                                              const SubtitleExportOptions& options) {
    std::ostringstream oss;

    // Cue identifier (optional)
    oss << entry.index << "\n";

    // Timestamps
    oss << format_vtt_timestamp(entry.start) << " --> "
        << format_vtt_timestamp(entry.end) << "\n";

    // Text with optional voice tags
    if (options.vtt_include_word_timestamps && !entry.words.empty()) {
        // Word-level timing with <v> tags
        std::string voice_tag = entry.speaker_id >= 0 ?
            ("Speaker" + std::to_string(entry.speaker_id)) : "Default";

        oss << "<v " << voice_tag << ">";

        for (const auto& word : entry.words) {
            oss << word.word;
            if (&word != &entry.words.back()) {
                oss << " ";
            }
        }

        oss << "</v>\n";

    } else if (options.vtt_include_speaker_colors && entry.speaker_id >= 0) {
        // Speaker voice tag (for styling)
        std::string voice_tag = "Speaker" + std::to_string(entry.speaker_id);
        oss << "<v " << voice_tag << ">" << entry.text << "</v>\n";

    } else {
        // Plain text
        oss << entry.text << "\n";
    }

    return oss.str();
}

std::string SubtitleExporter::format_vtt_timestamp(float seconds) {
    int hours = static_cast<int>(seconds / 3600);
    int minutes = static_cast<int>((seconds - hours * 3600) / 60);
    int secs = static_cast<int>(seconds - hours * 3600 - minutes * 60);
    int millis = static_cast<int>((seconds - static_cast<int>(seconds)) * 1000);

    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(2) << hours << ":"
        << std::setw(2) << minutes << ":"
        << std::setw(2) << secs << "."
        << std::setw(3) << millis;

    return oss.str();
}

// ═══════════════════════════════════════════════════════════
// Utilities
// ═══════════════════════════════════════════════════════════

std::string SubtitleExporter::generate_output_path(const std::string& video_path,
                                                  SubtitleFormat format) {
    std::filesystem::path video(video_path);
    std::filesystem::path output = video.parent_path();

    std::string stem = video.stem().string();

    switch (format) {
        case SubtitleFormat::SRT:
            output /= stem + ".srt";
            break;
        case SubtitleFormat::VTT:
            output /= stem + ".vtt";
            break;
        default:
            output /= stem + ".sub";
    }

    return output.string();
}

std::string SubtitleExporter::split_text(const std::string& text,
                                        int max_chars_per_line,
                                        int max_lines) {
    if (text.length() <= static_cast<size_t>(max_chars_per_line)) {
        return text;
    }

    std::vector<std::string> lines;
    std::string current_line;
    std::istringstream words(text);
    std::string word;

    while (words >> word) {
        if (current_line.empty()) {
            current_line = word;
        } else if (current_line.length() + word.length() + 1 <= static_cast<size_t>(max_chars_per_line)) {
            current_line += " " + word;
        } else {
            lines.push_back(current_line);
            current_line = word;

            if (lines.size() >= static_cast<size_t>(max_lines)) {
                break;  // Max lines reached
            }
        }
    }

    if (!current_line.empty() && lines.size() < static_cast<size_t>(max_lines)) {
        lines.push_back(current_line);
    }

    // Join with newlines
    std::string result;
    for (size_t i = 0; i < lines.size(); ++i) {
        result += lines[i];
        if (i < lines.size() - 1) {
            result += "\n";
        }
    }

    return result;
}

std::string SubtitleExporter::apply_speaker_format(const std::string& format_string,
                                                  int speaker_id,
                                                  const std::string& speaker_label,
                                                  const std::string& text) {
    std::string result = format_string;

    // Replace {label}
    size_t pos = result.find("{label}");
    if (pos != std::string::npos) {
        result.replace(pos, 7, speaker_label);
    }

    // Replace {id}
    pos = result.find("{id}");
    if (pos != std::string::npos) {
        result.replace(pos, 4, std::to_string(speaker_id));
    }

    // Replace {text}
    pos = result.find("{text}");
    if (pos != std::string::npos) {
        result.replace(pos, 6, text);
    }

    return result;
}

// ═══════════════════════════════════════════════════════════
// Japanese-Specific Processing
// ═══════════════════════════════════════════════════════════

// Helper: Get UTF-8 character length
static size_t utf8_char_length(unsigned char c) {
    if ((c & 0x80) == 0x00) return 1;      // ASCII
    if ((c & 0xE0) == 0xC0) return 2;      // 2-byte UTF-8
    if ((c & 0xF0) == 0xE0) return 3;      // 3-byte UTF-8 (Japanese)
    if ((c & 0xF8) == 0xF0) return 4;      // 4-byte UTF-8 (emoji)
    return 1;  // Invalid, treat as single byte
}

// Helper: Get last UTF-8 character from string
static std::string get_last_utf8_char(const std::string& text) {
    if (text.empty()) return "";

    size_t i = text.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) {
        --i;  // Skip continuation bytes
    }

    return text.substr(i);
}

size_t SubtitleExporter::count_japanese_chars(const std::string& text) {
    size_t count = 0;
    size_t i = 0;

    while (i < text.size()) {
        size_t char_len = utf8_char_length(static_cast<unsigned char>(text[i]));

        if (i + char_len <= text.size()) {
            std::string ch = text.substr(i, char_len);

            // Skip spaces and newlines
            if (ch != " " && ch != "　" && ch != "\n" && ch != "\r") {
                count++;
            }
        }

        i += char_len;
    }

    return count;
}

bool SubtitleExporter::ends_with_japanese_sentence_end(const std::string& text) {
    std::string last_char = get_last_utf8_char(text);

    // Sentence-ending punctuation: 。！？…
    return last_char == "。" || last_char == "！" || last_char == "？" ||
           last_char == "…" || last_char == "!" || last_char == "?";
}

bool SubtitleExporter::ends_with_japanese_comma(const std::string& text) {
    std::string last_char = get_last_utf8_char(text);

    // Japanese comma: 、
    return last_char == "、";
}

std::string SubtitleExporter::split_japanese_text(const std::string& text,
                                                  int max_chars,
                                                  int max_lines) {
    if (count_japanese_chars(text) <= static_cast<size_t>(max_chars)) {
        return text;
    }

    std::vector<std::string> lines;
    std::string current_line;
    size_t current_char_count = 0;
    size_t i = 0;

    while (i < text.size() && static_cast<int>(lines.size()) < max_lines) {
        size_t char_len = utf8_char_length(static_cast<unsigned char>(text[i]));

        if (i + char_len > text.size()) break;

        std::string ch = text.substr(i, char_len);

        // Check for natural break points (punctuation)
        bool is_break_point = (ch == "。" || ch == "、" || ch == "！" ||
                               ch == "？" || ch == "…" || ch == "," ||
                               ch == "." || ch == "!" || ch == "?");

        current_line += ch;
        current_char_count++;

        // Break conditions
        bool should_break = false;
        if (current_char_count >= static_cast<size_t>(max_chars)) {
            should_break = true;
        } else if (is_break_point && current_char_count >= static_cast<size_t>(max_chars / 2)) {
            // Break at punctuation if we're past halfway
            should_break = true;
        }

        if (should_break) {
            lines.push_back(current_line);
            current_line.clear();
            current_char_count = 0;
        }

        i += char_len;
    }

    // Add remaining text
    if (!current_line.empty() && static_cast<int>(lines.size()) < max_lines) {
        lines.push_back(current_line);
    }

    // Join with newlines
    std::string result;
    for (size_t j = 0; j < lines.size(); ++j) {
        result += lines[j];
        if (j < lines.size() - 1) {
            result += "\n";
        }
    }

    return result;
}

std::vector<SubtitleEntry> SubtitleExporter::segment_japanese(
    const std::vector<Segment>& segments,
    const SubtitleExportOptions& options) {

    std::vector<SubtitleEntry> entries;

    // Collect all words from all segments with timing
    std::vector<Word> all_words;
    for (const auto& seg : segments) {
        for (const auto& word : seg.words) {
            all_words.push_back(word);
        }
    }

    if (all_words.empty()) {
        // No word-level timestamps - fall back to segment-based
        return segments_to_entries(segments, options);
    }

    // Build subtitles from words using Japanese rules
    int entry_index = 1;
    std::string current_text;
    float current_start = -1.0f;
    float current_end = 0.0f;
    float last_word_end = 0.0f;

    auto flush_entry = [&]() {
        if (!current_text.empty() && current_start >= 0) {
            SubtitleEntry entry;
            entry.index = entry_index++;
            entry.start = current_start;
            entry.end = current_end;

            // Split text if too long for display
            if (count_japanese_chars(current_text) > static_cast<size_t>(options.max_chars_per_line)) {
                entry.text = split_japanese_text(current_text,
                                                 options.japanese_max_chars,
                                                 options.max_lines);
            } else {
                entry.text = current_text;
            }

            entries.push_back(entry);

            current_text.clear();
            current_start = -1.0f;
        }
    };

    for (size_t i = 0; i < all_words.size(); ++i) {
        const Word& word = all_words[i];

        // Check for pause between words (break point)
        if (current_start >= 0 && !current_text.empty()) {
            float pause_ms = (word.start - last_word_end) * 1000.0f;

            if (pause_ms > options.japanese_pause_threshold_ms) {
                flush_entry();
            }
        }

        // Start new subtitle if needed
        if (current_start < 0) {
            current_start = word.start;
        }

        // Add word to current subtitle
        current_text += word.word;
        current_end = word.end;
        last_word_end = word.end;

        // Check break conditions
        bool should_break = false;

        // 1. Duration limit
        float duration = current_end - current_start;
        if (duration > options.japanese_max_duration) {
            should_break = true;
        }

        // 2. Character count limit
        if (count_japanese_chars(current_text) >= static_cast<size_t>(options.japanese_max_chars)) {
            should_break = true;
        }

        // 3. Sentence-ending punctuation
        if (options.japanese_break_on_sentence && ends_with_japanese_sentence_end(current_text)) {
            should_break = true;
        }

        // 4. Comma (soft break, only if text is getting long)
        if (options.japanese_break_on_comma &&
            ends_with_japanese_comma(current_text) &&
            count_japanese_chars(current_text) >= static_cast<size_t>(options.japanese_max_chars / 2)) {
            should_break = true;
        }

        if (should_break) {
            flush_entry();
        }
    }

    // Flush any remaining text
    flush_entry();

    return entries;
}

// ═══════════════════════════════════════════════════════════
// AAA English Karaoke Processing
// ═══════════════════════════════════════════════════════════

bool SubtitleExporter::ends_with_english_sentence_end(const std::string& text) {
    if (text.empty()) return false;

    // Check for sentence-ending punctuation: . ! ? ...
    std::string trimmed = text;
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\n')) {
        trimmed.pop_back();
    }

    if (trimmed.empty()) return false;

    char last = trimmed.back();
    return last == '.' || last == '!' || last == '?';
}

float SubtitleExporter::calculate_cps(const std::string& text, float duration) {
    if (duration <= 0.0f) return 0.0f;

    // Count displayable characters (exclude leading/trailing spaces)
    size_t char_count = 0;
    bool in_word = false;

    for (char c : text) {
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
            char_count++;
            in_word = true;
        } else if (in_word) {
            char_count++;  // Count one space between words
            in_word = false;
        }
    }

    return static_cast<float>(char_count) / duration;
}

std::vector<Word> SubtitleExporter::correct_word_timestamps(
    const std::vector<Word>& words,
    const SubtitleExportOptions& options) {

    if (words.empty()) return words;

    std::vector<Word> corrected = words;
    float min_duration_s = options.min_word_duration_ms / 1000.0f;
    float micro_gap_s = options.micro_gap_merge_ms / 1000.0f;

    // Pass 1: Ensure minimum word durations
    for (auto& word : corrected) {
        float duration = word.end - word.start;
        if (duration < min_duration_s) {
            word.end = word.start + min_duration_s;
        }
    }

    // Pass 2: Merge micro-gaps and ensure monotonic timestamps
    for (size_t i = 1; i < corrected.size(); ++i) {
        // Ensure monotonic: word can't start before previous word ends
        if (corrected[i].start < corrected[i-1].end) {
            corrected[i].start = corrected[i-1].end;
        }

        // Merge micro-gaps: if gap is tiny, extend previous word
        float gap = corrected[i].start - corrected[i-1].end;
        if (gap > 0 && gap < micro_gap_s) {
            corrected[i-1].end = corrected[i].start;
        }

        // Ensure word end is after start (after all adjustments)
        if (corrected[i].end <= corrected[i].start) {
            corrected[i].end = corrected[i].start + min_duration_s;
        }
    }

    return corrected;
}

std::vector<SubtitleEntry> SubtitleExporter::build_karaoke_captions(
    const std::vector<Segment>& segments,
    const SubtitleExportOptions& options) {

    std::vector<SubtitleEntry> entries;

    // Collect all words from all segments
    std::vector<Word> all_words;
    for (const auto& seg : segments) {
        for (const auto& word : seg.words) {
            all_words.push_back(word);
        }
    }

    if (all_words.empty()) {
        // No word-level timestamps - fall back to segment-based
        return segments_to_entries(segments, options);
    }

    // ═══════════════════════════════════════════════════════════
    // TIMING LAYER: Correct word timestamps
    // ═══════════════════════════════════════════════════════════
    std::vector<Word> corrected_words = correct_word_timestamps(all_words, options);

    // ═══════════════════════════════════════════════════════════
    // LAYOUT LAYER: Build phrases with CPS control
    // ═══════════════════════════════════════════════════════════
    int entry_index = 1;
    std::string current_text;
    std::vector<Word> current_words;
    float phrase_start = -1.0f;
    float phrase_end = 0.0f;
    float last_word_end = 0.0f;

    float pause_threshold_s = options.pause_break_threshold_ms / 1000.0f;

    auto flush_phrase = [&]() {
        if (!current_text.empty() && phrase_start >= 0) {
            SubtitleEntry entry;
            entry.index = entry_index++;

            // ═══════════════════════════════════════════════════════════
            // PRESENTATION LAYER: Apply timing adjustments
            // ═══════════════════════════════════════════════════════════

            // Apply phrase lead-in: show caption slightly before first word
            float lead_in_s = options.phrase_lead_in_ms / 1000.0f;
            entry.start = std::max(0.0f, phrase_start - lead_in_s);

            // Apply minimum hold after last word
            entry.end = phrase_end + options.min_hold_after_speech_s;

            // Split text if too long for display
            if (current_text.length() > static_cast<size_t>(options.max_chars_per_line)) {
                entry.text = split_text(current_text, options.max_chars_per_line, options.max_lines);
            } else {
                entry.text = current_text;
            }

            // Store words for karaoke highlighting
            entry.words = current_words;

            // Apply word lead time to each word for highlight timing
            for (auto& w : entry.words) {
                float word_lead_s = options.word_lead_ms / 1000.0f;
                w.start = std::max(entry.start, w.start - word_lead_s);
            }

            entries.push_back(entry);

            current_text.clear();
            current_words.clear();
            phrase_start = -1.0f;
        }
    };

    for (size_t i = 0; i < corrected_words.size(); ++i) {
        const Word& word = corrected_words[i];

        // Check for pause break (new phrase)
        if (phrase_start >= 0 && !current_text.empty()) {
            float pause = word.start - last_word_end;

            // Break on significant pause
            if (pause > pause_threshold_s) {
                flush_phrase();
            }
        }

        // Start new phrase if needed
        if (phrase_start < 0) {
            phrase_start = word.start;
        }

        // Add word to current phrase
        if (!current_text.empty()) {
            current_text += " ";
        }
        current_text += word.word;
        current_words.push_back(word);
        phrase_end = word.end;
        last_word_end = word.end;

        // Check break conditions
        bool should_break = false;

        // 1. Duration limit (target max phrase duration)
        float duration = phrase_end - phrase_start;
        if (duration >= options.target_phrase_duration_max) {
            should_break = true;
        }

        // 2. CPS limit - ensure readability
        float cps = calculate_cps(current_text, duration);
        if (cps > options.max_cps && duration >= options.target_phrase_duration_min) {
            should_break = true;
        }

        // 3. Character limit per line (with some buffer for 2-line display)
        if (current_text.length() >= static_cast<size_t>(options.max_chars_per_line * options.max_lines)) {
            should_break = true;
        }

        // 4. Sentence-ending punctuation (natural break point)
        if (ends_with_english_sentence_end(word.word) && duration >= options.target_phrase_duration_min) {
            should_break = true;
        }

        if (should_break) {
            flush_phrase();
        }
    }

    // Flush any remaining phrase
    flush_phrase();

    // ═══════════════════════════════════════════════════════════
    // POST-PROCESSING: Handle silence-based clearing
    // ═══════════════════════════════════════════════════════════
    // Extend caption end times to respect clear_after_silence_s
    // This prevents captions from disappearing too early during pauses

    for (size_t i = 0; i + 1 < entries.size(); ++i) {
        float gap_to_next = entries[i+1].start - entries[i].end;

        // If there's a long gap, extend the current caption's hold time
        // but don't exceed clear_after_silence_s from last word
        if (gap_to_next > options.min_hold_after_speech_s) {
            float max_extension = options.clear_after_silence_s;

            // Find the actual last word end time for this entry
            float last_word_time = entries[i].end - options.min_hold_after_speech_s;

            // Extend end time, but not beyond clear_after_silence from speech end
            float extended_end = std::min(
                entries[i+1].start - 0.1f,  // Leave small gap before next
                last_word_time + max_extension
            );

            if (extended_end > entries[i].end) {
                entries[i].end = extended_end;
            }
        }
    }

    return entries;
}

// ═══════════════════════════════════════════════════════════
// Korean-Specific Processing
// ═══════════════════════════════════════════════════════════

// Helper: Decode UTF-8 to Unicode codepoint
static uint32_t utf8_to_codepoint(const std::string& ch) {
    if (ch.empty()) return 0;

    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(ch.data());
    size_t len = ch.size();

    if (len == 1) {
        return bytes[0];  // ASCII
    } else if (len == 2) {
        return ((bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F);
    } else if (len == 3) {
        return ((bytes[0] & 0x0F) << 12) | ((bytes[1] & 0x3F) << 6) | (bytes[2] & 0x3F);
    } else if (len == 4) {
        return ((bytes[0] & 0x07) << 18) | ((bytes[1] & 0x3F) << 12) |
               ((bytes[2] & 0x3F) << 6) | (bytes[3] & 0x3F);
    }

    return 0;
}

// Helper: Encode Unicode codepoint to UTF-8
static std::string codepoint_to_utf8(uint32_t cp) {
    std::string result;

    if (cp < 0x80) {
        result += static_cast<char>(cp);
    } else if (cp < 0x800) {
        result += static_cast<char>(0xC0 | (cp >> 6));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        result += static_cast<char>(0xE0 | (cp >> 12));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        result += static_cast<char>(0xF0 | (cp >> 18));
        result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    }

    return result;
}

// Helper: Extract UTF-8 characters as vector
static std::vector<std::string> utf8_to_chars(const std::string& text) {
    std::vector<std::string> chars;
    size_t i = 0;

    while (i < text.size()) {
        size_t char_len = utf8_char_length(static_cast<unsigned char>(text[i]));
        if (i + char_len <= text.size()) {
            chars.push_back(text.substr(i, char_len));
        }
        i += char_len;
    }

    return chars;
}

bool SubtitleExporter::is_hangul_syllable(uint32_t codepoint) {
    // Hangul Syllables block: U+AC00 to U+D7A3
    return codepoint >= 0xAC00 && codepoint <= 0xD7A3;
}

size_t SubtitleExporter::count_korean_chars(const std::string& text) {
    size_t count = 0;
    std::vector<std::string> chars = utf8_to_chars(text);

    for (const auto& ch : chars) {
        // Skip spaces and newlines
        if (ch == " " || ch == "　" || ch == "\n" || ch == "\r" || ch == "\t") {
            continue;
        }

        uint32_t cp = utf8_to_codepoint(ch);

        // Count Hangul syllables
        if (is_hangul_syllable(cp)) {
            count++;
            continue;
        }

        // Count other CJK characters (CJK Unified Ideographs, Japanese, etc.)
        // CJK Unified Ideographs: U+4E00 to U+9FFF
        // Hiragana: U+3040 to U+309F
        // Katakana: U+30A0 to U+30FF
        // Hangul Jamo: U+1100 to U+11FF
        // Hangul Compatibility Jamo: U+3130 to U+318F
        if ((cp >= 0x4E00 && cp <= 0x9FFF) ||   // CJK Unified
            (cp >= 0x3040 && cp <= 0x309F) ||   // Hiragana
            (cp >= 0x30A0 && cp <= 0x30FF) ||   // Katakana
            (cp >= 0x1100 && cp <= 0x11FF) ||   // Hangul Jamo
            (cp >= 0x3130 && cp <= 0x318F)) {   // Hangul Compat Jamo
            count++;
            continue;
        }

        // Count fullwidth characters as 1
        if (cp >= 0xFF00 && cp <= 0xFFEF) {
            count++;
            continue;
        }

        // Count ASCII characters
        if (cp < 0x80 && cp >= 0x21) {  // Printable ASCII
            count++;
        }
    }

    return count;
}

bool SubtitleExporter::ends_with_korean_sentence_end(const std::string& text) {
    std::string last_char = get_last_utf8_char(text);

    // Sentence-ending punctuation for Korean
    // ASCII: . ! ?
    // Fullwidth: 。！？
    return last_char == "." || last_char == "!" || last_char == "?" ||
           last_char == "。" || last_char == "！" || last_char == "？";
}

bool SubtitleExporter::is_geumchik_no_start(const std::string& ch) {
    // Characters that should NOT start a line (Korean geumchik / Japanese kinsoku)
    // Closing brackets, punctuation, etc.
    static const std::vector<std::string> no_start = {
        // Punctuation
        ".", ",", "!", "?", ":", ";",
        "。", "、", "！", "？", "：", "；",
        // Closing brackets
        ")", "]", "}", "〉", "》", "」", "』", "】", "〕",
        "）", "］", "｝",
        // Other marks
        "…", "·", "~", "～",
        // Small kana (Japanese but may appear)
        "ぁ", "ぃ", "ぅ", "ぇ", "ぉ", "っ", "ゃ", "ゅ", "ょ",
        "ァ", "ィ", "ゥ", "ェ", "ォ", "ッ", "ャ", "ュ", "ョ"
    };

    for (const auto& c : no_start) {
        if (ch == c) return true;
    }
    return false;
}

bool SubtitleExporter::is_geumchik_no_end(const std::string& ch) {
    // Characters that should NOT end a line
    // Opening brackets, currency symbols, etc.
    static const std::vector<std::string> no_end = {
        // Opening brackets
        "(", "[", "{", "〈", "《", "「", "『", "【", "〔",
        "（", "［", "｛",
        // Currency symbols
        "₩", "$", "￥", "€"
    };

    for (const auto& c : no_end) {
        if (ch == c) return true;
    }
    return false;
}

std::string SubtitleExporter::split_korean_text(const std::string& text,
                                                 int max_chars,
                                                 int max_lines,
                                                 bool use_geumchik) {
    if (count_korean_chars(text) <= static_cast<size_t>(max_chars)) {
        return text;
    }

    std::vector<std::string> chars = utf8_to_chars(text);
    std::vector<std::string> lines;
    std::string current_line;
    size_t current_char_count = 0;

    for (size_t i = 0; i < chars.size() && static_cast<int>(lines.size()) < max_lines; ++i) {
        const std::string& ch = chars[i];

        // Skip if adding this char would cause geumchik violation at line end
        bool can_end_line_here = true;
        if (use_geumchik && i + 1 < chars.size()) {
            // Check if current char can end line
            if (is_geumchik_no_end(ch)) {
                can_end_line_here = false;
            }
            // Check if next char can start line
            if (is_geumchik_no_start(chars[i + 1])) {
                can_end_line_here = false;
            }
        }

        // Count this character
        bool is_space = (ch == " " || ch == "　");
        if (!is_space && ch != "\n" && ch != "\r") {
            current_char_count++;
        }

        current_line += ch;

        // Check if we should break
        bool should_break = false;
        if (current_char_count >= static_cast<size_t>(max_chars) && can_end_line_here) {
            should_break = true;
        }

        // Prefer breaking at spaces (word boundaries) for Korean
        if (is_space && current_char_count >= static_cast<size_t>(max_chars * 2 / 3)) {
            should_break = true;
        }

        // Prefer breaking after punctuation
        if ((ch == "." || ch == "!" || ch == "?" || ch == "," ||
             ch == "。" || ch == "、" || ch == "！" || ch == "？") &&
            current_char_count >= static_cast<size_t>(max_chars / 2)) {
            should_break = true;
        }

        if (should_break) {
            // Trim trailing space from current line
            while (!current_line.empty() && current_line.back() == ' ') {
                current_line.pop_back();
            }
            lines.push_back(current_line);
            current_line.clear();
            current_char_count = 0;
        }
    }

    // Add remaining text
    if (!current_line.empty() && static_cast<int>(lines.size()) < max_lines) {
        while (!current_line.empty() && current_line.back() == ' ') {
            current_line.pop_back();
        }
        if (!current_line.empty()) {
            lines.push_back(current_line);
        }
    }

    // Join with newlines
    std::string result;
    for (size_t j = 0; j < lines.size(); ++j) {
        result += lines[j];
        if (j < lines.size() - 1) {
            result += "\n";
        }
    }

    return result;
}

std::vector<Word> SubtitleExporter::split_hangul_syllables(
    const Word& word,
    float min_syllable_duration_ms) {

    std::vector<Word> syllables;
    std::vector<std::string> chars = utf8_to_chars(word.word);

    if (chars.empty()) {
        return syllables;
    }

    // Count Hangul syllable blocks
    std::vector<std::string> hangul_blocks;
    std::vector<std::string> non_hangul_prefix;

    bool in_hangul = false;
    for (const auto& ch : chars) {
        uint32_t cp = utf8_to_codepoint(ch);
        if (is_hangul_syllable(cp)) {
            in_hangul = true;
            hangul_blocks.push_back(ch);
        } else if (!in_hangul) {
            // Non-Hangul at start (like punctuation) - collect as prefix
            non_hangul_prefix.push_back(ch);
        } else {
            // Non-Hangul after Hangul - include with last Hangul block
            if (!hangul_blocks.empty()) {
                hangul_blocks.back() += ch;
            }
        }
    }

    // If no Hangul blocks, return original word
    if (hangul_blocks.empty()) {
        syllables.push_back(word);
        return syllables;
    }

    // Distribute timing across syllable blocks
    float total_duration = word.end - word.start;
    float min_duration_s = min_syllable_duration_ms / 1000.0f;
    size_t num_syllables = hangul_blocks.size();

    // Calculate per-syllable duration
    float syllable_duration = total_duration / static_cast<float>(num_syllables);

    // Clamp to minimum
    if (syllable_duration < min_duration_s) {
        syllable_duration = min_duration_s;
    }

    float current_time = word.start;

    // Include prefix with first syllable
    std::string prefix_str;
    for (const auto& p : non_hangul_prefix) {
        prefix_str += p;
    }

    for (size_t i = 0; i < hangul_blocks.size(); ++i) {
        Word syllable;
        syllable.start = current_time;
        syllable.end = std::min(current_time + syllable_duration, word.end);
        syllable.probability = word.probability;

        // First syllable gets the prefix
        if (i == 0 && !prefix_str.empty()) {
            syllable.word = prefix_str + hangul_blocks[i];
        } else {
            syllable.word = hangul_blocks[i];
        }

        // Last syllable extends to word end
        if (i == hangul_blocks.size() - 1) {
            syllable.end = word.end;
        }

        syllables.push_back(syllable);
        current_time = syllable.end;
    }

    return syllables;
}

std::vector<SubtitleEntry> SubtitleExporter::segment_korean(
    const std::vector<Segment>& segments,
    const SubtitleExportOptions& options) {

    std::vector<SubtitleEntry> entries;

    // Collect all words from all segments with timing
    std::vector<Word> all_words;
    for (const auto& seg : segments) {
        for (const auto& word : seg.words) {
            all_words.push_back(word);
        }
    }

    if (all_words.empty()) {
        // No word-level timestamps - fall back to segment-based
        return segments_to_entries(segments, options);
    }

    // If syllable karaoke is enabled, split words into syllables
    std::vector<Word> processed_words;
    if (options.korean_syllable_karaoke) {
        for (const auto& word : all_words) {
            auto syllables = split_hangul_syllables(word, 60.0f);
            for (const auto& syl : syllables) {
                processed_words.push_back(syl);
            }
        }
    } else {
        processed_words = all_words;
    }

    // Build subtitles from words using Korean rules
    int entry_index = 1;
    std::string current_text;
    std::vector<Word> current_words;
    float current_start = -1.0f;
    float current_end = 0.0f;
    float last_word_end = 0.0f;

    float pause_threshold_s = options.korean_pause_threshold_ms / 1000.0f;

    auto flush_entry = [&]() {
        if (!current_text.empty() && current_start >= 0) {
            // Check minimum duration
            float duration = current_end - current_start;
            if (duration < options.korean_min_duration) {
                current_end = current_start + options.korean_min_duration;
            }

            SubtitleEntry entry;
            entry.index = entry_index++;
            entry.start = current_start;
            entry.end = current_end;
            entry.words = current_words;

            // Split text if too long for display
            if (count_korean_chars(current_text) > static_cast<size_t>(options.korean_max_chars)) {
                entry.text = split_korean_text(current_text,
                                               options.korean_max_chars,
                                               options.max_lines,
                                               options.korean_geumchik_rules);
            } else {
                entry.text = current_text;
            }

            entries.push_back(entry);

            current_text.clear();
            current_words.clear();
            current_start = -1.0f;
        }
    };

    for (size_t i = 0; i < processed_words.size(); ++i) {
        const Word& word = processed_words[i];

        // Check for pause between words (break point)
        if (current_start >= 0 && !current_text.empty()) {
            float pause = word.start - last_word_end;

            if (pause > pause_threshold_s) {
                flush_entry();
            }
        }

        // Start new subtitle if needed
        if (current_start < 0) {
            current_start = word.start;
        }

        // Add word to current subtitle (Korean typically has spaces between words)
        // Check if word starts with Hangul and current text ends with Hangul
        bool need_space = !current_text.empty() && !word.word.empty();
        if (need_space) {
            // Add space unless word is punctuation
            char first_char = word.word[0];
            if (first_char != '.' && first_char != ',' && first_char != '!' &&
                first_char != '?' && first_char != ':' && first_char != ';') {
                current_text += " ";
            }
        }
        current_text += word.word;
        current_words.push_back(word);
        current_end = word.end;
        last_word_end = word.end;

        // Check break conditions
        bool should_break = false;

        // 1. Duration limit
        float duration = current_end - current_start;
        if (duration > options.korean_max_duration) {
            should_break = true;
        }

        // 2. Character count limit
        if (count_korean_chars(current_text) >= static_cast<size_t>(options.korean_max_chars)) {
            should_break = true;
        }

        // 3. Sentence-ending punctuation
        if (options.korean_break_on_sentence && ends_with_korean_sentence_end(current_text)) {
            should_break = true;
        }

        if (should_break) {
            flush_entry();
        }
    }

    // Flush any remaining text
    flush_entry();

    return entries;
}

// ═══════════════════════════════════════════════════════════
// Chinese-Specific Processing
// ═══════════════════════════════════════════════════════════

bool SubtitleExporter::is_cjk_ideograph(uint32_t codepoint) {
    // CJK Unified Ideographs: U+4E00 to U+9FFF (main block)
    if (codepoint >= 0x4E00 && codepoint <= 0x9FFF) return true;

    // CJK Unified Ideographs Extension A: U+3400 to U+4DBF
    if (codepoint >= 0x3400 && codepoint <= 0x4DBF) return true;

    // CJK Unified Ideographs Extension B: U+20000 to U+2A6DF
    if (codepoint >= 0x20000 && codepoint <= 0x2A6DF) return true;

    // CJK Compatibility Ideographs: U+F900 to U+FAFF
    if (codepoint >= 0xF900 && codepoint <= 0xFAFF) return true;

    // CJK Radicals Supplement: U+2E80 to U+2EFF
    if (codepoint >= 0x2E80 && codepoint <= 0x2EFF) return true;

    // Kangxi Radicals: U+2F00 to U+2FDF
    if (codepoint >= 0x2F00 && codepoint <= 0x2FDF) return true;

    return false;
}

size_t SubtitleExporter::count_chinese_chars(const std::string& text) {
    size_t count = 0;
    std::vector<std::string> chars = utf8_to_chars(text);

    for (const auto& ch : chars) {
        // Skip spaces and newlines
        if (ch == " " || ch == "　" || ch == "\n" || ch == "\r" || ch == "\t") {
            continue;
        }

        uint32_t cp = utf8_to_codepoint(ch);

        // Count CJK ideographs
        if (is_cjk_ideograph(cp)) {
            count++;
            continue;
        }

        // Also count fullwidth punctuation as characters (they take visual space)
        // Fullwidth forms: U+FF00 to U+FFEF
        if (cp >= 0xFF00 && cp <= 0xFFEF) {
            count++;
            continue;
        }

        // CJK Symbols and Punctuation: U+3000 to U+303F
        if (cp >= 0x3000 && cp <= 0x303F) {
            count++;
            continue;
        }

        // Count ASCII characters (for mixed content)
        if (cp < 0x80 && cp >= 0x21) {
            count++;
        }
    }

    return count;
}

bool SubtitleExporter::ends_with_chinese_sentence_end(const std::string& text) {
    std::string last_char = get_last_utf8_char(text);

    // Chinese sentence-ending punctuation
    // Fullwidth: 。！？
    // Ellipsis: …
    // Also accept ASCII for mixed content
    return last_char == "。" || last_char == "！" || last_char == "？" ||
           last_char == "…" || last_char == "." || last_char == "!" || last_char == "?";
}

bool SubtitleExporter::is_chinese_no_start(const std::string& ch) {
    // Characters that should NOT start a line (Chinese punctuation rules)
    // Closing punctuation, fullwidth closing brackets, etc.
    static const std::vector<std::string> no_start = {
        // Closing punctuation (fullwidth Chinese)
        "\xE3\x80\x82",  // 。 U+3002 IDEOGRAPHIC FULL STOP
        "\xEF\xBC\x8C",  // ， U+FF0C FULLWIDTH COMMA
        "\xE3\x80\x81",  // 、 U+3001 IDEOGRAPHIC COMMA
        "\xEF\xBC\x81",  // ！ U+FF01 FULLWIDTH EXCLAMATION
        "\xEF\xBC\x9F",  // ？ U+FF1F FULLWIDTH QUESTION
        "\xEF\xBC\x9A",  // ： U+FF1A FULLWIDTH COLON
        "\xEF\xBC\x9B",  // ； U+FF1B FULLWIDTH SEMICOLON
        // ASCII punctuation
        ".", ",", "!", "?", ":", ";",
        // Closing brackets (fullwidth)
        "\xEF\xBC\x89",  // ） U+FF09 FULLWIDTH RIGHT PARENTHESIS
        "\xE3\x80\x91",  // 】 U+3011 RIGHT BLACK LENTICULAR BRACKET
        "\xE3\x80\x8B",  // 》 U+300B RIGHT DOUBLE ANGLE BRACKET
        "\xE3\x80\x8D",  // 」 U+300D RIGHT CORNER BRACKET
        "\xE3\x80\x8F",  // 』 U+300F RIGHT WHITE CORNER BRACKET
        "\xE3\x80\x89",  // 〉 U+3009 RIGHT ANGLE BRACKET
        "\xE3\x80\x95",  // 〕 U+3015 RIGHT TORTOISE SHELL BRACKET
        // ASCII closing brackets
        ")", "]", "}",
        // Ellipsis and dash
        "\xE2\x80\xA6",  // … U+2026 HORIZONTAL ELLIPSIS
        "\xE2\x80\x94",  // — U+2014 EM DASH
        "\xEF\xBD\x9E",  // ～ U+FF5E FULLWIDTH TILDE
        // Middle dot
        "\xC2\xB7"       // · U+00B7 MIDDLE DOT
    };

    for (const auto& c : no_start) {
        if (ch == c) return true;
    }
    return false;
}

bool SubtitleExporter::is_chinese_no_end(const std::string& ch) {
    // Characters that should NOT end a line
    // Opening brackets, currency symbols, etc.
    static const std::vector<std::string> no_end = {
        // Opening brackets (fullwidth)
        "\xEF\xBC\x88",  // （ U+FF08 FULLWIDTH LEFT PARENTHESIS
        "\xE3\x80\x90",  // 【 U+3010 LEFT BLACK LENTICULAR BRACKET
        "\xE3\x80\x8A",  // 《 U+300A LEFT DOUBLE ANGLE BRACKET
        "\xE3\x80\x8C",  // 「 U+300C LEFT CORNER BRACKET
        "\xE3\x80\x8E",  // 『 U+300E LEFT WHITE CORNER BRACKET
        "\xE3\x80\x88",  // 〈 U+3008 LEFT ANGLE BRACKET
        "\xE3\x80\x94",  // 〔 U+3014 LEFT TORTOISE SHELL BRACKET
        // ASCII opening brackets
        "(", "[", "{",
        // Currency symbols
        "\xEF\xBF\xA5",  // ￥ U+FFE5 FULLWIDTH YEN SIGN
        "$",
        "\xE2\x82\xAC",  // € U+20AC EURO SIGN
        "\xC2\xA3"       // £ U+00A3 POUND SIGN
    };

    for (const auto& c : no_end) {
        if (ch == c) return true;
    }
    return false;
}

std::string SubtitleExporter::split_chinese_text(const std::string& text,
                                                  int max_chars,
                                                  int max_lines,
                                                  bool use_punctuation_rules) {
    if (count_chinese_chars(text) <= static_cast<size_t>(max_chars)) {
        return text;
    }

    std::vector<std::string> chars = utf8_to_chars(text);
    std::vector<std::string> lines;
    std::string current_line;
    size_t current_char_count = 0;

    for (size_t i = 0; i < chars.size() && static_cast<int>(lines.size()) < max_lines; ++i) {
        const std::string& ch = chars[i];

        // Check if we can break here (punctuation rules)
        bool can_end_line_here = true;
        if (use_punctuation_rules && i + 1 < chars.size()) {
            // Check if current char can end line
            if (is_chinese_no_end(ch)) {
                can_end_line_here = false;
            }
            // Check if next char can start line
            if (is_chinese_no_start(chars[i + 1])) {
                can_end_line_here = false;
            }
        }

        // Count this character
        bool is_space = (ch == " " || ch == "　");
        if (!is_space && ch != "\n" && ch != "\r") {
            current_char_count++;
        }

        current_line += ch;

        // Check if we should break
        bool should_break = false;
        if (current_char_count >= static_cast<size_t>(max_chars) && can_end_line_here) {
            should_break = true;
        }

        // Prefer breaking after sentence-ending punctuation
        if ((ch == "。" || ch == "！" || ch == "？" || ch == "…" ||
             ch == "，" || ch == "、") &&
            current_char_count >= static_cast<size_t>(max_chars / 2) && can_end_line_here) {
            should_break = true;
        }

        if (should_break) {
            lines.push_back(current_line);
            current_line.clear();
            current_char_count = 0;
        }
    }

    // Add remaining text
    if (!current_line.empty() && static_cast<int>(lines.size()) < max_lines) {
        lines.push_back(current_line);
    }

    // Join with newlines
    std::string result;
    for (size_t j = 0; j < lines.size(); ++j) {
        result += lines[j];
        if (j < lines.size() - 1) {
            result += "\n";
        }
    }

    return result;
}

std::vector<Word> SubtitleExporter::split_chinese_characters(
    const Word& word,
    float min_char_duration_ms) {

    std::vector<Word> characters;
    std::vector<std::string> chars = utf8_to_chars(word.word);

    if (chars.empty()) {
        return characters;
    }

    // Collect CJK characters for splitting
    std::vector<std::string> cjk_chars;
    std::vector<std::string> prefix;

    bool found_cjk = false;
    for (const auto& ch : chars) {
        uint32_t cp = utf8_to_codepoint(ch);
        if (is_cjk_ideograph(cp)) {
            found_cjk = true;
            cjk_chars.push_back(ch);
        } else if (!found_cjk) {
            // Non-CJK at start - collect as prefix
            prefix.push_back(ch);
        } else {
            // Non-CJK after CJK - include with last CJK char (punctuation)
            if (!cjk_chars.empty()) {
                cjk_chars.back() += ch;
            }
        }
    }

    // If no CJK characters, return original word
    if (cjk_chars.empty()) {
        characters.push_back(word);
        return characters;
    }

    // Distribute timing across characters
    float total_duration = word.end - word.start;
    float min_duration_s = min_char_duration_ms / 1000.0f;
    size_t num_chars = cjk_chars.size();

    // Calculate per-character duration
    float char_duration = total_duration / static_cast<float>(num_chars);

    // Clamp to minimum
    if (char_duration < min_duration_s) {
        char_duration = min_duration_s;
    }

    float current_time = word.start;

    // Include prefix with first character
    std::string prefix_str;
    for (const auto& p : prefix) {
        prefix_str += p;
    }

    for (size_t i = 0; i < cjk_chars.size(); ++i) {
        Word character;
        character.start = current_time;
        character.end = std::min(current_time + char_duration, word.end);
        character.probability = word.probability;

        // First character gets the prefix
        if (i == 0 && !prefix_str.empty()) {
            character.word = prefix_str + cjk_chars[i];
        } else {
            character.word = cjk_chars[i];
        }

        // Last character extends to word end
        if (i == cjk_chars.size() - 1) {
            character.end = word.end;
        }

        characters.push_back(character);
        current_time = character.end;
    }

    return characters;
}

float SubtitleExporter::calculate_chinese_cps(const std::string& text, float duration) {
    if (duration <= 0.0f) return 0.0f;

    size_t char_count = count_chinese_chars(text);
    return static_cast<float>(char_count) / duration;
}

std::vector<SubtitleEntry> SubtitleExporter::segment_chinese(
    const std::vector<Segment>& segments,
    const SubtitleExportOptions& options) {

    std::vector<SubtitleEntry> entries;

    // Collect all words from all segments with timing
    std::vector<Word> all_words;
    for (const auto& seg : segments) {
        for (const auto& word : seg.words) {
            all_words.push_back(word);
        }
    }

    if (all_words.empty()) {
        // No word-level timestamps - fall back to segment-based
        return segments_to_entries(segments, options);
    }

    // Determine max chars based on Simplified vs Traditional
    int max_chars = options.chinese_traditional ?
        options.chinese_max_chars_traditional :
        options.chinese_max_chars_simplified;

    // If character karaoke is enabled, split words into characters
    std::vector<Word> processed_words;
    if (options.chinese_character_karaoke) {
        for (const auto& word : all_words) {
            auto chars = split_chinese_characters(word, 60.0f);
            for (const auto& ch : chars) {
                processed_words.push_back(ch);
            }
        }
    } else {
        processed_words = all_words;
    }

    // Build subtitles from words using Chinese rules
    int entry_index = 1;
    std::string current_text;
    std::vector<Word> current_words;
    float current_start = -1.0f;
    float current_end = 0.0f;
    float last_word_end = 0.0f;

    float pause_threshold_s = options.chinese_pause_threshold_ms / 1000.0f;

    auto flush_entry = [&]() {
        if (!current_text.empty() && current_start >= 0) {
            // Check minimum duration
            float duration = current_end - current_start;
            if (duration < options.chinese_min_duration) {
                current_end = current_start + options.chinese_min_duration;
                duration = options.chinese_min_duration;
            }

            SubtitleEntry entry;
            entry.index = entry_index++;
            entry.start = current_start;
            entry.end = current_end;
            entry.words = current_words;

            // Split text if too long for display
            if (count_chinese_chars(current_text) > static_cast<size_t>(max_chars)) {
                entry.text = split_chinese_text(current_text,
                                                max_chars,
                                                options.max_lines,
                                                options.chinese_punctuation_rules);
            } else {
                entry.text = current_text;
            }

            entries.push_back(entry);

            current_text.clear();
            current_words.clear();
            current_start = -1.0f;
        }
    };

    for (size_t i = 0; i < processed_words.size(); ++i) {
        const Word& word = processed_words[i];

        // Check for pause between words (break point)
        if (current_start >= 0 && !current_text.empty()) {
            float pause = word.start - last_word_end;

            if (pause > pause_threshold_s) {
                flush_entry();
            }
        }

        // Start new subtitle if needed
        if (current_start < 0) {
            current_start = word.start;
        }

        // Add word to current subtitle (Chinese has no spaces between words)
        current_text += word.word;
        current_words.push_back(word);
        current_end = word.end;
        last_word_end = word.end;

        // Check break conditions
        bool should_break = false;

        // 1. Duration limit
        float duration = current_end - current_start;
        if (duration > options.chinese_max_duration) {
            should_break = true;
        }

        // 2. Character count limit
        if (count_chinese_chars(current_text) >= static_cast<size_t>(max_chars)) {
            should_break = true;
        }

        // 3. CPS limit (reading speed)
        float cps = calculate_chinese_cps(current_text, duration);
        if (cps > options.chinese_max_cps && duration >= 1.0f) {
            should_break = true;
        }

        // 4. Sentence-ending punctuation
        if (options.chinese_break_on_sentence && ends_with_chinese_sentence_end(current_text)) {
            should_break = true;
        }

        if (should_break) {
            flush_entry();
        }
    }

    // Flush any remaining text
    flush_entry();

    return entries;
}

// ═══════════════════════════════════════════════════════════
// Russian-Specific Processing
// ═══════════════════════════════════════════════════════════

bool SubtitleExporter::is_cyrillic(uint32_t codepoint) {
    // Cyrillic block: U+0400 to U+04FF
    if (codepoint >= 0x0400 && codepoint <= 0x04FF) return true;

    // Cyrillic Supplement: U+0500 to U+052F
    if (codepoint >= 0x0500 && codepoint <= 0x052F) return true;

    // Cyrillic Extended-A: U+2DE0 to U+2DFF
    if (codepoint >= 0x2DE0 && codepoint <= 0x2DFF) return true;

    // Cyrillic Extended-B: U+A640 to U+A69F
    if (codepoint >= 0xA640 && codepoint <= 0xA69F) return true;

    return false;
}

size_t SubtitleExporter::count_russian_chars(const std::string& text) {
    size_t count = 0;
    std::vector<std::string> chars = utf8_to_chars(text);

    for (const auto& ch : chars) {
        // Skip spaces and newlines
        if (ch == " " || ch == "\n" || ch == "\r" || ch == "\t") {
            continue;
        }

        uint32_t cp = utf8_to_codepoint(ch);

        // Count Cyrillic characters
        if (is_cyrillic(cp)) {
            count++;
            continue;
        }

        // Count ASCII letters and digits
        if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
            (cp >= '0' && cp <= '9')) {
            count++;
            continue;
        }

        // Count punctuation as part of reading (contributes to CPS)
        if (cp >= 0x21 && cp <= 0x7E) {
            count++;
        }
    }

    return count;
}

bool SubtitleExporter::ends_with_russian_sentence_end(const std::string& text) {
    std::string last_char = get_last_utf8_char(text);

    // Russian sentence-ending punctuation
    // ASCII: . ! ?
    // Em dash: — (U+2014)
    // Ellipsis: … (U+2026)
    return last_char == "." || last_char == "!" || last_char == "?" ||
           last_char == "\xE2\x80\x94" ||  // — U+2014 EM DASH
           last_char == "\xE2\x80\xA6";    // … U+2026 HORIZONTAL ELLIPSIS
}

bool SubtitleExporter::is_russian_single_letter_preposition(const std::string& word) {
    // Russian single-letter prepositions and conjunctions that should not
    // be left at the end of a line:
    // в (v), к (k), с (s), о (o), у (u), а (a), и (i), я (ya)

    // Lowercase Cyrillic single-letter words
    static const std::vector<std::string> prepositions = {
        "\xD0\xB2",  // в (U+0432)
        "\xD0\xBA",  // к (U+043A)
        "\xD1\x81",  // с (U+0441)
        "\xD0\xBE",  // о (U+043E)
        "\xD1\x83",  // у (U+0443)
        "\xD0\xB0",  // а (U+0430)
        "\xD0\xB8",  // и (U+0438)
        "\xD1\x8F",  // я (U+044F)
        // Uppercase versions
        "\xD0\x92",  // В (U+0412)
        "\xD0\x9A",  // К (U+041A)
        "\xD0\xA1",  // С (U+0421)
        "\xD0\x9E",  // О (U+041E)
        "\xD0\xA3",  // У (U+0423)
        "\xD0\x90",  // А (U+0410)
        "\xD0\x98",  // И (U+0418)
        "\xD0\xAF",  // Я (U+042F)
    };

    for (const auto& prep : prepositions) {
        if (word == prep) return true;
    }

    return false;
}

bool SubtitleExporter::is_russian_no_start(const std::string& ch) {
    // Characters that should NOT start a line in Russian
    // Closing punctuation, etc.
    static const std::vector<std::string> no_start = {
        // ASCII punctuation
        ".", ",", "!", "?", ":", ";", ")", "]", "}",
        // Em dash, en dash
        "\xE2\x80\x94",  // — U+2014 EM DASH
        "\xE2\x80\x93",  // – U+2013 EN DASH
        // Ellipsis
        "\xE2\x80\xA6",  // … U+2026 HORIZONTAL ELLIPSIS
        // Guillemets (Russian quotation marks) - closing
        "\xC2\xBB",      // » U+00BB RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK
        "\xE2\x80\x9D",  // " U+201D RIGHT DOUBLE QUOTATION MARK
        "\xE2\x80\x99",  // ' U+2019 RIGHT SINGLE QUOTATION MARK
    };

    for (const auto& c : no_start) {
        if (ch == c) return true;
    }
    return false;
}

std::string SubtitleExporter::split_russian_text(const std::string& text,
                                                  int max_chars,
                                                  int max_lines,
                                                  bool use_line_break_rules) {
    if (count_russian_chars(text) <= static_cast<size_t>(max_chars)) {
        return text;
    }

    // Split into words (Russian uses spaces between words)
    std::vector<std::string> words;
    std::string current_word;

    for (size_t i = 0; i < text.size(); ) {
        size_t char_len = utf8_char_length(static_cast<unsigned char>(text[i]));
        std::string ch = text.substr(i, char_len);

        if (ch == " " || ch == "\n" || ch == "\r" || ch == "\t") {
            if (!current_word.empty()) {
                words.push_back(current_word);
                current_word.clear();
            }
        } else {
            current_word += ch;
        }
        i += char_len;
    }
    if (!current_word.empty()) {
        words.push_back(current_word);
    }

    // Build lines
    std::vector<std::string> lines;
    std::string current_line;
    size_t current_char_count = 0;

    for (size_t i = 0; i < words.size() && static_cast<int>(lines.size()) < max_lines; ++i) {
        const std::string& word = words[i];
        size_t word_len = count_russian_chars(word);

        // Check if adding this word would exceed the limit
        size_t new_count = current_char_count + word_len;
        if (!current_line.empty()) {
            new_count += 1;  // Account for space
        }

        bool should_break = (new_count > static_cast<size_t>(max_chars) && !current_line.empty());

        // Check line-break rules
        if (use_line_break_rules && should_break) {
            // Don't leave single-letter prepositions at line end
            if (!current_line.empty()) {
                // Get last word on current line
                size_t last_space = current_line.rfind(' ');
                std::string last_word = (last_space != std::string::npos)
                    ? current_line.substr(last_space + 1)
                    : current_line;

                if (is_russian_single_letter_preposition(last_word)) {
                    // Move preposition to next line with current word
                    if (last_space != std::string::npos) {
                        current_line = current_line.substr(0, last_space);
                    } else {
                        // Line is just the preposition - don't break
                        should_break = false;
                    }
                }
            }

            // Don't start line with closing punctuation
            if (should_break && !word.empty()) {
                std::string first_char = word.substr(0, utf8_char_length(static_cast<unsigned char>(word[0])));
                if (is_russian_no_start(first_char)) {
                    // Don't break - keep punctuation with previous line
                    should_break = false;
                }
            }
        }

        if (should_break) {
            lines.push_back(current_line);
            current_line = word;
            current_char_count = word_len;
        } else {
            if (!current_line.empty()) {
                current_line += " ";
                current_char_count += 1;
            }
            current_line += word;
            current_char_count += word_len;
        }
    }

    // Add remaining text
    if (!current_line.empty() && static_cast<int>(lines.size()) < max_lines) {
        lines.push_back(current_line);
    }

    // Join with newlines
    std::string result;
    for (size_t j = 0; j < lines.size(); ++j) {
        result += lines[j];
        if (j < lines.size() - 1) {
            result += "\n";
        }
    }

    return result;
}

float SubtitleExporter::calculate_russian_cps(const std::string& text, float duration) {
    if (duration <= 0.0f) return 0.0f;

    size_t char_count = count_russian_chars(text);
    return static_cast<float>(char_count) / duration;
}

std::vector<SubtitleEntry> SubtitleExporter::segment_russian(
    const std::vector<Segment>& segments,
    const SubtitleExportOptions& options) {

    std::vector<SubtitleEntry> entries;

    // Collect all words from all segments with timing
    std::vector<Word> all_words;
    for (const auto& seg : segments) {
        for (const auto& word : seg.words) {
            all_words.push_back(word);
        }
    }

    if (all_words.empty()) {
        // No word-level timestamps - fall back to segment-based
        return segments_to_entries(segments, options);
    }

    // Build subtitles from words using Russian rules
    int entry_index = 1;
    std::string current_text;
    std::vector<Word> current_words;
    float current_start = -1.0f;
    float current_end = 0.0f;
    float last_word_end = 0.0f;

    float pause_threshold_s = options.russian_pause_threshold_ms / 1000.0f;

    auto flush_entry = [&]() {
        if (!current_text.empty() && current_start >= 0) {
            // Check minimum duration
            float duration = current_end - current_start;
            if (duration < options.russian_min_duration) {
                current_end = current_start + options.russian_min_duration;
                duration = options.russian_min_duration;
            }

            SubtitleEntry entry;
            entry.index = entry_index++;
            entry.start = current_start;
            entry.end = current_end;
            entry.words = current_words;

            // Split text if too long for display
            if (count_russian_chars(current_text) > static_cast<size_t>(options.russian_max_chars)) {
                entry.text = split_russian_text(current_text,
                                                options.russian_max_chars,
                                                options.max_lines,
                                                options.russian_line_break_rules);
            } else {
                entry.text = current_text;
            }

            entries.push_back(entry);

            current_text.clear();
            current_words.clear();
            current_start = -1.0f;
        }
    };

    for (size_t i = 0; i < all_words.size(); ++i) {
        const Word& word = all_words[i];

        // Check for pause between words (break point)
        if (current_start >= 0 && !current_text.empty()) {
            float pause = word.start - last_word_end;

            if (pause > pause_threshold_s) {
                flush_entry();
            }
        }

        // Start new subtitle if needed
        if (current_start < 0) {
            current_start = word.start;
        }

        // Add word to current subtitle (Russian uses spaces between words)
        if (!current_text.empty()) {
            // Don't add space before punctuation
            std::string word_text = word.word;
            if (!word_text.empty()) {
                char first_char = word_text[0];
                if (first_char != '.' && first_char != ',' && first_char != '!' &&
                    first_char != '?' && first_char != ':' && first_char != ';') {
                    current_text += " ";
                }
            }
        }
        current_text += word.word;
        current_words.push_back(word);
        current_end = word.end;
        last_word_end = word.end;

        // Check break conditions
        bool should_break = false;

        // 1. Duration limit
        float duration = current_end - current_start;
        if (duration > options.russian_max_duration) {
            should_break = true;
        }

        // 2. Character count limit
        if (count_russian_chars(current_text) >= static_cast<size_t>(options.russian_max_chars)) {
            should_break = true;
        }

        // 3. CPS limit (reading speed)
        float cps = calculate_russian_cps(current_text, duration);
        if (cps > options.russian_max_cps && duration >= 1.0f) {
            should_break = true;
        }

        // 4. Sentence-ending punctuation
        if (options.russian_break_on_sentence && ends_with_russian_sentence_end(current_text)) {
            should_break = true;
        }

        // Check line-break rules before breaking
        if (should_break && options.russian_line_break_rules) {
            // Don't break if current word is a single-letter preposition
            // (would leave it stranded at end of subtitle)
            if (is_russian_single_letter_preposition(word.word)) {
                should_break = false;
            }
        }

        if (should_break) {
            flush_entry();
        }
    }

    // Flush any remaining text
    flush_entry();

    return entries;
}

// ═══════════════════════════════════════════════════════════
// Spanish/Portuguese-Specific Processing (Tier 1)
// ═══════════════════════════════════════════════════════════

size_t SubtitleExporter::count_spanish_chars(const std::string& text) {
    size_t count = 0;
    std::vector<std::string> chars = utf8_to_chars(text);

    for (const auto& ch : chars) {
        // Skip spaces and newlines
        if (ch == " " || ch == "\n" || ch == "\r" || ch == "\t") {
            continue;
        }

        uint32_t cp = utf8_to_codepoint(ch);

        // Count Latin characters (including extended Latin for Spanish/Portuguese)
        // Basic Latin: U+0000 to U+007F
        // Latin-1 Supplement: U+0080 to U+00FF (includes accented chars)
        // Latin Extended-A: U+0100 to U+017F
        // Latin Extended-B: U+0180 to U+024F
        if ((cp >= 0x0021 && cp <= 0x007E) ||  // Printable ASCII
            (cp >= 0x00A1 && cp <= 0x00FF) ||  // Latin-1 (Spanish: ñ, á, é, í, ó, ú, ü, ¿, ¡)
            (cp >= 0x0100 && cp <= 0x024F)) {  // Latin Extended
            count++;
        }
    }

    return count;
}

bool SubtitleExporter::ends_with_spanish_sentence_end(const std::string& text) {
    std::string last_char = get_last_utf8_char(text);

    // Spanish/Portuguese sentence-ending punctuation
    // . ! ? and opening marks at start: ¿ ¡
    return last_char == "." || last_char == "!" || last_char == "?" ||
           last_char == "\xC2\xBF" ||  // ¿ U+00BF INVERTED QUESTION MARK
           last_char == "\xC2\xA1";    // ¡ U+00A1 INVERTED EXCLAMATION MARK
}

bool SubtitleExporter::is_spanish_short_word(const std::string& word) {
    // Spanish/Portuguese short words that should not end a line:
    // Articles: a, al, el, la, las, los, o, os, as, um, uma
    // Prepositions: a, de, del, em, en, da, do, das, dos, na, no, nas, nos
    // Conjunctions: e, y, o, u
    // Portuguese: de, da, do, das, dos, em, na, no, nas, nos

    // Convert to lowercase for comparison
    std::string lower_word;
    for (char c : word) {
        if (c >= 'A' && c <= 'Z') {
            lower_word += (c + 32);
        } else {
            lower_word += c;
        }
    }

    static const std::vector<std::string> short_words = {
        // Single letters
        "a", "e", "o", "u", "y",
        // Two letters
        "al", "de", "do", "da", "el", "em", "en", "la", "na", "no", "os", "as",
        // Three letters
        "del", "das", "dos", "las", "los", "nas", "nos", "uma", "por", "con", "sin"
    };

    for (const auto& sw : short_words) {
        if (lower_word == sw) return true;
    }

    return false;
}

std::string SubtitleExporter::split_spanish_text(const std::string& text,
                                                  int max_chars,
                                                  int max_lines,
                                                  bool use_line_break_rules) {
    if (count_spanish_chars(text) <= static_cast<size_t>(max_chars)) {
        return text;
    }

    // Split into words (Spanish uses spaces)
    std::vector<std::string> words;
    std::string current_word;

    for (size_t i = 0; i < text.size(); ) {
        size_t char_len = utf8_char_length(static_cast<unsigned char>(text[i]));
        std::string ch = text.substr(i, char_len);

        if (ch == " " || ch == "\n" || ch == "\r" || ch == "\t") {
            if (!current_word.empty()) {
                words.push_back(current_word);
                current_word.clear();
            }
        } else {
            current_word += ch;
        }
        i += char_len;
    }
    if (!current_word.empty()) {
        words.push_back(current_word);
    }

    // Build lines
    std::vector<std::string> lines;
    std::string current_line;
    size_t current_char_count = 0;

    for (size_t i = 0; i < words.size() && static_cast<int>(lines.size()) < max_lines; ++i) {
        const std::string& word = words[i];
        size_t word_len = count_spanish_chars(word);

        size_t new_count = current_char_count + word_len;
        if (!current_line.empty()) {
            new_count += 1;  // Space
        }

        bool should_break = (new_count > static_cast<size_t>(max_chars) && !current_line.empty());

        // Check line-break rules
        if (use_line_break_rules && should_break && !current_line.empty()) {
            // Get last word on current line
            size_t last_space = current_line.rfind(' ');
            std::string last_word = (last_space != std::string::npos)
                ? current_line.substr(last_space + 1)
                : current_line;

            if (is_spanish_short_word(last_word)) {
                // Move short word to next line
                if (last_space != std::string::npos) {
                    current_line = current_line.substr(0, last_space);
                } else {
                    should_break = false;
                }
            }
        }

        if (should_break) {
            lines.push_back(current_line);
            current_line = word;
            current_char_count = word_len;
        } else {
            if (!current_line.empty()) {
                current_line += " ";
                current_char_count += 1;
            }
            current_line += word;
            current_char_count += word_len;
        }
    }

    if (!current_line.empty() && static_cast<int>(lines.size()) < max_lines) {
        lines.push_back(current_line);
    }

    // Join with newlines
    std::string result;
    for (size_t j = 0; j < lines.size(); ++j) {
        result += lines[j];
        if (j < lines.size() - 1) {
            result += "\n";
        }
    }

    return result;
}

float SubtitleExporter::calculate_spanish_cps(const std::string& text, float duration) {
    if (duration <= 0.0f) return 0.0f;
    size_t char_count = count_spanish_chars(text);
    return static_cast<float>(char_count) / duration;
}

std::vector<SubtitleEntry> SubtitleExporter::segment_spanish(
    const std::vector<Segment>& segments,
    const SubtitleExportOptions& options) {

    std::vector<SubtitleEntry> entries;

    // Collect all words
    std::vector<Word> all_words;
    for (const auto& seg : segments) {
        for (const auto& word : seg.words) {
            all_words.push_back(word);
        }
    }

    if (all_words.empty()) {
        return segments_to_entries(segments, options);
    }

    int entry_index = 1;
    std::string current_text;
    std::vector<Word> current_words;
    float current_start = -1.0f;
    float current_end = 0.0f;
    float last_word_end = 0.0f;

    float pause_threshold_s = options.spanish_pause_threshold_ms / 1000.0f;

    auto flush_entry = [&]() {
        if (!current_text.empty() && current_start >= 0) {
            float duration = current_end - current_start;
            if (duration < options.spanish_min_duration) {
                current_end = current_start + options.spanish_min_duration;
            }

            SubtitleEntry entry;
            entry.index = entry_index++;
            entry.start = current_start;
            entry.end = current_end;
            entry.words = current_words;

            if (count_spanish_chars(current_text) > static_cast<size_t>(options.spanish_max_chars)) {
                entry.text = split_spanish_text(current_text,
                                                options.spanish_max_chars,
                                                options.max_lines,
                                                options.spanish_line_break_rules);
            } else {
                entry.text = current_text;
            }

            entries.push_back(entry);

            current_text.clear();
            current_words.clear();
            current_start = -1.0f;
        }
    };

    for (size_t i = 0; i < all_words.size(); ++i) {
        const Word& word = all_words[i];

        if (current_start >= 0 && !current_text.empty()) {
            float pause = word.start - last_word_end;
            if (pause > pause_threshold_s) {
                flush_entry();
            }
        }

        if (current_start < 0) {
            current_start = word.start;
        }

        if (!current_text.empty()) {
            std::string word_text = word.word;
            if (!word_text.empty()) {
                char first_char = word_text[0];
                if (first_char != '.' && first_char != ',' && first_char != '!' &&
                    first_char != '?' && first_char != ':' && first_char != ';') {
                    current_text += " ";
                }
            }
        }
        current_text += word.word;
        current_words.push_back(word);
        current_end = word.end;
        last_word_end = word.end;

        bool should_break = false;

        float duration = current_end - current_start;
        if (duration > options.spanish_max_duration) {
            should_break = true;
        }

        if (count_spanish_chars(current_text) >= static_cast<size_t>(options.spanish_max_chars)) {
            should_break = true;
        }

        float cps = calculate_spanish_cps(current_text, duration);
        if (cps > options.spanish_max_cps && duration >= 1.0f) {
            should_break = true;
        }

        if (options.spanish_break_on_sentence && ends_with_spanish_sentence_end(current_text)) {
            should_break = true;
        }

        if (should_break && options.spanish_line_break_rules) {
            if (is_spanish_short_word(word.word)) {
                should_break = false;
            }
        }

        if (should_break) {
            flush_entry();
        }
    }

    flush_entry();
    return entries;
}

// ═══════════════════════════════════════════════════════════
// Arabic-Specific Processing (Tier 1 - RTL)
// ═══════════════════════════════════════════════════════════

bool SubtitleExporter::is_arabic(uint32_t codepoint) {
    // Arabic block: U+0600 to U+06FF
    if (codepoint >= 0x0600 && codepoint <= 0x06FF) return true;

    // Arabic Supplement: U+0750 to U+077F
    if (codepoint >= 0x0750 && codepoint <= 0x077F) return true;

    // Arabic Extended-A: U+08A0 to U+08FF
    if (codepoint >= 0x08A0 && codepoint <= 0x08FF) return true;

    // Arabic Presentation Forms-A: U+FB50 to U+FDFF
    if (codepoint >= 0xFB50 && codepoint <= 0xFDFF) return true;

    // Arabic Presentation Forms-B: U+FE70 to U+FEFF
    if (codepoint >= 0xFE70 && codepoint <= 0xFEFF) return true;

    return false;
}

size_t SubtitleExporter::count_arabic_chars(const std::string& text) {
    size_t count = 0;
    std::vector<std::string> chars = utf8_to_chars(text);

    for (const auto& ch : chars) {
        if (ch == " " || ch == "\n" || ch == "\r" || ch == "\t") {
            continue;
        }

        uint32_t cp = utf8_to_codepoint(ch);

        if (is_arabic(cp)) {
            count++;
            continue;
        }

        // Count ASCII printable characters
        if (cp >= 0x21 && cp <= 0x7E) {
            count++;
        }
    }

    return count;
}

bool SubtitleExporter::ends_with_arabic_sentence_end(const std::string& text) {
    std::string last_char = get_last_utf8_char(text);

    // Arabic sentence-ending punctuation
    // ASCII: . ! ?
    // Arabic question mark: ؟ (U+061F)
    // Arabic comma: ، (U+060C)
    return last_char == "." || last_char == "!" || last_char == "?" ||
           last_char == "\xD8\x9F" ||  // ؟ U+061F ARABIC QUESTION MARK
           last_char == "\xD8\x8C";    // ، U+060C ARABIC COMMA
}

std::string SubtitleExporter::split_arabic_text(const std::string& text,
                                                 int max_chars,
                                                 int max_lines,
                                                 bool /* ligature_safe */) {
    if (count_arabic_chars(text) <= static_cast<size_t>(max_chars)) {
        return text;
    }

    // Arabic uses spaces between words
    std::vector<std::string> words;
    std::string current_word;

    for (size_t i = 0; i < text.size(); ) {
        size_t char_len = utf8_char_length(static_cast<unsigned char>(text[i]));
        std::string ch = text.substr(i, char_len);

        if (ch == " " || ch == "\n" || ch == "\r" || ch == "\t") {
            if (!current_word.empty()) {
                words.push_back(current_word);
                current_word.clear();
            }
        } else {
            current_word += ch;
        }
        i += char_len;
    }
    if (!current_word.empty()) {
        words.push_back(current_word);
    }

    // Build lines (Arabic display is RTL, but storage is logical order)
    std::vector<std::string> lines;
    std::string current_line;
    size_t current_char_count = 0;

    for (size_t i = 0; i < words.size() && static_cast<int>(lines.size()) < max_lines; ++i) {
        const std::string& word = words[i];
        size_t word_len = count_arabic_chars(word);

        size_t new_count = current_char_count + word_len;
        if (!current_line.empty()) {
            new_count += 1;
        }

        if (new_count > static_cast<size_t>(max_chars) && !current_line.empty()) {
            lines.push_back(current_line);
            current_line = word;
            current_char_count = word_len;
        } else {
            if (!current_line.empty()) {
                current_line += " ";
                current_char_count += 1;
            }
            current_line += word;
            current_char_count += word_len;
        }
    }

    if (!current_line.empty() && static_cast<int>(lines.size()) < max_lines) {
        lines.push_back(current_line);
    }

    std::string result;
    for (size_t j = 0; j < lines.size(); ++j) {
        result += lines[j];
        if (j < lines.size() - 1) {
            result += "\n";
        }
    }

    return result;
}

float SubtitleExporter::calculate_arabic_cps(const std::string& text, float duration) {
    if (duration <= 0.0f) return 0.0f;
    size_t char_count = count_arabic_chars(text);
    return static_cast<float>(char_count) / duration;
}

std::vector<SubtitleEntry> SubtitleExporter::segment_arabic(
    const std::vector<Segment>& segments,
    const SubtitleExportOptions& options) {

    std::vector<SubtitleEntry> entries;

    std::vector<Word> all_words;
    for (const auto& seg : segments) {
        for (const auto& word : seg.words) {
            all_words.push_back(word);
        }
    }

    if (all_words.empty()) {
        return segments_to_entries(segments, options);
    }

    int entry_index = 1;
    std::string current_text;
    std::vector<Word> current_words;
    float current_start = -1.0f;
    float current_end = 0.0f;
    float last_word_end = 0.0f;

    float pause_threshold_s = options.arabic_pause_threshold_ms / 1000.0f;

    auto flush_entry = [&]() {
        if (!current_text.empty() && current_start >= 0) {
            float duration = current_end - current_start;
            if (duration < options.arabic_min_duration) {
                current_end = current_start + options.arabic_min_duration;
            }

            SubtitleEntry entry;
            entry.index = entry_index++;
            entry.start = current_start;
            entry.end = current_end;
            entry.words = current_words;

            if (count_arabic_chars(current_text) > static_cast<size_t>(options.arabic_max_chars)) {
                entry.text = split_arabic_text(current_text,
                                               options.arabic_max_chars,
                                               options.max_lines,
                                               options.arabic_ligature_safe);
            } else {
                entry.text = current_text;
            }

            entries.push_back(entry);

            current_text.clear();
            current_words.clear();
            current_start = -1.0f;
        }
    };

    for (size_t i = 0; i < all_words.size(); ++i) {
        const Word& word = all_words[i];

        if (current_start >= 0 && !current_text.empty()) {
            float pause = word.start - last_word_end;
            if (pause > pause_threshold_s) {
                flush_entry();
            }
        }

        if (current_start < 0) {
            current_start = word.start;
        }

        if (!current_text.empty()) {
            current_text += " ";
        }
        current_text += word.word;
        current_words.push_back(word);
        current_end = word.end;
        last_word_end = word.end;

        bool should_break = false;

        float duration = current_end - current_start;
        if (duration > options.arabic_max_duration) {
            should_break = true;
        }

        if (count_arabic_chars(current_text) >= static_cast<size_t>(options.arabic_max_chars)) {
            should_break = true;
        }

        float cps = calculate_arabic_cps(current_text, duration);
        if (cps > options.arabic_max_cps && duration >= 1.0f) {
            should_break = true;
        }

        if (options.arabic_break_on_sentence && ends_with_arabic_sentence_end(current_text)) {
            should_break = true;
        }

        if (should_break) {
            flush_entry();
        }
    }

    flush_entry();
    return entries;
}

// ═══════════════════════════════════════════════════════════
// Hindi/Devanagari-Specific Processing (Tier 1)
// ═══════════════════════════════════════════════════════════

bool SubtitleExporter::is_devanagari(uint32_t codepoint) {
    // Devanagari: U+0900 to U+097F
    if (codepoint >= 0x0900 && codepoint <= 0x097F) return true;

    // Devanagari Extended: U+A8E0 to U+A8FF
    if (codepoint >= 0xA8E0 && codepoint <= 0xA8FF) return true;

    // Vedic Extensions: U+1CD0 to U+1CFF
    if (codepoint >= 0x1CD0 && codepoint <= 0x1CFF) return true;

    return false;
}

size_t SubtitleExporter::count_hindi_chars(const std::string& text) {
    size_t count = 0;
    std::vector<std::string> chars = utf8_to_chars(text);

    for (const auto& ch : chars) {
        if (ch == " " || ch == "\n" || ch == "\r" || ch == "\t") {
            continue;
        }

        uint32_t cp = utf8_to_codepoint(ch);

        if (is_devanagari(cp)) {
            count++;
            continue;
        }

        // Count ASCII printable
        if (cp >= 0x21 && cp <= 0x7E) {
            count++;
        }
    }

    return count;
}

bool SubtitleExporter::ends_with_hindi_sentence_end(const std::string& text) {
    std::string last_char = get_last_utf8_char(text);

    // Hindi sentence-ending punctuation
    // Devanagari Danda: । (U+0964)
    // Devanagari Double Danda: ॥ (U+0965)
    // ASCII: . ! ?
    return last_char == "." || last_char == "!" || last_char == "?" ||
           last_char == "\xE0\xA5\xA4" ||  // । U+0964 DEVANAGARI DANDA
           last_char == "\xE0\xA5\xA5";    // ॥ U+0965 DEVANAGARI DOUBLE DANDA
}

std::string SubtitleExporter::split_hindi_text(const std::string& text,
                                                int max_chars,
                                                int max_lines) {
    if (count_hindi_chars(text) <= static_cast<size_t>(max_chars)) {
        return text;
    }

    // Hindi uses spaces between words
    std::vector<std::string> words;
    std::string current_word;

    for (size_t i = 0; i < text.size(); ) {
        size_t char_len = utf8_char_length(static_cast<unsigned char>(text[i]));
        std::string ch = text.substr(i, char_len);

        if (ch == " " || ch == "\n" || ch == "\r" || ch == "\t") {
            if (!current_word.empty()) {
                words.push_back(current_word);
                current_word.clear();
            }
        } else {
            current_word += ch;
        }
        i += char_len;
    }
    if (!current_word.empty()) {
        words.push_back(current_word);
    }

    std::vector<std::string> lines;
    std::string current_line;
    size_t current_char_count = 0;

    for (size_t i = 0; i < words.size() && static_cast<int>(lines.size()) < max_lines; ++i) {
        const std::string& word = words[i];
        size_t word_len = count_hindi_chars(word);

        size_t new_count = current_char_count + word_len;
        if (!current_line.empty()) {
            new_count += 1;
        }

        if (new_count > static_cast<size_t>(max_chars) && !current_line.empty()) {
            lines.push_back(current_line);
            current_line = word;
            current_char_count = word_len;
        } else {
            if (!current_line.empty()) {
                current_line += " ";
                current_char_count += 1;
            }
            current_line += word;
            current_char_count += word_len;
        }
    }

    if (!current_line.empty() && static_cast<int>(lines.size()) < max_lines) {
        lines.push_back(current_line);
    }

    std::string result;
    for (size_t j = 0; j < lines.size(); ++j) {
        result += lines[j];
        if (j < lines.size() - 1) {
            result += "\n";
        }
    }

    return result;
}

float SubtitleExporter::calculate_hindi_cps(const std::string& text, float duration) {
    if (duration <= 0.0f) return 0.0f;
    size_t char_count = count_hindi_chars(text);
    return static_cast<float>(char_count) / duration;
}

std::vector<SubtitleEntry> SubtitleExporter::segment_hindi(
    const std::vector<Segment>& segments,
    const SubtitleExportOptions& options) {

    std::vector<SubtitleEntry> entries;

    std::vector<Word> all_words;
    for (const auto& seg : segments) {
        for (const auto& word : seg.words) {
            all_words.push_back(word);
        }
    }

    if (all_words.empty()) {
        return segments_to_entries(segments, options);
    }

    int entry_index = 1;
    std::string current_text;
    std::vector<Word> current_words;
    float current_start = -1.0f;
    float current_end = 0.0f;
    float last_word_end = 0.0f;

    float pause_threshold_s = options.hindi_pause_threshold_ms / 1000.0f;

    auto flush_entry = [&]() {
        if (!current_text.empty() && current_start >= 0) {
            float duration = current_end - current_start;
            if (duration < options.hindi_min_duration) {
                current_end = current_start + options.hindi_min_duration;
            }

            SubtitleEntry entry;
            entry.index = entry_index++;
            entry.start = current_start;
            entry.end = current_end;
            entry.words = current_words;

            if (count_hindi_chars(current_text) > static_cast<size_t>(options.hindi_max_chars)) {
                entry.text = split_hindi_text(current_text,
                                              options.hindi_max_chars,
                                              options.max_lines);
            } else {
                entry.text = current_text;
            }

            entries.push_back(entry);

            current_text.clear();
            current_words.clear();
            current_start = -1.0f;
        }
    };

    for (size_t i = 0; i < all_words.size(); ++i) {
        const Word& word = all_words[i];

        if (current_start >= 0 && !current_text.empty()) {
            float pause = word.start - last_word_end;
            if (pause > pause_threshold_s) {
                flush_entry();
            }
        }

        if (current_start < 0) {
            current_start = word.start;
        }

        if (!current_text.empty()) {
            current_text += " ";
        }
        current_text += word.word;
        current_words.push_back(word);
        current_end = word.end;
        last_word_end = word.end;

        bool should_break = false;

        float duration = current_end - current_start;
        if (duration > options.hindi_max_duration) {
            should_break = true;
        }

        if (count_hindi_chars(current_text) >= static_cast<size_t>(options.hindi_max_chars)) {
            should_break = true;
        }

        float cps = calculate_hindi_cps(current_text, duration);
        if (cps > options.hindi_max_cps && duration >= 1.0f) {
            should_break = true;
        }

        if (options.hindi_break_on_sentence && ends_with_hindi_sentence_end(current_text)) {
            should_break = true;
        }

        if (should_break) {
            flush_entry();
        }
    }

    flush_entry();
    return entries;
}

// ═══════════════════════════════════════════════════════════
// Indonesian-Specific Processing (Tier 3)
// ═══════════════════════════════════════════════════════════

size_t SubtitleExporter::count_indonesian_chars(const std::string& text) {
    // Indonesian uses Latin script - same counting as Spanish/English
    return count_spanish_chars(text);
}

float SubtitleExporter::calculate_indonesian_cps(const std::string& text, float duration) {
    if (duration <= 0.0f) return 0.0f;
    size_t char_count = count_indonesian_chars(text);
    return static_cast<float>(char_count) / duration;
}

std::vector<SubtitleEntry> SubtitleExporter::segment_indonesian(
    const std::vector<Segment>& segments,
    const SubtitleExportOptions& options) {

    std::vector<SubtitleEntry> entries;

    std::vector<Word> all_words;
    for (const auto& seg : segments) {
        for (const auto& word : seg.words) {
            all_words.push_back(word);
        }
    }

    if (all_words.empty()) {
        return segments_to_entries(segments, options);
    }

    int entry_index = 1;
    std::string current_text;
    std::vector<Word> current_words;
    float current_start = -1.0f;
    float current_end = 0.0f;
    float last_word_end = 0.0f;

    float pause_threshold_s = options.indonesian_pause_threshold_ms / 1000.0f;

    auto flush_entry = [&]() {
        if (!current_text.empty() && current_start >= 0) {
            float duration = current_end - current_start;
            if (duration < options.indonesian_min_duration) {
                current_end = current_start + options.indonesian_min_duration;
            }

            SubtitleEntry entry;
            entry.index = entry_index++;
            entry.start = current_start;
            entry.end = current_end;
            entry.words = current_words;

            if (count_indonesian_chars(current_text) > static_cast<size_t>(options.indonesian_max_chars)) {
                entry.text = split_text(current_text, options.indonesian_max_chars, options.max_lines);
            } else {
                entry.text = current_text;
            }

            entries.push_back(entry);

            current_text.clear();
            current_words.clear();
            current_start = -1.0f;
        }
    };

    for (size_t i = 0; i < all_words.size(); ++i) {
        const Word& word = all_words[i];

        if (current_start >= 0 && !current_text.empty()) {
            float pause = word.start - last_word_end;
            if (pause > pause_threshold_s) {
                flush_entry();
            }
        }

        if (current_start < 0) {
            current_start = word.start;
        }

        if (!current_text.empty()) {
            current_text += " ";
        }
        current_text += word.word;
        current_words.push_back(word);
        current_end = word.end;
        last_word_end = word.end;

        bool should_break = false;

        float duration = current_end - current_start;
        if (duration > options.indonesian_max_duration) {
            should_break = true;
        }

        if (count_indonesian_chars(current_text) >= static_cast<size_t>(options.indonesian_max_chars)) {
            should_break = true;
        }

        float cps = calculate_indonesian_cps(current_text, duration);
        if (cps > options.indonesian_max_cps && duration >= 1.0f) {
            should_break = true;
        }

        if (options.indonesian_break_on_sentence && ends_with_english_sentence_end(current_text)) {
            should_break = true;
        }

        if (should_break) {
            flush_entry();
        }
    }

    flush_entry();
    return entries;
}

// ═══════════════════════════════════════════════════════════
// Vietnamese-Specific Processing (Tier 3)
// ═══════════════════════════════════════════════════════════

size_t SubtitleExporter::count_vietnamese_chars(const std::string& text) {
    size_t count = 0;
    std::vector<std::string> chars = utf8_to_chars(text);

    for (const auto& ch : chars) {
        if (ch == " " || ch == "\n" || ch == "\r" || ch == "\t") {
            continue;
        }

        uint32_t cp = utf8_to_codepoint(ch);

        // Vietnamese uses Latin script with extensive diacritics
        // Basic Latin: U+0021 to U+007E
        // Latin-1 Supplement: U+00C0 to U+00FF
        // Latin Extended-A: U+0100 to U+017F
        // Latin Extended-B: U+0180 to U+024F
        // Latin Extended Additional: U+1E00 to U+1EFF (Vietnamese specific)
        if ((cp >= 0x0021 && cp <= 0x007E) ||
            (cp >= 0x00C0 && cp <= 0x00FF) ||
            (cp >= 0x0100 && cp <= 0x024F) ||
            (cp >= 0x1E00 && cp <= 0x1EFF)) {
            count++;
        }
    }

    return count;
}

bool SubtitleExporter::ends_with_vietnamese_sentence_end(const std::string& text) {
    // Vietnamese uses standard Latin punctuation
    return ends_with_english_sentence_end(text);
}

float SubtitleExporter::calculate_vietnamese_cps(const std::string& text, float duration) {
    if (duration <= 0.0f) return 0.0f;
    size_t char_count = count_vietnamese_chars(text);
    return static_cast<float>(char_count) / duration;
}

std::vector<SubtitleEntry> SubtitleExporter::segment_vietnamese(
    const std::vector<Segment>& segments,
    const SubtitleExportOptions& options) {

    std::vector<SubtitleEntry> entries;

    std::vector<Word> all_words;
    for (const auto& seg : segments) {
        for (const auto& word : seg.words) {
            all_words.push_back(word);
        }
    }

    if (all_words.empty()) {
        return segments_to_entries(segments, options);
    }

    int entry_index = 1;
    std::string current_text;
    std::vector<Word> current_words;
    float current_start = -1.0f;
    float current_end = 0.0f;
    float last_word_end = 0.0f;

    float pause_threshold_s = options.vietnamese_pause_threshold_ms / 1000.0f;

    auto flush_entry = [&]() {
        if (!current_text.empty() && current_start >= 0) {
            float duration = current_end - current_start;
            if (duration < options.vietnamese_min_duration) {
                current_end = current_start + options.vietnamese_min_duration;
            }

            SubtitleEntry entry;
            entry.index = entry_index++;
            entry.start = current_start;
            entry.end = current_end;
            entry.words = current_words;

            if (count_vietnamese_chars(current_text) > static_cast<size_t>(options.vietnamese_max_chars)) {
                entry.text = split_text(current_text, options.vietnamese_max_chars, options.max_lines);
            } else {
                entry.text = current_text;
            }

            entries.push_back(entry);

            current_text.clear();
            current_words.clear();
            current_start = -1.0f;
        }
    };

    for (size_t i = 0; i < all_words.size(); ++i) {
        const Word& word = all_words[i];

        if (current_start >= 0 && !current_text.empty()) {
            float pause = word.start - last_word_end;
            if (pause > pause_threshold_s) {
                flush_entry();
            }
        }

        if (current_start < 0) {
            current_start = word.start;
        }

        // Vietnamese is monosyllabic - each word is typically one syllable
        if (!current_text.empty()) {
            current_text += " ";
        }
        current_text += word.word;
        current_words.push_back(word);
        current_end = word.end;
        last_word_end = word.end;

        bool should_break = false;

        float duration = current_end - current_start;
        if (duration > options.vietnamese_max_duration) {
            should_break = true;
        }

        if (count_vietnamese_chars(current_text) >= static_cast<size_t>(options.vietnamese_max_chars)) {
            should_break = true;
        }

        float cps = calculate_vietnamese_cps(current_text, duration);
        if (cps > options.vietnamese_max_cps && duration >= 1.0f) {
            should_break = true;
        }

        if (options.vietnamese_break_on_sentence && ends_with_vietnamese_sentence_end(current_text)) {
            should_break = true;
        }

        if (should_break) {
            flush_entry();
        }
    }

    flush_entry();
    return entries;
}

// ═══════════════════════════════════════════════════════════
// Thai-Specific Processing (Tier 3)
// ═══════════════════════════════════════════════════════════

bool SubtitleExporter::is_thai(uint32_t codepoint) {
    // Thai block: U+0E00 to U+0E7F
    return codepoint >= 0x0E00 && codepoint <= 0x0E7F;
}

size_t SubtitleExporter::count_thai_chars(const std::string& text) {
    size_t count = 0;
    std::vector<std::string> chars = utf8_to_chars(text);

    for (const auto& ch : chars) {
        if (ch == " " || ch == "\n" || ch == "\r" || ch == "\t") {
            continue;
        }

        uint32_t cp = utf8_to_codepoint(ch);

        if (is_thai(cp)) {
            count++;
            continue;
        }

        // Count ASCII printable
        if (cp >= 0x21 && cp <= 0x7E) {
            count++;
        }
    }

    return count;
}

bool SubtitleExporter::ends_with_thai_sentence_end(const std::string& text) {
    std::string last_char = get_last_utf8_char(text);

    // Thai uses ASCII punctuation and Thai specific marks
    // Thai Character Paiyannoi: ฯ (U+0E2F) - abbreviation
    // Thai Character Maiyamok: ๆ (U+0E46) - repetition
    // Plus standard ASCII
    return last_char == "." || last_char == "!" || last_char == "?" ||
           last_char == "\xE0\xB8\xAF" ||  // ฯ U+0E2F
           last_char == "\xE0\xB9\x86";    // ๆ U+0E46
}

std::string SubtitleExporter::split_thai_text(const std::string& text,
                                               int max_chars,
                                               int max_lines) {
    // Thai doesn't have spaces between words, similar to Japanese/Chinese
    // We split at character limits since word boundaries are from Whisper

    if (count_thai_chars(text) <= static_cast<size_t>(max_chars)) {
        return text;
    }

    std::vector<std::string> chars = utf8_to_chars(text);
    std::vector<std::string> lines;
    std::string current_line;
    size_t current_char_count = 0;

    for (size_t i = 0; i < chars.size() && static_cast<int>(lines.size()) < max_lines; ++i) {
        const std::string& ch = chars[i];

        bool is_space = (ch == " " || ch == "\n" || ch == "\r");
        if (!is_space) {
            current_char_count++;
        }

        current_line += ch;

        // Break at space or character limit
        bool should_break = false;
        if (current_char_count >= static_cast<size_t>(max_chars)) {
            should_break = true;
        }

        // Prefer breaking at spaces if available
        if (is_space && current_char_count >= static_cast<size_t>(max_chars * 2 / 3)) {
            should_break = true;
        }

        if (should_break) {
            // Trim trailing space
            while (!current_line.empty() && current_line.back() == ' ') {
                current_line.pop_back();
            }
            if (!current_line.empty()) {
                lines.push_back(current_line);
            }
            current_line.clear();
            current_char_count = 0;
        }
    }

    if (!current_line.empty() && static_cast<int>(lines.size()) < max_lines) {
        while (!current_line.empty() && current_line.back() == ' ') {
            current_line.pop_back();
        }
        if (!current_line.empty()) {
            lines.push_back(current_line);
        }
    }

    std::string result;
    for (size_t j = 0; j < lines.size(); ++j) {
        result += lines[j];
        if (j < lines.size() - 1) {
            result += "\n";
        }
    }

    return result;
}

float SubtitleExporter::calculate_thai_cps(const std::string& text, float duration) {
    if (duration <= 0.0f) return 0.0f;
    size_t char_count = count_thai_chars(text);
    return static_cast<float>(char_count) / duration;
}

std::vector<SubtitleEntry> SubtitleExporter::segment_thai(
    const std::vector<Segment>& segments,
    const SubtitleExportOptions& options) {

    std::vector<SubtitleEntry> entries;

    std::vector<Word> all_words;
    for (const auto& seg : segments) {
        for (const auto& word : seg.words) {
            all_words.push_back(word);
        }
    }

    if (all_words.empty()) {
        return segments_to_entries(segments, options);
    }

    int entry_index = 1;
    std::string current_text;
    std::vector<Word> current_words;
    float current_start = -1.0f;
    float current_end = 0.0f;
    float last_word_end = 0.0f;

    float pause_threshold_s = options.thai_pause_threshold_ms / 1000.0f;

    auto flush_entry = [&]() {
        if (!current_text.empty() && current_start >= 0) {
            float duration = current_end - current_start;
            if (duration < options.thai_min_duration) {
                current_end = current_start + options.thai_min_duration;
            }

            SubtitleEntry entry;
            entry.index = entry_index++;
            entry.start = current_start;
            entry.end = current_end;
            entry.words = current_words;

            if (count_thai_chars(current_text) > static_cast<size_t>(options.thai_max_chars)) {
                entry.text = split_thai_text(current_text,
                                             options.thai_max_chars,
                                             options.max_lines);
            } else {
                entry.text = current_text;
            }

            entries.push_back(entry);

            current_text.clear();
            current_words.clear();
            current_start = -1.0f;
        }
    };

    for (size_t i = 0; i < all_words.size(); ++i) {
        const Word& word = all_words[i];

        if (current_start >= 0 && !current_text.empty()) {
            float pause = word.start - last_word_end;
            if (pause > pause_threshold_s) {
                flush_entry();
            }
        }

        if (current_start < 0) {
            current_start = word.start;
        }

        // Thai has no spaces between words (Whisper provides word boundaries)
        current_text += word.word;
        current_words.push_back(word);
        current_end = word.end;
        last_word_end = word.end;

        bool should_break = false;

        float duration = current_end - current_start;
        if (duration > options.thai_max_duration) {
            should_break = true;
        }

        if (count_thai_chars(current_text) >= static_cast<size_t>(options.thai_max_chars)) {
            should_break = true;
        }

        float cps = calculate_thai_cps(current_text, duration);
        if (cps > options.thai_max_cps && duration >= 1.0f) {
            should_break = true;
        }

        if (options.thai_break_on_sentence && ends_with_thai_sentence_end(current_text)) {
            should_break = true;
        }

        if (should_break) {
            flush_entry();
        }
    }

    flush_entry();
    return entries;
}

// ═══════════════════════════════════════════════════════════
// Metadata JSON (Loki Studio format)
// ═══════════════════════════════════════════════════════════

namespace SubtitleMetadata {

// Helper: Escape JSON string
static std::string escape_json_string(const std::string& str) {
    std::string escaped;
    escaped.reserve(str.size());

    for (char c : str) {
        switch (c) {
            case '"':  escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (c >= 0 && c < 32) {
                    // Control character - use \uXXXX format
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    escaped += buf;
                } else {
                    escaped += c;
                }
        }
    }
    return escaped;
}

std::string generate_metadata_json(const std::vector<Segment>& segments,
                                   const std::string& video_path,
                                   const std::string& whisper_model,
                                   const std::string& language,
                                   float duration) {
    // Generate output path
    std::filesystem::path video(video_path);
    std::filesystem::path metadata_path = video.parent_path();
    metadata_path /= video.stem().string() + "_metadata.json";

    // Build full transcript text
    std::string full_text;
    for (const auto& seg : segments) {
        full_text += seg.text + " ";
    }

    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::system_clock::to_time_t(now);

    // Build JSON manually
    std::ostringstream json;
    json << std::setprecision(2) << std::fixed;

    json << "{\n";
    json << "  \"video_file\": \"" << escape_json_string(video.filename().string()) << "\",\n";
    json << "  \"processed_date\": " << timestamp << ",\n";
    json << "  \"whisper_model\": \"" << escape_json_string(whisper_model) << "\",\n";
    json << "  \"engine\": \"muninn-faster-whisper\",\n";
    json << "  \"device\": \"cuda\",\n";
    json << "  \"transcriptions\": {\n";
    json << "    \"channel_1\": {\n";
    json << "      \"text\": \"" << escape_json_string(full_text) << "\",\n";
    json << "      \"segments\": [\n";

    // Write segments
    for (size_t i = 0; i < segments.size(); ++i) {
        const auto& seg = segments[i];
        json << "        {\n";
        json << "          \"start\": " << seg.start << ",\n";
        json << "          \"end\": " << seg.end << ",\n";
        json << "          \"text\": \"" << escape_json_string(seg.text) << "\"";

        // Optional: Add words
        if (!seg.words.empty()) {
            json << ",\n          \"words\": [\n";
            for (size_t j = 0; j < seg.words.size(); ++j) {
                const auto& word = seg.words[j];
                json << "            {\n";
                json << "              \"word\": \"" << escape_json_string(word.word) << "\",\n";
                json << "              \"start\": " << word.start << ",\n";
                json << "              \"end\": " << word.end << ",\n";
                json << "              \"probability\": " << word.probability << "\n";
                json << "            }" << (j < seg.words.size() - 1 ? "," : "") << "\n";
            }
            json << "          ]";
        }

        // Optional: Add speaker info
        if (seg.speaker_id >= 0) {
            json << ",\n          \"speaker_id\": " << seg.speaker_id << ",\n";
            json << "          \"speaker_label\": \"" << escape_json_string(seg.speaker_label) << "\"";
        }

        json << "\n        }" << (i < segments.size() - 1 ? "," : "") << "\n";
    }

    json << "      ],\n";
    json << "      \"language\": \"" << escape_json_string(language) << "\",\n";
    json << "      \"duration\": " << duration << "\n";
    json << "    }\n";
    json << "  }\n";
    json << "}\n";

    // Write to file
    std::ofstream file(metadata_path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to create metadata JSON: " + metadata_path.string());
    }

    file << json.str();
    file.close();

    return metadata_path.string();
}

std::vector<Segment> load_from_metadata_json(const std::string& metadata_json_path) {
    // TODO: Implement JSON parsing without nlohmann/json dependency
    // For now, this function is not implemented.
    // Consider using a lightweight JSON parser or installing nlohmann/json via vcpkg
    throw std::runtime_error("load_from_metadata_json not yet implemented - requires JSON parser");
}

} // namespace SubtitleMetadata

} // namespace muninn
