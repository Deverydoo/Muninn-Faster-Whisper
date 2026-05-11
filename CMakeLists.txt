cmake_minimum_required(VERSION 3.20)

# vcpkg toolchain (must be before project) - Same as Heimdall
set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "Vcpkg target triplet")

project(MuninnFasterWhisper VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CRITICAL: Force UTF-8 encoding for MSVC on Windows
# Without this, string literals with non-ASCII (like CJK) get double-encoded
# /utf-8 sets both /source-charset:utf-8 and /execution-charset:utf-8
if(MSVC)
    add_compile_options(/utf-8)
    message(STATUS "MSVC: Enabling UTF-8 source and execution charset")
endif()

# Dynamic CRT linkage to match Qt (Qt uses /MD by default)
# MSVC default is /MD, so we don't need to set it explicitly
# (Removed CMAKE_MSVC_RUNTIME_LIBRARY setting to use default)

# Options
option(WITH_CUDA "Build with CUDA support" ON)
option(WITH_SILERO_VAD "Build with Silero VAD (requires ONNX Runtime)" ON)
option(BUILD_SHARED_LIBS "Build as shared library (DLL)" ON)
option(BUILD_TESTS "Build test applications" ON)
option(BUILD_EXAMPLES "Build example applications" OFF)

# Output directories - all binaries go to build/Release for easy deployment
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release")

# =======================
# CTranslate2
# =======================
set(CTranslate2_DIR "${CMAKE_SOURCE_DIR}/third_party/CTranslate2" CACHE PATH "Path to CTranslate2")

# Search for CTranslate2 library in multiple locations:
# 1. Pre-built in lib/ directory (recommended for quick setup)
# 2. Built within third_party/CTranslate2/
find_library(CTRANSLATE2_LIB
    NAMES ctranslate2
    PATHS
        "${CMAKE_SOURCE_DIR}/lib"
        "${CTranslate2_DIR}/lib"
        "${CTranslate2_DIR}/build/Release"
        "${CTranslate2_DIR}/build"
    NO_DEFAULT_PATH
)

if(NOT CTRANSLATE2_LIB)
    message(FATAL_ERROR "CTranslate2 library not found! Please build CTranslate2 first.")
endif()

message(STATUS "Found CTranslate2 library: ${CTRANSLATE2_LIB}")

set(CTRANSLATE2_INCLUDE_DIR "${CTranslate2_DIR}/include")
if(NOT EXISTS "${CTRANSLATE2_INCLUDE_DIR}")
    message(FATAL_ERROR "CTranslate2 include directory not found: ${CTRANSLATE2_INCLUDE_DIR}")
endif()

# Find CTranslate2 DLL for copying
find_file(CTRANSLATE2_DLL
    NAMES ctranslate2.dll
    PATHS
        "${CMAKE_SOURCE_DIR}/lib"
        "${CTranslate2_DIR}/lib"
        "${CTranslate2_DIR}/build/Release"
        "${CTranslate2_DIR}/bin"
    NO_DEFAULT_PATH
)

# =======================
# CUDA Support
# =======================
if(WITH_CUDA)
    find_package(CUDAToolkit)
    if(CUDAToolkit_FOUND)
        message(STATUS "CUDA Toolkit found: ${CUDAToolkit_VERSION}")
        add_compile_definitions(WITH_CUDA)
    else()
        message(WARNING "CUDA Toolkit not found. Building CPU-only version.")
        set(WITH_CUDA OFF)
    endif()
endif()

# =======================
# FFmpeg for Internal Audio Decoder
# =======================
# Three ways to find FFmpeg, in priority order:
#   Option 1: FFMPEG_DIR + FFMPEG_STATIC=ON
#       Static MSVC build (lib<name>.lib naming). Used by Loki Studio's
#       "Muninn Pattern" — FFmpeg gets baked into muninn.dll so no
#       av*/sw*.dll need to ship at runtime. Adds the Windows system
#       libs FFmpeg's static link requires (bcrypt, crypt32, ws2_32, …).
#       This is the production build path as of v1.2.0.
#   Option 2: FFMPEG_DIR alone (no STATIC flag)
#       Dynamic FFmpeg with import libs (avformat.lib, not libavformat.lib).
#       Legacy path — muninn.dll ends up dynamically linked.
#   Option 3: neither set
#       pkg-config + vcpkg fallback.
if(FFMPEG_DIR AND FFMPEG_STATIC)
    message(STATUS "Using STATIC FFmpeg from: ${FFMPEG_DIR}")
    set(FFMPEG_INCLUDE_DIRS "${FFMPEG_DIR}/include")

    # zlib is pulled in transitively by avformat (matroska/http/id3v2) and
    # avcodec (png/exr/flashsv encoders) — needs to be on the link line
    # whenever we statically link those. Default lookup is vcpkg's
    # x64-windows-static-md triplet, same place Loki Studio's other Norse
    # modules find their static deps.
    if(NOT VCPKG_STATIC_DIR)
        set(VCPKG_STATIC_DIR "C:/vcpkg/installed/x64-windows-static-md"
            CACHE PATH "vcpkg x64-windows-static-md prefix (zlib + transitive static deps)")
    endif()
    set(ZLIB_STATIC_LIB "${VCPKG_STATIC_DIR}/lib/zlib.lib")
    if(NOT EXISTS "${ZLIB_STATIC_LIB}")
        message(FATAL_ERROR
            "Static zlib not found at ${ZLIB_STATIC_LIB}\n"
            "Required by static FFmpeg. Install via:\n"
            "    ${VCPKG_STATIC_DIR}/../../vcpkg install zlib:x64-windows-static-md")
    endif()

    # Link order matters: higher-level libs reference lower-level ones,
    # so they come first. avformat → avcodec → swr/sws → avutil → zlib.
    set(FFMPEG_LIBRARIES
        "${FFMPEG_DIR}/lib/libavformat.lib"
        "${FFMPEG_DIR}/lib/libavcodec.lib"
        "${FFMPEG_DIR}/lib/libswresample.lib"
        "${FFMPEG_DIR}/lib/libswscale.lib"
        "${FFMPEG_DIR}/lib/libavutil.lib"
        "${ZLIB_STATIC_LIB}"
        # Windows system libs FFmpeg's static link needs (collected from
        # the per-lib .pc files; all ship with the Windows SDK).
        bcrypt
        crypt32
        mfuuid
        ncrypt
        ole32
        secur32
        strmiids
        user32
        ws2_32
    )
    add_library(PkgConfig::FFMPEG INTERFACE IMPORTED)
    set_target_properties(PkgConfig::FFMPEG PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${FFMPEG_LIBRARIES}"
    )
elseif(FFMPEG_DIR)
    message(STATUS "Using DYNAMIC FFmpeg from: ${FFMPEG_DIR} (muninn.dll will need av*.dll at runtime)")
    set(FFMPEG_INCLUDE_DIRS "${FFMPEG_DIR}/include")
    set(FFMPEG_LIBRARIES
        "${FFMPEG_DIR}/lib/avformat.lib"
        "${FFMPEG_DIR}/lib/avcodec.lib"
        "${FFMPEG_DIR}/lib/avutil.lib"
        "${FFMPEG_DIR}/lib/swresample.lib"
    )
    add_library(PkgConfig::FFMPEG INTERFACE IMPORTED)
    set_target_properties(PkgConfig::FFMPEG PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${FFMPEG_LIBRARIES}"
    )
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavformat
        libavcodec
        libavutil
        libswresample
    )
    message(STATUS "Found FFmpeg via vcpkg")
