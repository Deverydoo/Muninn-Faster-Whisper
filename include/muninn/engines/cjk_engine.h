#pragma once

#include "../caption_engine_base.h"
#include <vector>
#include <string>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// CJK Caption Engine Family
// ═══════════════════════════════════════════════════════════════════════════
//
// Handles Japanese, Korean, and Chinese captioning with:
// - Character-level granularity (Chinese, Japanese display)
// - Syllable-level granularity (Korean Hangul)
// - Kinsoku shori / Geumchik line-break rules
// - CJK-specific punctuation handling
// - No-space word joining
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Japanese caption engine
 *
 * Features:
 * - Character-level tokenization (kanji/kana)
 * - Kinsoku shori (line-break prohibition rules)
 * - 22 characters per line default
 * - Sentence breaks on: 。！？…
 */
class MUNINN_API JapaneseCaptionEngine : public CaptionEngineBase {
public:
    JapaneseCaptionEngine() = default;

    // Language support
    int supports(const std::string& langCode, const std::string& script) const override;
    std::vector<std::string> supportedLanguages() const override;
    std::string engineFamily() const override { return "CJK"; }

    // Token processing
    TokenGranularity tokenGranularity() const override { return TokenGranularity::Character; }
    std::vector<CaptionToken> tokenize(const std::vector<Word>& words,
                                        const CaptionEngineOptions& options) const override;

    // Character counting
    size_t countCharacters(const std::string& text) const override;
    bool isSentenceEnd(const std::string& text) const override;
    std::string splitLines(const std::string& text, int maxChars, int maxLines) const override;

protected:
    // Kinsoku shori rules
    bool canStartLine(const std::string& ch) const override;
    bool canEndLine(const std::string& ch) const override;

private:
    static bool isKinsokuNoStart(const std::string& ch);
    static bool isKinsokuNoEnd(const std::string& ch);
};

/**
 * @brief Korean caption engine
 *
 * Features:
 * - Syllable-level tokenization (Hangul blocks)
 * - Geumchik (line-break rules)
 * - 23 characters per line default
 * - Mixed Hangul/CJK support
 */
class MUNINN_API KoreanCaptionEngine : public CaptionEngineBase {
public:
    KoreanCaptionEngine() = default;

    // Language support
    int supports(const std::string& langCode, const std::string& script) const override;
    std::vector<std::string> supportedLanguages() const override;
    std::string engineFamily() const override { return "CJK"; }

    // Token processing
    TokenGranularity tokenGranularity() const override { return TokenGranularity::Syllable; }
    std::vector<CaptionToken> tokenize(const std::vector<Word>& words,
                                        const CaptionEngineOptions& options) const override;

    // Character counting
    size_t countCharacters(const std::string& text) const override;
    bool isSentenceEnd(const std::string& text) const override;
    std::string splitLines(const std::string& text, int maxChars, int maxLines) const override;

protected:
    // Geumchik rules
    bool canStartLine(const std::string& ch) const override;
    bool canEndLine(const std::string& ch) const override;

private:
    static bool isGeumchikNoStart(const std::string& ch);
    static bool isGeumchikNoEnd(const std::string& ch);
};

/**
 * @brief Chinese caption engine (Simplified and Traditional)
 *
 * Features:
 * - Character-level tokenization
 * - Punctuation line-break rules
 * - 18 characters per line (Simplified), 16 (Traditional)
 * - 9-11 CPS reading speed
 */
class MUNINN_API ChineseCaptionEngine : public CaptionEngineBase {
public:
    enum class Variant {
        Simplified,   // zh-CN, Hans
        Traditional   // zh-TW, Hant
    };

    explicit ChineseCaptionEngine(Variant variant = Variant::Simplified);

    // Language support
    int supports(const std::string& langCode, const std::string& script) const override;
    std::vector<std::string> supportedLanguages() const override;
    std::string engineFamily() const override { return "CJK"; }

    // Token processing
    TokenGranularity tokenGranularity() const override { return TokenGranularity::Character; }
    std::vector<CaptionToken> tokenize(const std::vector<Word>& words,
                                        const CaptionEngineOptions& options) const override;

    // Character counting
    size_t countCharacters(const std::string& text) const override;
    bool isSentenceEnd(const std::string& text) const override;
    std::string splitLines(const std::string& text, int maxChars, int maxLines) const override;

protected:
    // Punctuation rules
    bool canStartLine(const std::string& ch) const override;
    bool canEndLine(const std::string& ch) const override;

private:
    Variant variant_;

    static bool isChineseNoStart(const std::string& ch);
    static bool isChineseNoEnd(const std::string& ch);
};

} // namespace muninn
