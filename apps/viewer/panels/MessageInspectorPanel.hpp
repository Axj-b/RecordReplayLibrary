#pragma once
/// @file panels/MessageInspectorPanel.hpp
/// @brief Shows payload fields, hex dump, text view, and optional video preview
///        for the selected message.

#include "IPanel.hpp"
#include "../util/HexView.hpp"
#include "../util/VideoTexture.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace viewer::panels {

class MessageInspectorPanel final : public IPanel {
public:
    void        Draw(AppContext& ctx) override;
    const char* Name() const noexcept override { return "Message Inspector"; }

private:
    /// Refresh the GL texture when the selected message changes.
    void RefreshVideoTexture(const AppContext& ctx);

    /// Returns true when the current message comes from a rawvideo channel.
    bool IsVideoMessage(const AppContext& ctx) const;

    util::HexView      m_HexView;
    util::VideoTexture m_VideoTex;

    /// Timestamp of the message whose pixels are currently in m_VideoTex.
    /// Used to avoid re-uploading the same frame every frame.
    uint64_t m_LastVideoTimestampNs = 0;
    uint16_t m_LastVideoChannelId   = 0xFFFFu;
};

} // namespace viewer::panels
