#pragma once

/// @file channel.hpp
/// @brief Channel definition and configuration types.

// local includes
#include "types.hpp"
#include "format.hpp"

// system includes
#include <cstdint>
#include <string>
#include <vector>

namespace recplay {

    // ---------------------------------------------------------------------------
    // ChannelConfig — provided by the user when opening a channel for writing
    // ---------------------------------------------------------------------------

    /// Configuration for a single recording channel.
/// Passed to RecorderSession::DefineChannel().
struct ChannelConfig {
    /// Logical name for this channel, e.g. "lidar/front" or "can/chassis".
    /// Must be unique within a session. UTF-8. Max 255 bytes.
    std::string Name;

    /// OSI capture layer (L3L4 = raw UDP/TCP, L7 = application message).
    CaptureLayer Layer = CaptureLayer::L7;

    /// Per-channel compression codec.
    CompressionCodec Compression = CompressionCodec::LZ4;

    /// Maximum uncompressed bytes accumulated before a CHUNK is flushed to disk.
    /// Only relevant when Compression != None. Default: DEFAULT_CHUNK_BYTES (256 KiB).
    uint32_t ChunkSizeBytes = format::DEFAULT_CHUNK_BYTES;

    /// Maximum milliseconds between automatic chunk flushes even if ChunkSizeBytes
    /// has not been reached. Default: DEFAULT_CHUNK_FLUSH_MS (500 ms).
    uint32_t ChunkFlushIntervalMs = format::DEFAULT_CHUNK_FLUSH_MS;

    /// Optional schema / type identifier string (e.g. MIME type, Protobuf message name,
    /// or custom IDL tag). Stored verbatim in the CHANNEL_DEF record.
    /// Empty means the channel is schema-less raw bytes.
    std::string Schema;

    /// Optional opaque user metadata blob (e.g. serialized calibration, sensor params).
    /// Stored verbatim in the CHANNEL_DEF record; not interpreted by the library.
    std::vector<uint8_t> UserMetadata;
};

    // ---------------------------------------------------------------------------
    // ChannelDef — what the library returns when describing a recorded channel
    // ---------------------------------------------------------------------------

    /// Read-only description of a channel as written to (or read from) a session.
/// Returned by RecorderSession::GetChannelDef() and ReaderSession::Channels().
struct ChannelDef {
    ChannelId        Id;                 ///< Runtime channel ID assigned by the session
    std::string      Name;               ///< Logical channel name
    CaptureLayer     Layer;              ///< Capture OSI layer
    CompressionCodec Compression;        ///< Codec used for this channel
    uint32_t         ChunkSizeBytes;
    uint32_t         ChunkFlushIntervalMs;
    std::string      Schema;             ///< Schema identifier (may be empty)
    std::vector<uint8_t> UserMetadata;   ///< User metadata blob (may be empty)

    /// Convenience: true if this channel stores raw L3/L4 packet captures
    bool IsRawPacket() const noexcept { return Layer == CaptureLayer::L3L4; }

    /// Convenience: true if this channel is compressed
    bool IsCompressed() const noexcept { return Compression != CompressionCodec::None; }
};

}  // namespace recplay