/// @file reader.cpp
/// @brief ReaderSession implementation.

// local includes
#include <recplay/reader.hpp>
#include "detail/crc32.hpp"
#include "detail/index_builder.hpp"
#include "detail/manifest_io.hpp"
#include "detail/file_map.hpp"
#include "detail/platform.hpp"

// optional compression headers
#if RECPLAY_HAS_LZ4
#  include <lz4.h>
#endif
#if RECPLAY_HAS_ZSTD
#  include <zstd.h>
#endif

// system includes
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

namespace recplay {
namespace detail {

// ---------------------------------------------------------------------------
// SegmentData
// ---------------------------------------------------------------------------

struct SegmentData {
    MappedFile  File;
    SeekIndex   Idx;
    uint64_t    StartNs = 0;
    uint64_t    EndNs   = 0;
};

// ---------------------------------------------------------------------------
// ReaderImpl
// ---------------------------------------------------------------------------

struct ReaderImpl {
    SessionManifest          Mfst;
    std::string              Path;
    std::vector<SegmentData> Segs;

    // Read cursor
    size_t   CurSeg    = 0;
    uint64_t CurOffset = sizeof(format::FileHeader);

    // Chunk decode state
    bool               InChunk   = false;
    uint32_t           ChunkRecIdx = 0;
    uint32_t           ChunkRecCnt = 0;
    uint64_t           ChunkBufPos = 0;
    std::vector<uint8_t> DecBuf;
    ChannelId          ChunkChannel = INVALID_CHANNEL_ID;

    // Statistics
    uint64_t                CorruptCount = 0;
    std::vector<uint64_t>   ChTotalCount; ///< indexed by ChannelId

    // Annotations collected at open
    std::vector<Annotation> Annots;

    // ------------------------------------------------------------------
    // Decompression
    // ------------------------------------------------------------------

    bool DecompressChunk(uint8_t codec, uint32_t uncompLen,
                         const uint8_t* compData, uint32_t compLen) {
        DecBuf.resize(uncompLen);
        switch (static_cast<CompressionCodec>(codec)) {
            case CompressionCodec::None:
                if (compLen != uncompLen) return false;
                std::memcpy(DecBuf.data(), compData, compLen);
                return true;

#if RECPLAY_HAS_LZ4
            case CompressionCodec::LZ4: {
                int result = LZ4_decompress_safe(
                    reinterpret_cast<const char*>(compData),
                    reinterpret_cast<char*>(DecBuf.data()),
                    static_cast<int>(compLen),
                    static_cast<int>(uncompLen));
                return result == static_cast<int>(uncompLen);
            }
#endif
#if RECPLAY_HAS_ZSTD
            case CompressionCodec::Zstd: {
                size_t result = ZSTD_decompress(
                    DecBuf.data(), uncompLen,
                    compData, compLen);
                return !ZSTD_isError(result) && result == uncompLen;
            }
#endif
            default:
                return false; // unknown or uncompiled codec
        }
    }

    // ------------------------------------------------------------------
    // Helper: parse a CHANNEL_DEF record payload into a ChannelDef.
    // Mirror of WriterImpl::WriteChannelDefRecord's binary layout.
    // ------------------------------------------------------------------

    static ChannelDef ParseChannelDefPayload(const uint8_t* p, uint32_t len)
    {
        ChannelDef ch{};
        uint32_t pos = 0;

        auto rd16 = [&]() -> uint16_t {
            if (pos + 2 > len) return 0;
            uint16_t v{}; std::memcpy(&v, p + pos, 2); pos += 2;
            return Le16ToHost(v);
        };
        auto rd32 = [&]() -> uint32_t {
            if (pos + 4 > len) return 0;
            uint32_t v{}; std::memcpy(&v, p + pos, 4); pos += 4;
            return Le32ToHost(v);
        };
        auto rdStr16 = [&]() -> std::string {
            const uint16_t l = rd16();
            if (pos + l > len) return {};
            std::string s(reinterpret_cast<const char*>(p + pos), l);
            pos += l;
            return s;
        };

        ch.Id                   = rd16();
        if (pos >= len) return ch;
        ch.Layer                = static_cast<CaptureLayer>(p[pos++]);
        if (pos >= len) return ch;
        ch.Compression          = static_cast<CompressionCodec>(p[pos++]);
        ch.ChunkSizeBytes       = rd32();
        ch.ChunkFlushIntervalMs = rd32();
        ch.Name                 = rdStr16();
        ch.Schema               = rdStr16();
        const uint32_t metaLen  = rd32();
        if (pos + metaLen <= len)
            ch.UserMetadata.assign(p + pos, p + pos + metaLen);
        return ch;
    }

