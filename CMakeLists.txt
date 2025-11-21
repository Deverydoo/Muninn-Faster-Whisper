cmake_minimum_required(VERSION 3.20)

# vcpkg toolchain (must be before project) - Same as Heimdall
set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "Vcpkg target triplet")

project(MuninnFasterWhisper VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT linkage (same as Heimdall)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Options
option(WITH_CUDA "Build with CUDA support" ON)
option(WITH_SILERO_VAD "Build with Silero VAD (requires ONNX Runtime)" OFF)
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
# FFmpeg for Internal Audio Decoder (via vcpkg - same as Heimdall)
# =======================
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswresample
)
message(STATUS "Found FFmpeg via vcpkg")
message(STATUS "FFmpeg include: ${FFMPEG_INCLUDE_DIRS}")
message(STATUS "FFmpeg libs: ${FFMPEG_LIBRARIES}")

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
    src/mel_spectrogram.cpp
    src/transcriber.cpp
    src/audio_extractor.cpp
    src/audio/audio_decoder.cpp
    src/vad.cpp
    src/silero_vad.cpp
)

# =======================
# Build Muninn DLL
# =======================
if(BUILD_SHARED_LIBS)
    add_library(muninn SHARED ${MUNINN_SOURCES})
    target_compile_definitions(muninn PRIVATE MUNINN_EXPORTS)
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

# Link libraries
target_link_libraries(muninn
    PRIVATE
        ${CTRANSLATE2_LIB}
        PkgConfig::FFMPEG
)

if(SILERO_VAD_ENABLED)
    target_link_libraries(muninn PRIVATE ${ONNXRUNTIME_LIB})
endif()

if(WITH_CUDA)
    target_link_libraries(muninn PRIVATE CUDA::cudart)
endif()

# MSVC Optimizations (same as Heimdall)
if(MSVC)
    target_compile_options(muninn PRIVATE /O2 /arch:AVX2 /fp:fast /GL)
    target_link_options(muninn PRIVATE /LTCG)
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

    # Ensure test apps can find DLLs
    if(BUILD_SHARED_LIBS)
        set_target_properties(muninn_test_app PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
        )
        set_target_properties(test_audio_extraction PROPERTIES
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
