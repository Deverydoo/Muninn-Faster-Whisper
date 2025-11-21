# Muninn Faster-Whisper - Setup Instructions

## Dependencies

### 1. CTranslate2 (Optimized Build)

**Location**: `D:\machine_learning\Loki-Studio\CTranslate2`

This is a custom-optimized build of CTranslate2 that provides 2-4x faster performance.

**Option A: Symlink (Recommended)**
```bash
cd D:\Vibe_Projects\Muninn-Faster-Whisper\third_party
mklink /D CTranslate2 "D:\machine_learning\Loki-Studio\CTranslate2"
```

**Option B: Copy**
```bash
xcopy "D:\machine_learning\Loki-Studio\CTranslate2" ^
      "D:\Vibe_Projects\Muninn-Faster-Whisper\third_party\CTranslate2" /E /I /H /Y
```

### 2. FFmpeg (for audio extraction)

Download from: https://ffmpeg.org/download.html

Extract to: `D:\Vibe_Projects\Muninn-Faster-Whisper\third_party\ffmpeg`

### 3. CUDA Toolkit (for GPU acceleration)

Download from: https://developer.nvidia.com/cuda-downloads

Version: 11.8 or later

## Build Instructions

### Configure

```bash
cd D:\Vibe_Projects\Muninn-Faster-Whisper
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DWITH_CUDA=ON ^
  -DCTranslate2_DIR="third_party/CTranslate2/build/Release"
```

### Build

```bash
cmake --build build --config Release -j
```

### Test

```bash
cd build/Release
./muninn_test_app ../../../test_audio.mp3
```

## Quick Test

1. Place a test video/audio file: `test_audio.mp3`
2. Run: `muninn_test_app test_audio.mp3`
3. Check output transcript

## Project Structure

```
Muninn-Faster-Whisper/
├── README.md
├── SETUP.md (this file)
├── LICENSE
├── CMakeLists.txt
├── include/muninn/
│   ├── transcriber.h
│   ├── types.h
│   ├── vad.h
│   └── audio_processor.h
├── src/
│   ├── transcriber.cpp
│   ├── mel_spectrogram.cpp
│   ├── vad.cpp
│   └── audio_processor.cpp
├── tests/
│   └── muninn_test_app.cpp
├── examples/
│   └── basic_transcription.cpp
└── third_party/
    ├── CTranslate2/  (symlink or copy)
    └── ffmpeg/
```

## Next Steps

1. Create symlink to CTranslate2
2. Build the project
3. Run test application
4. Start implementing VAD and other features

## Development Workflow

1. **Edit code** in `src/` and `include/muninn/`
2. **Rebuild**: `cmake --build build --config Release`
3. **Test**: Run `muninn_test_app` with test audio
4. **Iterate**: Check transcript quality, fix issues, repeat

## Troubleshooting

### CTranslate2 not found
- Verify symlink: `dir third_party\CTranslate2`
- Check path in CMakeLists.txt

### CUDA errors
- Update NVIDIA drivers
- Verify CUDA Toolkit installation
- Check GPU compatibility

### Linking errors
- Rebuild CTranslate2 if needed
- Check library paths in CMakeLists.txt

## Performance Testing

Compare with Python faster-whisper:

```python
# Python
from faster_whisper import WhisperModel
model = WhisperModel("large-v3-turbo")
segments, info = model.transcribe("test_audio.mp3")
```

```cpp
// C++ Muninn
muninn::Transcriber transcriber("models/whisper-large-v3-turbo");
auto result = transcriber.transcribe("test_audio.mp3");
```

Time both and compare!
