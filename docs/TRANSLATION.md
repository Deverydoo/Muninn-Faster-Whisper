# Translation (NLLB-200)

`muninn::Translator` translates text with Meta's NLLB-200 model, run by
CTranslate2 and tokenized with SentencePiece. It translates between any two
supported languages (not only to or from English). It is separate from
Whisper's own `task = "translate"`, which can only produce English.

Muninn uses it for subtitles: transcribe once, then translate the segment texts
into each target language.

## Quick start

```cpp
#include <muninn/translator.h>

muninn::Translator translator("models/nllb-200-distilled-600M");  // "cuda", compute "default"

std::string es = translator.translate("Hello, world!", "en", "es");

std::vector<std::string> lines = {"Hello, how are you?", "Thank you very much."};
std::vector<std::string> ru = translator.translate_batch(lines, "en", "ru");
```

`examples/translation_example.cpp` transcribes a file and translates it into
the languages you list:
`translation_example <whisper_model> <nllb_model> <audio> [lang ...]`.

## Getting a model

Convert NLLB with CTranslate2's converter (Python, one time) and copy the
SentencePiece model into the output folder:

```
pip install ctranslate2 transformers sentencepiece
ct2-transformers-converter --model facebook/nllb-200-distilled-600M ^
    --output_dir models/nllb-200-distilled-600M --quantization float16 ^
    --copy_files sentencepiece.bpe.model
```

The folder must contain `model.bin`, `config.json`, `shared_vocabulary.json`
and `sentencepiece.bpe.model`. Without `sentencepiece.bpe.model` the
constructor throws: there is no fallback tokenizer, because NLLB given
whitespace-split words produces nonsense. A Muninn build without SentencePiece
can't translate at all (the constructor throws).

The 1.3B distilled model (`facebook/nllb-200-distilled-1.3B`) is better and
slower, and needs about twice the memory.

## Languages

Language codes are Whisper-style short codes. Case and region are ignored when
only the base language is known: `"EN"`, `"en-US"` and `"en_us"` all mean
English. `zh-TW`, `zh-HK`, `zh-MO` and `zh-Hant` mean Traditional Chinese;
`zh` and `zh-CN` mean Simplified. `nb`, `iw`, `in` and `fil` are accepted as
Norwegian, Hebrew, Indonesian and Filipino. An NLLB code from the table
(`"fra_Latn"`) is also accepted as it is.

The translator maps 36 languages (plus Traditional Chinese):

| | | | |
|---|---|---|---|
| `en` English | `es` Spanish | `fr` French | `de` German |
| `it` Italian | `pt` Portuguese | `nl` Dutch | `pl` Polish |
| `ro` Romanian | `sv` Swedish | `da` Danish | `no` Norwegian (Bokmål) |
| `fi` Finnish | `cs` Czech | `hu` Hungarian | `el` Greek |
| `tr` Turkish | `ru` Russian | `uk` Ukrainian | `bg` Bulgarian |
| `be` Belarusian | `zh` Chinese | `ja` Japanese | `ko` Korean |
| `vi` Vietnamese | `th` Thai | `id` Indonesian | `ms` Malay |
| `tl` Filipino | `ar` Arabic | `fa` Persian | `he` Hebrew |
| `hi` Hindi | `bn` Bengali | `ta` Tamil | `ur` Urdu |

`Translator::supported_languages()` returns a shorter list of 23 of these, with
display names (the ones a language picker shows). `is_language_supported()`,
`supports_language_pair()` and `to_nllb_code()` use the full table.

## API

All of it is in `include/muninn/translator.h`.

### Constructor

```cpp
Translator(const std::string& model_path,           // UTF-8
           const std::string& device = "cuda",      // "cuda" or "cpu"
           const std::string& compute_type = "default",
           int device_index = 0);
```

- `model_path` is UTF-8, so folders with non-ASCII names (`C:\Users\José\...`)
  work.
- `compute_type` `"default"` is float16 on CUDA and float32 on CPU. Other values
  (`"float16"`, `"int8"`, `"int8_float16"`, `"float32"`, ...) fall back to the
  nearest type the device supports, so float16 on the CPU becomes float32 (with
  a log line) instead of failing.
- Any device string other than `"cuda"` (any case) means CPU.
- Throws `std::runtime_error` if the folder, the tokenizer or the model can't be
  loaded.

### Translating

