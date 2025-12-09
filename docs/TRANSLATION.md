# Muninn Translation System

Muninn provides multi-language translation using Meta's NLLB-200 (No Language Left Behind) model, enabling translation between 20+ languages directly in C++ with GPU acceleration.

## Table of Contents

- [Quick Start](#quick-start)
- [Overview](#overview)
- [Setup](#setup)
- [Supported Languages](#supported-languages)
- [C++ API Reference](#c-api-reference)
- [Complete Examples](#complete-examples)
- [Command Line Usage](#command-line-usage)
- [Translation Options](#translation-options)
- [UTF-8 Output](#utf-8-output)
- [Thread Safety](#thread-safety)
- [Performance](#performance)
- [Troubleshooting](#troubleshooting)

## Quick Start

```cpp
#include <muninn/translator.h>

int main() {
    // Step 1: Create translator with model path, device, and compute type
    muninn::Translator translator(
        "models/nllb-200-distilled-600M",  // Path to converted NLLB model
        "cuda",                             // Device: "cuda" or "cpu"
        "float16"                           // Compute type: "float16", "int8", "float32"
    );

    // Step 2: Translate text (source_text, source_lang, target_lang)
    std::string result = translator.translate("Hello, world!", "en", "es");
    // result = "Hola, mundo!"

    return 0;
}
```

## Overview

The translation system uses:
- **NLLB-200**: Meta's multilingual translation model supporting 200+ languages
- **CTranslate2**: High-performance inference engine for transformer models
- **SentencePiece**: Subword tokenization for accurate text processing

### Key Features

- **Any-to-any translation** - Translate between any supported language pair (not just to/from English)
- Translate transcriptions to multiple languages simultaneously
- GPU-accelerated inference (CUDA)
- Batch translation with automatic chunking for stability and efficiency
- Thread-safe static methods for multi-threaded applications
- Proper UTF-8 support for all scripts (Cyrillic, CJK, Arabic, etc.)
- Seamless integration with Whisper transcription

## Setup

### Step 1: Install Python Dependencies (One-time)

```bash
pip install ctranslate2 transformers sentencepiece
```

### Step 2: Download and Convert NLLB Model

Run this command from your project root directory:

**Windows (PowerShell):**
```powershell
# Option 1: 600M model - Fast, ~1.2GB VRAM (recommended)
ct2-transformers-converter --model facebook/nllb-200-distilled-600M --output_dir models/nllb-200-distilled-600M --quantization float16

# Option 2: 1.3B model - Better quality, ~2.6GB VRAM
ct2-transformers-converter --model facebook/nllb-200-distilled-1.3B --output_dir models/nllb-200-distilled-1.3B --quantization float16
```

**Linux/macOS:**
```bash
# Option 1: 600M model - Fast, ~1.2GB VRAM (recommended)
ct2-transformers-converter \
    --model facebook/nllb-200-distilled-600M \
    --output_dir models/nllb-200-distilled-600M \
    --quantization float16

# Option 2: 1.3B model - Better quality, ~2.6GB VRAM
ct2-transformers-converter \
    --model facebook/nllb-200-distilled-1.3B \
    --output_dir models/nllb-200-distilled-1.3B \
    --quantization float16
```

### Step 3: Verify Model Files

After conversion, your model directory should contain these 4 files:
```
models/nllb-200-distilled-600M/
    config.json              <- Model configuration
    model.bin                <- Neural network weights (~1.2GB)
    shared_vocabulary.json   <- Token vocabulary
    sentencepiece.bpe.model  <- REQUIRED for proper tokenization
```

**Important**: If `sentencepiece.bpe.model` is missing, translation quality will be poor. Re-run the converter with an updated ctranslate2 package.

## Supported Languages

| Code | Language | Script | Code | Language | Script |
|------|----------|--------|------|----------|--------|
| `en` | English | Latin | `ru` | Russian | Cyrillic |
| `es` | Spanish | Latin | `uk` | Ukrainian | Cyrillic |
| `fr` | French | Latin | `bg` | Bulgarian | Cyrillic |
| `de` | German | Latin | `zh` | Chinese | Han |
| `it` | Italian | Latin | `ja` | Japanese | Japanese |
| `pt` | Portuguese | Latin | `ko` | Korean | Hangul |
| `nl` | Dutch | Latin | `ar` | Arabic | Arabic |
| `pl` | Polish | Latin | `he` | Hebrew | Hebrew |
| `ro` | Romanian | Latin | `hi` | Hindi | Devanagari |
| `sv` | Swedish | Latin | `bn` | Bengali | Bengali |
| `da` | Danish | Latin | `ta` | Tamil | Tamil |
| `no` | Norwegian | Latin | `ur` | Urdu | Arabic |
| `fi` | Finnish | Latin | `th` | Thai | Thai |
| `cs` | Czech | Latin | `vi` | Vietnamese | Latin |
| `hu` | Hungarian | Latin | `id` | Indonesian | Latin |
| `el` | Greek | Greek | `ms` | Malay | Latin |
| `tr` | Turkish | Latin | `fa` | Persian | Arabic |

## C++ API Reference

### Header File

```cpp
#include <muninn/translator.h>
```

### Class: `muninn::Translator`

#### Constructor

```cpp
Translator(
    const std::string& model_path,           // Path to NLLB model directory
    const std::string& device = "cuda",      // "cuda" or "cpu"
    const std::string& compute_type = "float16",  // "float16", "int8", "float32"
    int device_index = 0                     // GPU index for multi-GPU systems
);
```

**Parameters:**
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `model_path` | string | Yes | Path to the converted NLLB model directory |
| `device` | string | No | `"cuda"` for GPU (recommended), `"cpu"` for CPU-only |
| `compute_type` | string | No | `"float16"` (fast), `"int8"` (smaller), `"float32"` (precise) |
| `device_index` | int | No | GPU index if you have multiple GPUs |

**Example:**
```cpp
// GPU with float16 (recommended)
muninn::Translator translator("models/nllb-200-distilled-600M", "cuda", "float16");

// CPU fallback
muninn::Translator cpu_translator("models/nllb-200-distilled-600M", "cpu", "float32");

// Second GPU
muninn::Translator gpu1_translator("models/nllb-200-distilled-600M", "cuda", "float16", 1);
```

---

#### Method: `translate()`

Translate a single text from one language to another.

```cpp
std::string translate(
    const std::string& text,                 // Text to translate
    const std::string& source_lang,          // Source language code (e.g., "en")
    const std::string& target_lang,          // Target language code (e.g., "es")
    const TranslationOptions& options = {}   // Optional: translation parameters
);
```

**Parameters:**
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `text` | string | Yes | The text to translate |
| `source_lang` | string | Yes | 2-letter language code of the source (e.g., `"en"`, `"ja"`, `"de"`) |
| `target_lang` | string | Yes | 2-letter language code of the target (e.g., `"es"`, `"ru"`, `"zh"`) |
| `options` | TranslationOptions | No | Beam size, length penalty, etc. |

**Returns:** `std::string` - The translated text

**Examples:**
```cpp
// English to Spanish
std::string spanish = translator.translate("Hello, world!", "en", "es");
// Result: "Hola, mundo!"

// English to Japanese
std::string japanese = translator.translate("Hello, world!", "en", "ja");
// Result: "こんにちは、世界！"

// English to Russian
std::string russian = translator.translate("Hello, world!", "en", "ru");
// Result: "Привет, мир!"

// German to Japanese (any-to-any)
std::string jp_from_de = translator.translate("Guten Tag", "de", "ja");
// Result: "こんにちは"

// Chinese to Russian (any-to-any)
std::string ru_from_zh = translator.translate("你好世界", "zh", "ru");
// Result: "Привет мир"

// With custom options
muninn::TranslationOptions opts;
opts.beam_size = 5;
opts.repetition_penalty = 1.2f;
std::string result = translator.translate("Hello!", "en", "fr", opts);
```

---

#### Method: `translate_batch()`

Translate multiple texts at once (more efficient for bulk translation).

**Internal Chunking**: Batches are automatically processed in chunks of 8 segments to prevent GPU memory exhaustion and ensure stability with large transcriptions.

```cpp
std::vector<std::string> translate_batch(
    const std::vector<std::string>& texts,   // Texts to translate
    const std::string& source_lang,          // Source language code
    const std::string& target_lang,          // Target language code
    const TranslationOptions& options = {}   // Optional parameters
);
```

**Example:**
```cpp
std::vector<std::string> texts = {
    "Hello, how are you?",
    "The weather is nice today.",
    "Thank you very much."
};

auto translations = translator.translate_batch(texts, "en", "es");
// translations[0] = "Hola, ¿cómo estás?"
// translations[1] = "El tiempo es bueno hoy."
// translations[2] = "Muchas gracias."
```

**Note:** You can safely pass hundreds of segments to `translate_batch()` - the library handles chunking internally to prevent hangs or crashes.

---

#### Method: `translate_multi_target()`

Translate one text to multiple target languages at once.

```cpp
std::vector<std::pair<std::string, std::string>> translate_multi_target(
    const std::string& text,                     // Text to translate
    const std::string& source_lang,              // Source language code
    const std::vector<std::string>& target_langs, // Target language codes
    const TranslationOptions& options = {}       // Optional parameters
);
```

**Returns:** Vector of `(language_code, translated_text)` pairs

**Example:**
```cpp
auto results = translator.translate_multi_target(
    "Welcome to our application",
    "en",
    {"es", "fr", "de", "ja", "ru"}
);

for (const auto& [lang, text] : results) {
    std::cout << lang << ": " << text << "\n";
}
// Output:
// es: Bienvenido a nuestra aplicación
// fr: Bienvenue dans notre application
// de: Willkommen in unserer Anwendung
// ja: 私たちのアプリケーションへようこそ
// ru: Добро пожаловать в наше приложение
```

---

#### Method: `is_language_supported()`

Check if a language code is supported.

```cpp
bool is_language_supported(const std::string& lang_code) const;
```

**Example:**
```cpp
if (translator.is_language_supported("ja")) {
    std::cout << "Japanese is supported!\n";
}
```

---

#### Method: `supports_language_pair()`

Check if translation between two languages is supported.

```cpp
bool supports_language_pair(const std::string& source, const std::string& target) const;
```

**Example:**
```cpp
if (translator.supports_language_pair("en", "zh")) {
    std::cout << "English to Chinese is supported!\n";
}
```

---

#### Static Method: `supported_languages()`

Get a list of all supported languages.

```cpp
static std::vector<TranslationLanguage> supported_languages();
```

**Returns:** Vector of `TranslationLanguage` structs with `code`, `nllb_code`, and `name` fields.

**Example:**
```cpp
auto languages = muninn::Translator::supported_languages();
for (const auto& lang : languages) {
    std::cout << lang.code << " (" << lang.name << ")\n";
}
```

---

### Struct: `muninn::TranslationOptions`

```cpp
struct TranslationOptions {
    int beam_size = 4;              // Beam search width (1-10)
    float length_penalty = 1.0f;    // >1.0 = longer output, <1.0 = shorter
    int max_length = 256;           // Maximum output tokens
    float repetition_penalty = 1.0f; // >1.0 = discourage repetition
    int no_repeat_ngram_size = 0;   // Prevent n-gram repetitions (0 = disabled)
};
```

---

## Complete Examples

### Example 1: Basic Translation

```cpp
#include <muninn/translator.h>
#include <iostream>

int main() {
    // Initialize translator
    muninn::Translator translator("models/nllb-200-distilled-600M", "cuda", "float16");

    // Translate English to multiple languages
    std::string text = "The quick brown fox jumps over the lazy dog.";

    std::cout << "English: " << text << "\n";
    std::cout << "Spanish: " << translator.translate(text, "en", "es") << "\n";
    std::cout << "French:  " << translator.translate(text, "en", "fr") << "\n";
    std::cout << "German:  " << translator.translate(text, "en", "de") << "\n";
    std::cout << "Japanese:" << translator.translate(text, "en", "ja") << "\n";
    std::cout << "Russian: " << translator.translate(text, "en", "ru") << "\n";

    return 0;
}
```

### Example 2: Transcribe + Translate (FAST - Batch Method)

```cpp
#include <muninn/transcriber.h>
#include <muninn/translator.h>
#include <iostream>

int main() {
    // Load Whisper model for transcription
    muninn::ModelOptions model_opts;
    model_opts.model_path = "models/faster-whisper-large-v3-turbo";
    model_opts.device = muninn::DeviceType::CUDA;
    model_opts.compute_type = muninn::ComputeType::Float16;

    muninn::Transcriber transcriber(model_opts);

    // Transcribe the video
    muninn::TranscribeOptions opts;
    opts.language = "en";
    auto result = transcriber.transcribe("video.mp4", opts);

    // Load NLLB translator
    muninn::Translator translator("models/nllb-200-distilled-600M", "cuda", "float16");

    // IMPORTANT: Collect all segment texts first for batch translation
    // This is 5-10x faster than translating one segment at a time!
    std::vector<std::string> segment_texts;
    for (const auto& segment : result.segments) {
        segment_texts.push_back(segment.text);
    }

    // Batch translate all segments to Russian at once
    auto russian_translations = translator.translate_batch(segment_texts, result.language, "ru");

    // Output results
    for (size_t i = 0; i < result.segments.size(); ++i) {
        std::cout << "Original: " << result.segments[i].text << "\n";
        std::cout << "Russian:  " << russian_translations[i] << "\n\n";
    }

    return 0;
}
```

> **Performance Tip**: Always use `translate_batch()` when translating multiple texts.
> Calling `translate()` in a loop is significantly slower due to per-call overhead.

### Example 3: Any-to-Any Translation

```cpp
#include <muninn/translator.h>
#include <iostream>

int main() {
    muninn::Translator translator("models/nllb-200-distilled-600M", "cuda", "float16");

    // NLLB supports direct translation between ANY language pair
    // No need to go through English as a pivot!

    // German to Japanese
    std::cout << "DE->JA: " << translator.translate("Guten Morgen", "de", "ja") << "\n";
    // Output: おはようございます

    // French to Korean
    std::cout << "FR->KO: " << translator.translate("Bonjour le monde", "fr", "ko") << "\n";
    // Output: 안녕하세요 세계

    // Russian to Chinese
    std::cout << "RU->ZH: " << translator.translate("Привет мир", "ru", "zh") << "\n";
    // Output: 你好世界

    // Arabic to Spanish
    std::cout << "AR->ES: " << translator.translate("مرحبا بالعالم", "ar", "es") << "\n";
    // Output: Hola mundo

    return 0;
}
```

### Example 4: UTF-8 File Output

```cpp
#include <muninn/translator.h>
#include <fstream>
#include <iostream>

// Write UTF-8 BOM for editor compatibility
void write_utf8_bom(std::ofstream& file) {
    unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    file.write(reinterpret_cast<char*>(bom), 3);
}

int main() {
    muninn::Translator translator("models/nllb-200-distilled-600M", "cuda", "float16");

    // Open file in binary mode and write UTF-8 BOM
    std::ofstream out("translations.txt", std::ios::out | std::ios::binary);
    write_utf8_bom(out);

    std::string text = "Hello, world!";
    out << "English: " << text << "\n";
    out << "Russian: " << translator.translate(text, "en", "ru") << "\n";
    out << "Japanese: " << translator.translate(text, "en", "ja") << "\n";
    out << "Arabic: " << translator.translate(text, "en", "ar") << "\n";
    out << "Korean: " << translator.translate(text, "en", "ko") << "\n";
    out << "Chinese: " << translator.translate(text, "en", "zh") << "\n";

    out.close();
    std::cout << "Translations written to translations.txt\n";

    return 0;
}
```

## Command Line Usage

### Full Transcription + Translation Test

```bash
# Basic usage (output to console)
test_translation.exe <audio_file> [whisper_model] [nllb_model]

# With UTF-8 file output (recommended for non-ASCII languages)
test_translation.exe <audio_file> <whisper_model> <nllb_model> <output_file>

# Example
test_translation.exe video.mp4 models/faster-whisper-large-v3-turbo models/nllb-200-distilled-600M output.txt
```

### Quick UTF-8 Translation Test

```bash
# Test translation only (no transcription)
test_utf8_output.exe [nllb_model] [output_file]

# Example
test_utf8_output.exe models/nllb-200-distilled-600M translations.txt
```

## Translation Options

```cpp
muninn::TranslationOptions opts;

// Beam search width (1-10, higher = better quality, slower)
opts.beam_size = 4;  // Default: 4

// Length penalty (>1.0 = prefer longer, <1.0 = prefer shorter)
opts.length_penalty = 1.0f;  // Default: 1.0

// Maximum output tokens per segment
opts.max_length = 256;  // Default: 256

// Repetition penalty (>1.0 = discourage repetition)
opts.repetition_penalty = 1.0f;  // Default: 1.0

// Prevent n-gram repetitions (0 = disabled)
opts.no_repeat_ngram_size = 0;  // Default: 0

// Use options
std::string result = translator.translate(text, "en", "es", opts);
```

## UTF-8 Output

### The Problem

When running from PowerShell, console output may show garbled characters for non-ASCII scripts:
- Russian appears as: `╨ù╨┤╤Ç╨░...` instead of `Здравствуйте`
- Japanese appears as: `πüÖπü╣πüª...` instead of `こんにちは`

This happens because PowerShell's `Tee-Object` converts UTF-8 to UTF-16LE.

### The Solution

Use the file output parameter to write directly to a UTF-8 file:

```bash
# Instead of this (broken encoding):
test_translation.exe video.mp4 ... | tee output.txt

# Use this (correct UTF-8):
test_translation.exe video.mp4 models/whisper models/nllb output.txt
```

### Writing UTF-8 in Your Own Code

```cpp
#include <fstream>

// Write UTF-8 BOM for editor compatibility
void write_utf8_bom(std::ofstream& file) {
    unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    file.write(reinterpret_cast<char*>(bom), 3);
}

// Write translations to file
std::ofstream out("translations.txt", std::ios::out | std::ios::binary);
write_utf8_bom(out);

out << "English: Hello, world!\n";
out << "Russian: " << translator.translate("Hello, world!", "en", "ru") << "\n";
out << "Japanese: " << translator.translate("Hello, world!", "en", "ja") << "\n";

out.close();
```

## Thread Safety

The Translator class provides the following thread safety guarantees:

### Thread-Safe Operations
- `Translator::supported_languages()` - static, always safe
- `Translator::to_nllb_code()` - static, always safe
- `Translator::from_nllb_code()` - static, always safe (uses thread-safe lazy initialization)

### NOT Thread-Safe
- Instance methods (`translate()`, `translate_batch()`, etc.) are **not** thread-safe for concurrent calls on the same instance
- For multi-threaded applications, create one `Translator` instance per thread, or protect calls with external synchronization (mutex)

```cpp
// Option 1: One translator per thread (recommended)
void translation_thread(const std::string& model_path) {
    muninn::Translator translator(model_path, "cuda", "float16");
    // Use translator safely within this thread
}

// Option 2: Shared translator with mutex
std::mutex translator_mutex;
muninn::Translator shared_translator("models/nllb-200-distilled-600M");

std::string translate_safe(const std::string& text) {
    std::lock_guard<std::mutex> lock(translator_mutex);
    return shared_translator.translate(text, "en", "es");
}
```

## Performance

### Memory Usage

| Configuration | VRAM Usage |
|--------------|------------|
| Whisper large-v3-turbo alone | ~3.0 GB |
| NLLB 600M float16 alone | ~1.2 GB |
| **Both models loaded** | **~4.2 GB** |
| NLLB 1.3B float16 alone | ~2.6 GB |
| Whisper + NLLB 1.3B | ~5.6 GB |

Both models fit comfortably on an 8GB GPU.

### Translation Speed

On RTX 3080:
- Single sentence: ~50-100ms
- Batch of 8 sentences: ~150-300ms
- Batch of 100 sentences: ~1-2s (chunked internally)
- Full 20-minute transcription to 1 language: ~10-30s

### Batch Chunking

`translate_batch()` automatically processes texts in chunks of 8 to:
- Prevent GPU memory exhaustion with large batches
- Avoid hangs that can occur with very large batch sizes
- Provide graceful error recovery (failed chunks fall back to original text)

You don't need to manually chunk your batches - just pass all your texts and the library handles it.

### Tips for Better Performance

1. **Use batch translation** when translating multiple texts (5-10x faster than individual calls)
2. **Reuse the Translator instance** - model loading is expensive (~2-5 seconds)
3. **Use float16** compute type for best speed/quality balance
4. **Keep beam_size at 4** - higher values have diminishing returns
5. **Translate to one language at a time** - allows better batch optimization

## Troubleshooting

### "SentencePiece model not found"

```
[Muninn] SentencePiece model not found: models/nllb.../sentencepiece.bpe.model
[Muninn] Translation will use basic tokenization (may produce poor results)
```

**Solution**: Re-run the model conversion with a recent version of `ctranslate2`:
```bash
pip install --upgrade ctranslate2
ct2-transformers-converter --model facebook/nllb-200-distilled-600M ...
```

### "Unsupported language pair"

```
[Muninn] Unsupported language pair: xx -> yy
```

**Solution**: Check that both language codes are in the supported list. Use 2-letter ISO codes (`en`, `es`, `ja`), not NLLB codes.

### Garbled Characters in Output

**Symptoms**: `├¡├ñ` instead of `íä`, `╨ù╨┤╤Ç` instead of Cyrillic

**Solution**:
1. Use the file output parameter: `test_translation.exe ... output.txt`
2. Open the file in VS Code (automatically detects UTF-8 with BOM)
3. Don't pipe through PowerShell's `Tee-Object`

### Out of Memory

```
CUDA out of memory
```

**Solutions**:
1. Use the 600M model instead of 1.3B
2. Close other GPU applications
3. Use `compute_type = "int8"` for lower memory (slightly reduced quality)
4. Fall back to CPU: `Translator(model_path, "cpu", "float32")`

### Translation Hangs or Crashes

**Symptoms**: Application freezes during translation, eventually crashes or times out

**Cause**: This was a known issue in versions before 0.5.1 where large batches sent to CTranslate2 could exhaust GPU memory.

**Solution**: Update to the latest version. `translate_batch()` now automatically chunks large batches into groups of 8 segments, preventing GPU memory exhaustion. If you're on the latest version and still experiencing issues:
1. Ensure you're not calling `translate()` in a tight loop - use `translate_batch()` instead
2. Check for other GPU applications consuming memory
3. Try reducing `beam_size` in `TranslationOptions`

### Translation Quality Issues

**Symptoms**: Nonsensical output, repeated phrases, wrong language

**Solutions**:
1. Ensure SentencePiece model is loaded (check console output)
2. Increase `beam_size` to 5 or 6
3. Enable repetition penalty: `opts.repetition_penalty = 1.2f`
4. Try the larger 1.3B model for better quality

## Example Output

```
UTF-8 Translation Test
=====================

Original English:
Hello, this is a test of the translation system.

Spanish (es):
Hola, esta es una prueba del sistema de traducción.

German (de):
Hallo, das ist ein Test des Übersetzungssystems.

Japanese (ja):
こんにちは,これは翻訳システムのテストです.

Russian (ru):
Привет, это тест на систему перевода.

Korean (ko):
안녕하세요, 이것은 번역 시스템의 테스트입니다.

Arabic (ar):
مرحباً، هذا اختبار لنظام الترجمة.
```
