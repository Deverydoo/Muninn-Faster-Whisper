#pragma once

#include "export.h"
#include <string>
#include <vector>
#include <memory>

namespace muninn {

/**
 * @brief Language information for translation
 */
struct TranslationLanguage {
    std::string code;       // Short code ("en", "ja", "es", etc.)
    std::string nllb_code;  // NLLB code ("eng_Latn", "jpn_Jpan", etc.)
    std::string name;       // Human-readable name ("English", "Japanese", etc.)
};

/**
 * @brief Options for text translation
 */
struct TranslationOptions {
    int beam_size = 4;              // Beam search width (1-10)
    float length_penalty = 1.0f;    // Length penalty (>1 = longer, <1 = shorter)
    int max_length = 256;           // Maximum output tokens per segment
    float repetition_penalty = 1.0f; // Repetition penalty
    int no_repeat_ngram_size = 0;   // Prevent n-gram repetitions (0 = disabled)
};

/**
 * @brief Text translator using NLLB-200 model via CTranslate2
 *
 * Supports translation between 15 major world languages:
 * - Latin-script: English, Spanish, French, German, Italian, Portuguese, Vietnamese
 * - Cyrillic: Russian, Ukrainian
 * - Asian: Chinese (Simplified), Japanese, Korean, Thai
 * - Other: Arabic, Hindi
 *
 * Thread Safety:
 * - Static methods (supported_languages, to_nllb_code, from_nllb_code) are thread-safe
 * - Instance methods are NOT thread-safe for concurrent calls on the same instance
 * - For multi-threaded use, create one Translator per thread or use external synchronization
 *
 * Performance Tips:
 * - Use translate_batch() instead of calling translate() in a loop (5-10x faster)
 * - Batch translation amortizes tokenization and GPU kernel launch overhead
 *
 * Usage:
 * @code
 *   Translator translator("models/nllb-200-distilled-600M");
 *   std::string spanish = translator.translate("Hello world", "en", "es");
 *
 *   // For multiple texts, use batch (much faster):
 *   std::vector<std::string> texts = {"Hello", "World"};
 *   auto translations = translator.translate_batch(texts, "en", "ru");
 * @endcode
 */
class MUNINN_API Translator {
public:
    /**
     * @brief Initialize translator with NLLB model
     *
     * @param model_path Path to CTranslate2-converted NLLB model directory
     * @param device "cuda" or "cpu" (default: "cuda")
     * @param compute_type "float16", "int8", "float32" (default: "float16")
     * @param device_index GPU index for multi-GPU systems (default: 0)
     *
     * @throws std::runtime_error if model cannot be loaded
     */
    Translator(const std::string& model_path,
               const std::string& device = "cuda",
               const std::string& compute_type = "float16",
               int device_index = 0);

    ~Translator();

    // Non-copyable, movable
    Translator(const Translator&) = delete;
    Translator& operator=(const Translator&) = delete;
    Translator(Translator&&) noexcept;
    Translator& operator=(Translator&&) noexcept;

    /**
     * @brief Translate a single text
     *
     * @param text Source text to translate
     * @param source_lang Source language code ("en", "ja", "es", etc.)
     * @param target_lang Target language code
     * @param options Translation options
     * @return Translated text
     */
    std::string translate(const std::string& text,
                          const std::string& source_lang,
                          const std::string& target_lang,
                          const TranslationOptions& options = {});

    /**
     * @brief Translate multiple texts in a batch (more efficient)
     *
     * @param texts Source texts to translate
     * @param source_lang Source language code
     * @param target_lang Target language code
     * @param options Translation options
     * @return Translated texts (same order as input)
     */
    std::vector<std::string> translate_batch(
        const std::vector<std::string>& texts,
        const std::string& source_lang,
        const std::string& target_lang,
        const TranslationOptions& options = {});

    /**
     * @brief Translate to multiple target languages at once
     *
     * @param text Source text
     * @param source_lang Source language code
     * @param target_langs List of target language codes
     * @param options Translation options
     * @return Map of target_lang -> translated_text
     */
    std::vector<std::pair<std::string, std::string>> translate_multi_target(
        const std::string& text,
        const std::string& source_lang,
        const std::vector<std::string>& target_langs,
        const TranslationOptions& options = {});

    /**
     * @brief Check if a language is supported
     * @param lang_code Language code ("en", "ja", etc.)
     * @return true if supported
     */
    bool is_language_supported(const std::string& lang_code) const;

    /**
     * @brief Check if translation between two languages is supported
     * @param source Source language code
     * @param target Target language code
     * @return true if the pair is supported
     */
    bool supports_language_pair(const std::string& source,
                                const std::string& target) const;

    /**
     * @brief Get list of all supported languages
     * @return Vector of TranslationLanguage structs
     */
    static std::vector<TranslationLanguage> supported_languages();

    /**
     * @brief Convert Whisper/short language code to NLLB code
     * @param code Short code ("en", "ja", etc.)
     * @return NLLB code ("eng_Latn", "jpn_Jpan", etc.) or empty if unsupported
     */
    static std::string to_nllb_code(const std::string& code);

    /**
     * @brief Convert NLLB code to short language code
     * @param nllb_code NLLB code ("eng_Latn", etc.)
     * @return Short code ("en", etc.) or empty if unknown
     */
    static std::string from_nllb_code(const std::string& nllb_code);

    /**
     * @brief Check if the model is loaded and ready
     */
    bool is_loaded() const;

    /**
     * @brief Get the device being used ("cuda" or "cpu")
     */
    std::string device() const;

    /**
     * @brief Explicitly shutdown the translator and release GPU resources
     *
     * Call this method before destroying the Translator to ensure clean shutdown.
     * This is especially important on Windows where CUDA resource cleanup during
     * destructor can hang indefinitely.
     *
     * After calling shutdown(), the translator is no longer usable.
     * Calling translate() after shutdown() will return the original text unchanged.
     */
    void shutdown();

    /**
     * @brief Request cancellation of ongoing translation
     *
     * Thread-safe method that can be called from any thread (e.g., UI thread)
     * to request cancellation of ongoing translate_batch() calls.
     */
    void cancel();

    /**
     * @brief Reset cancellation flag
     *
     * Call this before starting a new translation if a previous one was cancelled.
     */
    void reset_cancel();

    /**
     * @brief Check if cancellation was requested
     * @return true if cancel() was called and not yet reset
     */
    bool is_cancelled() const;

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace muninn
