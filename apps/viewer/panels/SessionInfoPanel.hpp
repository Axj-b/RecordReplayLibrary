#pragma once
/// @file panels/SessionInfoPanel.hpp
/// @brief Displays session manifest metadata (ID, segments, duration, version).

#include "IPanel.hpp"

namespace viewer::panels {

class SessionInfoPanel final : public IPanel {
public:
    void        Draw(AppContext& ctx) override;
    const char* Name() const noexcept override { return "Session Info"; }
};

} // namespace viewer::panels
