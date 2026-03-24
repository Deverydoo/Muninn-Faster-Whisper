#include "muninn/engines/cyrillic_engine.h"
#include <algorithm>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// Russian Caption Engine
// ═══════════════════════════════════════════════════════════════════════════

int RussianCaptionEngine::supports(const std::string& langCode, const std::string& script) const {
    if (langCode == "ru" || langCode == "rus") {
        return 100;  // Primary support
    }
    // Other Cyrillic languages (lower priority - could add dedicated engines)
    if (langCode == "uk" || langCode == "ukr" ||  // Ukrainian
        langCode == "bg" || langCode == "bul" ||  // Bulgarian
        langCode == "be" || langCode == "bel") {  // Belarusian
        return 70;
    }
    if (script == "Cyrl") {
        return 50;  // Generic Cyrillic fallback
    }
    return 0;
}

std::vector<std::string> RussianCaptionEngine::supportedLanguages() const {
    return {"ru"};
}

std::vector<CaptionToken> RussianCaptionEngine::tokenize(
    const std::vector<Word>& words,
    const CaptionEngineOptions& options) const {

    return tokenizeWords(words, options);
}

size_t RussianCaptionEngine::countCharacters(const std::string& text) const {
    size_t count = 0;
    auto chars = utf8ToChars(text);

    for (const auto& ch : chars) {
        // Skip spaces and newlines
        if (ch == " " || ch == "\n" || ch == "\r" || ch == "\t") {
            continue;
        }

        uint32_t cp = utf8ToCodepoint(ch);

        // Count Cyrillic characters
        if (isCyrillic(cp)) {
            count++;
            continue;
        }

        // Count ASCII letters and digits
        if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
            (cp >= '0' && cp <= '9')) {
            count++;
            continue;
        }

        // Count punctuation (contributes to reading)
        if (cp >= 0x21 && cp <= 0x7E) {
            count++;
        }
    }

    return count;
}

bool RussianCaptionEngine::isSentenceEnd(const std::string& text) const {
    if (text.empty()) return false;

    std::string lastChar = getLastUtf8Char(text);

    // Russian sentence-ending punctuation
    // . ! ? — (em dash) … (ellipsis)
    return lastChar == "." || lastChar == "!" || lastChar == "?" ||
           lastChar == "\xE2\x80\x94" ||  // — U+2014 EM DASH
           lastChar == "\xE2\x80\xA6";    // … U+2026 HORIZONTAL ELLIPSIS
}

std::string RussianCaptionEngine::splitLines(const std::string& text,
                                              int maxChars,
                                              int maxLines) const {
    if (static_cast<int>(countCharacters(text)) <= maxChars) {
        return text;
    }

    // Split into words
    std::vector<std::string> words;
    std::string currentWord;

    for (size_t i = 0; i < text.size(); ) {
        size_t charLen = utf8CharLength(static_cast<unsigned char>(text[i]));
        std::string ch = text.substr(i, charLen);

        if (ch == " " || ch == "\n" || ch == "\r" || ch == "\t") {
            if (!currentWord.empty()) {
                words.push_back(currentWord);
                currentWord.clear();
            }
        } else {
            currentWord += ch;
        }
        i += charLen;
    }
    if (!currentWord.empty()) {
        words.push_back(currentWord);
    }

    // Build lines with preposition rules
    std::vector<std::string> lines;
    std::string currentLine;
    size_t currentCount = 0;

    for (size_t i = 0; i < words.size() && static_cast<int>(lines.size()) < maxLines; ++i) {
        const std::string& word = words[i];
        size_t wordLen = countCharacters(word);

        size_t newCount = currentCount + wordLen;
        if (!currentLine.empty()) {
            newCount += 1;  // Space
        }

        bool shouldBreak = (newCount > static_cast<size_t>(maxChars) && !currentLine.empty());

        // Check preposition rules before breaking
        if (shouldBreak && !currentLine.empty()) {
            // Get last word on current line
            size_t lastSpace = currentLine.rfind(' ');
            std::string lastWord = (lastSpace != std::string::npos)
                ? currentLine.substr(lastSpace + 1)
                : currentLine;

            // Don't leave single-letter prepositions at line end
            if (isSingleLetterPreposition(lastWord)) {
                if (lastSpace != std::string::npos) {
                    // Move preposition to next line
                    currentLine = currentLine.substr(0, lastSpace);
                    lines.push_back(currentLine);
                    currentLine = lastWord + " " + word;
                    currentCount = countCharacters(currentLine);
                    continue;
                } else {
                    // Line is just the preposition - don't break
                    shouldBreak = false;
                }
            }

            // Don't start line with closing punctuation
            if (shouldBreak && !word.empty()) {
                std::string firstChar = word.substr(0, utf8CharLength(static_cast<unsigned char>(word[0])));
                if (isRussianNoStart(firstChar)) {
                    shouldBreak = false;
                }
            }
        }

        if (shouldBreak) {
            lines.push_back(currentLine);
            currentLine = word;
            currentCount = wordLen;
        } else {
            if (!currentLine.empty()) {
                currentLine += " ";
                currentCount += 1;
            }
            currentLine += word;
            currentCount += wordLen;
        }
    }

    // Add remaining
    if (!currentLine.empty() && static_cast<int>(lines.size()) < maxLines) {
        lines.push_back(currentLine);
    }

    // Join with newlines
    std::string result;
    for (size_t i = 0; i < lines.size(); ++i) {
        result += lines[i];
        if (i < lines.size() - 1) {
            result += "\n";
        }
    }

    return result;
}

