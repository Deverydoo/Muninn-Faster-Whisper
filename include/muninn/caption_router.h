#pragma once

#include "export.h"
#include "caption_engine.h"
#include "caption_types.h"
#include "caption_options.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// Caption Router
// ═══════════════════════════════════════════════════════════════════════════
//
// 3-Stage routing system for automatic language handling:
//
// Stage A: Language Detection
//   - Use ASR language + user override
//
// Stage B: Engine Selection
//   - Route to best engine based on language/script
//
// Stage C: Preset Selection
//   - Apply language-specific caption options
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Language detection result
 */
struct MUNINN_API LanguageInfo {
    std::string langCode;       // ISO 639-1 (e.g., "ja", "ko", "zh")
    std::string script;         // ISO 15924 (e.g., "Hans", "Hant")
    float confidence = 0.0f;    // Detection confidence (0.0-1.0)
    std::string source;         // Detection source ("asr", "user", "detected")

    LanguageInfo() = default;
    LanguageInfo(const std::string& lang, const std::string& scr = "",
                 float conf = 1.0f, const std::string& src = "user")
        : langCode(lang), script(scr), confidence(conf), source(src) {}
};

/**
 * @brief Caption Router singleton
 *
 * Manages engine registration, language routing, and caption processing.
 */
class MUNINN_API CaptionRouter {
public:
    /**
     * @brief Get singleton instance
     */
    static CaptionRouter& instance();

    // Disable copy/move
    CaptionRouter(const CaptionRouter&) = delete;
    CaptionRouter& operator=(const CaptionRouter&) = delete;

    // ═══════════════════════════════════════════════════════════════════════
    // Engine Registration
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Register a caption engine
     * @param engine Engine to register (router takes ownership)
     */
    void registerEngine(std::unique_ptr<ICaptionEngine> engine);

    /**
     * @brief Register all default engines
     */
    void registerDefaultEngines();

    /**
     * @brief Get number of registered engines
     */
    size_t engineCount() const { return engines_.size(); }

    /**
     * @brief Get list of supported language codes
     */
    std::vector<std::string> supportedLanguages() const;

    // ═══════════════════════════════════════════════════════════════════════
    // Stage A: Language Detection
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Detect language from ASR segments
     *
     * @param segments ASR output with optional language hints
     * @return Detected language info
     */
    LanguageInfo detectLanguage(const std::vector<Segment>& segments) const;

    /**
     * @brief Resolve language with user override
     *
     * @param detected Auto-detected language
     * @param userHint User-specified language (takes priority)
     * @return Resolved language info
     */
    LanguageInfo resolveLanguage(const LanguageInfo& detected,
                                  const std::string& userHint) const;

    // ═══════════════════════════════════════════════════════════════════════
    // Stage B: Engine Selection
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Get best engine for a language
     *
     * @param langInfo Language info
     * @return Best matching engine (never null - falls back to English)
     */
    ICaptionEngine* getEngine(const LanguageInfo& langInfo) const;

    /**
     * @brief Get engine by language code
     */
    ICaptionEngine* getEngine(const std::string& langCode) const;

    // ═══════════════════════════════════════════════════════════════════════
    // Stage C: Preset Selection
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Get caption options preset for a language
     *
     * @param langInfo Language info
     * @return Language-specific caption options
     */
    CaptionEngineOptions getPreset(const LanguageInfo& langInfo) const;

    // ═══════════════════════════════════════════════════════════════════════
    // High-Level Processing
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Process ASR segments through the full pipeline
     *
     * Performs:
     * 1. Language detection/resolution
     * 2. Engine selection
     * 3. Preset application
     * 4. Token → Cue → Event pipeline
     *
     * @param segments ASR transcription segments
     * @param userOptions User options (merged with preset)
     * @return Complete caption document
     */
    CaptionDocument process(const std::vector<Segment>& segments,
                            const CaptionEngineOptions& userOptions = {}) const;

private:
    CaptionRouter() = default;
    ~CaptionRouter() = default;

    std::vector<std::unique_ptr<ICaptionEngine>> engines_;
    mutable std::unique_ptr<ICaptionEngine> fallbackEngine_;
};

} // namespace muninn
