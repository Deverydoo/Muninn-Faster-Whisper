cmake_minimum_required(VERSION 3.15)

# vcpkg toolchain (must be before project)
set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "Vcpkg target triplet")

project(heimdall)

# Heimdall - The Vigilant Guardian of Audio Waveforms
# Named after the Norse god with incredible hearing who guards Bifröst

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set Python path explicitly (conda environment)
set(Python_EXECUTABLE "C:/Users/craig/miniconda3/envs/lokistudio312/python.exe")
set(Python_ROOT_DIR "C:/Users/craig/miniconda3/envs/lokistudio312")

# Find Python 3.12 specifically
find_package(Python 3.12 EXACT COMPONENTS Interpreter Development REQUIRED)

# pybind11 (from pip install pybind11)
execute_process(
    COMMAND ${Python_EXECUTABLE} -m pybind11 --cmakedir
    OUTPUT_VARIABLE pybind11_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
find_package(pybind11 CONFIG REQUIRED)

# FFmpeg via vcpkg (using pkg-config)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswresample
)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
)

# Source files
set(SOURCES
    src/heimdall.cpp
    src/audio_decoder.cpp
    src/peak_detector.cpp
)

set(BINDING_SOURCES
    src/bindings.cpp
)

# ============================================================================
# Build 1: C++ Shared Library (DLL) - For OdinUI C++ Integration
# ============================================================================

add_library(heimdall_lib SHARED ${SOURCES})

# Link FFmpeg static libraries
target_link_libraries(heimdall_lib PRIVATE PkgConfig::FFMPEG)

# Export symbols for DLL
target_compile_definitions(heimdall_lib PRIVATE HEIMDALL_BUILD_DLL)
if(MSVC)
    target_compile_definitions(heimdall_lib PRIVATE HEIMDALL_EXPORTS)
endif()

# Optimizations for DLL
if(MSVC)
    target_compile_options(heimdall_lib PRIVATE /O2 /arch:AVX2 /fp:fast)
else()
    target_compile_options(heimdall_lib PRIVATE -O3 -march=native -mavx2 -ffast-math)
endif()

# Set output name and directory for DLL
set_target_properties(heimdall_lib PROPERTIES
    OUTPUT_NAME "heimdall"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../lib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../lib
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../lib
)

# ============================================================================
# Build 2: Python Extension Module (PYD) - For Python Integration
# ============================================================================

pybind11_add_module(heimdall_pyd ${SOURCES} ${BINDING_SOURCES})

# Link FFmpeg static libraries
target_link_libraries(heimdall_pyd PRIVATE PkgConfig::FFMPEG)

# Mark pybind11 code sections
target_compile_definitions(heimdall_pyd PRIVATE HEIMDALL_PYTHON_BINDINGS)

# Optimizations for PYD
if(MSVC)
    target_compile_options(heimdall_pyd PRIVATE /O2 /arch:AVX2 /fp:fast)
else()
    target_compile_options(heimdall_pyd PRIVATE -O3 -march=native -mavx2 -ffast-math)
endif()

# Set output name and directory for PYD
set_target_properties(heimdall_pyd PROPERTIES
    OUTPUT_NAME "heimdall"
    PREFIX ""
    SUFFIX ".pyd"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/..
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/..
)

# ============================================================================
# Installation
# ============================================================================

# Install DLL to lib folder
install(TARGETS heimdall_lib
    LIBRARY DESTINATION ${CMAKE_SOURCE_DIR}/../lib
    RUNTIME DESTINATION ${CMAKE_SOURCE_DIR}/../lib
    ARCHIVE DESTINATION ${CMAKE_SOURCE_DIR}/../lib
)

# Install PYD to project root (for Python import)
install(TARGETS heimdall_pyd
    LIBRARY DESTINATION ${CMAKE_SOURCE_DIR}/..
    RUNTIME DESTINATION ${CMAKE_SOURCE_DIR}/..
)

# Install headers for C++ integration (if they exist)
file(GLOB HEADERS "${CMAKE_SOURCE_DIR}/src/*.h")
if(HEADERS)
    install(FILES ${HEADERS} DESTINATION ${CMAKE_SOURCE_DIR}/../include)
endif()
