#include "muninn/engines/latin_engine.h"
#include <algorithm>
#include <cctype>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// Common Latin character counting (shared by all Latin engines)
// ═══════════════════════════════════════════════════════════════════════════

static size_t countLatinChars(const std::string& text) {
    size_t count = 0;
    auto chars = CaptionEngineBase::utf8ToChars(text);

    for (const auto& ch : chars) {
        // Skip spaces and newlines
        if (ch == " " || ch == "\n" || ch == "\r" || ch == "\t") {
            continue;
        }

        uint32_t cp = CaptionEngineBase::utf8ToCodepoint(ch);

        // Count printable ASCII
        if (cp >= 0x21 && cp <= 0x7E) {
            count++;
            continue;
        }

        // Count Latin-1 Supplement (accented characters)
        if (cp >= 0x00A1 && cp <= 0x00FF) {
            count++;
            continue;
        }

        // Count Latin Extended (more accented chars, Vietnamese diacritics)
        if (cp >= 0x0100 && cp <= 0x024F) {
            count++;
            continue;
        }

        // Count Latin Extended Additional (Vietnamese)
        if (cp >= 0x1E00 && cp <= 0x1EFF) {
            count++;
            continue;
        }
    }

    return count;
}

static bool endsWithLatinSentenceEnd(const std::string& text) {
    if (text.empty()) return false;

    // Trim trailing whitespace
    std::string trimmed = text;
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\n')) {
        trimmed.pop_back();
    }

    if (trimmed.empty()) return false;

    char last = trimmed.back();
    return last == '.' || last == '!' || last == '?';
}

// ═══════════════════════════════════════════════════════════════════════════
// English Caption Engine
// ═══════════════════════════════════════════════════════════════════════════

int EnglishCaptionEngine::supports(const std::string& langCode, const std::string& script) const {
    if (langCode == "en" || langCode == "eng" ||
        langCode == "en-US" || langCode == "en-GB" || langCode == "en-AU") {
        return 100;
    }
    if (script == "Latn" && langCode.empty()) {
        return 30;  // Low priority fallback for generic Latin
    }
    return 0;
}

std::vector<std::string> EnglishCaptionEngine::supportedLanguages() const {
    return {"en", "en-US", "en-GB"};
}

std::vector<CaptionToken> EnglishCaptionEngine::tokenize(
    const std::vector<Word>& words,
    const CaptionEngineOptions& options) const {

    return tokenizeWords(words, options);
}

size_t EnglishCaptionEngine::countCharacters(const std::string& text) const {
    return countLatinChars(text);
}

bool EnglishCaptionEngine::isSentenceEnd(const std::string& text) const {
    return endsWithLatinSentenceEnd(text);
}

std::string EnglishCaptionEngine::splitLines(const std::string& text,
                                              int maxChars,
                                              int maxLines) const {
    return splitLinesAtWords(text, maxChars, maxLines);
}

// ═══════════════════════════════════════════════════════════════════════════
// Spanish Caption Engine
// ═══════════════════════════════════════════════════════════════════════════

int SpanishCaptionEngine::supports(const std::string& langCode, const std::string& script) const {
    if (langCode == "es" || langCode == "spa" ||
        langCode == "es-ES" || langCode == "es-MX" || langCode == "es-AR") {
        return 100;
    }
    return 0;
}

std::vector<std::string> SpanishCaptionEngine::supportedLanguages() const {
    return {"es", "es-ES", "es-MX"};
}

std::vector<CaptionToken> SpanishCaptionEngine::tokenize(
    const std::vector<Word>& words,
    const CaptionEngineOptions& options) const {

    return tokenizeWords(words, options);
}

size_t SpanishCaptionEngine::countCharacters(const std::string& text) const {
    return countLatinChars(text);
}

bool SpanishCaptionEngine::isSentenceEnd(const std::string& text) const {
    if (text.empty()) return false;

    std::string lastChar = getLastUtf8Char(text);

    // Spanish sentence-ending punctuation (including inverted marks at start)
    return lastChar == "." || lastChar == "!" || lastChar == "?" ||
           lastChar == "¿" || lastChar == "¡";
}

std::string SpanishCaptionEngine::splitLines(const std::string& text,
                                              int maxChars,
                                              int maxLines) const {
    return splitLinesAtWords(text, maxChars, maxLines);
}

