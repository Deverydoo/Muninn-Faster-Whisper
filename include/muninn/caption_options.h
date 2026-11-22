#pragma once

#include "export.h"
#include "caption_types.h"
#include <optional>
#include <string>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// Caption Engine Options
// ═══════════════════════════════════════════════════════════════════════════
//
// Clean, engine-agnostic configuration replacing the monolithic
// SubtitleExportOptions. Language-specific settings are handled by
// engine presets, not scattered options.
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Presentation timing options (AAA Baseline)
 *
 * Controls karaoke-style caption appearance/disappearance timing.
 * Based on professional film/TV/game captioning guidelines.
 */
struct MUNINN_API PresentationOptions {
    // === Lead-in Timing ===
    float phraseLeadInMs = 220.0f;      // Show caption before first word (150-300ms)
    float tokenRevealMs = 60.0f;        // Per-token reveal lead time (0-120ms)

    // === Hold/Fade Timing ===
    // The hold and fade never run past the moment the next cue appears.
    float minHoldAfterSpeechS = 1.4f;   // Minimum hold after last word (1-2s)
    float fadeOutMs = 250.0f;           // Fade out duration (200-350ms)

    // === Pause Behavior ===
    float pauseBreakThresholdMs = 320.0f;  // Start a new cue on pauses > this (250-400ms)

    PresentationOptions() = default;

    // Preset: Fast-paced content (gaming, action)
    static PresentationOptions fastPaced() {
        PresentationOptions opts;
        opts.phraseLeadInMs = 150.0f;
        opts.minHoldAfterSpeechS = 1.0f;
        opts.pauseBreakThresholdMs = 250.0f;
        return opts;
    }

    // Preset: Slow, accessible content
    static PresentationOptions accessible() {
        PresentationOptions opts;
        opts.phraseLeadInMs = 300.0f;
        opts.minHoldAfterSpeechS = 2.0f;
        opts.pauseBreakThresholdMs = 400.0f;
        return opts;
    }
};

struct CaptionEngineOverrides;

/**
 * @brief Caption engine configuration
 *
 * Engine-agnostic options that apply to all languages.
 * Language-specific tuning is handled by engine presets.
 *
 * Line-break rules (kinsoku, geumchik, Russian prepositions) are part of
 * each language engine and always apply. Token granularity (word,
 * character, syllable) is also the engine's: see tokenGranularity().
 */
struct MUNINN_API CaptionEngineOptions {
    // === Layout Constraints ===
    int maxCharsPerLine = 42;           // Max characters per line
    int maxLines = 2;                   // Max lines per cue (text past this
                                        // starts a new cue; nothing is dropped)
    float maxDuration = 7.0f;           // Max cue duration (seconds)
    float minDuration = 1.0f;           // Min cue duration (seconds); never
                                        // runs into the next cue

    // === Reading Speed (CPS) ===
    float maxCps = 17.0f;               // Characters per second limit

    // === Break Behavior ===
    bool breakOnSentence = true;        // Break at sentence-ending punctuation
    bool breakOnPause = true;           // Break on pauses longer than
                                        // presentation.pauseBreakThresholdMs

    // === Typography Rules ===
    bool avoidOrphanWords = true;       // Don't leave one word alone on the last
                                        // line: pull one down from the line above
    bool balanceLines = false;          // Split two-line cues where the lines
                                        // come out closest in length

    // === Presentation ===
    PresentationOptions presentation;

    // === Language Hint (for routing) ===
    std::string languageHint;           // ISO 639-1 (e.g., "ja", "ko")
    std::string scriptHint;             // ISO 15924 (e.g., "Hans", "Hant")

    // === Output Format ===
    bool generatePresentationEvents = true;  // Generate karaoke events

    CaptionEngineOptions() = default;

    /**
     * @brief Apply user overrides on top of these options (usually a preset)
     *
     * Every field that is set in `overrides` replaces the value here, even
     * when it equals the built-in default. Unset fields leave the value alone.
     */
    void merge(const CaptionEngineOverrides& overrides);
};

/**
 * @brief Options a caller wants to change, on top of a language preset
 *
 * Each field is optional: set only what you want to change. A value that
 * happens to equal the default still counts (asking for 42 characters on a
 * Japanese preset of 22 gives 42).
 */
struct MUNINN_API CaptionEngineOverrides {
    std::optional<int> maxCharsPerLine;
    std::optional<int> maxLines;
    std::optional<float> maxDuration;
    std::optional<float> minDuration;
    std::optional<float> maxCps;
    std::optional<bool> breakOnSentence;
    std::optional<bool> breakOnPause;
    std::optional<bool> avoidOrphanWords;
    std::optional<bool> balanceLines;
    std::optional<bool> generatePresentationEvents;

    // Presentation timing
    std::optional<float> phraseLeadInMs;
    std::optional<float> tokenRevealMs;
    std::optional<float> minHoldAfterSpeechS;
    std::optional<float> fadeOutMs;
    std::optional<float> pauseBreakThresholdMs;

    // Routing: empty = detect from the segments
    std::string languageHint;
    std::string scriptHint;
};

