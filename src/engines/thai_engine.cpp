#include "muninn/engines/thai_engine.h"
#include <algorithm>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// Thai Caption Engine
// ═══════════════════════════════════════════════════════════════════════════

int ThaiCaptionEngine::supports(const std::string& langCode, const std::string& script) const {
    if (langCode == "th" || langCode == "tha") {
        return 100;  // Primary support
    }
    if (script == "Thai") {
        return 90;
    }
    return 0;
}

std::vector<std::string> ThaiCaptionEngine::supportedLanguages() const {
    return {"th"};
}

std::vector<CaptionToken> ThaiCaptionEngine::tokenize(
    const std::vector<Word>& words,
    const CaptionEngineOptions& options) const {

    std::vector<CaptionToken> tokens;

    // Thai has no word spaces, so split each "word" into characters
    for (size_t i = 0; i < words.size(); ++i) {
        const Word& word = words[i];

        // Split each word into individual characters
        auto charTokens = splitWordToCharacters(word, static_cast<int>(i));
        tokens.insert(tokens.end(), charTokens.begin(), charTokens.end());
    }

    return tokens;
}

size_t ThaiCaptionEngine::countCharacters(const std::string& text) const {
    size_t count = 0;
    auto chars = utf8ToChars(text);

    for (const auto& ch : chars) {
        // Skip spaces and newlines
        if (ch == " " || ch == "\n" || ch == "\r" || ch == "\t") {
            continue;
        }

        uint32_t cp = utf8ToCodepoint(ch);

        // Count Thai characters
        if (isThai(cp)) {
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

bool ThaiCaptionEngine::isSentenceEnd(const std::string& text) const {
    if (text.empty()) return false;

    std::string lastChar = getLastUtf8Char(text);

    // Thai sentence-ending punctuation
    // . ! ? and Thai-specific: ฯ (paiyannoi), ๆ (maiyamok - repetition mark)
    return lastChar == "." || lastChar == "!" || lastChar == "?" ||
           lastChar == "\xE0\xB8\xAF" ||  // ฯ U+0E2F THAI CHARACTER PAIYANNOI
           lastChar == "\xE0\xB9\x86";    // ๆ U+0E46 THAI CHARACTER MAIYAMOK
}

std::string ThaiCaptionEngine::splitLines(const std::string& text,
                                           int maxChars,
                                           int maxLines) const {
    // Thai has no word spaces, split at character boundaries
    return splitLinesAtCharacters(text, maxChars, maxLines);
}

} // namespace muninn
