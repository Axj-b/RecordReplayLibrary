#pragma once

/// @file session.hpp
/// @brief Session manifest, segment information, and session-level configuration.

// local includes
#include "types.hpp"
#include "channel.hpp"

// system includes
#include <cstdint>
#include <string>
#include <vector>

namespace recplay {

// ---------------------------------------------------------------------------
// SegmentInfo — metadata for one .rec file in a multi-segment session
// ---------------------------------------------------------------------------

/// Describes a single segment file within a session.
/// Stored in SessionManifest and in session.manifest on disk.
struct SegmentInfo {
    /// Segment filename relative to the session directory (e.g. "my_session_001.rec").
    std::string Filename;

    /// 0-based index of this segment within the session.
    uint32_t SegmentIndex = 0;

    /// Timestamp of the first DATA record written to this segment.
    Timestamp StartNs = 0;

    /// Timestamp of the last DATA record written to this segment.
    Timestamp EndNs = 0;

    /// On-disk file size in bytes.
    uint64_t SizeBytes = 0;
};

// ---------------------------------------------------------------------------
// SessionManifest — describes an entire recording session
// ---------------------------------------------------------------------------

/// Runtime representation of the session.manifest JSON file.
/// Written/updated by the writer; read by the reader, splitter, and merger.
struct SessionManifest {
    /// UUID v4 that ties all segment files to this session.
    SessionId Id{};

    /// Wall-clock UTC time at session open, nanoseconds since Unix epoch.
    Timestamp CreatedAtNs = 0;

    /// Ordered list of segment files belonging to this session.
    std::vector<SegmentInfo> Segments;

    /// All channels defined in this session (across all segments).
    /// Channel IDs are stable across segments.
    std::vector<ChannelDef> Channels;

    /// Semantic version string of the recorder library that wrote this session.
    std::string RecorderVersion;

    // ------------------------------------------------------------------
    // Convenience accessors
    // ------------------------------------------------------------------

    /// Total on-disk size across all segments.
    uint64_t TotalSizeBytes() const noexcept;

    /// Earliest timestamp across all segments.
    Timestamp StartNs() const noexcept;

    /// Latest timestamp across all segments.
    Timestamp EndNs() const noexcept;

    /// Duration in nanoseconds (EndNs - StartNs), or 0 if no data.
    uint64_t DurationNs() const noexcept;

    /// Find a channel definition by name. Returns nullptr if not found.
    const ChannelDef* FindChannel(const std::string& name) const noexcept;

    /// Find a channel definition by ID. Returns nullptr if not found.
    const ChannelDef* FindChannel(ChannelId id) const noexcept;
};

// ---------------------------------------------------------------------------
// SessionConfig — provided by the user when opening a session for writing
// ---------------------------------------------------------------------------

/// Configuration passed to RecorderSession::Open().
struct SessionConfig {
    /// Root directory for this session. The library will create a sub-directory
    /// named after the session (derived from start timestamp + short ID).
    /// Example: "/recordings" → creates "/recordings/20260227T120000_a1b2c3d4/"
    std::string OutputDir;

    /// Optional override for the session sub-directory name. If empty the library
    /// generates a name automatically.
    std::string SessionName;

    /// Maximum size in bytes for a single .rec segment file.
    /// When the file reaches this size the library transparently rotates to a new segment.
    /// Default: DEFAULT_MAX_SEGMENT_BYTES (1 GiB).
    uint64_t MaxSegmentBytes = format::DEFAULT_MAX_SEGMENT_BYTES;

    /// Optional additional rotation trigger: rotate after this many nanoseconds
    /// per segment regardless of size. 0 = disabled.
    uint64_t MaxSegmentDurationNs = 0;

    /// Enable CRC32 checksums on every record envelope.
    /// Slightly reduces write throughput but enables corruption detection on read.
    bool CrcEnabled = true;

    /// Index granularity: write one IndexEntry per channel approx every N nanoseconds
    /// (used by the reader for binary-search seeking). Default: 1 second.
    uint64_t IndexIntervalNs = 1'000'000'000ull;
};

}  // namespace recplay