bool RussianCaptionEngine::canStartLine(const std::string& ch) const {
    return !isRussianNoStart(ch);
}

std::vector<std::string> RussianCaptionEngine::getShortWordsNoBreak() const {
    // Russian single-letter prepositions and conjunctions
    // Both lowercase and uppercase
    return {
        // Lowercase Cyrillic
        "\xD0\xB2",  // в (U+0432)
        "\xD0\xBA",  // к (U+043A)
        "\xD1\x81",  // с (U+0441)
        "\xD0\xBE",  // о (U+043E)
        "\xD1\x83",  // у (U+0443)
        "\xD0\xB0",  // а (U+0430)
        "\xD0\xB8",  // и (U+0438)
        "\xD1\x8F",  // я (U+044F)
        // Uppercase Cyrillic
        "\xD0\x92",  // В (U+0412)
        "\xD0\x9A",  // К (U+041A)
        "\xD0\xA1",  // С (U+0421)
        "\xD0\x9E",  // О (U+041E)
        "\xD0\xA3",  // У (U+0423)
        "\xD0\x90",  // А (U+0410)
        "\xD0\x98",  // И (U+0418)
        "\xD0\xAF"   // Я (U+042F)
    };
}

bool RussianCaptionEngine::isRussianNoStart(const std::string& ch) {
    // Characters that should NOT start a line in Russian
    static const std::vector<std::string> noStart = {
        // ASCII punctuation
        ".", ",", "!", "?", ":", ";", ")", "]", "}",
        // Em dash, en dash
        "\xE2\x80\x94",  // — U+2014 EM DASH
        "\xE2\x80\x93",  // – U+2013 EN DASH
        // Ellipsis
        "\xE2\x80\xA6",  // … U+2026 HORIZONTAL ELLIPSIS
        // Guillemets (Russian quotation marks) - closing
        "\xC2\xBB",      // » U+00BB RIGHT DOUBLE ANGLE QUOTATION
        "\xE2\x80\x9D",  // " U+201D RIGHT DOUBLE QUOTATION MARK
        "\xE2\x80\x99"   // ' U+2019 RIGHT SINGLE QUOTATION MARK
    };

    for (const auto& c : noStart) {
        if (ch == c) return true;
    }
    return false;
}

bool RussianCaptionEngine::isSingleLetterPreposition(const std::string& word) {
    // Russian single-letter prepositions
    static const std::vector<std::string> prepositions = {
        // Lowercase
        "\xD0\xB2",  // в
        "\xD0\xBA",  // к
        "\xD1\x81",  // с
        "\xD0\xBE",  // о
        "\xD1\x83",  // у
        "\xD0\xB0",  // а
        "\xD0\xB8",  // и
        "\xD1\x8F",  // я
        // Uppercase
        "\xD0\x92",  // В
        "\xD0\x9A",  // К
        "\xD0\xA1",  // С
        "\xD0\x9E",  // О
        "\xD0\xA3",  // У
        "\xD0\x90",  // А
        "\xD0\x98",  // И
        "\xD0\xAF"   // Я
    };

    for (const auto& prep : prepositions) {
        if (word == prep) return true;
    }

    return false;
}

} // namespace muninn
