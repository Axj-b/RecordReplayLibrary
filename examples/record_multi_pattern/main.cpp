/// @file record_multi_pattern/main.cpp
/// @brief Example: record 4 ffmpeg test patterns as separate channels in a
///        single recplay session, all captured concurrently in a round-robin
///        drain loop so every ffmpeg pipe is drained at an equal rate.
///
/// Channels written:
///   video/testsrc      — classic colour bars + moving counter   (ffmpeg testsrc)
///   video/testsrc2     — SMPTE-style multi-feature pattern      (ffmpeg testsrc2)
///   video/smptebars    — pure SMPTE colour bars                 (ffmpeg smptebars)
///   video/rgbtestsrc   — RGB primary/secondary gradient         (ffmpeg rgbtestsrc)
///
/// All channels: RGB24, 640×480, 30 fps.
///
/// Usage:
///   record_multi_pattern [output_dir] [frame_count]
///   record_multi_pattern ./recordings 300

#include <recplay/recplay.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <io.h>
#  include <fcntl.h>
#  include <windows.h>
#  define POPEN  _popen
#  define PCLOSE _pclose
#else
#  define POPEN  popen
#  define PCLOSE pclose
#endif

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr int    VIDEO_W     = 640;
static constexpr int    VIDEO_H     = 480;
static constexpr int    VIDEO_FPS   = 30;
static constexpr size_t FRAME_BYTES = static_cast<size_t>(VIDEO_W) * VIDEO_H * 3;

// ---------------------------------------------------------------------------
// Pattern descriptor
// ---------------------------------------------------------------------------
struct Pattern {
    const char* ChannelName;
    const char* FfmpegSrc; ///< lavfi -i source expression (without size/rate params)
    const char* FfmpegVf;  ///< extra -vf filter chain, or "" for none
    const char* Description;
};

// All four patterns use the same reliable lavfi testsrc source (ffmpeg 8.0 compatible)
// and apply a simple video filter to make each channel visually distinct.
static const std::array<Pattern, 4> kPatterns = {{
    { "video/testsrc",    "testsrc",  "",       "Classic colour bars + moving counter" },
    { "video/inverted",   "testsrc",  "negate", "Colour-inverted test source"          },
    { "video/hflipped",   "testsrc",  "hflip",  "Horizontally mirrored test source"    },
    { "video/vflipped",   "testsrc",  "vflip",  "Vertically mirrored test source"      },
}};

static constexpr int N = static_cast<int>(kPatterns.size()); // 4

// ---------------------------------------------------------------------------
// Helper: current UTC nanoseconds
// ---------------------------------------------------------------------------
static recplay::Timestamp now_ns()
{
    using namespace std::chrono;
    return static_cast<recplay::Timestamp>(
        duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
}

// ---------------------------------------------------------------------------
// Helper: read exactly n bytes; returns false on EOF/error
// ---------------------------------------------------------------------------
static bool read_exact(FILE* f, void* buf, size_t n)
{
    auto* ptr = static_cast<char*>(buf);
    while (n > 0) {
        const size_t got = std::fread(ptr, 1, n, f);
        if (got == 0) return false;
        ptr += got;
        n   -= got;
    }
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    const std::string output_dir  = (argc >= 2) ? argv[1] : "./recordings";
    const int         frame_count = (argc >= 3) ? std::stoi(argv[2]) : 300;

    std::cout << "record_multi_pattern: recording " << N
              << " patterns x " << frame_count
              << " frames -> \"" << output_dir << "\"\n\n";
    for (const auto& p : kPatterns) {
        const std::string filter = p.FfmpegVf[0] ? std::string(" -vf ") + p.FfmpegVf : "";
        std::cout << "  [" << p.ChannelName << "]  " << p.Description << "  (src=testsrc" << filter << ")\n";
    }
    std::cout << "\n";

    // -----------------------------------------------------------------------
    // 1. Open session
    // -----------------------------------------------------------------------
    recplay::RecorderSession session;

    recplay::SessionConfig cfg;
    cfg.OutputDir        = output_dir;
    cfg.SessionName      = "multi_pattern";
    cfg.CrcEnabled       = true;
    cfg.SingleFile       = true;
    // MaxSegmentBytes is ignored in SingleFile mode (rotation disabled)
    cfg.MaxSegmentBytes  = 2ULL * 1024 * 1024 * 1024;

    if (auto s = session.Open(cfg); s != recplay::Status::Ok) {
        std::cerr << "Failed to open session (status " << static_cast<int>(s) << ")\n";
        return 1;
    }

    // -----------------------------------------------------------------------
    // 2. Define channels
    // -----------------------------------------------------------------------
    const std::string schema =
        "rawvideo:rgb24:" + std::to_string(VIDEO_W) + "x" + std::to_string(VIDEO_H) +
        ":" + std::to_string(VIDEO_FPS) + "fps";

    std::array<recplay::ChannelId, N> ids{};
    for (int i = 0; i < N; ++i) {
        recplay::ChannelConfig chcfg;
        chcfg.Name        = kPatterns[i].ChannelName;
        chcfg.Layer       = recplay::CaptureLayer::L7;
        chcfg.Compression = recplay::CompressionCodec::None;
        chcfg.Schema      = schema;
        session.DefineChannel(chcfg, ids[i]);
        std::cout << "  channel [" << ids[i] << "] = " << kPatterns[i].ChannelName << "\n";
    }
    std::cout << "\n";

    // -----------------------------------------------------------------------
    // 3. Open ffmpeg pipes — all 4 processes start simultaneously
    // -----------------------------------------------------------------------
    std::array<FILE*, N> pipes{};
    for (int i = 0; i < N; ++i) {
        const std::string vf_opt =
            (kPatterns[i].FfmpegVf[0] != '\0')
            ? (std::string(" -vf ") + kPatterns[i].FfmpegVf)
            : "";

        const std::string cmd =
            std::string("ffmpeg -hide_banner -loglevel error") +
            " -f lavfi -i " + kPatterns[i].FfmpegSrc +
            "=size=" + std::to_string(VIDEO_W) + "x" + std::to_string(VIDEO_H) +
            ":rate=" + std::to_string(VIDEO_FPS) +
            vf_opt +
            " -frames:v " + std::to_string(frame_count) +
            " -f rawvideo -pixel_format rgb24 pipe:1";

        pipes[i] = POPEN(cmd.c_str(), "rb");
        if (!pipes[i]) {
            std::cerr << "popen failed for " << kPatterns[i].FfmpegSrc << "\n";
            // Close already-opened pipes before bailing
            for (int j = 0; j < i; ++j) if (pipes[j]) PCLOSE(pipes[j]);
            session.Close();
            return 1;
        }
#ifdef _WIN32
        _setmode(_fileno(pipes[i]), _O_BINARY);
        // Mark this pipe's read-end as non-inheritable so subsequent _popen
        // children do not inherit it (which would cause premature EOF when
        // those children exit and close their copy of the handle).
        {
            HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(pipes[i])));
            if (h != INVALID_HANDLE_VALUE)
                SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0);
        }
