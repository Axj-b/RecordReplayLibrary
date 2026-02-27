/// @file writer.cpp
/// @brief RecorderSession implementation.

// local includes
#include <recplay/recplay.hpp>
#include <recplay/writer.hpp>
#include "detail/crc32.hpp"
#include "detail/chunk_accumulator.hpp"
#include "detail/index_builder.hpp"
#include "detail/manifest_io.hpp"
#include "detail/file_map.hpp"
#include "detail/platform.hpp"

// system includes
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <random>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace recplay {
namespace detail {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint64_t NowNs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
}

static SessionId GenerateUuidV4() {
    std::random_device                       rd;
    std::mt19937_64                          gen(rd());
    std::uniform_int_distribution<uint32_t>  dist(0, 255);
    SessionId id{};
    for (auto& b : id)
        b = static_cast<uint8_t>(dist(gen));
    // RFC 4122 version 4 bits
    id[6] = static_cast<uint8_t>((id[6] & 0x0Fu) | 0x40u);
    id[8] = static_cast<uint8_t>((id[8] & 0x3Fu) | 0x80u);
    return id;
}

// ---------------------------------------------------------------------------
// WriterImpl
// ---------------------------------------------------------------------------

struct WriterImpl {
    SessionConfig   Cfg;
    SessionManifest Mfst;
    std::string     Dir;          ///< Full session directory path
    std::string     SegBaseName;  ///< Base name for segment files (= session dir leaf name)

    MappedFile      SegFile;
    uint32_t        SegIdx      = 0;

    IndexBuilder    Idx;

    /// One accumulator per channel slot (indexed by ChannelId).
    std::vector<std::unique_ptr<ChunkAccumulator>> Accums;

    /// Timestamp of the first record pushed to each accumulator since last flush.
    std::vector<Timestamp>  AccumFirstTs;
    std::vector<bool>       AccumHasData;

    uint64_t RecordCount = 0;  ///< Records written to the current segment
    uint64_t SegStartNs  = 0;  ///< Timestamp of the first DATA record in this segment
    uint64_t SegLastNs   = 0;  ///< Timestamp of the most recent DATA record

    std::vector<uint64_t> MsgWritten;  ///< Per-channel written count (across all segments)
    std::vector<uint64_t> MsgDropped;  ///< Per-channel drop count

    explicit WriterImpl(uint64_t indexIntervalNs)
        : Idx(indexIntervalNs)
    {}

    // ------------------------------------------------------------------
    // Low-level record writer
    // ------------------------------------------------------------------

    Status WriteRecord(RecordOp op, ChannelId ch, Timestamp ts,
                       const void* data, uint32_t len) {
        // Assemble envelope in LE byte order
        format::RecordEnvelope env{};
        env.Op            = static_cast<uint8_t>(op);
        env.Channel       = HostToLe16(ch);
        env.Flags         = 0;
        env.PayloadLength = HostToLe32(len);
        env.TimestampNs   = HostToLe64(ts);
        env.Crc32         = 0;

        if (Cfg.CrcEnabled) {
            static_assert(offsetof(format::RecordEnvelope, Crc32) == 16,
                          "RecordEnvelope CRC field must be at offset 16");
            env.Flags |= static_cast<uint8_t>(RECORD_FLAG_CRC); // set flag BEFORE computing CRC
            uint32_t crc = Crc32(reinterpret_cast<const uint8_t*>(&env), 16);
            if (data && len)
                crc = Crc32(reinterpret_cast<const uint8_t*>(data), len, crc);
            env.Crc32 = HostToLe32(crc);
        }

        const uint64_t needed = sizeof(format::RecordEnvelope) + len;
        if (!SegFile.EnsureAvailable(needed))
            return Status::ErrorIO;

        std::memcpy(SegFile.WritePtr(), &env, sizeof(env));
        SegFile.Advance(sizeof(env));

        if (data && len) {
            std::memcpy(SegFile.WritePtr(), data, len);
            SegFile.Advance(len);
        }

        ++RecordCount;
        return Status::Ok;
    }

    // Write a CHUNK record: combines a ChunkHeader with the compressed payload.
    Status WriteChunkRecord(ChannelId ch, Timestamp firstTs,
                            CompressionCodec codec, uint32_t uncompBytes, uint32_t recCount,
                            const void* compData, uint32_t compLen) {
        // Build ChunkHeader in LE
        format::ChunkHeader chdr{};
        chdr.Codec               = static_cast<uint8_t>(codec);
        chdr.UncompressedLength  = HostToLe32(uncompBytes);
        chdr.RecordCount         = HostToLe32(recCount);

        const uint32_t totalLen = static_cast<uint32_t>(sizeof(chdr)) + compLen;

        // Acquire a contiguous write window for the whole chunk
        const uint64_t needed = sizeof(format::RecordEnvelope) + totalLen;
        if (!SegFile.EnsureAvailable(needed))
            return Status::ErrorIO;

        // Offset of this chunk's RecordEnvelope (needed for index)
        const uint64_t offset = SegFile.BytesWritten();

        // Build + write the RecordEnvelope manually so the chunk flags are correct
        format::RecordEnvelope env{};
        env.Op            = static_cast<uint8_t>(RecordOp::Chunk);
        env.Channel       = HostToLe16(ch);
        env.Flags         = static_cast<uint8_t>(RECORD_FLAG_COMPRESSED);
        env.PayloadLength = HostToLe32(totalLen);
        env.TimestampNs   = HostToLe64(firstTs);
        env.Crc32         = 0;

        if (Cfg.CrcEnabled) {
            env.Flags |= static_cast<uint8_t>(RECORD_FLAG_CRC); // flag set before CRC computation
            uint32_t crc = Crc32(reinterpret_cast<const uint8_t*>(&env), 16);
            crc = Crc32(reinterpret_cast<const uint8_t*>(&chdr), sizeof(chdr), crc);
            if (compData && compLen)
                crc = Crc32(reinterpret_cast<const uint8_t*>(compData), compLen, crc);
            env.Crc32 = HostToLe32(crc);
        }

        std::memcpy(SegFile.WritePtr(), &env, sizeof(env));
        SegFile.Advance(sizeof(env));
        std::memcpy(SegFile.WritePtr(), &chdr, sizeof(chdr));
        SegFile.Advance(sizeof(chdr));
        if (compData && compLen) {
            std::memcpy(SegFile.WritePtr(), compData, compLen);
            SegFile.Advance(compLen);
        }
        ++RecordCount;

        // Index the chunk
        Idx.MaybeAdd(ch, firstTs, offset);
        return Status::Ok;
    }

    // ------------------------------------------------------------------
    // Segment lifecycle
    // ------------------------------------------------------------------

    Status WriteFileHeader() {
        // Check if any channel uses compression
        bool anyCompressed = false;
        for (const auto& ch : Mfst.Channels)
            if (ch.Compression != CompressionCodec::None) { anyCompressed = true; break; }

        uint16_t flags = 0u;
        if (anyCompressed)    flags |= static_cast<uint16_t>(FILE_FLAG_COMPRESSION_ENABLED);
        if (Cfg.CrcEnabled)   flags |= static_cast<uint16_t>(FILE_FLAG_CRC_ENABLED);

        format::FileHeader hdr{};
        std::memcpy(hdr.Magic, format::MAGIC, 8);
        hdr.VersionMajor = format::VERSION_MAJOR;
        hdr.VersionMinor = format::VERSION_MINOR;
        std::memcpy(hdr.SessionId, Mfst.Id.data(), 16);
        hdr.SegmentIndex = HostToLe32(SegIdx);
        hdr.CreatedAtNs  = HostToLe64(Mfst.CreatedAtNs);
        hdr.Flags        = HostToLe16(flags);
        std::memset(hdr.Reserved, 0, sizeof(hdr.Reserved));

        if (!SegFile.EnsureAvailable(sizeof(hdr)))
            return Status::ErrorIO;
        std::memcpy(SegFile.WritePtr(), &hdr, sizeof(hdr));
        SegFile.Advance(sizeof(hdr));
        return Status::Ok;
    }

    Status WriteSessionStartRecord() {
        // Simple JSON payload
        std::ostringstream oss;
        oss << "{\"recorder_version\":\"" << Mfst.RecorderVersion
            << "\",\"segment_index\":" << SegIdx << "}";
        const std::string payload = oss.str();
        return WriteRecord(RecordOp::SessionStart, INVALID_CHANNEL_ID,
                           Mfst.CreatedAtNs,
                           payload.data(), static_cast<uint32_t>(payload.size()));
    }

    Status WriteChannelDefRecord(const ChannelDef& def) {
        // Serialize binary channel def payload
        std::vector<uint8_t> buf;
        buf.reserve(16 + def.Name.size() + def.Schema.size() + def.UserMetadata.size());

        auto push16 = [&](uint16_t v) {
            uint16_t le = HostToLe16(v);
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&le);
            buf.push_back(p[0]); buf.push_back(p[1]);
        };
        auto push32 = [&](uint32_t v) {
            uint32_t le = HostToLe32(v);
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&le);
            buf.push_back(p[0]); buf.push_back(p[1]); buf.push_back(p[2]); buf.push_back(p[3]);
        };
        auto pushStr16 = [&](const std::string& s) {
            push16(static_cast<uint16_t>(s.size()));
            buf.insert(buf.end(), s.begin(), s.end());
        };

        push16(def.Id);
        buf.push_back(static_cast<uint8_t>(def.Layer));
        buf.push_back(static_cast<uint8_t>(def.Compression));
        push32(def.ChunkSizeBytes);
        push32(def.ChunkFlushIntervalMs);
        pushStr16(def.Name);
        pushStr16(def.Schema);
        push32(static_cast<uint32_t>(def.UserMetadata.size()));
        buf.insert(buf.end(), def.UserMetadata.begin(), def.UserMetadata.end());

        return WriteRecord(RecordOp::ChannelDef, def.Id, Mfst.CreatedAtNs,
                           buf.data(), static_cast<uint32_t>(buf.size()));
    }

    Status OpenSegment(uint32_t idx) {
        SegIdx      = idx;
        RecordCount = 0;
        SegStartNs  = 0;
        SegLastNs   = 0;
        Idx.Reset();

        const std::string filename = MakeSegmentFilename(SegBaseName, idx);
        const std::string filepath = Dir + static_cast<char>(PATH_SEP) + filename;

        SegFile = MappedFile::OpenWrite(filepath);
        if (!SegFile.IsOpen())
            return Status::ErrorIO;

        Status st = WriteFileHeader();
        if (st != Status::Ok) return st;
        st = WriteSessionStartRecord();
        if (st != Status::Ok) return st;

        // Re-emit channel defs for all already-defined channels
        for (const auto& ch : Mfst.Channels) {
            st = WriteChannelDefRecord(ch);
            if (st != Status::Ok) return st;
        }

        return Status::Ok;
    }

    Status CloseSegment() {
        if (!SegFile.IsOpen()) return Status::Ok;

        // Flush all accumulators
        for (auto& acc : Accums) {
            if (acc) acc->Flush();
        }

        // Write INDEX record
        std::vector<uint8_t> indexPayload = Idx.Serialise();
        const uint64_t indexOffset = (indexPayload.empty())
            ? format::NO_INDEX
            : SegFile.BytesWritten();

        if (!indexPayload.empty()) {
            Status st = WriteRecord(RecordOp::Index, INVALID_CHANNEL_ID, SegLastNs,
                                    indexPayload.data(),
                                    static_cast<uint32_t>(indexPayload.size()));
            if (st != Status::Ok) return st;
        }

        // SESSION_END payload: [total_records:uint64_t][duration_ns:uint64_t]
        {
            const uint64_t durationNs = (SegStartNs > 0 && SegLastNs >= SegStartNs)
                ? (SegLastNs - SegStartNs) : 0;
            uint8_t endPayload[16]{};
            uint64_t le_rec = HostToLe64(RecordCount);
            uint64_t le_dur = HostToLe64(durationNs);
            std::memcpy(endPayload,     &le_rec, 8);
            std::memcpy(endPayload + 8, &le_dur, 8);
            WriteRecord(RecordOp::SessionEnd, INVALID_CHANNEL_ID, SegLastNs,
                        endPayload, sizeof(endPayload));
        }

        // File footer
        {
            format::FileFooter footer{};
            std::memcpy(footer.Magic, format::MAGIC, 8);
            footer.IndexOffset = HostToLe64(indexOffset);
            footer.RecordCount = HostToLe64(RecordCount);
            footer.Crc32       = 0;
                // CRC covers bytes 0-23 (Magic + IndexOffset + RecordCount);
                // Crc32 field is at offset 24 and is NOT included in the computation.
                footer.Crc32       = HostToLe32(Crc32(
                    reinterpret_cast<const uint8_t*>(&footer), 24));
            if (!SegFile.EnsureAvailable(sizeof(footer)))
                return Status::ErrorIO;
            std::memcpy(SegFile.WritePtr(), &footer, sizeof(footer));
            SegFile.Advance(sizeof(footer));
        }

        const uint64_t fileSize = SegFile.BytesWritten();
        SegFile.Close();

        // Update manifest
        SegmentInfo info{};
        info.Filename     = MakeSegmentFilename(SegBaseName, SegIdx);
        info.SegmentIndex = SegIdx;
        info.StartNs      = SegStartNs;
        info.EndNs        = SegLastNs;
        info.SizeBytes    = fileSize;
        Mfst.Segments.push_back(std::move(info));

        return Status::Ok;
    }

    // ------------------------------------------------------------------
    // Rotation check
    // ------------------------------------------------------------------

    bool NeedsRotation(uint64_t ts, uint64_t neededBytes) const {
        if (Cfg.MaxSegmentBytes > 0 &&
            SegFile.BytesWritten() + neededBytes > Cfg.MaxSegmentBytes)
            return true;
        if (Cfg.MaxSegmentDurationNs > 0 && SegStartNs > 0 &&
            ts >= SegStartNs && (ts - SegStartNs) >= Cfg.MaxSegmentDurationNs)
            return true;
        return false;
    }

    // ------------------------------------------------------------------
    // Channel management helpers
    // ------------------------------------------------------------------

    void EnsureVectors(ChannelId ch) {
        const size_t need = static_cast<size_t>(ch) + 1;
        if (MsgWritten.size()  < need) MsgWritten.resize(need, 0);
        if (MsgDropped.size()  < need) MsgDropped.resize(need, 0);
        if (Accums.size()      < need) Accums.resize(need);
        if (AccumFirstTs.size()< need) AccumFirstTs.resize(need, 0);
        if (AccumHasData.size()< need) AccumHasData.resize(need, false);
    }
};

} // namespace detail

