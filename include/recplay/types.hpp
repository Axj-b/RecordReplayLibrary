#pragma once

// system includes
#include <array>
#include <cstdint>
#include <string>

namespace recplay {

// ---------------------------------------------------------------------------
// Basic typedefs
// ---------------------------------------------------------------------------

/// Nanosecond-precision timestamp (UTC wall clock or hardware NIC timestamp)
using Timestamp = uint64_t;

/// Identifies a channel within a session (max 65534 channels; 0xFFFF = reserved)
using ChannelId = uint16_t;

constexpr ChannelId INVALID_CHANNEL_ID = 0xFFFFu;

/// 128-bit session identifier (stored as raw bytes, intended to be a UUID v4)
using SessionId = std::array<uint8_t, 16>;

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

/// Compression codec applied to chunk payloads.
/// Stored as a 1-byte value inside CHUNK records.
enum class CompressionCodec : uint8_t {
    None = 0x00, ///< No compression; raw bytes
    LZ4  = 0x01, ///< LZ4 frame format (fast, low CPU)
    Zstd = 0x02, ///< Zstandard (better ratio, more CPU)
};

/// At which OSI layer the data was captured.
/// Informational — stored in CHANNEL_DEF records and the session manifest.
enum class CaptureLayer : uint8_t {
    L3L4 = 0x01, ///< IP/UDP or IP/TCP — raw packet including addressing headers
    L7   = 0x02, ///< Application / middleware layer — serialized logical message
};

/// Record type opcodes written into the .rec binary stream.
enum class RecordOp : uint8_t {
    SessionStart = 0x01, ///< Session/segment open metadata
    ChannelDef   = 0x02, ///< Channel definition (name, codec, layer, schema)
    Data         = 0x03, ///< Single raw message payload (uncompressed channel)
    Chunk        = 0x04, ///< Compressed block containing N DATA records
    Index        = 0x05, ///< Seek table written once per segment at close
    Annotation   = 0x06, ///< User-defined label / bookmark at a timestamp
    SessionEnd   = 0x07, ///< Segment close marker
};

/// Bit flags on RecordEnvelope::Flags
enum RecordFlags : uint8_t {
    RECORD_FLAG_COMPRESSED = 0x01u, ///< Payload is compressed (CHUNK record)
    RECORD_FLAG_CRC        = 0x02u, ///< Crc32 field is valid
};

/// Bit flags on FileHeader::Flags
enum FileFlags : uint16_t {
    FILE_FLAG_COMPRESSION_ENABLED = 0x0001u,
    FILE_FLAG_CRC_ENABLED         = 0x0002u,
};

// ---------------------------------------------------------------------------
// Result type
// ---------------------------------------------------------------------------

/// Lightweight result/error code returned by library operations.
enum class Status : int32_t {
    Ok               =  0,
    ErrorInvalidArg  = -1,
    ErrorIO          = -2,
    ErrorCorrupted   = -3,
    ErrorNotFound    = -4,
    ErrorOutOfOrder  = -5,
    ErrorFull        = -6, ///< Segment size limit reached (internal — triggers rotation)
    ErrorNotOpen     = -7,
    ErrorAlreadyOpen = -8,
};

inline bool IsOk(Status s) noexcept { return s == Status::Ok; }

/// Convert a Status to a human-readable string (defined in version.cpp).
const char* ToString(Status s) noexcept;

}  // namespace recplay
