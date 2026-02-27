/// @file session.cpp
/// @brief SessionManifest accessor implementations.

// local includes
#include <recplay/session.hpp>

// system includes
#include <algorithm>

namespace recplay {

uint64_t SessionManifest::TotalSizeBytes() const noexcept {
    uint64_t total = 0;
    for (const auto& s : Segments) total += s.SizeBytes;
    return total;
}

Timestamp SessionManifest::StartNs() const noexcept {
    if (Segments.empty()) return 0;
    Timestamp t = Segments.front().StartNs;
    for (const auto& s : Segments) t = std::min(t, s.StartNs);
    return t;
}

Timestamp SessionManifest::EndNs() const noexcept {
    Timestamp t = 0;
    for (const auto& s : Segments) t = std::max(t, s.EndNs);
    return t;
}

uint64_t SessionManifest::DurationNs() const noexcept {
    const Timestamp s = StartNs();
    const Timestamp e = EndNs();
    return (e > s) ? (e - s) : 0u;
}

const ChannelDef* SessionManifest::FindChannel(const std::string& name) const noexcept {
    for (const auto& ch : Channels)
        if (ch.Name == name) return &ch;
    return nullptr;
}

const ChannelDef* SessionManifest::FindChannel(ChannelId id) const noexcept {
    for (const auto& ch : Channels)
        if (ch.Id == id) return &ch;
    return nullptr;
}

} // namespace recplay
