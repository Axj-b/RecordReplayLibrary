/// @file SessionLoader.cpp
#include "SessionLoader.hpp"
#include "AppContext.hpp"

#include <iostream>

// Stable preset colours cycled per-channel (RGBA, normalised)
static constexpr float kColors[][4] = {
    {0.40f, 0.80f, 1.00f, 1.f},
    {0.40f, 1.00f, 0.50f, 1.f},
    {1.00f, 0.75f, 0.20f, 1.f},
    {1.00f, 0.40f, 0.40f, 1.f},
    {0.80f, 0.40f, 1.00f, 1.f},
    {1.00f, 0.60f, 0.20f, 1.f},
};
static constexpr int kNumColors = sizeof(kColors) / sizeof(kColors[0]);

namespace viewer {

bool SessionLoader::Open(const std::string& path, AppContext& ctx)
{
    Close(ctx); // close any previously open session first

    auto reader = std::make_unique<recplay::ReaderSession>();
    const recplay::Status s = reader->Open(path);

    if (s != recplay::Status::Ok) {
        std::cerr << "[SessionLoader] Failed to open \"" << path
                  << "\" (status " << static_cast<int>(s) << ")\n";
        return false;
    }

    ctx.Manifest     = reader->Manifest();
    ctx.SessionPath  = path;
    ctx.PlayheadNs   = reader->StartNs();
    ctx.SelectedMessage.reset();

    // Build per-channel UI state
    ctx.ChannelStates.clear();
    ctx.ChannelStates.reserve(ctx.Manifest.Channels.size());
    int color_idx = 0;
    for (const auto& ch : ctx.Manifest.Channels) {
        ChannelUIState uis;
        uis.Id      = ch.Id;
        uis.Visible = true;
        const auto* col = kColors[color_idx++ % kNumColors];
        uis.Color[0] = col[0]; uis.Color[1] = col[1];
        uis.Color[2] = col[2]; uis.Color[3] = col[3];
        ctx.ChannelStates.push_back(uis);
    }

    ctx.Reader = std::move(reader);
    return true;
}

void SessionLoader::Close(AppContext& ctx)
{
    if (ctx.Reader)
        ctx.Reader->Close();
    ctx.Reader.reset();
    ctx.Manifest      = {};
    ctx.SessionPath.clear();
    ctx.ChannelStates.clear();
    ctx.PlayheadNs        = 0;
    ctx.InspectedChannelId = recplay::INVALID_CHANNEL_ID;
    ctx.SelectedMessage.reset();
}

} // namespace viewer
