# Muninn Faster-Whisper - Dependencies

## Core Dependencies

### 1. CTranslate2 (Optimized Build) ⚡

**Location**: `D:\machine_learning\Loki-Studio\CTranslate2`
**Version**: Custom optimized build (2-4x faster than standard)
**Usage**: Whisper model inference engine

**Setup** (run once):
```bash
cd D:\Vibe_Projects\Muninn-Faster-Whisper\third_party
mklink /D CTranslate2 "D:\machine_learning\Loki-Studio\CTranslate2"
```

**Why Custom Build?**
- Optimized compiler flags
- CUDA 11.8+ support
- Faster inference (2.5-3x vs Python)
- Lower memory usage

### 2. Heimdall Audio Decoder (DLL) 🎵

**Location**: `D:\machine_learning\Loki-Studio\cpp_heimdall_waveform_engine`
**Purpose**: Fast audio extraction using FFmpeg
**Output**: `heimdall.dll` + `heimdall_lib.lib`

**Usage as DLL**:
```cpp
// Muninn will load heimdall.dll at runtime
// No need to recompile when Heimdall updates
```

**Setup**:
```bash
# Copy Heimdall DLL to Muninn
copy "D:\machine_learning\Loki-Studio\cpp_heimdall_waveform_engine\build\Release\heimdall.dll" ^
     "D:\Vibe_Projects\Muninn-Faster-Whisper\third_party\heimdall\heimdall.dll"

# Copy import library for linking
copy "D:\machine_learning\Loki-Studio\cpp_heimdall_waveform_engine\build\Release\heimdall.lib" ^
     "D:\Vibe_Projects\Muninn-Faster-Whisper\third_party\heimdall\heimdall.lib"
```

**Heimdall API** (from `audio_decoder.h`):
```cpp
namespace heimdall {

class AudioDecoder {
public:
    // Open video/audio file for reading
    bool open(const std::string& filename, int target_sample_rate);

    // Get audio stream metadata
    int get_stream_count();
    int get_channels(int stream_index = 0);
    int64_t get_duration_ms();

    // Decode audio samples
    int decode_samples(int stream_index, std::vector<float>& samples);

    // Close file
    void close();
};

} // namespace heimdall
```

**Why DLL?**
- Separate audio decoding from transcription logic
- Can update Heimdall independently
- Smaller binary size
- Easier FFmpeg dependency management

### 3. FFmpeg Libraries (via Heimdall)

**Included in Heimdall DLL**:
- `avcodec` - Audio/video codec
- `avformat` - Container format handling
- `avutil` - Utility functions
- `swresample` - Audio resampling

**Note**: FFmpeg DLLs must be in PATH or same directory as heimdall.dll

## Optional Dependencies

### 4. CUDA Toolkit (for GPU acceleration)

**Version**: 11.8 or later
**Download**: https://developer.nvidia.com/cuda-downloads

**Required for**:
- GPU inference (massive speedup)
- float16 compute type
- CUDA device support

**Without CUDA**:
- Falls back to CPU inference (slower)
- Use compute_type="float32" or "int8"

## Dependency Tree

```
Muninn-Faster-Whisper
├── CTranslate2 (symlinked)
│   ├── CUDA Toolkit 11.8+
│   ├── Intel MKL / OpenBLAS (CPU)
│   └── cuBLAS (GPU)
│
├── Heimdall (DLL)
│   ├── FFmpeg avcodec
│   ├── FFmpeg avformat
│   ├── FFmpeg avutil
│   └── FFmpeg swresample
│
└── Standard Libraries
    ├── C++17 STL
    └── Windows SDK (MSVC 2022)
```

## Build-Time Dependencies

### CMake 3.20+
**Download**: https://cmake.org/download/

### Visual Studio 2022
**Components needed**:
- Desktop development with C++
- Windows 10/11 SDK
- C++ CMake tools

## Runtime Dependencies

For end users of Muninn-built applications:

