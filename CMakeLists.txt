cmake_minimum_required(VERSION 3.20)
project(MuninnFasterWhisper VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(WITH_CUDA "Build with CUDA support" ON)
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
# Heimdall Audio Decoder
# =======================

set(HEIMDALL_DIR "${CMAKE_SOURCE_DIR}/third_party/heimdall")

# Find FFmpeg libraries
find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h
    HINTS
        "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/include"
        "${HEIMDALL_DIR}/ffmpeg/include"
        "C:/ffmpeg/include"
        "C:/Program Files/ffmpeg/include"
)

find_library(AVFORMAT_LIB avformat
    HINTS
        "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/lib"
        "${HEIMDALL_DIR}/ffmpeg/lib"
        "C:/ffmpeg/lib"
        "C:/Program Files/ffmpeg/lib"
)

find_library(AVCODEC_LIB avcodec
    HINTS
        "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/lib"
        "${HEIMDALL_DIR}/ffmpeg/lib"
        "C:/ffmpeg/lib"
        "C:/Program Files/ffmpeg/lib"
)

find_library(AVUTIL_LIB avutil
    HINTS
        "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/lib"
        "${HEIMDALL_DIR}/ffmpeg/lib"
        "C:/ffmpeg/lib"
        "C:/Program Files/ffmpeg/lib"
)

find_library(SWRESAMPLE_LIB swresample
    HINTS
        "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/lib"
        "${HEIMDALL_DIR}/ffmpeg/lib"
        "C:/ffmpeg/lib"
        "C:/Program Files/ffmpeg/lib"
)

if(NOT FFMPEG_INCLUDE_DIR OR NOT AVFORMAT_LIB)
    message(WARNING "FFmpeg not found. Audio file loading will not be available.")
    message(WARNING "You can still use transcribe(samples, sample_rate) API.")
    set(WITH_FFMPEG OFF)
else()
    message(STATUS "FFmpeg found: ${FFMPEG_INCLUDE_DIR}")
    set(WITH_FFMPEG ON)
endif()

# =======================
# Muninn Library
# =======================

set(MUNINN_SOURCES
    src/mel_spectrogram.cpp
    src/transcriber.cpp
    src/audio_extractor.cpp
)

# Add Heimdall source if FFmpeg is available
if(WITH_FFMPEG)
    list(APPEND MUNINN_SOURCES
        ${HEIMDALL_DIR}/src/audio_decoder.cpp
    )
    add_compile_definitions(WITH_FFMPEG)
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

if(WITH_FFMPEG)
    target_include_directories(muninn_faster_whisper
        PRIVATE
            ${FFMPEG_INCLUDE_DIR}
            ${HEIMDALL_DIR}/src
    )
endif()

target_link_libraries(muninn_faster_whisper
    PUBLIC
        ${CTRANSLATE2_LIB}
)

if(WITH_FFMPEG)
    target_link_libraries(muninn_faster_whisper
        PUBLIC
            ${AVFORMAT_LIB}
            ${AVCODEC_LIB}
            ${AVUTIL_LIB}
            ${SWRESAMPLE_LIB}
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
message(STATUS "CTranslate2:      ${CTRANSLATE2_LIB}")
message(STATUS "Build Tests:      ${BUILD_TESTS}")
message(STATUS "Build Examples:   ${BUILD_EXAMPLES}")
message(STATUS "═══════════════════════════════════════════════════════════")
message(STATUS "")