#endif
    }

    // -----------------------------------------------------------------------
    // 4. Round-robin capture loop
    //
    //    Read one frame from each pipe in turn, then write it to the session.
    //    This drains all 4 pipes at an equal rate so no pipe buffer ever fills.
    //    A channel whose pipe ends early is marked done and skipped.
    // -----------------------------------------------------------------------
    // Per-channel scratch buffers: allocate once, reuse every frame.
    std::array<std::vector<uint8_t>, N> bufs;
    for (auto& b : bufs) b.resize(FRAME_BYTES);

    std::array<int,  N> frames_captured{};
    std::array<bool, N> done{};

    const recplay::Timestamp base_ts  = now_ns();
    const uint64_t           frame_ns = 1'000'000'000ull / VIDEO_FPS;
    recplay::Timestamp       last_ts  = base_ts;

    int last_progress = -1;
    int total_done    = 0;

    while (total_done < N) {
        for (int i = 0; i < N; ++i) {
            if (done[i]) continue;

            if (frames_captured[i] >= frame_count ||
                !read_exact(pipes[i], bufs[i].data(), FRAME_BYTES))
            {
                done[i] = true;
                ++total_done;
                continue;
            }

            // Timestamp assigned when frame is received from the pipe.
            const recplay::Timestamp ts =
                base_ts + static_cast<uint64_t>(frames_captured[i]) * frame_ns;

            session.Write(ids[i], ts,
                          bufs[i].data(), static_cast<uint32_t>(FRAME_BYTES));
            if (ts > last_ts) last_ts = ts;
            ++frames_captured[i];
        }

        // Progress every 30 frames (1 second)
        const int min_f = *std::min_element(frames_captured.begin(), frames_captured.end());
        if (min_f / 30 != last_progress) {
            last_progress = min_f / 30;
            std::cout << "\r  frames:";
            for (int i = 0; i < N; ++i)
                std::cout << "  " << kPatterns[i].ChannelName
                          << "=" << frames_captured[i] << "/" << frame_count;
            std::cout << std::flush;
        }
    }
    std::cout << "\n";

    // -----------------------------------------------------------------------
    // 5. Close pipes + session
    // -----------------------------------------------------------------------
    for (int i = 0; i < N; ++i)
        if (pipes[i]) PCLOSE(pipes[i]);

    // Annotation timestamp matches the last written frame (same synthetic timeline).
    session.Annotate(last_ts, "recording_end");
    const std::string sessPath = session.SessionPath();
    session.Close();

    // -----------------------------------------------------------------------
    // 6. Summary
    // -----------------------------------------------------------------------
    std::cout << "\nDone.\n"
              << "  Session path : " << sessPath << "\n";
    for (int i = 0; i < N; ++i)
        std::cout << "  " << kPatterns[i].ChannelName
                  << " : " << frames_captured[i] << " frames\n";

    return 0;
}
