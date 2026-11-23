# Live translation to English

Whisper can translate speech into English as it transcribes. In streaming mode
that gives live English captions for speech in another language: set
`task = "translate"` in `StreamingOptions`. No extra model is needed.

For any other target language, transcribe first and translate the text with
`muninn::Translator` (NLLB-200), see [TRANSLATION.md](TRANSLATION.md).

## Which models can do it

- A multilingual Whisper model (tiny, base, small, medium, large-v2,
  large-v3). An English-only model (`*.en`) can't translate: streaming logs a
  warning and transcribes instead.
- **Not large-v3-turbo.** It was fine-tuned without translation data and
  mostly writes the original language even when asked to translate.

## Example

```cpp
#include <muninn/streaming_transcriber.h>

muninn::StreamingTranscriber stream("models/faster-whisper-large-v3", "cuda");

muninn::StreamingOptions options;
options.language = "auto";       // or the source language, e.g. "es"
options.task = "translate";      // English output

stream.start(options, [](const muninn::Segment& seg) {
    std::cout << "[" << seg.language << " -> en] " << seg.text << "\n";
    return true;
});

// From your audio thread: interleaved float samples, any rate and channel count.
stream.push_audio(samples, frames * channels, 48000, channels);

auto all = stream.stop();
```

`examples/translation_streaming.cpp` does this with an audio file played in
real time: `translation_streaming <model_dir> <audio_file> [cpu|cuda|auto]`.

## How it behaves

- **Language.** With `"auto"`, the language is detected once, on the first
  window that has speech, and kept for the session; every segment's
  `language` and `language_probability` report it. Code-switching between
  languages is not followed. `reset()` forgets the detected language. A
  language code the model doesn't know makes `start()` throw.
- **Words.** `seg.text` and `seg.words` are the English output, with word
  times from CTranslate2's alignment. Alignment of translated words to the
  source audio is rougher than for transcription, because the English words
  weren't spoken.
- **Latency.** The same as transcription: about one window
  (`chunk_length_s`, default 2 s) plus half the overlap (`overlap_s`, default
  0.5 s), plus the decode time. Every window is padded to 30 s before
  decoding, so very short windows cost as much GPU time as long ones.
  Longer windows give the model more context.
- **Everything else** (VAD, callbacks, polling, limits, thread rules) is as in
  [STREAMING_API.md](STREAMING_API.md).

## Limitations

- English is the only target.
- Proper nouns may be transliterated, and idioms come out literally.
- Quality depends on the source language and the model size; it hasn't been
  measured for Muninn.

## See also

- [STREAMING_API.md](STREAMING_API.md)
- [OBS_KARAOKE_CAPTIONS.md](OBS_KARAOKE_CAPTIONS.md)
- [TRANSLATION.md](TRANSLATION.md)
