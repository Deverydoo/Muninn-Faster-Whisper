#include "muninn/caption_engine_base.h"
#include "muninn/caption_engine.h"
#include <algorithm>
#include <sstream>
#include <cmath>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// UTF-8 Utilities
// ═══════════════════════════════════════════════════════════════════════════

size_t CaptionEngineBase::utf8CharLength(unsigned char c) {
    if ((c & 0x80) == 0x00) return 1;      // ASCII
    if ((c & 0xE0) == 0xC0) return 2;      // 2-byte UTF-8
    if ((c & 0xF0) == 0xE0) return 3;      // 3-byte UTF-8 (CJK, etc.)
    if ((c & 0xF8) == 0xF0) return 4;      // 4-byte UTF-8 (emoji)
    return 1;  // Invalid, treat as single byte
}

std::string CaptionEngineBase::getLastUtf8Char(const std::string& text) {
    if (text.empty()) return "";

    size_t i = text.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) {
        --i;  // Skip continuation bytes
    }

    return text.substr(i);
}

std::string CaptionEngineBase::getFirstUtf8Char(const std::string& text) {
    if (text.empty()) return "";

    size_t charLen = utf8CharLength(static_cast<unsigned char>(text[0]));
    if (charLen > text.size()) return text;

    return text.substr(0, charLen);
}

std::vector<std::string> CaptionEngineBase::utf8ToChars(const std::string& text) {
    std::vector<std::string> chars;
    size_t i = 0;

    while (i < text.size()) {
        size_t charLen = utf8CharLength(static_cast<unsigned char>(text[i]));
        if (i + charLen <= text.size()) {
            chars.push_back(text.substr(i, charLen));
        }
        i += charLen;
    }

    return chars;
}

uint32_t CaptionEngineBase::utf8ToCodepoint(const std::string& ch) {
    if (ch.empty()) return 0;

    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(ch.data());
    size_t len = ch.size();

    if (len == 1) {
        return bytes[0];  // ASCII
    } else if (len == 2) {
        return ((bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F);
    } else if (len == 3) {
        return ((bytes[0] & 0x0F) << 12) | ((bytes[1] & 0x3F) << 6) | (bytes[2] & 0x3F);
    } else if (len == 4) {
        return ((bytes[0] & 0x07) << 18) | ((bytes[1] & 0x3F) << 12) |
               ((bytes[2] & 0x3F) << 6) | (bytes[3] & 0x3F);
    }

    return 0;
}

std::string CaptionEngineBase::codepointToUtf8(uint32_t cp) {
    std::string result;

    if (cp < 0x80) {
        result += static_cast<char>(cp);
    } else if (cp < 0x800) {
        result += static_cast<char>(0xC0 | (cp >> 6));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        result += static_cast<char>(0xE0 | (cp >> 12));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        result += static_cast<char>(0xF0 | (cp >> 18));
        result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    }

    return result;
}

size_t CaptionEngineBase::utf8Length(const std::string& text) {
    size_t count = 0;
    size_t i = 0;

    while (i < text.size()) {
        size_t charLen = utf8CharLength(static_cast<unsigned char>(text[i]));
        count++;
        i += charLen;
    }

    return count;
}

std::string CaptionEngineBase::trim(const std::string& text) {
    if (text.empty()) return text;

    size_t start = 0;
    size_t end = text.size();

    // Trim leading whitespace
    while (start < end) {
        unsigned char c = static_cast<unsigned char>(text[start]);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            start++;
        } else if (c == 0xE3 && start + 2 < end &&
                   static_cast<unsigned char>(text[start + 1]) == 0x80 &&
                   static_cast<unsigned char>(text[start + 2]) == 0x80) {
            // Ideographic space U+3000
            start += 3;
        } else {
            break;
        }
    }

    // Trim trailing whitespace
    while (end > start) {
        unsigned char c = static_cast<unsigned char>(text[end - 1]);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            end--;
        } else if (end >= 3 &&
                   static_cast<unsigned char>(text[end - 3]) == 0xE3 &&
                   static_cast<unsigned char>(text[end - 2]) == 0x80 &&
                   static_cast<unsigned char>(text[end - 1]) == 0x80) {
            // Ideographic space U+3000
            end -= 3;
        } else {
            break;
        }
    }

    return text.substr(start, end - start);
}