// ═══════════════════════════════════════════════════════════════════════════
// Language Presets
// ═══════════════════════════════════════════════════════════════════════════
//
// Pre-configured options for each supported language, based on professional
// subtitle guidelines (Netflix, YouTube, broadcast standards).
// ═══════════════════════════════════════════════════════════════════════════

namespace CaptionPresets {

// ═══════════════════════════════════════════════════════════════════════════
// CJK Family - Character-based, strict punctuation rules
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Japanese caption preset
 *
 * - 18-26 characters per line (22 default)
 * - Character-level granularity
 * - Strict kinsoku shori (line-break rules)
 * - Break on: 。！？…
 */
MUNINN_API CaptionEngineOptions japanese();

/**
 * @brief Korean caption preset
 *
 * - 23 characters per line (16 for Netflix Originals)
 * - Syllable-level granularity (Hangul blocks)
 * - Geumchik rules (punctuation restrictions)
 * - Break on: . ! ? 。！？
 */
MUNINN_API CaptionEngineOptions korean();

/**
 * @brief Simplified Chinese caption preset
 *
 * - 18 characters per line
 * - Character-level granularity
 * - 9-11 CPS reading speed
 * - Break on: 。！？…
 */
MUNINN_API CaptionEngineOptions chineseSimplified();

/**
 * @brief Traditional Chinese caption preset
 *
 * - 16 characters per line
 * - Character-level granularity
 * - 9-11 CPS reading speed
 * - Break on: 。！？…
 */
MUNINN_API CaptionEngineOptions chineseTraditional();

// ═══════════════════════════════════════════════════════════════════════════
// Cyrillic Family - Word-based, preposition handling
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Russian caption preset
 *
 * - 36-42 characters per line (40 default)
 * - Word-level granularity
 * - 14-17 CPS reading speed
 * - Avoid dangling single-letter prepositions (в, к, с, о, у, а, и)
 * - Break on: . ! ? — …
 */
MUNINN_API CaptionEngineOptions russian();

// ═══════════════════════════════════════════════════════════════════════════
// RTL Family - Right-to-left, ligature-safe
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Arabic caption preset
 *
 * - 42 characters per line
 * - Word-level granularity (RTL word order)
 * - 12-15 CPS reading speed (slower for RTL)
 * - Ligature-safe word boundaries
 * - Break on: . ! ? ؟ ،
 */
MUNINN_API CaptionEngineOptions arabic();

// ═══════════════════════════════════════════════════════════════════════════
// Indic Family - Phrase-level segmentation
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Hindi caption preset (Devanagari)
 *
 * - 42 characters per line
 * - Word-level granularity
 * - 14-16 CPS reading speed
 * - Break on: । ! ? (Devanagari Danda)
 */
MUNINN_API CaptionEngineOptions hindi();

// ═══════════════════════════════════════════════════════════════════════════
// Latin Family - Word-based, higher CPS
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief English caption preset (AAA karaoke)
 *
 * - 42 characters per line
 * - Word-level granularity
 * - 17 CPS reading speed
 * - Professional karaoke timing
 */
MUNINN_API CaptionEngineOptions english();

/**
 * @brief Spanish caption preset
 *
 * - 42 characters per line
 * - Word-level granularity
 * - 15-17 CPS reading speed
 * - Avoid short prepositions at line ends (a, de, en, la, etc.)
 * - Break on: . ! ? ¿ ¡
 */
MUNINN_API CaptionEngineOptions spanish();

/**
 * @brief Portuguese (Brazilian) caption preset
 *
 * - 42 characters per line
 * - Word-level granularity
 * - 15-17 CPS reading speed
 * - Avoid short prepositions at line ends (de, da, do, em, etc.)
 */
MUNINN_API CaptionEngineOptions portuguese();

/**
 * @brief Indonesian caption preset
 *
 * - 42 characters per line
 * - Word-level granularity
 * - 15-17 CPS reading speed
 */
MUNINN_API CaptionEngineOptions indonesian();

/**
 * @brief Vietnamese caption preset
 *
 * - 42 characters per line
 * - Word-level granularity (monosyllabic)
 * - 15-17 CPS reading speed
 * - Extensive diacritics support
 */
MUNINN_API CaptionEngineOptions vietnamese();

// ═══════════════════════════════════════════════════════════════════════════
// Southeast Asian - No word spaces
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Thai caption preset
 *
 * - 40 characters per line
 * - Character-level granularity (no word spaces)
 * - 12-14 CPS reading speed
 * - Break on: . ! ? ฯ ๆ
 */
MUNINN_API CaptionEngineOptions thai();

// ═══════════════════════════════════════════════════════════════════════════
// Utility Functions
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Get preset by language code
 * @param langCode ISO 639-1 language code
 * @return Preset options for the language (falls back to English)
 */
MUNINN_API CaptionEngineOptions forLanguage(const std::string& langCode);

/**
 * @brief Get all supported language codes
 */
MUNINN_API std::vector<std::string> supportedLanguages();

/**
 * @brief Check if a language is supported
 */
MUNINN_API bool isLanguageSupported(const std::string& langCode);

} // namespace CaptionPresets

} // namespace muninn
