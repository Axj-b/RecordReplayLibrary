#pragma once
/// @file SessionLoader.hpp
/// @brief Utility for opening and closing recplay sessions into AppContext.

#include <string>

namespace viewer {
struct AppContext;
} // namespace viewer

namespace viewer {

class SessionLoader {
public:
    /// Open the session at \p path and populate \p ctx.
    /// On failure writes an error message to stderr and leaves ctx unchanged.
    /// @returns true on success.
    static bool Open(const std::string& path, AppContext& ctx);

    /// Close the current session and reset ctx to an empty state.
    static void Close(AppContext& ctx);
};

} // namespace viewer
