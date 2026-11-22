#include "muninn/subtitle_export.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <map>
#include <optional>

namespace muninn {

namespace {

// Replace every occurrence of `from` in `text`, scanning left to right.
void replace_all(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

// WebVTT and HTML: & < > are markup, so they must be written as entities.
// Escaping > also takes care of a "-->" inside the text.
std::string escape_markup(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default:  out += c;
        }
    }
    return out;
}

// SRT has no escaping. A "-->" in the text would read as a timing line,
// so it becomes "->" (repeated, since "--->" contains it twice over).
std::string escape_srt_text(const std::string& text) {
    std::string out = text;
    while (out.find("-->") != std::string::npos) {
        replace_all(out, "-->", "->");
    }
    return out;
}

// Paths in the API are UTF-8. On Windows, std::filesystem::path(std::string)
// reads the bytes as the ANSI code page, so "José" or "田中" in a folder name
// would fail or come out garbled. u8path and u8string keep them intact.
std::filesystem::path utf8_path(const std::string& path) {
    return std::filesystem::u8path(path);
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

// Bytes in the UTF-8 sequence that starts with `lead` (1 for a stray byte).
size_t utf8_sequence_length(unsigned char lead) {
    if ((lead & 0x80) == 0x00) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

// Code points on a line, as a reader counts characters. Line breaks are not
// counted. "Ñandú" is 5, not 7; "привет" is 6, not 12.
size_t count_code_points(const std::string& text) {
    size_t count = 0;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c != '\n' && c != '\r') ++count;
        i += utf8_sequence_length(c);
    }
    return count;
}

// Code points that aren't whitespace: used to share a cue's time between the
// pieces it is split into, in proportion to how much text each piece holds.
size_t count_visible_code_points(const std::string& text) {
    size_t count = 0;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const size_t len = utf8_sequence_length(c);
        const bool ascii_space = (c == ' ' || c == '\n' || c == '\r' || c == '\t');
        const bool ideographic_space = (len == 3 && i + 2 < text.size() && c == 0xE3 &&
                                        static_cast<unsigned char>(text[i + 1]) == 0x80 &&
                                        static_cast<unsigned char>(text[i + 2]) == 0x80);
        if (!ascii_space && !ideographic_space) ++count;
        i += len;
    }
    return count;
}

std::vector<std::string> split_on_newlines(const std::string& text) {
    std::vector<std::string> lines;
    std::string line;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(line);
            line.clear();
        } else if (c != '\r') {
            line += c;
        }
    }
    lines.push_back(line);
    return lines;
}

std::string join_lines(const std::vector<std::string>& lines, size_t first, size_t last) {
    std::string out;
    for (size_t i = first; i < last; ++i) {
        if (i > first) out += '\n';
        out += lines[i];
    }
    return out;
}

// Lines of a cue without empty ones. A blank line ends a cue in SRT and VTT,
// so text must never contain one.
std::vector<std::string> non_empty_lines(const std::string& text) {
    std::vector<std::string> lines;
    for (auto& line : split_on_newlines(text)) {
        const bool blank = line.find_first_not_of(" \t") == std::string::npos;
        if (!blank) lines.push_back(std::move(line));
    }
    return lines;
}

// A cue whose text has more lines than max_lines becomes several cues of at
// most max_lines lines each, so no text is ever dropped. When the cue has word
// timings, each piece takes the time of the words it holds; otherwise the
// cue's time is shared in proportion to each piece's characters.
std::vector<SubtitleEntry> split_overflowing_entry(const SubtitleEntry& entry, int max_lines) {
    const std::vector<std::string> lines = non_empty_lines(entry.text);
    const size_t per_cue = static_cast<size_t>(std::max(1, max_lines));

    if (lines.size() <= per_cue) {
        SubtitleEntry copy = entry;
        copy.text = join_lines(lines, 0, lines.size());
        return {copy};
    }

    // Group the lines.
    struct Piece { size_t first, last, chars; };
    std::vector<Piece> pieces;
    size_t total_chars = 0;
    for (size_t i = 0; i < lines.size(); i += per_cue) {
        Piece p{i, std::min(lines.size(), i + per_cue), 0};
        for (size_t j = p.first; j < p.last; ++j) p.chars += count_visible_code_points(lines[j]);
        total_chars += p.chars;
        pieces.push_back(p);
    }
    if (total_chars == 0) total_chars = 1;

    const double start = entry.start;
    const double duration = std::max(0.0, static_cast<double>(entry.end) - start);

    // Hand the words out by their share of the text, measured at each word's
    // middle character.
    std::vector<std::vector<Word>> piece_words(pieces.size());
    if (!entry.words.empty()) {
        size_t word_chars_total = 0;
        for (const auto& w : entry.words) word_chars_total += count_visible_code_points(w.word);
        if (word_chars_total == 0) word_chars_total = entry.words.size();

        size_t before = 0;
        for (const auto& w : entry.words) {
            const size_t chars = count_visible_code_points(w.word);
            const double middle = (before + chars / 2.0) / static_cast<double>(word_chars_total);
            before += chars;

            double boundary = 0.0;
            size_t target = pieces.size() - 1;
            for (size_t k = 0; k < pieces.size(); ++k) {
                boundary += pieces[k].chars / static_cast<double>(total_chars);
                if (middle < boundary) { target = k; break; }
            }
            piece_words[target].push_back(w);
        }
    }

    // Start of each piece: its first word, or its share of the duration.
    std::vector<double> starts(pieces.size(), start);
    size_t chars_before = 0;
    for (size_t k = 0; k < pieces.size(); ++k) {
        double candidate = start + duration * (chars_before / static_cast<double>(total_chars));
        chars_before += pieces[k].chars;
        if (k == 0) continue;  // the first piece keeps the cue's start
        if (!piece_words[k].empty()) candidate = piece_words[k].front().start;
        starts[k] = std::min(std::max(candidate, starts[k - 1]), start + duration);
    }

    std::vector<SubtitleEntry> out;
    for (size_t k = 0; k < pieces.size(); ++k) {
        SubtitleEntry piece = entry;
        piece.text = join_lines(lines, pieces[k].first, pieces[k].last);
        piece.words = piece_words[k];
        piece.start = static_cast<float>(starts[k]);
        piece.end = (k + 1 < pieces.size()) ? static_cast<float>(starts[k + 1]) : entry.end;
        if (piece.end < piece.start) piece.end = piece.start;
        out.push_back(std::move(piece));
    }
    return out;
}

