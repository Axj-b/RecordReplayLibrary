/// @file main.cpp
/// @brief Entry point for the recplay Viewer application.
///
/// Usage:
///   viewer [session_dir]
///
/// If a session directory is provided on the command line it is opened
/// automatically on start; the user can open/close additional sessions
/// from the File menu or by drag-and-dropping a session folder onto the window.

#include "App.hpp"
#include "SessionLoader.hpp"

#include <iostream>
#include <string>

// SDL2 redefines main on Windows to allow SDL_main; include this shim last.
#include <SDL.h>

int main(int argc, char* argv[])
{
    viewer::App app;

    if (!app.Init("recplay Viewer", 1600, 900)) {
        std::cerr << "Failed to initialise viewer.\n";
        return 1;
    }

    // Pre-load session passed on the command line
    if (argc >= 2) {
        const std::string path = argv[1];
        std::cout << "Opening session: " << path << "\n";
        if (!viewer::SessionLoader::Open(path, app.Context()))
            std::cerr << "Warning: could not open \"" << path << "\"\n";
    }

    while (app.Update()) { /* frame loop */ }

    app.Shutdown();
    return 0;
}
