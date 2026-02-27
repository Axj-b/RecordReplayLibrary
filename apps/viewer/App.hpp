#pragma once
/// @file App.hpp
/// @brief Top-level application class.
///
/// Owns the SDL2 window, OpenGL context, ImGui setup, and the panel list.
/// Call Init(), then loop on Update() until it returns false, then Shutdown().

#include "AppContext.hpp"
#include "panels/IPanel.hpp"

#include <SDL.h>
#include <memory>
#include <string>
#include <vector>

namespace viewer {

class App {
public:
    App();
    ~App();

    App(const App&)            = delete;
    App& operator=(const App&) = delete;

    /// Create the window, initialise OpenGL + ImGui, register panels.
    /// @returns false on fatal error.
    bool Init(const std::string& title, int width, int height);

    /// Process events + render one frame.
    /// @returns false when the user requests quit.
    bool Update();

    /// Clean up ImGui, SDL2, and all panels.
    void Shutdown();

    /// Expose the context so callers can pre-load a session before the loop.
    AppContext& Context() { return m_Ctx; }

private:
    void DrawMenuBar();
    void DrawDockspace();

    SDL_Window*   m_Window   = nullptr;
    SDL_GLContext m_GlCtx    = nullptr;
    bool          m_Running  = true;

    AppContext m_Ctx;
    std::vector<std::unique_ptr<panels::IPanel>> m_Panels;
};

} // namespace viewer
