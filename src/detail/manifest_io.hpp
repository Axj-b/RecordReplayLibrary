#pragma once

/// @file detail/manifest_io.hpp
/// @brief JSON serialisation / deserialisation of SessionManifest — internal use only.

// local includes
#include <recplay/session.hpp>

// system includes
#include <string>

namespace recplay {
namespace detail {

/// Write (or overwrite) the session.manifest JSON file in the given session directory.
Status WriteManifest(const std::string& sessionDir, const SessionManifest& manifest);

/// Read and parse the session.manifest JSON file from the given session directory.
Status ReadManifest(const std::string& sessionDir, SessionManifest& outManifest);

/// Generate a session sub-directory name from the manifest (start timestamp + short ID).
/// Example output: "20260227T120000_a1b2c3d4"
std::string MakeSessionDirName(const SessionManifest& manifest);

/// Generate a segment filename for the given segment index.
/// Example: MakeSegmentFilename("my_session", 2) -> "my_session_003.rec"
std::string MakeSegmentFilename(const std::string& sessionName, uint32_t segmentIndex);

}  // namespace detail
}  // namespace recplay
