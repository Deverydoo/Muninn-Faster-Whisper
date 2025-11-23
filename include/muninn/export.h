#pragma once

/**
 * @file export.h
 * @brief DLL export/import macros for Muninn library
 *
 * NOTE: With WINDOWS_EXPORT_ALL_SYMBOLS, CMake auto-generates exports.
 *       MUNINN_API is kept as empty macro for compatibility but has no effect.
 */

// MUNINN_API is now a no-op - WINDOWS_EXPORT_ALL_SYMBOLS handles exports
#define MUNINN_API

// For classes that should not be exported (internal use only)
#define MUNINN_INTERNAL