// ---------------------------------------------------------------------------
// RecorderSession
// ---------------------------------------------------------------------------

RecorderSession::RecorderSession() noexcept  = default;
RecorderSession::~RecorderSession() noexcept = default;

RecorderSession::RecorderSession(RecorderSession&&) noexcept            = default;
RecorderSession& RecorderSession::operator=(RecorderSession&&) noexcept = default;

Status RecorderSession::Open(const SessionConfig& config) {
    if (m_Impl) return Status::ErrorInvalidArg;

    auto impl = std::make_unique<detail::WriterImpl>(config.IndexIntervalNs);
    impl->Cfg  = config;

    // --- Session identity ---
    impl->Mfst.Id              = detail::GenerateUuidV4();
    impl->Mfst.CreatedAtNs     = detail::NowNs();
    impl->Mfst.RecorderVersion = VersionString();

    // --- Session directory ---
    const std::string subDir = config.SessionName.empty()
        ? detail::MakeSessionDirName(impl->Mfst)
        : config.SessionName;
    impl->Dir         = config.OutputDir + static_cast<char>(detail::PATH_SEP) + subDir;
    impl->SegBaseName = subDir;

    try {
        fs::create_directories(impl->Dir);
    } catch (...) {
        return Status::ErrorIO;
    }

    // --- Open first segment ---
    Status st = impl->OpenSegment(0);
    if (st != Status::Ok) return st;

    // --- Write initial manifest ---
    st = detail::WriteManifest(impl->Dir, impl->Mfst);
    if (st != Status::Ok) return st;

    m_Impl = std::move(impl);
    return Status::Ok;
}

