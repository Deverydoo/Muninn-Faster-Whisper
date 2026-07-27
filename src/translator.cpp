#include "muninn/translator.h"
#include "muninn/logging.h"
#include <ctranslate2/translator.h>
#include <unordered_map>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>

#ifdef MUNINN_USE_SENTENCEPIECE
#include <sentencepiece_processor.h>
#endif

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// Language Mappings
// ═══════════════════════════════════════════════════════════════════════════

// Whisper/short code -> NLLB code
static const std::unordered_map<std::string, std::string> CODE_TO_NLLB = {
    // Latin-script European languages
    {"en", "eng_Latn"},      // English
    {"es", "spa_Latn"},      // Spanish
    {"fr", "fra_Latn"},      // French
    {"de", "deu_Latn"},      // German
    {"it", "ita_Latn"},      // Italian
    {"pt", "por_Latn"},      // Portuguese
    {"nl", "nld_Latn"},      // Dutch
    {"pl", "pol_Latn"},      // Polish
    {"ro", "ron_Latn"},      // Romanian
    {"sv", "swe_Latn"},      // Swedish
    {"da", "dan_Latn"},      // Danish
    {"no", "nob_Latn"},      // Norwegian (Bokmal)
    {"fi", "fin_Latn"},      // Finnish
    {"cs", "ces_Latn"},      // Czech
    {"hu", "hun_Latn"},      // Hungarian
    {"el", "ell_Grek"},      // Greek
    {"tr", "tur_Latn"},      // Turkish

    // Cyrillic languages
    {"ru", "rus_Cyrl"},      // Russian
    {"uk", "ukr_Cyrl"},      // Ukrainian
    {"bg", "bul_Cyrl"},      // Bulgarian
    {"be", "bel_Cyrl"},      // Belarusian

    // Asian languages
    {"zh", "zho_Hans"},      // Chinese (Simplified)
    {"ja", "jpn_Jpan"},      // Japanese
    {"ko", "kor_Hang"},      // Korean
    {"vi", "vie_Latn"},      // Vietnamese
    {"th", "tha_Thai"},      // Thai
    {"id", "ind_Latn"},      // Indonesian
    {"ms", "zsm_Latn"},      // Malay
    {"tl", "tgl_Latn"},      // Filipino/Tagalog

    // Middle Eastern / South Asian
    {"ar", "arb_Arab"},      // Arabic
    {"fa", "pes_Arab"},      // Persian (Farsi)
    {"he", "heb_Hebr"},      // Hebrew
    {"hi", "hin_Deva"},      // Hindi
    {"bn", "ben_Beng"},      // Bengali
    {"ta", "tam_Taml"},      // Tamil
    {"ur", "urd_Arab"},      // Urdu
};

// NLLB code -> short code (reverse mapping) - initialized thread-safely with std::call_once
static std::unordered_map<std::string, std::string> NLLB_TO_CODE;
static std::once_flag reverse_mapping_once;

// Initialize reverse mapping (guaranteed thread-safe by std::call_once)
static void init_reverse_mapping() {
    std::call_once(reverse_mapping_once, []() {
        for (const auto& pair : CODE_TO_NLLB) {
            NLLB_TO_CODE[pair.second] = pair.first;
        }
    });
}

// Language info for supported_languages()
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

// ═══════════════════════════════════════════════════════════════════════════
// Implementation
// ═══════════════════════════════════════════════════════════════════════════

class Translator::Impl {
public:
    std::unique_ptr<ctranslate2::Translator> model;
    std::string device_str;
    std::string model_path_;
    bool loaded = false;
    std::atomic<bool> shutdown_requested{false};
    std::atomic<bool> cancelled{false};

    // Mutex to protect model pointer access during load/unload/translate
    mutable std::mutex model_mutex;

#ifdef MUNINN_USE_SENTENCEPIECE
    sentencepiece::SentencePieceProcessor sp_processor;
    bool sp_loaded = false;
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

        std::lock_guard<std::mutex> lock(model_mutex);
        loaded = false;

