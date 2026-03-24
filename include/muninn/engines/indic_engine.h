#pragma once

#include "../caption_engine_base.h"
#include <vector>
#include <string>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// Indic Caption Engine
// ═══════════════════════════════════════════════════════════════════════════
//
// Handles Hindi and other Devanagari-script languages with:
// - Word-level granularity
// - Devanagari character counting
// - Danda punctuation (।)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Hindi caption engine (Devanagari)
 *
 * Features:
 * - Word-level tokenization
 * - 42 characters per line
 * - 14-16 CPS reading speed
 * - Sentence breaks on: । (Danda) ! ?
 */
class MUNINN_API HindiCaptionEngine : public CaptionEngineBase {
public:
    HindiCaptionEngine() = default;

    // Language support
    int supports(const std::string& langCode, const std::string& script) const override;
    std::vector<std::string> supportedLanguages() const override;
    std::string engineFamily() const override { return "Indic"; }

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
