#pragma once

/// @file detail/index_builder.hpp
/// @brief Builds the in-memory seek index during writing and serialises it as
///        an INDEX record at segment close — internal use only.

// local includes
#include <recplay/types.hpp>
#include <recplay/format.hpp>

// system includes
#include <cstdint>
#include <vector>

namespace recplay {
namespace detail {

/// A single in-memory index entry (mirrors format::IndexEntry but without packing).
struct SeekPoint {
    ChannelId Channel;     ///< Channel identifier
    Timestamp TimestampNs; ///< Nanosecond timestamp
    uint64_t  FileOffset;  ///< Byte offset of the RecordEnvelope in the current segment
};

/// Accumulates seek points during a recording session and serialises them when
/// the segment is closed.
///
/// A new SeekPoint is added for a channel when:
///   - The channel's first record is written, OR
///   - The elapsed time since the last SeekPoint for that channel >= indexIntervalNs.
///
/// Thread-safety: NOT thread-safe. Serialise access via the writer.
class IndexBuilder final {
public:
    /// @param indexIntervalNs  Minimum nanoseconds between index entries per channel.
    explicit IndexBuilder(uint64_t indexIntervalNs);

    /// Consider adding a seek point for the given channel at the current file offset.
    /// Does nothing if the interval since the last seek point for this channel has not
    /// elapsed.
    ///
    /// @param channel    Channel being written.
    /// @param timestamp  Timestamp of the record about to be written.
    /// @param offset     Byte offset of the RecordEnvelope in the segment file.
    void MaybeAdd(ChannelId channel, Timestamp timestamp, uint64_t offset);

    /// Force-add a seek point regardless of the interval.
    void Add(ChannelId channel, Timestamp timestamp, uint64_t offset);

    /// Serialise all accumulated seek points into the payload format of an INDEX record:
    ///   [IndexHeader][IndexEntry x N]  (packed, little-endian)
    ///
    /// Returns the serialised bytes ready to be written as a record payload.
    std::vector<uint8_t> Serialise() const;

    /// Total number of seek points.
    size_t Size() const noexcept;

    /// Reset — called after segment rotation to start fresh for the new segment.
    void Reset();

private:
    uint64_t               m_IndexIntervalNs;
    std::vector<SeekPoint> m_Points;
    /// Per-channel last timestamp at which a seek point was recorded.
    /// Flat array indexed by ChannelId for O(1) lookup; resizes as needed.
    std::vector<Timestamp> m_LastIndexed;
};

// ---------------------------------------------------------------------------
// Seek index loaded by the reader
// ---------------------------------------------------------------------------

/// Loaded in-memory by the reader for one segment.
/// Provides O(log n) binary-search lookup per channel.
class SeekIndex final {
public:
    /// Sentinel value returned by Find() when no entry exists.
    static constexpr uint64_t NO_OFFSET = UINT64_MAX;

    /// Deserialise from the payload of an INDEX record.
    /// @return true on success.
    bool Load(const void* payload, uint32_t payloadLength);

    /// Find the file offset of the last seek point with TimestampNs <= targetNs
    /// for the given channel. Returns NO_OFFSET if no entry exists.
    uint64_t Find(ChannelId channel, Timestamp targetNs) const noexcept;

    /// All seek points for a given channel, in ascending timestamp order.
    std::vector<SeekPoint> EntriesFor(ChannelId channel) const;

    bool   Empty() const noexcept { return m_Points.empty(); }
    size_t Size()  const noexcept { return m_Points.size();  }

private:
    // Sorted by (Channel, TimestampNs) for binary search
    std::vector<SeekPoint> m_Points;
};

}  // namespace detail
}  // namespace recplay
