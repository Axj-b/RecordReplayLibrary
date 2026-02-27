#pragma once

/// @file format.hpp
/// @brief On-disk binary layout of the .rec file format.
///
/// All multi-byte integer fields are little-endian.
/// Packed structs define the exact on-disk representation; they must not be
/// used as general-purpose data structures inside the library.

// local includes
#include "types.hpp"

// system includes
#include <cstddef>
#include <cstdint>

namespace recplay {
namespace format {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// Magic bytes at the start of every .rec file header and footer.
/// Pattern inspired by PNG: high bit set, "REC", CRLF, EOF, LF.
constexpr uint8_t MAGIC[8] = { 0x89, 'R', 'E', 'C', '\r', '\n', 0x1A, '\n' };

constexpr uint8_t  VERSION_MAJOR         = 1;
constexpr uint8_t  VERSION_MINOR         = 0;

/// Maximum number of channels per session.
constexpr uint16_t MAX_CHANNELS          = 0xFFFEu; // 0xFFFF is reserved

/// Default chunk flush size in bytes (256 KiB)
constexpr uint32_t DEFAULT_CHUNK_BYTES   = 256u * 1024u;

/// Default chunk flush interval in milliseconds
constexpr uint32_t DEFAULT_CHUNK_FLUSH_MS = 500u;

/// Default segment size limit (1 GiB)
constexpr uint64_t DEFAULT_MAX_SEGMENT_BYTES = 1ull * 1024ull * 1024ull * 1024ull;

/// Sentinel value for IndexOffset in the footer when no index was written.
constexpr uint64_t NO_INDEX = UINT64_MAX;

// ---------------------------------------------------------------------------
// File Header  (fixed 64 bytes, at file offset 0)
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

struct FileHeader {
    uint8_t  Magic[8];       ///< MAGIC
    uint8_t  VersionMajor;   ///< VERSION_MAJOR
    uint8_t  VersionMinor;   ///< VERSION_MINOR
    uint8_t  SessionId[16];  ///< UUID v4 raw bytes — ties all segments together
    uint32_t SegmentIndex;   ///< 0-based segment number within the session
    uint64_t CreatedAtNs;    ///< Wall-clock UTC timestamp at segment open
    uint16_t Flags;          ///< FileFlags bitmask
    uint8_t  Reserved[24];   ///< Zero-padded, reserved for future use
};
static_assert(sizeof(FileHeader) == 64, "FileHeader must be 64 bytes");

// ---------------------------------------------------------------------------
// Record Envelope  (20 bytes, precedes every record payload)
// ---------------------------------------------------------------------------

struct RecordEnvelope {
    uint8_t  Op;            ///< RecordOp — identifies the record type
    uint16_t Channel;       ///< ChannelId; 0xFFFF for session-level records
    uint8_t  Flags;         ///< RecordFlags bitmask
    uint32_t PayloadLength; ///< Byte length of the payload that follows
    uint64_t TimestampNs;   ///< Capture timestamp (hardware or CLOCK_REALTIME)
    uint32_t Crc32;         ///< CRC32 of (envelope bytes 0-15) + payload; 0 if CRC disabled
};
static_assert(sizeof(RecordEnvelope) == 20, "RecordEnvelope must be 20 bytes");

// ---------------------------------------------------------------------------
// Chunk inline header  (9 bytes, first bytes of a CHUNK record's payload)
// ---------------------------------------------------------------------------

struct ChunkHeader {
    uint8_t  Codec;               ///< CompressionCodec
    uint32_t UncompressedLength;  ///< Byte size after decompression
    uint32_t RecordCount;         ///< Number of DATA records packed in this chunk
    // Followed by: compressed_data[PayloadLength - sizeof(ChunkHeader)]
};
static_assert(sizeof(ChunkHeader) == 9, "ChunkHeader must be 9 bytes");

// ---------------------------------------------------------------------------
// Index entry  (18 bytes each, packed array inside an INDEX record payload)
// ---------------------------------------------------------------------------

struct IndexEntry {
    uint16_t Channel;     ///< Channel this entry belongs to
    uint64_t TimestampNs; ///< Timestamp of the referenced record / chunk start
    uint64_t FileOffset;  ///< Byte offset of the RecordEnvelope in this segment file
};
static_assert(sizeof(IndexEntry) == 18, "IndexEntry must be 18 bytes");

/// Leading field of an INDEX record payload (before the IndexEntry array).
struct IndexHeader {
    uint32_t EntryCount; ///< Number of IndexEntry structs that follow
};
static_assert(sizeof(IndexHeader) == 4, "IndexHeader must be 4 bytes");

// ---------------------------------------------------------------------------
// File Footer  (fixed 32 bytes, at the last 32 bytes of a complete segment)
// ---------------------------------------------------------------------------

struct FileFooter {
    uint8_t  Magic[8];     ///< MAGIC — presence confirms the segment was cleanly closed
    uint64_t IndexOffset;  ///< Byte offset of the INDEX RecordEnvelope; NO_INDEX if absent
    uint64_t RecordCount;  ///< Total RecordEnvelope records written to this segment
    uint32_t Crc32;        ///< CRC32 of footer bytes 0-23 (Magic+IndexOffset+RecordCount); Crc32 field itself is excluded
    uint8_t  Reserved[4];  ///< Zero-padded
};
static_assert(sizeof(FileFooter) == 32, "FileFooter must be 32 bytes");

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Variable-length record payload layouts (not packed structs — documented here)
// ---------------------------------------------------------------------------
//
// SESSION_START payload:
//   UTF-8 JSON blob describing session metadata (recorder version, config snapshot).
//
// CHANNEL_DEF payload:
//   [channel_id     : uint16_t          ]
//   [layer          : uint8_t  (CaptureLayer)]
//   [codec          : uint8_t  (CompressionCodec)]
//   [chunk_bytes    : uint32_t ]
//   [flush_ms       : uint32_t ]
//   [name_len       : uint16_t ]
//   [name           : uint8_t[name_len] ] (UTF-8, not null-terminated)
//   [schema_len     : uint16_t ]
//   [schema         : uint8_t[schema_len] ] (UTF-8 schema name / MIME type, may be empty)
//   [metadata_len   : uint32_t ]
//   [metadata       : uint8_t[metadata_len] ] (arbitrary user blob, may be empty)
//
// DATA payload:
//   Raw message bytes — library is payload-agnostic.
//
// ANNOTATION payload:
//   [label_len      : uint16_t ]
//   [label          : uint8_t[label_len] ] (UTF-8)
//   [metadata_len   : uint32_t ]
//   [metadata       : uint8_t[metadata_len] ]
//
// SESSION_END payload:
//   [total_records  : uint64_t ] (informational — equals FileFooter::record_count)
//   [duration_ns    : uint64_t ] (session_end_timestamp - first data timestamp)

}  // namespace format
}  // namespace recplay
