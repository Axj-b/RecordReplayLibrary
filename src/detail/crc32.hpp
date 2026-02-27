#pragma once

/// @file detail/crc32.hpp
/// @brief Fast CRC32 (ISO 3309 / ITU-T V.42) — internal use only.

// system includes
#include <cstddef>
#include <cstdint>

namespace recplay {
namespace detail {

/// Compute CRC32 of a buffer.
/// @param data    Pointer to input bytes.
/// @param length  Number of bytes.
/// @param crc     Previous CRC value for chained computation (default 0).
/// @return Updated CRC32.
uint32_t Crc32(const void* data, size_t length, uint32_t crc = 0) noexcept;

/// Helper: feed a POD value directly.
template<typename T>
inline uint32_t Crc32Pod(const T& value, uint32_t crc = 0) noexcept {
    return Crc32(&value, sizeof(T), crc);
}

}  // namespace detail
}  // namespace recplay
