#pragma once

/// @file detail/platform.hpp
/// @brief Platform detection and portable utility helpers — internal use only.
///
/// Include this header (rather than platform SDK headers) anywhere platform
/// differences must be papered over.

// system includes
#include <cstdint>
#include <ctime>

// ---------------------------------------------------------------------------
// Platform detection
// ---------------------------------------------------------------------------

#if defined(_WIN32) || defined(__CYGWIN__)
#   define RECPLAY_PLATFORM_WINDOWS 1
#else
#   define RECPLAY_PLATFORM_WINDOWS 0
#endif

#if defined(__linux__)
#   define RECPLAY_PLATFORM_LINUX 1
#else
#   define RECPLAY_PLATFORM_LINUX 0
#endif

// ---------------------------------------------------------------------------
// Byte-order detection
// ---------------------------------------------------------------------------
// At the moment the .rec format mandates little-endian on-disk layout and the
// library targets x86/x86_64 on both Windows and Linux (both little-endian).
// Byte-swap helpers are provided so that a future big-endian port only needs
// to flip the RECPLAY_LITTLE_ENDIAN guard and implement the serialise paths.

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
#   if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#       define RECPLAY_LITTLE_ENDIAN 1
#   else
#       define RECPLAY_LITTLE_ENDIAN 0
#   endif
#elif defined(_WIN32)
    // Windows on x86/x86_64/ARM (all LE in practice)
#   define RECPLAY_LITTLE_ENDIAN 1
#else
#   define RECPLAY_LITTLE_ENDIAN 1  // safe default for x86/x86_64 Linux
#endif

namespace recplay {
namespace detail {

/// Byte-swap helpers (no-ops on LE platforms).

inline uint16_t HostToLe16(uint16_t v) noexcept {
#if RECPLAY_LITTLE_ENDIAN
    return v;
#else
    return static_cast<uint16_t>((v >> 8u) | (v << 8u));
#endif
}

inline uint32_t HostToLe32(uint32_t v) noexcept {
#if RECPLAY_LITTLE_ENDIAN
    return v;
#else
    return ((v & 0x000000FFu) << 24u) |
           ((v & 0x0000FF00u) <<  8u) |
           ((v & 0x00FF0000u) >>  8u) |
           ((v & 0xFF000000u) >> 24u);
#endif
}

inline uint64_t HostToLe64(uint64_t v) noexcept {
#if RECPLAY_LITTLE_ENDIAN
    return v;
#else
    return ((uint64_t)HostToLe32(static_cast<uint32_t>(v))             << 32u) |
            (uint64_t)HostToLe32(static_cast<uint32_t>(v >> 32u));
#endif
}

// Le → host is the same operation as host → Le (both are XOR-symmetric).
inline uint16_t Le16ToHost(uint16_t v) noexcept { return HostToLe16(v); }
inline uint32_t Le32ToHost(uint32_t v) noexcept { return HostToLe32(v); }
inline uint64_t Le64ToHost(uint64_t v) noexcept { return HostToLe64(v); }

// ---------------------------------------------------------------------------
// Portable gmtime (thread-safe variant)
// ---------------------------------------------------------------------------

/// Converts a UTC time_t to a broken-down std::tm using the platform's
/// thread-safe gmtime variant.
/// @return true on success.
inline bool GmTimePortable(const std::time_t* time, std::tm* result) noexcept {
#if RECPLAY_PLATFORM_WINDOWS
    return ::gmtime_s(result, time) == 0;
#else
    return ::gmtime_r(time, result) != nullptr;
#endif
}

// ---------------------------------------------------------------------------
// Portable path separator
// ---------------------------------------------------------------------------

#if RECPLAY_PLATFORM_WINDOWS
    constexpr char PATH_SEP = '\\';
#else
    constexpr char PATH_SEP = '/';
#endif

}  // namespace detail
}  // namespace recplay
