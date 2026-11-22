# Muninn Code Audit, November 2026

The first full audit since the thread-safety review of December 2024. Four reviewers worked in parallel, all read-only:

1. The offline transcription core: transcriber, VAD, mel spectrogram, audio extraction.
2. Parity with faster-whisper master (6cb639e, Nov 12 2026).
3. The CTranslate2 4.8.3 → 4.9.1 update.
4. Streaming, translation, diarization, subtitle export, the DLL interface and the docs.

I checked the high-severity findings in the code myself. One reviewer's claim was wrong and is left out: offline transcription does follow the model's mel band count (`transcriber.cpp:2195`); only streaming is fixed at 128.

"Loki" marks what Loki Studio's GPU worker actually runs: `Transcriber` with `sequential=true`, `word_timestamps=true`, beam 10, patience 2, Silero or energy VAD, plus `Translator`. Paths are under `src/` unless given.

## What matters most

1. **The audio features the model reads are computed wrongly (Loki).** `mel_spectrogram.cpp` departs from Whisper's feature extractor in four ways:
   - the filterbank has no Slaney area normalisation (`:105-119`);
   - the Hann window is symmetric, not periodic (`:42`);
   - the log floor is a fixed −8 instead of "loudest value − 8" (`:271`);
   - frames aren't centred with reflect padding.

   Two reviewers measured it independently against faster-whisper's extractor on the same audio. The mean difference was 0.37–0.42 on a scale of about −0.75 to 1.25, and filter peaks were 23x–179x too large. Every window of every transcription is decoded from features the model wasn't trained on. The model copes (WER 1.9–3.1% against faster-whisper on two of the three test clips), but this is the biggest accuracy item in the list.
2. **Muninn-only filters delete real speech (Loki).** `is_hallucination` drops whole segments that faster-whisper keeps:
   - one word filling half the segment ("go go go go", "run, run, run!"), `:1041-1068`;
   - a repeated three-word phrase ("oh my god oh my god"), `:1072-1091`;
   - six identical characters ("Noooooo!", "AAAAAH"), `:1127-1146`;
   - any short segment in a window under −1 average log-probability, `:1028-1031`.

   None of these checks confidence. This is gaming commentary, so they cut exactly the reactions it's full of. Temperature fallback already handles real loops.
3. **Failures are reported as success (Loki).** `transcribe_chunk` catches every exception and returns nothing (`:1699`). Track-level failures just `continue` (`:3196`, `:3210`, `:3279`). There is no error field. A CUDA out-of-memory error while a game holds VRAM gives "Transcription complete, 0 segments".
4. **Loki's silence setting means something else in Muninn (Loki).** Loki's spinbox (0–10, "seconds") feeds `filter_silence_hallucinations`, which reads it as the minimum fraction of a segment that must be speech (`:1258`). Any value of 1.0 or more deletes every segment on VAD tracks.
5. **Text is lost at window edges (Loki).**
   - The next window's context includes text that was filtered out, and the unfinished last segment (`:1549-1554`).
   - The unfinished last segment is dropped in the final window, and in any audio of 30 s or less (`:942-949`, `:2858-2862`, `:2977`).
   - A window whose only finished segment ended in its first second skips up to 29 s.
6. **Settings Loki sends that never arrive (Loki).**
   - The compute type, CPU thread count and GPU index are ignored: the model is built as `Whisper(path, device)`, `:2150`. CPU uses CTranslate2's default of 4 threads.
   - Track hints (mic/game/music) are never passed to VAD auto-detection (`:2404`).
7. **Subtitle export drops words and merges cues across minutes.**
   - `split_text` throws away everything after the second line (`subtitle_export.cpp:410`), and the same bug sits in every language's splitter.
   - With the default `gap_threshold` of 0.1 s, touching segments chain into cues minutes long, across tracks too.
8. **Streaming isn't usable as shipped.**
   - It has no GPT-2 byte decoding (`streaming_transcriber.cpp:30-63`), so non-ASCII text is garbled. That is the bug fixed for offline in `GPT2_BPE_ENCODING_FIX.md`.
   - An exception in the worker thread kills the process.
   - Its mel is fixed at 128 bands.
   - `push_audio` blocks for a whole decode.
   - Chunks aren't padded to 30 s.
   - Timestamps use the total pushed audio.
   - "auto" is forced to English.
