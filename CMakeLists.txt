cmake_minimum_required(VERSION 3.20)
project(MuninnFasterWhisper VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(WITH_CUDA "Build with CUDA support" ON)
option(WITH_SILERO_VAD "Build with Silero VAD (requires ONNX Runtime)" OFF)
option(BUILD_TESTS "Build test applications" ON)
option(BUILD_EXAMPLES "Build example applications" ON)

# Find CTranslate2 (via symlink or CMake config)
set(CTranslate2_DIR "${CMAKE_SOURCE_DIR}/third_party/CTranslate2" CACHE PATH "Path to CTranslate2")

# Try to find CTranslate2 library and headers
find_library(CTRANSLATE2_LIB
    NAMES ctranslate2
    PATHS
        "${CTranslate2_DIR}/lib"
        "${CTranslate2_DIR}/build/Release"
        "${CTranslate2_DIR}/build"
    NO_DEFAULT_PATH
)

if(NOT CTRANSLATE2_LIB)
    message(FATAL_ERROR "CTranslate2 library not found! Please run setup_dependencies.bat first.")
endif()

message(STATUS "Found CTranslate2 library: ${CTRANSLATE2_LIB}")

# CTranslate2 include directory
set(CTRANSLATE2_INCLUDE_DIR "${CTranslate2_DIR}/include")
if(NOT EXISTS "${CTRANSLATE2_INCLUDE_DIR}")
    message(FATAL_ERROR "CTranslate2 include directory not found: ${CTRANSLATE2_INCLUDE_DIR}")
endif()

# CUDA support
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
# Heimdall Audio Decoder (DLL)
# =======================

set(HEIMDALL_DIR "${CMAKE_SOURCE_DIR}/lib")

# Find Heimdall DLL and import library
find_library(HEIMDALL_LIB heimdall
    HINTS "${HEIMDALL_DIR}"
    NO_DEFAULT_PATH
)

if(HEIMDALL_LIB)
    message(STATUS "Found Heimdall: ${HEIMDALL_LIB}")
    set(WITH_HEIMDALL ON)
else()
    message(WARNING "Heimdall not found in ${HEIMDALL_DIR}")
    message(WARNING "Audio file loading will not be available.")
    message(WARNING "You can still use transcribe(samples, sample_rate) API.")
    set(WITH_HEIMDALL OFF)
endif()

# =======================
# Muninn Library
# =======================

set(MUNINN_SOURCES
    src/mel_spectrogram.cpp
    src/transcriber.cpp
    src/audio_extractor.cpp
    src/vad.cpp
    src/silero_vad.cpp
)

# Enable Heimdall if available
if(WITH_HEIMDALL)
    add_compile_definitions(WITH_HEIMDALL)
endif()

# =======================
# ONNX Runtime for Silero VAD (optional)
# =======================

if(WITH_SILERO_VAD)
    set(ONNXRUNTIME_DIR "${CMAKE_SOURCE_DIR}/third_party/onnx/onnxruntime-win-x64-gpu-1.23.2" CACHE PATH "Path to ONNX Runtime")

    # Support multiple layouts:
    # 1. GitHub C API release: lib/onnxruntime.lib, include/
    # 2. NuGet package: runtimes/win-x64/native/*.lib, build/native/include/
    find_library(ONNXRUNTIME_LIB
        NAMES onnxruntime
        PATHS
            "${ONNXRUNTIME_DIR}/lib"
            "${ONNXRUNTIME_DIR}/runtimes/win-x64/native"
            "${ONNXRUNTIME_DIR}/build/Release"
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
    elseif(ONNXRUNTIME_LIB)
        message(WARNING "ONNX Runtime library found but headers missing.")
        message(WARNING "Headers expected at: ${ONNXRUNTIME_DIR}/include/ or ${ONNXRUNTIME_DIR}/build/native/include/")
        message(WARNING "Download C API release from: https://github.com/microsoft/onnxruntime/releases")
        set(SILERO_VAD_ENABLED OFF)
    else()
        message(WARNING "ONNX Runtime not found. Silero VAD will be disabled.")
        message(WARNING "Download from: https://github.com/microsoft/onnxruntime/releases")
        set(SILERO_VAD_ENABLED OFF)
    endif()
else()
    set(SILERO_VAD_ENABLED OFF)
endif()

add_library(muninn_faster_whisper STATIC
    ${MUNINN_SOURCES}
)

target_include_directories(muninn_faster_whisper
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CTRANSLATE2_INCLUDE_DIR}
)

if(WITH_HEIMDALL)
    target_include_directories(muninn_faster_whisper
        PRIVATE
            ${HEIMDALL_DIR}/include
    )
endif()

if(SILERO_VAD_ENABLED)
    target_include_directories(muninn_faster_whisper
        PRIVATE
            ${ONNXRUNTIME_INCLUDE_DIR}
    )
endif()

target_link_libraries(muninn_faster_whisper
    PUBLIC
        ${CTRANSLATE2_LIB}
)

if(SILERO_VAD_ENABLED)
    target_link_libraries(muninn_faster_whisper
        PUBLIC
            ${ONNXRUNTIME_LIB}
    )
endif()

if(WITH_HEIMDALL)
    target_link_libraries(muninn_faster_whisper
        PUBLIC
            ${HEIMDALL_LIB}
    )
endif()

if(WITH_CUDA)
    target_link_libraries(muninn_faster_whisper
        PUBLIC
            CUDA::cudart
    )
endif()

# =======================
# Test Application
# =======================

if(BUILD_TESTS)
    add_executable(muninn_test_app
        tests/muninn_test_app.cpp
    )

    target_link_libraries(muninn_test_app
        PRIVATE
            muninn_faster_whisper
    )
endif()

# =======================
# Examples
# =======================

if(BUILD_EXAMPLES)
    add_executable(basic_transcription
        examples/basic_transcription.cpp
    )

    target_link_libraries(basic_transcription
        PRIVATE
            muninn_faster_whisper
    )
endif()

# =======================
# Installation
# =======================

install(TARGETS muninn_faster_whisper
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY include/muninn
    DESTINATION include
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
message(STATUS "CUDA Support:     ${WITH_CUDA}")
message(STATUS "Heimdall:         ${WITH_HEIMDALL}")
message(STATUS "Silero VAD:       ${SILERO_VAD_ENABLED}")
message(STATUS "CTranslate2:      ${CTRANSLATE2_LIB}")
message(STATUS "Build Tests:      ${BUILD_TESTS}")
message(STATUS "Build Examples:   ${BUILD_EXAMPLES}")
message(STATUS "═══════════════════════════════════════════════════════════")
message(STATUS "")
