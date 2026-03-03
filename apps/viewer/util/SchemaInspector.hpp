#pragma once
/// @file util/SchemaInspector.hpp
/// @brief Produces a list of human-readable key/value fields for a message,
///        based on the channel's schema tag.
///
/// Supported schemas (same format as record_ffmpeg example):
///   rawvideo:rgb24:WxH:FPSfps
///   pcm:s16le:RATE:NCHch
///
/// Unknown schemas fall back to generic byte-level statistics.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace viewer::util {

struct SchemaField {
    std::string Key;
    std::string Value;
};

inline std::vector<SchemaField> InspectSchema(
    const std::string& schema,
    const void*        data,
    size_t             len)
{
    std::vector<SchemaField> fields;
    const auto* bytes = static_cast<const uint8_t*>(data);

    auto addField = [&](const char* key, std::string val) {
        fields.push_back({ key, std::move(val) });
    };

    char buf[64]{};

    // ------------------------------------------------------------------
    // rawvideo:rgb24:WxH:FPSfps
    // ------------------------------------------------------------------
    if (schema.rfind("rawvideo:", 0) == 0) {
        char pix_fmt[32]{};
        int w = 0, h = 0, fps = 0;
        if (std::sscanf(schema.c_str(),
                        "rawvideo:%31[^:]:%dx%d:%dfps",
                        pix_fmt, &w, &h, &fps) == 4) {
            addField("Type",        "Video frame");
            addField("Pixel format", pix_fmt);
            std::snprintf(buf, sizeof(buf), "%d × %d", w, h);
            addField("Resolution",  buf);
            std::snprintf(buf, sizeof(buf), "%d fps", fps);
            addField("Frame rate",  buf);
            const size_t expected = static_cast<size_t>(w) * h * 3;
            std::snprintf(buf, sizeof(buf), "%zu B  (expected %zu B)", len, expected);
            addField("Payload size", buf);
            if (len == expected) {
                // Compute simple stats: mean luminance from first 4 KB sample
                uint64_t sum = 0;
                const size_t sample = std::min(len, size_t(4096));
                for (size_t i = 0; i < sample; ++i) sum += bytes[i];
                std::snprintf(buf, sizeof(buf), "%.1f / 255",
                              static_cast<double>(sum) / sample);
                addField("Mean brightness (sample)", buf);
            }
        } else {
            addField("Schema", schema);
        }
        return fields;
    }

    // ------------------------------------------------------------------
    // pcm:s16le:RATE:NCHch
    // ------------------------------------------------------------------
    if (schema.rfind("pcm:", 0) == 0) {
        char fmt[32]{};
        int rate = 0, ch = 0;
        if (std::sscanf(schema.c_str(),
                        "pcm:%31[^:]:%d:%dch",
                        fmt, &rate, &ch) == 3) {
            addField("Type",        "PCM audio chunk");
            addField("Format",      fmt);
            std::snprintf(buf, sizeof(buf), "%d Hz", rate);
            addField("Sample rate", buf);
            std::snprintf(buf, sizeof(buf), "%d", ch);
            addField("Channels",    buf);
            const int bps = 2; // s16le
            const size_t n_samples = len / (bps * ch);
            const double dur_ms    = static_cast<double>(n_samples) * 1000.0 / rate;
            std::snprintf(buf, sizeof(buf), "%zu  (%.2f ms)", n_samples, dur_ms);
            addField("Samples",      buf);
            std::snprintf(buf, sizeof(buf), "%zu B", len);
            addField("Payload size", buf);
            // Peak amplitude
            uint16_t peak = 0;
            const auto* s16 = reinterpret_cast<const int16_t*>(data);
            const size_t ns = len / 2;
            for (size_t i = 0; i < ns; ++i) {
                const auto v = static_cast<uint16_t>(s16[i] < 0 ? -s16[i] : s16[i]);
                if (v > peak) peak = v;
            }
            std::snprintf(buf, sizeof(buf), "%u  (%.1f dBFS)",
                          peak,
                          peak > 0 ? 20.0 * std::log10(peak / 32767.0) : -999.0);
            addField("Peak amplitude", buf);
        } else {
            addField("Schema", schema);
        }
        return fields;
    }

    // ------------------------------------------------------------------
    // Unknown schema — generic byte stats
    // ------------------------------------------------------------------
    if (!schema.empty())
        addField("Schema", schema);
    else
        addField("Schema", "(none)");

    std::snprintf(buf, sizeof(buf), "%zu B", len);
    addField("Size", buf);

    if (len > 0) {
        // Byte histogram: count distinct values
        uint32_t hist[256]{};
        for (size_t i = 0; i < len; ++i) ++hist[bytes[i]];
        int distinct = 0;
        for (int i = 0; i < 256; ++i) if (hist[i]) ++distinct;
        std::snprintf(buf, sizeof(buf), "%d / 256", distinct);
        addField("Distinct byte values", buf);

        uint64_t sum = 0;
        for (size_t i = 0; i < len; ++i) sum += bytes[i];
        std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(sum) / len);
        addField("Mean byte value", buf);

        std::snprintf(buf, sizeof(buf),
                      "0x%02X 0x%02X 0x%02X 0x%02X ...",
                      bytes[0],
                      len > 1 ? bytes[1] : 0,
                      len > 2 ? bytes[2] : 0,
                      len > 3 ? bytes[3] : 0);
        addField("First bytes", buf);
    }

    return fields;
}

} // namespace viewer::util
