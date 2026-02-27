#pragma once
/// @file panels/MessageInspectorPanel.hpp
/// @brief Shows payload bytes (hex dump) and schema info for the selected message.

#include "IPanel.hpp"
#include "../util/HexView.hpp"

namespace viewer::panels {

class MessageInspectorPanel final : public IPanel {
public:
    void        Draw(AppContext& ctx) override;
    const char* Name() const noexcept override { return "Message Inspector"; }

private:
    util::HexView m_HexView;
};

} // namespace viewer::panels