endif()
message(STATUS "FFmpeg include: ${FFMPEG_INCLUDE_DIRS}")
message(STATUS "FFmpeg libs: ${FFMPEG_LIBRARIES}")

# =======================
# SentencePiece for NLLB Translation
# =======================
set(SENTENCEPIECE_DIR "${CMAKE_SOURCE_DIR}/third_party/sentencepiece" CACHE PATH "Path to SentencePiece")
set(SENTENCEPIECE_BUILD_DIR "${SENTENCEPIECE_DIR}/build")

find_library(SENTENCEPIECE_LIB
    NAMES sentencepiece
    PATHS
        "${SENTENCEPIECE_BUILD_DIR}/src/Release"
        "${SENTENCEPIECE_BUILD_DIR}/src"
    NO_DEFAULT_PATH
)

if(SENTENCEPIECE_LIB)
    set(SENTENCEPIECE_INCLUDE_DIR "${SENTENCEPIECE_DIR}/src")
    message(STATUS "Found SentencePiece: ${SENTENCEPIECE_LIB}")
    message(STATUS "SentencePiece includes: ${SENTENCEPIECE_INCLUDE_DIR}")
    set(SENTENCEPIECE_ENABLED ON)
    add_compile_definitions(MUNINN_USE_SENTENCEPIECE)
