Muninn @VERSION@ - Windows x64 binaries (CUDA 13)
=================================================

Muninn is a C++ port of faster-whisper: Whisper speech-to-text on
CTranslate2, with its own FFmpeg audio decoder, voice activity detection
(energy, Silero), word timestamps, a streaming API, translation and
subtitle export. Source: https://github.com/Deverydoo/Muninn-Faster-Whisper

What's in this folder
---------------------
  bin/       muninn.dll and the DLLs it loads:
               ctranslate2.dll   CTranslate2 4.9.1 with Muninn's local patch,
                                 CUDA 13.2, sm_75 to sm_120
               onnxruntime.dll   ONNX Runtime 1.31.0, CPU (Silero VAD)
               avformat-63.dll, avcodec-63.dll, avutil-61.dll,
               swresample-7.dll  FFmpeg 9.0.2, LGPL build
  lib/       muninn.lib (import library), onnxruntime.lib
  include/   muninn/        the public headers
             onnxruntime/   ONNX Runtime headers, needed only if you include
                            muninn/diarization.h
  models/    silero_vad.onnx (Silero VAD v6.2, MIT), for VADType::Silero and
             StreamingOptions::vad_model_path
  licenses/  full license texts of the third-party parts
  LICENSE, THIRD_PARTY_NOTICES.txt

Whisper models are not included. Muninn reads CTranslate2-format models,
for example Systran/faster-whisper-large-v3 or
deepdml/faster-whisper-large-v3-turbo-ct2 on Hugging Face.

Requirements
------------
  - Windows 10 or 11, x64, a CPU with AVX2
  - An NVIDIA GPU, RTX 20 series (compute capability 7.5) or newer
  - NVIDIA driver 570 or newer
  - The CUDA 13 runtime and cuDNN 9, on PATH or beside muninn.dll. They are
    not included (they are over 700 MB). ctranslate2.dll needs
    cublas64_13.dll, cublasLt64_13.dll, cudart64_13.dll and the cuDNN 9 DLLs
    (cudnn64_9.dll, cudnn_ops64_9.dll, cudnn_cnn64_9.dll, cudnn_graph64_9.dll,
    cudnn_heuristic64_9.dll, cudnn_engines_precompiled64_9.dll,
    cudnn_engines_runtime_compiled64_9.dll). Installing the CUDA Toolkit 13.x
    and cuDNN 9 for CUDA 13 provides them.
  - The Microsoft Visual C++ 2015-2022 Redistributable (x64)

Without a supported GPU, device "cpu" works (CTranslate2 is built with the Ruy
CPU backend and OpenMP); it is much slower than a GPU. VCOMP140.DLL comes with
the Visual C++ Redistributable.

Using it
--------
Add include/ to your include path, link lib/muninn.lib, and put everything in
bin/ next to your executable.

Muninn's API passes C++ standard-library types (std::string, std::vector,
std::function) and exceptions across the DLL boundary, so your program must be
built the same way:
  - C++17 or newer, MSVC (Visual Studio 2022 17.10 or newer), x64
  - Release configuration with the /MD runtime. A Debug build (/MDd) has a
    different std::string layout and crashes on the first call; /MT frees
    memory on the wrong heap.
  - Visual C++ Redistributable 14.40 or newer at run time.
Call muninn::check_version() (include <muninn/version.h>) once at startup: it
throws if the muninn.dll that loaded isn't from the same release as the
headers.

Next to the executable, not on PATH: Windows 10 and 11 have an older
onnxruntime.dll of their own in System32, and Windows looks there before it
looks at PATH, so a PATH-only setup picks up the wrong one and the program
fails to start (0xC000007B).

  #include <muninn/transcriber.h>
  muninn::Transcriber t("models/faster-whisper-large-v3-turbo", "cuda", "float16");
  auto result = t.transcribe("video.mp4");

See the README in the repository for the full API, the streaming API and the
VAD options.

License
-------
Muninn is under the Business Source License 1.1 (see LICENSE): free for any
non-commercial use; contact the licensor for commercial use. Each release
becomes MIT-licensed four years after it is published. The third-party parts
keep their own licenses; see THIRD_PARTY_NOTICES.txt.