Status RecorderSession::Close() {
    if (!m_Impl) return Status::Ok;

    Status st = m_Impl->CloseSegment();
    detail::WriteManifest(m_Impl->Dir, m_Impl->Mfst); // best-effort
    m_Impl.reset();
    return st;
}

bool RecorderSession::IsOpen() const noexcept { return m_Impl != nullptr; }

Status RecorderSession::DefineChannel(const ChannelConfig& config, ChannelId& outId) {
    if (!m_Impl) return Status::ErrorNotOpen;
    if (config.Name.empty()) return Status::ErrorInvalidArg;

    // Check for duplicate name
    for (const auto& ch : m_Impl->Mfst.Channels) {
        if (ch.Name == config.Name) {
            outId = ch.Id;
            return Status::Ok;
        }
    }

    const auto newId = static_cast<ChannelId>(m_Impl->Mfst.Channels.size());
    if (newId >= format::MAX_CHANNELS) return Status::ErrorInvalidArg;

    ChannelDef def{};
    def.Id                  = newId;
    def.Name                = config.Name;
    def.Layer               = config.Layer;
    def.Compression         = config.Compression;
    def.ChunkSizeBytes      = config.ChunkSizeBytes;
    def.ChunkFlushIntervalMs = config.ChunkFlushIntervalMs;
    def.Schema              = config.Schema;
    def.UserMetadata        = config.UserMetadata;

    m_Impl->Mfst.Channels.push_back(def);
    m_Impl->EnsureVectors(newId);

    // Write CHANNEL_DEF record to current segment
    Status st = m_Impl->WriteChannelDefRecord(def);
    if (st != Status::Ok) {
        m_Impl->Mfst.Channels.pop_back();
        return st;
    }

    // Create ChunkAccumulator for compressed channels
    if (def.Compression != CompressionCodec::None) {
        detail::WriterImpl* pImpl = m_Impl.get();
        const ChannelId chId   = newId;
        const auto       codec  = def.Compression;

        auto cb = [pImpl, chId, codec](const std::vector<uint8_t>& payload,
                                        uint32_t uncompBytes, uint32_t recCnt) {
            const Timestamp firstTs = pImpl->AccumFirstTs[chId];
            pImpl->AccumHasData[chId] = false;
            pImpl->WriteChunkRecord(chId, firstTs, codec, uncompBytes, recCnt,
                                    payload.data(), static_cast<uint32_t>(payload.size()));
        };

        m_Impl->Accums[newId] = std::make_unique<detail::ChunkAccumulator>(
            def.Compression,
            def.ChunkSizeBytes,
            def.ChunkFlushIntervalMs,
            std::move(cb));
    }

    outId = newId;
    return Status::Ok;
}