```cpp
std::string translate(text, source_lang, target_lang, options = {});
std::vector<std::string> translate_batch(texts, source_lang, target_lang, options = {});
TranslationBatchResult translate_batch_result(texts, source_lang, target_lang, options = {});
std::vector<std::pair<std::string, std::string>>
    translate_multi_target(text, source_lang, target_langs, options = {});
```

Results come back in input order, one per input. An entry is `""` when:

- the input was empty or only whitespace (it never reaches the model);
- it failed (a decode error, no hypothesis);
- the batch was cancelled before it was done;
- either language isn't supported (then every entry is `""`).

A failed entry is never the untranslated source text. The one exception is
source and target being the same language: then the source is returned.

Each input is split into sentences, the sentences are translated, and the
results are joined again (with no space for Chinese, Japanese and Thai
targets). An entry counts only when all of its sentences were translated. The
splitter doesn't break at decimals (`3.5`), dots inside words (`file.txt`),
initials (`U.S.`, `e.g.`) or common abbreviations (`Mr.`, `Dr.`, `etc.`).
The output is tidied per target language: a stray space before punctuation is
removed, French keeps its space before `; : ! ?`, and URLs, paths, file names
and numbers are left alone.

`translate_batch_result()` tells you what happened:

```cpp
struct TranslationBatchResult {
    std::vector<std::string> texts;
    bool cancelled = false;   // cancel() or unload_model() stopped the batch early
    size_t failed_count = 0;  // non-empty inputs left ""
    std::string error;        // the first error, "" if none
};
```

`translate()` and `translate_batch()` return just the texts.
`translate_multi_target()` calls `translate()` once per target and returns
`(code, text)` pairs; for many lines and several languages, call
`translate_batch()` once per language instead.

### Options

```cpp
struct TranslationOptions {
    int beam_size = 4;
    float length_penalty = 1.0f;
    int max_length = 256;             // max output tokens per sentence
    float repetition_penalty = 1.0f;
    int no_repeat_ngram_size = 3;     // stops NLLB repeating itself
    int max_batch_size = 32;          // sentences CTranslate2 decodes together
};
```

`translate_batch()` hands CTranslate2 up to 128 sentences at a time, and
CTranslate2 sorts them by length and decodes them in batches of
`max_batch_size`. Lower `max_batch_size` if GPU memory is tight. Cancellation
is checked between the 128-sentence groups.

### Cancelling and unloading

- `cancel()` (any thread) stops a running batch after its current group of
  sentences; unfinished entries come back `""` with `cancelled = true`. The flag
  stays set, and new batches return at once, until `reset_cancel()`.
- `unload_model(timeout_ms = 3000)` frees the model's memory. If a batch is
  running, it is asked to stop at its next group and `unload_model()` waits up
  to `timeout_ms` for it (negative = no limit). It returns `false`, having
  released nothing, if the batch still held the model when time ran out. After
  an unload the translator can't be used again; later calls return `""` with
  the error "translator is not loaded". Calling it twice is safe.
- `shutdown()` destroys the CTranslate2 model. The destructor unloads with a
  5 s timeout.
- `is_loaded()` and `device()` (`"cuda"` or `"cpu"`) report the state.

### Threads

- The static functions (`supported_languages`, `to_nllb_code`,
  `from_nllb_code`) are safe from any thread.
- `translate_batch()` holds a shared lock on the model for the whole decode;
  `unload_model()` and `shutdown()` take it exclusively, so unloading waits for
  a running batch instead of freeing the model under it.
- `cancel()` affects every batch running on that translator.
- Don't destroy a `Translator` while another thread is inside one of its
  methods.

## Logging and console output

Muninn logs through `muninn::Logger` (stderr by default, or your callback); it
never writes to stdout. Translations are UTF-8 strings. If non-ASCII text looks
garbled in a Windows console, the console code page is the problem, not the
text: write to a file, or call `SetConsoleOutputCP(CP_UTF8)` before printing.

## Troubleshooting

- **"SentencePiece model not found"**: copy `sentencepiece.bpe.model` from the
  Hugging Face model into the converted folder (or reconvert with
  `--copy_files sentencepiece.bpe.model`).
- **"unsupported language pair"** in `error`: one of the codes isn't in the
  table above.
- **Out of GPU memory**: lower `max_batch_size`, use `"int8_float16"`, use the
  600M model, or run on the CPU.
- **Repeated phrases**: keep `no_repeat_ngram_size` at 3, or raise
  `repetition_penalty` a little (1.1 to 1.2).
