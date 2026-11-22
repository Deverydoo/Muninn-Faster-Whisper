#pragma once

#include "export.h"
#include "caption_engine.h"
#include "caption_types.h"
#include "caption_options.h"
#include <string>
#include <vector>
#include <memory>
#include <shared_mutex>
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
 * The default engines are registered when the router is first used.
 * Registration and lookups are thread-safe, and engines are never removed,
 * so a pointer from getEngine() stays valid for the life of the program.
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
     *
     * When two engines score the same for a language, the one registered
     * first is used.
     *
     * @param engine Engine to register (router takes ownership)
     */
    void registerEngine(std::unique_ptr<ICaptionEngine> engine);

    /**
     * @brief Register all default engines
     *
     * The constructor already does this, so there is no need to call it.
     * Calling it again does nothing: it never clears engines (which would
     * leave dangling pointers in other threads).
     */
    void registerDefaultEngines();

    /**
     * @brief Get number of registered engines
     */
    size_t engineCount() const;

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
     * @param overrides Options to change on top of the language preset
     * @return Complete caption document
     */
    CaptionDocument process(const std::vector<Segment>& segments,
                            const CaptionEngineOverrides& overrides = {}) const;

    /**
     * @brief Process with complete options of your own (no preset)
     *
     * The engine is chosen from options.languageHint, or detected from the
     * segments when the hint is empty.
     */
    CaptionDocument processWithOptions(const std::vector<Segment>& segments,
                                       const CaptionEngineOptions& options) const;

private:
    CaptionRouter();
    ~CaptionRouter() = default;

    void registerDefaultEnginesLocked();

    mutable std::shared_mutex mutex_;
    std::vector<std::unique_ptr<ICaptionEngine>> engines_;
    bool defaultsRegistered_ = false;
    const std::unique_ptr<ICaptionEngine> fallbackEngine_;  // English, made up front
};

} // namespace muninn