ChannelId RecorderSession::DefineChannel(const ChannelConfig& config) {
    ChannelId id = INVALID_CHANNEL_ID;
    DefineChannel(config, id);
    return id;
}

const ChannelDef* RecorderSession::GetChannelDef(ChannelId channelId) const noexcept {
    if (!m_Impl) return nullptr;
    for (const auto& ch : m_Impl->Mfst.Channels)
        if (ch.Id == channelId) return &ch;
    return nullptr;
}

const std::vector<ChannelDef>& RecorderSession::Channels() const noexcept {
    if (!m_Impl) {
        static const std::vector<ChannelDef> empty;
        return empty;
    }
    return m_Impl->Mfst.Channels;
}

Status RecorderSession::Write(ChannelId   channelId,
                              Timestamp   timestampNs,
                              const void* data,
                              uint32_t    length) {
    if (!m_Impl) return Status::ErrorNotOpen;

    const ChannelDef* def = GetChannelDef(channelId);
    if (!def) return Status::ErrorInvalidArg;

    // Update segment time window
    if (m_Impl->SegStartNs == 0) m_Impl->SegStartNs = timestampNs;
    if (timestampNs > m_Impl->SegLastNs) m_Impl->SegLastNs = timestampNs;

    if (def->Compression == CompressionCodec::None) {
        // ---- Uncompressed: write DATA record directly ----
        const uint64_t estimatedSize =
            sizeof(format::RecordEnvelope) + length
            + sizeof(format::FileFooter)
            + sizeof(format::RecordEnvelope) + 16   // SESSION_END
            + sizeof(format::RecordEnvelope);         // INDEX (rough)

        if (m_Impl->NeedsRotation(timestampNs, estimatedSize)) {
            Status st = RotateSegment();
            if (st != Status::Ok) return st;
            // Update timestamps in new segment
            m_Impl->SegStartNs = timestampNs;
            m_Impl->SegLastNs  = timestampNs;
        }

        const uint64_t offset = m_Impl->SegFile.BytesWritten();
        Status st = m_Impl->WriteRecord(RecordOp::Data, channelId, timestampNs, data, length);
        if (st != Status::Ok) { ++m_Impl->MsgDropped[channelId]; return st; }

        m_Impl->Idx.MaybeAdd(channelId, timestampNs, offset);
        ++m_Impl->MsgWritten[channelId];
    } else {
        // ---- Compressed: push to accumulator ----
        auto& acc = m_Impl->Accums[channelId];
        if (!acc) return Status::ErrorInvalidArg;

        // Track first timestamp this flush window
        if (!m_Impl->AccumHasData[channelId]) {
            m_Impl->AccumFirstTs[channelId]  = timestampNs;
            m_Impl->AccumHasData[channelId]  = true;
        }

        acc->Push(timestampNs, data, length);
        ++m_Impl->MsgWritten[channelId];
    }

    return Status::Ok;
}

