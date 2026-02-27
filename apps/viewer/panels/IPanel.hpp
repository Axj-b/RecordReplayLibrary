#pragma once
/// @file panels/IPanel.hpp
/// @brief Abstract interface that every viewer panel must implement.

namespace viewer {
struct AppContext;
} // namespace viewer

namespace viewer::panels {

/// Base class for all viewer panels.
/// Each panel is responsible for rendering one ImGui window / child region.
class IPanel {
public:
    virtual ~IPanel() = default;

    /// Draw the panel for the current frame.
    /// Called once per frame inside the main render loop.
    /// @param ctx  Shared application context (may be modified by the panel).
    virtual void Draw(AppContext& ctx) = 0;

    /// Human-readable name used for window titles / docking labels.
    virtual const char* Name() const noexcept = 0;
};

} // namespace viewer::panels
