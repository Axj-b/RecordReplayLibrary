/// @file detail/manifest_io.cpp
/// @brief SessionManifest JSON read/write.

#include "manifest_io.hpp"
#include "platform.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <filesystem>

namespace recplay {
namespace detail {
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Internal JSON helpers
// ---------------------------------------------------------------------------

static std::string JsonEscape(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
            case '"':  r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n";  break;
            case '\r': r += "\\r";  break;
            case '\t': r += "\\t";  break;
            default:   r += c;
        }
    }
    return r;
}

static std::string ToHex(const uint8_t* data, size_t len) {
    static const char HEX[] = "0123456789abcdef";
    std::string r;
    r.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        r += HEX[data[i] >> 4];
        r += HEX[data[i] & 0xFu];
    }
    return r;
}

static std::string ToHex(const std::vector<uint8_t>& v) {
    return ToHex(v.data(), v.size());
}

static std::vector<uint8_t> FromHex(const std::string& hex) {
    std::vector<uint8_t> r;
    r.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        auto nibble = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
            return 0;
        };
        r.push_back(static_cast<uint8_t>((nibble(hex[i]) << 4u) | nibble(hex[i + 1])));
    }
    return r;
}

// ------------------------------------------------------------------
// JSON value extraction
// ------------------------------------------------------------------

/// Returns the raw JSON string value (without surrounding quotes, unescaped)
/// for the first occurrence of "key" in the JSON object string.
static std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return {};
    ++pos; // skip opening quote
    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
            switch (json[pos]) {
                case '"':  result += '"';  break;
                case '\\': result += '\\'; break;
                case '/':  result += '/';  break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                default:   result += json[pos]; break;
            }
        } else {
            result += json[pos];
        }
        ++pos;
    }
    return result;
}

/// Returns the uint64 value for the first occurrence of "key" in the JSON.
static uint64_t ExtractJsonUint64(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return 0;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return 0;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r'))
        ++pos;
    if (pos >= json.size()) return 0;
    try { return std::stoull(json.c_str() + pos); }
    catch (...) { return 0; }
}

/// Returns the uint32 value for the first occurrence of "key".
static uint32_t ExtractJsonUint32(const std::string& json, const std::string& key) {
    return static_cast<uint32_t>(ExtractJsonUint64(json, key));
}

/// Returns the uint16 value for the first occurrence of "key".
static uint16_t ExtractJsonUint16(const std::string& json, const std::string& key) {
    return static_cast<uint16_t>(ExtractJsonUint64(json, key));
}

