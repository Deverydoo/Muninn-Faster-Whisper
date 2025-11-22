# Speaker Diarization Setup Guide

## Overview

Muninn's speaker diarization feature requires the pyannote embedding model to identify different speakers in audio. This guide covers model download and setup.

## Requirements

✅ **Already Included:**
- ONNX Runtime CUDA provider DLLs (in `third_party/onnx/runtimes/win-x64/native/`)
- Diarization API integrated into Muninn

❌ **You Need to Download:**
- Pyannote speaker embedding model (ONNX format)

## Quick Setup

### Step 1: Download Pyannote Embedding Model

The pyannote embedding model extracts 512-dimensional speaker features from audio.

**Option A: Pre-converted ONNX Model (Recommended)**

Visit HuggingFace and search for "pyannote embedding onnx" models. Some community members have pre-converted models available.

Example sources:
- https://huggingface.co/models (search "pyannote embedding onnx")
- Look for models tagged with "speaker-diarization" and "onnx"

**Option B: Convert from PyTorch Yourself**

If you have Python installed:

```bash
# Install dependencies
pip install pyannote.audio onnx torch

# Download and convert model
python -c "
from pyannote.audio import Model
import torch
import onnx

# Load pretrained model
model = Model.from_pretrained('pyannote/embedding')
model.eval()

# Create dummy input (1.5s of audio at 16kHz)
dummy_input = torch.randn(1, 24000)

# Export to ONNX
torch.onnx.export(
    model,
    dummy_input,
    'pyannote-embedding.onnx',
    input_names=['audio'],
    output_names=['embedding'],
    dynamic_axes={'audio': {1: 'samples'}, 'embedding': {0: 'batch'}}
)
print('Saved to pyannote-embedding.onnx')
"
```

### Step 2: Place Model File

Move the downloaded ONNX model to your models directory:

```
models/
  ├── faster-whisper-large-v3-turbo/
  ├── silero_vad.onnx
  └── pyannote-embedding.onnx  ← Place here
```

### Step 3: Enable Diarization in Code

```cpp
#include <muninn/transcriber.h>

muninn::Transcriber transcriber("models/faster-whisper-large-v3-turbo");

muninn::TranscribeOptions options;
options.vad_type = muninn::VADType::Auto;  // Auto VAD

// Enable speaker diarization
options.enable_diarization = true;
options.diarization_model_path = "models/pyannote-embedding.onnx";
options.diarization_threshold = 0.7f;  // Default clustering threshold
options.diarization_min_speakers = 1;
options.diarization_max_speakers = 10;

auto result = transcriber.transcribe("conversation.mp4", options);

// Segments now have speaker labels
for (const auto& seg : result.segments) {
    std::cout << "[" << seg.speaker_label << "] " << seg.text << "\n";
}
```

**Output:**
```
[Speaker 0] Hello everyone, welcome to the show.
[Speaker 1] Thanks for having me!
[Speaker 0] Let's dive right in...
```

## Verification

Run the test app to verify diarization works:

```bash
# In tests/muninn_test_app.cpp, set:
options.enable_diarization = true;

# Build and run
cmake --build build --config Release
build\Release\Release\muninn_test_app.exe models/faster-whisper-large-v3-turbo test.mp4
```

**Expected output:**
```
[Muninn] Speaker diarization: ENABLED
[Diarization] Processing Track 0...
[Diarization] Track 0: Detected 2 speaker(s)
[Muninn] ✓ Speaker diarization complete
```

**If model is missing:**
```
[Muninn] Warning: Diarization failed: Failed to load model
[Muninn] Continuing without speaker labels...
```

The transcription will continue successfully without speaker labels.

## Performance Impact

With diarization enabled:
- **Processing time:** +10-20% (one-time cost for speaker analysis)
- **VRAM usage:** +500MB (pyannote embedding model on GPU)
- **Total time:** Still 10-30x real-time on RTX 4090

**Example:**
- 243-second video without diarization: 8 seconds
- 243-second video with diarization: 9-10 seconds

## Tuning Speaker Detection

### Clustering Threshold

Controls how similar speaker embeddings must be to belong to the same speaker:

| Threshold | Effect | Use Case |
|-----------|--------|----------|
| 0.5 | Loose - fewer speakers | Similar voices (siblings, twins) |
| 0.6 | Balanced | Most conversations |
| **0.7** | **Default** | Recommended starting point |
| 0.8 | Strict - more speakers | Distinct voices needed |
| 0.9 | Very strict | High precision required |

```cpp
// Podcast with 2-3 similar voices
options.diarization_threshold = 0.6f;

// Conference with 5-10 distinct speakers
options.diarization_threshold = 0.8f;
```

### Speaker Count Limits

```cpp
options.diarization_min_speakers = 2;   // Enforce at least 2 speakers
options.diarization_max_speakers = 5;   // Cap at 5 speakers
```

## Troubleshooting

### Issue: "Failed to load model"

**Cause:** Model file not found or invalid path

**Solution:** Check file exists at specified path
```cpp
options.diarization_model_path = "models/pyannote-embedding.onnx";
// Verify file exists at: D:\YourProject\models\pyannote-embedding.onnx
```

### Issue: "Failed to initialize ONNX session"

**Cause:** Missing ONNX Runtime provider DLLs

**Solution:** Copy DLLs to executable directory
```bash
# Copy from:
third_party/onnx/runtimes/win-x64/native/*.dll

# To:
build/Release/Release/
```

Required DLLs:
- `onnxruntime.dll`
- `onnxruntime_providers_shared.dll`
- `onnxruntime_providers_cuda.dll`

### Issue: Too many speakers detected

**Cause:** Threshold too low

**Solution:** Increase clustering threshold
```cpp
options.diarization_threshold = 0.8f;  // Was 0.7f
```

### Issue: Not enough speakers detected

**Cause:** Threshold too high

**Solution:** Decrease clustering threshold
```cpp
options.diarization_threshold = 0.6f;  // Was 0.7f
```

## Model Information

**Pyannote Embedding Model:**
- **Input:** Audio waveform (mono, 16kHz)
- **Output:** 512-dimensional speaker embedding
- **Model size:** ~30MB (ONNX)
- **Architecture:** SincNet + LSTM
- **Inference time:** ~10ms per 1.5s window (GPU)

**Supported by:**
- ONNX Runtime 1.23.2+
- CUDA 12.x (GPU) or CPU

## See Also

- [Speaker Diarization Guide](SPEAKER_DIARIZATION.md) - Complete feature documentation
- [Integration Guide](INTEGRATION.md) - API reference and examples
- [VAD Auto-Detection](VAD_AUTO_DETECTION.md) - Voice activity detection setup

---

**Note:** Diarization is opt-in and gracefully degrades if the model is unavailable. Your application will continue to work without speaker labels if diarization setup is incomplete.