// The last step for every way of building entries: no cue has more than
// max_lines lines, entries are in time order, and they are numbered 1..N.
std::vector<SubtitleEntry> finalize_entries(std::vector<SubtitleEntry> entries,
                                            const SubtitleExportOptions& options) {
    std::vector<SubtitleEntry> out;
    out.reserve(entries.size());
    for (auto& entry : entries) {
        if (!std::isfinite(entry.start) || entry.start < 0.0f) entry.start = 0.0f;
        if (!std::isfinite(entry.end) || entry.end < entry.start) entry.end = entry.start;
        for (auto& piece : split_overflowing_entry(entry, options.max_lines)) {
            if (piece.text.empty()) continue;
            out.push_back(std::move(piece));
        }
    }

    std::stable_sort(out.begin(), out.end(),
                     [](const SubtitleEntry& a, const SubtitleEntry& b) { return a.start < b.start; });

    // No cue may still be on screen when the next one starts. Karaoke hold
    // times and min-duration padding both run past the next cue, so clamp
    // each end to the next start. Two cues starting on the same millisecond
    // (two tracks talking at once) can't both be shown that way, so they
    // become one cue with both texts.
    std::vector<SubtitleEntry> clamped;
    clamped.reserve(out.size());
    for (auto& entry : out) {
        if (!clamped.empty()) {
            SubtitleEntry& prev = clamped.back();
            if (entry.start - prev.start < 0.0005f) {
                prev.text += "\n" + entry.text;
                prev.end = std::max(prev.end, entry.end);
                prev.words.insert(prev.words.end(), entry.words.begin(), entry.words.end());
                std::stable_sort(prev.words.begin(), prev.words.end(),
                                 [](const Word& a, const Word& b) { return a.start < b.start; });
                continue;
            }
            if (prev.end > entry.start) prev.end = entry.start;
        }
        clamped.push_back(std::move(entry));
    }
    out = std::move(clamped);

    for (size_t i = 0; i < out.size(); ++i) {
        out[i].index = static_cast<int>(i + 1);
    }
    return out;
}

// All words of the segments in time order (segments sorted by start first).
std::vector<Word> collect_words(const std::vector<Segment>& segments) {
    std::vector<const Segment*> ordered;
    ordered.reserve(segments.size());
    for (const auto& seg : segments) ordered.push_back(&seg);
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const Segment* a, const Segment* b) { return a->start < b->start; });

    std::vector<Word> words;
    for (const Segment* seg : ordered) {
        words.insert(words.end(), seg->words.begin(), seg->words.end());
    }
    return words;
}

using Segmenter = std::vector<SubtitleEntry> (*)(const std::vector<Segment>&,
                                                  const SubtitleExportOptions&);

// Word-based segmenters must never join words from different audio tracks
// into one cue. When the segments come from more than one track, run the
// segmenter once per track and combine the results. Returns nothing when
// there is only one track, so the caller carries on as normal.
std::optional<std::vector<SubtitleEntry>> segment_per_track(const std::vector<Segment>& segments,
                                                            const SubtitleExportOptions& options,
                                                            Segmenter segmenter) {
    std::map<int, std::vector<Segment>> by_track;
    for (const auto& seg : segments) by_track[seg.track_id].push_back(seg);
    if (by_track.size() <= 1) return std::nullopt;

    std::vector<SubtitleEntry> all;
    for (const auto& [track, track_segments] : by_track) {
        for (auto entry : segmenter(track_segments, options)) {
            entry.track_id = track;
            all.push_back(std::move(entry));
        }
    }
    return finalize_entries(std::move(all), options);
}

int single_track_id(const std::vector<Segment>& segments) {
    return segments.empty() ? 0 : segments.front().track_id;
}

// Open a subtitle file for writing. Honours overwrite_existing.
std::ofstream open_output(const std::string& output_path,
                          const SubtitleExportOptions& options,
                          const char* what) {
    const std::filesystem::path path = utf8_path(output_path);
    std::error_code ec;
    if (!options.overwrite_existing && std::filesystem::exists(path, ec)) {
        throw std::runtime_error(std::string(what) + " file already exists and overwrite_existing "
                                 "is false: " + output_path);
    }
    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error(std::string("Failed to create ") + what + " file: " + output_path);
    }
    return file;
}

// ASS text: { } and \ would start override tags or escapes, so they get a
// backslash in front (what FFmpeg does); a line break becomes \N.
std::string escape_ass_text(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        if (c == '{' || c == '}' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\N";
        } else if (c != '\r') {
            out += c;
        }
    }
    return out;
}

}  // namespace

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
        case SubtitleFormat::ASS:
            return export_ass(segments, video_path, options);
        default:
            throw std::runtime_error("Unsupported subtitle format");
    }
}