1. **CTranslate2 DLLs** (included in build):
   - `ctranslate2.dll`
   - `cudart64_110.dll` (if CUDA build)

2. **Heimdall DLL** (included):
   - `heimdall.dll`

3. **FFmpeg DLLs** (distributed with Heimdall):
   - `avcodec-*.dll`
   - `avformat-*.dll`
   - `avutil-*.dll`
   - `swresample-*.dll`

4. **NVIDIA Driver** (for GPU inference):
   - Latest GeForce/Quadro driver
   - Supports CUDA 11.8+

## Directory Structure

```
third_party/
├── CTranslate2/                 # Symlink to optimized build
│   ├── include/
│   ├── lib/
│   └── bin/
│
├── heimdall/                    # Audio decoder DLL
│   ├── heimdall.dll            # Runtime library
│   ├── heimdall.lib            # Import library
│   └── include/
│       ├── audio_decoder.h
│       └── heimdall.h
│
└── ffmpeg/                      # (Optional) FFmpeg development files
    ├── include/
    └── lib/
```

## Setup Script

Create `setup_dependencies.bat`:

```batch
@echo off
echo Setting up Muninn dependencies...

REM 1. Create symlink to CTranslate2
cd third_party
if not exist CTranslate2 (
    mklink /D CTranslate2 "D:\machine_learning\Loki-Studio\CTranslate2"
    if errorlevel 1 (
        echo ERROR: Failed to create CTranslate2 symlink
        echo Run this script as Administrator
        pause
        exit /b 1
    )
)

REM 2. Copy Heimdall DLL
mkdir heimdall 2>nul
copy "D:\machine_learning\Loki-Studio\cpp_heimdall_waveform_engine\build\Release\heimdall.dll" heimdall\
copy "D:\machine_learning\Loki-Studio\cpp_heimdall_waveform_engine\build\Release\heimdall.lib" heimdall\

REM 3. Copy Heimdall headers
mkdir heimdall\include 2>nul
copy "D:\machine_learning\Loki-Studio\cpp_heimdall_waveform_engine\src\audio_decoder.h" heimdall\include\
copy "D:\machine_learning\Loki-Studio\cpp_heimdall_waveform_engine\src\heimdall.h" heimdall\include\

echo.
echo Dependencies setup complete!
echo.
echo Next steps:
echo 1. Build Muninn: cmake --build build --config Release
echo 2. Test: muninn_test_app test_audio.mp3
echo.
pause
```

## Verification

After setup, verify dependencies:

```bash
# Check CTranslate2 symlink
dir third_party\CTranslate2

# Check Heimdall DLL
dir third_party\heimdall\heimdall.dll

# Check headers
dir third_party\heimdall\include
```

## Troubleshooting

### CTranslate2 not found
- Verify symlink: `dir /AL third_party\CTranslate2`
- Re-run as Administrator
- Check source path exists

### Heimdall DLL not found
- Rebuild Heimdall: `cd cpp_heimdall_waveform_engine && cmake --build build --config Release`
- Copy DLL manually
- Check FFmpeg DLLs are present

### CUDA errors
- Update NVIDIA drivers
- Verify CUDA Toolkit installation
- Check GPU compatibility (Compute Capability 6.0+)

### Linking errors
- Rebuild dependencies
- Check library paths in CMakeLists.txt
- Verify DLL architecture (x64)

## Updating Dependencies

### Update CTranslate2
```bash
cd D:\machine_learning\Loki-Studio\CTranslate2
git pull
cmake --build build --config Release
# Muninn will automatically use updated version via symlink
```

### Update Heimdall
```bash
cd D:\machine_learning\Loki-Studio\cpp_heimdall_waveform_engine
cmake --build build --config Release
copy build\Release\heimdall.dll D:\Vibe_Projects\Muninn-Faster-Whisper\third_party\heimdall\
```

---

**Status**: Setup required before first build
**Setup Time**: ~5 minutes
**Administrator Rights**: Required for symlink creation
