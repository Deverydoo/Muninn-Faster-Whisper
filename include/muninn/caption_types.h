#pragma once

#include "export.h"
#include <string>
#include <vector>
#include <cstdint>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// Caption Engine - Canonical Data Model
// ═══════════════════════════════════════════════════════════════════════════
//
// This module defines the intermediate representation between ASR output and
// subtitle file export. All caption engines produce this canonical format,
// enabling consistent rendering across SRT/VTT/ASS outputs.
//
// Pipeline: ASR Segments -> CaptionToken -> CaptionCue -> PresentationEvent
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Token granularity for caption presentation
 *
 * Determines how text is broken down for karaoke-style highlighting:
 * - Word: Western languages (whitespace-delimited)
 * - Character: CJK languages (per-character)
 * - Syllable: Korean (Hangul syllable blocks)
 * - Mora: Japanese (phonetic units)
 */
enum class TokenGranularity {
    Word,       // Word-level (English, Spanish, Russian, Arabic, Hindi, Indonesian, Vietnamese)
    Character,  // Character-level (Chinese, Thai)
    Syllable,   // Syllable-level (Korean Hangul blocks)
    Mora        // Mora-level (Japanese phonetic units)
};

/**
 * @brief Script family for routing
 */
enum class ScriptFamily {
    Latin,      // A-Z, extended Latin (Spanish, Portuguese, Vietnamese, Indonesian)
    CJK,        // Chinese, Japanese, Korean
    Cyrillic,   // Russian, Ukrainian, Bulgarian
    Arabic,     // Arabic, Persian, Urdu (RTL)
    Devanagari, // Hindi, Sanskrit
    Thai,       // Thai script (no word spaces)
    Hebrew,     // Hebrew (RTL)
    Unknown
};

/**
 * @brief Caption metadata
 */
struct MUNINN_API CaptionMetadata {
    std::string language;        // ISO 639-1 code (e.g., "ja", "ko", "en")
    std::string script;          // ISO 15924 code (e.g., "Jpan", "Hans", "Latn")
    std::string variant;         // Regional variant (e.g., "JP", "TW", "BR")
    std::string engineVersion;   // Engine version that produced this
    std::string engineFamily;    // Engine family name (CJK, Latin, Cyrillic, etc.)
    float confidence = 0.0f;     // Language detection confidence (0.0-1.0)

    CaptionMetadata() = default;
    CaptionMetadata(const std::string& lang, const std::string& scr = "")
        : language(lang), script(scr), confidence(1.0f) {}
};

/**
 * @brief Single caption token (word/character/syllable)
 *
 * The atomic unit for karaoke highlighting. Each token has:
 * - Text content
 * - Precise timing (from ASR word timestamps)
 * - Metadata for line-breaking and presentation
 */
struct MUNINN_API CaptionToken {
    std::string text;               // Token text
    float start = 0.0f;             // Start time (seconds)
    float end = 0.0f;               // End time (seconds)
    float confidence = 1.0f;        // ASR confidence (0.0-1.0)

    // Token properties
    TokenGranularity granularity = TokenGranularity::Word;
    bool isSentenceEnd = false;     // Ends with sentence-ending punctuation
    bool isPunctuation = false;     // Token is pure punctuation
    bool isLineBreakable = true;    // Can line break after this token?
    bool isNoLineStart = false;     // Cannot start a line (e.g., closing bracket)
    bool isNoLineEnd = false;       // Cannot end a line (e.g., opening bracket)

    // Original reference (for debugging/mapping)
    int sourceWordIndex = -1;       // Index in original Word array
    int sourceSegmentIndex = -1;    // Index in original Segment array

    CaptionToken() = default;
    CaptionToken(const std::string& t, float s, float e)
        : text(t), start(s), end(e) {}
    CaptionToken(const std::string& t, float s, float e, TokenGranularity g)
        : text(t), start(s), end(e), granularity(g) {}

    // Duration helper
    float duration() const { return end - start; }
};

/**
 * @brief Caption cue (displayed unit)
 *
 * A cue is what appears on screen at one time. It contains:
 * - Timing window (when to show/hide)
 * - Text content (optionally split into lines)
 * - References to tokens (for karaoke highlighting)
 * - Computed metrics (CPS, character count)
 */
struct MUNINN_API CaptionCue {
    int index = 0;                  // Cue number (1-based for SRT compat)
    float start = 0.0f;             // Display start time (seconds)
    float end = 0.0f;               // Display end time (seconds)

    // Content
    std::string text;               // Full text (may include \n for line breaks)
    std::vector<std::string> lines; // Split into lines

    // Token references for karaoke
    std::vector<int> tokenIndices;  // Indices into CaptionDocument::tokens
    std::vector<CaptionToken> tokens; // Direct token storage (alternative)