// ═══════════════════════════════════════════════════════════════════════════
// Unicode Range Checks
// ═══════════════════════════════════════════════════════════════════════════

bool CaptionEngineBase::isCjkIdeograph(uint32_t cp) {
    // CJK Unified Ideographs: U+4E00 to U+9FFF (main block)
    if (cp >= 0x4E00 && cp <= 0x9FFF) return true;

    // CJK Unified Ideographs Extension A: U+3400 to U+4DBF
    if (cp >= 0x3400 && cp <= 0x4DBF) return true;

    // CJK Unified Ideographs Extension B: U+20000 to U+2A6DF
    if (cp >= 0x20000 && cp <= 0x2A6DF) return true;

    // CJK Compatibility Ideographs: U+F900 to U+FAFF
    if (cp >= 0xF900 && cp <= 0xFAFF) return true;

    // CJK Radicals Supplement: U+2E80 to U+2EFF
    if (cp >= 0x2E80 && cp <= 0x2EFF) return true;

    // Kangxi Radicals: U+2F00 to U+2FDF
    if (cp >= 0x2F00 && cp <= 0x2FDF) return true;

    return false;
}

bool CaptionEngineBase::isHiragana(uint32_t cp) {
    // Hiragana: U+3040 to U+309F
    return cp >= 0x3040 && cp <= 0x309F;
}

bool CaptionEngineBase::isKatakana(uint32_t cp) {
    // Katakana: U+30A0 to U+30FF
    if (cp >= 0x30A0 && cp <= 0x30FF) return true;

    // Katakana Phonetic Extensions: U+31F0 to U+31FF
    if (cp >= 0x31F0 && cp <= 0x31FF) return true;

    return false;
}

bool CaptionEngineBase::isJapanese(uint32_t cp) {
    return isHiragana(cp) || isKatakana(cp) || isCjkIdeograph(cp);
}

bool CaptionEngineBase::isHangulSyllable(uint32_t cp) {
    // Hangul Syllables block: U+AC00 to U+D7A3
    return cp >= 0xAC00 && cp <= 0xD7A3;
}

bool CaptionEngineBase::isHangulJamo(uint32_t cp) {
    // Hangul Jamo: U+1100 to U+11FF
    if (cp >= 0x1100 && cp <= 0x11FF) return true;

    // Hangul Compatibility Jamo: U+3130 to U+318F
    if (cp >= 0x3130 && cp <= 0x318F) return true;

    // Hangul Jamo Extended-A: U+A960 to U+A97F
    if (cp >= 0xA960 && cp <= 0xA97F) return true;

    // Hangul Jamo Extended-B: U+D7B0 to U+D7FF
    if (cp >= 0xD7B0 && cp <= 0xD7FF) return true;

    return false;
}

bool CaptionEngineBase::isKorean(uint32_t cp) {
    return isHangulSyllable(cp) || isHangulJamo(cp) || isCjkIdeograph(cp);
}

bool CaptionEngineBase::isCyrillic(uint32_t cp) {
    // Cyrillic block: U+0400 to U+04FF
    if (cp >= 0x0400 && cp <= 0x04FF) return true;

    // Cyrillic Supplement: U+0500 to U+052F
    if (cp >= 0x0500 && cp <= 0x052F) return true;

    // Cyrillic Extended-A: U+2DE0 to U+2DFF
    if (cp >= 0x2DE0 && cp <= 0x2DFF) return true;

    // Cyrillic Extended-B: U+A640 to U+A69F
    if (cp >= 0xA640 && cp <= 0xA69F) return true;

    return false;
}

