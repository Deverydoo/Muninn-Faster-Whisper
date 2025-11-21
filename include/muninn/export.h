#pragma once

/**
 * @file export.h
 * @brief DLL export/import macros for Muninn library
 */

#ifdef _WIN32
    #ifdef MUNINN_EXPORTS
        #define MUNINN_API __declspec(dllexport)
    #else
        #define MUNINN_API __declspec(dllimport)
    #endif
#else
    #ifdef MUNINN_EXPORTS
        #define MUNINN_API __attribute__((visibility("default")))
    #else
        #define MUNINN_API
    #endif
#endif

// For classes that should not be exported (internal use only)
#define MUNINN_INTERNAL
