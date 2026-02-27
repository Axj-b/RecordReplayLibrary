#pragma once
/// @file panels/TimelinePanel.hpp
/// @brief Horizontal time ruler with a draggable playhead.

#include "IPanel.hpp"

namespace viewer::panels {

class TimelinePanel final : public IPanel {
public:
    void        Draw(AppContext& ctx) override;
    const char* Name() const noexcept override { return "Timeline"; }

private:
    /// Fetch and cache the message nearest to the new playhead position.
    void FetchMessageAtPlayhead(AppContext& ctx);
};

} // namespace viewer::panels