    // ------------------------------------------------------------------
    // At-open scan: load index, collect annotations, and (when Mfst has
    // no channels yet) parse SessionStart + ChannelDef records so that
    // a standalone .rec file can be opened without a session.manifest.
    // ------------------------------------------------------------------

    void ScanSegmentAtOpen(SegmentData& seg) {
        if (!seg.File.IsOpen()) return;
        const uint8_t* base = seg.File.Data();
        const uint64_t size = seg.File.FileSize();

        if (size < sizeof(format::FileHeader)) return;

        // Verify magic
        if (std::memcmp(base, format::MAGIC, 8) != 0) return;

        // Walk records
        uint64_t offset = sizeof(format::FileHeader);
        seg.StartNs = std::numeric_limits<uint64_t>::max();
        seg.EndNs   = 0;

        while (offset + sizeof(format::RecordEnvelope) <= size) {
            const auto* env = reinterpret_cast<const format::RecordEnvelope*>(base + offset);
            const uint32_t payloadLen = Le32ToHost(env->PayloadLength);
            const uint64_t nextOffset = offset + sizeof(format::RecordEnvelope) + payloadLen;

            if (nextOffset > size) break; // truncated

            const uint8_t* payload = base + offset + sizeof(format::RecordEnvelope);
            const uint64_t ts      = Le64ToHost(env->TimestampNs);
            const auto     op      = static_cast<RecordOp>(env->Op);

            switch (op) {
                case RecordOp::SessionStart: {
                    // Extract recorder_version from the small JSON blob when
                    // we are reconstructing the manifest from the file itself.
                    if (Mfst.RecorderVersion.empty() && payloadLen > 0) {
                        const std::string json(
                            reinterpret_cast<const char*>(payload), payloadLen);
                        const std::string key = "\"recorder_version\":\"";
                        const size_t      kp  = json.find(key);
                        if (kp != std::string::npos) {
                            const size_t vs = kp + key.size();
                            const size_t ve = json.find('"', vs);
                            if (ve != std::string::npos)
                                Mfst.RecorderVersion = json.substr(vs, ve - vs);
                        }
                    }
                    break;
                }
                case RecordOp::ChannelDef: {
                    // Parse and register the channel if not yet known.
                    // This makes the reader self-sufficient for single .rec files
                    // that carry no external session.manifest.
                    ChannelDef ch = ParseChannelDefPayload(payload, payloadLen);
                    if (!ch.Name.empty() && Mfst.FindChannel(ch.Id) == nullptr)
                        Mfst.Channels.push_back(std::move(ch));
                    break;
                }
                case RecordOp::Data: {
                    const ChannelId ch = Le16ToHost(env->Channel);
                    if (ts < seg.StartNs) seg.StartNs = ts;
                    if (ts > seg.EndNs)   seg.EndNs   = ts;
                    const size_t need = static_cast<size_t>(ch) + 1;
                    if (ChTotalCount.size() < need)
                        ChTotalCount.resize(need, 0);
                    ++ChTotalCount[ch];
                    break;
                }
                case RecordOp::Chunk: {
                    if (payloadLen >= sizeof(format::ChunkHeader)) {
                        const auto* chdr = reinterpret_cast<const format::ChunkHeader*>(payload);
                        const ChannelId ch = Le16ToHost(env->Channel);
                        const uint32_t recCnt = Le32ToHost(chdr->RecordCount);
                        const size_t need = static_cast<size_t>(ch) + 1;
                        if (ChTotalCount.size() < need)
                            ChTotalCount.resize(need, 0);
                        ChTotalCount[ch] += recCnt;
                    }
                    if (ts < seg.StartNs) seg.StartNs = ts;
                    if (ts > seg.EndNs)   seg.EndNs   = ts;
                    break;
                }
                case RecordOp::Index:
                    seg.Idx.Load(payload, payloadLen);
                    break;
                case RecordOp::Annotation: {
                    // [label_len:uint16_t][label][metadata_len:uint32_t][metadata]
                    if (payloadLen >= 2) {
                        uint16_t labelLen = 0;
                        std::memcpy(&labelLen, payload, 2);
                        labelLen = Le16ToHost(labelLen);
                        if (static_cast<uint32_t>(2 + labelLen + 4) <= payloadLen) {
                            Annotation ann;
                            ann.TimestampNs = ts;
                            ann.Label.assign(
                                reinterpret_cast<const char*>(payload + 2), labelLen);
                            uint32_t metaLen = 0;
                            std::memcpy(&metaLen, payload + 2 + labelLen, 4);
                            metaLen = Le32ToHost(metaLen);
                            const uint32_t metaOffset = 2 + labelLen + 4;
                            if (metaOffset + metaLen <= payloadLen) {
                                ann.Metadata.assign(payload + metaOffset,
                                                    payload + metaOffset + metaLen);
                            }
                            Annots.push_back(std::move(ann));
                        }
                    }
                    break;
                }
                default:
                    break;
            }

            offset = nextOffset;
        }

        if (seg.StartNs == std::numeric_limits<uint64_t>::max())
            seg.StartNs = 0;
    }

