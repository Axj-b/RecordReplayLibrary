/// @file detail/crc32.cpp
/// @brief CRC32 (ISO 3309 / ITU-T V.42) implementation — internal use only.
///
/// Uses the reflected 32-bit polynomial 0xEDB88320 (same as Ethernet, ZIP, PNG).
/// The table is generated at compile time via a constexpr helper so there is no
/// runtime initialisation cost and no risk of a hand-transcribed table being wrong.

#include "crc32.hpp"

// system includes
#include <cstddef>
#include <cstdint>

namespace recplay {
namespace detail {

namespace {

// ---------------------------------------------------------------------------
// Compile-time CRC32 table (reflected polynomial 0xEDB88320)
// ---------------------------------------------------------------------------

struct CrcTable {
    uint32_t data[256]{};

    constexpr CrcTable() noexcept {
        for (uint32_t i = 0; i < 256u; ++i) {
            uint32_t crc = i;
            for (int j = 0; j < 8; ++j)
                crc = (crc >> 1u) ^ (0xEDB88320u * (crc & 1u));
            data[i] = crc;
        }
    }
};

static constexpr CrcTable kCrcTable;

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

uint32_t Crc32(const void* data, size_t length, uint32_t crc) noexcept {
    const auto* bytes = static_cast<const uint8_t*>(data);
    crc = ~crc;  // pre-condition
    for (size_t i = 0; i < length; ++i)
        crc = kCrcTable.data[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8u);
    return ~crc;  // post-condition (ones-complement finalisation)
}

}  // namespace detail
}  // namespace recplay