std::vector<SubtitleEntry> SubtitleExporter::build_entries(const std::vector<Segment>& segments,
                                                           const SubtitleExportOptions& options) {
    // Specialised modes give better timing when word timestamps exist.
    if (options.enable_karaoke_mode)    return build_karaoke_captions(segments, options);
    if (options.enable_thai_mode)       return segment_thai(segments, options);
    if (options.enable_vietnamese_mode) return segment_vietnamese(segments, options);
    if (options.enable_indonesian_mode) return segment_indonesian(segments, options);
    if (options.enable_hindi_mode)      return segment_hindi(segments, options);
    if (options.enable_arabic_mode)     return segment_arabic(segments, options);
    if (options.enable_spanish_mode)    return segment_spanish(segments, options);
    if (options.enable_russian_mode)    return segment_russian(segments, options);
    if (options.enable_chinese_mode)    return segment_chinese(segments, options);
    if (options.enable_korean_mode)     return segment_korean(segments, options);
    if (options.enable_japanese_mode)   return segment_japanese(segments, options);

    // No mode flag set: the language code can pick one.
    if (!options.language.empty()) {
        SubtitleExportOptions chosen = options;
        chosen.language.clear();
        const std::string& code = options.language;
        const std::string base = code.substr(0, code.find_first_of("-_"));
        if (base == "ja") chosen.enable_japanese_mode = true;
        else if (base == "ko") chosen.enable_korean_mode = true;
        else if (base == "zh") {
            chosen.enable_chinese_mode = true;
            chosen.chinese_traditional = (code == "zh-TW" || code == "zh-HK" || code == "zh-Hant" ||
                                          code == "zh_TW" || code == "zh_HK");
        }
        else if (base == "ru") chosen.enable_russian_mode = true;
        else if (base == "es" || base == "pt") chosen.enable_spanish_mode = true;
        else if (base == "ar") chosen.enable_arabic_mode = true;
        else if (base == "hi") chosen.enable_hindi_mode = true;
        else if (base == "id") chosen.enable_indonesian_mode = true;
        else if (base == "vi") chosen.enable_vietnamese_mode = true;
        else if (base == "th") chosen.enable_thai_mode = true;
        return build_entries(segments, chosen);
    }

    // Standard segment-based subtitles
    return segments_to_entries(segments, options);
}

std::string SubtitleExporter::export_srt(const std::vector<Segment>& segments,
                                        const std::string& video_path,
                                        const SubtitleExportOptions& options) {
    const std::string output_path = options.output_path.empty() ?
        generate_output_path(video_path, SubtitleFormat::SRT) :
        options.output_path;

    const std::vector<SubtitleEntry> entries = build_entries(segments, options);

    std::ofstream file = open_output(output_path, options, "SRT");

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
    const std::string output_path = options.output_path.empty() ?
        generate_output_path(video_path, SubtitleFormat::VTT) :
        options.output_path;

    const std::vector<SubtitleEntry> entries = build_entries(segments, options);

    std::ofstream file = open_output(output_path, options, "VTT");

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
    if (options.vtt_include_confidence_styling) {
        file << "STYLE\n";
        file << "::cue(.uncertain) {\n";
        file << "  color: #ff8080;\n";
        file << "}\n\n";
    }

    // Write cues
    for (const auto& entry : entries) {
        file << format_vtt_entry(entry, options);
        file << "\n";  // Blank line between cues
    }

    file.close();
    return output_path;
}

std::string SubtitleExporter::export_ass(const std::vector<Segment>& segments,
                                        const std::string& video_path,
                                        const SubtitleExportOptions& options) {
    const std::string output_path = options.output_path.empty() ?
        generate_output_path(video_path, SubtitleFormat::ASS) :
        options.output_path;

    const std::vector<SubtitleEntry> entries = build_entries(segments, options);

    std::ofstream file = open_output(output_path, options, "ASS");

    file << "[Script Info]\n"
            "; Written by Muninn\n"
            "ScriptType: v4.00+\n"
            "PlayResX: 1920\n"
            "PlayResY: 1080\n"
            "WrapStyle: 0\n"
            "ScaledBorderAndShadow: yes\n"
            "\n"
            "[V4+ Styles]\n"
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
            "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, "
            "BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
            "Style: Default,Arial,64,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,"
            "0,0,0,0,100,100,0,0,1,3,1,2,60,60,50,1\n"
            "\n"
            "[Events]\n"
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";

    for (const auto& entry : entries) {
        file << format_ass_entry(entry);
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

    // 1. One entry per segment, in time order, with its text unsplit.
    std::vector<SubtitleEntry> entries;
    entries.reserve(segments.size());
    for (const auto& seg : segments) {
        SubtitleEntry entry;
        entry.start = seg.start;
        entry.end = seg.end;
        entry.track_id = seg.track_id;
        entry.speaker_id = seg.speaker_id;
        entry.speaker_label = seg.speaker_label;
        entry.words = seg.words;
        const size_t first = seg.text.find_first_not_of(" \t\r\n");
        const size_t last = seg.text.find_last_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        entry.text = seg.text.substr(first, last - first + 1);
        entries.push_back(std::move(entry));
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const SubtitleEntry& a, const SubtitleEntry& b) { return a.start < b.start; });

    // 2. Optionally merge a segment into the cue just before it. Only within
    //    one track and one speaker, and never past max_duration or past what
    //    fits on max_lines lines.
    if (options.gap_threshold > 0.0f && entries.size() > 1) {
        const size_t max_chars = static_cast<size_t>(std::max(1, options.max_chars_per_line)) *
                                 static_cast<size_t>(std::max(1, options.max_lines));
        std::vector<SubtitleEntry> merged;
        merged.push_back(entries[0]);

        for (size_t i = 1; i < entries.size(); ++i) {
            SubtitleEntry& last = merged.back();
            const SubtitleEntry& curr = entries[i];

            const float gap = curr.start - last.end;
            const std::string joined = last.text + " " + curr.text;
            const bool can_merge =
                gap < options.gap_threshold &&
                curr.track_id == last.track_id &&
                curr.speaker_id == last.speaker_id &&
                std::max(last.end, curr.end) - last.start <= options.max_duration &&
                count_code_points(joined) <= max_chars;

            if (can_merge) {
                last.end = std::max(last.end, curr.end);
                last.text = joined;
                last.words.insert(last.words.end(), curr.words.begin(), curr.words.end());
            } else {
                merged.push_back(curr);
            }
        }
        entries = std::move(merged);
    }

    // 3. Speaker labels, line splitting, more cues for text that doesn't fit,
    //    then the duration limits.
    std::vector<SubtitleEntry> out;
    out.reserve(entries.size());
    for (auto& entry : entries) {
        if (options.include_speakers && entry.speaker_id >= 0) {
            std::string label;
            const auto named = options.speaker_names.find(entry.speaker_id);
            if (named != options.speaker_names.end()) {
                label = named->second;
            } else if (!entry.speaker_label.empty()) {
                label = entry.speaker_label;
            } else {
                label = "Speaker " + std::to_string(entry.speaker_id);
            }
            entry.text = apply_speaker_format(options.speaker_format, entry.speaker_id, label, entry.text);
        }

        if (options.auto_split_long_text) {
            entry.text = split_text(entry.text, options.max_chars_per_line, options.max_lines);
        }

        for (auto& piece : split_overflowing_entry(entry, options.max_lines)) {
            const float duration = piece.end - piece.start;
            if (duration < options.min_duration) {
                piece.end = piece.start + options.min_duration;
            }
            if (duration > options.max_duration) {
                piece.end = piece.start + options.max_duration;
            }
            out.push_back(std::move(piece));
        }
    }

    return finalize_entries(std::move(out), options);
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
    oss << escape_srt_text(entry.text) << "\n";

    return oss.str();
}

