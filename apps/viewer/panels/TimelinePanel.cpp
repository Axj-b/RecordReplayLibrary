/// @file panels/TimelinePanel.cpp
#include "TimelinePanel.hpp"
#include "../AppContext.hpp"
#include "../util/FormatUtil.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace viewer::panels {

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

// Round ns up to a "nice" grid interval (returns ns)
static uint64_t NiceInterval(double range_ns)
{
    // Candidate intervals in ns: 1ms, 5ms, 10ms, 50ms, 100ms, 500ms,
    //                            1s, 5s, 10s, 30s, 60s, 300s ...
    static const double kCandidates[] = {
        1e6, 5e6, 10e6, 50e6, 100e6, 500e6,
        1e9, 5e9, 10e9, 30e9, 60e9, 300e9, 600e9, 3600e9
    };
    constexpr int kMaxLabels = 10;
    for (double c : kCandidates) {
        if (range_ns / c <= kMaxLabels)
            return static_cast<uint64_t>(c);
    }
    return static_cast<uint64_t>(3600e9); // 1 hour
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void TimelinePanel::Draw(AppContext& ctx)
{
    ImGui::Begin(Name());

    if (!ctx.HasSession()) {
        ImGui::TextDisabled("No session open.");
        ImGui::End();
        return;
    }

    const uint64_t start_ns = ctx.Manifest.StartNs();
    const uint64_t end_ns   = ctx.Manifest.EndNs();
    const uint64_t dur_ns   = ctx.Manifest.DurationNs();

    if (dur_ns == 0) {
        ImGui::TextDisabled("Empty session.");
        ImGui::End();
        return;
    }

    // -- top row: time labels
    ImGui::Text("Start: %s", util::FormatTimestampHMS(start_ns).c_str());
    ImGui::SameLine();
    ImGui::Text("  Duration: %s", util::FormatDurationNs(dur_ns).c_str());
    ImGui::SameLine();
    ImGui::Text("  Playhead: %s",
        util::FormatTimestampHMS(ctx.PlayheadNs).c_str());

    // -- timeline track area
    const float  avail_w = ImGui::GetContentRegionAvail().x;
    const float  track_h = 48.f;
    const ImVec2 track_pos  = ImGui::GetCursorScreenPos();
    const ImVec2 track_size = ImVec2(avail_w, track_h);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background
    dl->AddRectFilled(track_pos,
                      ImVec2(track_pos.x + track_size.x, track_pos.y + track_size.y),
                      IM_COL32(30, 30, 30, 255));

    // Grid lines + labels
    const uint64_t interval_ns = NiceInterval(static_cast<double>(dur_ns));
    const uint64_t first_tick  = (start_ns / interval_ns + 1) * interval_ns;
    for (uint64_t t = first_tick; t < end_ns; t += interval_ns) {
        const float x = track_pos.x +
            avail_w * static_cast<float>(t - start_ns) / static_cast<float>(dur_ns);
        dl->AddLine(ImVec2(x, track_pos.y),
                    ImVec2(x, track_pos.y + track_size.y),
                    IM_COL32(80, 80, 80, 255), 1.f);
        const std::string lbl = util::FormatTimestampHMS(t);
        dl->AddText(ImVec2(x + 2.f, track_pos.y + 2.f),
                    IM_COL32(160, 160, 160, 255), lbl.c_str());
    }

    // Segment markers
    for (const auto& seg : ctx.Manifest.Segments) {
        if (seg.SegmentIndex == 0) continue;
        const float x = track_pos.x +
            avail_w * static_cast<float>(seg.StartNs - start_ns) / static_cast<float>(dur_ns);
        dl->AddLine(ImVec2(x, track_pos.y),
                    ImVec2(x, track_pos.y + track_size.y),
                    IM_COL32(255, 200, 50, 160), 1.5f);
    }

    // Annotation markers
    for (const auto& ann : ctx.Reader->Annotations()) {
        if (ann.TimestampNs < start_ns || ann.TimestampNs > end_ns) continue;
        const float x = track_pos.x +
            avail_w * static_cast<float>(ann.TimestampNs - start_ns) / static_cast<float>(dur_ns);
        dl->AddLine(ImVec2(x, track_pos.y),
                    ImVec2(x, track_pos.y + track_size.y),
                    IM_COL32(50, 255, 100, 200), 1.f);
        dl->AddText(ImVec2(x + 2.f, track_pos.y + track_size.y - 14.f),
                    IM_COL32(50, 255, 100, 200), ann.Label.c_str());
    }

    // Playhead line
    const float ph_x = track_pos.x +
        avail_w * static_cast<float>(ctx.PlayheadNs - start_ns) / static_cast<float>(dur_ns);
    dl->AddLine(ImVec2(ph_x, track_pos.y),
                ImVec2(ph_x, track_pos.y + track_size.y),
                IM_COL32(255, 80, 80, 255), 2.f);
    // Small triangle handle
    dl->AddTriangleFilled(
        ImVec2(ph_x,       track_pos.y),
        ImVec2(ph_x - 6.f, track_pos.y + 10.f),
        ImVec2(ph_x + 6.f, track_pos.y + 10.f),
        IM_COL32(255, 80, 80, 255));

    // Invisible button to capture mouse interaction
    ImGui::InvisibleButton("##timeline", track_size);

    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float mouse_x = ImGui::GetMousePos().x;
        float t = (mouse_x - track_pos.x) / avail_w;
        t = std::clamp(t, 0.f, 1.f);
        const uint64_t new_ph = start_ns + static_cast<uint64_t>(t * static_cast<float>(dur_ns));
        if (new_ph != ctx.PlayheadNs) {
            ctx.PlayheadNs = new_ph;
            FetchMessageAtPlayhead(ctx);
        }
    }

    // Tooltip on hover
    if (ImGui::IsItemHovered()) {
        const float mouse_x = ImGui::GetMousePos().x;
        float t = (mouse_x - track_pos.x) / avail_w;
        t = std::clamp(t, 0.f, 1.f);
        const uint64_t hover_ns = start_ns + static_cast<uint64_t>(t * static_cast<float>(dur_ns));
        ImGui::SetTooltip("%s", util::FormatTimestampHMS(hover_ns).c_str());
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// FetchMessageAtPlayhead
// ---------------------------------------------------------------------------

void TimelinePanel::FetchMessageAtPlayhead(AppContext& ctx)
{
    ctx.FetchAtPlayhead();
}

} // namespace viewer::panels
