#pragma once
/// @file panels/AnnotationsPanel.hpp
/// @brief Displays all session annotations in a sortable table.

#include "IPanel.hpp"

namespace viewer::panels {

class AnnotationsPanel final : public IPanel {
public:
    void        Draw(AppContext& ctx) override;
    const char* Name() const noexcept override { return "Annotations"; }
};

} // namespace viewer::panels