else()
    message(WARNING "SentencePiece not found. Translation will use basic tokenization.")
    set(SENTENCEPIECE_ENABLED OFF)
endif()

# =======================
# ONNX Runtime for Silero VAD
# =======================
if(WITH_SILERO_VAD)
    set(ONNXRUNTIME_DIR "${CMAKE_SOURCE_DIR}/third_party/onnx/onnxruntime-win-x64-gpu-1.23.2" CACHE PATH "Path to ONNX Runtime")

    find_library(ONNXRUNTIME_LIB
        NAMES onnxruntime
        PATHS
            "${ONNXRUNTIME_DIR}/lib"
            "${ONNXRUNTIME_DIR}/runtimes/win-x64/native"
            "${ONNXRUNTIME_DIR}/build/Release"
        NO_DEFAULT_PATH
    )

    # Find DLL
    find_file(ONNXRUNTIME_DLL
        NAMES onnxruntime.dll
        PATHS
            "${ONNXRUNTIME_DIR}/lib"
            "${ONNXRUNTIME_DIR}/runtimes/win-x64/native"
            "${ONNXRUNTIME_DIR}/bin"
        NO_DEFAULT_PATH
    )

    # Find include directory
    if(EXISTS "${ONNXRUNTIME_DIR}/include/onnxruntime_cxx_api.h")
        set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_DIR}/include")
    elseif(EXISTS "${ONNXRUNTIME_DIR}/build/native/include/onnxruntime_cxx_api.h")
        set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_DIR}/build/native/include")
    else()
        set(ONNXRUNTIME_INCLUDE_DIR "")
    endif()

    if(ONNXRUNTIME_LIB AND ONNXRUNTIME_INCLUDE_DIR)
        message(STATUS "Found ONNX Runtime: ${ONNXRUNTIME_LIB}")
        message(STATUS "ONNX Runtime includes: ${ONNXRUNTIME_INCLUDE_DIR}")
        add_compile_definitions(MUNINN_USE_SILERO_VAD)
        set(SILERO_VAD_ENABLED ON)
    else()
        message(WARNING "ONNX Runtime not found. Silero VAD will be disabled.")
        set(SILERO_VAD_ENABLED OFF)
    endif()
else()
    set(SILERO_VAD_ENABLED OFF)
endif()

# =======================
# Muninn Library Sources
# =======================
set(MUNINN_SOURCES
    src/logging.cpp
    src/mel_spectrogram.cpp
    src/transcriber.cpp
    src/streaming_transcriber.cpp
    src/translator.cpp
    src/audio_extractor.cpp
    src/audio/audio_decoder.cpp
    src/vad.cpp
    src/silero_vad.cpp
    src/ten_vad.cpp
    src/diarization.cpp
    src/subtitle_export.cpp
    # Caption Engine System
    src/caption_types.cpp
    src/caption_options.cpp
    src/caption_engine.cpp
    src/caption_engine_base.cpp
    src/caption_router.cpp
    # Caption Engine Families
    src/engines/cjk_engine.cpp
    src/engines/latin_engine.cpp
    src/engines/cyrillic_engine.cpp
    src/engines/rtl_engine.cpp
    src/engines/indic_engine.cpp
    src/engines/thai_engine.cpp
)

