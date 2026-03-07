/// @file splitter.cpp
/// @brief Splitter / Merger implementation.

// local includes
#include <recplay/splitter.hpp>
#include <recplay/reader.hpp>
#include <recplay/writer.hpp>
#include "detail/index_builder.hpp"
#include "detail/manifest_io.hpp"
#include "detail/file_map.hpp"
#include "detail/crc32.hpp"
#include "detail/platform.hpp"

// system includes
#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <limits>
#include <queue>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace recplay {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/// Replace characters illegal in directory names ('/', '\\', ':', ' ') with '_'
static std::string SanitiseName(const std::string& name) {
    std::string r = name;
    for (char& c : r)
        if (c == '/' || c == '\\' || c == ':' || c == ' ') c = '_';
    return r;
}

/// Build an output session directory pattern like "{base}_{channelName}".
static std::string MakeSplitDirName(const std::string& pattern,
                                    const std::string& sessionBase,
                                    const std::string& channelName) {
    const std::string sanitised = SanitiseName(channelName);
    if (pattern.empty())
        return sessionBase + "_" + sanitised;

    std::string result = pattern;

    auto replace_all = [](std::string& text,
                          const std::string& needle,
                          const std::string& repl) {
        for (size_t pos = 0; (pos = text.find(needle, pos)) != std::string::npos; ) {
            text.replace(pos, needle.size(), repl);
            pos += repl.size();
        }
    };

    replace_all(result, "{channel_name}", sanitised);
    replace_all(result, "{original_session_name}", sessionBase);
    return result;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// SplitResult
// ---------------------------------------------------------------------------

uint64_t SplitResult::TotalBytesWritten() const noexcept {
    uint64_t total = 0;
    for (const auto& e : Outputs) total += e.BytesWritten;
    return total;
}

// ---------------------------------------------------------------------------
// Splitter::Split
// ---------------------------------------------------------------------------

Status Splitter::Split(const std::string&  sourceSessionPath,
                       const std::string&  outputDir,
                       const SplitOptions& opts,
                       SplitResult&        outResult) {
    outResult = {};

    // Open source
    ReaderSession src;
    Status st = src.Open(sourceSessionPath);
    if (st != Status::Ok) return st;

    const SessionManifest& mfst = src.Manifest();

    // Determine session base name (last component of source path)
    const std::string sessionBase =
        fs::path(sourceSessionPath).filename().string();

    // Build the list of channels to extract
    std::vector<ChannelDef> toExtract;
    if (opts.ChannelIds.empty()) {
        toExtract = mfst.Channels;
    } else {
        for (ChannelId id : opts.ChannelIds) {
            const ChannelDef* def = mfst.FindChannel(id);
            if (def) toExtract.push_back(*def);
        }
    }
    if (toExtract.empty()) return Status::ErrorNotFound;

    // Open one RecorderSession per channel
    struct OutSession {
        RecorderSession recorder;
        ChannelId       srcId = INVALID_CHANNEL_ID;
        ChannelId       dstId = INVALID_CHANNEL_ID;
        std::string     sessionPath;
        std::string     channelName;
    };

    std::vector<OutSession> outs(toExtract.size());
    auto close_outs_best_effort = [&]() {
        for (auto& o : outs)
            o.recorder.Close();
    };

    for (size_t i = 0; i < toExtract.size(); ++i) {
        const ChannelDef& ch = toExtract[i];
        auto& o = outs[i];
        o.srcId       = ch.Id;
        o.channelName = ch.Name;

        SessionConfig cfg;
        cfg.OutputDir       = opts.OneDirPerChannel
                                  ? (outputDir + static_cast<char>(detail::PATH_SEP) + SanitiseName(ch.Name))
                                  : outputDir;
        cfg.SessionName     = MakeSplitDirName(opts.OutputDirPattern, sessionBase, ch.Name);
        cfg.MaxSegmentBytes = opts.MaxSegmentBytes > 0
                                  ? opts.MaxSegmentBytes
                                  : mfst.Segments.empty()
                                        ? format::DEFAULT_MAX_SEGMENT_BYTES
                                        : mfst.Segments[0].SizeBytes;
        cfg.CrcEnabled      = true;

        st = o.recorder.Open(cfg);
        if (st != Status::Ok) {
            close_outs_best_effort();
            return st;
        }
        o.sessionPath = o.recorder.SessionPath();

        ChannelConfig ccfg;
        ccfg.Name               = ch.Name;
        ccfg.Layer              = ch.Layer;
        ccfg.Compression        = ch.Compression;
        ccfg.ChunkSizeBytes     = ch.ChunkSizeBytes;
        ccfg.ChunkFlushIntervalMs = ch.ChunkFlushIntervalMs;
        ccfg.Schema             = ch.Schema;
        ccfg.UserMetadata       = ch.UserMetadata;
        st = o.recorder.DefineChannel(ccfg, o.dstId);
        if (st != Status::Ok) {
            close_outs_best_effort();
            return st;
        }
    }

    // Stream all messages — dispatch by channel
    MessageView msg;
    uint64_t bytesProcessed = 0;
    while (src.ReadNext(msg)) {
        for (auto& o : outs) {
            if (o.srcId == msg.Channel) {
                st = o.recorder.Write(o.dstId, msg.TimestampNs, msg.Data, msg.Length);
                if (st != Status::Ok) {
                    close_outs_best_effort();
                    return st;
                }
                break;
            }
        }

        bytesProcessed += msg.Length;
        if (opts.Progress && !opts.Progress(bytesProcessed, 0)) break;
    }

    // Annotations
    if (opts.IncludeAnnotations) {
        for (const auto& ann : src.Annotations()) {
            for (auto& o : outs) {
                const void* meta = ann.Metadata.empty() ? nullptr : ann.Metadata.data();
                st = o.recorder.Annotate(ann.TimestampNs, ann.Label,
                                         meta, static_cast<uint32_t>(ann.Metadata.size()));
                if (st != Status::Ok) {
                    close_outs_best_effort();
                    return st;
                }
            }
        }
    }

    // Close and collect results
    for (auto& o : outs) {
        const uint64_t msgCnt = o.recorder.MessagesWritten(o.dstId);
        const uint64_t bytes  = o.recorder.CurrentSegmentBytes();
        (void)bytes; // will be re-read from manifest after close
        st = o.recorder.Close();
        if (st != Status::Ok) return st;

        SplitResult::Entry entry{};
        entry.Channel      = o.srcId;
        entry.ChannelName  = o.channelName;
        entry.SessionPath  = o.sessionPath;
        entry.MessageCount = msgCnt;
        // Sum segment sizes from the closed manifest on disk
        {
            SessionManifest closed;
            if (detail::ReadManifest(o.sessionPath, closed) == Status::Ok)
                entry.BytesWritten = closed.TotalSizeBytes();
        }
        outResult.Outputs.push_back(std::move(entry));
    }

    return Status::Ok;
}

Status Splitter::Split(const std::string& source,
                       const std::string& outputDir,
                       SplitResult&       result) {
    return Split(source, outputDir, SplitOptions{}, result);
}

// ---------------------------------------------------------------------------
// Splitter::Merge
// ---------------------------------------------------------------------------

Status Splitter::Merge(const std::vector<std::string>& sources,
                       const std::string&              outputDir,
                       const MergeOptions&             opts,
                       MergeResult&                    outResult) {
    outResult = {};
    if (sources.empty()) return Status::ErrorInvalidArg;

    // Open all sources
    std::vector<ReaderSession> readers(sources.size());
    for (size_t i = 0; i < sources.size(); ++i) {
        Status st = readers[i].Open(sources[i]);
        if (st != Status::Ok) return st;
    }

    // Build merged channel list and source->destination mapping.
    struct SourceChannelMap {
        size_t    ReaderIdx;
        ChannelId SourceChannel;
        size_t    MergedIndex;
    };

    std::vector<ChannelDef>      mergedChannels;
    std::vector<SourceChannelMap> chMap;

    for (size_t ri = 0; ri < readers.size(); ++ri) {
        for (const ChannelDef& ch : readers[ri].Channels()) {
            size_t mergedIndex = SIZE_MAX;
            for (size_t mi = 0; mi < mergedChannels.size(); ++mi) {
                if (mergedChannels[mi].Name == ch.Name) {
                    mergedIndex = mi;
                    break;
                }
            }

            if (mergedIndex == SIZE_MAX) {
                mergedIndex = mergedChannels.size();
                ChannelDef copy = ch;
                copy.Id = static_cast<ChannelId>(mergedIndex);
                mergedChannels.push_back(std::move(copy));
            } else if (!opts.MergeDuplicateChannelNames) {
                return Status::ErrorInvalidArg;
            }

            chMap.push_back({ri, ch.Id, mergedIndex});
        }
    }

    // Open output recorder
    RecorderSession dst;
    SessionConfig cfg;
    cfg.OutputDir       = outputDir;
    cfg.MaxSegmentBytes = opts.MaxSegmentBytes;
    cfg.CrcEnabled      = true;
    Status st = dst.Open(cfg);
    if (st != Status::Ok) return st;

    const std::string dstPath = dst.SessionPath();

    // Define all channels
    std::vector<ChannelId> dstIds(mergedChannels.size());
    for (size_t i = 0; i < mergedChannels.size(); ++i) {
        const ChannelDef& mc = mergedChannels[i];
        ChannelConfig ccfg;
        ccfg.Name               = mc.Name;
        ccfg.Layer              = mc.Layer;
        ccfg.Compression        = mc.Compression;
        ccfg.ChunkSizeBytes     = mc.ChunkSizeBytes;
        ccfg.ChunkFlushIntervalMs = mc.ChunkFlushIntervalMs;
        ccfg.Schema             = mc.Schema;
        ccfg.UserMetadata       = mc.UserMetadata;
        st = dst.DefineChannel(ccfg, dstIds[i]);
        if (st != Status::Ok) {
            dst.Close();
            return st;
        }
    }

    // Buffer of the next unread message per reader
    std::vector<MessageView> heads(readers.size());
    std::vector<bool>        headValid(readers.size(), false);

    auto fetchHead = [&](size_t ri) -> bool {
        MessageView mv;
        if (readers[ri].ReadNext(mv)) {
            heads[ri]     = mv;
            headValid[ri] = true;
            return true;
        }
        headValid[ri] = false;
        return false;
    };

    // Seed
    for (size_t ri = 0; ri < readers.size(); ++ri)
        fetchHead(ri);

    uint64_t totalMessages = 0;
    uint64_t bytesProcessed = 0;

    while (true) {
        // Find reader with smallest timestamp
        size_t   best   = SIZE_MAX;
        Timestamp bestTs = std::numeric_limits<Timestamp>::max();
        for (size_t ri = 0; ri < readers.size(); ++ri) {
            if (headValid[ri] && heads[ri].TimestampNs < bestTs) {
                bestTs = heads[ri].TimestampNs;
                best   = ri;
            }
        }
        if (best == SIZE_MAX) break;

        const MessageView& mv = heads[best];

        // Map srcChannel → dstChannel
        ChannelId dstCh = INVALID_CHANNEL_ID;
        for (const auto& map : chMap) {
            if (map.ReaderIdx == best && map.SourceChannel == mv.Channel) {
                if (map.MergedIndex < dstIds.size()) {
                    dstCh = dstIds[map.MergedIndex];
                    break;
                }
            }
        }

        if (dstCh == INVALID_CHANNEL_ID) {
            dst.Close();
            return Status::ErrorCorrupted;
        }

        st = dst.Write(dstCh, mv.TimestampNs, mv.Data, mv.Length);
        if (st != Status::Ok) {
            dst.Close();
            return st;
        }

        bytesProcessed += mv.Length;
        ++totalMessages;

        if (opts.Progress && !opts.Progress(bytesProcessed, 0)) break;

        fetchHead(best);
    }

    // Annotations
    if (opts.IncludeAnnotations) {
        std::vector<Annotation> allAnns;
        for (auto& r : readers)
            for (const auto& a : r.Annotations())
                allAnns.push_back(a);
        std::sort(allAnns.begin(), allAnns.end(),
                  [](const Annotation& a, const Annotation& b){
                      return a.TimestampNs < b.TimestampNs; });
        for (const auto& ann : allAnns) {
            const void* meta = ann.Metadata.empty() ? nullptr : ann.Metadata.data();
            st = dst.Annotate(ann.TimestampNs, ann.Label,
                              meta, static_cast<uint32_t>(ann.Metadata.size()));
            if (st != Status::Ok) {
                dst.Close();
                return st;
            }
        }
    }

    st = dst.Close();
    if (st != Status::Ok) return st;

    outResult.SessionPath        = dstPath;
    outResult.TotalMessageCount  = totalMessages;
    outResult.ChannelCount       = mergedChannels.size();
    {
        SessionManifest closed;
        if (detail::ReadManifest(dstPath, closed) == Status::Ok)
            outResult.TotalBytesWritten = closed.TotalSizeBytes();
    }

    return Status::Ok;
}

Status Splitter::Merge(const std::vector<std::string>& sources,
                       const std::string&              outputDir,
                       MergeResult&                    result) {
    return Merge(sources, outputDir, MergeOptions{}, result);
}

// ---------------------------------------------------------------------------
// Splitter::Validate
// ---------------------------------------------------------------------------

Status Splitter::Validate(const std::string& sessionPath,
                           uint64_t*          outCorruptRecords) {
    uint64_t corrupt = 0;

    SessionManifest mfst;
    Status st = detail::ReadManifest(sessionPath, mfst);
    if (st != Status::Ok) return st;

    for (const auto& segInfo : mfst.Segments) {
        const std::string filepath =
            sessionPath + static_cast<char>(detail::PATH_SEP) + segInfo.Filename;

        detail::MappedFile file = detail::MappedFile::OpenRead(filepath);
        if (!file.IsOpen()) return Status::ErrorIO;

        const uint8_t* base = file.Data();
        const uint64_t size = file.FileSize();

        // Check minimum size
        if (size < sizeof(format::FileHeader) + sizeof(format::FileFooter)) {
            return Status::ErrorCorrupted;
        }

        // Verify file header magic
        if (std::memcmp(base, format::MAGIC, 8) != 0) {
            return Status::ErrorCorrupted;
        }

        // Verify footer magic (last 32 bytes)
        const auto* footer = reinterpret_cast<const format::FileFooter*>(
            base + size - sizeof(format::FileFooter));
        if (std::memcmp(footer->Magic, format::MAGIC, 8) != 0) {
            return Status::ErrorCorrupted;
        }

        // Verify footer CRC (covers bytes 0-23: Magic + IndexOffset + RecordCount)
        const uint32_t expectedFooterCrc =
            detail::Le32ToHost(footer->Crc32);
        const uint32_t computedFooterCrc =
            detail::Crc32(reinterpret_cast<const uint8_t*>(footer), 24);
        if (expectedFooterCrc != computedFooterCrc) {
            return Status::ErrorCorrupted;
        }

        // Walk all records and validate envelope CRCs
        uint64_t offset     = sizeof(format::FileHeader);
        uint64_t recCount   = 0;

        while (offset + sizeof(format::RecordEnvelope) <= size) {
            const auto* env = reinterpret_cast<const format::RecordEnvelope*>(base + offset);
            const uint32_t payloadLen = detail::Le32ToHost(env->PayloadLength);
            const uint64_t nextOffset = offset + sizeof(format::RecordEnvelope) + payloadLen;

            if (nextOffset > size) break;

            if (env->Flags & static_cast<uint8_t>(RECORD_FLAG_CRC)) {
                uint32_t crc = detail::Crc32(reinterpret_cast<const uint8_t*>(env), 16);
                crc = detail::Crc32(base + offset + sizeof(format::RecordEnvelope),
                                    payloadLen, crc);
                if (detail::HostToLe32(crc) != env->Crc32)
                    ++corrupt;
            }

            ++recCount;
            offset = nextOffset;
        }

        // Cross-check record count against footer
        const uint64_t footerRecCount = detail::Le64ToHost(footer->RecordCount);
        if (recCount != footerRecCount)
            ++corrupt;
    }

    if (outCorruptRecords) *outCorruptRecords = corrupt;
    return (corrupt == 0) ? Status::Ok : Status::ErrorCorrupted;
}

// ---------------------------------------------------------------------------
// Splitter::Recover
// ---------------------------------------------------------------------------

Status Splitter::Recover(const std::string& sessionPath) {
    SessionManifest mfst;
    Status st = detail::ReadManifest(sessionPath, mfst);
    // If the manifest is missing or corrupt, try to discover .rec files
    if (st != Status::Ok) mfst = {};

    // Determine session directory listing for .rec files
    if (mfst.Segments.empty()) {
        try {
            for (const auto& entry : fs::directory_iterator(sessionPath)) {
                if (entry.path().extension() == ".rec") {
                    SegmentInfo si;
                    si.Filename     = entry.path().filename().string();
                    si.SegmentIndex = static_cast<uint32_t>(mfst.Segments.size());
                    mfst.Segments.push_back(si);
                }
            }
        } catch (...) {
            return Status::ErrorIO;
        }
        std::sort(mfst.Segments.begin(), mfst.Segments.end(),
                  [](const SegmentInfo& a, const SegmentInfo& b){
                      return a.Filename < b.Filename; });
    }

    bool anyRecovered = false;

    for (auto& segInfo : mfst.Segments) {
        const std::string filepath =
            sessionPath + static_cast<char>(detail::PATH_SEP) + segInfo.Filename;

        // Read the existing file bytes
        detail::MappedFile file = detail::MappedFile::OpenRead(filepath);
        if (!file.IsOpen()) continue;

        const uint8_t* base = file.Data();
        const uint64_t size = file.FileSize();

        if (size < sizeof(format::FileHeader)) continue;

        // Validate header magic; if corrupt skip
        if (std::memcmp(base, format::MAGIC, 8) != 0) continue;

        // Read header to get session ID for manifest
        const auto* hdr = reinterpret_cast<const format::FileHeader*>(base);
        std::memcpy(mfst.Id.data(), hdr->SessionId, 16);
        if (mfst.CreatedAtNs == 0)
            mfst.CreatedAtNs = detail::Le64ToHost(hdr->CreatedAtNs);

        // Scan all records — harvest channel defs, data timestamps, build index
        detail::IndexBuilder idx(1'000'000'000ull);

        uint64_t offset   = sizeof(format::FileHeader);
        uint64_t recCount = 0;
        uint64_t startNs  = std::numeric_limits<uint64_t>::max();
        uint64_t endNs    = 0;

        while (offset + sizeof(format::RecordEnvelope) <= size) {
            const auto* env = reinterpret_cast<const format::RecordEnvelope*>(base + offset);
            const uint32_t payloadLen = detail::Le32ToHost(env->PayloadLength);
            const uint64_t nextOffset = offset + sizeof(format::RecordEnvelope) + payloadLen;
            if (nextOffset > size) break;

            const auto     op  = static_cast<RecordOp>(env->Op);
            const uint64_t ts  = detail::Le64ToHost(env->TimestampNs);
            const ChannelId ch = detail::Le16ToHost(env->Channel);

            ++recCount;

            if (op == RecordOp::Data || op == RecordOp::Chunk) {
                if (ts < startNs) startNs = ts;
                if (ts > endNs)   endNs   = ts;
                idx.MaybeAdd(ch, ts, offset);
            } else if (op == RecordOp::ChannelDef) {
                // Re-harvest channel definitions if not in manifest
                // (minimal parsing: just track IDs)
            }

            offset = nextOffset;
        }

        if (startNs == std::numeric_limits<uint64_t>::max()) startNs = 0;
        segInfo.StartNs  = startNs;
        segInfo.EndNs    = endNs;
        segInfo.SizeBytes = size; // will be overwritten on rewrite

        // Close read mapping so we can open for write
        file.Close();

        // Rewrite: copy payload up to `offset`, append reconstructed index + footer
        {
            const std::string tmpPath = filepath + ".recover";
            detail::MappedFile writer = detail::MappedFile::OpenWrite(tmpPath);
            if (!writer.IsOpen()) continue;

            // Copy verbatim bytes
            if (!writer.EnsureAvailable(offset)) { writer.Close(); continue; }
            {
                // Re-open original for reading the raw bytes
                detail::MappedFile orig = detail::MappedFile::OpenRead(filepath);
                if (!orig.IsOpen()) { writer.Close(); continue; }
                std::memcpy(writer.WritePtr(), orig.Data(), offset);
                writer.Advance(offset);
            }

            // Write INDEX record
            std::vector<uint8_t> indexPayload = idx.Serialise();
            uint64_t indexOffset = format::NO_INDEX;
            if (!indexPayload.empty()) {
                indexOffset = writer.BytesWritten();

                format::RecordEnvelope env2{};
                env2.Op            = static_cast<uint8_t>(RecordOp::Index);
                env2.Channel       = detail::HostToLe16(INVALID_CHANNEL_ID);
                env2.PayloadLength = detail::HostToLe32(
                    static_cast<uint32_t>(indexPayload.size()));
                env2.TimestampNs   = detail::HostToLe64(endNs);
                env2.Crc32         = 0;

                env2.Flags  = static_cast<uint8_t>(RECORD_FLAG_CRC); // set flag before CRC
                uint32_t crc = detail::Crc32(reinterpret_cast<const uint8_t*>(&env2), 16);
                crc = detail::Crc32(indexPayload.data(),
                                    static_cast<uint32_t>(indexPayload.size()), crc);
                env2.Crc32  = detail::HostToLe32(crc);

                const uint64_t need =
                    sizeof(env2) + indexPayload.size() + sizeof(format::FileFooter) + 4;
                if (!writer.EnsureAvailable(need)) { writer.Close(); continue; }

                std::memcpy(writer.WritePtr(), &env2, sizeof(env2));
                writer.Advance(sizeof(env2));
                std::memcpy(writer.WritePtr(), indexPayload.data(), indexPayload.size());
                writer.Advance(indexPayload.size());
                ++recCount;
            }

            // Write FILE FOOTER
            if (!writer.EnsureAvailable(sizeof(format::FileFooter))) {
                writer.Close(); continue;
            }
            {
                format::FileFooter footer{};
                std::memcpy(footer.Magic, format::MAGIC, 8);
                footer.IndexOffset = detail::HostToLe64(indexOffset);
                footer.RecordCount = detail::HostToLe64(recCount);
                footer.Crc32       = 0;
                footer.Crc32       = detail::HostToLe32(
                    detail::Crc32(reinterpret_cast<const uint8_t*>(&footer), 24));
                std::memset(footer.Reserved, 0, sizeof(footer.Reserved));

                std::memcpy(writer.WritePtr(), &footer, sizeof(footer));
                writer.Advance(sizeof(footer));
            }

            segInfo.SizeBytes = writer.BytesWritten();
            writer.Close();

            // Replace original (works on Windows where rename over an existing file can fail).
            try {
                fs::copy_file(tmpPath, filepath, fs::copy_options::overwrite_existing);
                fs::remove(tmpPath);
                anyRecovered = true;
            } catch (...) {
                fs::remove(tmpPath);
            }
        }
    }

    // Rewrite manifest
    if (mfst.RecorderVersion.empty()) mfst.RecorderVersion = "1.0.0 (recovered)";
    detail::WriteManifest(sessionPath, mfst);

    return anyRecovered ? Status::Ok : Status::ErrorCorrupted;
}

} // namespace recplay

