#include "muninn/caption_types.h"
#include <unordered_map>
#include <algorithm>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// Script Family Detection
// ═══════════════════════════════════════════════════════════════════════════

ScriptFamily getScriptFamily(const std::string& langCode) {
    // Map ISO 639-1 language codes to script families
    static const std::unordered_map<std::string, ScriptFamily> langToFamily = {
        // CJK
        {"ja", ScriptFamily::CJK},
        {"ko", ScriptFamily::CJK},
        {"zh", ScriptFamily::CJK},
        {"zh-CN", ScriptFamily::CJK},
        {"zh-TW", ScriptFamily::CJK},
        {"zh-Hans", ScriptFamily::CJK},
        {"zh-Hant", ScriptFamily::CJK},

        // Cyrillic
        {"ru", ScriptFamily::Cyrillic},
        {"uk", ScriptFamily::Cyrillic},
        {"bg", ScriptFamily::Cyrillic},
        {"sr", ScriptFamily::Cyrillic},
        {"mk", ScriptFamily::Cyrillic},
        {"be", ScriptFamily::Cyrillic},

        // Arabic (RTL)
        {"ar", ScriptFamily::Arabic},
        {"fa", ScriptFamily::Arabic},  // Persian/Farsi
        {"ur", ScriptFamily::Arabic},  // Urdu

        // Hebrew (RTL)
        {"he", ScriptFamily::Hebrew},
        {"yi", ScriptFamily::Hebrew},  // Yiddish

        // Devanagari
        {"hi", ScriptFamily::Devanagari},
        {"mr", ScriptFamily::Devanagari},  // Marathi
        {"ne", ScriptFamily::Devanagari},  // Nepali
        {"sa", ScriptFamily::Devanagari},  // Sanskrit

        // Thai
        {"th", ScriptFamily::Thai},

        // Latin (default for most European languages)
        {"en", ScriptFamily::Latin},
        {"es", ScriptFamily::Latin},
        {"pt", ScriptFamily::Latin},
        {"fr", ScriptFamily::Latin},
        {"de", ScriptFamily::Latin},
        {"it", ScriptFamily::Latin},
        {"nl", ScriptFamily::Latin},
        {"pl", ScriptFamily::Latin},
        {"tr", ScriptFamily::Latin},
        {"vi", ScriptFamily::Latin},
        {"id", ScriptFamily::Latin},
        {"ms", ScriptFamily::Latin},
        {"tl", ScriptFamily::Latin},  // Tagalog/Filipino
        {"sw", ScriptFamily::Latin},  // Swahili
        {"ro", ScriptFamily::Latin},
        {"hu", ScriptFamily::Latin},
        {"cs", ScriptFamily::Latin},
        {"sv", ScriptFamily::Latin},
        {"da", ScriptFamily::Latin},
        {"no", ScriptFamily::Latin},
        {"fi", ScriptFamily::Latin},
    };

    auto it = langToFamily.find(langCode);
    if (it != langToFamily.end()) {
        return it->second;
    }

    // Try without region suffix (e.g., "en-US" -> "en")
    size_t dashPos = langCode.find('-');
    if (dashPos != std::string::npos) {
        std::string baseLang = langCode.substr(0, dashPos);
        it = langToFamily.find(baseLang);
        if (it != langToFamily.end()) {
            return it->second;
        }
    }

    return ScriptFamily::Unknown;
}

ScriptFamily getScriptFamilyFromScript(const std::string& scriptCode) {
    // Map ISO 15924 script codes to families
    static const std::unordered_map<std::string, ScriptFamily> scriptToFamily = {
        // CJK scripts
        {"Jpan", ScriptFamily::CJK},   // Japanese
        {"Kore", ScriptFamily::CJK},   // Korean
        {"Hang", ScriptFamily::CJK},   // Hangul
        {"Hani", ScriptFamily::CJK},   // Han
        {"Hans", ScriptFamily::CJK},   // Simplified Chinese
        {"Hant", ScriptFamily::CJK},   // Traditional Chinese
        {"Hira", ScriptFamily::CJK},   // Hiragana
        {"Kana", ScriptFamily::CJK},   // Katakana

        // Cyrillic
        {"Cyrl", ScriptFamily::Cyrillic},

        // Arabic
        {"Arab", ScriptFamily::Arabic},

        // Hebrew
        {"Hebr", ScriptFamily::Hebrew},

        // Devanagari
        {"Deva", ScriptFamily::Devanagari},

        // Thai
        {"Thai", ScriptFamily::Thai},

        // Latin
        {"Latn", ScriptFamily::Latin},
    };

    auto it = scriptToFamily.find(scriptCode);
    if (it != scriptToFamily.end()) {
        return it->second;
    }

    return ScriptFamily::Unknown;
}

// ═══════════════════════════════════════════════════════════════════════════
// Token Granularity
// ═══════════════════════════════════════════════════════════════════════════

TokenGranularity getDefaultGranularity(const std::string& langCode) {
    // Character-based languages
    if (langCode == "zh" || langCode == "zh-CN" || langCode == "zh-TW" ||
        langCode == "zh-Hans" || langCode == "zh-Hant") {
        return TokenGranularity::Character;
    }

    // Thai (no word spaces)
    if (langCode == "th") {
        return TokenGranularity::Character;
    }

    // Japanese (mora-based for karaoke, character for display)
    if (langCode == "ja") {
        return TokenGranularity::Character;  // Could be Mora for precise karaoke
    }

    // Korean (syllable-based)
    if (langCode == "ko") {
        return TokenGranularity::Syllable;
    }

    // Default: word-based
    return TokenGranularity::Word;
}

// ═══════════════════════════════════════════════════════════════════════════
// RTL Detection
// ═══════════════════════════════════════════════════════════════════════════

bool isRtlScript(ScriptFamily family) {
    return family == ScriptFamily::Arabic || family == ScriptFamily::Hebrew;
}

// ═══════════════════════════════════════════════════════════════════════════
// String Conversions
// ═══════════════════════════════════════════════════════════════════════════

std::string tokenGranularityToString(TokenGranularity g) {
    switch (g) {
        case TokenGranularity::Word:      return "Word";
        case TokenGranularity::Character: return "Character";
        case TokenGranularity::Syllable:  return "Syllable";
        case TokenGranularity::Mora:      return "Mora";
        default:                          return "Unknown";
    }
}

std::string scriptFamilyToString(ScriptFamily f) {
    switch (f) {
        case ScriptFamily::Latin:      return "Latin";
        case ScriptFamily::CJK:        return "CJK";
        case ScriptFamily::Cyrillic:   return "Cyrillic";
        case ScriptFamily::Arabic:     return "Arabic";
        case ScriptFamily::Devanagari: return "Devanagari";
        case ScriptFamily::Thai:       return "Thai";
        case ScriptFamily::Hebrew:     return "Hebrew";
        case ScriptFamily::Unknown:    return "Unknown";
        default:                       return "Unknown";
    }
}

std::string presentationEventTypeToString(PresentationEventType t) {
    switch (t) {
        case PresentationEventType::CueShow:        return "CueShow";
        case PresentationEventType::TokenReveal:    return "TokenReveal";
        case PresentationEventType::TokenHighlight: return "TokenHighlight";
        case PresentationEventType::TokenDim:       return "TokenDim";
        case PresentationEventType::CueHold:        return "CueHold";
        case PresentationEventType::CueFadeOut:     return "CueFadeOut";
        case PresentationEventType::CueClear:       return "CueClear";
        default:                                    return "Unknown";
    }
}

} // namespace muninn
