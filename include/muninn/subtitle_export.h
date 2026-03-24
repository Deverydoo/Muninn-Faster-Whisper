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
    // Language
    // ═══════════════════════════════════════════════════════════
    std::string language = "";             // Language code (empty = use segment language)
    bool enable_japanese_mode = false;     // Enable Japanese-specific segmentation

    // ═══════════════════════════════════════════════════════════
    // Text Formatting
    // ═══════════════════════════════════════════════════════════
    int max_chars_per_line = 42;           // Max characters per line (Japanese: 18-26)
    int max_lines = 2;                     // Max lines per subtitle
    bool auto_split_long_text = true;      // Auto-split at punctuation/spaces

    // ═══════════════════════════════════════════════════════════
    // Japanese-Specific Segmentation (when enable_japanese_mode = true)
    // Based on professional subtitle guidelines for Japanese media
    // ═══════════════════════════════════════════════════════════
    int japanese_max_chars = 22;           // Max Japanese chars per subtitle (18-26 typical)
    float japanese_pause_threshold_ms = 300.0f;  // Break on pauses > 300ms between words
    float japanese_max_duration = 5.0f;    // Max subtitle duration (4-6 sec typical)
    bool japanese_break_on_sentence = true;      // Break on sentence-enders: 。！？…
    bool japanese_break_on_comma = false;        // Soft break on: 、

    // ═══════════════════════════════════════════════════════════
    // Korean-Specific Segmentation (when enable_korean_mode = true)
    // Based on Netflix/professional Korean subtitle guidelines
    // ═══════════════════════════════════════════════════════════
    bool enable_korean_mode = false;       // Enable Korean-specific segmentation
    int korean_max_chars = 23;             // Max Korean chars per line (23 standard, 16 Netflix Originals)
    float korean_pause_threshold_ms = 320.0f;  // Break on pauses > 320ms between words
    float korean_max_duration = 6.0f;      // Max subtitle duration (6-7 sec)
    float korean_min_duration = 1.0f;      // Min subtitle duration (avoid flash)
    bool korean_break_on_sentence = true;  // Break on sentence-enders: . ! ? ...
    bool korean_geumchik_rules = true;     // Enforce geumchik line-break rules (punctuation restrictions)
    bool korean_syllable_karaoke = false;  // Enable per-syllable (Hangul block) karaoke timing

    // ═══════════════════════════════════════════════════════════
    // Chinese-Specific Segmentation (when enable_chinese_mode = true)
    // Based on Netflix/professional Chinese subtitle guidelines
    // Supports both Simplified (zh-CN) and Traditional (zh-TW) Chinese
    // ═══════════════════════════════════════════════════════════
    bool enable_chinese_mode = false;      // Enable Chinese-specific segmentation
    bool chinese_traditional = false;      // True = Traditional (16 chars), False = Simplified (18 chars)
    int chinese_max_chars_simplified = 18; // Max Simplified Chinese chars per line (Netflix baseline)
    int chinese_max_chars_traditional = 16; // Max Traditional Chinese chars per line (Netflix baseline)
    float chinese_pause_threshold_ms = 320.0f;  // Break on pauses > 320ms between words
    float chinese_max_duration = 6.0f;     // Max subtitle duration (6-7 sec)
    float chinese_min_duration = 1.0f;     // Min subtitle duration (avoid flash)
    float chinese_max_cps = 11.0f;         // Max characters per second for adults (9 for children)
    bool chinese_break_on_sentence = true; // Break on sentence-enders: 。！？…
    bool chinese_punctuation_rules = true; // Enforce Chinese line-break punctuation rules
    bool chinese_character_karaoke = false; // Enable per-character karaoke timing

    // ═══════════════════════════════════════════════════════════
    // Russian-Specific Segmentation (when enable_russian_mode = true)
    // Based on AAA Russian captioning guidelines for YouTube/Film/TV/Games
    // ═══════════════════════════════════════════════════════════
    bool enable_russian_mode = false;      // Enable Russian-specific segmentation
    int russian_max_chars = 40;            // Max Russian chars per line (36-42 typical)
    float russian_pause_threshold_ms = 320.0f;  // Break on pauses > 320ms between words
    float russian_max_duration = 7.0f;     // Max subtitle duration (6-7 sec)
    float russian_min_duration = 1.0f;     // Min subtitle duration (avoid flash)
    float russian_max_cps = 17.0f;         // Max characters per second (14-17 CPS)
    bool russian_break_on_sentence = true; // Break on sentence-enders: . ! ? — …
    bool russian_line_break_rules = true;  // Enforce Russian line-break rules (preposition handling)
    bool russian_word_karaoke = false;     // Enable per-word karaoke timing

    // ═══════════════════════════════════════════════════════════
    // Spanish/Portuguese-Specific Segmentation (Tier 1 - Latin America)
    // Based on Netflix LATAM and Brazilian Portuguese subtitle guidelines
    // ═══════════════════════════════════════════════════════════
    bool enable_spanish_mode = false;      // Enable Spanish/Portuguese-specific segmentation
    int spanish_max_chars = 42;            // Max chars per line (42 standard)
    float spanish_pause_threshold_ms = 300.0f;  // Break on pauses > 300ms
    float spanish_max_duration = 7.0f;     // Max subtitle duration (7 sec)
    float spanish_min_duration = 1.0f;     // Min subtitle duration
    float spanish_max_cps = 17.0f;         // Max characters per second (15-17 CPS)
    bool spanish_break_on_sentence = true; // Break on sentence-enders: . ! ? ¿ ¡ ...
    bool spanish_line_break_rules = true;  // Avoid short prepositions at line ends
    bool spanish_word_karaoke = false;     // Enable per-word karaoke timing

    // ═══════════════════════════════════════════════════════════
    // Arabic-Specific Segmentation (Tier 1 - Middle East/North Africa)
    // Based on professional Arabic subtitle guidelines with RTL support
    // ═══════════════════════════════════════════════════════════
    bool enable_arabic_mode = false;       // Enable Arabic-specific segmentation
    int arabic_max_chars = 42;             // Max chars per line (42 standard)
    float arabic_pause_threshold_ms = 350.0f;  // Break on pauses > 350ms (slower speech)
    float arabic_max_duration = 7.0f;      // Max subtitle duration (7 sec)
    float arabic_min_duration = 1.2f;      // Min subtitle duration (longer for RTL reading)
    float arabic_max_cps = 15.0f;          // Max characters per second (12-15 CPS)
    bool arabic_break_on_sentence = true;  // Break on sentence-enders: . ! ? ؟ ،
    bool arabic_ligature_safe = true;      // Avoid breaking Arabic ligatures
    bool arabic_word_karaoke = false;      // Enable per-word karaoke timing

    // ═══════════════════════════════════════════════════════════
    // Hindi/Devanagari-Specific Segmentation (Tier 1 - India)
    // Based on professional Hindi subtitle guidelines
    // ═══════════════════════════════════════════════════════════
    bool enable_hindi_mode = false;        // Enable Hindi-specific segmentation
    int hindi_max_chars = 42;              // Max chars per line (42 standard)
    float hindi_pause_threshold_ms = 320.0f;  // Break on pauses > 320ms
    float hindi_max_duration = 7.0f;       // Max subtitle duration (7 sec)
    float hindi_min_duration = 1.0f;       // Min subtitle duration
    float hindi_max_cps = 16.0f;           // Max characters per second (14-16 CPS)
    bool hindi_break_on_sentence = true;   // Break on sentence-enders: । ! ? ...
    bool hindi_phrase_segmentation = true; // Phrase-level segmentation
    bool hindi_word_karaoke = false;       // Enable per-word karaoke timing

    // ═══════════════════════════════════════════════════════════
    // Indonesian-Specific Segmentation (Tier 3 - Southeast Asia Growth)
    // Latin script, similar to English but with local preferences
    // ═══════════════════════════════════════════════════════════
    bool enable_indonesian_mode = false;   // Enable Indonesian-specific segmentation
    int indonesian_max_chars = 42;         // Max chars per line (42 standard)
    float indonesian_pause_threshold_ms = 300.0f;  // Break on pauses > 300ms
    float indonesian_max_duration = 7.0f;  // Max subtitle duration
    float indonesian_min_duration = 1.0f;  // Min subtitle duration
    float indonesian_max_cps = 17.0f;      // Max characters per second (15-17 CPS)
    bool indonesian_break_on_sentence = true;  // Break on sentence-enders
    bool indonesian_word_karaoke = false;  // Enable per-word karaoke timing

    // ═══════════════════════════════════════════════════════════
    // Vietnamese-Specific Segmentation (Tier 3 - Southeast Asia Growth)
    // Latin script with diacritics, tonal language
    // ═══════════════════════════════════════════════════════════
    bool enable_vietnamese_mode = false;   // Enable Vietnamese-specific segmentation
    int vietnamese_max_chars = 42;         // Max chars per line (42 standard)
    float vietnamese_pause_threshold_ms = 300.0f;  // Break on pauses > 300ms
    float vietnamese_max_duration = 7.0f;  // Max subtitle duration
    float vietnamese_min_duration = 1.0f;  // Min subtitle duration
    float vietnamese_max_cps = 17.0f;      // Max characters per second (15-17 CPS)
    bool vietnamese_break_on_sentence = true;  // Break on sentence-enders
    bool vietnamese_word_karaoke = false;  // Enable per-word karaoke timing

    // ═══════════════════════════════════════════════════════════
    // Thai-Specific Segmentation (Tier 3 - Southeast Asia Growth)
    // No spaces between words, complex script like Japanese
    // ═══════════════════════════════════════════════════════════
    bool enable_thai_mode = false;         // Enable Thai-specific segmentation
    int thai_max_chars = 40;               // Max Thai chars per line (40 standard)
    float thai_pause_threshold_ms = 320.0f;  // Break on pauses > 320ms
    float thai_max_duration = 6.0f;        // Max subtitle duration (6 sec)
    float thai_min_duration = 1.0f;        // Min subtitle duration
    float thai_max_cps = 14.0f;            // Max characters per second (12-14 CPS)
    bool thai_break_on_sentence = true;    // Break on Thai sentence-enders
    bool thai_word_karaoke = false;        // Enable per-word karaoke timing

    // ═══════════════════════════════════════════════════════════
    // AAA English Karaoke Captioning (when enable_karaoke_mode = true)
    // Based on AAA production captioning guidelines for film/TV/games
    // ═══════════════════════════════════════════════════════════
    bool enable_karaoke_mode = false;      // Enable AAA karaoke-style captioning

    // Timing Layer - Word timestamp correction
    float min_word_duration_ms = 80.0f;    // Clamp minimum word durations (60-120ms)
    float micro_gap_merge_ms = 80.0f;      // Merge micro-gaps under this threshold

    // Layout Layer - Phrase building
    float target_phrase_duration_min = 1.5f;  // Target phrase duration minimum (seconds)
    float target_phrase_duration_max = 3.5f;  // Target phrase duration maximum (seconds)
    float pause_break_threshold_ms = 320.0f;  // Break on pauses > this threshold (250-400ms)
    float max_cps = 17.0f;                    // Maximum characters per second (15-20, target ~17)

    // Presentation Layer - Caption appearance/disappearance
    float phrase_lead_in_ms = 220.0f;      // Show phrase before first word (150-300ms)
    float word_lead_ms = 60.0f;            // Per-word reveal lead time (0-120ms)
    float min_hold_after_speech_s = 1.4f;  // Minimum hold after last word (1-2s)
    float clear_after_silence_s = 4.5f;    // Clear captions after this silence (3-6s)
    float fade_out_ms = 250.0f;            // Fade out duration (200-350ms), never hard cut

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

    // ═══════════════════════════════════════════════════════════
    // Japanese-Specific Processing
    // ═══════════════════════════════════════════════════════════

    /**
     * @brief Segment Japanese text using word-level timestamps
     *
     * Builds subtitles from word-level timing using Japanese-specific rules:
     * - Break on sentence-ending punctuation: 。！？…
     * - Break on pause > threshold between words
     * - Break when character count exceeds limit
     * - Break when duration exceeds limit
     *
     * This is the approach recommended by professional Japanese subtitle guides:
     * "Good Japanese subtitles are built, not emitted."
     *
     * @param segments Original transcription segments (must have word timestamps)
     * @param options Subtitle export options with Japanese settings
     * @return Re-segmented subtitle entries with improved timing
     */
    static std::vector<SubtitleEntry> segment_japanese(
        const std::vector<Segment>& segments,
        const SubtitleExportOptions& options);

    /**
     * @brief Count Japanese characters (excluding punctuation and spaces)
     *
     * @param text UTF-8 text
     * @return Number of Japanese display characters
     */
    static size_t count_japanese_chars(const std::string& text);

    /**
     * @brief Check if text ends with Japanese sentence-ending punctuation
     *
     * Checks for: 。！？…
     *
     * @param text UTF-8 text
     * @return True if ends with sentence-ending punctuation
     */
    static bool ends_with_japanese_sentence_end(const std::string& text);

    /**
     * @brief Check if text ends with Japanese comma (soft break)
     *
     * Checks for: 、
     *
     * @param text UTF-8 text
     * @return True if ends with comma
     */
    static bool ends_with_japanese_comma(const std::string& text);

    /**
     * @brief Split Japanese text into lines for display
     *
     * Unlike English, Japanese doesn't split on spaces.
     * Splits at punctuation or at character limits.
     *
     * @param text Japanese text
     * @param max_chars Max characters per line
     * @param max_lines Max number of lines
     * @return Text with line breaks inserted
     */
    static std::string split_japanese_text(const std::string& text,
                                           int max_chars,
                                           int max_lines = 2);

    // ═══════════════════════════════════════════════════════════
    // AAA English Karaoke Processing
    // ═══════════════════════════════════════════════════════════

    /**
     * @brief Build AAA-quality karaoke captions from word-level timestamps
     *
     * Implements the three-layer AAA captioning system:
     * 1. Timing Layer: Corrects word timestamps (min duration, micro-gap merging)
     * 2. Layout Layer: Builds phrases with CPS control, punctuation breaks
     * 3. Presentation Layer: Applies lead-in, hold, and silence clearing
     *
     * "AAA-quality captions are not produced directly by ASR. They are constructed
     * through deliberate timing control, intelligent phrase building, and
     * presentation rules that respect human perception."
     *
     * @param segments Original transcription segments (must have word timestamps)
     * @param options Subtitle export options with AAA karaoke settings
     * @return Karaoke-ready subtitle entries with corrected timing
     */
    static std::vector<SubtitleEntry> build_karaoke_captions(
        const std::vector<Segment>& segments,
        const SubtitleExportOptions& options);

    /**
     * @brief Correct word timestamps for AAA quality
     *
     * - Clamps minimum word durations (60-120ms)
     * - Merges micro-gaps under threshold (80ms)
     * - Ensures strictly monotonic timestamps
     *
     * @param words Input words with raw ASR timestamps
     * @param options Options containing timing parameters
     * @return Words with corrected timestamps
     */
    static std::vector<Word> correct_word_timestamps(
        const std::vector<Word>& words,
        const SubtitleExportOptions& options);

    /**
     * @brief Calculate characters per second for a phrase
     *
     * @param text Phrase text
     * @param duration Phrase duration in seconds
     * @return CPS value
     */
    static float calculate_cps(const std::string& text, float duration);

    /**
     * @brief Check if text ends with English sentence-ending punctuation
     *
     * Checks for: . ! ? ...
     *
     * @param text UTF-8 text
     * @return True if ends with sentence-ending punctuation
     */
    static bool ends_with_english_sentence_end(const std::string& text);

    // ═══════════════════════════════════════════════════════════
    // Korean-Specific Processing
    // ═══════════════════════════════════════════════════════════

    /**
     * @brief Segment Korean text using word-level timestamps
     *
     * Builds subtitles from word-level timing using Korean-specific rules:
     * - Break on sentence-ending punctuation: . ! ? ...
     * - Break on pause > threshold between words
     * - Break when character count exceeds limit (23 chars/line)
     * - Break when duration exceeds limit (6s max)
     * - Apply geumchik rules for line-break restrictions
     *
     * @param segments Original transcription segments (must have word timestamps)
     * @param options Subtitle export options with Korean settings
     * @return Re-segmented subtitle entries with improved timing
     */
    static std::vector<SubtitleEntry> segment_korean(
        const std::vector<Segment>& segments,
        const SubtitleExportOptions& options);

    /**
     * @brief Check if a Unicode codepoint is a Hangul syllable block
     *
     * Hangul syllables are in the range U+AC00 to U+D7A3.
     *
     * @param codepoint Unicode codepoint
     * @return True if it's a Hangul syllable block
     */
    static bool is_hangul_syllable(uint32_t codepoint);

    /**
     * @brief Count Korean/CJK characters in text
     *
     * Counts Hangul syllables and other CJK characters for line-length calculations.
     *
     * @param text UTF-8 text
     * @return Number of Korean/CJK display characters
     */
    static size_t count_korean_chars(const std::string& text);

    /**
     * @brief Check if text ends with Korean sentence-ending punctuation
     *
     * Checks for: . ! ? 。 ！ ？
     *
     * @param text UTF-8 text
     * @return True if ends with sentence-ending punctuation
     */
    static bool ends_with_korean_sentence_end(const std::string& text);

    /**
     * @brief Check if a character violates geumchik rules at line start
     *
     * Certain punctuation marks should not appear at the start of a line
     * in Korean typography (similar to Japanese kinsoku).
     *
     * @param text UTF-8 character to check
     * @return True if character should not start a line
     */
    static bool is_geumchik_no_start(const std::string& ch);

    /**
     * @brief Check if a character violates geumchik rules at line end
     *
     * Certain punctuation marks should not appear at the end of a line.
     *
     * @param text UTF-8 character to check
     * @return True if character should not end a line
     */
    static bool is_geumchik_no_end(const std::string& ch);

    /**
     * @brief Split Korean text into lines for display
     *
     * Applies geumchik rules and character limits.
     * Korean can break on character (syllable block) or word basis.
     *
     * @param text Korean text
     * @param max_chars Max characters per line
     * @param max_lines Max number of lines
     * @param use_geumchik Apply geumchik line-break rules
     * @return Text with line breaks inserted
     */
    static std::string split_korean_text(const std::string& text,
                                         int max_chars,
                                         int max_lines = 2,
                                         bool use_geumchik = true);

    /**
     * @brief Split Korean word into Hangul syllable blocks with timing
     *
     * For karaoke-style highlighting, distributes word timing across
     * individual Hangul syllable blocks proportionally.
     *
     * @param word Word with timing
     * @param min_syllable_duration_ms Minimum duration per syllable to prevent flicker
     * @return Vector of syllable-level words with distributed timing
     */
    static std::vector<Word> split_hangul_syllables(
        const Word& word,
        float min_syllable_duration_ms = 60.0f);

    // ═══════════════════════════════════════════════════════════
    // Chinese-Specific Processing
    // ═══════════════════════════════════════════════════════════

    /**
     * @brief Segment Chinese text using word-level timestamps
     *
     * Builds subtitles from word-level timing using Chinese-specific rules:
     * - Break on sentence-ending punctuation: 。！？…
     * - Break on pause > threshold between words
     * - Break when character count exceeds limit (18 Simplified, 16 Traditional)
     * - Break when duration exceeds limit (6s max)
     * - Enforce CPS reading speed limits (9-11 CPS)
     * - Apply Chinese punctuation line-break rules
     *
     * @param segments Original transcription segments (must have word timestamps)
     * @param options Subtitle export options with Chinese settings
     * @return Re-segmented subtitle entries with improved timing
     */
    static std::vector<SubtitleEntry> segment_chinese(
        const std::vector<Segment>& segments,
        const SubtitleExportOptions& options);

    /**
     * @brief Check if a Unicode codepoint is a CJK ideograph
     *
     * CJK Unified Ideographs are in range U+4E00 to U+9FFF.
     * Also includes CJK Extension blocks and compatibility characters.
     *
     * @param codepoint Unicode codepoint
     * @return True if it's a CJK ideograph
     */
    static bool is_cjk_ideograph(uint32_t codepoint);

    /**
     * @brief Count Chinese/CJK characters in text
     *
     * Counts CJK ideographs for line-length and CPS calculations.
     *
     * @param text UTF-8 text
     * @return Number of Chinese/CJK display characters
     */
    static size_t count_chinese_chars(const std::string& text);

    /**
     * @brief Check if text ends with Chinese sentence-ending punctuation
     *
     * Checks for: 。！？…
     *
     * @param text UTF-8 text
     * @return True if ends with sentence-ending punctuation
     */
    static bool ends_with_chinese_sentence_end(const std::string& text);

    /**
     * @brief Check if a character violates Chinese line-break rules at line start
     *
     * Closing punctuation should not appear at the start of a line.
     *
     * @param ch UTF-8 character to check
     * @return True if character should not start a line
     */
    static bool is_chinese_no_start(const std::string& ch);

    /**
     * @brief Check if a character violates Chinese line-break rules at line end
     *
     * Opening punctuation should not appear at the end of a line.
     *
     * @param ch UTF-8 character to check
     * @return True if character should not end a line
     */
    static bool is_chinese_no_end(const std::string& ch);

    /**
     * @brief Split Chinese text into lines for display
     *
     * Applies Chinese punctuation rules and character limits.
     * Chinese text can break between any two ideographs.
     *
     * @param text Chinese text
     * @param max_chars Max characters per line
     * @param max_lines Max number of lines
     * @param use_punctuation_rules Apply Chinese line-break punctuation rules
     * @return Text with line breaks inserted
     */
    static std::string split_chinese_text(const std::string& text,
                                          int max_chars,
                                          int max_lines = 2,
                                          bool use_punctuation_rules = true);

    /**
     * @brief Split Chinese word into individual characters with timing
     *
     * For karaoke-style highlighting, distributes word timing across
     * individual CJK characters proportionally.
     *
     * @param word Word with timing
     * @param min_char_duration_ms Minimum duration per character to prevent flicker
     * @return Vector of character-level words with distributed timing
     */
    static std::vector<Word> split_chinese_characters(
        const Word& word,
        float min_char_duration_ms = 60.0f);

    /**
     * @brief Calculate Chinese CPS (characters per second)
     *
     * @param text Chinese text
     * @param duration Duration in seconds
     * @return CPS value
     */
    static float calculate_chinese_cps(const std::string& text, float duration);

    // ═══════════════════════════════════════════════════════════
    // Russian-Specific Processing
    // ═══════════════════════════════════════════════════════════

    /**
     * @brief Segment Russian text using word-level timestamps
     *
     * Builds subtitles from word-level timing using Russian-specific rules:
     * - Break on sentence-ending punctuation: . ! ? — …
     * - Break on pause > threshold between words (320ms)
     * - Break when character count exceeds limit (40 chars/line)
     * - Break when duration exceeds limit (7s max)
     * - Enforce CPS reading speed limits (14-17 CPS)
     * - Apply Russian line-break rules (preposition handling)
     *
     * @param segments Original transcription segments (must have word timestamps)
     * @param options Subtitle export options with Russian settings
     * @return Re-segmented subtitle entries with improved timing
     */
    static std::vector<SubtitleEntry> segment_russian(
        const std::vector<Segment>& segments,
        const SubtitleExportOptions& options);

    /**
     * @brief Check if a Unicode codepoint is a Cyrillic character
     *
     * Cyrillic characters are in range U+0400 to U+04FF.
     *
     * @param codepoint Unicode codepoint
     * @return True if it's a Cyrillic character
     */
    static bool is_cyrillic(uint32_t codepoint);

    /**
     * @brief Count Russian/Cyrillic characters in text
     *
     * Counts Cyrillic characters for line-length and CPS calculations.
     *
     * @param text UTF-8 text
     * @return Number of Russian/Cyrillic display characters
     */
    static size_t count_russian_chars(const std::string& text);

    /**
     * @brief Check if text ends with Russian sentence-ending punctuation
     *
     * Checks for: . ! ? — …
     *
     * @param text UTF-8 text
     * @return True if ends with sentence-ending punctuation
     */
    static bool ends_with_russian_sentence_end(const std::string& text);

    /**
     * @brief Check if a word is a Russian single-letter preposition
     *
     * Single-letter prepositions (в, к, с, о, у, а, и) should not be
     * left at the end of a line in Russian typography.
     *
     * @param word UTF-8 word to check
     * @return True if it's a single-letter preposition
     */
    static bool is_russian_single_letter_preposition(const std::string& word);

    /**
     * @brief Check if a character should not start a line in Russian
     *
     * Closing punctuation should not appear at the start of a line.
     *
     * @param ch UTF-8 character to check
     * @return True if character should not start a line
     */
    static bool is_russian_no_start(const std::string& ch);

    /**
     * @brief Split Russian text into lines for display
     *
     * Applies Russian typography rules:
     * - Don't leave single-letter prepositions at line end
     * - Don't start lines with closing punctuation
     * - Break at word boundaries (spaces)
     * - Balance line lengths visually
     *
     * @param text Russian text
     * @param max_chars Max characters per line
     * @param max_lines Max number of lines
     * @param use_line_break_rules Apply Russian line-break rules
     * @return Text with line breaks inserted
     */
    static std::string split_russian_text(const std::string& text,
                                          int max_chars,
                                          int max_lines = 2,
                                          bool use_line_break_rules = true);

    /**
     * @brief Calculate Russian CPS (characters per second)
     *
     * @param text Russian text
     * @param duration Duration in seconds
     * @return CPS value
     */
    static float calculate_russian_cps(const std::string& text, float duration);

    // ═══════════════════════════════════════════════════════════
    // Spanish/Portuguese-Specific Processing (Tier 1)
    // ═══════════════════════════════════════════════════════════

    /**
     * @brief Segment Spanish/Portuguese text using word-level timestamps
     *
     * Builds subtitles from word-level timing using Spanish/Portuguese rules:
     * - Break on sentence-ending punctuation: . ! ? ¿ ¡ ...
     * - Break on pause > threshold between words (300ms)
     * - Break when character count exceeds limit (42 chars/line)
     * - Enforce CPS reading speed limits (15-17 CPS)
     * - Apply Spanish line-break rules (avoid short prepositions at line ends)
     *
     * @param segments Original transcription segments (must have word timestamps)
     * @param options Subtitle export options with Spanish settings
     * @return Re-segmented subtitle entries with improved timing
     */
    static std::vector<SubtitleEntry> segment_spanish(
        const std::vector<Segment>& segments,
        const SubtitleExportOptions& options);

    /**
     * @brief Count Spanish/Latin characters in text
     */
    static size_t count_spanish_chars(const std::string& text);

    /**
     * @brief Check if text ends with Spanish sentence-ending punctuation
     */
    static bool ends_with_spanish_sentence_end(const std::string& text);

    /**
     * @brief Check if a word is a Spanish short preposition that shouldn't end a line
     *
     * Spanish/Portuguese short words: a, al, de, del, el, en, la, las, los, y, o, u
     */
    static bool is_spanish_short_word(const std::string& word);

    /**
     * @brief Split Spanish text into lines for display
     */
    static std::string split_spanish_text(const std::string& text,
                                          int max_chars,
                                          int max_lines = 2,
                                          bool use_line_break_rules = true);

    /**
     * @brief Calculate Spanish CPS (characters per second)
     */
    static float calculate_spanish_cps(const std::string& text, float duration);

    // ═══════════════════════════════════════════════════════════
    // Arabic-Specific Processing (Tier 1 - RTL)
    // ═══════════════════════════════════════════════════════════

    /**
     * @brief Segment Arabic text using word-level timestamps
     *
     * Builds subtitles from word-level timing using Arabic rules:
     * - RTL text direction (handled by display layer, not subtitle file)
     * - Break on sentence-ending punctuation: . ! ? ؟ ،
     * - Break on pause > threshold between words (350ms)
     * - Enforce CPS reading speed limits (12-15 CPS)
     * - Ligature-safe word boundaries (don't break Arabic words mid-ligature)
     *
     * @param segments Original transcription segments (must have word timestamps)
     * @param options Subtitle export options with Arabic settings
     * @return Re-segmented subtitle entries with improved timing
     */
    static std::vector<SubtitleEntry> segment_arabic(
        const std::vector<Segment>& segments,
        const SubtitleExportOptions& options);

    /**
     * @brief Check if a Unicode codepoint is an Arabic character
     */
    static bool is_arabic(uint32_t codepoint);

    /**
     * @brief Count Arabic characters in text
     */
    static size_t count_arabic_chars(const std::string& text);

    /**
     * @brief Check if text ends with Arabic sentence-ending punctuation
     */
    static bool ends_with_arabic_sentence_end(const std::string& text);

    /**
     * @brief Split Arabic text into lines for display
     */
    static std::string split_arabic_text(const std::string& text,
                                         int max_chars,
                                         int max_lines = 2,
                                         bool ligature_safe = true);

    /**
     * @brief Calculate Arabic CPS (characters per second)
     */
    static float calculate_arabic_cps(const std::string& text, float duration);

    // ═══════════════════════════════════════════════════════════
    // Hindi/Devanagari-Specific Processing (Tier 1)
    // ═══════════════════════════════════════════════════════════

    /**
     * @brief Segment Hindi text using word-level timestamps
     *
     * Builds subtitles from word-level timing using Hindi rules:
     * - Break on sentence-ending punctuation: । ! ? ...
     * - Break on pause > threshold between words (320ms)
     * - Enforce CPS reading speed limits (14-16 CPS)
     * - Phrase-level segmentation for better readability
     *
     * @param segments Original transcription segments (must have word timestamps)
     * @param options Subtitle export options with Hindi settings
     * @return Re-segmented subtitle entries with improved timing
     */
    static std::vector<SubtitleEntry> segment_hindi(
        const std::vector<Segment>& segments,
        const SubtitleExportOptions& options);

    /**
     * @brief Check if a Unicode codepoint is a Devanagari character
     */
    static bool is_devanagari(uint32_t codepoint);

    /**
     * @brief Count Hindi/Devanagari characters in text
     */
    static size_t count_hindi_chars(const std::string& text);

    /**
     * @brief Check if text ends with Hindi sentence-ending punctuation
     */
    static bool ends_with_hindi_sentence_end(const std::string& text);

    /**
     * @brief Split Hindi text into lines for display
     */
    static std::string split_hindi_text(const std::string& text,
                                        int max_chars,
                                        int max_lines = 2);

    /**
     * @brief Calculate Hindi CPS (characters per second)
     */
    static float calculate_hindi_cps(const std::string& text, float duration);

    // ═══════════════════════════════════════════════════════════
    // Indonesian-Specific Processing (Tier 3)
    // ═══════════════════════════════════════════════════════════

    /**
     * @brief Segment Indonesian text using word-level timestamps
     *
     * Indonesian uses Latin script with similar rules to English.
     * Handles Indonesian-specific sentence structure and word boundaries.
     *
     * @param segments Original transcription segments (must have word timestamps)
     * @param options Subtitle export options with Indonesian settings
     * @return Re-segmented subtitle entries with improved timing
     */
    static std::vector<SubtitleEntry> segment_indonesian(
        const std::vector<Segment>& segments,
        const SubtitleExportOptions& options);

    /**
     * @brief Count Indonesian characters in text
     */
    static size_t count_indonesian_chars(const std::string& text);

    /**
     * @brief Calculate Indonesian CPS
     */
    static float calculate_indonesian_cps(const std::string& text, float duration);

    // ═══════════════════════════════════════════════════════════
    // Vietnamese-Specific Processing (Tier 3)
    // ═══════════════════════════════════════════════════════════

    /**
     * @brief Segment Vietnamese text using word-level timestamps
     *
     * Vietnamese uses Latin script with extensive diacritics.
     * Each syllable is a separate word (monosyllabic).
     *
     * @param segments Original transcription segments (must have word timestamps)
     * @param options Subtitle export options with Vietnamese settings
     * @return Re-segmented subtitle entries with improved timing
     */
    static std::vector<SubtitleEntry> segment_vietnamese(
        const std::vector<Segment>& segments,
        const SubtitleExportOptions& options);

    /**
     * @brief Count Vietnamese characters in text
     */
    static size_t count_vietnamese_chars(const std::string& text);

    /**
     * @brief Check if text ends with Vietnamese sentence-ending punctuation
     */
    static bool ends_with_vietnamese_sentence_end(const std::string& text);

    /**
     * @brief Calculate Vietnamese CPS
     */
    static float calculate_vietnamese_cps(const std::string& text, float duration);

    // ═══════════════════════════════════════════════════════════
    // Thai-Specific Processing (Tier 3)
    // ═══════════════════════════════════════════════════════════

    /**
     * @brief Segment Thai text using word-level timestamps
     *
     * Thai script has no spaces between words (like Japanese/Chinese).
     * Breaks at word boundaries using Whisper's word-level timestamps.
     *
     * @param segments Original transcription segments (must have word timestamps)
     * @param options Subtitle export options with Thai settings
     * @return Re-segmented subtitle entries with improved timing
     */
    static std::vector<SubtitleEntry> segment_thai(
        const std::vector<Segment>& segments,
        const SubtitleExportOptions& options);

    /**
     * @brief Check if a Unicode codepoint is a Thai character
     */
    static bool is_thai(uint32_t codepoint);

    /**
     * @brief Count Thai characters in text
     */
    static size_t count_thai_chars(const std::string& text);

    /**
     * @brief Check if text ends with Thai sentence-ending punctuation
     */
    static bool ends_with_thai_sentence_end(const std::string& text);

    /**
     * @brief Split Thai text into lines for display
     */
    static std::string split_thai_text(const std::string& text,
                                       int max_chars,
                                       int max_lines = 2);

    /**
     * @brief Calculate Thai CPS
     */
    static float calculate_thai_cps(const std::string& text, float duration);
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
