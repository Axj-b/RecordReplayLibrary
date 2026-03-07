#pragma once

/// @file writer.hpp
/// @brief RecorderSession — the write-side API for creating .rec recordings.

// local includes
#include "export.hpp"
#include "types.hpp"
#include "channel.hpp"
#include "session.hpp"

// system includes
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace recplay {

    namespace detail {
        struct WriterImpl;
    }

    // ---------------------------------------------------------------------------
    // RecorderSession
    // ---------------------------------------------------------------------------

    /// Write-side session handle. Not copyable; moveable.
    ///
    /// Typical usage:
    /// @code
    ///   recplay::RecorderSession rec;
    ///   rec.Open(config);
    ///
    ///   auto lidarCh = rec.DefineChannel({ "lidar/front", CaptureLayer::L3L4,
    ///                                       CompressionCodec::LZ4 });
    ///   auto canCh   = rec.DefineChannel({ "can/chassis", CaptureLayer::L7,
    ///                                       CompressionCodec::None });
    ///
    ///   // --- in your capture loop ---
    ///   rec.Write(lidarCh, timestampNs, packetPtr, packetLen);
    ///
    ///   rec.Close();
    /// @endcode
    ///
    /// Thread-safety:
    /// - Not thread-safe for concurrent calls on the same instance.
    /// - External synchronisation is required when multiple threads share one RecorderSession.
    class RECPLAY_API RecorderSession final {
        public:
            RecorderSession() noexcept;
            ~RecorderSession() noexcept;

            RecorderSession(RecorderSession &&) noexcept;
            RecorderSession &operator=(RecorderSession &&) noexcept;

            RecorderSession(const RecorderSession &)            = delete;
            RecorderSession &operator=(const RecorderSession &) = delete;

            // ------------------------------------------------------------------
            // Lifecycle
            // ------------------------------------------------------------------

            /// Open (or create) a recording session in the given directory.
            ///
            /// Creates the session sub-directory, writes the file header,
            /// SESSION_START record, and an initial session.manifest.
            ///
            /// @param config  Session-level configuration (output path, size limits, etc.)
            /// @return Status::Ok on success.
            Status Open(const SessionConfig &config);

            /// Close the session cleanly: flush all pending chunks, write the INDEX record,
            /// SESSION_END record, file footer, and update session.manifest.
            ///
            /// Calling Close() on an already-closed session is a no-op.
            Status Close();

            /// Returns true if the session is currently open for writing.
            bool IsOpen() const noexcept;

            // ------------------------------------------------------------------
            // Channel management
            // ------------------------------------------------------------------

            /// Register a new channel with the session.
            ///
            /// Writes a CHANNEL_DEF record to the current segment and registers the
            /// channel in the manifest. Must be called before the first Write() for
            /// this channel. All channels must be defined before or during writing —
            /// they cannot be removed once defined.
            ///
            /// @param config   Channel configuration.
            /// @param outId    Receives the assigned ChannelId on success.
            /// @return Status::Ok, or Status::ErrorAlreadyOpen if a channel with
            ///         the same name already exists.
            Status DefineChannel(const ChannelConfig &config, ChannelId &outId);

            /// Convenience overload that throws on error (for use in non-error-code contexts).
            ChannelId DefineChannel(const ChannelConfig &config);

            /// Retrieve the definition of a previously defined channel.
            /// Returns nullptr if the channelId is not known.
            const ChannelDef *GetChannelDef(ChannelId channelId) const noexcept;

            /// All channels currently defined in this session.
            const std::vector<ChannelDef> &Channels() const noexcept;

            // ------------------------------------------------------------------
            // Writing data
            // ------------------------------------------------------------------

            /// Write a single message to the given channel.
            ///
            /// For channels with compression enabled the payload is buffered in a
            /// chunk accumulator and written as a CHUNK record when the chunk size
            /// threshold or flush interval is reached.
            ///
            /// For channels with compression disabled a DATA record is written
            /// immediately.
            ///
            /// Triggers automatic segment rotation if the current segment is about
            /// to exceed the configured size or duration limit.
            ///
            /// @param channelId   Channel to write to (from DefineChannel()).
            /// @param timestampNs Capture timestamp in nanoseconds (UTC).
            /// @param data        Pointer to the payload bytes (not null if length > 0).
            /// @param length      Payload length in bytes. May be 0.
            /// @return Status::Ok on success.
            Status Write(ChannelId channelId,
                Timestamp          timestampNs,
                const void        *data,
                uint32_t           length);

            // ------------------------------------------------------------------
            // Annotations
            // ------------------------------------------------------------------

            /// Write a timestamped annotation (bookmark / event label) to the stream.
            ///
            /// Annotations are stored as ANNOTATION records in the session-level
            /// (channelId = INVALID_CHANNEL_ID) and are queryable by the reader.
            ///
            /// @param timestampNs   Timestamp of the event.
            /// @param label         Short human-readable label (UTF-8, max 255 bytes).
            /// @param metadata      Optional opaque metadata blob.
            Status Annotate(Timestamp timestampNs,
                const std::string    &label,
                const void           *metadata       = nullptr,
                uint32_t              metadataLength = 0);

            // ------------------------------------------------------------------
            // Manual flush / control
            // ------------------------------------------------------------------

            /// Force-flush all channel chunk accumulators to disk without closing.
            /// Useful before a time-sensitive operation or an external trigger snapshot.
            Status Flush();

            /// Force a segment rotation now, regardless of size/duration limits.
            /// The current segment is cleanly closed and a new one is opened.
            Status RotateSegment();

            // ------------------------------------------------------------------
            // Diagnostics
            // ------------------------------------------------------------------

            /// Approximate on-disk size of the current segment file in bytes.
            uint64_t CurrentSegmentBytes() const noexcept;

            /// Total messages written to the given channel since session open.
            uint64_t MessagesWritten(ChannelId channelId) const noexcept;

            /// Total messages dropped (e.g. due to I/O error) since session open.
            uint64_t MessagesDropped(ChannelId channelId) const noexcept;

            /// Session manifest (reflects the current state including open segments).
            const SessionManifest &Manifest() const noexcept;

            /// Path to the session directory on disk.
            const std::string &SessionPath() const noexcept;

        private:
            std::unique_ptr<detail::WriterImpl> m_Impl;
    };

} // namespace recplay
