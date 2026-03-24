#include "muninn/engines/cjk_engine.h"
#include <algorithm>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// Japanese Caption Engine
// ═══════════════════════════════════════════════════════════════════════════

int JapaneseCaptionEngine::supports(const std::string& langCode, const std::string& script) const {
    if (langCode == "ja" || langCode == "jpn") {
        return 100;  // Primary support
    }
    if (script == "Jpan" || script == "Hira" || script == "Kana") {
        return 90;
    }
    return 0;
}

std::vector<std::string> JapaneseCaptionEngine::supportedLanguages() const {
    return {"ja"};
}

std::vector<CaptionToken> JapaneseCaptionEngine::tokenize(
    const std::vector<Word>& words,
    const CaptionEngineOptions& options) const {

    std::vector<CaptionToken> tokens;

    for (size_t i = 0; i < words.size(); ++i) {
        const Word& word = words[i];

        // Split each word into individual characters
        auto charTokens = splitWordToCharacters(word, static_cast<int>(i));
        tokens.insert(tokens.end(), charTokens.begin(), charTokens.end());
    }

    return tokens;
}

size_t JapaneseCaptionEngine::countCharacters(const std::string& text) const {
    size_t count = 0;
    auto chars = utf8ToChars(text);

    for (const auto& ch : chars) {
        // Skip spaces and newlines
        if (ch == " " || ch == "　" || ch == "\n" || ch == "\r") {
            continue;
        }
        count++;
    }

    return count;
}

bool JapaneseCaptionEngine::isSentenceEnd(const std::string& text) const {
    if (text.empty()) return false;

    std::string lastChar = getLastUtf8Char(text);

    // Japanese sentence-ending punctuation: 。！？…
    return lastChar == "。" || lastChar == "！" || lastChar == "？" ||
           lastChar == "…" || lastChar == "!" || lastChar == "?";
}

std::string JapaneseCaptionEngine::splitLines(const std::string& text,
                                               int maxChars,
                                               int maxLines) const {
    return splitLinesAtCharacters(text, maxChars, maxLines);
}

bool JapaneseCaptionEngine::canStartLine(const std::string& ch) const {
    return !isKinsokuNoStart(ch);
}

bool JapaneseCaptionEngine::canEndLine(const std::string& ch) const {
    return !isKinsokuNoEnd(ch);
}

bool JapaneseCaptionEngine::isKinsokuNoStart(const std::string& ch) {
    // Characters that should NOT start a line (kinsoku shori)
    static const std::vector<std::string> noStart = {
        // Punctuation
        ".", ",", "!", "?", ":", ";",
        "。", "、", "！", "？", "：", "；",
        // Closing brackets
        ")", "]", "}", "〉", "》", "」", "』", "】", "〕",
        "）", "］", "｝",
        // Small kana
        "ぁ", "ぃ", "ぅ", "ぇ", "ぉ", "っ", "ゃ", "ゅ", "ょ", "ゎ",
        "ァ", "ィ", "ゥ", "ェ", "ォ", "ッ", "ャ", "ュ", "ョ", "ヮ",
        // Other marks
        "…", "‥", "ー", "〜", "～",
        // Iteration marks
        "ゝ", "ゞ", "ヽ", "ヾ"
    };

    for (const auto& c : noStart) {
        if (ch == c) return true;
    }
    return false;
}

