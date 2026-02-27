/// @file version.cpp
/// @brief Library version and Status string helpers.

// local includes
#include <recplay/recplay.hpp>

namespace recplay {

const char* VersionString() noexcept { return "1.0.0"; }

void Version(int& major, int& minor, int& patch) noexcept {
    major = 1; minor = 0; patch = 0;
}

const char* ToString(Status s) noexcept {
    switch (s) {
        case Status::Ok:               return "Ok";
        case Status::ErrorInvalidArg:  return "ErrorInvalidArg";
        case Status::ErrorIO:          return "ErrorIO";
        case Status::ErrorCorrupted:   return "ErrorCorrupted";
        case Status::ErrorNotFound:    return "ErrorNotFound";
        case Status::ErrorOutOfOrder:  return "ErrorOutOfOrder";
        case Status::ErrorFull:        return "ErrorFull";
        case Status::ErrorNotOpen:     return "ErrorNotOpen";
        case Status::ErrorAlreadyOpen: return "ErrorAlreadyOpen";
        default:                       return "Unknown";
    }
}

} // namespace recplay
