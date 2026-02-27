#pragma once
/// @file util/HexView.hpp
/// @brief Self-contained ImGui hex-dump widget.
///
/// Usage:
///   viewer::util::HexView hex;
///   hex.Draw("##hex", data_ptr, data_len);

#include <imgui.h>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace viewer::util {

class HexView {
public:
    int  Columns          = 16;   ///< Bytes per row
    bool ShowAscii        = true; ///< Show ASCII sidebar
    bool ShowAddresses    = true; ///< Show row addresses

    /// Draw the hex-dump inside the current ImGui window.
    /// @param label   ImGui widget ID (must start with "##" or be unique).
    /// @param data    Pointer to raw bytes.
    /// @param size    Number of bytes to display.
    void Draw(const char* label, const void* data, size_t size)
    {
        const auto* bytes = static_cast<const uint8_t*>(data);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));

        const float char_w = ImGui::CalcTextSize("F").x;
        const int   rows   = static_cast<int>((size + Columns - 1) / Columns);

        ImGuiListClipper clipper;
        clipper.Begin(rows);

        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const size_t row_start = static_cast<size_t>(row) * Columns;
                const size_t row_end   = std::min(row_start + Columns, size);

                // Address
                if (ShowAddresses) {
                    ImGui::TextDisabled("%08zX  ", row_start);
                    ImGui::SameLine();
                }

                // Hex bytes
                for (size_t i = row_start; i < row_end; ++i) {
                    if ((i - row_start) == static_cast<size_t>(Columns / 2))
                        ImGui::SameLine(0.f, char_w); // mid-row gap

                    // Highlight selected byte on hover (simple version)
                    ImGui::Text("%02X ", bytes[i]);
                    ImGui::SameLine(0.f, 0.f);
                }

                // Padding for incomplete last row
                if (ShowAscii) {
                    for (size_t i = row_end; i < row_start + Columns; ++i) {
                        ImGui::Text("   ");
                        ImGui::SameLine(0.f, 0.f);
                    }

                    ImGui::SameLine(0.f, char_w * 2.f);

                    // ASCII
                    for (size_t i = row_start; i < row_end; ++i) {
                        char c = static_cast<char>(bytes[i]);
                        if (c < 0x20 || c > 0x7E) c = '.';
                        ImGui::Text("%c", c);
                        ImGui::SameLine(0.f, 0.f);
                    }
                }

                ImGui::NewLine();
            }
        }

        clipper.End();
        ImGui::PopStyleVar();

        (void)label; // used as ImGui scope guard upstream if needed
    }
};

} // namespace viewer::util
