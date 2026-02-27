/// @file replay_ffplay/main.cpp
/// @brief Example: open a recplay session produced by record_ffmpeg and
///        replay the video stream through ffplay, with optional audio.
///
/// The example re-creates the exact frame timing stored in the recording
/// (using the saved timestamps) so playback runs at the original capture rate.
///
/// Usage:
///   replay_ffplay <session_dir>
///   replay_ffplay ./recordings/ffmpeg_testsrc_20260227T120000_a1b2c3d4

#include <recplay/recplay.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  include <io.h>
#  include <fcntl.h>
#  define POPEN  _popen
#  define PCLOSE _pclose
#else
#  define POPEN  popen
#  define PCLOSE pclose
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Parse "rawvideo:rgb24:WxH:FPSfps" schema tag and extract width/height/fps.
static bool parse_video_schema(const std::string& schema, int& w, int& h, int& fps)
{
    // Expected format: "rawvideo:rgb24:WxH:FPSfps"
    // e.g.             "rawvideo:rgb24:640x480:30fps"
    char pix_fmt[32]{};
    if (std::sscanf(schema.c_str(),
                    "rawvideo:%31[^:]:%dx%d:%dfps",
                    pix_fmt, &w, &h, &fps) == 4) {
        return true;
    }
    return false;
}

/// Parse "pcm:s16le:RATE:NCHch" schema tag.
static bool parse_audio_schema(const std::string& schema, int& rate, int& channels)
{
    char fmt[32]{};
    if (std::sscanf(schema.c_str(),
                    "pcm:%31[^:]:%d:%dch",
                    fmt, &rate, &channels) == 3) {
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: replay_ffplay <session_dir>\n";
        return 1;
    }

    const std::string session_path = argv[1];

    // -----------------------------------------------------------------------
    // 1. Open session
    // -----------------------------------------------------------------------

    recplay::ReaderSession reader;
    if (auto s = reader.Open(session_path); s != recplay::Status::Ok) {
        std::cerr << "Failed to open session \"" << session_path
                  << "\" (status " << static_cast<int>(s) << ")\n";
        return 1;
    }

    const auto& manifest = reader.Manifest();
    std::cout << "replay_ffplay: opened session " << session_path << "\n"
              << "  channels  : " << manifest.Channels.size() << "\n"
              << "  segments  : " << manifest.Segments.size() << "\n"
              << "  duration  : "
              << (manifest.DurationNs() / 1'000'000) << " ms\n";

    // -----------------------------------------------------------------------
    // 2. Locate video (and optional audio) channels
    // -----------------------------------------------------------------------

    const recplay::ChannelDef* vid_def = nullptr;
    const recplay::ChannelDef* aud_def = nullptr;

    for (const auto& ch : reader.Channels()) {
        if (!ch.Schema.empty()) {
            if (ch.Schema.rfind("rawvideo:", 0) == 0) {
                vid_def = &ch;
                std::cout << "  found video channel: \"" << ch.Name << "\"  schema=" << ch.Schema << "\n";
            } else if (ch.Schema.rfind("pcm:", 0) == 0) {
                aud_def = &ch;
                std::cout << "  found audio channel: \"" << ch.Name << "\"  schema=" << ch.Schema << "\n";
            }
        }
    }

    if (!vid_def) {
        std::cerr << "No raw-video channel found in session.\n";
        return 1;
    }

    // -----------------------------------------------------------------------
    // 3. Parse video schema → build ffplay command
    // -----------------------------------------------------------------------

    int vid_w = 0, vid_h = 0, vid_fps = 0;
    if (!parse_video_schema(vid_def->Schema, vid_w, vid_h, vid_fps)) {
        std::cerr << "Cannot parse video schema: \"" << vid_def->Schema << "\"\n";
        return 1;
    }

    const std::string ffplay_cmd =
        "ffplay -hide_banner -loglevel warning"
        " -f rawvideo -pix_fmt rgb24"
        " -video_size " + std::to_string(vid_w) + "x" + std::to_string(vid_h) +
        " -framerate " + std::to_string(vid_fps) +
        " -window_title \"recplay replay\" -";

    std::cout << "Launching: " << ffplay_cmd << "\n";

    FILE* ffplay = POPEN(ffplay_cmd.c_str(), "wb");
    if (!ffplay) {
        std::cerr << "Failed to spawn ffplay — make sure it is on your PATH.\n";
        return 1;
    }

#ifdef _WIN32
    _setmode(_fileno(ffplay), _O_BINARY);
#endif

    // -----------------------------------------------------------------------
    // 4. Optional: audio pipe to ffplay (separate process for audio preview)
    // -----------------------------------------------------------------------
    FILE* ffplay_aud = nullptr;
    if (aud_def) {
        int aud_rate = 0, aud_ch = 0;
        if (parse_audio_schema(aud_def->Schema, aud_rate, aud_ch)) {
            const std::string aud_cmd =
                "ffplay -hide_banner -loglevel warning"
                " -f s16le -ar " + std::to_string(aud_rate) +
                " -ac " + std::to_string(aud_ch) +
                " -nodisp -";
            ffplay_aud = POPEN(aud_cmd.c_str(), "wb");
#ifdef _WIN32
            if (ffplay_aud) _setmode(_fileno(ffplay_aud), _O_BINARY);
#endif
            std::cout << "Audio preview launched (ffplay audio sink).\n";
        }
    }

    // -----------------------------------------------------------------------
    // 5. Replay loop — iterate messages in timestamp order
    // -----------------------------------------------------------------------

    using clock = std::chrono::steady_clock;
    using ns_t  = std::chrono::nanoseconds;

    // We pace playback relative to the session's start timestamp
    const recplay::Timestamp session_start_ns = reader.StartNs();
    const auto               playback_start   = clock::now();

    uint64_t video_frames = 0;

    auto status = reader.Read(
        recplay::ReadOptions{},
        [&](const recplay::MessageView& msg) -> bool
        {
            // -- Pace to original timestamps
            const uint64_t relative_ns = msg.TimestampNs - session_start_ns;
            const auto target = playback_start + ns_t(relative_ns);
            const auto now    = clock::now();
            if (target > now)
                std::this_thread::sleep_until(target);

            // -- Route to the right sink
            if (msg.Channel == vid_def->Id) {
                if (std::fwrite(msg.Data, 1, msg.Length, ffplay) != msg.Length) {
                    std::cerr << "ffplay pipe closed.\n";
                    return false; // stop iteration
                }
                ++video_frames;
            } else if (ffplay_aud && aud_def && msg.Channel == aud_def->Id) {
                std::fwrite(msg.Data, 1, msg.Length, ffplay_aud);
            }

            return true; // continue
        });

    // -----------------------------------------------------------------------
    // 6. Cleanup
    // -----------------------------------------------------------------------

    std::fflush(ffplay);
    PCLOSE(ffplay);
    if (ffplay_aud) {
        std::fflush(ffplay_aud);
        PCLOSE(ffplay_aud);
    }
    reader.Close();

    if (status != recplay::Status::Ok)
        std::cerr << "Read ended with status " << static_cast<int>(status) << "\n";

    std::cout << "Replay done. Video frames replayed: " << video_frames << "\n";
    return 0;
}
