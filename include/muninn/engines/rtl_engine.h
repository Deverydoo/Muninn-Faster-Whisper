#pragma once

#include "../caption_engine_base.h"
#include <vector>
#include <string>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// RTL (Right-to-Left) Caption Engine
// ═══════════════════════════════════════════════════════════════════════════
//
// Handles Arabic and other RTL languages with:
// - Word-level granularity (RTL word order)
// - Ligature-safe word boundaries
// - Lower CPS for complex scripts
// - Arabic-specific punctuation
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Arabic caption engine
 *
 * Features:
 * - Word-level tokenization (RTL order)
 * - 42 characters per line
 * - 12-15 CPS reading speed (slower for RTL)
 * - Ligature-safe word boundaries
 * - Sentence breaks on: . ! ? ؟ ،
 */
class MUNINN_API ArabicCaptionEngine : public CaptionEngineBase {
public:
    ArabicCaptionEngine() = default;

    // Language support
    int supports(const std::string& langCode, const std::string& script) const override;
    std::vector<std::string> supportedLanguages() const override;
    std::string engineFamily() const override { return "RTL"; }

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
