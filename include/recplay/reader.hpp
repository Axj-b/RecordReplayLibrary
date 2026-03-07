#pragma once

/// @file reader.hpp
/// @brief ReaderSession — the read/replay-side API for opening .rec recordings.

// local includes
#include "export.hpp"
#include "types.hpp"
#include "channel.hpp"
#include "session.hpp"

// system includes
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace recplay {

namespace detail { struct ReaderImpl; }

// ---------------------------------------------------------------------------
// MessageView — a non-owning view of a single decoded message
// ---------------------------------------------------------------------------

/// Non-owning reference to a decoded message returned by the reader.
///
/// The memory pointed to by `Data` is valid only until the next call to
/// ReaderSession::ReadNext() or ReaderSession::Close().
/// Copy the bytes if you need them to outlive the read call.
struct MessageView {
    ChannelId   Channel     = INVALID_CHANNEL_ID;
    Timestamp   TimestampNs = 0;
    const void* Data        = nullptr;
    uint32_t    Length      = 0;

    /// Returns true if this is a valid, populated message.
    bool IsValid() const noexcept { return Channel != INVALID_CHANNEL_ID && Data != nullptr; }
};

// ---------------------------------------------------------------------------
// Annotation — a timestamped label stored in the recording
// ---------------------------------------------------------------------------

struct Annotation {
    Timestamp            TimestampNs = 0;
    std::string          Label;
    std::vector<uint8_t> Metadata;
};

// ---------------------------------------------------------------------------
// ReadOptions — controls filtering during iteration / seek
// ---------------------------------------------------------------------------

struct ReadOptions {
    /// Only yield messages from these channel IDs. Empty = all channels.
    std::vector<ChannelId> ChannelFilter;

    /// Only yield messages with TimestampNs >= StartNs. 0 = beginning.
    Timestamp StartNs = 0;

    /// Only yield messages with TimestampNs <= EndNs. 0 = end of session.
    Timestamp EndNs = 0;
};

// ---------------------------------------------------------------------------
// ReaderCallback — used by the channel-range reader overload
// ---------------------------------------------------------------------------

/// Callback signature for ReaderSession::ReadChannel().
/// Return false from the callback to stop iteration early.
using ReadCallback = std::function<bool(const MessageView&)>;

// ---------------------------------------------------------------------------
// ReaderSession
// ---------------------------------------------------------------------------

/// Read-side session handle. Not copyable; moveable.
///
/// Can open a session by pointing to its directory (which contains
/// session.manifest). Transparently reads across all segment files.
///
/// Typical usage:
/// @code
///   recplay::ReaderSession reader;
///   reader.Open("/recordings/20260227T120000_a1b2c3d4");
///
///   for (const auto& ch : reader.Channels())
///       std::cout << ch.Name << "\n";
///
///   reader.Seek(myStartNs);
///
///   recplay::MessageView msg;
///   while (reader.ReadNext(msg)) {
///       process(msg);
///   }
/// @endcode
///
/// Thread-safety:
/// - Not thread-safe for concurrent calls on the same instance.
/// - External synchronisation is required when multiple threads share one ReaderSession.
class RECPLAY_API ReaderSession final {
public:
    ReaderSession() noexcept;
    ~ReaderSession() noexcept;

    ReaderSession(ReaderSession&&) noexcept;
    ReaderSession& operator=(ReaderSession&&) noexcept;

    ReaderSession(const ReaderSession&)            = delete;
    ReaderSession& operator=(const ReaderSession&) = delete;

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    /// Open a session directory for reading.
    ///
    /// Reads session.manifest, maps all segment files, and loads all segment
    /// indexes into memory for O(log n) seeking.
    ///
    /// @param sessionPath  Path to the session directory (contains session.manifest).
    Status Open(const std::string& sessionPath);

    /// Close the session and release all resources.
    Status Close();

    bool IsOpen() const noexcept;

    // ------------------------------------------------------------------
    // Session metadata
    // ------------------------------------------------------------------

    const SessionManifest& Manifest() const noexcept;

    const std::vector<ChannelDef>& Channels() const noexcept;

    /// Find a channel definition by name. Returns nullptr if not found.
    const ChannelDef* FindChannel(const std::string& name) const noexcept;

    /// Find a channel definition by ID. Returns nullptr if not found.
    const ChannelDef* FindChannel(ChannelId id) const noexcept;

    /// Timestamp of the first message in the session.
    Timestamp StartNs() const noexcept;

    /// Timestamp of the last message in the session.
    Timestamp EndNs() const noexcept;

    // ------------------------------------------------------------------
    // Sequential iteration
    // ------------------------------------------------------------------

    /// Seek the read cursor to the first message with TimestampNs >= targetNs.
    ///
    /// Uses the in-memory index for O(log n) seek per channel.
    /// After Seek(), ReadNext() will yield messages from that position onward.
    ///
    /// @param targetNs  Target timestamp in nanoseconds.
    Status Seek(Timestamp targetNs);

    /// Read the next message in timestamp order across all channels.
    ///
    /// @param out  Populated on success. Valid until the next call to ReadNext().
    /// @return true if a message was returned; false at end-of-session.
    bool ReadNext(MessageView& out);

    // ------------------------------------------------------------------
    // Filtered / channel-level access
    // ------------------------------------------------------------------

    /// Iterate over all messages matching the given options, in timestamp order.
    ///
    /// @param options   Filtering options (channel subset, time range).
    /// @param callback  Called for each matching message. Return false to stop early.
    /// @return Status::Ok when iteration completes or callback returned false.
    Status Read(const ReadOptions& options, const ReadCallback& callback);

    /// Convenience: iterate over a single channel in a time range.
    ///
    /// @param channelId  Channel to iterate.
    /// @param startNs    Start timestamp (0 = beginning of session).
    /// @param endNs      End timestamp   (0 = end of session).
    /// @param callback   Called for each message. Return false to stop early.
    Status ReadChannel(ChannelId           channelId,
                       Timestamp           startNs,
                       Timestamp           endNs,
                       const ReadCallback& callback);

    // ------------------------------------------------------------------
    // Annotations
    // ------------------------------------------------------------------

    /// Return all annotations in the session, ordered by timestamp.
    std::vector<Annotation> Annotations() const;

    /// Return annotations within a time range.
    std::vector<Annotation> Annotations(Timestamp startNs, Timestamp endNs) const;

    // ------------------------------------------------------------------
    // Diagnostics
    // ------------------------------------------------------------------

    /// Total number of messages across all channels.
    uint64_t TotalMessageCount() const noexcept;

    /// Number of messages on a specific channel.
    uint64_t MessageCount(ChannelId channelId) const noexcept;

    /// Number of corrupt records detected during this session (requires CRC enabled).
    uint64_t CorruptRecordCount() const noexcept;

private:
    std::unique_ptr<detail::ReaderImpl> m_Impl;
};

}  // namespace recplay