# =======================
# Build Muninn DLL
# =======================
if(BUILD_SHARED_LIBS)
    add_library(muninn SHARED ${MUNINN_SOURCES})
    # Use WINDOWS_EXPORT_ALL_SYMBOLS for automatic DLL export (Muninn pattern)
    set_target_properties(muninn PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
else()
    add_library(muninn STATIC ${MUNINN_SOURCES})
endif()

# Set output name
set_target_properties(muninn PROPERTIES
    OUTPUT_NAME "muninn"
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)

# Include directories
target_include_directories(muninn
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CTRANSLATE2_INCLUDE_DIR}
        ${FFMPEG_INCLUDE_DIRS}
)

if(SILERO_VAD_ENABLED)
    target_include_directories(muninn PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
endif()

if(SENTENCEPIECE_ENABLED)
    target_include_directories(muninn PRIVATE ${SENTENCEPIECE_INCLUDE_DIR})
endif()

# Find dav1d library (AV1 codec, required by FFmpeg)
find_library(DAV1D_LIB
    NAMES dav1d
    PATHS "C:/vcpkg/installed/x64-windows-static/lib"
    NO_DEFAULT_PATH
)
if(DAV1D_LIB)
    message(STATUS "Found dav1d: ${DAV1D_LIB}")
else()
    message(WARNING "dav1d not found - AV1 video decoding may fail")
endif()

# Link libraries
target_link_libraries(muninn
    PRIVATE
        ${CTRANSLATE2_LIB}
        PkgConfig::FFMPEG
)
if(DAV1D_LIB)
    target_link_libraries(muninn PRIVATE ${DAV1D_LIB})
endif()

if(SILERO_VAD_ENABLED)
    target_link_libraries(muninn PRIVATE ${ONNXRUNTIME_LIB})
endif()

if(SENTENCEPIECE_ENABLED)
    target_link_libraries(muninn PRIVATE ${SENTENCEPIECE_LIB})
endif()

if(WITH_CUDA)
    # Link to CUDA driver API (nvcuda.dll) for device enumeration
    # Avoid linking cudart statically - it conflicts with onnxruntime's cudart64_12.dll
    # CTranslate2 already provides the CUDA runtime it needs
    target_link_libraries(muninn PRIVATE CUDA::cuda_driver)
endif()

# MSVC Optimizations (LTCG disabled - incompatible with WINDOWS_EXPORT_ALL_SYMBOLS)
if(MSVC)
    target_compile_options(muninn PRIVATE /O2 /arch:AVX2 /fp:fast)
else()
    target_compile_options(muninn PRIVATE -O3 -march=native -mavx2 -ffast-math)
endif()

# =======================
# Copy Dependencies to Output
# =======================
if(BUILD_SHARED_LIBS)
    # Copy CTranslate2 DLL
    if(CTRANSLATE2_DLL)
        add_custom_command(TARGET muninn POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CTRANSLATE2_DLL}"
                "$<TARGET_FILE_DIR:muninn>"
            COMMENT "Copying ctranslate2.dll"
        )
    endif()

    # Copy ONNX Runtime DLL
    if(SILERO_VAD_ENABLED AND ONNXRUNTIME_DLL)
        add_custom_command(TARGET muninn POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_DLL}"
                "$<TARGET_FILE_DIR:muninn>"
            COMMENT "Copying onnxruntime.dll"
        )
    endif()
endif()