bool CaptionEngineBase::isArabic(uint32_t cp) {
    // Arabic block: U+0600 to U+06FF
    if (cp >= 0x0600 && cp <= 0x06FF) return true;

    // Arabic Supplement: U+0750 to U+077F
    if (cp >= 0x0750 && cp <= 0x077F) return true;

    // Arabic Extended-A: U+08A0 to U+08FF
    if (cp >= 0x08A0 && cp <= 0x08FF) return true;

    // Arabic Presentation Forms-A: U+FB50 to U+FDFF
    if (cp >= 0xFB50 && cp <= 0xFDFF) return true;

    // Arabic Presentation Forms-B: U+FE70 to U+FEFF
    if (cp >= 0xFE70 && cp <= 0xFEFF) return true;

    return false;
}

bool CaptionEngineBase::isDevanagari(uint32_t cp) {
    // Devanagari block: U+0900 to U+097F
    if (cp >= 0x0900 && cp <= 0x097F) return true;

    // Devanagari Extended: U+A8E0 to U+A8FF
    if (cp >= 0xA8E0 && cp <= 0xA8FF) return true;

    // Vedic Extensions: U+1CD0 to U+1CFF
    if (cp >= 0x1CD0 && cp <= 0x1CFF) return true;

    return false;
}

bool CaptionEngineBase::isThai(uint32_t cp) {
    // Thai block: U+0E00 to U+0E7F
    return cp >= 0x0E00 && cp <= 0x0E7F;
}

bool CaptionEngineBase::isLatin(uint32_t cp) {
    // Basic Latin (ASCII letters): U+0041 to U+007A
    if ((cp >= 0x0041 && cp <= 0x005A) || (cp >= 0x0061 && cp <= 0x007A)) return true;

    // Latin-1 Supplement: U+00C0 to U+00FF (accented characters)
    if (cp >= 0x00C0 && cp <= 0x00FF) return true;

    // Latin Extended-A: U+0100 to U+017F
    if (cp >= 0x0100 && cp <= 0x017F) return true;

    // Latin Extended-B: U+0180 to U+024F
    if (cp >= 0x0180 && cp <= 0x024F) return true;

    // Latin Extended Additional: U+1E00 to U+1EFF (Vietnamese diacritics)
    if (cp >= 0x1E00 && cp <= 0x1EFF) return true;

    return false;
}

bool CaptionEngineBase::isVietnamese(uint32_t cp) {
    // Vietnamese uses Latin with extensive diacritics
    // Latin Extended Additional: U+1E00 to U+1EFF
    if (cp >= 0x1E00 && cp <= 0x1EFF) return true;

    // Also includes basic Latin and Latin-1 accents
    return isLatin(cp);
}

