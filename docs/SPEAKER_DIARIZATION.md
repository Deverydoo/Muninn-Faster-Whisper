# Speaker Diarization

Diarization labels who spoke when: each transcript segment gets a `speaker_id` and a `speaker_label` ("Speaker 0", or a name you set). Muninn does it with a pyannote speaker-embedding model run through ONNX Runtime, then clusters the embeddings. Getting the model is covered in [DIARIZATION_SETUP.md](DIARIZATION_SETUP.md).

How it works:
1. The audio is cut into windows of `embedding_window_s` (1 s), one every `embedding_step_s` (0.5 s), and the model turns each window into an embedding (512 numbers for pyannote/embedding).
2. The embeddings are clustered by cosine similarity (average linkage). Clusters keep merging while their average similarity is at least `clustering_threshold`, then while there are more than `max_speakers`.
3. Speakers are numbered by talking time: speaker 0 talks the most. Each window becomes a diarization segment, and adjacent windows of the same speaker (less than 0.5 s apart) are merged.
4. A transcript segment takes the speaker found at its midpoint.

One speaker per moment: overlapping speech goes to whichever cluster the window landed in.

## The simple way: let the transcriber do it

```cpp
#include <muninn/transcriber.h>

muninn::Transcriber transcriber("models/faster-whisper-large-v3-turbo");

muninn::TranscribeOptions options;
options.enable_diarization = true;
options.diarization_model_path = "models/pyannote_embedding.onnx";
options.diarization_threshold = 0.7f;     // clustering_threshold
options.diarization_max_speakers = 10;    // 0 = no limit

auto result = transcriber.transcribe("conversation.mp4", options);
for (const auto& seg : result.segments) {
    std::cout << "[" << seg.speaker_label << "] " << seg.text << "\n";
}
```

This works only with `transcribe(path)`, not with the in-memory overload. Each audio track that produced segments is diarized on its own, on the CPU. Labels are always "Speaker N" and `speaker_confidence` is a fixed 0.8. If the model can't be loaded or diarization fails, a warning is logged and the transcript comes back without speaker labels. `diarization_min_speakers` is not used.

## Doing it yourself

Use the `Diarizer` directly when you want your own names, colours, or a different window:

```cpp
#include <muninn/audio_extractor.h>
#include <muninn/diarization.h>
#include <muninn/transcriber.h>

muninn::Transcriber transcriber("models/faster-whisper-large-v3-turbo");
auto result = transcriber.transcribe("podcast.mp4");

// 16 kHz mono audio of the track to diarize
muninn::AudioExtractor extractor;
std::vector<float> audio;
extractor.open("podcast.mp4");
extractor.extract_track(0, audio);

muninn::Diarizer diarizer("models/pyannote_embedding.onnx");
auto diarization = diarizer.diarize(audio.data(), audio.size(), 16000);

// Names go on the diarization result BEFORE assigning:
// assign_speakers_to_segments copies the labels it finds there.
muninn::Diarizer::set_speaker_labels(diarization, {{0, "Host"}, {1, "Guest"}});
muninn::Diarizer::assign_speakers_to_segments(result.segments, diarization);
```

`examples/subtitle_export.cpp` does this (its `label_speakers()` helper) and then writes subtitles with the speaker names.

Notes:
- `diarize()` needs 16 kHz mono audio and throws for any other sample rate. Audio shorter than one window gives an empty result.
- `assign_speakers_to_segments` sets `speaker_id` to -1 for a segment whose midpoint falls in no diarization segment, and leaves its `speaker_label` as it was.
- Because speakers are ordered by talking time, speaker 0 is the person who talks most, not the first to speak.
- For a multi-track recording where each person has their own track, you don't need diarization: `Segment::track_id` already tells you who spoke.

## DiarizationOptions

| Option | Default | Meaning |
|---|---|---|
| `clustering_threshold` | 0.7 | Minimum average cosine similarity for two clusters to merge. Lower merges more (fewer speakers); higher merges less (more speakers). |
| `max_speakers` | 10 | After the threshold, keep merging the closest clusters until there are at most this many. 0 = no limit. |
| `embedding_window_s` | 1.0 | Audio per embedding. A model with a fixed input length gets the window padded or cut to that length, with a warning at load. |
| `embedding_step_s` | 0.5 | Distance between window starts. Window and step must be greater than 0 (the constructor throws otherwise). |
| `merge_adjacent_same_speaker` | true | Merge consecutive diarization segments of one speaker less than 0.5 s apart. |
| `device` | "cpu" | "cpu", "cuda" or "auto". CUDA needs an ONNX Runtime with the CUDA provider; the one Muninn ships is CPU-only, so "cuda" falls back to the CPU with a warning. `get_options().device` says where it actually runs. |
| `num_threads` | 4 | ONNX Runtime intra-op threads. |
| `min_speakers`, `min_segment_duration`, `segmentation_model_path` | | Not used. |

## Other API

- `extract_embedding(audio, n)` and `extract_embeddings(audio, n, 16000)`: embeddings without clustering.
- `cluster_speakers(embeddings)`: the clustering step alone.
- `Diarizer::cosine_similarity(a, b)`: compare two embeddings, for example to check a segment against a known voice.
- `Diarizer::get_speaker_at_time(result, t)`: the speaker at a time, -1 if none.
- `Diarizer::get_speaker_stats(result, id)`: a `Speaker` with its embeddings and total talking time.
- `SpeakerFormatting::format_speaker_text(seg, "[{label}] {text}")`, `build_speaker_html(seg, colors)` (HTML-escaped `<font>` tags, for OBS text sources) and `generate_speaker_colors(n)` (a fixed palette of 10 colours, repeating).

## Cost

Clustering takes O(n²) time and n²/2 floats of memory for n embeddings: an hour at the default 0.5 s step is 7200 embeddings and about 100 MB. Embedding extraction runs the model once per window, so it grows linearly with the audio.

## Building

`diarization.h` contains no ONNX Runtime types (the session sits behind a private `Impl`), so it compiles without ONNX Runtime's headers. A Muninn built without ONNX Runtime still compiles it, and the `Diarizer` constructor throws. Model paths are UTF-8.

## Tuning

| Problem | Try |
|---|---|
| One person split into several speakers | Lower `clustering_threshold` (0.7 to 0.6), or set `max_speakers` to the real count |
| Two people merged into one | Raise `clustering_threshold` (0.7 to 0.8) |
| Labels flicker on short turns | A longer `embedding_window_s` (1.5 to 2 s) gives steadier embeddings but blurs quick turn changes |
