#include "muninn/translator.h"
#include "muninn/logging.h"
#include "translate_text.h"
#include <ctranslate2/translator.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>

#ifdef MUNINN_USE_SENTENCEPIECE
#include <sentencepiece_processor.h>
#endif

namespace muninn {

// Language info for supported_languages(). The code -> NLLB mapping itself
// (with case and region normalisation) is in translate_text.cpp.
static const std::vector<TranslationLanguage> SUPPORTED_LANGUAGES = {
    {"en", "eng_Latn", "English"},
    {"es", "spa_Latn", "Spanish"},
    {"fr", "fra_Latn", "French"},
    {"de", "deu_Latn", "German"},
    {"it", "ita_Latn", "Italian"},
    {"pt", "por_Latn", "Portuguese"},
    {"nl", "nld_Latn", "Dutch"},
    {"pl", "pol_Latn", "Polish"},
    {"ru", "rus_Cyrl", "Russian"},
    {"uk", "ukr_Cyrl", "Ukrainian"},
    {"bg", "bul_Cyrl", "Bulgarian"},
    {"be", "bel_Cyrl", "Belarusian"},
    {"zh", "zho_Hans", "Chinese (Simplified)"},
    {"ja", "jpn_Jpan", "Japanese"},
    {"ko", "kor_Hang", "Korean"},
    {"ar", "arb_Arab", "Arabic"},
    {"hi", "hin_Deva", "Hindi"},
    {"vi", "vie_Latn", "Vietnamese"},
    {"th", "tha_Thai", "Thai"},
    {"tr", "tur_Latn", "Turkish"},
    {"id", "ind_Latn", "Indonesian"},
    {"he", "heb_Hebr", "Hebrew"},
    {"tl", "tgl_Latn", "Filipino"},
};

namespace {

std::string lower_ascii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

// Picks the compute type to load with. "default" (or "") is float16 on CUDA
// and float32 on CPU. Anything else is resolved against what the device can
// actually run with CTranslate2's fallback on (float16 on CPU -> float32),
// because the model loader resolves without fallback and would throw.
ctranslate2::ComputeType choose_compute_type(const std::string& requested_str,
                                             ctranslate2::Device device,
                                             int device_index) {
    std::string name = lower_ascii(requested_str);
    if (name.empty() || name == "default") {
        name = (device == ctranslate2::Device::CUDA) ? "float16" : "float32";
    }
    const ctranslate2::ComputeType requested = ctranslate2::str_to_compute_type(name);

    // DEFAULT keeps the model's own type and INT8 depends on the model's float
    // type; neither can be resolved before the model is read, and neither
    // throws for lack of fallback (INT8 already resolves with it).
    if (requested == ctranslate2::ComputeType::DEFAULT ||
        requested == ctranslate2::ComputeType::INT8) {
        return requested;
    }
    try {
        const ctranslate2::ComputeType resolved = ctranslate2::resolve_compute_type(
            requested, ctranslate2::ComputeType::FLOAT32, device, device_index,
            /*enable_fallback=*/true);
        if (resolved != requested) {
            Logger::info("Translator: " + name + " is not supported on this device, using " +
                         ctranslate2::compute_type_to_str(resolved));
        }
        return resolved;
    } catch (const std::exception& e) {
        // Asking the device failed (no CUDA driver, say). Let the model load
        // report the real problem.
        Logger::debug(std::string("Translator: could not check compute type support: ") + e.what());
        return requested;
    }
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Implementation
// ═══════════════════════════════════════════════════════════════════════════

class Translator::Impl {
public:
    std::unique_ptr<ctranslate2::Translator> model;
    std::string device_str;
    std::string model_path_;
    std::atomic<bool> loaded{false};
    std::atomic<bool> shutdown_requested{false};
    std::atomic<bool> cancelled{false};
    // Set by unload_model() so a running batch stops at its next chunk.
    std::atomic<bool> unload_requested{false};

    // translate_batch holds this shared for the whole decode; unload and
    // shutdown take it exclusively.
    mutable std::shared_timed_mutex model_mutex;

#ifdef MUNINN_USE_SENTENCEPIECE
    sentencepiece::SentencePieceProcessor sp_processor;
#endif

    ~Impl() {
        // Ensure clean shutdown - model must be destroyed before SentencePiece
        // to avoid CUDA synchronization issues on Windows
        shutdown();
    }

    void shutdown() {
        if (shutdown_requested.exchange(true)) {
            return;  // Already shutting down
        }

        unload_requested.store(true);
        std::unique_lock<std::shared_timed_mutex> lock(model_mutex);
        loaded = false;

        // Destroy CTranslate2 model first - this triggers ThreadPool shutdown
        // which can block on CUDA synchronization
        if (model) {
            Logger::debug("Translator shutting down...");
            model.reset();
            Logger::debug("Translator shutdown complete");
        }
    }

    Impl(const std::string& model_path,
         const std::string& device,
         const std::string& compute_type,
         int device_index)
        : device_str(device), model_path_(model_path)
    {
        // Paths arrive as UTF-8. std::filesystem::path(std::string) would
        // read them in the ANSI code page on Windows, and a user named José
        // or 田中 would lose the tokenizer.
        const std::filesystem::path dir = std::filesystem::u8path(model_path);
        if (!std::filesystem::is_directory(dir)) {
            throw std::runtime_error("Translation model folder not found: " + model_path);
        }

        // The tokenizer first: it's quick, and without it there's no point
        // loading the model. There is no fallback tokenizer (whitespace
        // splitting gives NLLB input it can't read and nonsense output).
#ifdef MUNINN_USE_SENTENCEPIECE
        const std::filesystem::path sp_path = dir / "sentencepiece.bpe.model";
        const std::string sp_path_utf8 = sp_path.u8string();
        std::ifstream sp_file(sp_path, std::ios::binary);
        if (!sp_file) {
            throw std::runtime_error("SentencePiece model not found: " + sp_path_utf8 +
                                     " (the NLLB model folder must contain sentencepiece.bpe.model)");
        }
        const std::string sp_bytes((std::istreambuf_iterator<char>(sp_file)),
                                   std::istreambuf_iterator<char>());
        // Loaded from memory so SentencePiece never opens the path itself.
        const auto status = sp_processor.LoadFromSerializedProto(sp_bytes);
        if (!status.ok()) {
            throw std::runtime_error("SentencePiece model failed to load: " + sp_path_utf8 +
                                     ": " + status.ToString());
        }
        Logger::debug("SentencePiece tokenizer loaded: " + sp_path_utf8);
#else
        throw std::runtime_error(
            "This Muninn build has no SentencePiece, so it can't translate "
            "(rebuild with SentencePiece; see patches/README.md)");
#endif

        const std::string dev = lower_ascii(device);
        const ctranslate2::Device ct_device =
            (dev == "cuda") ? ctranslate2::Device::CUDA : ctranslate2::Device::CPU;
        device_str = (ct_device == ctranslate2::Device::CUDA) ? "cuda" : "cpu";
        const ctranslate2::ComputeType ct_compute = choose_compute_type(compute_type, ct_device, device_index);

        try {
            // CTranslate2 opens model files as UTF-8 itself (filesystem.cc
            // widens with CP_UTF8), so the path goes in unchanged.
            model = std::make_unique<ctranslate2::Translator>(
                model_path,
                ct_device,
                ct_compute,
                std::vector<int>{device_index}
            );
        } catch (const std::exception& e) {
            Logger::error(std::string("Failed to load translator: ") + e.what());
            throw;
        }

        loaded = true;
        Logger::info("Translator loaded: " + model_path + " (device=" + device_str +
                     ", compute=" + ctranslate2::compute_type_to_str(ct_compute) + ")");
    }

    // SentencePiece pieces for one sentence. Throws on failure.
    std::vector<std::string> tokenize(const std::string& text) {
        std::vector<std::string> tokens;
#ifdef MUNINN_USE_SENTENCEPIECE
        const auto status = sp_processor.Encode(text, &tokens);
        if (!status.ok()) {
            throw std::runtime_error("SentencePiece encode failed: " + status.ToString());
        }
#else
        (void)text;
#endif
        return tokens;
    }

    // Text from SentencePiece pieces. Throws on failure.
    std::string detokenize(const std::vector<std::string>& tokens) {
        std::string result;
#ifdef MUNINN_USE_SENTENCEPIECE
        const auto status = sp_processor.Decode(tokens, &result);
        if (!status.ok()) {
            throw std::runtime_error("SentencePiece decode failed: " + status.ToString());
        }
#else
        (void)tokens;
#endif
        return result;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════

Translator::Translator(const std::string& model_path,
                       const std::string& device,
                       const std::string& compute_type,
                       int device_index)
    : pimpl_(std::make_unique<Impl>(model_path, device, compute_type, device_index))
{}

Translator::~Translator() {
    if (pimpl_) {
        unload_model(5000);  // Match CTranslate2's internal Worker::join(5000) timeout
    }
}

bool Translator::unload_model(int timeout_ms) {
    if (!pimpl_) {
        return true;
    }

    // A running batch holds the lock shared; ask it to stop at its next
    // chunk, then wait for it to let go.
    pimpl_->unload_requested.store(true, std::memory_order_release);
    std::unique_lock<std::shared_timed_mutex> lock(pimpl_->model_mutex, std::defer_lock);
    if (timeout_ms < 0) {
        lock.lock();
    } else if (!lock.try_lock_for(std::chrono::milliseconds(timeout_ms))) {
        // Nothing was unloaded, so later batches may run again.
        pimpl_->unload_requested.store(false, std::memory_order_release);
        Logger::warn("NLLB unload: a translation still held the model after " +
                     std::to_string(timeout_ms) + " ms; not unloaded");
        return false;
    }

    if (!pimpl_->model) {
        return true;
    }

    // These were "TEMPORARY:" info lines from tracking down the VRAM leak; they
    // shipped and filled the user-facing log with step-by-step noise on every
    // unload. Kept as debug so the trace is still there when it's needed.
    Logger::debug("NLLB unload: detaching models");
    pimpl_->loaded = false;

    auto detached_models = pimpl_->model->detach_models();
    Logger::debug("NLLB unload: detached " + std::to_string(detached_models.size()) + " model(s)");

    for (auto& model : detached_models) {
        const_cast<ctranslate2::models::Model*>(model.get())->release_gpu_memory();
    }
    detached_models.clear();

    try {
        ctranslate2::get_allocator(ctranslate2::Device::CUDA).clear_cache();
    } catch (...) {
        Logger::debug("NLLB unload: allocator cache clear failed (non-fatal)");
    }

    // DO NOT call model.reset() — corrupts CUDA context for subsequent operations.
    // pimpl_->model.reset();  // INTENTIONALLY SKIPPED
    //
    // Consequence worth knowing: CTranslate2's ThreadPool lives inside the
    // Translator, so its worker threads are only joined when this object is
    // destroyed, not here. Unloading frees the VRAM but leaves the threads
    // parked until the Translator itself goes away.

    Logger::info("NLLB translator unloaded (VRAM released)");
    return true;
}

Translator::Translator(Translator&&) noexcept = default;
Translator& Translator::operator=(Translator&&) noexcept = default;

std::string Translator::translate(const std::string& text,
                                  const std::string& source_lang,
                                  const std::string& target_lang,
                                  const TranslationOptions& options) {
    auto results = translate_batch({text}, source_lang, target_lang, options);
    return results.empty() ? std::string() : results[0];
}

std::vector<std::string> Translator::translate_batch(
    const std::vector<std::string>& texts,
    const std::string& source_lang,
    const std::string& target_lang,
    const TranslationOptions& options)
{
    return translate_batch_result(texts, source_lang, target_lang, options).texts;
}

TranslationBatchResult Translator::translate_batch_result(
    const std::vector<std::string>& texts,
    const std::string& source_lang,
    const std::string& target_lang,
    const TranslationOptions& options)
{
    TranslationBatchResult out;
    out.texts.assign(texts.size(), std::string());
    if (texts.empty()) return out;

    // Inputs that need work: empty and whitespace-only ones pass through as "".
    size_t non_blank = 0;
    for (const auto& t : texts) {
        if (!detail::is_blank(t)) ++non_blank;
    }
    if (non_blank == 0) return out;

    auto fail_all = [&](const std::string& why) {
        out.error = why;
        out.failed_count = non_blank;
        Logger::warn("Translator: " + why);
        return out;
    };

    if (!pimpl_) return fail_all("translator has been moved from");

    // Held for the whole decode so unload_model() can't free the model under us.
    std::shared_lock<std::shared_timed_mutex> lock(pimpl_->model_mutex);
    if (!pimpl_->loaded || !pimpl_->model) {
        return fail_all("translator is not loaded");
    }

    const std::string src_nllb = to_nllb_code(source_lang);
    const std::string tgt_nllb = to_nllb_code(target_lang);
    Logger::debug("translate_batch: " + source_lang + " (" + src_nllb + ") -> " + target_lang +
                  " (" + tgt_nllb + "), " + std::to_string(texts.size()) + " texts");

    if (src_nllb.empty() || tgt_nllb.empty()) {
        return fail_all("unsupported language pair: " + source_lang + " -> " + target_lang);
    }

    // Same language: the source is the translation.
    if (src_nllb == tgt_nllb) {
        for (size_t i = 0; i < texts.size(); ++i) {
            if (!detail::is_blank(texts[i])) out.texts[i] = texts[i];
        }
        return out;
    }

    // Set translation options
    // Note: no_repeat_ngram_size=3 by default prevents hallucination loops
    ctranslate2::TranslationOptions ct_options;
    ct_options.beam_size = options.beam_size;
    ct_options.length_penalty = options.length_penalty;
    ct_options.repetition_penalty = options.repetition_penalty;
    ct_options.no_repeat_ngram_size = options.no_repeat_ngram_size;
    ct_options.max_decoding_length = options.max_length;

    // =========================================================================
    // SENTENCE-LEVEL TRANSLATION
    // NLLB-200 works best on individual sentences, not long paragraphs.
    // We split each input text into sentences, translate each sentence,
    // then rejoin them. This significantly improves translation quality.
    // =========================================================================

    // Step 1: Split all texts into sentences, tracking which original text each came from
    std::vector<std::string> all_sentences;
    std::vector<size_t> text_boundaries;  // Index where each original text's sentences start
    text_boundaries.push_back(0);

    for (const auto& text : texts) {
        if (!detail::is_blank(text)) {
            for (auto& sentence : detail::split_sentences(text)) {
                all_sentences.push_back(std::move(sentence));
            }
        }
        text_boundaries.push_back(all_sentences.size());
    }

    Logger::debug("Split " + std::to_string(texts.size()) + " texts into " +
                  std::to_string(all_sentences.size()) + " sentences");

    // Step 2: Translate all sentences in batches
    //
    // This used to hand CTranslate2 8 sentences per call, in timeline order.
    // Two costs came out of that. A decoding batch runs until its *longest*
    // member finishes, so one long sentence dragged seven short ones along with
    // it; and CTranslate2 sorts by length and re-batches internally to avoid
    // exactly that, but it can only sort across what it is given at once — with
    // 8 arbitrary sentences there was nothing to sort.
    //
    // So hand it a large window and let it do the batching. Results come back
    // in input order regardless of how it reorders internally (each example
    // carries its original index), so the rejoin below is unaffected. The
    // window still bounds how long a cancel takes to be noticed, which is why
    // this is 128 rather than "all of them": sorting gains flatten out well
    // before that, and a bigger window only makes Cancel feel unresponsive.
    const size_t CHUNK_SIZE = 128;
    const size_t max_batch_size = static_cast<size_t>(std::max(1, options.max_batch_size));
    std::vector<std::string> translated_sentences(all_sentences.size());
    std::vector<char> sentence_ok(all_sentences.size(), 0);

    for (size_t chunk_start = 0; chunk_start < all_sentences.size(); chunk_start += CHUNK_SIZE) {
        if (pimpl_->cancelled.load(std::memory_order_acquire) ||
            pimpl_->unload_requested.load(std::memory_order_acquire)) {
            Logger::info("Translation cancelled");
            out.cancelled = true;
            break;  // the rest stay not-ok, so their items come back ""
        }

        const size_t chunk_end = std::min(chunk_start + CHUNK_SIZE, all_sentences.size());

        try {
            // Tokenize inputs for this chunk
            // NLLB format: [source_lang_token, tokens..., </s>]
            std::vector<std::vector<std::string>> tokenized_inputs;
            std::vector<std::vector<std::string>> target_prefixes;
            tokenized_inputs.reserve(chunk_end - chunk_start);
            target_prefixes.reserve(chunk_end - chunk_start);

            for (size_t i = chunk_start; i < chunk_end; ++i) {
                std::vector<std::string> tokens;
                tokens.push_back(src_nllb);  // NLLB requires source lang token first

                auto text_tokens = pimpl_->tokenize(all_sentences[i]);
                tokens.insert(tokens.end(), text_tokens.begin(), text_tokens.end());

                tokens.push_back("</s>");  // EOS at the end of the source
                tokenized_inputs.push_back(std::move(tokens));

                // Target prefix: [</s>, target_lang_token] - NLLB decoder starts with </s>
                target_prefixes.push_back({"</s>", tgt_nllb});
            }

            // Translate this chunk. max_batch_size lets CTranslate2 length-sort
            // and split into GPU-sized batches itself; passing 0 (the old
            // behaviour) makes it decode the whole window as one padded batch.
            auto results = pimpl_->model->translate_batch(
                tokenized_inputs,
                target_prefixes,
                ct_options,
                max_batch_size,
                ctranslate2::BatchType::Examples
            );

            for (size_t i = 0; i < results.size() && chunk_start + i < chunk_end; ++i) {
                const size_t idx = chunk_start + i;
                if (results[i].hypotheses.empty()) {
                    Logger::warn("Translator: no hypothesis for sentence " + std::to_string(idx));
                    if (out.error.empty()) out.error = "the model returned no translation for a sentence";
                    continue;
                }
                std::string translated = pimpl_->detokenize(results[i].hypotheses[0]);
                translated_sentences[idx] = detail::clean_output(translated, tgt_nllb);
                sentence_ok[idx] = 1;
                if (idx == 0) {
                    Logger::debug("First sentence: \"" + detail::utf8_truncate(all_sentences[0], 50) +
                                  "\" -> \"" + detail::utf8_truncate(translated_sentences[0], 50) + "\"");
                }
            }

        } catch (const std::exception& e) {
            Logger::error("Translation error in chunk " + std::to_string(chunk_start / CHUNK_SIZE) +
                          ": " + e.what());
            if (out.error.empty()) out.error = e.what();
            // This chunk's sentences stay not-ok.
        }
    }

    // Step 3: Rejoin. An item counts only if every one of its sentences was
    // translated; a half-translated line would read as a whole one.
    for (size_t text_idx = 0; text_idx < texts.size(); ++text_idx) {
        const size_t start = text_boundaries[text_idx];
        const size_t end = text_boundaries[text_idx + 1];
        if (start == end) continue;  // blank input

        bool all_ok = true;
        std::vector<std::string> parts;
        parts.reserve(end - start);
        for (size_t i = start; i < end; ++i) {
            if (!sentence_ok[i]) { all_ok = false; break; }
            parts.push_back(translated_sentences[i]);
        }
        if (all_ok) {
            out.texts[text_idx] = detail::join_sentences(parts, tgt_nllb);
        } else {
            ++out.failed_count;
        }
    }

    if (out.failed_count > 0) {
        Logger::debug("translate_batch: " + std::to_string(out.failed_count) + " of " +
                      std::to_string(non_blank) + " texts not translated" +
                      (out.cancelled ? " (cancelled)" : ""));
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> Translator::translate_multi_target(
    const std::string& text,
    const std::string& source_lang,
    const std::vector<std::string>& target_langs,
    const TranslationOptions& options)
{
    std::vector<std::pair<std::string, std::string>> results;
    results.reserve(target_langs.size());

    for (const auto& target : target_langs) {
        std::string translated = translate(text, source_lang, target, options);
        results.emplace_back(target, translated);
    }

    return results;
}

bool Translator::is_language_supported(const std::string& lang_code) const {
    return !detail::nllb_code_for(lang_code).empty();
}

bool Translator::supports_language_pair(const std::string& source,
                                        const std::string& target) const {
    return is_language_supported(source) && is_language_supported(target);
}

std::vector<TranslationLanguage> Translator::supported_languages() {
    return SUPPORTED_LANGUAGES;
}

std::string Translator::to_nllb_code(const std::string& code) {
    return detail::nllb_code_for(code);
}

std::string Translator::from_nllb_code(const std::string& nllb_code) {
    return detail::short_code_for_nllb(nllb_code);
}

bool Translator::is_loaded() const {
    return pimpl_ && pimpl_->loaded;
}

std::string Translator::device() const {
    return pimpl_ ? pimpl_->device_str : "unknown";
}

void Translator::shutdown() {
    if (pimpl_) {
        pimpl_->shutdown();
    }
}

void Translator::cancel() {
    if (pimpl_) {
        pimpl_->cancelled.store(true, std::memory_order_release);
        Logger::debug("Translator cancellation requested");
    }
}

void Translator::reset_cancel() {
    if (pimpl_) {
        pimpl_->cancelled.store(false, std::memory_order_release);
    }
}

bool Translator::is_cancelled() const {
    return pimpl_ && pimpl_->cancelled.load(std::memory_order_acquire);
}

} // namespace muninn
