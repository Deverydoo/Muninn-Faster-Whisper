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
 *
 * Defaults are tuned for NLLB-200 based on research:
 * - no_repeat_ngram_size=3 prevents hallucination/repetition loops
 * - beam_size=4 is optimal quality/speed tradeoff
 * - length_penalty=1.0 produces natural length outputs
 */
struct TranslationOptions {
    int beam_size = 4;              // Beam search width (1-10)
    float length_penalty = 1.0f;    // Length penalty (>1 = longer, <1 = shorter)
    int max_length = 256;           // Maximum output tokens per segment
    float repetition_penalty = 1.0f; // Repetition penalty
    int no_repeat_ngram_size = 3;   // Prevent 3-gram repetitions (prevents hallucination loops)

    // How many sentences CTranslate2 decodes together on the GPU. A batch runs
    // until its longest member finishes, so throughput depends on grouping
    // similar-length sentences — which CTranslate2 does for us, but only across
    // the sentences it is handed at once. Lower this if VRAM is tight.
    int max_batch_size = 32;
};

/**
 * @brief What translate_batch_result() returns
 *
 * texts has one entry per input, in input order. An entry is "" when its input
 * was empty or whitespace, when it failed, or when it was never reached
 * because the batch was cancelled or the language isn't supported. It is never
 * the untranslated source text (except when source and target are the same
 * language, where the source is the correct answer).
 */
struct TranslationBatchResult {
    std::vector<std::string> texts;
    bool cancelled = false;     // cancel() or unload_model() stopped the batch before every item was done
    size_t failed_count = 0;    // non-empty inputs left "" (errors, cancellation, unsupported language)
    std::string error;          // the first error, "" when there was none
};

/**
 * @brief Text translator using NLLB-200 model via CTranslate2
 *
 * Language codes are Whisper-style short codes ("en", "ja"). Case and region
 * are ignored when only the base language is mapped: "EN", "en-US" and "en_us"
 * all mean English. "zh-TW"/"zh-Hant" mean Traditional Chinese.
 *
 * Thread Safety:
 * - Static methods (supported_languages, to_nllb_code, from_nllb_code) are thread-safe
 * - translate_batch() holds a shared lock on the model for the whole decode,
 *   and unload_model()/shutdown() take it exclusively, so unloading while a
 *   batch runs waits for (and shortens) that batch instead of freeing the
 *   model under it. cancel() may be called from any thread.
 * - Destroying the Translator while another thread is still inside one of its
 *   methods is not allowed.
 *
 * Performance: use translate_batch() rather than translate() in a loop, so
 * CTranslate2 can decode many sentences together.
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
     * @param model_path Path to CTranslate2-converted NLLB model directory (UTF-8).
     *                   It must contain sentencepiece.bpe.model.
     * @param device "cuda" or "cpu" (default: "cuda")
     * @param compute_type "default" (float16 on CUDA, float32 on CPU), or
     *                     "float16", "int8", "int8_float16", "float32", ...
     *                     A type the device can't run falls back to the
     *                     nearest one it can (float16 on CPU becomes float32).
     * @param device_index GPU index for multi-GPU systems (default: 0)
     *
     * @throws std::runtime_error if the model or its SentencePiece tokenizer
     *         can't be loaded (there is no fallback tokenizer), or if Muninn was
     *         built without SentencePiece.
     */
    Translator(const std::string& model_path,
               const std::string& device = "cuda",
               const std::string& compute_type = "default",
               int device_index = 0);

    ~Translator();

    // Non-copyable, movable
    Translator(const Translator&) = delete;
    Translator& operator=(const Translator&) = delete;
    Translator(Translator&&) noexcept;
    Translator& operator=(Translator&&) noexcept;

    /**
     * @brief Unload the model and release GPU resources
     *
     * Releases the model's memory (VRAM on CUDA). If a translate_batch() is
     * running, it is asked to stop after its current chunk (its remaining
     * items come back "" with cancelled = true) and this waits for it, up to
     * timeout_ms. After an unload the Translator can't translate again (calls
     * return ""); create a new one. A second call does nothing. The
     * destructor calls it with a 5000 ms timeout.
     *
     * @param timeout_ms How long to wait for a running batch to let go of the
     *                   model (default 3000 ms; negative = wait as long as it takes)
     * @return true when the model is unloaded (or was not loaded); false when
     *         the timeout passed first, in which case nothing was released
     */
    bool unload_model(int timeout_ms = 3000);

    /**
     * @brief Translate a single text
     *
     * @param text Source text to translate
     * @param source_lang Source language code ("en", "ja", "es", etc.)
     * @param target_lang Target language code
     * @param options Translation options
     * @return Translated text, or "" if the text was empty or translation failed
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
     * @return Translated texts (same order as input); "" for an empty input
     *         and for any item that failed or was cancelled. Use
     *         translate_batch_result() to tell those apart.
     */
    std::vector<std::string> translate_batch(
        const std::vector<std::string>& texts,
        const std::string& source_lang,
        const std::string& target_lang,
        const TranslationOptions& options = {});

    /**
     * @brief translate_batch() with the outcome: cancelled, failures, error
     */
    TranslationBatchResult translate_batch_result(
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
     * @return (target_lang, translated_text) pairs in the order of target_langs;
     *         "" for a target that failed. One translate() call per target.
     */
    std::vector<std::pair<std::string, std::string>> translate_multi_target(
        const std::string& text,
        const std::string& source_lang,
        const std::vector<std::string>& target_langs,
        const TranslationOptions& options = {});

    /**
     * @brief Check if a language is supported
     * @param lang_code Language code ("en", "ja", "en-US", etc.)
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
     * @param code Short code ("en", "ja", "pt-BR", "ZH_tw", etc.; case and
     *             region are normalised) or an NLLB code, returned as is
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
     * Calling translate() after shutdown() returns "" (translate_batch_result
     * reports the error).
     */
    void shutdown();

    /**
     * @brief Request cancellation of ongoing translation
     *
     * Thread-safe method that can be called from any thread (e.g., UI thread)
     * to request cancellation of ongoing translate_batch() calls. The batch
     * stops after its current chunk; items not finished come back "".
     * The flag stays set (new batches return at once) until reset_cancel().
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
