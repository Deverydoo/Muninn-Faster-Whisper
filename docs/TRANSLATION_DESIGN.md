# Multi-Target Translation Feature Design

## Overview

Add support for translating transcriptions from **any language to any language** using Meta's NLLB (No Language Left Behind) model through CTranslate2.

This enables workflows like:
- English audio → Spanish subtitles
- Japanese audio → English + French + German subtitles
- Any of 15 major world languages ↔ Any other

## Model Selection: NLLB-200

**Recommended model:** `facebook/nllb-200-distilled-600M`

| Model | Size | Languages | Quality | Speed |
|-------|------|-----------|---------|-------|
| nllb-200-distilled-600M | ~1.2GB | 200 | Good | Fast |
| nllb-200-distilled-1.3B | ~2.6GB | 200 | Better | Medium |
| nllb-200-3.3B | ~6.6GB | 200 | Best | Slow |

NLLB advantages over M2M-100:
- More languages (200 vs 100)
- Better quality on low-resource languages
- Smaller distilled versions available
- Better maintained by Meta

## Supported Languages (Top 15)

| Code | Language | NLLB Code |
|------|----------|-----------|
| en | English | eng_Latn |
| es | Spanish | spa_Latn |
| fr | French | fra_Latn |
| de | German | deu_Latn |
| it | Italian | ita_Latn |
| pt | Portuguese | por_Latn |
| ru | Russian | rus_Cyrl |
| uk | Ukrainian | ukr_Cyrl |
| zh | Chinese (Simplified) | zho_Hans |
| ja | Japanese | jpn_Jpan |
| ko | Korean | kor_Hang |
| ar | Arabic | arb_Arab |
| hi | Hindi | hin_Deva |
| vi | Vietnamese | vie_Latn |
| th | Thai | tha_Thai |

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        Muninn Pipeline                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Audio ──► Whisper ──► Transcription ──► NLLB ──► Translation   │
│            (STT)         (source)        (MT)      (target)     │
│                                                                 │
│  Example:                                                       │
│  Japanese.mp4 ──► Whisper ──► 日本語テキスト ──► NLLB ──► English │
│                                             ├──► Spanish        │
│                                             └──► German         │
└─────────────────────────────────────────────────────────────────┘
```

## API Design

### New Types

```cpp
// include/muninn/translator.h

namespace muninn {

/**
 * @brief Language code for NLLB translation
 */
struct TranslationLanguage {
    std::string whisper_code;  // Whisper language code ("en", "ja", etc.)
    std::string nllb_code;     // NLLB language code ("eng_Latn", "jpn_Jpan", etc.)
    std::string name;          // Human-readable name
};

/**
 * @brief Translation options
 */
struct TranslationOptions {
    std::string target_language = "en";      // Target language (Whisper code)
    int beam_size = 4;                       // Beam search width
    float length_penalty = 1.0f;             // Length penalty
    int max_length = 256;                    // Max tokens per segment
    bool preserve_formatting = true;         // Keep punctuation style
};

/**
 * @brief Translation result for a segment
 */
struct TranslationResult {
    std::string source_text;      // Original transcription
    std::string translated_text;  // Translated text
    std::string source_language;  // Source language code
    std::string target_language;  // Target language code
    float confidence;             // Translation confidence (if available)
};

/**
 * @brief Text translator using NLLB model
 */
class Translator {
public:
    /**
     * @brief Initialize translator with NLLB model
     * @param model_path Path to CTranslate2-converted NLLB model
     * @param device "cuda" or "cpu"
     * @param compute_type "float16", "int8", etc.
     */
    Translator(const std::string& model_path,
               const std::string& device = "cuda",
               const std::string& compute_type = "float16");

    ~Translator();

    /**
     * @brief Translate text from source to target language
     * @param text Source text
     * @param source_lang Source language (Whisper code: "en", "ja", etc.)
     * @param target_lang Target language (Whisper code)
     * @param options Translation options
     * @return Translated text
     */
    std::string translate(const std::string& text,
                          const std::string& source_lang,
                          const std::string& target_lang,
                          const TranslationOptions& options = {});

    /**
     * @brief Translate multiple texts (batched for efficiency)
     */
    std::vector<std::string> translate_batch(
        const std::vector<std::string>& texts,
        const std::string& source_lang,
        const std::string& target_lang,
        const TranslationOptions& options = {});

    /**
     * @brief Check if a language pair is supported
     */
    bool supports_language_pair(const std::string& source,
                                const std::string& target) const;

