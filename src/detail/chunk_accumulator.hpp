#pragma once

/// @file detail/chunk_accumulator.hpp
/// @brief Buffering and compression of DATA records into CHUNK records — internal use only.

// local includes
#include <recplay/types.hpp>
#include <recplay/channel.hpp>

// system includes
#include <cstdint>
#include <functional>
#include <vector>

namespace recplay {
namespace detail {

/// Callback invoked when a chunk is ready to be flushed to disk.
/// Parameters: compressed payload bytes, uncompressed byte count, record count.
using ChunkFlushCallback = std::function<void(const std::vector<uint8_t>& payload,
                                              uint32_t uncompressedBytes,
                                              uint32_t recordCount)>;

/// Accumulates raw DATA record payloads for one channel and compresses them
/// into a CHUNK record payload when either the size threshold or the flush
/// interval is reached.
///
/// Thread-safety: NOT thread-safe. Callers must serialise access.
class ChunkAccumulator final {
public:
    /// @param codec            Compression codec for this channel.
    /// @param maxUncompressed  Flush when accumulated bytes >= this value.
    /// @param flushIntervalMs  Flush when this many ms have elapsed since last flush.
    /// @param onFlush          Called with the encoded chunk when flushing.
    ChunkAccumulator(CompressionCodec   codec,
                     uint32_t           maxUncompressed,
                     uint32_t           flushIntervalMs,
                     ChunkFlushCallback onFlush);

    ~ChunkAccumulator();

    /// Append a message payload. Triggers a flush if thresholds are exceeded.
    /// @param timestampNs  Timestamp stored in the embedded DATA envelope.
    /// @param data         Raw payload bytes.
    /// @param length       Payload length.
    void Push(Timestamp timestampNs, const void* data, uint32_t length);

    /// Force a flush regardless of thresholds. No-op if the accumulator is empty.
    void Flush();

    /// Returns true if there are pending unflushed records.
    bool HasPending() const noexcept;

    /// Accumulated uncompressed bytes since last flush.
    uint32_t PendingBytes() const noexcept;

    /// Number of records accumulated since last flush.
    uint32_t PendingRecords() const noexcept;

    /// Update the flush interval (e.g. if session config changes).
    void SetFlushIntervalMs(uint32_t intervalMs) noexcept;

private:
    struct Impl;
    Impl* m_Impl; ///< PIMPL to keep the header clean of compression library includes
};

}  // namespace detail
}  // namespace recplay