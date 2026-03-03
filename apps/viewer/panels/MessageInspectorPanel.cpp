/// @file panels/MessageInspectorPanel.cpp
#include "MessageInspectorPanel.hpp"
#include "../AppContext.hpp"
#include "../util/FormatUtil.hpp"
#include "../util/SchemaInspector.hpp"

#include <imgui.h>
#include <cstdio>
#include <cmath>
#include <string>

namespace viewer::panels {

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

bool MessageInspectorPanel::IsVideoMessage(const AppContext& ctx) const
{
    if (!ctx.SelectedMessage.has_value() || !ctx.HasSession()) return false;
    const auto* def = ctx.Reader->FindChannel(ctx.SelectedMessage->Channel);
    return def && def->Schema.rfind("rawvideo:", 0) == 0;
}

void MessageInspectorPanel::RefreshVideoTexture(const AppContext& ctx)
{
    if (!ctx.SelectedMessage.has_value()) return;
    const auto& msg = *ctx.SelectedMessage;

    // Skip if same frame already uploaded
    if (msg.TimestampNs == m_LastVideoTimestampNs &&
        msg.Channel     == m_LastVideoChannelId)
        return;

    // Parse dimensions from schema
    if (!ctx.HasSession()) return;
    const auto* def = ctx.Reader->FindChannel(msg.Channel);
    if (!def) return;

    char pix_fmt[32]{};
    int w = 0, h = 0, fps = 0;
    if (std::sscanf(def->Schema.c_str(),
                    "rawvideo:%31[^:]:%dx%d:%dfps",
                    pix_fmt, &w, &h, &fps) != 4)
        return;

    const size_t expected = static_cast<size_t>(w) * h * 3;
    if (msg.Payload.size() != expected) return;

    m_VideoTex.Upload(msg.Payload.data(), w, h);
    m_LastVideoTimestampNs = msg.TimestampNs;
    m_LastVideoChannelId   = msg.Channel;
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void MessageInspectorPanel::Draw(AppContext& ctx)
{
    ImGui::Begin(Name());

    // -- Channel selector (always visible when a session is open)
    if (ctx.HasSession()) {
        const auto& channels = ctx.Manifest.Channels;
        int current = 0; // 0 = "(any)"
        for (int i = 0; i < static_cast<int>(channels.size()); ++i) {
            if (channels[i].Id == ctx.InspectedChannelId) {
                current = i + 1;
                break;
            }
        }
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::BeginCombo("##ch_sel",
            current == 0 ? "(any channel)" : channels[current - 1].Name.c_str()))
        {
            if (ImGui::Selectable("(any channel)", current == 0)) {
                ctx.InspectedChannelId = recplay::INVALID_CHANNEL_ID;
                ctx.FetchAtPlayhead();
            }
            if (current == 0) ImGui::SetItemDefaultFocus();

            for (int i = 0; i < static_cast<int>(channels.size()); ++i) {
                ImGui::PushID(i);
                const bool sel = (current == i + 1);
                char label[128];
                std::snprintf(label, sizeof(label), "[%u] %s",
                              channels[i].Id, channels[i].Name.c_str());
                if (ImGui::Selectable(label, sel)) {
                    ctx.InspectedChannelId = channels[i].Id;
                    ctx.FetchAtPlayhead();
                }
                if (sel) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Select which channel to inspect");

        ImGui::Separator();
    }

    if (!ctx.SelectedMessage.has_value()) {
        ImGui::TextDisabled("No message selected — drag the timeline playhead.");
        ImGui::End();
        return;
    }

    const auto& msg = *ctx.SelectedMessage;

    // Resolve channel metadata
    const char* ch_name = "(unknown)";
    const char* schema  = "";
    if (ctx.HasSession()) {
        if (const auto* def = ctx.Reader->FindChannel(msg.Channel)) {
            ch_name = def->Name.c_str();
            schema  = def->Schema.c_str();
        }
    }

    // -- Header
    ImGui::SeparatorText("Message");
    ImGui::LabelText("Channel",   "[%u] %s", msg.Channel, ch_name);
    ImGui::LabelText("Timestamp", "%s  (%llu ns)",
                     util::FormatTimestampHMS(msg.TimestampNs).c_str(),
                     (unsigned long long)msg.TimestampNs);
    ImGui::LabelText("Size",      "%s", util::FormatBytes(msg.Payload.size()).c_str());
    if (*schema)
        ImGui::LabelText("Schema", "%s", schema);

    ImGui::Spacing();
    ImGui::SeparatorText("Payload");

    // -- Tab bar
    const bool has_video = IsVideoMessage(ctx);

    if (ImGui::BeginTabBar("##payload_tabs")) {

        // ----------------------------------------------------------------
        // FIELDS tab
        // ----------------------------------------------------------------
        if (ImGui::BeginTabItem("Fields")) {
            const auto fields = util::InspectSchema(
                schema,
                msg.Payload.data(),
                msg.Payload.size());

            constexpr ImGuiTableFlags tflags =
                ImGuiTableFlags_Borders     |
                ImGuiTableFlags_RowBg       |
                ImGuiTableFlags_SizingStretchProp;

            if (ImGui::BeginTable("##fields", 2, tflags)) {
                ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 180.f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (const auto& f : fields) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%s", f.Key.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(f.Value.c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        // ----------------------------------------------------------------
        // VIDEO tab (rawvideo channels only)
        // ----------------------------------------------------------------
        if (has_video) {
            if (ImGui::BeginTabItem("Video")) {
                RefreshVideoTexture(ctx);

                if (m_VideoTex.IsValid()) {
                    const ImVec2 avail = ImGui::GetContentRegionAvail();
                    const ImVec2 size  = m_VideoTex.FitSize(
                        avail.x, std::max(avail.y - 4.f, 64.f));

                    // Centre the image horizontally
                    const float pad = (avail.x - size.x) * 0.5f;
                    if (pad > 0.f)
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);

                    ImGui::Image(m_VideoTex.TextureId(), size,
                                 ImVec2(0, 0), ImVec2(1, 1));

                    if (ImGui::IsItemHovered()) {
                        // Show pixel coordinates + RGB under cursor
                        const ImVec2 img_pos = ImGui::GetItemRectMin();
                        const ImVec2 mouse   = ImGui::GetMousePos();
                        const float  rx = (mouse.x - img_pos.x) / size.x;
                        const float  ry = (mouse.y - img_pos.y) / size.y;

                        const int px = static_cast<int>(rx * m_VideoTex.Width());
                        const int py = static_cast<int>(ry * m_VideoTex.Height());

                        if (px >= 0 && px < m_VideoTex.Width() &&
                            py >= 0 && py < m_VideoTex.Height())
                        {
                            const size_t idx =
                                (static_cast<size_t>(py) * m_VideoTex.Width() + px) * 3;
                            if (idx + 2 < msg.Payload.size()) {
                                const uint8_t r = msg.Payload[idx + 0];
                                const uint8_t g = msg.Payload[idx + 1];
                                const uint8_t b = msg.Payload[idx + 2];
                                ImGui::SetTooltip(
                                    "(%d, %d)  R:%u  G:%u  B:%u",
                                    px, py, r, g, b);
                            }
                        }
                    }

                    char info[64];
                    std::snprintf(info, sizeof(info),
                                  "%d × %d px",
                                  m_VideoTex.Width(), m_VideoTex.Height());
                    ImGui::TextDisabled("%s", info);
                } else {
                    ImGui::TextDisabled("Texture upload failed.");
                }
                ImGui::EndTabItem();
            }
        }

        // ----------------------------------------------------------------
        // HEX tab
        // ----------------------------------------------------------------
        if (ImGui::BeginTabItem("Hex")) {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1
                            ? ImGui::GetIO().Fonts->Fonts[1]
                            : nullptr);
            m_HexView.Draw("##hex", msg.Payload.data(), msg.Payload.size());
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        // ----------------------------------------------------------------
        // TEXT tab
        // ----------------------------------------------------------------
        if (ImGui::BeginTabItem("Text")) {
            std::string text;
            text.reserve(msg.Payload.size());
            for (uint8_t byt : msg.Payload)
                text += (byt >= 0x20 && byt < 0x7F) ? static_cast<char>(byt) : '.';

            ImGui::InputTextMultiline(
                "##text",
                text.data(),
                text.size() + 1,
                ImVec2(-1.f, -1.f),
                ImGuiInputTextFlags_ReadOnly);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace viewer::panels