    // Computed metrics
    float cps = 0.0f;               // Characters per second (reading speed)
    int characterCount = 0;         // Total displayable characters (excluding spaces)
    int lineCount = 1;              // Number of lines

    // Speaker info (for diarization)
    int speakerId = -1;
    std::string speakerLabel;

    CaptionCue() = default;

    // Duration helper
    float duration() const { return end - start; }

    // Check if cue is valid
    bool isValid() const {
        return !text.empty() && end > start;
    }
};

/**
 * @brief Presentation event types
 *
 * Events for karaoke-style rendering with AAA-grade timing:
 * - CueShow: Cue appears (with optional lead-in before speech)
 * - TokenReveal: Token becomes visible (for progressive reveal)
 * - TokenHighlight: Token highlight turns ON (currently speaking)
 * - TokenDim: Token highlight turns OFF (already spoken)
 * - CueHold: Hold cue visible after speech
 * - CueFadeOut: Begin fade-out animation
 * - CueClear: Remove cue from screen
 */
enum class PresentationEventType {
    CueShow,        // Cue appears on screen
    TokenReveal,    // Token becomes visible (pre-highlight)
    TokenHighlight, // Token highlight ON (currently speaking)
    TokenDim,       // Token highlight OFF (already spoken)
    CueHold,        // Hold cue after last token
    CueFadeOut,     // Begin fade-out
    CueClear        // Remove cue from screen
};

/**
 * @brief Single presentation event
 *
 * Used by renderers to implement smooth karaoke timing.
 * Events are sorted chronologically for playback.
 */
struct MUNINN_API PresentationEvent {
    PresentationEventType type;
    float time = 0.0f;              // Event time (seconds)
    int cueIndex = -1;              // Which cue (index in CaptionDocument::cues)
    int tokenIndex = -1;            // Which token (for token events)
    float duration = 0.0f;          // Event duration (for fades)

    PresentationEvent() : type(PresentationEventType::CueShow) {}
    PresentationEvent(PresentationEventType t, float tm, int cue = -1, int tok = -1)
        : type(t), time(tm), cueIndex(cue), tokenIndex(tok) {}

    // Comparison for sorting
    bool operator<(const PresentationEvent& other) const {
        if (time != other.time) return time < other.time;
        // Same time: show before highlight, highlight before dim
        return static_cast<int>(type) < static_cast<int>(other.type);
    }
};

/**
 * @brief Complete caption document
 *
 * The canonical output from caption engines. Contains:
 * - Metadata (language, script, engine info)
 * - Token stream (all tokens with timing)
 * - Cue list (display units)
 * - Presentation events (karaoke timing)
 */
struct MUNINN_API CaptionDocument {
    CaptionMetadata metadata;
    std::vector<CaptionToken> tokens;
    std::vector<CaptionCue> cues;
    std::vector<PresentationEvent> events;

    // Duration info
    float totalDuration = 0.0f;     // Total media duration
    float lastSpeechEnd = 0.0f;     // End time of last speech

    CaptionDocument() = default;

    // Check if document is valid
    bool isValid() const {
        return !cues.empty();
    }

    // Get cue at a specific time
    const CaptionCue* getCueAtTime(float time) const {
        for (const auto& cue : cues) {
            if (time >= cue.start && time < cue.end) {
                return &cue;
            }
        }
        return nullptr;
    }

    // Get active token at a specific time (for karaoke)
    const CaptionToken* getActiveTokenAtTime(float time) const {
        for (const auto& token : tokens) {
            if (time >= token.start && time < token.end) {
                return &token;
            }
        }
        return nullptr;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Helper Functions
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Get script family from language code
 */
MUNINN_API ScriptFamily getScriptFamily(const std::string& langCode);

/**
 * @brief Get script family from script code (ISO 15924)
 */
MUNINN_API ScriptFamily getScriptFamilyFromScript(const std::string& scriptCode);

/**
 * @brief Get default token granularity for a language
 */
MUNINN_API TokenGranularity getDefaultGranularity(const std::string& langCode);

/**
 * @brief Check if a script family is RTL (right-to-left)
 */
MUNINN_API bool isRtlScript(ScriptFamily family);

/**
 * @brief Convert TokenGranularity to string
 */
MUNINN_API std::string tokenGranularityToString(TokenGranularity g);

/**
 * @brief Convert ScriptFamily to string
 */
MUNINN_API std::string scriptFamilyToString(ScriptFamily f);

/**
 * @brief Convert PresentationEventType to string
 */
MUNINN_API std::string presentationEventTypeToString(PresentationEventType t);

} // namespace muninn
