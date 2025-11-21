# Muninn Faster-Whisper - Migration Plan

## Current Status

✅ **Completed**:
- [x] Project structure created
- [x] README and documentation
- [x] Core type definitions (`muninn/types.h`)
- [x] Main API header (`muninn/transcriber.h`)

## Next Steps

### 1. Copy Optimized CTranslate2 ⚡ **CRITICAL**

**From**: `D:\machine_learning\Loki-Studio\CTranslate2`
**To**: `D:\Vibe_Projects\Muninn-Faster-Whisper\third_party\CTranslate2`

**Manual Step Required**:
```bash
# Open Command Prompt as Administrator
cd D:\Vibe_Projects\Muninn-Faster-Whisper\third_party
mklink /D CTranslate2 "D:\machine_learning\Loki-Studio\CTranslate2"
```

This is our **blazing fast** custom CTranslate2 build - the secret sauce!

### 2. Extract Core Components from OdinUI

Copy and adapt these files from `odin_ui/src/services/`:

#### A. MelSpectrogram
**From**: `odin_ui/src/services/MelSpectrogram.{h,cpp}`
**To**: `src/mel_spectrogram.{h,cpp}`
**Changes needed**:
- Remove Qt dependencies (QString → std::string)
- Keep the optimized FFT and mel-filterbank code
- Already has 128 mel bins support ✓

####B. WhisperTranscriber (Core Logic)
**From**: `odin_ui/src/services/WhisperTranscriber.{h,cpp}`
**To**: `src/transcriber.cpp` (implementation)
**Changes needed**:
- Remove Qt dependencies (QString → std::string, QList → std::vector)
- Remove SagaLogger, use std::cerr or custom logger
- Keep the hallucination detection logic we just added! ✓
- Keep the timestamp filtering ✓
- Adapt to use muninn::types.h structures

#### C. AudioExtractor (FFmpeg wrapper)
**From**: `odin_ui/src/services/AudioExtractor.{h,cpp}`
**To**: `src/audio_extractor.{h,cpp}`
**Changes needed**:
- Remove Qt dependencies
- Keep Heimdall audio decoder integration
- Simplify to single-track extraction for now

### 3. Implement New Components

#### A. VAD (Voice Activity Detection)
**File**: `src/vad.cpp`, `include/muninn/vad.h`
**Algorithm**: Energy-based VAD
**Estimated**: ~150 lines

```cpp
class VAD {
public:
    struct SpeechSegment { float start; float end; };
    std::vector<SpeechSegment> detect_speech(
        const std::vector<float>& audio,
        int sample_rate = 16000
    );
};
```

#### B. AudioProcessor (Sliding Windows)
**File**: `src/audio_processor.cpp`, `include/muninn/audio_processor.h`
**Features**: 30-second chunks with 5-second overlap
**Estimated**: ~200 lines

```cpp
class AudioProcessor {
public:
    struct AudioChunk {
        std::vector<float> samples;
        float start_time;
        float end_time;
    };
    std::vector<AudioChunk> split_audio(
        const std::vector<float>& audio,
        const std::vector<VAD::SpeechSegment>& speech_segments
    );
};
```

#### C. HallucinationFilter (Enhanced)
**File**: `src/hallucination_filter.cpp`, `include/muninn/hallucination_filter.h`
**Features**: All filtering logic we implemented today
**Estimated**: ~100 lines

### 4. Implement Transcriber Class

**File**: `src/transcriber.cpp`

**Structure**:
```cpp
class Transcriber::Impl {
    std::unique_ptr<ctranslate2::models::Whisper> model_;
    std::unique_ptr<MelSpectrogram> mel_converter_;
    std::unique_ptr<VAD> vad_;
    std::unique_ptr<AudioProcessor> audio_processor_;
    std::unique_ptr<HallucinationFilter> hallucination_filter_;

    TranscribeResult transcribe_internal(
        const std::vector<float>& audio,
        const TranscribeOptions& options
    );
};
```

### 5. Create Test Application

**File**: `tests/muninn_test_app.cpp`

```cpp
#include <muninn/transcriber.h>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: muninn_test_app <audio_file>\n";
        return 1;
    }

    try {
        muninn::Transcriber transcriber("models/faster-whisper-large-v3-turbo");

        muninn::TranscribeOptions opts;
        opts.vad_filter = true;
        opts.language = "en";

        auto result = transcriber.transcribe(argv[1], opts);

        std::cout << "Transcription (" << result.language << "):\n\n";
        for (const auto& segment : result.segments) {
            std::cout << "[" << segment.start << " -> " << segment.end << "] "
                      << segment.text << "\n";
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
```

### 6. CMakeLists.txt

**File**: `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.20)
project(MuninnFasterWhisper VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find CTranslate2
find_package(CTranslate2 REQUIRED
    HINTS "${CMAKE_SOURCE_DIR}/third_party/CTranslate2/build/Release")

# Muninn library
add_library(muninn_faster_whisper
    src/transcriber.cpp
    src/mel_spectrogram.cpp
    src/vad.cpp
    src/audio_processor.cpp
    src/hallucination_filter.cpp
    src/audio_extractor.cpp
)

target_include_directories(muninn_faster_whisper PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(muninn_faster_whisper PUBLIC
    CTranslate2::ctranslate2
)

# Test application
add_executable(muninn_test_app tests/muninn_test_app.cpp)
target_link_libraries(muninn_test_app PRIVATE muninn_faster_whisper)
```

## Development Workflow

1. **Copy CTranslate2** (symlink)
2. **Extract MelSpectrogram** from OdinUI
3. **Extract WhisperTranscriber** core logic
4. **Implement VAD** (energy-based)
5. **Implement AudioProcessor** (sliding windows)
6. **Wire everything together** in Transcriber class
7. **Build**: `cmake --build build --config Release`
8. **Test**: `muninn_test_app test_audio.mp3`
9. **Compare** with Python faster-whisper

## Testing Strategy

### Test Files
Place in project root:
- `test_short.mp3` (30 seconds) - Quick testing
- `test_medium.mp3` (5 minutes) - Hallucination testing
- `test_long.mp3` (30 minutes) - Performance testing
- `test_silent.mp3` (mostly silence) - VAD testing

### Comparison Script
```python
# test_compare.py
from faster_whisper import WhisperModel
import subprocess
import time

# Python
model = WhisperModel("large-v3-turbo")
start = time.time()
segments, info = model.transcribe("test_medium.mp3")
python_time = time.time() - start
python_text = " ".join([seg.text for seg in segments])

# C++ Muninn
start = time.time()
result = subprocess.run(["muninn_test_app", "test_medium.mp3"],
                       capture_output=True, text=True)
cpp_time = time.time() - start
cpp_text = result.stdout

print(f"Python: {python_time:.2f}s")
print(f"C++:    {cpp_time:.2f}s")
print(f"Speedup: {python_time/cpp_time:.2f}x")
print(f"Match: {python_text.strip() == cpp_text.strip()}")
```

## Performance Goals

- **Speed**: 2.5-3x faster than Python
- **Memory**: 40% less than Python
- **Accuracy**: 99.5%+ match with Python output
- **Hallucinations**: <1% on silent audio

## Timeline

- **Week 1**: Extract code, implement VAD
- **Week 2**: Implement sliding windows, integrate components
- **Week 3**: Testing, optimization, documentation
- **Week 4**: Community release, GitHub setup

## Current Focus

**NEXT IMMEDIATE TASKS**:
1. Create symlink to CTranslate2
2. Extract MelSpectrogram from OdinUI
3. Test standalone compilation

Start simple, test early, iterate fast! 🚀