/// Extracts the JSON array for "key" and splits it into individual object strings.
static std::vector<std::string> ExtractJsonObjects(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return {};
    pos = json.find('[', pos + pattern.size());
    if (pos == std::string::npos) return {};

    // Walk to matching ']'
    int depth = 0;
    size_t endPos = pos;
    for (size_t i = pos; i < json.size(); ++i) {
        if (json[i] == '[' || json[i] == '{') ++depth;
        else if (json[i] == ']' || json[i] == '}') {
            --depth;
            if (json[i] == ']' && depth == 0) { endPos = i; break; }
        }
    }

    // Extract individual { ... } objects from the array substring
    std::string arr = json.substr(pos + 1, endPos - pos - 1);
    std::vector<std::string> result;
    int d = 0;
    size_t objStart = std::string::npos;
    for (size_t i = 0; i < arr.size(); ++i) {
        if (arr[i] == '{') {
            if (d == 0) objStart = i;
            ++d;
        } else if (arr[i] == '}') {
            --d;
            if (d == 0 && objStart != std::string::npos) {
                result.push_back(arr.substr(objStart, i - objStart + 1));
                objStart = std::string::npos;
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// WriteManifest
// ---------------------------------------------------------------------------

Status WriteManifest(const std::string& sessionDir, const SessionManifest& manifest) {
    const std::string path    = sessionDir + static_cast<char>(PATH_SEP) + "session.manifest";
    const std::string tmpPath = path + ".tmp";

    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"recorder_version\": \"" << JsonEscape(manifest.RecorderVersion) << "\",\n";
    oss << "  \"id\": \"" << ToHex(manifest.Id.data(), manifest.Id.size()) << "\",\n";
    oss << "  \"created_at_ns\": " << manifest.CreatedAtNs << ",\n";

    // Channels
    oss << "  \"channels\": [\n";
    for (size_t i = 0; i < manifest.Channels.size(); ++i) {
        const ChannelDef& ch = manifest.Channels[i];
        oss << "    {\n";
        oss << "      \"id\": "                << ch.Id << ",\n";
        oss << "      \"name\": \""            << JsonEscape(ch.Name) << "\",\n";
        oss << "      \"layer\": "            << static_cast<int>(ch.Layer) << ",\n";
        oss << "      \"compression\": "     << static_cast<int>(ch.Compression) << ",\n";
        oss << "      \"chunk_size_bytes\": " << ch.ChunkSizeBytes << ",\n";
        oss << "      \"chunk_flush_ms\": "   << ch.ChunkFlushIntervalMs << ",\n";
        oss << "      \"schema\": \""         << JsonEscape(ch.Schema) << "\",\n";
        oss << "      \"user_metadata_hex\": \"" << ToHex(ch.UserMetadata) << "\"\n";
        oss << "    }" << (i + 1 < manifest.Channels.size() ? "," : "") << "\n";
    }
    oss << "  ],\n";

    // Segments
    oss << "  \"segments\": [\n";
    for (size_t i = 0; i < manifest.Segments.size(); ++i) {
        const SegmentInfo& seg = manifest.Segments[i];
        oss << "    {\n";
        oss << "      \"filename\": \""     << JsonEscape(seg.Filename) << "\",\n";
        oss << "      \"segment_index\": " << seg.SegmentIndex << ",\n";
        oss << "      \"start_ns\": "      << seg.StartNs << ",\n";
        oss << "      \"end_ns\": "         << seg.EndNs << ",\n";
        oss << "      \"size_bytes\": "    << seg.SizeBytes << "\n";
        oss << "    }" << (i + 1 < manifest.Segments.size() ? "," : "") << "\n";
    }
    oss << "  ]\n}\n";

    std::string payload = oss.str();

    FILE* f = nullptr;
#if RECPLAY_PLATFORM_WINDOWS
    if (fopen_s(&f, tmpPath.c_str(), "wb") != 0) f = nullptr;
#else
    f = std::fopen(tmpPath.c_str(), "wb");
#endif
    if (!f) return Status::ErrorIO;

    bool ok = (std::fwrite(payload.data(), 1, payload.size(), f) == payload.size());
    std::fclose(f);
    if (!ok) {
        fs::remove(tmpPath);
        return Status::ErrorIO;
    }

    // Prefer rename for an atomic replace on platforms/filesystems that support it.
    // Fall back to copy+overwrite when rename over an existing file fails (e.g. Windows).
    try {
        fs::rename(tmpPath, path);
        return Status::Ok;
    } catch (...) {
        try {
            fs::copy_file(tmpPath, path, fs::copy_options::overwrite_existing);
            fs::remove(tmpPath);
            return Status::Ok;
        } catch (...) {
            fs::remove(tmpPath);
            return Status::ErrorIO;
        }
    }
}

// ---------------------------------------------------------------------------
// ReadManifest
// ---------------------------------------------------------------------------

Status ReadManifest(const std::string& sessionDir, SessionManifest& outManifest) {
    std::string path = sessionDir + static_cast<char>(PATH_SEP) + "session.manifest";

    FILE* f = nullptr;
#if RECPLAY_PLATFORM_WINDOWS
    if (fopen_s(&f, path.c_str(), "rb") != 0) f = nullptr;
#else
    f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return Status::ErrorNotFound;

    std::fseek(f, 0, SEEK_END);
    long fileLen = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (fileLen <= 0) { std::fclose(f); return Status::ErrorCorrupted; }

    std::string json(static_cast<size_t>(fileLen), '\0');
    if (std::fread(&json[0], 1, static_cast<size_t>(fileLen), f) != static_cast<size_t>(fileLen)) {
        std::fclose(f);
        return Status::ErrorIO;
    }
    std::fclose(f);

    // Parse top-level fields
    outManifest.RecorderVersion = ExtractJsonString(json, "recorder_version");
    outManifest.CreatedAtNs     = ExtractJsonUint64(json, "created_at_ns");

    // Parse id (32 hex chars → 16 bytes)
    std::string idHex = ExtractJsonString(json, "id");
    if (idHex.size() == 32) {
        auto bytes = FromHex(idHex);
        std::copy(bytes.begin(), bytes.end(), outManifest.Id.begin());
    }

    // Parse channels
    outManifest.Channels.clear();
    for (const std::string& obj : ExtractJsonObjects(json, "channels")) {
        ChannelDef ch{};
        ch.Id                  = ExtractJsonUint16(obj, "id");
        ch.Name                = ExtractJsonString(obj, "name");
        ch.Layer               = static_cast<CaptureLayer>(ExtractJsonUint32(obj, "layer"));
        ch.Compression         = static_cast<CompressionCodec>(ExtractJsonUint32(obj, "compression"));
        ch.ChunkSizeBytes      = ExtractJsonUint32(obj, "chunk_size_bytes");
        ch.ChunkFlushIntervalMs = ExtractJsonUint32(obj, "chunk_flush_ms");
        ch.Schema              = ExtractJsonString(obj, "schema");
        ch.UserMetadata        = FromHex(ExtractJsonString(obj, "user_metadata_hex"));
        outManifest.Channels.push_back(std::move(ch));
    }

    // Parse segments
    outManifest.Segments.clear();
    for (const std::string& obj : ExtractJsonObjects(json, "segments")) {
        SegmentInfo seg{};
        seg.Filename      = ExtractJsonString(obj, "filename");
        seg.SegmentIndex  = ExtractJsonUint32(obj, "segment_index");
        seg.StartNs       = ExtractJsonUint64(obj, "start_ns");
        seg.EndNs         = ExtractJsonUint64(obj, "end_ns");
        seg.SizeBytes     = ExtractJsonUint64(obj, "size_bytes");
        outManifest.Segments.push_back(std::move(seg));
    }

    return Status::Ok;
}

std::string MakeSessionDirName(const SessionManifest& manifest) {
    // Format: YYYYMMDDTHHMMSS_XXXXXXXX (first 4 bytes of session Id as hex)
    std::time_t t = static_cast<std::time_t>(manifest.CreatedAtNs / 1'000'000'000ull);
    std::tm tm_val{};
    GmTimePortable(&t, &tm_val);
    char buf[32]{};
    std::strftime(buf, sizeof(buf), "%Y%m%dT%H%M%S", &tm_val);

    std::ostringstream oss;
    oss << buf << '_';
    for (int i = 0; i < 4; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(manifest.Id[i]);
    return oss.str();
}

std::string MakeSegmentFilename(const std::string& sessionName,
                                 uint32_t           segmentIndex) {
    std::ostringstream oss;
    oss << sessionName << '_'
        << std::setw(3) << std::setfill('0') << (segmentIndex + 1)
        << ".rec";
    return oss.str();
}

} // namespace detail
} // namespace recplay
