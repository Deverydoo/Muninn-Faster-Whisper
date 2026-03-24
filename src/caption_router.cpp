#include "muninn/caption_router.h"
#include "muninn/engines/cjk_engine.h"
#include "muninn/engines/latin_engine.h"
#include "muninn/engines/cyrillic_engine.h"
#include "muninn/engines/rtl_engine.h"
#include "muninn/engines/indic_engine.h"
#include "muninn/engines/thai_engine.h"
#include <algorithm>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// Singleton Instance
// ═══════════════════════════════════════════════════════════════════════════

CaptionRouter& CaptionRouter::instance() {
    static CaptionRouter instance;
    return instance;
}

// ═══════════════════════════════════════════════════════════════════════════
// Engine Registration
// ═══════════════════════════════════════════════════════════════════════════

void CaptionRouter::registerEngine(std::unique_ptr<ICaptionEngine> engine) {
    if (engine) {
        engines_.push_back(std::move(engine));
    }
}

void CaptionRouter::registerDefaultEngines() {
    // Clear existing engines
    engines_.clear();

    // CJK Family
    registerEngine(std::make_unique<JapaneseCaptionEngine>());
    registerEngine(std::make_unique<KoreanCaptionEngine>());
    registerEngine(std::make_unique<ChineseCaptionEngine>(ChineseCaptionEngine::Variant::Simplified));
    registerEngine(std::make_unique<ChineseCaptionEngine>(ChineseCaptionEngine::Variant::Traditional));

    // Latin Family
    registerEngine(std::make_unique<EnglishCaptionEngine>());
    registerEngine(std::make_unique<SpanishCaptionEngine>());
    registerEngine(std::make_unique<PortugueseCaptionEngine>());
    registerEngine(std::make_unique<IndonesianCaptionEngine>());
    registerEngine(std::make_unique<VietnameseCaptionEngine>());

    // Other Families
    registerEngine(std::make_unique<RussianCaptionEngine>());
    registerEngine(std::make_unique<ArabicCaptionEngine>());
    registerEngine(std::make_unique<HindiCaptionEngine>());
    registerEngine(std::make_unique<ThaiCaptionEngine>());
}

std::vector<std::string> CaptionRouter::supportedLanguages() const {
    std::vector<std::string> languages;

    for (const auto& engine : engines_) {
        auto engineLangs = engine->supportedLanguages();
        for (const auto& lang : engineLangs) {
            // Avoid duplicates
            if (std::find(languages.begin(), languages.end(), lang) == languages.end()) {
                languages.push_back(lang);
            }
        }
    }

    return languages;
}

// ═══════════════════════════════════════════════════════════════════════════
// Stage A: Language Detection
// ═══════════════════════════════════════════════════════════════════════════

LanguageInfo CaptionRouter::detectLanguage(const std::vector<Segment>& segments) const {
    // For now, we rely on ASR to provide language info via segment metadata
    // In the future, we could analyze text content for script detection

    // Check if any segment has language info
    for (const auto& seg : segments) {
        if (!seg.language.empty()) {
            return LanguageInfo(seg.language, "", 0.9f, "asr");
        }
    }

    // Default to English
    return LanguageInfo("en", "", 0.5f, "default");
}

LanguageInfo CaptionRouter::resolveLanguage(const LanguageInfo& detected,
                                             const std::string& userHint) const {
    // User hint takes priority
    if (!userHint.empty()) {
        return LanguageInfo(userHint, "", 1.0f, "user");
    }

    return detected;
}

// ═══════════════════════════════════════════════════════════════════════════
// Stage B: Engine Selection
// ═══════════════════════════════════════════════════════════════════════════

ICaptionEngine* CaptionRouter::getEngine(const LanguageInfo& langInfo) const {
    return getEngine(langInfo.langCode);
}

ICaptionEngine* CaptionRouter::getEngine(const std::string& langCode) const {
    // Find best matching engine
    ICaptionEngine* bestEngine = nullptr;
    int bestScore = 0;

    for (const auto& engine : engines_) {
        int score = engine->supports(langCode, "");
        if (score > bestScore) {
            bestScore = score;
            bestEngine = engine.get();
        }
    }

    // Fallback to English engine
    if (!bestEngine) {
        if (!fallbackEngine_) {
            fallbackEngine_ = std::make_unique<EnglishCaptionEngine>();
        }
        return fallbackEngine_.get();
    }

    return bestEngine;
}

// ═══════════════════════════════════════════════════════════════════════════
// Stage C: Preset Selection
// ═══════════════════════════════════════════════════════════════════════════

CaptionEngineOptions CaptionRouter::getPreset(const LanguageInfo& langInfo) const {
    return CaptionPresets::forLanguage(langInfo.langCode);
}

// ═══════════════════════════════════════════════════════════════════════════
// High-Level Processing
// ═══════════════════════════════════════════════════════════════════════════

