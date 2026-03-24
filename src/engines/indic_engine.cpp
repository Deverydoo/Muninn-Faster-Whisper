#include "muninn/engines/indic_engine.h"
#include <algorithm>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// Hindi Caption Engine
// ═══════════════════════════════════════════════════════════════════════════

int HindiCaptionEngine::supports(const std::string& langCode, const std::string& script) const {
    if (langCode == "hi" || langCode == "hin") {
        return 100;  // Primary support
    }
    // Other Devanagari languages (lower priority)
    if (langCode == "mr" || langCode == "mar" ||  // Marathi
        langCode == "ne" || langCode == "nep" ||  // Nepali
        langCode == "sa" || langCode == "san") {  // Sanskrit
        return 70;
    }
    if (script == "Deva") {
        return 50;  // Generic Devanagari fallback
    }
    return 0;
}

std::vector<std::string> HindiCaptionEngine::supportedLanguages() const {
    return {"hi"};
}

std::vector<CaptionToken> HindiCaptionEngine::tokenize(
    const std::vector<Word>& words,
    const CaptionEngineOptions& options) const {

    return tokenizeWords(words, options);
}

size_t HindiCaptionEngine::countCharacters(const std::string& text) const {
    size_t count = 0;
    auto chars = utf8ToChars(text);

    for (const auto& ch : chars) {
        // Skip spaces and newlines
        if (ch == " " || ch == "\n" || ch == "\r" || ch == "\t") {
            continue;
        }

        uint32_t cp = utf8ToCodepoint(ch);

        // Count Devanagari characters
        if (isDevanagari(cp)) {
            count++;
            continue;
        }

        // Count ASCII (for mixed content, numbers)
        if (cp >= 0x21 && cp <= 0x7E) {
            count++;
        }
    }

    return count;
}

bool HindiCaptionEngine::isSentenceEnd(const std::string& text) const {
    if (text.empty()) return false;

    std::string lastChar = getLastUtf8Char(text);

    // Hindi sentence-ending punctuation
    // । (Devanagari Danda), ॥ (Double Danda), and ASCII . ! ?
    return lastChar == "." || lastChar == "!" || lastChar == "?" ||
           lastChar == "\xE0\xA5\xA4" ||  // । U+0964 DEVANAGARI DANDA
           lastChar == "\xE0\xA5\xA5";    // ॥ U+0965 DEVANAGARI DOUBLE DANDA
}

std::string HindiCaptionEngine::splitLines(const std::string& text,
                                            int maxChars,
                                            int maxLines) const {
    // Hindi uses spaces between words, so split at word boundaries
    return splitLinesAtWords(text, maxChars, maxLines);
}

} // namespace muninn
