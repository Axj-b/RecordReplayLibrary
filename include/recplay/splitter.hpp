#pragma once

/// @file splitter.hpp
/// @brief Splitter and Merger — post-process split a muxed session into per-channel
///        sessions, or merge per-channel sessions back into one muxed session.
///
/// Both operations work entirely on the index and chunk files — raw payload bytes
/// are simply relocated, never re-parsed or re-compressed.

// local includes
#include "export.hpp"
#include "types.hpp"
#include "channel.hpp"
#include "session.hpp"

// system includes
#include <functional>
#include <string>
#include <vector>

namespace recplay {

// ---------------------------------------------------------------------------
// Progress callback
// ---------------------------------------------------------------------------

/// Progress callback invoked periodically during split or merge operations.
/// @param bytes_processed  Bytes read so far.
/// @param bytes_total      Total bytes to process (may be 0 if unknown).
/// Return false to request cancellation (operation will stop at the next chunk boundary).
using ProgressCallback = std::function<bool(uint64_t bytes_processed, uint64_t bytes_total)>;

// ---------------------------------------------------------------------------
// SplitOptions
// ---------------------------------------------------------------------------

/// Options controlling a split operation.
struct SplitOptions {
    /// Channels to extract. If empty, each channel is extracted into its own session.
    /// If non-empty, only the listed channel IDs are extracted.
    std::vector<ChannelId> ChannelIds;

    /// If true, each channel is written to its own named sub-directory under output dir.
    /// If false, all selected channels are written to one session per channel in output dir.
    /// Default: true.
    bool OneDirPerChannel = true;

    /// Optional naming pattern for output session directories.
    /// The placeholder {channel_name} is replaced with the channel's name (slashes → underscores).
    /// Default: "{original_session_name}_{channel_name}".
    std::string OutputDirPattern;

    /// Maximum segment size for the output sessions.
    /// Defaults to the same limit as the source session.
    uint64_t MaxSegmentBytes = 0;

    /// Whether to preserve annotations in each output session.
    bool IncludeAnnotations = true;

    /// Optional progress callback. nullptr = no progress reporting.
    ProgressCallback Progress;
};

// ---------------------------------------------------------------------------
// MergeOptions
// ---------------------------------------------------------------------------

/// Options controlling a merge operation.
struct MergeOptions {
    /// Maximum segment size for the merged output session.
    uint64_t MaxSegmentBytes = format::DEFAULT_MAX_SEGMENT_BYTES;

    /// If true and two input sessions have a channel with the same name, they are
    /// merged into one channel in the output. If false, conflicting names cause an error.
    bool MergeDuplicateChannelNames = false;

    /// If true, annotations from all input sessions are included in the output.
    bool IncludeAnnotations = true;

    /// Optional progress callback.
    ProgressCallback Progress;
};

// ---------------------------------------------------------------------------
// SplitResult / MergeResult
// ---------------------------------------------------------------------------

/// Result of a successful split operation.
struct SplitResult {
    /// One entry per channel extracted.
    struct Entry {
        ChannelId   Channel;
        std::string ChannelName;
        std::string SessionPath; ///< Path to the new single-channel session directory
        uint64_t    BytesWritten;
        uint64_t    MessageCount;
    };

    std::vector<Entry> Outputs;

    uint64_t TotalBytesWritten() const noexcept;
};

/// Result of a successful merge operation.
struct MergeResult {
    std::string SessionPath;        ///< Path to the merged session directory
    uint64_t    TotalBytesWritten;
    uint64_t    TotalMessageCount;
    uint64_t    ChannelCount;
};

// ---------------------------------------------------------------------------
// Splitter
// ---------------------------------------------------------------------------

/// Utility class for splitting and merging recording sessions.
///
/// All methods are static — there is no instance state.
///
/// Split: muxed session → N single-channel sessions.
/// Merge: N sessions   → one muxed session.
///
/// Neither operation re-compresses or re-encodes payloads; chunk boundaries
/// from the source session(s) are preserved where possible.
class RECPLAY_API Splitter final {
public:

    // ------------------------------------------------------------------
    // Split
    // ------------------------------------------------------------------

    /// Split a muxed session into per-channel session directories.
    ///
    /// The source session is not modified. New session directories are created
    /// under outputDir.
    ///
    /// @param sourceSessionPath  Path to the source session directory.
    /// @param outputDir          Directory where per-channel sessions are created.
    /// @param options            Split options (channel filter, naming, etc.).
    /// @param outResult          On success, describes the created sessions.
    /// @return Status::Ok on success.
    static Status Split(const std::string&  sourceSessionPath,
                        const std::string&  outputDir,
                        const SplitOptions& options,
                        SplitResult&        outResult);

    /// Convenience overload with default options.
    static Status Split(const std::string& sourceSessionPath,
                        const std::string& outputDir,
                        SplitResult&       outResult);

    // ------------------------------------------------------------------
    // Merge
    // ------------------------------------------------------------------

    /// Merge one or more sessions into a single muxed session.
    ///
    /// All input sessions must have non-overlapping channel names, or
    /// MergeOptions::MergeDuplicateChannelNames must be true.
    ///
    /// Records from all inputs are interleaved in ascending timestamp order.
    ///
    /// @param sourceSessionPaths  Paths to the sessions to merge (order does not matter).
    /// @param outputDir           Directory where the merged session is created.
    /// @param options             Merge options.
    /// @param outResult           On success, describes the created session.
    /// @return Status::Ok on success.
    static Status Merge(const std::vector<std::string>& sourceSessionPaths,
                        const std::string&              outputDir,
                        const MergeOptions&             options,
                        MergeResult&                    outResult);

    /// Convenience overload with default options.
    static Status Merge(const std::vector<std::string>& sourceSessionPaths,
                        const std::string&              outputDir,
                        MergeResult&                    outResult);

    // ------------------------------------------------------------------
    // Validation
    // ------------------------------------------------------------------

    /// Validate the integrity of a session: checks magic bytes, CRCs if enabled,
    /// index consistency, and manifest completeness.
    ///
    /// @param sessionPath        Session directory to validate.
    /// @param outCorruptRecords  If non-null, receives the count of corrupt records found.
    /// @return Status::Ok if the session is fully intact.
    static Status Validate(const std::string& sessionPath,
                           uint64_t*          outCorruptRecords = nullptr);

    // ------------------------------------------------------------------
    // Recovery
    // ------------------------------------------------------------------

    /// Attempt to recover a session that was not cleanly closed (e.g. after a crash).
    ///
    /// Performs a linear scan of all segment files, skips corrupt records, rebuilds
    /// the index, and writes a corrected footer and manifest.
    ///
    /// @param sessionPath  Session directory to recover in-place.
    /// @return Status::Ok if at least partial data could be recovered.
    static Status Recover(const std::string& sessionPath);

private:
    Splitter() = delete;
};

}  // namespace recplay