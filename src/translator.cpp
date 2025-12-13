#include "muninn/translator.h"
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

    // Asian languages
    {"zh", "zho_Hans"},      // Chinese (Simplified)
    {"ja", "jpn_Jpan"},      // Japanese
    {"ko", "kor_Hang"},      // Korean
    {"vi", "vie_Latn"},      // Vietnamese
    {"th", "tha_Thai"},      // Thai
    {"id", "ind_Latn"},      // Indonesian
    {"ms", "zsm_Latn"},      // Malay

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

Translator::~Translator() = default;

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
    if (texts.empty() || !pimpl_->loaded) {
        return texts;
    }

    // Get NLLB codes
    std::string src_nllb = to_nllb_code(source_lang);
    std::string tgt_nllb = to_nllb_code(target_lang);

    if (src_nllb.empty() || tgt_nllb.empty()) {
        std::cerr << "[Muninn] Unsupported language pair: " << source_lang
                  << " -> " << target_lang << "\n";
        return texts;
    }

    // Skip translation if source == target
    if (src_nllb == tgt_nllb) {
        return texts;
    }

    // Set translation options
    ctranslate2::TranslationOptions ct_options;
    ct_options.beam_size = options.beam_size;
    ct_options.length_penalty = options.length_penalty;
    ct_options.repetition_penalty = options.repetition_penalty;
    ct_options.no_repeat_ngram_size = options.no_repeat_ngram_size;
    ct_options.max_decoding_length = options.max_length;

    // Process in chunks to avoid GPU memory issues and hangs
    // Chunk size of 8 is safe for most GPUs while still being efficient
    const size_t CHUNK_SIZE = 8;
    std::vector<std::string> all_translations;
    all_translations.reserve(texts.size());

    for (size_t chunk_start = 0; chunk_start < texts.size(); chunk_start += CHUNK_SIZE) {
        // Check for cancellation
        if (pimpl_->cancelled.load(std::memory_order_acquire)) {
            std::cout << "[Muninn] Translation cancelled\n";
            // Return what we have so far, fill rest with originals
            for (size_t i = chunk_start; i < texts.size(); ++i) {
                all_translations.push_back(texts[i]);
            }
            break;
        }

        size_t chunk_end = std::min(chunk_start + CHUNK_SIZE, texts.size());

        try {
            // Tokenize inputs for this chunk
            // NLLB format: [source_lang_token, tokens..., </s>]
            std::vector<std::vector<std::string>> tokenized_inputs;
            std::vector<std::vector<std::string>> target_prefixes;

            for (size_t i = chunk_start; i < chunk_end; ++i) {
                std::vector<std::string> tokens;
                tokens.push_back(src_nllb);  // NLLB requires source lang token first

                // Tokenize the text
                auto text_tokens = pimpl_->tokenize(texts[i]);
                tokens.insert(tokens.end(), text_tokens.begin(), text_tokens.end());

                // Add EOS token at end of source
                tokens.push_back("</s>");

                tokenized_inputs.push_back(tokens);

                // Target prefix: [</s>, target_lang_token] - NLLB decoder starts with </s>
                target_prefixes.push_back({"</s>", tgt_nllb});
            }

            // Translate this chunk
            auto results = pimpl_->model->translate_batch(
                tokenized_inputs,
                target_prefixes,
                ct_options
            );

            // Detokenize and clean results
            for (size_t i = 0; i < results.size(); ++i) {
                if (!results[i].hypotheses.empty()) {
                    std::string translated = pimpl_->detokenize(results[i].hypotheses[0]);
                    translated = pimpl_->clean_output(translated, tgt_nllb);
                    all_translations.push_back(translated);
                } else {
                    // Fallback to original text
                    all_translations.push_back(texts[chunk_start + i]);
                }
            }

        } catch (const std::exception& e) {
            std::cerr << "[Muninn] Translation error in chunk " << (chunk_start / CHUNK_SIZE)
                      << ": " << e.what() << "\n";
            // On error, add original texts for this chunk
            for (size_t i = chunk_start; i < chunk_end; ++i) {
                all_translations.push_back(texts[i]);
            }
        }
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
