#pragma once
/// @file util/FormatUtil.hpp
/// @brief Lightweight formatting helpers (no external dependencies).

#include <cstdint>
#include <cstdio>
#include <string>

namespace viewer::util {

/// Format an absolute nanosecond timestamp as "HH:MM:SS.mmm".
inline std::string FormatTimestampHMS(uint64_t ns)
{
    uint64_t total_ms = ns / 1'000'000ull;
    uint64_t ms  = total_ms % 1000;
    uint64_t sec = (total_ms / 1000) % 60;
    uint64_t min = (total_ms / 60'000) % 60;
    uint64_t hr  = (total_ms / 3'600'000);

    char buf[32];
    std::snprintf(buf, sizeof(buf),
                  "%02llu:%02llu:%02llu.%03llu",
                  (unsigned long long)hr,
                  (unsigned long long)min,
                  (unsigned long long)sec,
                  (unsigned long long)ms);
    return buf;
}

/// Format a duration given in nanoseconds as "X h Y min Z.mmm s".
inline std::string FormatDurationNs(uint64_t ns)
{
    if (ns == 0) return "0 ms";

    uint64_t ms  = ns / 1'000'000ull;
    uint64_t sec = ms  / 1000;
    uint64_t min = sec / 60;
    uint64_t hr  = min / 60;

    char buf[64];
    if (hr > 0)
        std::snprintf(buf, sizeof(buf), "%lluh %02llum %02llu.%03llus",
            (unsigned long long)hr,
            (unsigned long long)(min % 60),
            (unsigned long long)(sec % 60),
            (unsigned long long)(ms  % 1000));
    else if (min > 0)
        std::snprintf(buf, sizeof(buf), "%llum %02llu.%03llus",
            (unsigned long long)min,
            (unsigned long long)(sec % 60),
            (unsigned long long)(ms  % 1000));
    else
        std::snprintf(buf, sizeof(buf), "%llu.%03llus",
            (unsigned long long)(sec),
            (unsigned long long)(ms % 1000));
    return buf;
}

/// Format a byte count as a human-readable string ("1.23 MiB", etc.).
inline std::string FormatBytes(uint64_t bytes)
{
    constexpr double kib = 1024.0;
    constexpr double mib = 1024.0 * 1024.0;
    constexpr double gib = 1024.0 * 1024.0 * 1024.0;

    char buf[32];
    if (bytes >= static_cast<uint64_t>(gib))
        std::snprintf(buf, sizeof(buf), "%.2f GiB", bytes / gib);
    else if (bytes >= static_cast<uint64_t>(mib))
        std::snprintf(buf, sizeof(buf), "%.2f MiB", bytes / mib);
    else if (bytes >= static_cast<uint64_t>(kib))
        std::snprintf(buf, sizeof(buf), "%.1f KiB", bytes / kib);
    else
        std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
    return buf;
}

/// Format a session UUID stored as a 16-byte array into "xxxxxxxx-xxxx-…".
inline std::string FormatSessionId(const std::array<uint8_t, 16>& id)
{
    char buf[37];
    std::snprintf(buf, sizeof(buf),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        id[0],id[1],id[2],id[3], id[4],id[5], id[6],id[7],
        id[8],id[9], id[10],id[11],id[12],id[13],id[14],id[15]);
    return buf;
}

} // namespace viewer::util
