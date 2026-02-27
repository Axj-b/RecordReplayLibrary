/// @file detail/index_builder.cpp
/// @brief IndexBuilder and SeekIndex implementation.

// local includes
#include "index_builder.hpp"
#include "platform.hpp"

// system includes
#include <algorithm>
#include <cstring>

namespace recplay {
namespace detail {

// ---------------------------------------------------------------------------
// IndexBuilder
// ---------------------------------------------------------------------------

IndexBuilder::IndexBuilder(uint64_t indexIntervalNs)
    : m_IndexIntervalNs(indexIntervalNs)
{}

void IndexBuilder::MaybeAdd(ChannelId channel, Timestamp timestamp, uint64_t offset) {
    if (channel >= m_LastIndexed.size())
        m_LastIndexed.resize(static_cast<size_t>(channel) + 1, 0);

    // Always index the very first record on a channel, then apply the interval.
    const bool firstOnChannel =
        (m_LastIndexed[channel] == 0 &&
         std::none_of(m_Points.begin(), m_Points.end(),
                      [channel](const SeekPoint& p) { return p.Channel == channel; }));

    if (firstOnChannel || timestamp - m_LastIndexed[channel] >= m_IndexIntervalNs)
        Add(channel, timestamp, offset);
}

void IndexBuilder::Add(ChannelId channel, Timestamp timestamp, uint64_t offset) {
    if (channel >= m_LastIndexed.size())
        m_LastIndexed.resize(static_cast<size_t>(channel) + 1, 0);

    m_Points.push_back({ channel, timestamp, offset });
    m_LastIndexed[channel] = timestamp;
}

std::vector<uint8_t> IndexBuilder::Serialise() const {
    using namespace format;

    const auto count = static_cast<uint32_t>(m_Points.size());
    std::vector<uint8_t> buf(sizeof(IndexHeader) +
                             static_cast<size_t>(count) * sizeof(IndexEntry));

    // Write IndexHeader
    IndexHeader hdr{};
    hdr.EntryCount = HostToLe32(count);
    std::memcpy(buf.data(), &hdr, sizeof(hdr));

    // Write IndexEntry array — always little-endian on disk
    uint8_t* p = buf.data() + sizeof(IndexHeader);
    for (const SeekPoint& pt : m_Points) {
        IndexEntry e{};
        e.Channel     = HostToLe16(pt.Channel);
        e.TimestampNs = HostToLe64(pt.TimestampNs);
        e.FileOffset  = HostToLe64(pt.FileOffset);
        std::memcpy(p, &e, sizeof(e));
        p += sizeof(e);
    }
    return buf;
}

size_t IndexBuilder::Size() const noexcept { return m_Points.size(); }

void IndexBuilder::Reset() {
    m_Points.clear();
    m_LastIndexed.clear();
}

// ---------------------------------------------------------------------------
// SeekIndex
// ---------------------------------------------------------------------------

bool SeekIndex::Load(const void* payload, uint32_t payloadLength) {
    using namespace format;

    if (!payload || payloadLength < sizeof(IndexHeader))
        return false;

    const auto* p = static_cast<const uint8_t*>(payload);

    IndexHeader hdr{};
    std::memcpy(&hdr, p, sizeof(hdr));
    const uint32_t count = Le32ToHost(hdr.EntryCount);
    p += sizeof(IndexHeader);

    const uint64_t required = sizeof(IndexHeader) +
                              static_cast<uint64_t>(count) * sizeof(IndexEntry);
    if (payloadLength < required)
        return false;

    m_Points.clear();
    m_Points.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        IndexEntry e{};
        std::memcpy(&e, p, sizeof(e));
        p += sizeof(e);
        m_Points.push_back({
            Le16ToHost(e.Channel),
            Le64ToHost(e.TimestampNs),
            Le64ToHost(e.FileOffset)
        });
    }

    // Sort by (Channel, TimestampNs) so Find() binary search works correctly.
    std::sort(m_Points.begin(), m_Points.end(),
              [](const SeekPoint& a, const SeekPoint& b) {
                  if (a.Channel != b.Channel) return a.Channel < b.Channel;
                  return a.TimestampNs < b.TimestampNs;
              });
    return true;
}

uint64_t SeekIndex::Find(ChannelId channel, Timestamp targetNs) const noexcept {
    // Skip to the first entry for this channel using lower_bound.
    const SeekPoint key{ channel, 0, 0 };
    auto it = std::lower_bound(
        m_Points.begin(), m_Points.end(), key,
        [](const SeekPoint& a, const SeekPoint& b) {
            if (a.Channel != b.Channel) return a.Channel < b.Channel;
            return a.TimestampNs < b.TimestampNs;
        });

    // Walk forward while the channel matches; keep the last entry whose
    // TimestampNs <= targetNs — that is the best seek point to jump to.
    uint64_t result = NO_OFFSET;
    for (; it != m_Points.end() && it->Channel == channel; ++it) {
        if (it->TimestampNs <= targetNs)
            result = it->FileOffset;
        else
            break;  // entries are sorted ascending — no point continuing
    }
    return result;
}

std::vector<SeekPoint> SeekIndex::EntriesFor(ChannelId channel) const {
    std::vector<SeekPoint> result;
    for (const auto& p : m_Points)
        if (p.Channel == channel) result.push_back(p);
    return result;
}

}  // namespace detail
}  // namespace recplay
