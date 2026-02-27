#pragma once

/// @file recplay.hpp
/// @brief Umbrella include for the recplay recording/replay library.
///
/// Include this single header to get the full public API:
///   #include <recplay/recplay.hpp>

#include "types.hpp"
#include "format.hpp"
#include "channel.hpp"
#include "session.hpp"
#include "writer.hpp"
#include "reader.hpp"
#include "splitter.hpp"

/// @mainpage recplay — Sensor Data Recording & Replay Library
///
/// recplay is a self-contained, payload-agnostic C++ library for recording and
/// replaying multi-channel sensor data streams to/from a custom binary .rec format.
///
/// ## Key concepts
///
/// - **Session**: a logical recording consisting of one or more segment files in a
///   single directory, described by a session.manifest file.
///
/// - **Segment**: a single .rec binary file bounded by a configurable max size.
///   Each segment is independently readable even without the manifest.
///
/// - **Channel**: a named stream of raw byte messages (L3/L4 packets or L7 application
///   messages). Channels are registered once at session open; they persist across
///   all segments of the session.
///
/// - **Mux mode**: all channels written into one session (default, lower overhead).
///
/// - **Split mode**: per-channel sessions, each a self-contained directory; created
///   either at record time or post-hoc via Splitter::split().
///
/// ## Quick start — recording
/// @code
///   #include <recplay/recplay.hpp>
///
///   recplay::SessionConfig cfg;
///   cfg.OutputDir        = "/recordings";
///   cfg.MaxSegmentBytes  = 512 * 1024 * 1024; // 512 MiB
///
///   recplay::RecorderSession rec;
///   rec.Open(cfg);
///
///   recplay::ChannelId lidar = rec.DefineChannel({
///       "lidar/front", recplay::CaptureLayer::L3L4, recplay::CompressionCodec::LZ4
///   });
///   recplay::ChannelId can = rec.DefineChannel({
///       "can/chassis", recplay::CaptureLayer::L7, recplay::CompressionCodec::None
///   });
///
///   // In your data-capture loop:
///   rec.Write(lidar, timestampNs, udpPayload, udpLen);
///   rec.Write(can,   timestampNs, canFrame,   canLen);
///
///   rec.Close();
/// @endcode
///
/// ## Quick start — replay
/// @code
///   recplay::ReaderSession reader;
///   reader.Open("/recordings/20260227T120000_a1b2c3d4");
///
///   reader.Seek(startNs);
///
///   recplay::MessageView msg;
///   while (reader.ReadNext(msg)) {
///       if (msg.Channel == lidarId)
///           InjectUdp(msg.Data, msg.Length);
///   }
/// @endcode

namespace recplay {

/// Library version as a human-readable string, e.g. "1.0.0".
const char* VersionString() noexcept;

/// Library version as major/minor/patch integers.
void Version(int& major, int& minor, int& patch) noexcept;

}  // namespace recplay
