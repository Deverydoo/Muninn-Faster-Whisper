#pragma once

#include "../caption_engine_base.h"
#include <vector>
#include <string>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// Thai Caption Engine
// ═══════════════════════════════════════════════════════════════════════════
//
// Handles Thai language with:
// - Character-level granularity (no word spaces)
// - Thai character counting
// - Thai-specific punctuation
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Thai caption engine
 *
 * Features:
 * - Character-level tokenization (no word spaces)
 * - 40 characters per line
 * - 12-14 CPS reading speed
 * - Sentence breaks on: . ! ? ฯ ๆ
 */
class MUNINN_API ThaiCaptionEngine : public CaptionEngineBase {
public:
    ThaiCaptionEngine() = default;

    // Language support
    int supports(const std::string& langCode, const std::string& script) const override;
    std::vector<std::string> supportedLanguages() const override;
    std::string engineFamily() const override { return "Thai"; }

    // Token processing
    TokenGranularity tokenGranularity() const override { return TokenGranularity::Character; }
    std::vector<CaptionToken> tokenize(const std::vector<Word>& words,
                                        const CaptionEngineOptions& options) const override;

    // Character counting
    size_t countCharacters(const std::string& text) const override;
    bool isSentenceEnd(const std::string& text) const override;
    std::string splitLines(const std::string& text, int maxChars, int maxLines) const override;
};

} // namespace muninn