# =======================
# Test Application
# =======================
if(BUILD_TESTS)
    add_executable(muninn_test_app tests/muninn_test_app.cpp)
    target_link_libraries(muninn_test_app PRIVATE muninn)

    # Audio extraction test (tests internal audio decoder)
    add_executable(test_audio_extraction tests/test_audio_extraction.cpp)
    target_link_libraries(test_audio_extraction PRIVATE muninn)

    # Translation test (transcribe + translate to multiple languages)
    add_executable(test_translation tests/test_translation.cpp)
    target_link_libraries(test_translation PRIVATE muninn)

    # Quick UTF-8 output test (translation only, no transcription)
    add_executable(test_utf8_output tests/test_utf8_output.cpp)
    target_link_libraries(test_utf8_output PRIVATE muninn)

    # VAD parameter tuning test (iterates through VAD configs for CJK optimization)
    add_executable(vad_tuning_app tests/vad_tuning_app.cpp)
    target_link_libraries(vad_tuning_app PRIVATE muninn)

    # Ensure test apps can find DLLs
    if(BUILD_SHARED_LIBS)
        set_target_properties(muninn_test_app PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
        )
        set_target_properties(test_audio_extraction PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
        )
        set_target_properties(test_translation PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
        )
        set_target_properties(test_utf8_output PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
        )
        set_target_properties(vad_tuning_app PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
        )
    endif()
endif()

# =======================
# Examples
# =======================
if(BUILD_EXAMPLES)
    add_executable(basic_transcription examples/basic_transcription.cpp)
    target_link_libraries(basic_transcription PRIVATE muninn)

    # Translation example (transcribe + translate to multiple languages)
    add_executable(translation_example examples/translation_example.cpp)
    target_link_libraries(translation_example PRIVATE muninn)

    # Diarization example (requires ONNX Runtime and StreamingTranscriber)
    if(SILERO_VAD_ENABLED)
        # Note: diarization_streaming disabled until streaming_transcriber.cpp is fixed
        # add_executable(diarization_streaming examples/diarization_streaming.cpp)
        # target_include_directories(diarization_streaming PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
        # target_link_libraries(diarization_streaming PRIVATE muninn ${ONNXRUNTIME_LIB})

        add_executable(subtitle_export examples/subtitle_export.cpp)
        target_include_directories(subtitle_export PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
        target_link_libraries(subtitle_export PRIVATE muninn ${ONNXRUNTIME_LIB})
    else()
        message(STATUS "Skipping diarization examples (ONNX Runtime not found)")
    endif()
endif()

# =======================
# Installation
# =======================
include(GNUInstallDirs)

install(TARGETS muninn
    EXPORT MuninnTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/muninn
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# =======================
# Summary
# =======================
message(STATUS "")
message(STATUS "═══════════════════════════════════════════════════════════")
message(STATUS "Muninn Faster-Whisper Configuration Summary")
message(STATUS "═══════════════════════════════════════════════════════════")
message(STATUS "Version:          ${PROJECT_VERSION}")
message(STATUS "C++ Standard:     C++${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type:       ${CMAKE_BUILD_TYPE}")
message(STATUS "Library Type:     ${BUILD_SHARED_LIBS}")
message(STATUS "Toolchain:        vcpkg (x64-windows-static)")
message(STATUS "CUDA Support:     ${WITH_CUDA}")
message(STATUS "Audio Decoder:    Internal (FFmpeg via vcpkg)")
message(STATUS "Silero VAD:       ${SILERO_VAD_ENABLED}")
message(STATUS "SentencePiece:    ${SENTENCEPIECE_ENABLED}")
message(STATUS "CTranslate2:      ${CTRANSLATE2_LIB}")
if(CTRANSLATE2_DLL)
    message(STATUS "CTranslate2 DLL:  ${CTRANSLATE2_DLL}")
endif()
if(ONNXRUNTIME_DLL)
    message(STATUS "ONNX Runtime DLL: ${ONNXRUNTIME_DLL}")
endif()
message(STATUS "Build Tests:      ${BUILD_TESTS}")
message(STATUS "Build Examples:   ${BUILD_EXAMPLES}")
message(STATUS "Output Directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "═══════════════════════════════════════════════════════════")
message(STATUS "")
