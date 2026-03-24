#include "muninn/engines/rtl_engine.h"
#include <algorithm>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// Arabic Caption Engine
// ═══════════════════════════════════════════════════════════════════════════

int ArabicCaptionEngine::supports(const std::string& langCode, const std::string& script) const {
    if (langCode == "ar" || langCode == "ara") {
        return 100;  // Primary support
    }
    // Other Arabic-script languages (lower priority)
    if (langCode == "fa" || langCode == "fas" ||  // Persian/Farsi
        langCode == "ur" || langCode == "urd") {  // Urdu
        return 70;
    }
    if (script == "Arab") {
        return 50;  // Generic Arabic script fallback
    }
    return 0;
}

std::vector<std::string> ArabicCaptionEngine::supportedLanguages() const {
    return {"ar"};
}

std::vector<CaptionToken> ArabicCaptionEngine::tokenize(
    const std::vector<Word>& words,
    const CaptionEngineOptions& options) const {

    return tokenizeWords(words, options);
}

size_t ArabicCaptionEngine::countCharacters(const std::string& text) const {
    size_t count = 0;
    auto chars = utf8ToChars(text);

    for (const auto& ch : chars) {
        // Skip spaces and newlines
        if (ch == " " || ch == "\n" || ch == "\r" || ch == "\t") {
            continue;
        }

        uint32_t cp = utf8ToCodepoint(ch);

        // Count Arabic characters
        if (isArabic(cp)) {
            count++;
            continue;
        }

        // Count ASCII (for mixed content)
        if (cp >= 0x21 && cp <= 0x7E) {
            count++;
        }
    }

    return count;
}

bool ArabicCaptionEngine::isSentenceEnd(const std::string& text) const {
    if (text.empty()) return false;

    std::string lastChar = getLastUtf8Char(text);

    // Arabic sentence-ending punctuation
    // . ! ? and Arabic question mark ؟
    return lastChar == "." || lastChar == "!" || lastChar == "?" ||
           lastChar == "\xD8\x9F" ||  // ؟ U+061F ARABIC QUESTION MARK
           lastChar == "\xD8\x9B";    // ؛ U+061B ARABIC SEMICOLON
}

std::string ArabicCaptionEngine::splitLines(const std::string& text,
                                             int maxChars,
                                             int maxLines) const {
    // Arabic uses spaces between words, so split at word boundaries
    // Note: The text itself is stored LTR in memory, rendering handles RTL display
    return splitLinesAtWords(text, maxChars, maxLines);
}

} // namespace muninn
