#pragma once

#include "caption_engine.h"
#include <string>
#include <vector>
#include <cstdint>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// Caption Engine Base Class
// ═══════════════════════════════════════════════════════════════════════════
//
// Base implementation of ICaptionEngine with common logic shared by all
// language engines:
//
// - UTF-8 character handling utilities
// - Generic cue building algorithm (template method pattern)
// - Default presentation planning (AAA karaoke timing)
// - Character counting helpers
//
// Subclasses override hooks for language-specific behavior.
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Base implementation of ICaptionEngine
 *
 * Provides default implementations and shared utilities.
 * Subclasses must implement:
 * - supports()
 * - supportedLanguages()
 * - engineFamily()
 * - tokenGranularity()
 * - tokenize()
 * - countCharacters()
 * - isSentenceEnd()
 * - splitLines()
 *
 * Can optionally override:
 * - normalize()
 * - buildCues()
 * - planPresentation()
 * - shouldBreakCue()
 * - canStartLine() / canEndLine()
 */
class MUNINN_API CaptionEngineBase : public ICaptionEngine {
public:
    CaptionEngineBase() = default;
    virtual ~CaptionEngineBase() = default;

    // ═══════════════════════════════════════════════════════════════════════
    // Default Implementations (can be overridden)
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Default normalization (trim whitespace, no language-specific changes)
     */
    std::string normalize(const std::string& text) const override;

    /**
     * @brief Generic cue builder using template method pattern
     *
     * Calls hooks for language-specific decisions:
     * - shouldBreakCue()
     * - canStartLine() / canEndLine()
     */
    std::vector<CaptionCue> buildCues(
        const std::vector<CaptionToken>& tokens,
        const CaptionEngineOptions& options) const override;

    /**
     * @brief Default presentation planning (AAA karaoke timing)
     */
    std::vector<PresentationEvent> planPresentation(
        const CaptionCue& cue,
        const PresentationOptions& options) const override;

public:
    // ═══════════════════════════════════════════════════════════════════════
    // UTF-8 Utilities (shared by all engines - public static for helpers)
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Get UTF-8 character length from first byte
     * @param c First byte of UTF-8 character
     * @return Number of bytes (1-4)
     */
    static size_t utf8CharLength(unsigned char c);

    /**
     * @brief Get the last UTF-8 character in a string
     * @param text UTF-8 string
     * @return Last character as string (may be multi-byte)
     */
    static std::string getLastUtf8Char(const std::string& text);

    /**
     * @brief Get the first UTF-8 character in a string
     * @param text UTF-8 string
     * @return First character as string (may be multi-byte)
     */
    static std::string getFirstUtf8Char(const std::string& text);

    /**
     * @brief Split string into individual UTF-8 characters
     * @param text UTF-8 string
     * @return Vector of individual characters
     */
    static std::vector<std::string> utf8ToChars(const std::string& text);

    /**
     * @brief Convert UTF-8 character to Unicode codepoint
     * @param ch UTF-8 character (1-4 bytes)
     * @return Unicode codepoint
     */
    static uint32_t utf8ToCodepoint(const std::string& ch);

    /**
     * @brief Convert Unicode codepoint to UTF-8 string
     * @param cp Unicode codepoint
     * @return UTF-8 encoded string
     */
    static std::string codepointToUtf8(uint32_t cp);

    /**
     * @brief Count UTF-8 characters in string (not bytes)
     * @param text UTF-8 string
     * @return Number of characters
     */
    static size_t utf8Length(const std::string& text);

    /**
     * @brief Trim leading/trailing whitespace
     * @param text String to trim
     * @return Trimmed string
     */
    static std::string trim(const std::string& text);

    // ═══════════════════════════════════════════════════════════════════════
    // Unicode Range Checks (for script detection)
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Check if codepoint is CJK ideograph
     */
    static bool isCjkIdeograph(uint32_t cp);

    /**
     * @brief Check if codepoint is Hiragana
     */
    static bool isHiragana(uint32_t cp);

    /**
     * @brief Check if codepoint is Katakana
     */
    static bool isKatakana(uint32_t cp);

    /**
     * @brief Check if codepoint is Japanese (Hiragana, Katakana, or CJK)
     */
    static bool isJapanese(uint32_t cp);

    /**
     * @brief Check if codepoint is Hangul syllable
     */
    static bool isHangulSyllable(uint32_t cp);

    /**
     * @brief Check if codepoint is Hangul Jamo
     */
    static bool isHangulJamo(uint32_t cp);

