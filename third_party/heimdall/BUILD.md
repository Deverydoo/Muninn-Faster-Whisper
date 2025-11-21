# Waveform Generator C++ Module - Build Instructions

## Prerequisites

1. **Visual Studio 2019/2022** with C++ development tools
2. **CMake** (3.15+)
3. **Python 3.12** (conda environment)
4. **pybind11** (`pip install pybind11`)
5. **FFmpeg development libraries** (already available in project)

## Build Steps

### 1. Create Build Directory

```cmd
cd cpp_waveform_generator
mkdir build
cd build
```

### 2. Configure with CMake

```cmd
cmake -G "Visual Studio 17 2022" -A x64 ..
```

Or for Visual Studio 2019:
```cmd
cmake -G "Visual Studio 16 2019" -A x64 ..
```

### 3. Build Release Version

```cmd
cmake --build . --config Release
```

### 4. Install to Project Root

```cmd
cmake --install . --config Release
```

This copies `waveform_generator.pyd` to the project root.

## Quick Build Script

```cmd
cd cpp_waveform_generator
build.bat
```

## Testing

```python
import waveform_generator

gen = waveform_generator.WaveformGenerator()
info = gen.get_audio_info("test_video.mp4")
print(f"Duration: {info.duration_ms}ms")
print(f"Streams: {info.stream_count}")

peaks = gen.generate_peaks("test_video.mp4", stream_index=0, width=800)
print(f"Generated {len(peaks)} peak values")
```

## Integration with Timeline Editor

Replace Python waveform generation in `gui_timeline_editor.py`:

```python
# Old Python loop (slow):
# for i, sample in enumerate(audio_data):
#     peaks[i] = max(audio_data[i:i+chunk_size])

# New C++ module (fast):
import waveform_generator
gen = waveform_generator.WaveformGenerator()
peaks = gen.generate_peaks(video_path, stream_idx, width=800)
```

## Performance Target

- **Current (Python)**: ~5-10 seconds per track
- **Target (C++)**: <1 second per track
- **Expected speedup**: 10-20x

## Troubleshooting

### Missing FFmpeg DLLs
Ensure FFmpeg DLLs are in PATH or copy to build directory:
- `avcodec-62.dll`
- `avformat-62.dll`
- `avutil-60.dll`
- `swresample-6.dll`

### pybind11 Not Found
```cmd
pip install pybind11
```

### Wrong Python Version
Ensure CMake finds the correct Python:
```cmd
cmake -DPython_EXECUTABLE=C:/Users/craig/miniconda3/envs/lokistudio312/python.exe ..
```