9. **Translation on non-ASCII model paths (Loki).** `translator.cpp:186-198` builds `std::filesystem::path` from UTF-8 bytes, which MSVC reads as ANSI. A user named José or 田中 loses the SentencePiece tokenizer and gets nonsense NLLB output, with no error.
10. **CTranslate2 4.9.1 is worth taking**, but 4.9 dropped FindCUDA:
    - Our `CUDA_ARCH_LIST`/`CUDA_NVCC_FLAGS` would be ignored silently, giving one GPU generation.
    - The ABI changes, so muninn.dll must be rebuilt.
    - The local patch needs a rebase in `replica_pool.h`.
    - Relevant fixes: FP32 attention scores for alignment (word timings), FP32 QKᵀ accumulation, and the caching allocator outliving its thread.

## Findings

### Transcription core (Loki)

| # | Finding | Where | Fix |
|---|---|---|---|
| T1 | Mel features differ from Whisper's (above). | `mel_spectrogram.cpp` | Port `feature_extractor.py` exactly. |
| T2 | Muninn-only repetition and low-confidence filters (above). Rule 6 (compression in a low-confidence window) is stricter than faster-whisper too. | `transcriber.cpp:1028-1177` | Remove rules 4–6 and the short-segment rule. Keep the prompt-echo filter (deliberate). Keep the CJK no-space check only for text without spaces. |
| T3 | Errors swallowed (above). | `:1699`, `:3196-3283` | Count failed windows and tracks and report them in the result. Throw when nothing succeeded. LokiWorker sends `TRANSCRIPTION_FAILED`. |
| T4 | Silence threshold semantics (above). | `:1258`; Loki `ApplicationSettingsView.qml:2335` | Keep Muninn's calibrated meaning (a fraction, 0.1 = owner's tuned value) and clamp it to 0–1 in Muninn. Change Loki's control to a 0–1 fraction with matching wording. |
| T5 | Context includes filtered and unfinished text. | `:1549-1554`, `:2847` | Feed only the tokens of kept segments; none for a no-speech window. |
| T6 | Unfinished text dropped in the final window and in audio ≤ 30 s. A short first segment skips the window. | `:942-949`, `:2856-2863`, `:2977` | Return the last closed timestamp from the raw tokens. Seek there in every window. Run the loop for any length. |
| T7 | `<|2.40|><|3.10|> text` starts at 2.40 instead of 3.10. | `:821-895` | When text is empty, a timestamp moves the start. |
| T8 | VAD timestamp restore treats starts and ends alike. Words are mapped one by one, so a word on a join spans the silence. | `:47-72`, `:2946-2960` | Port `SpeechTimestampsMap` with `is_end`, words by their midpoint, segment = span of its words. Then drop the `tighten_to_words` workaround. |
| T9 | Compute type, threads and device index ignored. | `:2150-2174`, `:2243-2256` | Pass `ComputeType` and `ReplicaPoolConfig`. float16 on CPU becomes float32. Loki maps `int8_float16`. |
| T10 | Track hints unused; `skip_silent_tracks` unread. | `:2404` | Pass `track_hints[track]`; honour `skip_silent_tracks`. |
| T11 | Word splitting tests letters byte by byte, so non-ASCII words merge ("une école" → "uneécole"). No punctuation merging. Alignment gets only the start token. | `:366-384`, `:1583`, `:1847` | Port `split_tokens_on_spaces`, `merge_punctuations`, and sot + language + task for alignment. |
| T12 | An unknown language code becomes the unknown token, not an error. `.en` models get language tokens. "auto" can reach the decoder as `<|auto|>`. | `:1392`, `:1424`, `:2681` | Check `token_to_id`; throw for an unsupported code; `.en` models use English with no language token. |
| T13 | No check of config.json keys: a missing `suppress_ids` silently suppresses nothing. | model load | Check `lang_ids`, `suppress_ids`, `suppress_ids_begin`, and `alignment_heads` when word timestamps are on. |
| T14 | Temperature fallback samples with beam 10 / patience 2 and returns the best attempt's temperature, so the context isn't reset after a loop. | `:1449-1463`, `:1538-1548` | faster-whisper's way: beam 1 with `best_of` 5 above T=0; report the final temperature. |
| T15 | The initial prompt is re-inserted whenever the context is empty, is tokenized by longest match rather than BPE, and isn't trimmed. | `:1298-1342`, `:1404-1418` | A faster-whisper-style rolling token list. A real BPE tokenizer from `tokenizer.json`. |
| T16 | Silero always keeps a 2 s preamble, so a track with no speech still decodes 2 s (a likely source of the "Icarus, Tusker." echo). | `silero_vad.h:19`, `silero_vad.cpp:279-301`, `vad.cpp:189` | No preamble when nothing was detected; keep the warm-up only around real speech. |
| T17 | Language detection: no padding to 3000 frames, first 30 s only, track 0 (game audio) can decide, failure silently becomes "en". | `:174-226`, `:2681-2710`, `:3261` | Pad; detect from several windows, mic track first, majority vote; log failure. |
| T18 | Cancelled runs return timestamps on the wrong timeline (before the VAD remap and clip offset). A `cancel()` before the run starts is lost. Extraction, VAD and mel can't be cancelled. | `:2831-2936`, `:2604`, `:3102` | Break out and post-process. Reset the flag at entry under the same rule the docs state. Check it in extraction, VAD and mel. |
| T19 | With `concurrent_pipeline` (off by default), one track's prep error skips every later track. | `:3189-3213` | Launch the next prep before `get()`. |
| T20 | `clear_cache()` locks before the null check. `model_mutex` isn't held during inference, so `unload_model()` alongside `transcribe()` is a use-after-free. | `:3469`, `:105` | A shared lock held during inference; null check first. |
| T21 | Audio decoding: frame timestamps ignored (gaps shift later text), a failed stream shifts every later track number, decoder not drained at EOF, `AV_NOPTS_VALUE` duration goes negative. | `audio/audio_decoder.cpp:71-82`, `:177`, `:317-361` | Timestamp-aware assembly, a placeholder for failed streams, drain at EOF, guard the duration. |
| T22 | Peak memory: all tracks held at once, `clipped_samples` copied without a clip, a full second mel copy in sequential mode, mel as vector of vectors. About 4–5 GB for 3 h × 3 tracks. | `:2378`, `:2790-2804`, `:3093` | Skip the copies; flat mel buffer; tracks one at a time. |
| T23 | Muninn prints to `std::cout`, which is LokiWorker's JSON channel. | transcriber, translator, streaming, diarization | Everything through `Logger`. |
| T24 | `Segment.id` never set; `Segment.compression_ratio` holds the old tokens-per-character value. | `:1657`, `:1941` | Set both properly. |
| T25 | Silero min silence forced to 100 ms above 200 ms; the max-speech split ignores pauses; a Silero runtime failure reuses its threshold as an RMS floor. | `:2410`, `:2454`, `:2473`, `silero_vad.cpp:220-227` | Honour the setting; split at the quietest point (#1386); use the energy default on fallback. |
| T26 | The batched path (not used by Loki) ignores the initial prompt, pads to the longest chunk not 30 s, doesn't check `sequences_ids[0]`, and has no temperature fallback. | `:1729-1934` | Pad to 3000; check results; prompt; fallback. |
| T27 | TEN VAD feeds raw audio to a model expecting features and state, so it always throws and falls back. Loki already turns it off. | `ten_vad.cpp:178-212` | Remove TEN from the options until it's implemented properly, and say so. |

### CTranslate2

| # | Finding | Fix |
|---|---|---|
| C1 | 4.9.1 available. It brings Whisper-relevant fixes (#2143, #2131, #2136, #2097/#2128). | Update, with the rebased patch, `CMAKE_CUDA_ARCHITECTURES=75-real;…;120-real`, `CMAKE_CUDA_RUNTIME_LIBRARY=Shared`, `-T cuda=v13.2`. Check with cuobjdump, dumpbin and compare.py, plus a word-timing diff. |
| C2 | The `primitives.cu` hunk of the local patch is described as "CUDA 13 changes". It actually switches float32 GEMM to `CUBLAS_GEMM_DEFAULT_TENSOR_OP`, which may let cuBLAS round inputs down. | Measure with it and without it on the harness; keep whichever is more accurate, and describe it truthfully. |
| C3 | The `.gitmodules` submodule is the old 4.7.1 tree. CMake defaults to an untracked folder. The `rebuild_*.ps1` scripts are stale (4.7.1, sm_60, `FFMPEG_STATIC=ON`). | Remove the submodule and the stale scripts; one documented script for the current tree. |

### Translator (Loki)

| # | Finding | Where | Fix |
|---|---|---|---|
| X1 | Non-ASCII model path loses the tokenizer and gives nonsense silently (above). | `translator.cpp:186-198` | `u8path`; throw when the tokenizer is missing. |
| X2 | Any failure, cancellation or unmapped language returns the source text, which Loki saves as the "translation". | `:485-502`, `:570-642`; Loki `LokiWorker.cpp:408` | Return "" for a failed item, plus a cancelled flag. Normalise `xx-YY` and case. |
| X3 | Empty and whitespace-only strings go to NLLB. | `:531-536` | Pass them through as "". |
| X4 | The sentence splitter breaks "3.5" and "e.g."; output cleanup removes French spaces before `;:!?` and adds one in "file.txt". | `:280-298`, `:357-362` | Digit.digit and abbreviations aren't boundaries; punctuation fixes per language. |
| X5 | The default compute type float16 throws on CPU. | `:166-178` | float32 on CPU; `enable_fallback`. |
| X6 | `translate_batch` uses the model without the lock; the unload timeout is unused. | `:421`, `:483-489`, `:607` | Shared lock during decode. |

### Streaming

| # | Finding | Where | Fix |
|---|---|---|---|
| S1 | No GPT-2 byte decoding (non-ASCII garbled). | `streaming_transcriber.cpp:30-63` | Share the offline decoder. |
| S2 | Worker exceptions call `std::terminate`. Mel fixed at 128 bands. | `:75`, `:357-365`, `:505-517` | try/catch with error reporting; mel from `n_mels()`. |
| S3 | `push_audio` blocks for a whole decode. The callback runs under the lock (re-entry deadlocks). | `:359-362`, `:606-613` | Decode outside the lock; short-lock ring buffer; callback without the lock. |
| S4 | Chunks not padded to 30 s. | `:458-473` | Pad to 3000 frames. |
| S5 | Timestamps from total pushed audio; the overlap is decoded twice; the flush decodes the overlap alone. | `:377-399`, `:575-625` | An absolute sample offset; drop or prompt with the overlap's tokens. |
| S6 | "auto" forced to English; word timestamps, compute type, device index, threads, `include_partial` and `batch_size` ignored; Auto never falls back to CPU. | `:81-89`, `:313-321`, `:479`, `:590`, `:674-694` | Detect on the first speech; pass options through; implement words or remove the option. |
| S7 | Its compression-ratio and log-prob filters can never trigger (wrong formulas). | `:553-571` | The offline formulas. |
| S8 | More than 2 channels treated as mono; `sample_rate <= 0` loops forever; nearest-neighbour resampling with drift; unbounded segment queue. | `:268-290`, `:595` | Validate; a phase-keeping resampler; cap the queue. |

### Subtitle export and captions

| # | Finding | Where | Fix |
|---|---|---|---|
| E1 | Words past the line limit dropped; line length in bytes; cues merged across minutes and tracks; entries not sorted. | `subtitle_export.cpp:210-262`, `:389-430`, and every `split_*_text` | Never drop words; code points; `gap_threshold` 0; cap merges; same track only; sort. |
| E2 | Non-ASCII output paths fail or produce garbled names. | `:78`, `:142`, `:370-386`, `:3549`, `:3618` | `u8path` everywhere. |
| E3 | Karaoke cues overlap by about 1.6 s. Min-duration padding overlaps the next cue in every segmenter. | `:817-933`, `:218`, `:1322`, `:1771`, `:3060`, `:3433` | One final pass clamping each end to the next start. |
| E4 | Korean syllable karaoke spaces every syllable. | `:1298-1378` | Spaces only at original word boundaries. |
| E5 | No HTML/WebVTT escaping ("<3", "AT&T", "-->"). | `word_styling.h:174`, `diarization.cpp:583`, `:319-343` | Escape. |
| E6 | Timestamps truncated, not rounded; negative and NaN inputs. | `:287-301`, `:348-362` | `llround`, clamp, integer maths. |
| E7 | The caption engine: truncation, overlaps, no engines until `registerDefaultEngines()`, a race on the lazy fallback, overrides that can't be expressed, options read by nothing. | `caption_engine_base.cpp`, `caption_router.cpp`, `caption_options.cpp` | Never truncate; register in the constructor; eager fallback; `std::optional` overrides; implement or remove the dead options. |
| E8 | Declared but missing: ASS export, `load_from_metadata_json`, several options, per-word VTT tags, word intensity/emphasis (never computed); metadata hard-codes `"device":"cuda"`. | `:23`, `:3565-3634` | Implement what's cheap (ASS, VTT word tags, metadata); remove what isn't real (intensity) and correct the docs. |

### Diarization

| # | Finding | Where | Fix |
|---|---|---|---|
| D1 | Clustering is O(n³): one hour never finishes. | `diarization.cpp:333-393` | Similarity matrix once, Lance–Williams updates (O(n²)). |
| D2 | Default device CUDA throws with the CPU ONNX Runtime; paths widened byte by byte; input name and 16000 samples hard-coded; dangling name pointers. | `:44-74`, `:241-255` | CPU default; UTF-8 → wide; names from the session; honour the window; own the strings. |
| D3 | `diarization.h` uses `Ort::` types behind a private define, so it doesn't compile for users. | `diarization.h:11-13`, `:271` | pimpl. |

### The DLL and logging

| # | Finding | Fix |
|---|---|---|
| A1 | The exported API passes STL types and exceptions across the DLL. Only Release /MD with VS 2022 17.10+ and runtime 14.40+ works; struct changes break users silently; there's no version macro. | Now: state the requirements in README_BINARIES; add `MUNINN_VERSION` and a runtime version check; build with `_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR`. Later (see below): a C interface. |
| A2 | The `Logger` callback is read and written without a lock. | A mutex or an atomic `shared_ptr`. |

### Docs

Most docs describe a 2025 Muninn:
- **Static linking:** README claims "copy two DLLs" and static FFmpeg.
- **Build steps:** they use the 4.7.1 submodule with `/MT`, CUDA 11.8, no SentencePiece step, a Linux build that can't work, and a `find_package(Muninn)` that doesn't exist.
- **SETUP.md and GETTING_STARTED.md:** stale paths, missing scripts, "MIT".
- **PROJECT_STATUS.md:** from Nov 2025.
- **Streaming docs:** claim word timestamps, language detection, thread safety and a free `export_srt` that don't exist.
- **Translation docs:** wrong batch sizes and language counts.
- **Diarization docs:** wrong window defaults, and a model export the code can't load.
- **INTEGRATION.md:** options that don't compile.
- **Header comments:** several are wrong.
- **Examples:** two missing from CMake; one diarizes an empty buffer; one sets speaker labels too late.

Full list in the reviewers' notes, summarised in the fix commits.

## The accuracy harness

The reference itself needs fixing first, so the fixes can be measured:
- **Old reference:** it is faster-whisper 1.2.0 with CTranslate2 4.5.0 and Silero v5.
- **Different settings:** the reference uses no word timestamps, 400 ms padding and 2000 ms minimum silence. Muninn uses word timestamps, 500 ms padding, 100 ms minimum silence and `hallucination_silence_threshold` 0.1.
- **What it misses:** `compare.py` doesn't look at word timings, which #2143 and T8 change.

Plan:
1. Update the reference to faster-whisper master.
2. Run both sides with matched settings.
3. Add a word-timing comparison.
4. Measure after each step.

## Fix order

1. Harness: reference on faster-whisper master, matched settings, word timings.
2. CTranslate2 4.9.1 on its own (C1), measured against 4.8.3. Then C2.
3. Transcription accuracy, one change at a time, each measured:
   - T1 (mel)
   - T2 (filters)
   - T5, T6 and T7 (context and edges)
   - T8 and T11 (timestamps and words)
   - T14 and T15 (fallback and prompt)
   - T16 and T17 (preamble and language)
4. Transcription robustness: T3, T4 (with Loki), T9, T10, T12, T13, T18–T25, T26 and T27.
5. Translator: X1–X6 (with Loki's worker for X2).
6. Streaming: S1–S8.
7. Subtitle export and captions: E1–E8.
8. Diarization: D1–D3.
9. DLL and logging: A1 and A2.
10. Docs, examples, build scripts and the submodule (C3). Then a v0.6.0 release.

Steps 2 and 3 need the GPU free, about 1 minute per harness run. Everything else can go ahead meanwhile.

## Not planned

**A C interface for the DLL (A1, long term)** is a redesign of every public header and of Loki's integration. This audit adds the version check and states the build requirements instead. It's worth doing before Muninn has outside users who build against it.