    /**
     * @brief Check if codepoint is Korean (Hangul or CJK)
     */
    static bool isKorean(uint32_t cp);

    /**
     * @brief Check if codepoint is Cyrillic
     */
    static bool isCyrillic(uint32_t cp);

    /**
     * @brief Check if codepoint is Arabic
     */
    static bool isArabic(uint32_t cp);

    /**
     * @brief Check if codepoint is Devanagari (Hindi)
     */
    static bool isDevanagari(uint32_t cp);

    /**
     * @brief Check if codepoint is Thai
     */
    static bool isThai(uint32_t cp);

    /**
     * @brief Check if codepoint is Latin (basic + extended)
     */
    static bool isLatin(uint32_t cp);

    /**
     * @brief Check if codepoint is Vietnamese Latin (with diacritics)
     */
    static bool isVietnamese(uint32_t cp);

    /**
     * @brief Check if codepoint is printable (not whitespace/control)
     */
    static bool isPrintable(uint32_t cp);

protected:
    // ═══════════════════════════════════════════════════════════════════════
    // Cue Building Hooks (override in subclasses)
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Check if current cue should be flushed
     *
     * Called during cue building to determine break points.
     * Override for language-specific break conditions.
     *
     * @param text Current accumulated text
     * @param duration Current cue duration
     * @param tokenCount Number of tokens in cue
     * @param options Engine options
     * @return True to flush cue and start new one
     */
    virtual bool shouldBreakCue(const std::string& text,
                                float duration,
                                size_t tokenCount,
                                const CaptionEngineOptions& options) const;

    /**
     * @brief Check if character can start a line
     *
     * For kinsoku shori / geumchik rules.
     * Override for language-specific restrictions.
     *
     * @param ch UTF-8 character
     * @return True if can start a line
     */
    virtual bool canStartLine(const std::string& ch) const { return true; }

    /**
     * @brief Check if character can end a line
     *
     * For kinsoku shori / geumchik rules.
     * Override for language-specific restrictions.
     *
     * @param ch UTF-8 character
     * @return True if can end a line
     */
    virtual bool canEndLine(const std::string& ch) const { return true; }

    /**
     * @brief Get short words that shouldn't end a line
     *
     * For Russian single-letter prepositions, Spanish short words, etc.
     * Override to return language-specific list.
     *
     * @return List of words that shouldn't be left dangling at line end
     */
    virtual std::vector<std::string> getShortWordsNoBreak() const { return {}; }

    /**
     * @brief Check if word is a short word that shouldn't end a line
     * @param word Word to check
     * @return True if word shouldn't end a line
     */
    bool isShortWordNoBreak(const std::string& word) const;

    // ═══════════════════════════════════════════════════════════════════════
    // Token Building Helpers
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Create word-level tokens (1 word = 1 token)
     *
     * For word-based languages (English, Spanish, Russian, etc.)
     */
    std::vector<CaptionToken> tokenizeWords(
        const std::vector<Word>& words,
        const CaptionEngineOptions& options) const;

    /**
     * @brief Split a word into character tokens
     *
     * For character-based languages (Chinese, Thai)
     * Distributes word timing evenly across characters.
     */
    std::vector<CaptionToken> splitWordToCharacters(
        const Word& word,
        int wordIndex) const;

    /**
     * @brief Split a Hangul word into syllable tokens
     *
     * For Korean. Distributes word timing across syllables.
     */
    std::vector<CaptionToken> splitHangulSyllables(
        const Word& word,
        int wordIndex) const;

    // ═══════════════════════════════════════════════════════════════════════
    // Punctuation Detection
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Check if character is sentence-ending punctuation
     *
     * Checks common sentence enders across languages.
     * Override isSentenceEnd() for language-specific behavior.
     */
    static bool isSentenceEndPunctuation(const std::string& ch);

    /**
     * @brief Check if character is any punctuation
     */
    static bool isPunctuation(const std::string& ch);

    /**
     * @brief Check if character is whitespace
     */
    static bool isWhitespace(const std::string& ch);

    // ═══════════════════════════════════════════════════════════════════════
    // Line Splitting Helpers
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Split text at word boundaries (space-delimited)
     *
     * For word-based languages.
     */
    std::string splitLinesAtWords(const std::string& text,
                                  int maxChars,
                                  int maxLines) const;

    /**
     * @brief Split text at character boundaries
     *
     * For character-based languages (CJK, Thai).
     * Respects canStartLine() / canEndLine() rules.
     */
    std::string splitLinesAtCharacters(const std::string& text,
                                       int maxChars,
                                       int maxLines) const;
};

} // namespace muninn