    /**
     * @brief Get list of supported languages
     */
    static std::vector<TranslationLanguage> supported_languages();

    /**
     * @brief Convert Whisper language code to NLLB code
     */
    static std::string whisper_to_nllb(const std::string& whisper_code);

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace muninn
```

### Extended TranscribeOptions

```cpp
// Addition to TranscribeOptions in types.h

struct TranscribeOptions {
    // ... existing options ...

    // ═══════════════════════════════════════════════════════════
    // Post-Transcription Translation (NEW)
    // ═══════════════════════════════════════════════════════════
    bool enable_translation = false;           // Enable post-transcription translation
    std::string translation_target = "";       // Target language ("es", "fr", "de", etc.)
    std::string translation_model_path = "";   // Path to NLLB CTranslate2 model
    bool keep_original_text = true;            // Keep original in segment.text, translated in segment.translated_text
};
```

### Extended Segment

```cpp
// Addition to Segment in types.h

struct Segment {
    // ... existing fields ...

    // Translation (when enable_translation = true)
    std::string translated_text;       // Translated text (if translation enabled)
    std::string translation_language;  // Target language of translation
};
```

## Implementation

### translator.cpp

```cpp
#include "muninn/translator.h"
#include <ctranslate2/translator.h>
#include <unordered_map>

namespace muninn {

// Whisper code → NLLB code mapping
static const std::unordered_map<std::string, std::string> WHISPER_TO_NLLB = {
    {"en", "eng_Latn"},
    {"es", "spa_Latn"},
    {"fr", "fra_Latn"},
    {"de", "deu_Latn"},
    {"it", "ita_Latn"},
    {"pt", "por_Latn"},
    {"ru", "rus_Cyrl"},
    {"uk", "ukr_Cyrl"},
    {"zh", "zho_Hans"},
    {"ja", "jpn_Jpan"},
    {"ko", "kor_Hang"},
    {"ar", "arb_Arab"},
    {"hi", "hin_Deva"},
    {"vi", "vie_Latn"},
    {"th", "tha_Thai"},
};

class Translator::Impl {
public:
    std::unique_ptr<ctranslate2::Translator> model;
    std::string device;

    Impl(const std::string& model_path,
         const std::string& device,
         const std::string& compute_type)
        : device(device)
    {
        ctranslate2::Device ct_device = (device == "cuda")
            ? ctranslate2::Device::CUDA
            : ctranslate2::Device::CPU;

        model = std::make_unique<ctranslate2::Translator>(
            model_path,
            ct_device,
            ctranslate2::str_to_compute_type(compute_type)
        );
    }
};

Translator::Translator(const std::string& model_path,
                       const std::string& device,
                       const std::string& compute_type)
    : pimpl_(std::make_unique<Impl>(model_path, device, compute_type))
{}

Translator::~Translator() = default;

std::string Translator::whisper_to_nllb(const std::string& whisper_code) {
    auto it = WHISPER_TO_NLLB.find(whisper_code);
    if (it != WHISPER_TO_NLLB.end()) {
        return it->second;
    }
    // Default to English if unknown
    return "eng_Latn";
}

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
    if (texts.empty()) return {};

    // Convert to NLLB language codes
    std::string src_nllb = whisper_to_nllb(source_lang);
    std::string tgt_nllb = whisper_to_nllb(target_lang);

    // NLLB uses target language as prefix token
    std::vector<std::string> target_prefix = {tgt_nllb};

    // Tokenize input (simple whitespace for now, could use SentencePiece)
    std::vector<std::vector<std::string>> tokenized_inputs;
    for (const auto& text : texts) {
        std::vector<std::string> tokens;
        tokens.push_back(src_nllb);  // Source language token

        // Simple whitespace tokenization (NLLB uses SentencePiece internally)
        std::istringstream iss(text);
        std::string word;
        while (iss >> word) {
            tokens.push_back(word);
        }
        tokenized_inputs.push_back(tokens);
    }

    // Set translation options
    ctranslate2::TranslationOptions ct_options;
    ct_options.beam_size = options.beam_size;
    ct_options.length_penalty = options.length_penalty;
    ct_options.max_decoding_length = options.max_length;

    // Translate
    auto results = pimpl_->model->translate_batch(
        tokenized_inputs,
        {{tgt_nllb}},  // Target prefix for each input
        ct_options
    );

