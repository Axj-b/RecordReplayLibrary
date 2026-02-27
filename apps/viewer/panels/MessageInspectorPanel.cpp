/// @file panels/MessageInspectorPanel.cpp
#include "MessageInspectorPanel.hpp"
#include "../AppContext.hpp"
#include "../util/FormatUtil.hpp"

#include <imgui.h>

namespace viewer::panels {

void MessageInspectorPanel::Draw(AppContext& ctx)
{
    ImGui::Begin(Name());

    if (!ctx.SelectedMessage.has_value()) {
        ImGui::TextDisabled("No message selected — drag the timeline playhead.");
        ImGui::End();
        return;
    }

    const auto& msg = *ctx.SelectedMessage;

    // -- Header info
    ImGui::SeparatorText("Message");

    // Resolve channel name
    const char* ch_name = "(unknown)";
    const char* schema  = "";
    if (ctx.HasSession()) {
        if (const auto* def = ctx.Reader->FindChannel(msg.Channel)) {
            ch_name = def->Name.c_str();
            schema  = def->Schema.c_str();
        }
    }

    ImGui::LabelText("Channel",   "[%u] %s", msg.Channel, ch_name);
    ImGui::LabelText("Timestamp", "%s  (%llu ns)",
                     util::FormatTimestampHMS(msg.TimestampNs).c_str(),
                     (unsigned long long)msg.TimestampNs);
    ImGui::LabelText("Size",      "%s", util::FormatBytes(msg.Payload.size()).c_str());
    if (*schema)
        ImGui::LabelText("Schema", "%s", schema);

    ImGui::Spacing();
    ImGui::SeparatorText("Payload");

    // Tabs: Hex | Text
    if (ImGui::BeginTabBar("##payload_tabs")) {

        if (ImGui::BeginTabItem("Hex")) {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1
                            ? ImGui::GetIO().Fonts->Fonts[1] // mono font if loaded
                            : nullptr);
            m_HexView.Draw("##hex", msg.Payload.data(), msg.Payload.size());
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Text")) {
            // Print printable ASCII, substitute '.' for non-printable
            std::string text;
            text.reserve(msg.Payload.size());
            for (uint8_t b : msg.Payload)
                text += (b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.';

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
