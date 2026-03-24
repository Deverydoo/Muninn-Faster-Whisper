#pragma once

#include "../caption_engine_base.h"
#include <vector>
#include <string>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// Latin Caption Engine Family
// ═══════════════════════════════════════════════════════════════════════════
//
// Handles Latin-script languages with:
// - Word-level granularity
// - Whitespace-based word separation
// - Short preposition handling (avoid dangling at line end)
// - Standard Western punctuation rules
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief English caption engine (AAA karaoke)
 *
 * Features:
 * - Word-level tokenization
 * - 42 characters per line
 * - 17 CPS reading speed
 * - Professional karaoke timing
 */
class MUNINN_API EnglishCaptionEngine : public CaptionEngineBase {
public:
    EnglishCaptionEngine() = default;

    // Language support
    int supports(const std::string& langCode, const std::string& script) const override;
    std::vector<std::string> supportedLanguages() const override;
    std::string engineFamily() const override { return "Latin"; }

    // Token processing
    TokenGranularity tokenGranularity() const override { return TokenGranularity::Word; }
    std::vector<CaptionToken> tokenize(const std::vector<Word>& words,
                                        const CaptionEngineOptions& options) const override;

    // Character counting
    size_t countCharacters(const std::string& text) const override;
    bool isSentenceEnd(const std::string& text) const override;
    std::string splitLines(const std::string& text, int maxChars, int maxLines) const override;
};

/**
 * @brief Spanish caption engine
 *
 * Features:
 * - Word-level tokenization
 * - 42 characters per line
 * - 15-17 CPS reading speed
 * - Short preposition handling (a, de, en, la, etc.)
 * - Inverted punctuation support (¿, ¡)
 */
class MUNINN_API SpanishCaptionEngine : public CaptionEngineBase {
public:
    SpanishCaptionEngine() = default;

    // Language support
    int supports(const std::string& langCode, const std::string& script) const override;
    std::vector<std::string> supportedLanguages() const override;
    std::string engineFamily() const override { return "Latin"; }

    // Token processing
    TokenGranularity tokenGranularity() const override { return TokenGranularity::Word; }
    std::vector<CaptionToken> tokenize(const std::vector<Word>& words,
                                        const CaptionEngineOptions& options) const override;

    // Character counting
    size_t countCharacters(const std::string& text) const override;
    bool isSentenceEnd(const std::string& text) const override;
    std::string splitLines(const std::string& text, int maxChars, int maxLines) const override;

protected:
    std::vector<std::string> getShortWordsNoBreak() const override;
};

/**
 * @brief Portuguese caption engine
 *
 * Features:
 * - Word-level tokenization
 * - 42 characters per line
 * - 15-17 CPS reading speed
 * - Short preposition handling (de, da, do, em, etc.)
 */
class MUNINN_API PortugueseCaptionEngine : public CaptionEngineBase {
public:
    PortugueseCaptionEngine() = default;

    // Language support
    int supports(const std::string& langCode, const std::string& script) const override;
    std::vector<std::string> supportedLanguages() const override;
    std::string engineFamily() const override { return "Latin"; }

    // Token processing
    TokenGranularity tokenGranularity() const override { return TokenGranularity::Word; }
    std::vector<CaptionToken> tokenize(const std::vector<Word>& words,
                                        const CaptionEngineOptions& options) const override;

    // Character counting
    size_t countCharacters(const std::string& text) const override;
    bool isSentenceEnd(const std::string& text) const override;
    std::string splitLines(const std::string& text, int maxChars, int maxLines) const override;

protected:
    std::vector<std::string> getShortWordsNoBreak() const override;
};

/**
 * @brief Indonesian caption engine
 *
 * Features:
 * - Word-level tokenization
 * - 42 characters per line
 * - 15-17 CPS reading speed
 * - Standard Latin punctuation
 */
class MUNINN_API IndonesianCaptionEngine : public CaptionEngineBase {
public:
    IndonesianCaptionEngine() = default;

    // Language support
    int supports(const std::string& langCode, const std::string& script) const override;
    std::vector<std::string> supportedLanguages() const override;
    std::string engineFamily() const override { return "Latin"; }

    // Token processing
    TokenGranularity tokenGranularity() const override { return TokenGranularity::Word; }
    std::vector<CaptionToken> tokenize(const std::vector<Word>& words,
                                        const CaptionEngineOptions& options) const override;

    // Character counting
    size_t countCharacters(const std::string& text) const override;
    bool isSentenceEnd(const std::string& text) const override;
    std::string splitLines(const std::string& text, int maxChars, int maxLines) const override;
};

/**
 * @brief Vietnamese caption engine
 *
 * Features:
 * - Word-level tokenization (monosyllabic)
 * - 42 characters per line
 * - 15-17 CPS reading speed
 * - Extensive diacritics support
 */
class MUNINN_API VietnameseCaptionEngine : public CaptionEngineBase {
public:
    VietnameseCaptionEngine() = default;

    // Language support
    int supports(const std::string& langCode, const std::string& script) const override;
    std::vector<std::string> supportedLanguages() const override;
    std::string engineFamily() const override { return "Latin"; }

    // Token processing
    TokenGranularity tokenGranularity() const override { return TokenGranularity::Word; }
    std::vector<CaptionToken> tokenize(const std::vector<Word>& words,
                                        const CaptionEngineOptions& options) const override;

    // Character counting
    size_t countCharacters(const std::string& text) const override;
    bool isSentenceEnd(const std::string& text) const override;
    std::string splitLines(const std::string& text, int maxChars, int maxLines) const override;
};

} // namespace muninn
