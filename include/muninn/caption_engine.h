#pragma once

#include "export.h"
#include "types.h"
#include "caption_types.h"
#include "caption_options.h"
#include <string>
#include <vector>
#include <memory>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// Caption Engine Interface
// ═══════════════════════════════════════════════════════════════════════════
//
// Abstract interface for language-specific caption engines. Each engine
// handles the complete pipeline from ASR output to caption document:
//
// 1. supports() - Check if engine handles a language/script
// 2. normalize() - Optional text cleanup
// 3. tokenize() - Break into word/character/syllable tokens
// 4. buildCues() - Segment tokens into display cues
// 5. planPresentation() - Generate karaoke timing events
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Abstract interface for language-specific caption engines
 *
 * Implement this interface to add support for new languages.
 * Each engine handles:
 * - Language detection/support
 * - Text normalization
 * - Token segmentation (word/character/syllable)
 * - Caption cue construction (line breaking, timing)
 * - Presentation planning (karaoke events)
 */
class MUNINN_API ICaptionEngine {
public:
    virtual ~ICaptionEngine() = default;

    // ═══════════════════════════════════════════════════════════════════════
    // Stage A: Language Support
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Check if engine supports a language/script combination
     *
     * @param langCode ISO 639-1 language code (e.g., "ja", "ko", "zh")
     * @param script Optional script hint (e.g., "Hans", "Hant", "Jpan")
     * @return Support level (0 = no support, 100 = primary support)
     *
     * Higher values indicate better support. Used by router to select
     * the best engine when multiple engines can handle a language.
     */
    virtual int supports(const std::string& langCode,
                        const std::string& script = "") const = 0;

    /**
     * @brief Get the primary languages this engine handles
     * @return List of ISO 639-1 language codes
     */
    virtual std::vector<std::string> supportedLanguages() const = 0;

    /**
     * @brief Get engine family name (for logging/debugging)
     * @return Family name (e.g., "CJK", "Latin", "Cyrillic", "RTL")
     */
    virtual std::string engineFamily() const = 0;

    /**
     * @brief Get engine version
     * @return Version string (e.g., "1.0.0")
     */
    virtual std::string engineVersion() const { return "1.0.0"; }

    // ═══════════════════════════════════════════════════════════════════════
    // Stage B: Text Processing
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Normalize text for the target language
     *
     * Language-specific cleanup before tokenization:
     * - CJK: Normalize fullwidth/halfwidth characters
     * - Arabic: Normalize diacritics and ligatures
     * - Russian: Normalize ё/е alternation
     * - General: Trim whitespace, normalize Unicode
     *
     * @param text Raw ASR text
     * @return Normalized text
     */
    virtual std::string normalize(const std::string& text) const = 0;

    /**
     * @brief Get the token granularity for this engine
     *
     * Determines how text is broken down for karaoke highlighting:
     * - Word: Western languages
     * - Character: CJK, Thai
     * - Syllable: Korean
     *
     * @return Default token granularity
     */
    virtual TokenGranularity tokenGranularity() const = 0;

    /**
     * @brief Tokenize ASR words into caption tokens
     *
     * Converts ASR word-level output into tokens appropriate for
     * the language's display requirements.
     *
     * For word-based languages: 1 Word → 1 CaptionToken
     * For character-based: 1 Word → N CaptionTokens (characters)
     * For syllable-based: 1 Word → N CaptionTokens (syllables)
     *
     * @param words ASR word-level segments
     * @param options Engine configuration
     * @return Token stream with timing
     */
    virtual std::vector<CaptionToken> tokenize(
        const std::vector<Word>& words,
        const CaptionEngineOptions& options) const = 0;

    // ═══════════════════════════════════════════════════════════════════════
    // Stage C: Caption Construction
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Build caption cues from token stream
     *
     * Groups tokens into display cues, applying:
     * - Line length limits
     * - Duration constraints
     * - CPS (characters per second) control
     * - Language-specific punctuation/line-break rules
     * - Pause-based segmentation
     *
     * @param tokens Token stream from tokenize()
     * @param options Engine configuration
     * @return Caption cues ready for presentation
     */
    virtual std::vector<CaptionCue> buildCues(
        const std::vector<CaptionToken>& tokens,
        const CaptionEngineOptions& options) const = 0;

