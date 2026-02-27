/// @file panels/AnnotationsPanel.cpp
#include "AnnotationsPanel.hpp"
#include "../AppContext.hpp"
#include "../util/FormatUtil.hpp"

#include <imgui.h>
#include <vector>

namespace viewer::panels {

void AnnotationsPanel::Draw(AppContext& ctx)
{
    ImGui::Begin(Name());

    if (!ctx.HasSession()) {
        ImGui::TextDisabled("No session open.");
        ImGui::End();
        return;
    }

    const auto annotations = ctx.Reader->Annotations();

    if (annotations.empty()) {
        ImGui::TextDisabled("No annotations in this session.");
        ImGui::End();
        return;
    }

    ImGui::Text("%zu annotation(s)", annotations.size());
    ImGui::Spacing();

    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_Borders       |
        ImGuiTableFlags_RowBg         |
        ImGuiTableFlags_ScrollY       |
        ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_Sortable;

    if (ImGui::BeginTable("annotations", 3, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Timestamp", ImGuiTableColumnFlags_WidthFixed, 140.f);
        ImGui::TableSetupColumn("Label",     ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Metadata",  ImGuiTableColumnFlags_WidthFixed, 80.f);
        ImGui::TableHeadersRow();

        for (const auto& ann : annotations) {
            ImGui::TableNextRow();

            // -- Timestamp  (click to jump playhead)
            ImGui::TableSetColumnIndex(0);
            const std::string ts_str = util::FormatTimestampHMS(ann.TimestampNs);
            bool clicked = ImGui::Selectable(ts_str.c_str(), false,
                                             ImGuiSelectableFlags_SpanAllColumns);
            if (clicked)
                ctx.PlayheadNs = ann.TimestampNs;

            // -- Label
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(ann.Label.c_str());

            // -- Metadata size
            ImGui::TableSetColumnIndex(2);
            if (ann.Metadata.empty())
                ImGui::TextDisabled("—");
            else
                ImGui::Text("%zu B", ann.Metadata.size());
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace viewer::panels
