/// @file record_ffmpeg/main.cpp
/// @brief Example: capture an ffmpeg test-pattern stream and write it to a
///        recplay session.
///
/// Video channel  — "video/testsrc"
///   Raw RGB24 frames, 640×480 @ 30 fps (300 frames by default).
///   Schema tag: "rawvideo:rgb24:640x480:30fps"
///
/// Audio channel  — "audio/sine440"
///   Raw signed 16-bit LE PCM, 1 channel, 48 000 Hz.
///   Schema tag: "pcm:s16le:48000:1ch"
///
/// Usage:
///   record_ffmpeg [output_dir] [frame_count]
///   record_ffmpeg ./recordings 300

#include <recplay/recplay.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
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
// Constants
// ---------------------------------------------------------------------------

static constexpr int    VIDEO_W     = 640;
static constexpr int    VIDEO_H     = 480;
static constexpr int    VIDEO_FPS   = 30;
static constexpr int    VIDEO_CHANS = 3; // RGB24
static constexpr size_t FRAME_BYTES = static_cast<size_t>(VIDEO_W) * VIDEO_H * VIDEO_CHANS;

static constexpr int    AUDIO_RATE  = 48000;
static constexpr int    AUDIO_CHANS = 1;
static constexpr int    AUDIO_BITS  = 16; // s16le
// Samples per video frame (interleaved delivery: one audio chunk per video frame)
static constexpr int    AUDIO_SAMPLES_PER_FRAME = AUDIO_RATE / VIDEO_FPS;
static constexpr size_t AUDIO_CHUNK_BYTES =
    static_cast<size_t>(AUDIO_SAMPLES_PER_FRAME) * AUDIO_CHANS * (AUDIO_BITS / 8);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Read exactly `n` bytes from a FILE*, returning false on EOF/error.
static bool read_exact(FILE* f, void* buf, size_t n)
{
    size_t remaining = n;
    auto*  ptr       = static_cast<char*>(buf);
    while (remaining > 0) {
        size_t got = std::fread(ptr, 1, remaining, f);
        if (got == 0) return false;
        ptr       += got;
        remaining -= got;
    }
    return true;
}