// HH:MM:SS<sep>mmm. The time is rounded to the nearest millisecond once and
// then split with integer maths, so 1.9999 s gives 00:00:02,000 rather than
// 00:00:01,999 or 00:00:01,1000. Negative and NaN times become zero.
static std::string format_hmsm(float seconds, char separator) {
    long long total_ms = 0;
    if (std::isfinite(seconds) && seconds > 0.0f) {
        total_ms = std::llround(static_cast<double>(seconds) * 1000.0);
    } else if (std::isinf(seconds) && seconds > 0.0f) {
        total_ms = 99LL * 3600000LL + 59LL * 60000LL + 59LL * 1000LL + 999LL;
    }

    const long long hours = total_ms / 3600000LL;
    const long long minutes = (total_ms / 60000LL) % 60LL;
    const long long secs = (total_ms / 1000LL) % 60LL;
    const long long millis = total_ms % 1000LL;

    char buf[48];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld%c%03lld",
                  hours, minutes, secs, separator, millis);
    return buf;
}

std::string SubtitleExporter::format_srt_timestamp(float seconds) {
    return format_hmsm(seconds, ',');
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

    const bool voice = entry.speaker_id >= 0 &&
                       (options.vtt_include_speaker_colors || options.vtt_include_word_timestamps);
    if (voice) {
        oss << "<v Speaker" << entry.speaker_id << ">";
    }

    const bool per_word = !entry.words.empty() &&
                          (options.vtt_include_word_timestamps || options.vtt_include_confidence_styling);
    if (per_word) {
        // Words are written one by one so each can carry its own timestamp
        // tag (<00:00:01.250>, the time it is spoken) and confidence class.
        // Line breaks fall where the cue text has them: after the word that
        // fills a line's characters. Words are separated by a space only when
        // the cue text has spaces (not for Chinese, Japanese or Thai).
        const std::vector<std::string> lines = non_empty_lines(entry.text);
        std::vector<size_t> line_ends;  // visible characters up to the end of each line
        size_t running = 0;
        for (const auto& line : lines) {
            running += count_visible_code_points(line);
            line_ends.push_back(running);
        }
        const bool spaced = entry.text.find(' ') != std::string::npos;

        size_t written = 0;
        size_t line = 0;
        float last_tag = entry.start;
        for (size_t i = 0; i < entry.words.size(); ++i) {
            const Word& word = entry.words[i];
            const size_t begin = word.word.find_first_not_of(' ');
            const std::string text = (begin == std::string::npos) ? "" : word.word.substr(begin);
            if (text.empty()) continue;

            if (written > 0) {
                if (line + 1 < line_ends.size() && written >= line_ends[line]) {
                    oss << "\n";
                    ++line;
                } else if (spaced) {
                    oss << " ";
                }
            }

            // Timestamp tags must lie inside the cue and never go backwards.
            if (options.vtt_include_word_timestamps && written > 0) {
                float t = std::max(word.start, last_tag);
                t = std::min(std::max(t, entry.start), entry.end);
                oss << "<" << format_vtt_timestamp(t) << ">";
                last_tag = t;
            }

            const bool uncertain = options.vtt_include_confidence_styling && word.probability < 0.5f;
            if (uncertain) oss << "<c.uncertain>";
            oss << escape_markup(text);
            if (uncertain) oss << "</c>";

            written += count_visible_code_points(text);
        }
    } else {
        oss << escape_markup(entry.text);
    }

    if (voice) {
        oss << "</v>";
    }
    oss << "\n";

    return oss.str();
}

std::string SubtitleExporter::format_vtt_timestamp(float seconds) {
    return format_hmsm(seconds, '.');
}

// ═══════════════════════════════════════════════════════════
// ASS Formatting
// ═══════════════════════════════════════════════════════════

std::string SubtitleExporter::format_ass_timestamp(float seconds) {
    long long total_cs = 0;
    if (std::isfinite(seconds) && seconds > 0.0f) {
        total_cs = std::llround(static_cast<double>(seconds) * 100.0);
    }
    const long long hours = total_cs / 360000LL;
    const long long minutes = (total_cs / 6000LL) % 60LL;
    const long long secs = (total_cs / 100LL) % 60LL;
    const long long centis = total_cs % 100LL;

    char buf[48];
    std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld.%02lld", hours, minutes, secs, centis);
    return buf;
}

std::string SubtitleExporter::format_ass_entry(const SubtitleEntry& entry) {
    // The Name field is one CSV column, so it can't hold a comma.
    std::string name = entry.speaker_label;
    if (name.empty() && entry.speaker_id >= 0) {
        name = "Speaker " + std::to_string(entry.speaker_id);
    }
    std::replace(name.begin(), name.end(), ',', ' ');
    std::replace(name.begin(), name.end(), '\n', ' ');

    std::ostringstream oss;
    oss << "Dialogue: 0," << format_ass_timestamp(entry.start) << ","
        << format_ass_timestamp(entry.end) << ",Default," << name << ",0,0,0,,"
        << escape_ass_text(entry.text) << "\n";
    return oss.str();
}

// ═══════════════════════════════════════════════════════════
// Utilities
// ═══════════════════════════════════════════════════════════