bool CaptionEngineBase::isPrintable(uint32_t cp) {
    // Exclude control characters and whitespace
    if (cp < 0x21) return false;  // Control chars and space
    if (cp == 0x7F) return false;  // DEL
    if (cp >= 0x80 && cp <= 0x9F) return false;  // C1 control chars

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Punctuation Detection
// ═══════════════════════════════════════════════════════════════════════════

bool CaptionEngineBase::isSentenceEndPunctuation(const std::string& ch) {
    // ASCII sentence enders
    if (ch == "." || ch == "!" || ch == "?") return true;

    // Fullwidth (CJK)
    if (ch == "。" || ch == "！" || ch == "？") return true;

    // Ellipsis
    if (ch == "…" || ch == "\xE2\x80\xA6") return true;  // U+2026

    // Arabic question mark
    if (ch == "؟" || ch == "\xD8\x9F") return true;  // U+061F

    // Hindi/Devanagari danda
    if (ch == "।" || ch == "\xE0\xA5\xA4") return true;  // U+0964
    if (ch == "॥" || ch == "\xE0\xA5\xA5") return true;  // U+0965 Double danda

    // Thai sentence enders
    if (ch == "ฯ" || ch == "\xE0\xB8\xAF") return true;  // U+0E2F Thai character paiyannoi

    return false;
}

bool CaptionEngineBase::isPunctuation(const std::string& ch) {
    if (ch.empty()) return false;

    uint32_t cp = utf8ToCodepoint(ch);

    // ASCII punctuation
    if ((cp >= 0x21 && cp <= 0x2F) ||  // !"#$%&'()*+,-./
        (cp >= 0x3A && cp <= 0x40) ||  // :;<=>?@
        (cp >= 0x5B && cp <= 0x60) ||  // [\]^_`
        (cp >= 0x7B && cp <= 0x7E)) {  // {|}~
        return true;
    }

    // CJK punctuation: U+3000 to U+303F
    if (cp >= 0x3000 && cp <= 0x303F) return true;

    // Fullwidth forms: U+FF00 to U+FF5F (punctuation subset)
    if (cp >= 0xFF01 && cp <= 0xFF0F) return true;  // ！ to ／
    if (cp >= 0xFF1A && cp <= 0xFF20) return true;  // ： to ＠
    if (cp >= 0xFF3B && cp <= 0xFF40) return true;  // ［ to ｀
    if (cp >= 0xFF5B && cp <= 0xFF65) return true;  // ｛ to ･

    // General punctuation: U+2000 to U+206F
    if (cp >= 0x2010 && cp <= 0x2027) return true;  // Hyphens, dashes, quotes
    if (cp >= 0x2030 && cp <= 0x205E) return true;  // Various punctuation

    return false;
}

bool CaptionEngineBase::isWhitespace(const std::string& ch) {
    if (ch.empty()) return false;

    // ASCII whitespace
    if (ch == " " || ch == "\t" || ch == "\n" || ch == "\r") return true;

    // Ideographic space (CJK)
    if (ch == "　" || ch == "\xE3\x80\x80") return true;  // U+3000

    uint32_t cp = utf8ToCodepoint(ch);

    // Other Unicode spaces
    if (cp >= 0x2000 && cp <= 0x200A) return true;  // Various spaces
    if (cp == 0x00A0) return true;  // Non-breaking space
    if (cp == 0x202F) return true;  // Narrow no-break space
    if (cp == 0x205F) return true;  // Medium mathematical space

    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// Default Implementations
// ═══════════════════════════════════════════════════════════════════════════

std::string CaptionEngineBase::normalize(const std::string& text) const {
    // Default: just trim whitespace
    return trim(text);
}

bool CaptionEngineBase::shouldBreakCue(const std::string& text,
                                        float duration,
                                        size_t tokenCount,
                                        const CaptionEngineOptions& options) const {
    // 1. Max duration exceeded
    if (duration > options.maxDuration) {
        return true;
    }

    // 2. Max characters exceeded (estimate using UTF-8 length)
    size_t charCount = countCharacters(text);
    if (static_cast<int>(charCount) >= options.maxCharsPerLine * options.maxLines) {
        return true;
    }

    // 3. CPS limit exceeded (if we have enough duration to measure)
    if (duration >= 0.5f && options.maxCps > 0) {
        float cps = static_cast<float>(charCount) / duration;
        if (cps > options.maxCps) {
            return true;
        }
    }

    // 4. Sentence-ending punctuation (if enabled)
    if (options.breakOnSentence && !text.empty()) {
        std::string lastChar = getLastUtf8Char(text);
        if (isSentenceEndPunctuation(lastChar)) {
            return true;
        }
    }

    return false;
}

bool CaptionEngineBase::isShortWordNoBreak(const std::string& word) const {
    auto shortWords = getShortWordsNoBreak();
    for (const auto& sw : shortWords) {
        if (word == sw) return true;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// Token Building Helpers
// ═══════════════════════════════════════════════════════════════════════════

std::vector<CaptionToken> CaptionEngineBase::tokenizeWords(
    const std::vector<Word>& words,
    const CaptionEngineOptions& options) const {

    std::vector<CaptionToken> tokens;
    tokens.reserve(words.size());

    for (size_t i = 0; i < words.size(); ++i) {
        const Word& word = words[i];

        CaptionToken token;
        token.text = word.word;
        token.start = word.start;
        token.end = word.end;
        token.confidence = word.probability;
        token.granularity = TokenGranularity::Word;
        token.sourceWordIndex = static_cast<int>(i);

        // Check for sentence-ending punctuation
        if (!word.word.empty()) {
            std::string lastChar = getLastUtf8Char(word.word);
            token.isSentenceEnd = isSentenceEndPunctuation(lastChar);
            token.isPunctuation = isPunctuation(word.word);
        }

        tokens.push_back(token);
    }

    return tokens;
}

std::vector<CaptionToken> CaptionEngineBase::splitWordToCharacters(
    const Word& word,
    int wordIndex) const {

    std::vector<CaptionToken> tokens;
    std::vector<std::string> chars = utf8ToChars(word.word);

    if (chars.empty()) {
        return tokens;
    }

    // Distribute timing evenly across characters
    float totalDuration = word.end - word.start;
    float charDuration = totalDuration / static_cast<float>(chars.size());
    float currentTime = word.start;

    for (size_t i = 0; i < chars.size(); ++i) {
        CaptionToken token;
        token.text = chars[i];
        token.start = currentTime;
        token.end = (i == chars.size() - 1) ? word.end : currentTime + charDuration;
        token.confidence = word.probability;
        token.granularity = TokenGranularity::Character;
        token.sourceWordIndex = wordIndex;

        // Check properties
        token.isSentenceEnd = isSentenceEndPunctuation(chars[i]);
        token.isPunctuation = isPunctuation(chars[i]);
        token.isNoLineStart = !canStartLine(chars[i]);
        token.isNoLineEnd = !canEndLine(chars[i]);

        tokens.push_back(token);
        currentTime = token.end;
    }

    return tokens;
}

std::vector<CaptionToken> CaptionEngineBase::splitHangulSyllables(
    const Word& word,
    int wordIndex) const {

    std::vector<CaptionToken> tokens;
    std::vector<std::string> chars = utf8ToChars(word.word);

    if (chars.empty()) {
        return tokens;
    }

    // Separate Hangul syllables from other characters
    std::vector<std::string> syllables;
    std::string prefix;

    bool inHangul = false;
    for (const auto& ch : chars) {
        uint32_t cp = utf8ToCodepoint(ch);
        if (isHangulSyllable(cp)) {
            inHangul = true;
            syllables.push_back(ch);
        } else if (!inHangul) {
            prefix += ch;
        } else {
            // Non-Hangul after Hangul - attach to last syllable
            if (!syllables.empty()) {
                syllables.back() += ch;
            }
        }
    }

    if (syllables.empty()) {
        // No Hangul - return as single word token
        CaptionToken token;
        token.text = word.word;
        token.start = word.start;
        token.end = word.end;
        token.confidence = word.probability;
        token.granularity = TokenGranularity::Word;
        token.sourceWordIndex = wordIndex;
        tokens.push_back(token);
        return tokens;
    }

    // Distribute timing across syllables
    float totalDuration = word.end - word.start;
    float syllableDuration = totalDuration / static_cast<float>(syllables.size());
    float currentTime = word.start;

    for (size_t i = 0; i < syllables.size(); ++i) {
        CaptionToken token;
        token.text = (i == 0 && !prefix.empty()) ? prefix + syllables[i] : syllables[i];
        token.start = currentTime;
        token.end = (i == syllables.size() - 1) ? word.end : currentTime + syllableDuration;
        token.confidence = word.probability;
        token.granularity = TokenGranularity::Syllable;
        token.sourceWordIndex = wordIndex;

        // Check for sentence-ending punctuation
        if (!token.text.empty()) {
            std::string lastChar = getLastUtf8Char(token.text);
            token.isSentenceEnd = isSentenceEndPunctuation(lastChar);
            token.isPunctuation = isPunctuation(lastChar);
        }

        tokens.push_back(token);
        currentTime = token.end;
    }

    return tokens;
}

// ═══════════════════════════════════════════════════════════════════════════
// Line Splitting Helpers
// ═══════════════════════════════════════════════════════════════════════════

std::string CaptionEngineBase::splitLinesAtWords(const std::string& text,
                                                  int maxChars,
                                                  int maxLines) const {
    if (static_cast<int>(countCharacters(text)) <= maxChars) {
        return text;
    }

    std::vector<std::string> lines;
    std::string currentLine;
    size_t currentCount = 0;

    // Split into words
    std::vector<std::string> words;
    std::string currentWord;

    for (size_t i = 0; i < text.size(); ) {
        size_t charLen = utf8CharLength(static_cast<unsigned char>(text[i]));
        std::string ch = text.substr(i, charLen);

        if (isWhitespace(ch)) {
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

    // Build lines
    for (size_t i = 0; i < words.size() && static_cast<int>(lines.size()) < maxLines; ++i) {
        const std::string& word = words[i];
        size_t wordLen = countCharacters(word);

        size_t newCount = currentCount + wordLen;
        if (!currentLine.empty()) {
            newCount += 1;  // Space
        }

        bool shouldBreak = (newCount > static_cast<size_t>(maxChars) && !currentLine.empty());

        // Check short word rules
        if (shouldBreak && isShortWordNoBreak(word)) {
            // Don't break if this word is a short preposition
            // Check if we can move the last word to the new line
            size_t lastSpace = currentLine.rfind(' ');
            if (lastSpace != std::string::npos) {
                std::string lastWord = currentLine.substr(lastSpace + 1);
                if (isShortWordNoBreak(lastWord)) {
                    // Move last word to next line with this word
                    currentLine = currentLine.substr(0, lastSpace);
                    lines.push_back(currentLine);
                    currentLine = lastWord + " " + word;
                    currentCount = countCharacters(currentLine);
                    continue;
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

std::string CaptionEngineBase::splitLinesAtCharacters(const std::string& text,
                                                       int maxChars,
                                                       int maxLines) const {
    if (static_cast<int>(countCharacters(text)) <= maxChars) {
        return text;
    }

    std::vector<std::string> chars = utf8ToChars(text);
    std::vector<std::string> lines;
    std::string currentLine;
    int currentCount = 0;

    for (size_t i = 0; i < chars.size() && static_cast<int>(lines.size()) < maxLines; ++i) {
        const std::string& ch = chars[i];

        // Check if we can break here
        bool canBreakHere = true;
        if (i + 1 < chars.size()) {
            // Check canEndLine for current char
            if (!canEndLine(ch)) {
                canBreakHere = false;
            }
            // Check canStartLine for next char
            if (!canStartLine(chars[i + 1])) {
                canBreakHere = false;
            }
        }

        // Count character (skip spaces for CJK)
        if (!isWhitespace(ch)) {
            currentCount++;
        }

        currentLine += ch;

        // Check if we should break
        if (currentCount >= maxChars && canBreakHere) {
            lines.push_back(currentLine);
            currentLine.clear();
            currentCount = 0;
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

// ═══════════════════════════════════════════════════════════════════════════
// Generic Cue Builder (Template Method Pattern)
// ═══════════════════════════════════════════════════════════════════════════

std::vector<CaptionCue> CaptionEngineBase::buildCues(
    const std::vector<CaptionToken>& tokens,
    const CaptionEngineOptions& options) const {

    std::vector<CaptionCue> cues;

    if (tokens.empty()) {
        return cues;
    }

    int cueIndex = 1;
    std::string currentText;
    std::vector<CaptionToken> currentTokens;
    float cueStart = -1.0f;
    float cueEnd = 0.0f;
    float lastTokenEnd = 0.0f;

    float pauseThresholdS = options.presentation.pauseBreakThresholdMs / 1000.0f;

    auto flushCue = [&]() {
        if (currentText.empty() || cueStart < 0) {
            return;
        }

        // Enforce minimum duration
        float duration = cueEnd - cueStart;
        if (duration < options.minDuration) {
            cueEnd = cueStart + options.minDuration;
        }

        CaptionCue cue;
        cue.index = cueIndex++;
        cue.start = cueStart;
        cue.end = cueEnd;
        cue.tokens = currentTokens;

        // Split text into lines if needed
        cue.text = splitLines(currentText, options.maxCharsPerLine, options.maxLines);

        // Split into lines array
        std::istringstream stream(cue.text);
        std::string line;
        while (std::getline(stream, line)) {
            cue.lines.push_back(line);
        }
        cue.lineCount = static_cast<int>(cue.lines.size());

        // Calculate metrics
        cue.characterCount = static_cast<int>(countCharacters(currentText));
        cue.cps = (duration > 0) ? static_cast<float>(cue.characterCount) / duration : 0.0f;

        cues.push_back(cue);

        // Reset
        currentText.clear();
        currentTokens.clear();
        cueStart = -1.0f;
    };

    for (size_t i = 0; i < tokens.size(); ++i) {
        const CaptionToken& token = tokens[i];

        // Check for pause-based break
        if (options.breakOnPause && cueStart >= 0 && !currentText.empty()) {
            float pause = token.start - lastTokenEnd;
            if (pause > pauseThresholdS) {
                flushCue();
            }
        }

        // Start new cue if needed
        if (cueStart < 0) {
            cueStart = token.start;
        }

        // Add token to current cue
        // Add space/separator based on granularity
        if (!currentText.empty() && token.granularity == TokenGranularity::Word) {
            // Word-based: add space unless token is punctuation
            if (!token.isPunctuation) {
                currentText += " ";
            }
        }
        currentText += token.text;
        currentTokens.push_back(token);
        cueEnd = token.end;
        lastTokenEnd = token.end;

        // Check if we should break this cue
        float duration = cueEnd - cueStart;
        if (shouldBreakCue(currentText, duration, currentTokens.size(), options)) {
            flushCue();
        }
    }

    // Flush any remaining content
    flushCue();

    return cues;
}

// ═══════════════════════════════════════════════════════════════════════════
// Default Presentation Planning (AAA Karaoke Timing)
// ═══════════════════════════════════════════════════════════════════════════

std::vector<PresentationEvent> CaptionEngineBase::planPresentation(
    const CaptionCue& cue,
    const PresentationOptions& options) const {

    std::vector<PresentationEvent> events;

    if (cue.tokens.empty()) {
        return events;
    }

    int cueIdx = cue.index - 1;  // Convert to 0-based for event storage

    // Lead-in: Show cue before first token
    float leadInS = options.phraseLeadInMs / 1000.0f;
    float showTime = std::max(0.0f, cue.tokens.front().start - leadInS);

    PresentationEvent showEvent(PresentationEventType::CueShow, showTime, cueIdx);
    events.push_back(showEvent);

    // Token events
    float tokenRevealS = options.tokenRevealMs / 1000.0f;

    for (size_t i = 0; i < cue.tokens.size(); ++i) {
        const CaptionToken& token = cue.tokens[i];
        int tokenIdx = static_cast<int>(i);

        // Token reveal (slightly before highlight)
        float revealTime = std::max(showTime, token.start - tokenRevealS);
        PresentationEvent revealEvent(PresentationEventType::TokenReveal, revealTime, cueIdx, tokenIdx);
        events.push_back(revealEvent);

        // Token highlight on (at token start)
        PresentationEvent highlightEvent(PresentationEventType::TokenHighlight, token.start, cueIdx, tokenIdx);
        events.push_back(highlightEvent);

        // Token dim (at token end)
        PresentationEvent dimEvent(PresentationEventType::TokenDim, token.end, cueIdx, tokenIdx);
        events.push_back(dimEvent);
    }

    // Hold: Keep cue visible after last token
    float lastTokenEnd = cue.tokens.back().end;
    float holdTime = lastTokenEnd + options.minHoldAfterSpeechS;

    PresentationEvent holdEvent(PresentationEventType::CueHold, holdTime, cueIdx);
    events.push_back(holdEvent);

    // Fade out
    float fadeOutS = options.fadeOutMs / 1000.0f;
    float fadeTime = holdTime + 0.1f;  // Small delay after hold

    PresentationEvent fadeEvent(PresentationEventType::CueFadeOut, fadeTime, cueIdx);
    fadeEvent.duration = fadeOutS;
    events.push_back(fadeEvent);

    // Clear
    float clearTime = fadeTime + fadeOutS;
    PresentationEvent clearEvent(PresentationEventType::CueClear, clearTime, cueIdx);
    events.push_back(clearEvent);

    // Sort events by time
    std::sort(events.begin(), events.end());

    return events;
}

} // namespace muninn