/// Current UTC nanoseconds since Unix epoch.
static recplay::Timestamp now_ns()
{
    using namespace std::chrono;
    return static_cast<recplay::Timestamp>(
        duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    const std::string output_dir  = (argc >= 2) ? argv[1] : "./recordings";
    const int         frame_count = (argc >= 3) ? std::stoi(argv[2]) : 300;

    std::cout << "record_ffmpeg: capturing " << frame_count
              << " frames into \"" << output_dir << "\"\n";

    // -----------------------------------------------------------------------
    // 1. Build ffmpeg command — muxes video + audio into stdout
    //    We use two separate pipe invocations (one per media type) to keep
    //    the parsing simple.  Two parallel threads read each pipe.
    // -----------------------------------------------------------------------

    // Video pipe: testsrc → raw RGB24 on stdout
    const std::string video_cmd =
        "ffmpeg -hide_banner -loglevel error"
        " -f lavfi -i testsrc=size=" + std::to_string(VIDEO_W) + "x" + std::to_string(VIDEO_H) +
        ":rate=" + std::to_string(VIDEO_FPS) +
        " -frames:v " + std::to_string(frame_count) +
        " -f rawvideo -pix_fmt rgb24 pipe:1";

    // Audio pipe: sine 440 Hz → s16le PCM on stdout (same duration)
    const double duration_s = static_cast<double>(frame_count) / VIDEO_FPS;
    const std::string audio_cmd =
        "ffmpeg -hide_banner -loglevel error"
        " -f lavfi -i sine=frequency=440:sample_rate=" + std::to_string(AUDIO_RATE) +
        ":duration=" + std::to_string(duration_s) +
        " -f s16le -ar " + std::to_string(AUDIO_RATE) +
        " -ac " + std::to_string(AUDIO_CHANS) +
        " pipe:1";

    // -----------------------------------------------------------------------
    // 2. Open recplay session
    // -----------------------------------------------------------------------

    recplay::RecorderSession session;

    recplay::SessionConfig cfg;
    cfg.OutputDir   = output_dir;
    cfg.SessionName = "ffmpeg_testsrc";
    cfg.CrcEnabled  = true;

    if (auto s = session.Open(cfg); s != recplay::Status::Ok) {
        std::cerr << "Failed to open session (status " << static_cast<int>(s) << ")\n";
        return 1;
    }

    // Define video channel (uncompressed — raw frames are already hard to compress)
    recplay::ChannelId vid_ch{};
    session.DefineChannel(
        recplay::ChannelConfig{
            "video/testsrc",
            recplay::CaptureLayer::L7,
            recplay::CompressionCodec::None,
            0, 0,
            "rawvideo:rgb24:" + std::to_string(VIDEO_W) + "x" + std::to_string(VIDEO_H) +
                ":" + std::to_string(VIDEO_FPS) + "fps"
        },
        vid_ch);

    // Define audio channel (LZ4 — PCM compresses well)
    recplay::ChannelId aud_ch{};
    session.DefineChannel(
        recplay::ChannelConfig{
            "audio/sine440",
            recplay::CaptureLayer::L7,
            recplay::CompressionCodec::LZ4,
            0, 0,
            "pcm:s16le:" + std::to_string(AUDIO_RATE) + ":1ch"
        },
        aud_ch);

    std::cout << "Session opened at: " << session.SessionPath() << "\n"
              << "  video channel id=" << vid_ch << "\n"
              << "  audio channel id=" << aud_ch << "\n";

    // -----------------------------------------------------------------------
    // 3. Open ffmpeg pipes
    // -----------------------------------------------------------------------

    FILE* vid_pipe = POPEN(video_cmd.c_str(), "rb");
    FILE* aud_pipe = POPEN(audio_cmd.c_str(), "rb");

    if (!vid_pipe || !aud_pipe) {
        std::cerr << "Failed to spawn ffmpeg — make sure it is on your PATH.\n";
        session.Close();
        return 1;
    }

#ifdef _WIN32
    // Ensure the pipes are in binary mode on Windows
    if (vid_pipe) _setmode(_fileno(vid_pipe), _O_BINARY);
    if (aud_pipe) _setmode(_fileno(aud_pipe), _O_BINARY);
#endif

    // -----------------------------------------------------------------------
    // 4. Capture loop
    // -----------------------------------------------------------------------

    const uint64_t frame_ns  = 1'000'000'000ull / VIDEO_FPS; // nanoseconds per frame
    recplay::Timestamp ts    = now_ns();

    std::vector<uint8_t> video_buf(FRAME_BYTES);
    std::vector<uint8_t> audio_buf(AUDIO_CHUNK_BYTES);

    int frames_captured = 0;

    while (frames_captured < frame_count) {
        // Read one video frame
        if (!read_exact(vid_pipe, video_buf.data(), FRAME_BYTES)) {
            std::cout << "Video pipe ended after " << frames_captured << " frames.\n";
            break;
        }

        // Read matching audio chunk (may not have audio for last partial frame)
        bool has_audio = read_exact(aud_pipe, audio_buf.data(), AUDIO_CHUNK_BYTES);

        // Write to session
        session.Write(vid_ch, ts, video_buf.data(), static_cast<uint32_t>(FRAME_BYTES));
        if (has_audio)
            session.Write(aud_ch, ts, audio_buf.data(), static_cast<uint32_t>(AUDIO_CHUNK_BYTES));

        ts += frame_ns;
        ++frames_captured;

        if (frames_captured % VIDEO_FPS == 0)
            std::cout << "  captured " << frames_captured << " / " << frame_count
                      << " frames ...\n";
    }

    // -----------------------------------------------------------------------
    // 5. Cleanup
    // -----------------------------------------------------------------------

    PCLOSE(vid_pipe);
    PCLOSE(aud_pipe);

    session.Annotate(ts, "recording_end");
    session.Close();

    std::cout << "Done. Session path: " << session.SessionPath() << "\n"
              << "  video frames : " << session.MessagesWritten(vid_ch) << "\n"
              << "  audio chunks : " << session.MessagesWritten(aud_ch) << "\n";

    return 0;
}