        // Destroy CTranslate2 model first - this triggers ThreadPool shutdown
        // which can block on CUDA synchronization
        if (model) {
            std::cout << "[Muninn] Translator shutting down...\n";
            model.reset();
            std::cout << "[Muninn] Translator shutdown complete\n";
        }
    }

    Impl(const std::string& model_path,
         const std::string& device,
         const std::string& compute_type,
         int device_index)
        : device_str(device), model_path_(model_path)
    {
        init_reverse_mapping();

        try {
            // Determine device
            ctranslate2::Device ct_device;
            if (device == "cuda" || device == "CUDA") {
                ct_device = ctranslate2::Device::CUDA;
            } else {
                ct_device = ctranslate2::Device::CPU;
            }

            // Create translator
            model = std::make_unique<ctranslate2::Translator>(
                model_path,
                ct_device,
                ctranslate2::str_to_compute_type(compute_type),
                std::vector<int>{device_index}
            );

            loaded = true;
            std::cout << "[Muninn] Translator loaded: " << model_path
                      << " (device=" << device << ", compute=" << compute_type << ")\n";

#ifdef MUNINN_USE_SENTENCEPIECE
            // Load SentencePiece model from the same directory as the NLLB model
            std::filesystem::path sp_model_path = std::filesystem::path(model_path) / "sentencepiece.bpe.model";
            if (std::filesystem::exists(sp_model_path)) {
                auto status = sp_processor.Load(sp_model_path.string());
                if (status.ok()) {
                    sp_loaded = true;
                    std::cout << "[Muninn] SentencePiece tokenizer loaded: " << sp_model_path.string() << "\n";
                } else {
                    std::cerr << "[Muninn] SentencePiece load failed: " << status.ToString() << "\n";
                }
            } else {
                std::cerr << "[Muninn] SentencePiece model not found: " << sp_model_path.string() << "\n";
                std::cerr << "[Muninn] Translation will use basic tokenization (may produce poor results)\n";
            }
#endif

        } catch (const std::exception& e) {
            std::cerr << "[Muninn] Failed to load translator: " << e.what() << "\n";
            throw;
        }
    }

    // Tokenize text using SentencePiece (or fallback to whitespace)
    std::vector<std::string> tokenize(const std::string& text) {
        std::vector<std::string> tokens;

#ifdef MUNINN_USE_SENTENCEPIECE
        if (sp_loaded) {
            auto status = sp_processor.Encode(text, &tokens);
            if (status.ok()) {
                return tokens;
            }
            std::cerr << "[Muninn] SentencePiece encode failed: " << status.ToString() << "\n";
        }
#endif

        // Fallback: simple whitespace tokenizer
        std::istringstream iss(text);
        std::string word;
        while (iss >> word) {
            tokens.push_back(word);
        }
        return tokens;
    }

    // Detokenize using SentencePiece (or fallback to space-join)
    std::string detokenize(const std::vector<std::string>& tokens) {
#ifdef MUNINN_USE_SENTENCEPIECE
        if (sp_loaded) {
            std::string result;
            auto status = sp_processor.Decode(tokens, &result);
            if (status.ok()) {
                return result;
            }
            std::cerr << "[Muninn] SentencePiece decode failed: " << status.ToString() << "\n";
        }
#endif

        // Fallback: simple space-join
        std::string result;
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (i > 0) result += " ";
            result += tokens[i];
        }
        return result;
    }

    // Clean up NLLB output (remove language tokens, fix spacing)
    // NOTE: Using simple string operations instead of std::regex for performance
    // std::regex is extremely slow in C++ and was causing hangs
    std::string clean_output(const std::string& text, const std::string& target_nllb) {
        std::string result = text;

        // Remove target language token if it appears at the start
        if (result.find(target_nllb) == 0) {
            result = result.substr(target_nllb.length());
        }

        // Trim leading/trailing whitespace
        auto start = result.find_first_not_of(" \t\n\r");
        auto end = result.find_last_not_of(" \t\n\r");
        if (start != std::string::npos && end != std::string::npos) {
            result = result.substr(start, end - start + 1);
        } else if (start == std::string::npos) {
            return "";  // All whitespace
        }

        // Fix common spacing issues around punctuation using simple string operations
        // Remove space before punctuation: " ." -> "."
        std::string cleaned;
        cleaned.reserve(result.size());

        for (size_t i = 0; i < result.size(); ++i) {
            char c = result[i];

            // Skip space if next char is punctuation
            if (c == ' ' && i + 1 < result.size()) {
                char next = result[i + 1];
                if (next == '.' || next == ',' || next == '!' ||
                    next == '?' || next == ';' || next == ':') {
                    continue;  // Skip this space
                }
            }

            cleaned += c;

            // Add space after punctuation if followed by letter without space
            if ((c == '.' || c == ',' || c == '!' || c == '?' || c == ';' || c == ':') &&
                i + 1 < result.size()) {
                char next = result[i + 1];
                if ((next >= 'A' && next <= 'Z') || (next >= 'a' && next <= 'z')) {
                    cleaned += ' ';
                }
            }
        }

        return cleaned;
    }

    // Split text into sentences for better NLLB translation quality
    // NLLB-200 works best on individual sentences, not long paragraphs
    // This uses simple heuristics - not perfect but good enough for captions/subtitles
    std::vector<std::string> split_sentences(const std::string& text) {
        std::vector<std::string> sentences;
        if (text.empty()) return sentences;

        std::string current;
        current.reserve(text.size());

        for (size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            current += c;

            // Check for sentence-ending punctuation
            bool is_sentence_end = (c == '.' || c == '!' || c == '?');

            // Also check for Chinese/Japanese sentence enders
            // Note: These are multi-byte UTF-8, so we check the sequence
            if (i >= 2) {
                // Check for CJK sentence-ending punctuation (3-byte UTF-8)
                unsigned char b1 = text[i-2];
                unsigned char b2 = text[i-1];
                unsigned char b3 = c;
                // 。(U+3002) = E3 80 82, ！(U+FF01) = EF BC 81, ？(U+FF1F) = EF BC 9F
                if ((b1 == 0xE3 && b2 == 0x80 && b3 == 0x82) ||  // 。
                    (b1 == 0xEF && b2 == 0xBC && b3 == 0x81) ||  // ！
                    (b1 == 0xEF && b2 == 0xBC && b3 == 0x9F)) {  // ？
                    is_sentence_end = true;
                }
            }

            if (is_sentence_end) {
                // Check for common abbreviations that shouldn't end sentences
                // e.g., "Dr.", "Mr.", "vs.", etc.
                bool is_abbreviation = false;
                if (c == '.' && current.size() >= 3) {
                    // Check last 3-4 chars for common abbreviations
                    std::string tail = current.size() >= 4 ? current.substr(current.size() - 4) : current;
                    if (tail.find("Dr.") != std::string::npos ||
                        tail.find("Mr.") != std::string::npos ||
                        tail.find("Ms.") != std::string::npos ||
                        tail.find("vs.") != std::string::npos ||
                        tail.find("Jr.") != std::string::npos ||
                        tail.find("Sr.") != std::string::npos ||
                        tail.find("St.") != std::string::npos ||
                        tail.find("No.") != std::string::npos) {
                        is_abbreviation = true;
                    }
                }

                // Check if followed by lowercase letter (probably not sentence end)
                // e.g., "The item costs 3.5 dollars" - the "." in "3.5" isn't a sentence end
                if (c == '.' && i + 2 < text.size()) {
                    char after_space = text[i + 1] == ' ' ? text[i + 2] : text[i + 1];
                    if (after_space >= 'a' && after_space <= 'z') {
                        is_abbreviation = true;  // Not a real sentence end
                    }
                }

                if (!is_abbreviation) {
                    // Skip any trailing whitespace
                    while (i + 1 < text.size() && (text[i + 1] == ' ' || text[i + 1] == '\n')) {
                        i++;
                    }

                    // Trim and save sentence
                    auto start = current.find_first_not_of(" \t\n\r");
                    auto end = current.find_last_not_of(" \t\n\r");
                    if (start != std::string::npos && end != std::string::npos) {
                        sentences.push_back(current.substr(start, end - start + 1));
                    }
                    current.clear();
                }
            }
        }

        // Don't forget the last sentence if it doesn't end with punctuation
        if (!current.empty()) {
            auto start = current.find_first_not_of(" \t\n\r");
            auto end = current.find_last_not_of(" \t\n\r");
            if (start != std::string::npos && end != std::string::npos) {
                sentences.push_back(current.substr(start, end - start + 1));
            }
        }

        return sentences;
    }

    // Join translated sentences back together
    std::string join_sentences(const std::vector<std::string>& sentences) {
        std::string result;
        for (size_t i = 0; i < sentences.size(); ++i) {
            if (i > 0) result += " ";
            result += sentences[i];
        }
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

void Translator::unload_model(int timeout_ms) {
    if (!pimpl_) {
        return;
    }

    std::lock_guard<std::mutex> lock(pimpl_->model_mutex);
    if (!pimpl_->model) {
        return;
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
}

Translator::Translator(Translator&&) noexcept = default;
Translator& Translator::operator=(Translator&&) noexcept = default;

std::string Translator::translate(const std::string& text,
                                  const std::string& source_lang,
                                  const std::string& target_lang,
                                  const TranslationOptions& options) {
    auto results = translate_batch({text}, source_lang, target_lang, options);
    return results.empty() ? text : results[0];
}

std::vector<std::string> Translator::translate_batch(
    const std::vector<std::string>& texts,
    const std::string& source_lang,
    const std::string& target_lang,
    const TranslationOptions& options)
{
    std::cout << "[Muninn] translate_batch called: source=" << source_lang
              << ", target=" << target_lang << ", texts=" << texts.size() << "\n";
    std::cout.flush();

    {
        std::lock_guard<std::mutex> lock(pimpl_->model_mutex);
        if (texts.empty() || !pimpl_->loaded || !pimpl_->model) {
            std::cout << "[Muninn] translate_batch: empty texts or not loaded, returning originals\n";
            return texts;
        }
    }

    // Get NLLB codes
    std::string src_nllb = to_nllb_code(source_lang);
    std::string tgt_nllb = to_nllb_code(target_lang);

    std::cout << "[Muninn] NLLB codes: src=" << src_nllb << ", tgt=" << tgt_nllb << "\n";
    std::cout.flush();

    if (src_nllb.empty() || tgt_nllb.empty()) {
        std::cerr << "[Muninn] Unsupported language pair: " << source_lang
                  << " -> " << target_lang << "\n";
        return texts;
    }

    // Skip translation if source == target
    if (src_nllb == tgt_nllb) {
        std::cout << "[Muninn] translate_batch: source == target, returning originals\n";
        return texts;
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
        auto sentences = pimpl_->split_sentences(text);
        if (sentences.empty()) {
            // Text has no sentence structure, treat whole thing as one "sentence"
            all_sentences.push_back(text);
        } else {
            for (const auto& sentence : sentences) {
                all_sentences.push_back(sentence);
            }
        }
        text_boundaries.push_back(all_sentences.size());
    }

    std::cout << "[Muninn] Split " << texts.size() << " texts into "
              << all_sentences.size() << " sentences for better translation quality\n";
    std::cout.flush();

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
    std::vector<std::string> translated_sentences;
    translated_sentences.reserve(all_sentences.size());

    for (size_t chunk_start = 0; chunk_start < all_sentences.size(); chunk_start += CHUNK_SIZE) {
        // Check for cancellation
        if (pimpl_->cancelled.load(std::memory_order_acquire)) {
            std::cout << "[Muninn] Translation cancelled\n";
            // Return what we have so far, fill rest with originals
            for (size_t i = chunk_start; i < all_sentences.size(); ++i) {
                translated_sentences.push_back(all_sentences[i]);
            }
            break;
        }

        size_t chunk_end = std::min(chunk_start + CHUNK_SIZE, all_sentences.size());

        try {
            // Tokenize inputs for this chunk
            // NLLB format: [source_lang_token, tokens..., </s>]
            std::vector<std::vector<std::string>> tokenized_inputs;
            std::vector<std::vector<std::string>> target_prefixes;

            for (size_t i = chunk_start; i < chunk_end; ++i) {
                std::vector<std::string> tokens;
                tokens.push_back(src_nllb);  // NLLB requires source lang token first

                // Tokenize the sentence
                auto text_tokens = pimpl_->tokenize(all_sentences[i]);
                tokens.insert(tokens.end(), text_tokens.begin(), text_tokens.end());

                // Add EOS token at end of source
                tokens.push_back("</s>");

                tokenized_inputs.push_back(tokens);

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

            // Detokenize and clean results
            for (size_t i = 0; i < results.size(); ++i) {
                if (!results[i].hypotheses.empty()) {
                    std::string translated = pimpl_->detokenize(results[i].hypotheses[0]);
                    translated = pimpl_->clean_output(translated, tgt_nllb);
                    translated_sentences.push_back(translated);
                    // Debug: Log first translation
                    if (chunk_start == 0 && i == 0) {
                        std::cout << "[Muninn] First sentence translation:\n";
                        std::cout << "  Input: " << all_sentences[0].substr(0, 50) << "...\n";
                        std::cout << "  Output: " << translated.substr(0, 50) << "...\n";
                        std::cout.flush();
                    }
                } else {
                    // Fallback to original sentence
                    std::cout << "[Muninn] WARNING: Empty hypotheses for sentence " << (chunk_start + i) << ", using original\n";
                    translated_sentences.push_back(all_sentences[chunk_start + i]);
                }
            }

        } catch (const std::exception& e) {
            std::cerr << "[Muninn] Translation error in chunk " << (chunk_start / CHUNK_SIZE)
                      << ": " << e.what() << "\n";
            // On error, add original sentences for this chunk
            for (size_t i = chunk_start; i < chunk_end; ++i) {
                translated_sentences.push_back(all_sentences[i]);
            }
        }
    }

    // Step 3: Rejoin translated sentences back into original text structure
    std::vector<std::string> all_translations;
    all_translations.reserve(texts.size());

    for (size_t text_idx = 0; text_idx < texts.size(); ++text_idx) {
        size_t start = text_boundaries[text_idx];
        size_t end = text_boundaries[text_idx + 1];

        if (start >= translated_sentences.size()) {
            // Safety: if we don't have enough translations, use original
            all_translations.push_back(texts[text_idx]);
            continue;
        }

        // Collect all sentences for this original text
        std::vector<std::string> text_sentences;
        for (size_t i = start; i < end && i < translated_sentences.size(); ++i) {
            text_sentences.push_back(translated_sentences[i]);
        }

        // Join them back together
        all_translations.push_back(pimpl_->join_sentences(text_sentences));
    }

    return all_translations;
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
    return CODE_TO_NLLB.count(lang_code) > 0;
}

bool Translator::supports_language_pair(const std::string& source,
                                        const std::string& target) const {
    return is_language_supported(source) && is_language_supported(target);
}

std::vector<TranslationLanguage> Translator::supported_languages() {
    return SUPPORTED_LANGUAGES;
}

std::string Translator::to_nllb_code(const std::string& code) {
    auto it = CODE_TO_NLLB.find(code);
    return (it != CODE_TO_NLLB.end()) ? it->second : "";
}

std::string Translator::from_nllb_code(const std::string& nllb_code) {
    init_reverse_mapping();
    auto it = NLLB_TO_CODE.find(nllb_code);
    return (it != NLLB_TO_CODE.end()) ? it->second : "";
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
        std::cout << "[Muninn] Translator cancellation requested\n";
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