CaptionDocument CaptionRouter::process(const std::vector<Segment>& segments,
                                        const CaptionEngineOptions& userOptions) const {
    // Stage A: Language detection
    LanguageInfo detected = detectLanguage(segments);
    LanguageInfo resolved = resolveLanguage(detected, userOptions.languageHint);

    // Stage B: Engine selection
    ICaptionEngine* engine = getEngine(resolved);

    // Stage C: Preset selection + merge user options
    CaptionEngineOptions options = getPreset(resolved);
    options.merge(userOptions);

    // Process through engine pipeline
    CaptionDocument doc = engine->process(segments, options);

    // Set resolved metadata
    doc.metadata.language = resolved.langCode;
    doc.metadata.script = resolved.script;
    doc.metadata.confidence = resolved.confidence;

    return doc;
}

// ═══════════════════════════════════════════════════════════════════════════
// Caption Engine Factory Implementation
// ═══════════════════════════════════════════════════════════════════════════

std::unique_ptr<ICaptionEngine> CaptionEngineFactory::create(const std::string& langCode) {
    // CJK
    if (langCode == "ja") {
        return std::make_unique<JapaneseCaptionEngine>();
    }
    if (langCode == "ko") {
        return std::make_unique<KoreanCaptionEngine>();
    }
    if (langCode == "zh" || langCode == "zh-CN" || langCode == "zh-Hans") {
        return std::make_unique<ChineseCaptionEngine>(ChineseCaptionEngine::Variant::Simplified);
    }
    if (langCode == "zh-TW" || langCode == "zh-HK" || langCode == "zh-Hant") {
        return std::make_unique<ChineseCaptionEngine>(ChineseCaptionEngine::Variant::Traditional);
    }

    // Latin
    if (langCode == "en" || langCode.substr(0, 2) == "en") {
        return std::make_unique<EnglishCaptionEngine>();
    }
    if (langCode == "es" || langCode.substr(0, 2) == "es") {
        return std::make_unique<SpanishCaptionEngine>();
    }
    if (langCode == "pt" || langCode.substr(0, 2) == "pt") {
        return std::make_unique<PortugueseCaptionEngine>();
    }
    if (langCode == "id" || langCode == "ms") {
        return std::make_unique<IndonesianCaptionEngine>();
    }
    if (langCode == "vi") {
        return std::make_unique<VietnameseCaptionEngine>();
    }

    // Cyrillic
    if (langCode == "ru") {
        return std::make_unique<RussianCaptionEngine>();
    }

    // RTL
    if (langCode == "ar") {
        return std::make_unique<ArabicCaptionEngine>();
    }

    // Indic
    if (langCode == "hi") {
        return std::make_unique<HindiCaptionEngine>();
    }

    // Thai
    if (langCode == "th") {
        return std::make_unique<ThaiCaptionEngine>();
    }

    // Default fallback
    return std::make_unique<EnglishCaptionEngine>();
}

std::unique_ptr<ICaptionEngine> CaptionEngineFactory::createByFamily(const std::string& family) {
    if (family == "CJK") {
        return std::make_unique<JapaneseCaptionEngine>();
    }
    if (family == "Latin") {
        return std::make_unique<EnglishCaptionEngine>();
    }
    if (family == "Cyrillic") {
        return std::make_unique<RussianCaptionEngine>();
    }
    if (family == "RTL") {
        return std::make_unique<ArabicCaptionEngine>();
    }
    if (family == "Indic") {
        return std::make_unique<HindiCaptionEngine>();
    }
    if (family == "Thai") {
        return std::make_unique<ThaiCaptionEngine>();
    }

    // Default
    return std::make_unique<EnglishCaptionEngine>();
}

std::vector<std::unique_ptr<ICaptionEngine>> CaptionEngineFactory::createAll() {
    std::vector<std::unique_ptr<ICaptionEngine>> engines;

    // CJK
    engines.push_back(std::make_unique<JapaneseCaptionEngine>());
    engines.push_back(std::make_unique<KoreanCaptionEngine>());
    engines.push_back(std::make_unique<ChineseCaptionEngine>(ChineseCaptionEngine::Variant::Simplified));
    engines.push_back(std::make_unique<ChineseCaptionEngine>(ChineseCaptionEngine::Variant::Traditional));

    // Latin
    engines.push_back(std::make_unique<EnglishCaptionEngine>());
    engines.push_back(std::make_unique<SpanishCaptionEngine>());
    engines.push_back(std::make_unique<PortugueseCaptionEngine>());
    engines.push_back(std::make_unique<IndonesianCaptionEngine>());
    engines.push_back(std::make_unique<VietnameseCaptionEngine>());

    // Other
    engines.push_back(std::make_unique<RussianCaptionEngine>());
    engines.push_back(std::make_unique<ArabicCaptionEngine>());
    engines.push_back(std::make_unique<HindiCaptionEngine>());
    engines.push_back(std::make_unique<ThaiCaptionEngine>());

    return engines;
}

std::vector<std::string> CaptionEngineFactory::families() {
    return {"CJK", "Latin", "Cyrillic", "RTL", "Indic", "Thai"};
}

} // namespace muninn
