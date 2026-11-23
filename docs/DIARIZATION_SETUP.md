# Diarization Model Setup

Diarization needs a speaker-embedding model in ONNX format. Muninn doesn't ship one. This page covers making it; [SPEAKER_DIARIZATION.md](SPEAKER_DIARIZATION.md) covers the API.

## What Muninn needs

- A Muninn built with ONNX Runtime (`WITH_SILERO_VAD=ON`, the default; the same ONNX Runtime runs Silero VAD). `onnxruntime.dll` must sit next to your executable.
- An ONNX model that takes 16 kHz mono samples and returns one embedding.

Muninn reads the model's first input and output names, and its input shape, from the model itself. The last input dimension is the sample count: if it is open, each window goes in at its own length; if it is fixed, every window is padded or cut to that length (with a warning when it differs from `embedding_window_s`). Any other open dimension is set to 1. The last output dimension is taken as the embedding size.

## Making the model

`convert_pyannote_to_onnx.py` in the repository root exports [pyannote/embedding](https://huggingface.co/pyannote/embedding) (an x-vector SincNet model, 512-dimensional embeddings):

1. pyannote/embedding is gated on Hugging Face: accept its conditions there, then download its files (`pytorch_model.bin` and the `.yaml` files) into `models/pyannote/`.
2. Install the Python packages: `pip install pyannote.audio torch onnx`.
3. Run:

   ```
   python convert_pyannote_to_onnx.py models/pyannote models/pyannote_embedding.onnx
   ```

The result (about 17 MB) has input `waveform` with shape `[1, num_samples]` (any length) and output `embedding` with shape `[batch, 512]`, opset 15. Checked with ONNX Runtime on the CPU: 0.5, 1, 1.5 and 3 s of audio each give a 512-value embedding.

## Using it

```cpp
muninn::TranscribeOptions options;
options.enable_diarization = true;
options.diarization_model_path = "models/pyannote_embedding.onnx";
```

or `muninn::Diarizer diarizer("models/pyannote_embedding.onnx");`. It runs on the CPU; see SPEAKER_DIARIZATION.md for the options.

## Problems

| Message | Cause |
|---|---|
| `Failed to initialize ONNX session: ...` | The file is missing or isn't a model ONNX Runtime can load. |
| The program doesn't start (0xC000007B) | Windows found its own older `onnxruntime.dll` in System32. Put Muninn's next to your executable, not just on PATH. |
| `This Muninn build has no ONNX Runtime, so it can't diarize` | Muninn was built without ONNX Runtime. |
| `Diarizer: this ONNX Runtime has no CUDA provider, using CPU` | `device = "cuda"` with Muninn's CPU-only ONNX Runtime. Harmless. |
| `Diarizer: the model takes exactly N samples; ...` | The model has a fixed input length different from `embedding_window_s`. Windows are padded or cut to fit. |

When the transcriber runs diarization itself (`enable_diarization`), a failure is logged as a warning and the transcript comes back without speaker labels.
