/// @file detail/chunk_accumulator.cpp
/// @brief ChunkAccumulator implementation.
///
/// Inner-buffer wire format (uncompressed, per record):
///   [TimestampNs : uint64_t LE]
///   [Length      : uint32_t LE]
///   [payload     : uint8_t[Length]]
///
/// This is what gets compressed (or passed through) and handed to the
/// ChunkFlushCallback. The WriterImpl prepends a ChunkHeader and wraps
/// it in a CHUNK RecordEnvelope before writing to disk.

// local includes
#include "chunk_accumulator.hpp"
#include "platform.hpp"

// system includes
#include <chrono>
#include <cstring>

#if defined(RECPLAY_HAS_LZ4)
#   include <lz4.h>
#endif
#if defined(RECPLAY_HAS_ZSTD)
#   include <zstd.h>
#endif

namespace recplay {
namespace detail {

struct ChunkAccumulator::Impl {
    CompressionCodec   Codec;
    uint32_t           MaxUncompressed;
    uint32_t           FlushIntervalMs;
    ChunkFlushCallback OnFlush;

    std::vector<uint8_t> Buffer;       ///< Accumulated uncompressed inner records
    uint32_t             RecordCount = 0;
    std::chrono::steady_clock::time_point LastFlushTime = std::chrono::steady_clock::now();
};

ChunkAccumulator::ChunkAccumulator(CompressionCodec   codec,
                                   uint32_t           maxUncompressed,
                                   uint32_t           flushIntervalMs,
                                   ChunkFlushCallback onFlush)
    : m_Impl(new Impl{codec, maxUncompressed, flushIntervalMs, std::move(onFlush)})
{}

ChunkAccumulator::~ChunkAccumulator() { delete m_Impl; }

Status ChunkAccumulator::Push(Timestamp timestampNs,
                              const void* data,
                              uint32_t    length) {
    if (!m_Impl) return Status::ErrorInvalidArg;
    if (length > 0 && data == nullptr) return Status::ErrorInvalidArg;

    // Append inner record: [ts:8][len:4][payload]
    const size_t pos = m_Impl->Buffer.size();
    m_Impl->Buffer.resize(pos + sizeof(uint64_t) + sizeof(uint32_t) + length);
    uint8_t* p = m_Impl->Buffer.data() + pos;

    const uint64_t ts  = HostToLe64(timestampNs);
    const uint32_t len = HostToLe32(length);
    std::memcpy(p, &ts,  sizeof(ts));  p += sizeof(ts);
    std::memcpy(p, &len, sizeof(len)); p += sizeof(len);
    if (length > 0 && data)
        std::memcpy(p, data, length);

    ++m_Impl->RecordCount;

    // Check flush thresholds
    const bool sizeExceeded = m_Impl->Buffer.size() >= m_Impl->MaxUncompressed;

    bool timeExceeded = false;
    if (m_Impl->FlushIntervalMs > 0) {
        const auto now     = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 now - m_Impl->LastFlushTime).count();
        timeExceeded = static_cast<uint32_t>(elapsed) >= m_Impl->FlushIntervalMs;
    }

    if (sizeExceeded || timeExceeded)
        return Flush();

    return Status::Ok;
}

Status ChunkAccumulator::Flush() {
    if (!m_Impl) return Status::ErrorInvalidArg;
    if (m_Impl->Buffer.empty()) return Status::Ok;

    const uint32_t uncompressedBytes = static_cast<uint32_t>(m_Impl->Buffer.size());
    const uint32_t recordCount       = m_Impl->RecordCount;

    std::vector<uint8_t> payload;

    switch (m_Impl->Codec) {

#if defined(RECPLAY_HAS_LZ4)
        case CompressionCodec::LZ4: {
            const int maxDst = LZ4_compressBound(static_cast<int>(uncompressedBytes));
            if (maxDst > 0) {
                payload.resize(static_cast<size_t>(maxDst));
                const int compressed = LZ4_compress_default(
                    reinterpret_cast<const char*>(m_Impl->Buffer.data()),
                    reinterpret_cast<char*>(payload.data()),
                    static_cast<int>(uncompressedBytes),
                    maxDst);
                if (compressed > 0) {
                    payload.resize(static_cast<size_t>(compressed));
                    break;
                }
            }
            // Fall through to raw on error
            payload = m_Impl->Buffer;
            break;
        }
#endif

#if defined(RECPLAY_HAS_ZSTD)
        case CompressionCodec::Zstd: {
            const size_t maxDst    = ZSTD_compressBound(uncompressedBytes);
            const int    zstdLevel = 1;  // fastest level
            payload.resize(maxDst);
            const size_t compressed = ZSTD_compress(
                payload.data(), maxDst,
                m_Impl->Buffer.data(), uncompressedBytes,
                zstdLevel);
            if (!ZSTD_isError(compressed)) {
                payload.resize(compressed);
                break;
            }
            // Fall through to raw on error
            payload = m_Impl->Buffer;
            break;
        }
#endif

        case CompressionCodec::None:
        default:
            payload = m_Impl->Buffer;
            break;
    }

    if (m_Impl->OnFlush) {
        const Status st = m_Impl->OnFlush(payload, uncompressedBytes, recordCount);
        if (st != Status::Ok)
            return st;
    }

    m_Impl->Buffer.clear();
    m_Impl->RecordCount   = 0;
    m_Impl->LastFlushTime = std::chrono::steady_clock::now();
    return Status::Ok;
}

bool     ChunkAccumulator::HasPending()    const noexcept { return m_Impl && !m_Impl->Buffer.empty(); }
uint32_t ChunkAccumulator::PendingBytes()  const noexcept { return m_Impl ? static_cast<uint32_t>(m_Impl->Buffer.size()) : 0u; }
uint32_t ChunkAccumulator::PendingRecords()const noexcept { return m_Impl ? m_Impl->RecordCount : 0u; }

void ChunkAccumulator::SetFlushIntervalMs(uint32_t intervalMs) noexcept {
    if (m_Impl) m_Impl->FlushIntervalMs = intervalMs;
}

}  // namespace detail
}  // namespace recplay
