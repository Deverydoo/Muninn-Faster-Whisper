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
    auto entries = segments_to_entries(segments, options);

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
    auto entries = segments_to_entries(segments, options);

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
