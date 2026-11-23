# Building Muninn

Muninn builds on Windows x64 with Visual Studio 2022. Other platforms aren't
supported: `CMakeLists.txt` assumes MSVC and a vcpkg toolchain, and nothing
else is tested. If you only want to use Muninn, the release zip has the DLLs
(see `packaging/README_BINARIES.txt`).

## What you need

- Visual Studio 2022 (17.10 or newer) with the C++ workload, CMake 3.20+, Git
- CUDA Toolkit 13.2 for a GPU build (`C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2`)
- vcpkg at `C:\vcpkg`. `CMakeLists.txt` uses it as the toolchain file; pass
  `-DCMAKE_TOOLCHAIN_FILE=<your vcpkg>/scripts/buildsystems/vcpkg.cmake` if
  yours is elsewhere. Its packages are only needed when you don't give
  `FFMPEG_DIR` (see below).

Everything is built Release with the dynamic runtime (/MD, CMake's default).
A program that uses muninn.dll has to be built the same way; see
`packaging/README_BINARIES.txt`.

## 1. CTranslate2 4.9.1

Muninn needs CTranslate2 4.9.1 with the local patch in
`patches/ctranslate2-v4.9.1-local.patch`, in `third_party/CTranslate2-4.9.1`:

```
powershell -ExecutionPolicy Bypass -File scripts/build_ctranslate2.ps1
```

The script clones the tag, applies the patch, builds `build_cuda13`
(CUDA 13.2, sm_75 to sm_120, and the Ruy CPU backend) and checks the GPU
architectures. `patches/README.md` has the same steps by hand and what the
patch changes.

## 2. SentencePiece

The translator needs SentencePiece (static, in `third_party/sentencepiece`).
CI uses commit `c40fb66f94ba04855bbd71f6b318b5427939ec7e`:

```
git clone https://github.com/google/sentencepiece.git third_party/sentencepiece
git -C third_party/sentencepiece checkout c40fb66f94ba04855bbd71f6b318b5427939ec7e
cmake -S third_party/sentencepiece -B third_party/sentencepiece/build -A x64 -DSPM_ENABLE_SHARED=OFF -DSPM_BUILD_TEST=OFF
cmake --build third_party/sentencepiece/build --config Release --target sentencepiece-static
```

Without it Muninn still builds, but `Translator` throws when it is constructed.

## 3. ONNX Runtime 1.31.0 (CPU)

For Silero VAD and diarization. Unzip `onnxruntime-win-x64-1.31.0.zip` from
the [ONNX Runtime releases](https://github.com/microsoft/onnxruntime/releases)
to `third_party/onnx/onnxruntime-win-x64-1.31.0` (it has `include/` and
`lib/`). Without it, build with `-DWITH_SILERO_VAD=OFF`: energy VAD still works,
Silero falls back to energy and the diarizer throws.

## 4. FFmpeg 9 (shared, LGPL)

Muninn decodes audio with FFmpeg's DLLs. Point `FFMPEG_DIR` at an LGPL shared
build that has `include/` and the import libraries (`avformat.lib`,
`avcodec.lib`, `avutil.lib`, `swresample.lib`, in `lib/` or `bin/`). The
release uses FFmpeg 9.0.2 built with MSVC; CI uses
`ffmpeg-n9.0-latest-win64-lgpl-shared-9.0` from
[BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds/releases).

## 5. Muninn

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
    -DFFMPEG_DIR=C:/path/to/ffmpeg-9-shared -DFFMPEG_STATIC=OFF ^
    -DWITH_CUDA=ON -DWITH_SILERO_VAD=ON -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build --config Release --parallel
```

Options: `WITH_CUDA` (ON; OFF needs no CUDA toolkit for Muninn itself, which
is how CI builds it against a CPU-only CTranslate2), `WITH_SILERO_VAD` (ON),
`BUILD_TESTS` (ON), `BUILD_EXAMPLES` (OFF), `BUILD_SHARED_LIBS` (ON).

The output is in `build/Release/Release/`: `muninn.dll` and `muninn.lib`, with
`ctranslate2.dll` and `onnxruntime.dll` copied beside them. The FFmpeg DLLs are
not copied: put `FFMPEG_DIR/bin` on PATH or copy them next to the program.
A GPU run also needs the CUDA 13 and cuDNN 9 DLLs (listed in
`packaging/README_BINARIES.txt`).

When you switch CTranslate2 versions, reconfigure with
`-DCTranslate2_DIR=<tree> -U CTRANSLATE2_LIB -U CTRANSLATE2_DLL`.

The compression ratio uses zlib only when `zlib.h` is on the include path; a
build with `FFMPEG_DIR` and no zlib uses an estimate from repeated 4-character
pieces instead.

## Tests

```
ctest --test-dir build -C Release -L cpu
```

The `cpu` tests need no model and no GPU. Tests that need a Whisper model are
registered when `MUNINN_TEST_MODEL` and `MUNINN_TEST_AUDIO` are set at
configure time (labels `model;gpu`), and the translator model test when
`MUNINN_TEST_NLLB_MODEL` is.

## Examples

With `-DBUILD_EXAMPLES=ON`:

| Example | Usage |
|---|---|
| `basic_transcription` | `<model_dir> <audio_file> [cuda\|cpu\|auto] [language]` |
| `translation_example` | `<whisper_model> <nllb_model> <audio_file> [target_langs...]` |
| `karaoke_captions` | `<model_dir> <audio_file> [cpu\|cuda\|auto]` (streaming, word highlighting) |
| `translation_streaming` | `<model_dir> <audio_file> [cpu\|cuda\|auto]` (streaming, to English) |
| `subtitle_export` | `<video_file> [example_number]` (needs ONNX Runtime) |
| `streaming_vad_selection` | `<whisper_model> <silero_vad.onnx>` (needs ONNX Runtime) |

## Release zip

`packaging/make_release_zip.ps1` packs a CUDA Release build into
`Muninn-<version>-win-x64-cuda13.zip`; its header lists the arguments.