    // ------------------------------------------------------------------
    // Read one MessageView from the current position
    // ------------------------------------------------------------------

    bool NextMessage(MessageView& out) {
        // First drain any pending chunk inner records
        if (InChunk) {
            if (ChunkRecIdx < ChunkRecCnt) {
                if (ChunkBufPos + 12 <= DecBuf.size()) {
                    uint64_t tsLE = 0;
                    uint32_t lenLE = 0;
                    std::memcpy(&tsLE,  DecBuf.data() + ChunkBufPos,     8);
                    std::memcpy(&lenLE, DecBuf.data() + ChunkBufPos + 8, 4);
                    const uint64_t ts  = Le64ToHost(tsLE);
                    const uint32_t len = Le32ToHost(lenLE);
                    if (ChunkBufPos + 12 + len <= DecBuf.size()) {
                        out.Channel     = ChunkChannel;
                        out.TimestampNs = ts;
                        out.Data        = DecBuf.data() + ChunkBufPos + 12;
                        out.Length      = len;

                        ChunkBufPos += 12 + len;
                        ++ChunkRecIdx;

                        if (ChunkRecIdx >= ChunkRecCnt) InChunk = false;
                        return true;
                    }
                }
                InChunk = false; // malformed — stop chunk
            } else {
                InChunk = false;
            }
        }

        // Walk the segment stream
        while (CurSeg < Segs.size()) {
            const SegmentData& seg = Segs[CurSeg];
            if (!seg.File.IsOpen()) {
                ++CurSeg;
                CurOffset = sizeof(format::FileHeader);
                continue;
            }

            const uint8_t* base = seg.File.Data();
            const uint64_t size = seg.File.FileSize();

            // End of segment data
            if (CurOffset + sizeof(format::RecordEnvelope) > size) {
                ++CurSeg;
                CurOffset = sizeof(format::FileHeader);
                continue;
            }

            const auto* env = reinterpret_cast<const format::RecordEnvelope*>(base + CurOffset);
            const uint32_t payloadLen = Le32ToHost(env->PayloadLength);
            const uint64_t nextOffset = CurOffset
                                        + sizeof(format::RecordEnvelope)
                                        + payloadLen;

            if (nextOffset > size) {
                // Truncated record — advance segment
                ++CurSeg;
                CurOffset = sizeof(format::FileHeader);
                continue;
            }

            const uint8_t* payload = base + CurOffset + sizeof(format::RecordEnvelope);
            const uint64_t ts      = Le64ToHost(env->TimestampNs);
            const auto     op      = static_cast<RecordOp>(env->Op);
            const ChannelId ch     = Le16ToHost(env->Channel);

            // CRC check
            if (env->Flags & static_cast<uint8_t>(RECORD_FLAG_CRC)) {
                uint32_t crc = Crc32(reinterpret_cast<const uint8_t*>(env), 16);
                crc = Crc32(payload, payloadLen, crc);
                if (HostToLe32(crc) != env->Crc32) {
                    ++CorruptCount;
                    CurOffset = nextOffset;
                    continue;
                }
            }

            CurOffset = nextOffset;

            switch (op) {
                case RecordOp::Data: {
                    out.Channel     = ch;
                    out.TimestampNs = ts;
                    out.Data        = payload;
                    out.Length      = payloadLen;
                    return true;
                }
                case RecordOp::Chunk: {
                    if (payloadLen < sizeof(format::ChunkHeader)) continue;
                    const auto* chdr = reinterpret_cast<const format::ChunkHeader*>(payload);
                    const uint32_t uncLen   = Le32ToHost(chdr->UncompressedLength);
                    const uint32_t recCnt   = Le32ToHost(chdr->RecordCount);
                    const uint8_t  codec    = chdr->Codec;
                    const uint32_t compLen  = payloadLen - static_cast<uint32_t>(sizeof(format::ChunkHeader));
                    const uint8_t* compData = payload + sizeof(format::ChunkHeader);

                    if (!DecompressChunk(codec, uncLen, compData, compLen)) {
                        ++CorruptCount;
                        continue;
                    }

                    if (recCnt == 0) continue;

                    // Start chunk iteration
                    ChunkChannel = ch;
                    ChunkRecIdx  = 0;
                    ChunkRecCnt  = recCnt;
                    ChunkBufPos  = 0;
                    InChunk      = true;

                    // Return the first inner record immediately
                    if (ChunkBufPos + 12 <= DecBuf.size()) {
                        uint64_t tsLE = 0;
                        uint32_t lenLE = 0;
                        std::memcpy(&tsLE,  DecBuf.data() + ChunkBufPos,     8);
                        std::memcpy(&lenLE, DecBuf.data() + ChunkBufPos + 8, 4);
                        const uint64_t innerTs  = Le64ToHost(tsLE);
                        const uint32_t innerLen = Le32ToHost(lenLE);
                        if (ChunkBufPos + 12 + innerLen <= DecBuf.size()) {
                            out.Channel     = ChunkChannel;
                            out.TimestampNs = innerTs;
                            out.Data        = DecBuf.data() + ChunkBufPos + 12;
                            out.Length      = innerLen;
                            ChunkBufPos += 12 + innerLen;
                            ++ChunkRecIdx;

                            if (ChunkRecIdx >= ChunkRecCnt) InChunk = false;
                            return true;
                        }
                    }
                    InChunk = false;
                    continue;
                }
                default:
                    // Skip SESSION_START, CHANNEL_DEF, INDEX, ANNOTATION, SESSION_END
                    continue;
            }
        }

        return false; // end of all segments
    }
};

} // namespace detail

// ---------------------------------------------------------------------------
// ReaderSession
// ---------------------------------------------------------------------------

ReaderSession::ReaderSession()  noexcept = default;
ReaderSession::~ReaderSession() noexcept = default;

ReaderSession::ReaderSession(ReaderSession&&) noexcept            = default;
ReaderSession& ReaderSession::operator=(ReaderSession&&) noexcept = default;

Status ReaderSession::Open(const std::string& sessionPath) {
    if (m_Impl) return Status::ErrorInvalidArg;

    auto impl = std::make_unique<detail::ReaderImpl>();
    impl->Path = sessionPath;

    // ----------------------------------------------------------------
    // Auto-detect: if sessionPath points directly to a .rec file
    // (identified by the 8-byte magic), open it in single-file mode
    // and reconstruct the manifest from the embedded records.
    // Otherwise fall back to the original directory + session.manifest
    // approach for multi-segment sessions.
    // ----------------------------------------------------------------
    bool isSingleFile = false;
    {
        uint8_t magic[8]{};
        FILE* f = nullptr;
#if RECPLAY_PLATFORM_WINDOWS
        fopen_s(&f, sessionPath.c_str(), "rb");
#else
        f = std::fopen(sessionPath.c_str(), "rb");
#endif
        if (f) {
            isSingleFile = (std::fread(magic, 1, 8, f) == 8 &&
                            std::memcmp(magic, format::MAGIC, 8) == 0);
            std::fclose(f);
        }
    }

    if (isSingleFile) {
        // ---- Single .rec file: reconstruct manifest by scanning the file ----
        detail::SegmentData sd;
        sd.File = detail::MappedFile::OpenRead(sessionPath);
        if (!sd.File.IsOpen()) return Status::ErrorNotFound;

        // Extract session identity from the FileHeader.
        if (sd.File.FileSize() >= sizeof(format::FileHeader)) {
            const auto* hdr =
                reinterpret_cast<const format::FileHeader*>(sd.File.Data());
            std::memcpy(impl->Mfst.Id.data(), hdr->SessionId, 16);
            impl->Mfst.CreatedAtNs = detail::Le64ToHost(hdr->CreatedAtNs);
        }

        // ScanSegmentAtOpen parses ChannelDef + SessionStart into impl->Mfst
        // and fills sd.StartNs / sd.EndNs from the data records.
        impl->ScanSegmentAtOpen(sd);

        SegmentInfo si{};
        si.Filename     = sessionPath;  // full path for display
        si.SegmentIndex = 0;
        si.StartNs      = sd.StartNs;
        si.EndNs        = sd.EndNs;
        si.SizeBytes    = sd.File.FileSize();
        impl->Mfst.Segments.push_back(std::move(si));
        impl->Segs.push_back(std::move(sd));

    } else {
        // ---- Directory mode: read session.manifest then open each segment ----
        Status st = detail::ReadManifest(sessionPath, impl->Mfst);
        if (st != Status::Ok) return st;

        for (const auto& segInfo : impl->Mfst.Segments) {
            const std::string filepath =
                sessionPath + static_cast<char>(detail::PATH_SEP) + segInfo.Filename;

            detail::SegmentData sd;
            sd.File = detail::MappedFile::OpenRead(filepath);
            if (!sd.File.IsOpen()) continue; // non-fatal: keep going

            impl->ScanSegmentAtOpen(sd);
            // Fall back to manifest info if scan found nothing
            if (sd.StartNs == 0) sd.StartNs = segInfo.StartNs;
            if (sd.EndNs   == 0) sd.EndNs   = segInfo.EndNs;

            impl->Segs.push_back(std::move(sd));
        }

        if (impl->Segs.empty())
            return Status::ErrorNotFound;
    }

    impl->CurSeg    = 0;
    impl->CurOffset = sizeof(format::FileHeader);
    impl->InChunk   = false;

    m_Impl = std::move(impl);
    return Status::Ok;
}

Status ReaderSession::Close() {
    m_Impl.reset();
    return Status::Ok;
}

bool ReaderSession::IsOpen() const noexcept { return m_Impl != nullptr; }

const SessionManifest& ReaderSession::Manifest() const noexcept {
    if (!m_Impl) { static const SessionManifest e; return e; }
    return m_Impl->Mfst;
}

const std::vector<ChannelDef>& ReaderSession::Channels() const noexcept {
    if (!m_Impl) { static const std::vector<ChannelDef> e; return e; }
    return m_Impl->Mfst.Channels;
}

const ChannelDef* ReaderSession::FindChannel(const std::string& name) const noexcept {
    return m_Impl ? m_Impl->Mfst.FindChannel(name) : nullptr;
}

const ChannelDef* ReaderSession::FindChannel(ChannelId id) const noexcept {
    return m_Impl ? m_Impl->Mfst.FindChannel(id) : nullptr;
}

Timestamp ReaderSession::StartNs() const noexcept {
    return m_Impl ? m_Impl->Mfst.StartNs() : 0;
}

Timestamp ReaderSession::EndNs() const noexcept {
    return m_Impl ? m_Impl->Mfst.EndNs() : 0;
}

Status ReaderSession::Seek(Timestamp targetNs) {
    if (!m_Impl) return Status::ErrorNotOpen;

    detail::ReaderImpl* p = m_Impl.get();
    p->InChunk = false;
    if (p->Segs.empty()) return Status::ErrorNotFound;

    // Find the segment that covers targetNs (last segment whose StartNs <= targetNs)
    size_t segIdx = 0;
    for (size_t i = 0; i < p->Segs.size(); ++i) {
        if (p->Segs[i].StartNs <= targetNs)
            segIdx = i;
    }
    p->CurSeg = segIdx;

    // Use the seek index to find the earliest offset covering targetNs
    const detail::SeekIndex& idx = p->Segs[segIdx].Idx;
    uint64_t minOffset = sizeof(format::FileHeader);
    bool found = false;

    for (const auto& ch : p->Mfst.Channels) {
        uint64_t off = idx.Find(ch.Id, targetNs);
        if (off != detail::SeekIndex::NO_OFFSET) {
            if (!found || off < minOffset) {
                minOffset = off;
                found     = true;
            }
        }
    }

    p->CurOffset = found ? minOffset : sizeof(format::FileHeader);
    return Status::Ok;
}

bool ReaderSession::ReadNext(MessageView& out) {
    if (!m_Impl) return false;
    return m_Impl->NextMessage(out);
}

Status ReaderSession::Read(const ReadOptions& options, const ReadCallback& cb) {
    if (!m_Impl) return Status::ErrorNotOpen;
    if (!cb)     return Status::ErrorInvalidArg;

    // Seek to start if requested
    if (options.StartNs > 0) {
        Status st = Seek(options.StartNs);
        if (st != Status::Ok) return st;
    }

    const bool filterChannels = !options.ChannelFilter.empty();
    MessageView msg;
    while (m_Impl->NextMessage(msg)) {
        if (options.EndNs > 0 && msg.TimestampNs > options.EndNs)
            break;
        if (options.StartNs > 0 && msg.TimestampNs < options.StartNs)
            continue;
        if (filterChannels) {
            bool keep = false;
            for (ChannelId id : options.ChannelFilter)
                if (id == msg.Channel) { keep = true; break; }
            if (!keep) continue;
        }
        if (!cb(msg)) break;
    }
    return Status::Ok;
}

Status ReaderSession::ReadChannel(ChannelId           channelId,
                                  Timestamp           startNs,
                                  Timestamp           endNs,
                                  const ReadCallback& cb) {
    ReadOptions opts;
    opts.ChannelFilter.push_back(channelId);
    opts.StartNs = startNs;
    opts.EndNs   = endNs;
    return Read(opts, cb);
}

std::vector<Annotation> ReaderSession::Annotations() const {
    return m_Impl ? m_Impl->Annots : std::vector<Annotation>{};
}

std::vector<Annotation> ReaderSession::Annotations(Timestamp startNs, Timestamp endNs) const {
    if (!m_Impl) return {};
    std::vector<Annotation> result;
    for (const auto& a : m_Impl->Annots)
        if (a.TimestampNs >= startNs && (endNs == 0 || a.TimestampNs <= endNs))
            result.push_back(a);
    return result;
}

uint64_t ReaderSession::TotalMessageCount() const noexcept {
    if (!m_Impl) return 0;
    uint64_t total = 0;
    for (uint64_t c : m_Impl->ChTotalCount) total += c;
    return total;
}

uint64_t ReaderSession::MessageCount(ChannelId ch) const noexcept {
    if (!m_Impl || ch >= m_Impl->ChTotalCount.size()) return 0;
    return m_Impl->ChTotalCount[ch];
}

uint64_t ReaderSession::CorruptRecordCount() const noexcept {
    return m_Impl ? m_Impl->CorruptCount : 0;
}

} // namespace recplay

