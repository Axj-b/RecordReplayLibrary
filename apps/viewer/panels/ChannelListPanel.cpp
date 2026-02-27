/// @file panels/ChannelListPanel.cpp
#include "ChannelListPanel.hpp"
#include "../AppContext.hpp"

#include <imgui.h>

namespace viewer::panels {

// Stable list of preset colours cycled per-channel
static constexpr float kChannelColors[][4] = {
    {0.40f, 0.80f, 1.00f, 1.f},
    {0.40f, 1.00f, 0.50f, 1.f},
    {1.00f, 0.75f, 0.20f, 1.f},
    {1.00f, 0.40f, 0.40f, 1.f},
    {0.80f, 0.40f, 1.00f, 1.f},
    {1.00f, 0.60f, 0.20f, 1.f},
};
static constexpr int kNumColors = sizeof(kChannelColors) / sizeof(kChannelColors[0]);

static const char* CompressionName(recplay::CompressionCodec c)
{
    switch (c) {
        case recplay::CompressionCodec::None: return "None";
        case recplay::CompressionCodec::LZ4:  return "LZ4";
        case recplay::CompressionCodec::Zstd: return "Zstd";
    }
    return "?";
}

static const char* LayerName(recplay::CaptureLayer l)
{
    switch (l) {
        case recplay::CaptureLayer::L3L4: return "L3/L4";
        case recplay::CaptureLayer::L7:   return "L7";
    }
    return "?";
}

void ChannelListPanel::Draw(AppContext& ctx)
{
    ImGui::Begin(Name());

    if (!ctx.HasSession()) {
        ImGui::TextDisabled("No session open.");
        ImGui::End();
        return;
    }

    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_Borders       |
        ImGuiTableFlags_RowBg         |
        ImGuiTableFlags_ScrollY       |
        ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_Resizable;

    if (ImGui::BeginTable("channels", 6, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("",           ImGuiTableColumnFlags_WidthFixed, 20.f); // colour swatch
        ImGui::TableSetupColumn("Show",       ImGuiTableColumnFlags_WidthFixed, 38.f);
        ImGui::TableSetupColumn("Name",       ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Layer",      ImGuiTableColumnFlags_WidthFixed, 52.f);
        ImGui::TableSetupColumn("Codec",      ImGuiTableColumnFlags_WidthFixed, 50.f);
        ImGui::TableSetupColumn("Schema",     ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        const auto& channels = ctx.Manifest.Channels;
        for (int i = 0; i < static_cast<int>(channels.size()); ++i) {
            const auto& ch  = channels[i];
            auto*       uis = ctx.FindChannelState(ch.Id);

            ImGui::TableNextRow();

            // -- Colour swatch
            ImGui::TableSetColumnIndex(0);
            if (uis) {
                ImGui::ColorButton("##col", ImVec4(uis->Color[0], uis->Color[1],
                                                   uis->Color[2], uis->Color[3]),
                                   ImGuiColorEditFlags_NoTooltip |
                                   ImGuiColorEditFlags_NoPicker,
                                   ImVec2(14.f, 14.f));
            }

            // -- Visibility toggle
            ImGui::TableSetColumnIndex(1);
            if (uis) {
                ImGui::PushID(i);
                ImGui::Checkbox("##vis", &uis->Visible);
                ImGui::PopID();
            }

            // -- Name
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(ch.Name.c_str());

            // -- Layer
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(LayerName(ch.Layer));

            // -- Codec
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(CompressionName(ch.Compression));

            // -- Schema
            ImGui::TableSetColumnIndex(5);
            ImGui::TextDisabled("%s", ch.Schema.empty() ? "(none)" : ch.Schema.c_str());
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace viewer::panels