std::vector<std::string> SpanishCaptionEngine::getShortWordsNoBreak() const {
    // Spanish short words that shouldn't end a line
    return {
        // Single letters
        "a", "e", "o", "u", "y",
        // Short articles
        "el", "la", "lo", "al",
        // Short prepositions
        "de", "en", "a",
        // Short conjunctions
        "o", "u", "y", "e"
    };
}

// ═══════════════════════════════════════════════════════════════════════════
// Portuguese Caption Engine
// ═══════════════════════════════════════════════════════════════════════════

int PortugueseCaptionEngine::supports(const std::string& langCode, const std::string& script) const {
    if (langCode == "pt" || langCode == "por" ||
        langCode == "pt-BR" || langCode == "pt-PT") {
        return 100;
    }
    return 0;
}

std::vector<std::string> PortugueseCaptionEngine::supportedLanguages() const {
    return {"pt", "pt-BR", "pt-PT"};
}

std::vector<CaptionToken> PortugueseCaptionEngine::tokenize(
    const std::vector<Word>& words,
    const CaptionEngineOptions& options) const {

    return tokenizeWords(words, options);
}

size_t PortugueseCaptionEngine::countCharacters(const std::string& text) const {
    return countLatinChars(text);
}

bool PortugueseCaptionEngine::isSentenceEnd(const std::string& text) const {
    return endsWithLatinSentenceEnd(text);
}

std::string PortugueseCaptionEngine::splitLines(const std::string& text,
                                                 int maxChars,
                                                 int maxLines) const {
    return splitLinesAtWords(text, maxChars, maxLines);
}

std::vector<std::string> PortugueseCaptionEngine::getShortWordsNoBreak() const {
    // Portuguese short words that shouldn't end a line
    return {
        // Single letters
        "a", "e", "o", "u",
        // Short articles
        "o", "a", "os", "as", "um", "uma",
        // Short prepositions
        "de", "da", "do", "das", "dos",
        "em", "na", "no", "nas", "nos",
        // Short conjunctions
        "e", "ou"
    };
}

// ═══════════════════════════════════════════════════════════════════════════
// Indonesian Caption Engine
// ═══════════════════════════════════════════════════════════════════════════

int IndonesianCaptionEngine::supports(const std::string& langCode, const std::string& script) const {
    if (langCode == "id" || langCode == "ind" || langCode == "ms" || langCode == "msa") {
        return 100;
    }
    return 0;
}

std::vector<std::string> IndonesianCaptionEngine::supportedLanguages() const {
    return {"id", "ms"};
}

std::vector<CaptionToken> IndonesianCaptionEngine::tokenize(
    const std::vector<Word>& words,
    const CaptionEngineOptions& options) const {

    return tokenizeWords(words, options);
}

size_t IndonesianCaptionEngine::countCharacters(const std::string& text) const {
    return countLatinChars(text);
}

bool IndonesianCaptionEngine::isSentenceEnd(const std::string& text) const {
    return endsWithLatinSentenceEnd(text);
}

std::string IndonesianCaptionEngine::splitLines(const std::string& text,
                                                 int maxChars,
                                                 int maxLines) const {
    return splitLinesAtWords(text, maxChars, maxLines);
}

// ═══════════════════════════════════════════════════════════════════════════
// Vietnamese Caption Engine
// ═══════════════════════════════════════════════════════════════════════════

int VietnameseCaptionEngine::supports(const std::string& langCode, const std::string& script) const {
    if (langCode == "vi" || langCode == "vie") {
        return 100;
    }
    return 0;
}

std::vector<std::string> VietnameseCaptionEngine::supportedLanguages() const {
    return {"vi"};
}

std::vector<CaptionToken> VietnameseCaptionEngine::tokenize(
    const std::vector<Word>& words,
    const CaptionEngineOptions& options) const {

    return tokenizeWords(words, options);
}

size_t VietnameseCaptionEngine::countCharacters(const std::string& text) const {
    return countLatinChars(text);
}

bool VietnameseCaptionEngine::isSentenceEnd(const std::string& text) const {
    return endsWithLatinSentenceEnd(text);
}

std::string VietnameseCaptionEngine::splitLines(const std::string& text,
                                                 int maxChars,
                                                 int maxLines) const {
    return splitLinesAtWords(text, maxChars, maxLines);
}

} // namespace muninn
