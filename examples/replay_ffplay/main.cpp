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
#  include <windows.h>
#  define POPEN  _popen
#  define PCLOSE _pclose
#else
#  define POPEN  popen
#  define PCLOSE pclose
#endif

// ---------------------------------------------------------------------------
// On Windows, each _popen call inherits ALL currently-open pipe handles into
// the new child process.  That means the second _popen (audio ffplay) receives
// a copy of the first pipe's write-end, which prevents the video ffplay from
// ever reaching EOF — and, on some Windows builds, corrupts the first pipe on
// the very next write.  Fix: mark each write-end non-inheritable immediately
// after opening it so subsequent _popen children don't pick it up.
static void make_pipe_noninheritable(FILE* f)
{
#ifdef _WIN32
    if (!f) return;
    HANDLE h = reinterpret_cast<HANDLE>(
        _get_osfhandle(_fileno(f)));
    if (h != INVALID_HANDLE_VALUE)
        SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0);
#else
    (void)f;
#endif
}

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

    // Store channel info by value — do NOT store pointers into the range-for
    // variable; the reference is only valid for the loop iteration.
    recplay::ChannelId  vid_id     = recplay::INVALID_CHANNEL_ID;
    std::string         vid_schema;
    recplay::ChannelId  aud_id     = recplay::INVALID_CHANNEL_ID;
    std::string         aud_schema;

    for (const auto& ch : reader.Channels()) {
        if (!ch.Schema.empty()) {
            if (ch.Schema.rfind("rawvideo:", 0) == 0) {
                vid_id     = ch.Id;
                vid_schema = ch.Schema;
                std::cout << "  found video channel: \"" << ch.Name
                          << "\"  id=" << ch.Id << "  schema=" << ch.Schema << "\n";
            } else if (ch.Schema.rfind("pcm:", 0) == 0) {
                aud_id     = ch.Id;
                aud_schema = ch.Schema;
                std::cout << "  found audio channel: \"" << ch.Name
                          << "\"  id=" << ch.Id << "  schema=" << ch.Schema << "\n";
            }
        }
    }

    if (vid_id == recplay::INVALID_CHANNEL_ID) {
        std::cerr << "No raw-video channel found in session.\n";
        return 1;
    }

    // -----------------------------------------------------------------------
    // 3. Parse video schema → build ffplay command
    // -----------------------------------------------------------------------

    int vid_w = 0, vid_h = 0, vid_fps = 0;
    if (!parse_video_schema(vid_schema, vid_w, vid_h, vid_fps)) {
        std::cerr << "Cannot parse video schema: \"" << vid_schema << "\"\n";
        return 1;
    }

    // Use "pipe:0" instead of "-" so ffmpeg unambiguously reads from stdin fd.
    const std::string ffplay_cmd =
        "ffplay -hide_banner -loglevel warning"
        " -f rawvideo -pixel_format rgb24"
        " -video_size " + std::to_string(vid_w) + "x" + std::to_string(vid_h) +
        " -framerate " + std::to_string(vid_fps) +
        " pipe:0";

    std::cout << "Launching video: " << ffplay_cmd << "\n";

    FILE* ffplay = POPEN(ffplay_cmd.c_str(), "wb");
    if (!ffplay) {
        std::cerr << "Failed to spawn ffplay — make sure it is on your PATH.\n";
        return 1;
    }
    // Mark write-end non-inheritable BEFORE spawning the audio process so the
    // audio ffplay child doesn't receive a copy of this pipe handle.
    make_pipe_noninheritable(ffplay);
#ifdef _WIN32
    _setmode(_fileno(ffplay), _O_BINARY);
#endif

    // -----------------------------------------------------------------------
    // 4. Optional: audio pipe to ffplay (separate process for audio preview)
    // -----------------------------------------------------------------------
    FILE* ffplay_aud = nullptr;
    if (aud_id != recplay::INVALID_CHANNEL_ID) {
        int aud_rate = 0, aud_ch = 0;
        if (parse_audio_schema(aud_schema, aud_rate, aud_ch)) {
            // -ch_layout replaces -ac in FFmpeg 8.x
            const std::string ch_layout = (aud_ch == 1) ? "mono"
                                        : (aud_ch == 2) ? "stereo"
                                        : std::to_string(aud_ch) + "c";
            const std::string aud_cmd =
                "ffplay -hide_banner -loglevel warning"
                " -f s16le -ar " + std::to_string(aud_rate) +
                " -ch_layout " + ch_layout +
                " -nodisp pipe:0";
            ffplay_aud = POPEN(aud_cmd.c_str(), "wb");
            if (ffplay_aud) {
                make_pipe_noninheritable(ffplay_aud);
#ifdef _WIN32
                _setmode(_fileno(ffplay_aud), _O_BINARY);
#endif
                std::cout << "Audio preview launched (ffplay audio sink).\n";
            }
        }
    }

    // -----------------------------------------------------------------------
    // 5. Replay loop — iterate messages in timestamp order
    // -----------------------------------------------------------------------

    using clock = std::chrono::steady_clock;
    using ns_t  = std::chrono::nanoseconds;

    const recplay::Timestamp session_start_ns = reader.StartNs();
    const auto               playback_start   = clock::now();

    uint64_t video_frames  = 0;
    uint64_t write_errors  = 0;
    uint64_t total_messages = 0;
    bool     pipe_broken   = false;

    auto status = reader.Read(
        recplay::ReadOptions{},
        [&](const recplay::MessageView& msg) -> bool
        {
            ++total_messages;

            // -- Pace to original timestamps
            const uint64_t relative_ns = msg.TimestampNs - session_start_ns;
            const auto target = playback_start + ns_t(static_cast<int64_t>(relative_ns));
            const auto now    = clock::now();
            if (target > now)
                std::this_thread::sleep_until(target);

            // -- Route to the right sink
            if (msg.Channel == vid_id) {
                ++video_frames;
                if (!pipe_broken) {
                    const size_t written = std::fwrite(msg.Data, 1, msg.Length, ffplay);
                    std::fflush(ffplay);
                    if (written != msg.Length) {
                        std::cerr << "ffplay video pipe closed after "
                                  << video_frames << " frame(s).\n";
                        ++write_errors;
                        pipe_broken = true;
                    }
                }
            } else if (ffplay_aud && msg.Channel == aud_id) {
                std::fwrite(msg.Data, 1, msg.Length, ffplay_aud);
            }

            return true; // always continue — don't abort on first pipe error
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

    std::cout << "Replay done.\n"
              << "  total messages   : " << total_messages  << "\n"
              << "  video frames read: " << video_frames    << "\n"
              << "  write errors     : " << write_errors    << "\n";
    return 0;
}
