# Translator design notes

How `muninn::Translator` works inside, and why. For the API, see
[TRANSLATION.md](TRANSLATION.md). (The first version of this file was the
2025 proposal; its sketch used whitespace tokens and returned the source text
on failure, and has been replaced by what was built.)

## Pipeline

```
Audio -> Transcriber (Whisper) -> segment texts -> Translator (NLLB-200) -> one list per target language
```

Translation is a separate step: `Transcriber` doesn't call the translator.
`Segment` has `translated_text` and `translation_target` fields for the caller
to fill in.

NLLB-200 was chosen over M2M-100 for its language coverage, its quality on
less common languages, and its distilled 600M and 1.3B models. CTranslate2 runs
it, the same engine as Whisper.

## One call

`translate_batch_result()` (the others wrap it):

1. Empty and whitespace-only inputs are set aside as `""`.
2. The language codes go to NLLB codes (`src/translate_text.cpp`): lower-cased,
   `_` to `-`, then the longest known prefix (`zh-hant-tw` -> `zh-hant`,
   `en-us` -> `en`). Region-specific entries (`zh-TW` -> `zho_Hant`) win over
   the base language. An unknown code fails every item.
3. Same source and target: the source texts are returned.
4. Each input is split into sentences (`split_sentences`), because NLLB
   translates single sentences much better than paragraphs.
5. Sentences are tokenized with SentencePiece as `[src_code, pieces..., </s>]`,
   with the target prefix `[</s>, tgt_code]`.
6. CTranslate2 gets 128 sentences per call with `max_batch_size` (default 32)
   and `BatchType::Examples`, so it can sort by length and batch them itself.
   Results come back in input order. Cancellation and unload requests are
   checked between these 128-sentence groups.
7. Each translated sentence is detokenized and cleaned per target language
   (`clean_output`), then the sentences of each input are joined
   (`join_sentences`: no space for Chinese, Japanese and Thai).
8. An input is translated only if all of its sentences are; otherwise it is
   `""` and counted in `failed_count`.

## Decisions

- **No silent fallbacks.** A failed, cancelled or unsupported item is `""`,
  never the source text: Loki Studio would otherwise save the original as the
  "translation". `translate_batch_result()` reports `cancelled`,
  `failed_count` and the first `error`.
- **No fallback tokenizer.** NLLB fed whitespace-split words produces
  nonsense, so a missing `sentencepiece.bpe.model` (or a build without
  SentencePiece) makes the constructor throw.
- **UTF-8 paths.** The model folder is opened with `std::filesystem::u8path`
  and the tokenizer is loaded from memory, so non-ASCII user names work.
  CTranslate2 opens its own files as UTF-8.
- **Compute type.** `"default"` is float16 on CUDA and float32 on CPU. Other
  types are resolved with CTranslate2's fallback, so float16 on the CPU
  becomes float32 instead of throwing.
- **`no_repeat_ngram_size = 3`** by default, to stop NLLB looping on a phrase.
- **Locking.** A batch holds a shared lock on the model for the whole decode;
  `unload_model()` and `shutdown()` take it exclusively. `unload_model()` sets
  a flag that stops the batch at its next group, then waits up to its timeout.
- **Unloading keeps the object.** As in the transcriber, unloading detaches the
  models and frees their GPU memory with the patched
  `Model::release_gpu_memory()` (see [CTRANSLATE2_CHANGES.md](CTRANSLATE2_CHANGES.md))
  rather than destroying the CTranslate2 translator, whose teardown can hang on
  Windows. A CPU translator doesn't touch CUDA when it unloads.

## Not done

- Translation inside `Transcriber::transcribe()`.
- Confidence scores for translations.
- Glossaries or terminology lists.
