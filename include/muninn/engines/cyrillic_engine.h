#pragma once

#include "../caption_engine_base.h"
#include <vector>
#include <string>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// Cyrillic Caption Engine
// ═══════════════════════════════════════════════════════════════════════════
//
// Handles Russian and other Cyrillic-script languages with:
// - Word-level granularity
// - Single-letter preposition handling (в, к, с, о, etc.)
// - Cyrillic character counting for CPS
// - Russian punctuation rules
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Russian caption engine
 *
 * Features:
 * - Word-level tokenization
 * - 40 characters per line
 * - 14-17 CPS reading speed
 * - Single-letter preposition handling (в, к, с, о, у, а, и)
 * - Sentence breaks on: . ! ? — …
 */
class MUNINN_API RussianCaptionEngine : public CaptionEngineBase {
public:
    RussianCaptionEngine() = default;

    // Language support
    int supports(const std::string& langCode, const std::string& script) const override;
    std::vector<std::string> supportedLanguages() const override;
    std::string engineFamily() const override { return "Cyrillic"; }

    // Token processing
    TokenGranularity tokenGranularity() const override { return TokenGranularity::Word; }
    std::vector<CaptionToken> tokenize(const std::vector<Word>& words,
                                        const CaptionEngineOptions& options) const override;

    // Character counting
    size_t countCharacters(const std::string& text) const override;
    bool isSentenceEnd(const std::string& text) const override;
    std::string splitLines(const std::string& text, int maxChars, int maxLines) const override;

protected:
    // Line-break rules
    bool canStartLine(const std::string& ch) const override;
    std::vector<std::string> getShortWordsNoBreak() const override;

private:
    static bool isRussianNoStart(const std::string& ch);
    static bool isSingleLetterPreposition(const std::string& word);
};

} // namespace muninn