Status RecorderSession::Annotate(Timestamp          timestampNs,
                                 const std::string& label,
                                 const void*        metadata,
                                 uint32_t           metadataLength) {
    if (!m_Impl) return Status::ErrorNotOpen;

    // ANNOTATION payload: [label_len:uint16_t][label][metadata_len:uint32_t][metadata]
    const auto labelLen = static_cast<uint16_t>(label.size());
    std::vector<uint8_t> buf;
    buf.reserve(2 + label.size() + 4 + metadataLength);

    auto push16 = [&](uint16_t v) {
        uint16_t le = detail::HostToLe16(v);
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&le);
        buf.push_back(p[0]); buf.push_back(p[1]);
    };
    auto push32 = [&](uint32_t v) {
        uint32_t le = detail::HostToLe32(v);
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&le);
        buf.push_back(p[0]); buf.push_back(p[1]); buf.push_back(p[2]); buf.push_back(p[3]);
    };

    push16(labelLen);
    buf.insert(buf.end(), label.begin(), label.end());
    push32(metadataLength);
    if (metadata && metadataLength)
        buf.insert(buf.end(),
                   reinterpret_cast<const uint8_t*>(metadata),
                   reinterpret_cast<const uint8_t*>(metadata) + metadataLength);

    return m_Impl->WriteRecord(RecordOp::Annotation, INVALID_CHANNEL_ID, timestampNs,
                               buf.data(), static_cast<uint32_t>(buf.size()));
}

