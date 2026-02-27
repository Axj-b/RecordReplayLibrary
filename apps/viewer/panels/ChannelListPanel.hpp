#pragma once
/// @file panels/ChannelListPanel.hpp
/// @brief Lists all channels in the open session with visibility toggles.

#include "IPanel.hpp"

namespace viewer::panels {

class ChannelListPanel final : public IPanel {
public:
    void        Draw(AppContext& ctx) override;
    const char* Name() const noexcept override { return "Channels"; }
};

} // namespace viewer::panels