std::string SubtitleExporter::generate_output_path(const std::string& video_path,
                                                  SubtitleFormat format) {
    std::filesystem::path output = utf8_path(video_path);

    switch (format) {
        case SubtitleFormat::SRT:
            output.replace_extension(".srt");
            break;
        case SubtitleFormat::VTT:
            output.replace_extension(".vtt");
            break;
        case SubtitleFormat::ASS:
            output.replace_extension(".ass");
            break;
        default:
            output.replace_extension(".sub");
    }

    return path_to_utf8(output);
}

std::string SubtitleExporter::split_text(const std::string& text,
                                        int max_chars_per_line,
                                        int /* max_lines: never truncates, see the header */) {
    const size_t max_chars = static_cast<size_t>(std::max(1, max_chars_per_line));
    if (count_code_points(text) <= max_chars && text.find('\n') == std::string::npos) {
        return text;
    }

    std::vector<std::string> lines;
    std::string current_line;
    size_t current_chars = 0;
    std::istringstream words(text);
    std::string word;

    while (words >> word) {
        const size_t word_chars = count_code_points(word);
        if (current_line.empty()) {
            current_line = word;
            current_chars = word_chars;
        } else if (current_chars + 1 + word_chars <= max_chars) {
            current_line += " " + word;
            current_chars += 1 + word_chars;
        } else {
            lines.push_back(current_line);
            current_line = word;
            current_chars = word_chars;
        }
    }
    if (!current_line.empty()) {
        lines.push_back(current_line);
    }

    return join_lines(lines, 0, lines.size());
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

    while (i < text.size()) {
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
    if (!current_line.empty()) {
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

    if (auto per_track = segment_per_track(segments, options, &SubtitleExporter::segment_japanese)) {
        return std::move(*per_track);
    }

    std::vector<SubtitleEntry> entries;

    // Collect all words from all segments with timing
    std::vector<Word> all_words = collect_words(segments);

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

    for (auto& entry : entries) entry.track_id = single_track_id(segments);
    return finalize_entries(std::move(entries), options);
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

    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
            char_count++;  // one per code point, not per byte
            in_word = true;
        } else if (in_word) {
            char_count++;  // Count one space between words
            in_word = false;
        }
        i += utf8_sequence_length(c);
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

    if (auto per_track = segment_per_track(segments, options, &SubtitleExporter::build_karaoke_captions)) {
        return std::move(*per_track);
    }

    std::vector<SubtitleEntry> entries;

    // Collect all words from all segments
    std::vector<Word> all_words = collect_words(segments);

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
            if (count_code_points(current_text) > static_cast<size_t>(options.max_chars_per_line)) {
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
        if (count_code_points(current_text) >= static_cast<size_t>(options.max_chars_per_line * options.max_lines)) {
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

    for (auto& entry : entries) entry.track_id = single_track_id(segments);
    return finalize_entries(std::move(entries), options);
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

    for (size_t i = 0; i < chars.size(); ++i) {
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
    if (!current_line.empty()) {
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

    if (auto per_track = segment_per_track(segments, options, &SubtitleExporter::segment_korean)) {
        return std::move(*per_track);
    }

    std::vector<SubtitleEntry> entries;

    // Collect all words from all segments with timing
    std::vector<Word> all_words = collect_words(segments);

    if (all_words.empty()) {
        // No word-level timestamps - fall back to segment-based
        return segments_to_entries(segments, options);
    }

    // If syllable karaoke is enabled, split words into syllables. Remember
    // which pieces start an original word: only there does the text get a
    // space, so "안녕하세요" stays one word instead of "안 녕 하 세 요".
    std::vector<Word> processed_words;
    std::vector<bool> starts_word;
    if (options.korean_syllable_karaoke) {
        for (const auto& word : all_words) {
            auto syllables = split_hangul_syllables(word, 60.0f);
            for (size_t k = 0; k < syllables.size(); ++k) {
                processed_words.push_back(syllables[k]);
                starts_word.push_back(k == 0);
            }
        }
    } else {
        processed_words = all_words;
        starts_word.assign(processed_words.size(), true);
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

        // Add word to current subtitle. Korean puts spaces between words,
        // never inside one, and none before punctuation.
        const size_t text_begin = word.word.find_first_not_of(' ');
        const std::string word_text =
            (text_begin == std::string::npos) ? std::string() : word.word.substr(text_begin);
        bool need_space = !current_text.empty() && !word_text.empty() && starts_word[i];
        if (need_space) {
            char first_char = word_text[0];
            if (first_char != '.' && first_char != ',' && first_char != '!' &&
                first_char != '?' && first_char != ':' && first_char != ';') {
                current_text += " ";
            }
        }
        current_text += word_text;
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

    for (auto& entry : entries) entry.track_id = single_track_id(segments);
    return finalize_entries(std::move(entries), options);
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

    for (size_t i = 0; i < chars.size(); ++i) {
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
    if (!current_line.empty()) {
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

    if (auto per_track = segment_per_track(segments, options, &SubtitleExporter::segment_chinese)) {
        return std::move(*per_track);
    }

    std::vector<SubtitleEntry> entries;

    // Collect all words from all segments with timing
    std::vector<Word> all_words = collect_words(segments);

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

    for (auto& entry : entries) entry.track_id = single_track_id(segments);
    return finalize_entries(std::move(entries), options);
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
    // No early return on count_russian_chars(text): it leaves out spaces, while
    // the loop below counts one per word gap, as a reader sees the line.

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

    for (size_t i = 0; i < words.size(); ++i) {
        const std::string& word = words[i];
        size_t word_len = count_russian_chars(word);

        // Check if adding this word would exceed the limit
        size_t new_count = current_char_count + word_len;
        if (!current_line.empty()) {
            new_count += 1;  // Account for space
        }

        bool should_break = (new_count > static_cast<size_t>(max_chars) && !current_line.empty());
        std::string carried;  // a preposition moved down to the new line

        // Check line-break rules
        if (use_line_break_rules && should_break) {
            // Don't start line with closing punctuation
            if (!word.empty()) {
                std::string first_char = word.substr(0, utf8_char_length(static_cast<unsigned char>(word[0])));
                if (is_russian_no_start(first_char)) {
                    // Don't break - keep punctuation with previous line
                    should_break = false;
                }
            }

            // Don't leave single-letter prepositions at line end
            if (should_break) {
                size_t last_space = current_line.rfind(' ');
                std::string last_word = (last_space != std::string::npos)
                    ? current_line.substr(last_space + 1)
                    : current_line;

                if (is_russian_single_letter_preposition(last_word)) {
                    if (last_space != std::string::npos) {
                        // Move the preposition to the next line with this word
                        current_line = current_line.substr(0, last_space);
                        carried = last_word;
                    } else {
                        // Line is just the preposition - don't break
                        should_break = false;
                    }
                }
            }
        }

        if (should_break) {
            lines.push_back(current_line);
            current_line = carried.empty() ? word : carried + " " + word;
            current_char_count = count_russian_chars(current_line) + (carried.empty() ? 0 : 1);
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
    if (!current_line.empty()) {
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

    if (auto per_track = segment_per_track(segments, options, &SubtitleExporter::segment_russian)) {
        return std::move(*per_track);
    }

    std::vector<SubtitleEntry> entries;

    // Collect all words from all segments with timing
    std::vector<Word> all_words = collect_words(segments);

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
            entry.text = split_russian_text(current_text,
                                            options.russian_max_chars,
                                            options.max_lines,
                                            options.russian_line_break_rules);

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

    for (auto& entry : entries) entry.track_id = single_track_id(segments);
    return finalize_entries(std::move(entries), options);
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
    // No early return on count_spanish_chars(text): it leaves out spaces, while
    // the loop below counts one per word gap, as a reader sees the line.

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

    for (size_t i = 0; i < words.size(); ++i) {
        const std::string& word = words[i];
        size_t word_len = count_spanish_chars(word);

        size_t new_count = current_char_count + word_len;
        if (!current_line.empty()) {
            new_count += 1;  // Space
        }

        bool should_break = (new_count > static_cast<size_t>(max_chars) && !current_line.empty());
        std::string carried;  // a short word moved down to the new line

        // Check line-break rules
        if (use_line_break_rules && should_break && !current_line.empty()) {
            // Get last word on current line
            size_t last_space = current_line.rfind(' ');
            std::string last_word = (last_space != std::string::npos)
                ? current_line.substr(last_space + 1)
                : current_line;

            if (is_spanish_short_word(last_word)) {
                if (last_space != std::string::npos) {
                    // Move the short word to the next line with this word
                    current_line = current_line.substr(0, last_space);
                    carried = last_word;
                } else {
                    should_break = false;
                }
            }
        }

        if (should_break) {
            lines.push_back(current_line);
            current_line = carried.empty() ? word : carried + " " + word;
            current_char_count = count_spanish_chars(current_line) + (carried.empty() ? 0 : 1);
        } else {
            if (!current_line.empty()) {
                current_line += " ";
                current_char_count += 1;
            }
            current_line += word;
            current_char_count += word_len;
        }
    }

    if (!current_line.empty()) {
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

    if (auto per_track = segment_per_track(segments, options, &SubtitleExporter::segment_spanish)) {
        return std::move(*per_track);
    }

    std::vector<SubtitleEntry> entries;

    // Collect all words
    std::vector<Word> all_words = collect_words(segments);

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

            entry.text = split_spanish_text(current_text,
                                            options.spanish_max_chars,
                                            options.max_lines,
                                            options.spanish_line_break_rules);

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
    for (auto& entry : entries) entry.track_id = single_track_id(segments);
    return finalize_entries(std::move(entries), options);
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
    // No early return on count_arabic_chars(text): it leaves out spaces, while
    // the loop below counts one per word gap, as a reader sees the line.

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

    for (size_t i = 0; i < words.size(); ++i) {
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

    if (!current_line.empty()) {
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

    if (auto per_track = segment_per_track(segments, options, &SubtitleExporter::segment_arabic)) {
        return std::move(*per_track);
    }

    std::vector<SubtitleEntry> entries;

    std::vector<Word> all_words = collect_words(segments);

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

            entry.text = split_arabic_text(current_text,
                                           options.arabic_max_chars,
                                           options.max_lines,
                                           options.arabic_ligature_safe);

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
    for (auto& entry : entries) entry.track_id = single_track_id(segments);
    return finalize_entries(std::move(entries), options);
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
    // No early return on count_hindi_chars(text): it leaves out spaces, while
    // the loop below counts one per word gap, as a reader sees the line.

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

    for (size_t i = 0; i < words.size(); ++i) {
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

    if (!current_line.empty()) {
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

    if (auto per_track = segment_per_track(segments, options, &SubtitleExporter::segment_hindi)) {
        return std::move(*per_track);
    }

    std::vector<SubtitleEntry> entries;

    std::vector<Word> all_words = collect_words(segments);

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

            entry.text = split_hindi_text(current_text,
                                          options.hindi_max_chars,
                                          options.max_lines);

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
    for (auto& entry : entries) entry.track_id = single_track_id(segments);
    return finalize_entries(std::move(entries), options);
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

    if (auto per_track = segment_per_track(segments, options, &SubtitleExporter::segment_indonesian)) {
        return std::move(*per_track);
    }

    std::vector<SubtitleEntry> entries;

    std::vector<Word> all_words = collect_words(segments);

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

            entry.text = split_text(current_text, options.indonesian_max_chars, options.max_lines);

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
    for (auto& entry : entries) entry.track_id = single_track_id(segments);
    return finalize_entries(std::move(entries), options);
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

    if (auto per_track = segment_per_track(segments, options, &SubtitleExporter::segment_vietnamese)) {
        return std::move(*per_track);
    }

    std::vector<SubtitleEntry> entries;

    std::vector<Word> all_words = collect_words(segments);

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

            entry.text = split_text(current_text, options.vietnamese_max_chars, options.max_lines);

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
    for (auto& entry : entries) entry.track_id = single_track_id(segments);
    return finalize_entries(std::move(entries), options);
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

    for (size_t i = 0; i < chars.size(); ++i) {
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

    if (!current_line.empty()) {
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

    if (auto per_track = segment_per_track(segments, options, &SubtitleExporter::segment_thai)) {
        return std::move(*per_track);
    }

    std::vector<SubtitleEntry> entries;

    std::vector<Word> all_words = collect_words(segments);

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
    for (auto& entry : entries) entry.track_id = single_track_id(segments);
    return finalize_entries(std::move(entries), options);
}

// ═══════════════════════════════════════════════════════════
// Metadata JSON (Loki Studio format)
// ═══════════════════════════════════════════════════════════

namespace SubtitleMetadata {

namespace {

// Helper: Escape JSON string
std::string escape_json_string(const std::string& str) {
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

// A number with a fixed count of decimals; NaN and infinity become 0 so the
// file stays valid JSON.
std::string json_number(double value, int decimals) {
    if (!std::isfinite(value)) value = 0.0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    return buf;
}

// ── A small JSON reader, enough for the files generate_metadata_json writes
// (and hand-edited ones). Throws std::runtime_error on malformed input.

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;  // keeps file order

    const JsonValue* find(const std::string& key) const {
        for (const auto& [k, v] : object) {
            if (k == key) return &v;
        }
        return nullptr;
    }
    double number_or(const std::string& key, double fallback) const {
        const JsonValue* v = find(key);
        return (v && v->type == Type::Number) ? v->number : fallback;
    }
    std::string string_or(const std::string& key, const std::string& fallback) const {
        const JsonValue* v = find(key);
        return (v && v->type == Type::String) ? v->string : fallback;
    }
};

class JsonReader {
public:
    explicit JsonReader(const std::string& text) : text_(text) {}

    JsonValue parse_document() {
        JsonValue value = parse_value(0);
        skip_space();
        if (pos_ != text_.size()) fail("unexpected text after the end");
        return value;
    }

private:
    const std::string& text_;
    size_t pos_ = 0;

    [[noreturn]] void fail(const std::string& what) const {
        throw std::runtime_error("Invalid metadata JSON at byte " + std::to_string(pos_) + ": " + what);
    }

    void skip_space() {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool consume(char c) {
        skip_space();
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    void expect(char c) {
        if (!consume(c)) fail(std::string("expected '") + c + "'");
    }

    bool consume_word(const char* word) {
        const size_t len = std::char_traits<char>::length(word);
        if (text_.compare(pos_, len, word) == 0) {
            pos_ += len;
            return true;
        }
        return false;
    }

    JsonValue parse_value(int depth) {
        if (depth > 64) fail("nested too deeply");
        skip_space();
        if (pos_ >= text_.size()) fail("unexpected end");

        JsonValue value;
        const char c = text_[pos_];
        if (c == '{') {
            ++pos_;
            value.type = JsonValue::Type::Object;
            if (consume('}')) return value;
            do {
                skip_space();
                if (pos_ >= text_.size() || text_[pos_] != '"') fail("expected a key");
                std::string key = parse_string();
                expect(':');
                value.object.emplace_back(std::move(key), parse_value(depth + 1));
            } while (consume(','));
            expect('}');
        } else if (c == '[') {
            ++pos_;
            value.type = JsonValue::Type::Array;
            if (consume(']')) return value;
            do {
                value.array.push_back(parse_value(depth + 1));
            } while (consume(','));
            expect(']');
        } else if (c == '"') {
            value.type = JsonValue::Type::String;
            value.string = parse_string();
        } else if (consume_word("true")) {
            value.type = JsonValue::Type::Bool;
            value.boolean = true;
        } else if (consume_word("false")) {
            value.type = JsonValue::Type::Bool;
        } else if (consume_word("null")) {
            value.type = JsonValue::Type::Null;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            value.type = JsonValue::Type::Number;
            const char* begin = text_.c_str() + pos_;
            char* end = nullptr;
            value.number = std::strtod(begin, &end);
            if (end == begin) fail("bad number");
            pos_ += static_cast<size_t>(end - begin);
        } else {
            fail("unexpected character");
        }
        return value;
    }

    static void append_utf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    uint32_t parse_hex4() {
        if (pos_ + 4 > text_.size()) fail("short \\u escape");
        uint32_t cp = 0;
        for (int i = 0; i < 4; ++i) {
            const char h = text_[pos_++];
            cp <<= 4;
            if (h >= '0' && h <= '9') cp |= static_cast<uint32_t>(h - '0');
            else if (h >= 'a' && h <= 'f') cp |= static_cast<uint32_t>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') cp |= static_cast<uint32_t>(h - 'A' + 10);
            else fail("bad \\u escape");
        }
        return cp;
    }

    std::string parse_string() {
        ++pos_;  // opening quote
        std::string out;
        while (true) {
            if (pos_ >= text_.size()) fail("unterminated string");
            const char c = text_[pos_++];
            if (c == '"') break;
            if (c != '\\') {
                out += c;  // UTF-8 bytes pass straight through
                continue;
            }
            if (pos_ >= text_.size()) fail("unterminated escape");
            const char e = text_[pos_++];
            switch (e) {
                case '"':  out += '"'; break;
                case '\\': out += '\\'; break;
                case '/':  out += '/'; break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    uint32_t cp = parse_hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && text_.compare(pos_, 2, "\\u") == 0) {
                        pos_ += 2;
                        const uint32_t low = parse_hex4();
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            fail("bad surrogate pair");
                        }
                    }
                    append_utf8(out, cp);
                    break;
                }
                default:
                    fail("unknown escape");
            }
        }
        return out;
    }
};

// "channel_3" -> 3, anything else -> 0.
int channel_number(const std::string& key) {
    const std::string prefix = "channel_";
    if (key.compare(0, prefix.size(), prefix) != 0) return 0;
    try {
        return std::stoi(key.substr(prefix.size()));
    } catch (...) {
        return 0;
    }
}

}  // namespace

std::string generate_metadata_json(const std::vector<Segment>& segments,
                                   const std::string& video_path,
                                   const std::string& whisper_model,
                                   const std::string& language,
                                   float duration,
                                   const std::string& device) {
    // Generate output path
    const std::filesystem::path video = utf8_path(video_path);
    std::filesystem::path metadata_path = video.parent_path();
    metadata_path /= video.stem();
    metadata_path += "_metadata.json";

    // One channel per audio track, in track order: channel_1 is track 0.
    std::map<int, std::vector<const Segment*>> by_track;
    for (const auto& seg : segments) by_track[seg.track_id].push_back(&seg);
    if (by_track.empty()) by_track[0];

    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::system_clock::to_time_t(now);

    // Build JSON manually. Times have millisecond precision (3 decimals).
    std::ostringstream json;

    json << "{\n";
    json << "  \"video_file\": \"" << escape_json_string(path_to_utf8(video.filename())) << "\",\n";
    json << "  \"processed_date\": " << static_cast<long long>(timestamp) << ",\n";
    json << "  \"whisper_model\": \"" << escape_json_string(whisper_model) << "\",\n";
    json << "  \"engine\": \"muninn-faster-whisper\",\n";
    json << "  \"device\": \"" << escape_json_string(device.empty() ? "unknown" : device) << "\",\n";
    json << "  \"transcriptions\": {\n";

    size_t channel_index = 0;
    for (const auto& [track, track_segments] : by_track) {
        std::string full_text;
        for (const Segment* seg : track_segments) {
            if (!full_text.empty()) full_text += " ";
            full_text += seg->text;
        }

        json << "    \"channel_" << (track + 1) << "\": {\n";
        json << "      \"track_id\": " << track << ",\n";
        json << "      \"text\": \"" << escape_json_string(full_text) << "\",\n";
        json << "      \"segments\": [\n";

        for (size_t i = 0; i < track_segments.size(); ++i) {
            const Segment& seg = *track_segments[i];
            json << "        {\n";
            json << "          \"start\": " << json_number(seg.start, 3) << ",\n";
            json << "          \"end\": " << json_number(seg.end, 3) << ",\n";
            json << "          \"text\": \"" << escape_json_string(seg.text) << "\"";

            if (!seg.words.empty()) {
                json << ",\n          \"words\": [\n";
                for (size_t j = 0; j < seg.words.size(); ++j) {
                    const auto& word = seg.words[j];
                    json << "            {\n";
                    json << "              \"word\": \"" << escape_json_string(word.word) << "\",\n";
                    json << "              \"start\": " << json_number(word.start, 3) << ",\n";
                    json << "              \"end\": " << json_number(word.end, 3) << ",\n";
                    json << "              \"probability\": " << json_number(word.probability, 4) << "\n";
                    json << "            }" << (j + 1 < seg.words.size() ? "," : "") << "\n";
                }
                json << "          ]";
            }

            if (seg.speaker_id >= 0) {
                json << ",\n          \"speaker_id\": " << seg.speaker_id << ",\n";
                json << "          \"speaker_label\": \"" << escape_json_string(seg.speaker_label) << "\"";
            }

            json << "\n        }" << (i + 1 < track_segments.size() ? "," : "") << "\n";
        }

        json << "      ],\n";
        json << "      \"language\": \"" << escape_json_string(language) << "\",\n";
        json << "      \"duration\": " << json_number(duration, 3) << "\n";
        json << "    }" << (++channel_index < by_track.size() ? "," : "") << "\n";
    }

    json << "  }\n";
    json << "}\n";

    // Write to file
    std::ofstream file(metadata_path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to create metadata JSON: " + path_to_utf8(metadata_path));
    }

    file << json.str();
    file.close();

    return path_to_utf8(metadata_path);
}

std::vector<Segment> load_from_metadata_json(const std::string& metadata_json_path) {
    std::ifstream file(utf8_path(metadata_json_path), std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open metadata JSON: " + metadata_json_path);
    }
    std::ostringstream content;
    content << file.rdbuf();
    std::string text = content.str();
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);  // UTF-8 byte order mark
    }

    const JsonValue root = JsonReader(text).parse_document();
    if (root.type != JsonValue::Type::Object) {
        throw std::runtime_error("Invalid metadata JSON: the top level is not an object");
    }
    const JsonValue* transcriptions = root.find("transcriptions");
    if (!transcriptions || transcriptions->type != JsonValue::Type::Object) {
        throw std::runtime_error("Invalid metadata JSON: no \"transcriptions\" object");
    }

    // Channels in track order.
    std::vector<std::pair<int, const JsonValue*>> channels;
    for (const auto& [key, channel] : transcriptions->object) {
        if (channel.type != JsonValue::Type::Object) continue;
        const int track = static_cast<int>(channel.number_or("track_id", channel_number(key) - 1));
        channels.emplace_back(std::max(0, track), &channel);
    }
    std::stable_sort(channels.begin(), channels.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<Segment> segments;
    for (const auto& [track, channel] : channels) {
        const std::string language = channel->string_or("language", "");
        const JsonValue* list = channel->find("segments");
        if (!list || list->type != JsonValue::Type::Array) continue;

        for (const JsonValue& item : list->array) {
            if (item.type != JsonValue::Type::Object) continue;
            Segment seg;
            seg.id = static_cast<int>(segments.size());
            seg.track_id = track;
            seg.start = static_cast<float>(item.number_or("start", 0.0));
            seg.end = static_cast<float>(item.number_or("end", seg.start));
            seg.text = item.string_or("text", "");
            seg.language = language;
            seg.speaker_id = static_cast<int>(item.number_or("speaker_id", -1));
            seg.speaker_label = item.string_or("speaker_label", "");

            const JsonValue* words = item.find("words");
            if (words && words->type == JsonValue::Type::Array) {
                for (const JsonValue& w : words->array) {
                    if (w.type != JsonValue::Type::Object) continue;
                    Word word;
                    word.word = w.string_or("word", "");
                    word.start = static_cast<float>(w.number_or("start", 0.0));
                    word.end = static_cast<float>(w.number_or("end", word.start));
                    word.probability = static_cast<float>(w.number_or("probability", 1.0));
                    seg.words.push_back(std::move(word));
                }
            }
            segments.push_back(std::move(seg));
        }
    }
    return segments;
}

} // namespace SubtitleMetadata

} // namespace muninn
