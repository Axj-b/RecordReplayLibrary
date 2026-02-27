/// @file panels/SessionInfoPanel.cpp
#include "SessionInfoPanel.hpp"
#include "../AppContext.hpp"
#include "../util/FormatUtil.hpp"

#include <imgui.h>
#include <cstdio>
#include <ctime>

namespace viewer::panels {

void SessionInfoPanel::Draw(AppContext& ctx)
{
    ImGui::Begin(Name());

    if (!ctx.HasSession()) {
        ImGui::TextDisabled("No session open.");
        ImGui::End();
        return;
    }

    const auto& m = ctx.Manifest;

    ImGui::SeparatorText("Identity");
    ImGui::LabelText("Session ID",       "%s", util::FormatSessionId(m.Id).c_str());
    ImGui::LabelText("Path",             "%s", ctx.SessionPath.c_str());
    ImGui::LabelText("Recorder version", "%s", m.RecorderVersion.c_str());

    const time_t wall_sec = static_cast<time_t>(m.CreatedAtNs / 1'000'000'000ull);
    char tbuf[64]{};
    std::strftime(tbuf, sizeof(tbuf), "%Y-%m-%d  %H:%M:%S UTC", std::gmtime(&wall_sec));
    ImGui::LabelText("Recorded at", "%s", tbuf);

    ImGui::SeparatorText("Time range");
    ImGui::LabelText("Start",    "%s", util::FormatTimestampHMS(m.StartNs()).c_str());
    ImGui::LabelText("End",      "%s", util::FormatTimestampHMS(m.EndNs()).c_str());
    ImGui::LabelText("Duration", "%s", util::FormatDurationNs(m.DurationNs()).c_str());

    ImGui::SeparatorText("Storage");
    ImGui::LabelText("Segments",   "%zu", m.Segments.size());
    ImGui::LabelText("Total size", "%s",  util::FormatBytes(m.TotalSizeBytes()).c_str());

    if (ImGui::TreeNode("Segments")) {
        for (const auto& seg : m.Segments) {
            char label[64];
            std::snprintf(label, sizeof(label), "[%u] %s",
                          seg.SegmentIndex, seg.Filename.c_str());
            if (ImGui::TreeNode(label)) {
                ImGui::LabelText("Start",  "%s", util::FormatTimestampHMS(seg.StartNs).c_str());
                ImGui::LabelText("End",    "%s", util::FormatTimestampHMS(seg.EndNs).c_str());
                ImGui::LabelText("Size",   "%s", util::FormatBytes(seg.SizeBytes).c_str());
                ImGui::TreePop();
            }
        }
        ImGui::TreePop();
    }

    ImGui::End();
}

} // namespace viewer::panels
