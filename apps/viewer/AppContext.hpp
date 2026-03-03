#pragma once
/// @file AppContext.hpp
/// @brief Shared mutable state owned by App and read/written by all panels.
///
/// Passed by reference into every panel's Draw() call so panels can
/// communicate (e.g. the timeline sets PlayheadNs and the inspector reads it).

#include <recplay/recplay.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace viewer {

/// A cached, decoded message kept in memory for the inspector panel.
struct CachedMessage {
    recplay::ChannelId Channel     = recplay::INVALID_CHANNEL_ID;
    recplay::Timestamp TimestampNs = 0;
    std::vector<uint8_t> Payload;
};

/// Per-channel UI state (visibility toggle, colour).
struct ChannelUIState {
    recplay::ChannelId Id      = recplay::INVALID_CHANNEL_ID;
    bool               Visible = true;
    float              Color[4]{ 1.f, 1.f, 1.f, 1.f };
};

/// Central state shared among all panels.
struct AppContext {
    // ------------------------------------------------------------------
    // Session
    // ------------------------------------------------------------------

    /// Loaded session reader (nullptr when no session is open).
    std::unique_ptr<recplay::ReaderSession> Reader;

    /// Copy of the manifest (valid when Reader != nullptr).
    recplay::SessionManifest Manifest;

    /// Path passed to SessionLoader::Open().
    std::string SessionPath;

    bool HasSession() const noexcept { return Reader != nullptr; }

    // ------------------------------------------------------------------
    // Channel UI state
    // ------------------------------------------------------------------

    std::vector<ChannelUIState> ChannelStates;

    /// Returns the ChannelUIState for \p id, or nullptr if not found.
    ChannelUIState* FindChannelState(recplay::ChannelId id) noexcept {
        for (auto& s : ChannelStates)
            if (s.Id == id) return &s;
        return nullptr;
    }

    // ------------------------------------------------------------------
    // Playhead / selection
    // ------------------------------------------------------------------

    /// Current playhead position in the timeline (nanoseconds, absolute).
    recplay::Timestamp PlayheadNs = 0;

    /// Channel to show in the inspector.
    /// INVALID_CHANNEL_ID = "any channel" (returns the first message found at
    /// PlayheadNs regardless of channel).
    recplay::ChannelId InspectedChannelId = recplay::INVALID_CHANNEL_ID;

    /// Message currently selected / shown in the inspector.
    std::optional<CachedMessage> SelectedMessage;

    /// Seek to PlayheadNs and fetch the first message for InspectedChannelId
    /// (or any channel when InspectedChannelId == INVALID_CHANNEL_ID).
    /// Stores the result in SelectedMessage.
    void FetchAtPlayhead()
    {
        if (!HasSession()) return;
        if (InspectedChannelId != recplay::INVALID_CHANNEL_ID) {
            // Use filtered read so we get the right channel even in a mixed file.
            recplay::ReadOptions opts;
            opts.ChannelFilter = { InspectedChannelId };
            opts.StartNs       = PlayheadNs;
            Reader->Read(opts, [this](const recplay::MessageView& mv) -> bool {
                CachedMessage cm;
                cm.Channel     = mv.Channel;
                cm.TimestampNs = mv.TimestampNs;
                cm.Payload.assign(static_cast<const uint8_t*>(mv.Data),
                                  static_cast<const uint8_t*>(mv.Data) + mv.Length);
                SelectedMessage = std::move(cm);
                return false; // stop after the first match
            });
        } else {
            recplay::MessageView mv;
            Reader->Seek(PlayheadNs);
            if (Reader->ReadNext(mv) && mv.IsValid()) {
                CachedMessage cm;
                cm.Channel     = mv.Channel;
                cm.TimestampNs = mv.TimestampNs;
                cm.Payload.assign(static_cast<const uint8_t*>(mv.Data),
                                  static_cast<const uint8_t*>(mv.Data) + mv.Length);
                SelectedMessage = std::move(cm);
            }
        }
    }

    // ------------------------------------------------------------------
    // UI flags
    // ------------------------------------------------------------------

    /// Set to true by menus / shortcuts to trigger session open dialog.
    bool RequestOpenSession = false;

    /// Set to true by menus / shortcuts to close the current session.
    bool RequestCloseSession = false;
};

} // namespace viewer
