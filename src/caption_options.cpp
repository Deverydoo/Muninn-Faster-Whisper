#include "muninn/caption_options.h"
#include <unordered_map>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// CaptionEngineOptions::merge
// ═══════════════════════════════════════════════════════════════════════════

void CaptionEngineOptions::merge(const CaptionEngineOverrides& overrides) {
    // Every value the caller set wins, whatever it is. Unset values keep the
    // preset's. (The old version compared against the defaults, so asking for
    // exactly 42 characters or 2 lines on a preset with other values did
    // nothing, and the booleans always overwrote the preset.)
    if (!overrides.languageHint.empty()) languageHint = overrides.languageHint;
    if (!overrides.scriptHint.empty()) scriptHint = overrides.scriptHint;

    if (overrides.maxCharsPerLine) maxCharsPerLine = *overrides.maxCharsPerLine;
    if (overrides.maxLines) maxLines = *overrides.maxLines;
    if (overrides.maxDuration) maxDuration = *overrides.maxDuration;
    if (overrides.minDuration) minDuration = *overrides.minDuration;
    if (overrides.maxCps) maxCps = *overrides.maxCps;
    if (overrides.breakOnSentence) breakOnSentence = *overrides.breakOnSentence;
    if (overrides.breakOnPause) breakOnPause = *overrides.breakOnPause;
    if (overrides.avoidOrphanWords) avoidOrphanWords = *overrides.avoidOrphanWords;
    if (overrides.balanceLines) balanceLines = *overrides.balanceLines;
    if (overrides.generatePresentationEvents) {
        generatePresentationEvents = *overrides.generatePresentationEvents;
    }

    if (overrides.phraseLeadInMs) presentation.phraseLeadInMs = *overrides.phraseLeadInMs;
    if (overrides.tokenRevealMs) presentation.tokenRevealMs = *overrides.tokenRevealMs;
    if (overrides.minHoldAfterSpeechS) presentation.minHoldAfterSpeechS = *overrides.minHoldAfterSpeechS;
    if (overrides.fadeOutMs) presentation.fadeOutMs = *overrides.fadeOutMs;
    if (overrides.pauseBreakThresholdMs) {
        presentation.pauseBreakThresholdMs = *overrides.pauseBreakThresholdMs;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Language Presets
// ═══════════════════════════════════════════════════════════════════════════

namespace CaptionPresets {

CaptionEngineOptions japanese() {
    CaptionEngineOptions opts;
    opts.languageHint = "ja";
    opts.scriptHint = "Jpan";
    opts.maxCharsPerLine = 22;
    opts.maxLines = 2;
    opts.maxDuration = 7.0f;
    opts.minDuration = 1.0f;
    opts.maxCps = 11.0f;  // Lower CPS for character-based
    opts.breakOnSentence = true;
    opts.breakOnPause = true;
    return opts;
}

CaptionEngineOptions korean() {
    CaptionEngineOptions opts;
    opts.languageHint = "ko";
    opts.scriptHint = "Kore";
    opts.maxCharsPerLine = 23;
    opts.maxLines = 2;
    opts.maxDuration = 7.0f;
    opts.minDuration = 1.0f;
    opts.maxCps = 13.0f;
    opts.breakOnSentence = true;
    opts.breakOnPause = true;
    return opts;
}

CaptionEngineOptions chineseSimplified() {
    CaptionEngineOptions opts;
    opts.languageHint = "zh-Hans";
    opts.scriptHint = "Hans";
    opts.maxCharsPerLine = 18;
    opts.maxLines = 2;
    opts.maxDuration = 7.0f;
    opts.minDuration = 1.0f;
    opts.maxCps = 11.0f;  // 9-11 CPS for Chinese
    opts.breakOnSentence = true;
    opts.breakOnPause = true;
    return opts;
}

CaptionEngineOptions chineseTraditional() {
    CaptionEngineOptions opts;
    opts.languageHint = "zh-Hant";
    opts.scriptHint = "Hant";
    opts.maxCharsPerLine = 16;
    opts.maxLines = 2;
    opts.maxDuration = 7.0f;
    opts.minDuration = 1.0f;
    opts.maxCps = 11.0f;
    opts.breakOnSentence = true;
    opts.breakOnPause = true;
    return opts;
}

CaptionEngineOptions russian() {
    CaptionEngineOptions opts;
    opts.languageHint = "ru";
    opts.scriptHint = "Cyrl";
    opts.maxCharsPerLine = 40;
    opts.maxLines = 2;
    opts.maxDuration = 7.0f;
    opts.minDuration = 1.0f;
    opts.maxCps = 17.0f;  // 14-17 CPS
    opts.breakOnSentence = true;
    opts.breakOnPause = true;
    opts.avoidOrphanWords = true;
    return opts;
}

CaptionEngineOptions arabic() {
    CaptionEngineOptions opts;
    opts.languageHint = "ar";
    opts.scriptHint = "Arab";
    opts.maxCharsPerLine = 42;
    opts.maxLines = 2;
    opts.maxDuration = 7.0f;
    opts.minDuration = 1.0f;
    opts.maxCps = 15.0f;  // 12-15 CPS (slower for RTL)
    opts.breakOnSentence = true;
    opts.breakOnPause = true;
    return opts;
}

CaptionEngineOptions hindi() {
    CaptionEngineOptions opts;
    opts.languageHint = "hi";
    opts.scriptHint = "Deva";
    opts.maxCharsPerLine = 42;
    opts.maxLines = 2;
    opts.maxDuration = 7.0f;
    opts.minDuration = 1.0f;
    opts.maxCps = 16.0f;  // 14-16 CPS
    opts.breakOnSentence = true;
    opts.breakOnPause = true;
    return opts;
}

CaptionEngineOptions english() {
    CaptionEngineOptions opts;
    opts.languageHint = "en";
    opts.scriptHint = "Latn";
    opts.maxCharsPerLine = 42;
    opts.maxLines = 2;
    opts.maxDuration = 7.0f;
    opts.minDuration = 1.0f;
    opts.maxCps = 17.0f;
    opts.breakOnSentence = true;
    opts.breakOnPause = true;
    opts.avoidOrphanWords = true;
    return opts;
}

CaptionEngineOptions spanish() {
    CaptionEngineOptions opts;
    opts.languageHint = "es";
    opts.scriptHint = "Latn";
    opts.maxCharsPerLine = 42;
    opts.maxLines = 2;
    opts.maxDuration = 7.0f;
    opts.minDuration = 1.0f;
    opts.maxCps = 17.0f;  // 15-17 CPS
    opts.breakOnSentence = true;
    opts.breakOnPause = true;
    opts.avoidOrphanWords = true;
    return opts;
}

CaptionEngineOptions portuguese() {
    CaptionEngineOptions opts;
    opts.languageHint = "pt";
    opts.scriptHint = "Latn";
    opts.maxCharsPerLine = 42;
    opts.maxLines = 2;
    opts.maxDuration = 7.0f;
    opts.minDuration = 1.0f;
    opts.maxCps = 17.0f;  // 15-17 CPS
    opts.breakOnSentence = true;
    opts.breakOnPause = true;
    opts.avoidOrphanWords = true;
    return opts;
}

CaptionEngineOptions indonesian() {
    CaptionEngineOptions opts;
    opts.languageHint = "id";
    opts.scriptHint = "Latn";
    opts.maxCharsPerLine = 42;
    opts.maxLines = 2;
    opts.maxDuration = 7.0f;
    opts.minDuration = 1.0f;
    opts.maxCps = 17.0f;
    opts.breakOnSentence = true;
    opts.breakOnPause = true;
    return opts;
}

CaptionEngineOptions vietnamese() {
    CaptionEngineOptions opts;
    opts.languageHint = "vi";
    opts.scriptHint = "Latn";
    opts.maxCharsPerLine = 42;
    opts.maxLines = 2;
    opts.maxDuration = 7.0f;
    opts.minDuration = 1.0f;
    opts.maxCps = 17.0f;
    opts.breakOnSentence = true;
    opts.breakOnPause = true;
    return opts;
}

CaptionEngineOptions thai() {
    CaptionEngineOptions opts;
    opts.languageHint = "th";
    opts.scriptHint = "Thai";
    opts.maxCharsPerLine = 40;
    opts.maxLines = 2;
    opts.maxDuration = 7.0f;
    opts.minDuration = 1.0f;
    opts.maxCps = 14.0f;  // 12-14 CPS
    opts.breakOnSentence = true;
    opts.breakOnPause = true;
    return opts;
}

CaptionEngineOptions forLanguage(const std::string& langCode) {
    // Map language codes to presets
    static const std::unordered_map<std::string, CaptionEngineOptions(*)()> presetMap = {
        // CJK
        {"ja", japanese},
        {"ko", korean},
        {"zh", chineseSimplified},
        {"zh-CN", chineseSimplified},
        {"zh-Hans", chineseSimplified},
        {"zh-TW", chineseTraditional},
        {"zh-HK", chineseTraditional},
        {"zh-Hant", chineseTraditional},

        // Cyrillic
        {"ru", russian},

        // RTL
        {"ar", arabic},

        // Indic
        {"hi", hindi},

        // Thai
        {"th", thai},

        // Latin
        {"en", english},
        {"en-US", english},
        {"en-GB", english},
        {"es", spanish},
        {"es-ES", spanish},
        {"es-MX", spanish},
        {"pt", portuguese},
        {"pt-BR", portuguese},
        {"pt-PT", portuguese},
        {"id", indonesian},
        {"vi", vietnamese},
    };

    auto it = presetMap.find(langCode);
    if (it != presetMap.end()) {
        return it->second();
    }

    // Try base language code (e.g., "en-US" -> "en")
    size_t dashPos = langCode.find('-');
    if (dashPos != std::string::npos) {
        std::string baseLang = langCode.substr(0, dashPos);
        it = presetMap.find(baseLang);
        if (it != presetMap.end()) {
            return it->second();
        }
    }

    // Default to English
    return english();
}

std::vector<std::string> supportedLanguages() {
    return {
        // CJK
        "ja", "ko", "zh", "zh-CN", "zh-TW", "zh-Hans", "zh-Hant",
        // Cyrillic
        "ru",
        // RTL
        "ar",
        // Indic
        "hi",
        // Thai
        "th",
        // Latin
        "en", "es", "pt", "id", "vi"
    };
}

bool isLanguageSupported(const std::string& langCode) {
    auto langs = supportedLanguages();
    for (const auto& lang : langs) {
        if (lang == langCode) return true;
    }

    // Check base language
    size_t dashPos = langCode.find('-');
    if (dashPos != std::string::npos) {
        std::string baseLang = langCode.substr(0, dashPos);
        for (const auto& lang : langs) {
            if (lang == baseLang) return true;
        }
    }

    return false;
}

} // namespace CaptionPresets

} // namespace muninn