bool JapaneseCaptionEngine::isKinsokuNoEnd(const std::string& ch) {
    // Characters that should NOT end a line
    static const std::vector<std::string> noEnd = {
        // Opening brackets
        "(", "[", "{", "〈", "《", "「", "『", "【", "〔",
        "（", "［", "｛",
        // Currency and number prefixes
        "¥", "$", "￥", "€", "#"
    };

    for (const auto& c : noEnd) {
        if (ch == c) return true;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// Korean Caption Engine
// ═══════════════════════════════════════════════════════════════════════════

int KoreanCaptionEngine::supports(const std::string& langCode, const std::string& script) const {
    if (langCode == "ko" || langCode == "kor") {
        return 100;  // Primary support
    }
    if (script == "Kore" || script == "Hang") {
        return 90;
    }
    return 0;
}

std::vector<std::string> KoreanCaptionEngine::supportedLanguages() const {
    return {"ko"};
}

std::vector<CaptionToken> KoreanCaptionEngine::tokenize(
    const std::vector<Word>& words,
    const CaptionEngineOptions& options) const {

    std::vector<CaptionToken> tokens;

    for (size_t i = 0; i < words.size(); ++i) {
        const Word& word = words[i];

        // Split Korean words into syllable tokens
        auto syllableTokens = splitHangulSyllables(word, static_cast<int>(i));
        tokens.insert(tokens.end(), syllableTokens.begin(), syllableTokens.end());
    }

    return tokens;
}

size_t KoreanCaptionEngine::countCharacters(const std::string& text) const {
    size_t count = 0;
    auto chars = utf8ToChars(text);

    for (const auto& ch : chars) {
        // Skip spaces and newlines
        if (ch == " " || ch == "　" || ch == "\n" || ch == "\r" || ch == "\t") {
            continue;
        }

        uint32_t cp = utf8ToCodepoint(ch);

        // Count Hangul syllables
        if (isHangulSyllable(cp) || isHangulJamo(cp)) {
            count++;
            continue;
        }

        // Count CJK ideographs
        if (isCjkIdeograph(cp)) {
            count++;
            continue;
        }

        // Count fullwidth characters
        if (cp >= 0xFF00 && cp <= 0xFFEF) {
            count++;
            continue;
        }

        // Count printable ASCII
        if (cp >= 0x21 && cp < 0x80) {
            count++;
        }
    }

    return count;
}

bool KoreanCaptionEngine::isSentenceEnd(const std::string& text) const {
    if (text.empty()) return false;

    std::string lastChar = getLastUtf8Char(text);

    // Korean sentence-ending punctuation
    return lastChar == "." || lastChar == "!" || lastChar == "?" ||
           lastChar == "。" || lastChar == "！" || lastChar == "？";
}

std::string KoreanCaptionEngine::splitLines(const std::string& text,
                                             int maxChars,
                                             int maxLines) const {
    // Korean uses spaces, so we can split at word boundaries
    // But also respect geumchik rules
    if (static_cast<int>(countCharacters(text)) <= maxChars) {
        return text;
    }

    auto chars = utf8ToChars(text);
    std::vector<std::string> lines;
    std::string currentLine;
    size_t currentCount = 0;

    for (size_t i = 0; i < chars.size() && static_cast<int>(lines.size()) < maxLines; ++i) {
        const std::string& ch = chars[i];

        // Check if we can break here
        bool canBreakHere = true;
        if (i + 1 < chars.size()) {
            if (isGeumchikNoEnd(ch)) canBreakHere = false;
            if (isGeumchikNoStart(chars[i + 1])) canBreakHere = false;
        }

        // Count non-space characters
        bool isSpace = (ch == " " || ch == "　");
        if (!isSpace && ch != "\n" && ch != "\r") {
            currentCount++;
        }

        currentLine += ch;

        // Prefer breaking at spaces for Korean
        if (isSpace && currentCount >= static_cast<size_t>(maxChars * 2 / 3)) {
            lines.push_back(currentLine);
            currentLine.clear();
            currentCount = 0;
            continue;
        }

        if (currentCount >= static_cast<size_t>(maxChars) && canBreakHere) {
            // Trim trailing space
            while (!currentLine.empty() && currentLine.back() == ' ') {
                currentLine.pop_back();
            }
            lines.push_back(currentLine);
            currentLine.clear();
            currentCount = 0;
        }
    }

    // Add remaining
    if (!currentLine.empty() && static_cast<int>(lines.size()) < maxLines) {
        while (!currentLine.empty() && currentLine.back() == ' ') {
            currentLine.pop_back();
        }
        if (!currentLine.empty()) {
            lines.push_back(currentLine);
        }
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

bool KoreanCaptionEngine::canStartLine(const std::string& ch) const {
    return !isGeumchikNoStart(ch);
}

bool KoreanCaptionEngine::canEndLine(const std::string& ch) const {
    return !isGeumchikNoEnd(ch);
}

bool KoreanCaptionEngine::isGeumchikNoStart(const std::string& ch) {
    // Characters that should NOT start a line (Korean geumchik)
    static const std::vector<std::string> noStart = {
        // Punctuation
        ".", ",", "!", "?", ":", ";",
        "。", "、", "！", "？", "：", "；",
        // Closing brackets
        ")", "]", "}", "〉", "》", "」", "』", "】", "〕",
        "）", "］", "｝",
        // Other marks
        "…", "·", "~", "～"
    };

    for (const auto& c : noStart) {
        if (ch == c) return true;
    }
    return false;
}

bool KoreanCaptionEngine::isGeumchikNoEnd(const std::string& ch) {
    // Characters that should NOT end a line
    static const std::vector<std::string> noEnd = {
        // Opening brackets
        "(", "[", "{", "〈", "《", "「", "『", "【", "〔",
        "（", "［", "｛",
        // Currency symbols
        "₩", "$", "￥", "€"
    };

    for (const auto& c : noEnd) {
        if (ch == c) return true;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// Chinese Caption Engine
// ═══════════════════════════════════════════════════════════════════════════

ChineseCaptionEngine::ChineseCaptionEngine(Variant variant)
    : variant_(variant) {}

int ChineseCaptionEngine::supports(const std::string& langCode, const std::string& script) const {
    // Simplified Chinese
    if (variant_ == Variant::Simplified) {
        if (langCode == "zh" || langCode == "zh-CN" || langCode == "zh-Hans" || langCode == "cmn") {
            return 100;
        }
        if (script == "Hans") {
            return 95;
        }
    }
    // Traditional Chinese
    else {
        if (langCode == "zh-TW" || langCode == "zh-HK" || langCode == "zh-Hant") {
            return 100;
        }
        if (script == "Hant") {
            return 95;
        }
    }

    // Generic Chinese (lower priority)
    if (langCode == "zh" && script.empty()) {
        return 50;
    }
    if (script == "Hani") {
        return 40;
    }

    return 0;
}

std::vector<std::string> ChineseCaptionEngine::supportedLanguages() const {
    if (variant_ == Variant::Simplified) {
        return {"zh", "zh-CN", "zh-Hans"};
    } else {
        return {"zh-TW", "zh-HK", "zh-Hant"};
    }
}

std::vector<CaptionToken> ChineseCaptionEngine::tokenize(
    const std::vector<Word>& words,
    const CaptionEngineOptions& options) const {

    std::vector<CaptionToken> tokens;

    for (size_t i = 0; i < words.size(); ++i) {
        const Word& word = words[i];

        // Split each word into individual characters
        auto charTokens = splitWordToCharacters(word, static_cast<int>(i));
        tokens.insert(tokens.end(), charTokens.begin(), charTokens.end());
    }

    return tokens;
}

size_t ChineseCaptionEngine::countCharacters(const std::string& text) const {
    size_t count = 0;
    auto chars = utf8ToChars(text);

    for (const auto& ch : chars) {
        // Skip spaces and newlines
        if (ch == " " || ch == "　" || ch == "\n" || ch == "\r" || ch == "\t") {
            continue;
        }

        uint32_t cp = utf8ToCodepoint(ch);

        // Count CJK ideographs
        if (isCjkIdeograph(cp)) {
            count++;
            continue;
        }

        // Count fullwidth punctuation
        if (cp >= 0xFF00 && cp <= 0xFFEF) {
            count++;
            continue;
        }

        // CJK Symbols and Punctuation
        if (cp >= 0x3000 && cp <= 0x303F) {
            count++;
            continue;
        }

        // Count printable ASCII
        if (cp >= 0x21 && cp < 0x80) {
            count++;
        }
    }

    return count;
}

bool ChineseCaptionEngine::isSentenceEnd(const std::string& text) const {
    if (text.empty()) return false;

    std::string lastChar = getLastUtf8Char(text);

    // Chinese sentence-ending punctuation
    return lastChar == "。" || lastChar == "！" || lastChar == "？" ||
           lastChar == "…" || lastChar == "." || lastChar == "!" || lastChar == "?";
}

std::string ChineseCaptionEngine::splitLines(const std::string& text,
                                              int maxChars,
                                              int maxLines) const {
    return splitLinesAtCharacters(text, maxChars, maxLines);
}

bool ChineseCaptionEngine::canStartLine(const std::string& ch) const {
    return !isChineseNoStart(ch);
}

bool ChineseCaptionEngine::canEndLine(const std::string& ch) const {
    return !isChineseNoEnd(ch);
}

bool ChineseCaptionEngine::isChineseNoStart(const std::string& ch) {
    // Characters that should NOT start a line (Chinese punctuation rules)
    static const std::vector<std::string> noStart = {
        // Closing punctuation
        "。", "，", "、", "！", "？", "：", "；",
        ".", ",", "!", "?", ":", ";",
        // Closing brackets
        "）", "】", "》", "」", "』", "〉", "〕",
        ")", "]", "}",
        // Ellipsis and dash
        "…", "—", "～",
        // Middle dot
        "·"
    };

    for (const auto& c : noStart) {
        if (ch == c) return true;
    }
    return false;
}

bool ChineseCaptionEngine::isChineseNoEnd(const std::string& ch) {
    // Characters that should NOT end a line
    static const std::vector<std::string> noEnd = {
        // Opening brackets
        "（", "【", "《", "「", "『", "〈", "〔",
        "(", "[", "{",
        // Currency symbols
        "￥", "$", "€", "£"
    };

    for (const auto& c : noEnd) {
        if (ch == c) return true;
    }
    return false;
}

} // namespace muninn
