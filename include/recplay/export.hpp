#pragma once

/// @file export.hpp
/// @brief DLL export/import visibility macros for the recplay library.
///
/// Consumers of recplay as a **static library** do not need to define anything.
/// When building as a **shared library** (DLL / .so):
///
///   - The library build sets `RECPLAY_BUILDING_LIBRARY` (done automatically by CMake).
///   - Consumers must define `RECPLAY_SHARED_LIBRARY` when linking against the DLL.
///     With CMake this is handled automatically via the imported target.
///
/// Usage: decorate public class / function declarations with `RECPLAY_API`:
/// @code
///   class RECPLAY_API MyClass { ... };
///   RECPLAY_API void  MyFunction();
/// @endcode

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(RECPLAY_BUILDING_LIBRARY)
        #define RECPLAY_API __declspec(dllexport)
    #elif defined(RECPLAY_SHARED_LIBRARY)
        #define RECPLAY_API __declspec(dllimport)
    #else
        #define RECPLAY_API  // static link — no decoration needed
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #if defined(RECPLAY_BUILDING_LIBRARY)
        #define RECPLAY_API __attribute__((visibility("default")))
    #else
        #define RECPLAY_API
    #endif
#else
    #define RECPLAY_API
#endif