    // ═══════════════════════════════════════════════════════════════════════
    // Stage D: Presentation Planning
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Plan presentation events for a cue
     *
     * Generates karaoke-style timing events:
     * - Lead-in: Show caption before first token
     * - Token reveal: Progressive token visibility
     * - Token highlight: Current token highlight on/off
     * - Hold: Keep visible after speech
     * - Fade-out: Smooth disappearance
     *
     * @param cue Caption cue to plan
     * @param options Presentation settings
     * @return Presentation events for rendering
     */
    virtual std::vector<PresentationEvent> planPresentation(
        const CaptionCue& cue,
        const PresentationOptions& options) const = 0;

    // ═══════════════════════════════════════════════════════════════════════
    // Convenience Methods
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Count displayable characters (for CPS/line length)
     *
     * Counts characters appropriate for the language:
     * - Excludes spaces for CJK
     * - Counts accented chars as single units
     * - Handles multi-byte UTF-8 correctly
     *
     * @param text Text to count
     * @return Character count for display/CPS calculations
     */
    virtual size_t countCharacters(const std::string& text) const = 0;

    /**
     * @brief Check if text ends with sentence-ending punctuation
     *
     * Language-specific sentence detection:
     * - Japanese: 。！？…
     * - Korean: . ! ? 。！？
     * - Chinese: 。！？…
     * - Russian: . ! ? — …
     * - Arabic: . ! ? ؟
     * - Hindi: । ! ?
     *
     * @param text Text to check
     * @return True if ends with sentence punctuation
     */
    virtual bool isSentenceEnd(const std::string& text) const = 0;

    /**
     * @brief Split text into lines for display
     *
     * Applies language-specific line-breaking rules:
     * - CJK: Kinsoku shori (punctuation restrictions)
     * - Korean: Geumchik rules
     * - Russian: Avoid dangling prepositions
     * - Spanish: Avoid short words at line ends
     *
     * @param text Text to split
     * @param maxChars Maximum characters per line
     * @param maxLines Maximum number of lines
     * @return Text with newlines inserted
     */
    virtual std::string splitLines(const std::string& text,
                                   int maxChars,
                                   int maxLines = 2) const = 0;

    /**
     * @brief Calculate CPS (characters per second)
     *
     * @param text Text to measure
     * @param duration Duration in seconds
     * @return Characters per second
     */
    virtual float calculateCps(const std::string& text, float duration) const {
        if (duration <= 0.0f) return 0.0f;
        return static_cast<float>(countCharacters(text)) / duration;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // High-Level API
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Process ASR segments into a complete caption document
     *
     * Convenience method that performs the full pipeline:
     * 1. Extract words from segments
     * 2. Normalize text
     * 3. Tokenize
     * 4. Build cues
     * 5. Plan presentation
     *
     * @param segments ASR transcription segments
     * @param options Engine configuration
     * @return Complete caption document
     */
    virtual CaptionDocument process(
        const std::vector<Segment>& segments,
        const CaptionEngineOptions& options) const;

protected:
    /**
     * @brief Extract all words from segments
     * @param segments ASR segments
     * @return Flattened word list
     */
    std::vector<Word> extractWords(const std::vector<Segment>& segments) const;

    /**
     * @brief Generate presentation events for all cues
     * @param cues Caption cues
     * @param options Presentation settings
     * @return All presentation events, sorted by time
     */
    std::vector<PresentationEvent> planAllPresentation(
        const std::vector<CaptionCue>& cues,
        const PresentationOptions& options) const;
};

// ═══════════════════════════════════════════════════════════════════════════
// Engine Factory
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Factory for creating caption engines
 */
class MUNINN_API CaptionEngineFactory {
public:
    /**
     * @brief Create engine by language code
     * @param langCode ISO 639-1 language code
     * @return New engine instance (caller owns)
     */
    static std::unique_ptr<ICaptionEngine> create(const std::string& langCode);

    /**
     * @brief Create engine by family name
     * @param family Engine family (CJK, Latin, Cyrillic, RTL, Indic, Thai)
     * @return New engine instance (caller owns)
     */
    static std::unique_ptr<ICaptionEngine> createByFamily(const std::string& family);

    /**
     * @brief Create all default engines
     * @return Vector of all engine instances
     */
    static std::vector<std::unique_ptr<ICaptionEngine>> createAll();

    /**
     * @brief Get list of supported engine families
     */
    static std::vector<std::string> families();
};

} // namespace muninn