Status RecorderSession::Flush() {
    if (!m_Impl) return Status::ErrorNotOpen;
    for (auto& acc : m_Impl->Accums)
        if (acc) acc->Flush();
    return Status::Ok;
}

Status RecorderSession::RotateSegment() {
    if (!m_Impl) return Status::ErrorNotOpen;
    Status st = m_Impl->CloseSegment();
    if (st != Status::Ok) return st;

    detail::WriteManifest(m_Impl->Dir, m_Impl->Mfst); // best-effort mid-session update

    // Reset accumulators' tracking state
    std::fill(m_Impl->AccumHasData.begin(), m_Impl->AccumHasData.end(), false);

    return m_Impl->OpenSegment(m_Impl->SegIdx + 1);
}

uint64_t RecorderSession::CurrentSegmentBytes() const noexcept {
    return m_Impl ? m_Impl->SegFile.BytesWritten() : 0;
}

uint64_t RecorderSession::MessagesWritten(ChannelId ch) const noexcept {
    if (!m_Impl || ch >= m_Impl->MsgWritten.size()) return 0;
    return m_Impl->MsgWritten[ch];
}

uint64_t RecorderSession::MessagesDropped(ChannelId ch) const noexcept {
    if (!m_Impl || ch >= m_Impl->MsgDropped.size()) return 0;
    return m_Impl->MsgDropped[ch];
}

const SessionManifest& RecorderSession::Manifest() const noexcept {
    if (!m_Impl) {
        static const SessionManifest empty;
        return empty;
    }
    return m_Impl->Mfst;
}

const std::string& RecorderSession::SessionPath() const noexcept {
    if (!m_Impl) {
        static const std::string empty;
        return empty;
    }
    return m_Impl->Dir;
}

} // namespace recplay