    // Detokenize results
    std::vector<std::string> translations;
    for (const auto& result : results) {
        if (!result.hypotheses.empty()) {
            std::string translated;
            for (const auto& token : result.hypotheses[0]) {
                if (!translated.empty()) translated += " ";
                translated += token;
            }
            translations.push_back(translated);
        } else {
            translations.push_back("");
        }
    }

    return translations;
}

std::vector<TranslationLanguage> Translator::supported_languages() {
    return {
        {"en", "eng_Latn", "English"},
        {"es", "spa_Latn", "Spanish"},
        {"fr", "fra_Latn", "French"},
        {"de", "deu_Latn", "German"},
        {"it", "ita_Latn", "Italian"},
        {"pt", "por_Latn", "Portuguese"},
        {"ru", "rus_Cyrl", "Russian"},
        {"uk", "ukr_Cyrl", "Ukrainian"},
        {"zh", "zho_Hans", "Chinese (Simplified)"},
        {"ja", "jpn_Jpan", "Japanese"},
        {"ko", "kor_Hang", "Korean"},
        {"ar", "arb_Arab", "Arabic"},
        {"hi", "hin_Deva", "Hindi"},
        {"vi", "vie_Latn", "Vietnamese"},
        {"th", "tha_Thai", "Thai"},
    };
}

bool Translator::supports_language_pair(const std::string& source,
                                        const std::string& target) const {
    return WHISPER_TO_NLLB.count(source) && WHISPER_TO_NLLB.count(target);
}

} // namespace muninn
```

## Usage Example

```cpp
#include <muninn/transcriber.h>
#include <muninn/translator.h>

int main() {
    // Load Whisper model
    muninn::ModelOptions model_opts;
    model_opts.model_path = "models/faster-whisper-large-v3-turbo";
    model_opts.device = muninn::DeviceType::CUDA;

    muninn::Transcriber transcriber(model_opts);

    // Load NLLB translator
    muninn::Translator translator(
        "models/nllb-200-distilled-600M",
        "cuda",
        "float16"
    );

    // Transcribe Japanese audio
    muninn::TranscribeOptions opts;
    opts.language = "ja";
    opts.word_timestamps = true;

    auto result = transcriber.transcribe("japanese_video.mp4", opts);

    // Translate to multiple languages
    for (auto& segment : result.segments) {
        std::string spanish = translator.translate(
            segment.text, "ja", "es"
        );
        std::string german = translator.translate(
            segment.text, "ja", "de"
        );

        std::cout << "Original (JA): " << segment.text << "\n";
        std::cout << "Spanish:       " << spanish << "\n";
        std::cout << "German:        " << german << "\n\n";
    }

    return 0;
}
```

## Model Setup

### Download and Convert NLLB Model

```bash
# Install converter (Python)
pip install ctranslate2 transformers sentencepiece

# Convert NLLB model to CTranslate2 format
ct2-transformers-converter \
    --model facebook/nllb-200-distilled-600M \
    --output_dir models/nllb-200-distilled-600M \
    --quantization float16

# For smaller/faster model with int8:
ct2-transformers-converter \
    --model facebook/nllb-200-distilled-600M \
    --output_dir models/nllb-200-distilled-600M-int8 \
    --quantization int8
```

### Directory Structure

```
models/
├── faster-whisper-large-v3-turbo/    # Whisper model
│   ├── model.bin
│   ├── config.json
│   └── ...
└── nllb-200-distilled-600M/          # NLLB translation model
    ├── model.bin
    ├── config.json
    ├── shared_vocabulary.json
    └── ...
```

## Performance Considerations

1. **GPU Memory**: Both Whisper and NLLB can share the same GPU
   - Whisper large-v3-turbo: ~3GB VRAM
   - NLLB 600M float16: ~1.2GB VRAM
   - Total: ~4.2GB (fits on most GPUs)

2. **Batching**: Translate segments in batches for efficiency
   - Group segments by similar length
   - Use batch_size=8-16 for optimal throughput

3. **Caching**: Cache translator instance across transcriptions
   - Don't reload model for each file
   - Share between Transcriber calls

## Future Enhancements

1. **Integrated Pipeline**: Add translation directly to `Transcriber::transcribe()`
2. **Multi-target**: Translate to multiple languages in one pass
3. **Streaming**: Real-time translation for streaming transcription
4. **Quality Estimation**: Return confidence scores for translations
5. **Terminology**: Support custom glossaries for domain-specific terms
