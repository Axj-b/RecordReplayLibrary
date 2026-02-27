/// @file App.cpp
#include "App.hpp"
#include "SessionLoader.hpp"

#include "panels/SessionInfoPanel.hpp"
#include "panels/ChannelListPanel.hpp"
#include "panels/TimelinePanel.hpp"
#include "panels/MessageInspectorPanel.hpp"
#include "panels/AnnotationsPanel.hpp"

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>

#include <SDL.h>
#include <SDL_opengl.h>

#include <iostream>
#include <stdexcept>

namespace viewer {

// ---------------------------------------------------------------------------
App::App()  = default;
App::~App() = default;

// ---------------------------------------------------------------------------
bool App::Init(const std::string& title, int width, int height)
{
    // SDL2
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init error: " << SDL_GetError() << "\n";
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    const SDL_WindowFlags wflags = static_cast<SDL_WindowFlags>(
        SDL_WINDOW_OPENGL |
        SDL_WINDOW_RESIZABLE |
        SDL_WINDOW_ALLOW_HIGHDPI);

    m_Window = SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        wflags);

    if (!m_Window) {
        std::cerr << "SDL_CreateWindow error: " << SDL_GetError() << "\n";
        return false;
    }

    m_GlCtx = SDL_GL_CreateContext(m_Window);
    if (!m_GlCtx) {
        std::cerr << "SDL_GL_CreateContext error: " << SDL_GetError() << "\n";
        return false;
    }
    SDL_GL_MakeCurrent(m_Window, m_GlCtx);
    SDL_GL_SetSwapInterval(1); // vsync

    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    // Slightly softer dark theme adjustments
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 4.f;
    style.FrameRounding     = 3.f;
    style.ScrollbarRounding = 3.f;
    style.GrabRounding      = 3.f;

    ImGui_ImplSDL2_InitForOpenGL(m_Window, m_GlCtx);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Register panels in rendering order
    m_Panels.emplace_back(std::make_unique<panels::SessionInfoPanel>());
    m_Panels.emplace_back(std::make_unique<panels::ChannelListPanel>());
    m_Panels.emplace_back(std::make_unique<panels::TimelinePanel>());
    m_Panels.emplace_back(std::make_unique<panels::MessageInspectorPanel>());
    m_Panels.emplace_back(std::make_unique<panels::AnnotationsPanel>());

    return true;
}

// ---------------------------------------------------------------------------
bool App::Update()
{
    // Event processing
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT)
            m_Running = false;
        if (event.type == SDL_WINDOWEVENT &&
            event.window.event == SDL_WINDOWEVENT_CLOSE &&
            event.window.windowID == SDL_GetWindowID(m_Window))
            m_Running = false;

        // Drag-and-drop a session directory onto the window
        if (event.type == SDL_DROPFILE) {
            SessionLoader::Open(event.drop.file, m_Ctx);
            SDL_free(event.drop.file);
        }
    }

    // Handle deferred requests (from menu bar)
    if (m_Ctx.RequestCloseSession) {
        SessionLoader::Close(m_Ctx);
        m_Ctx.RequestCloseSession = false;
    }

    // New frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    DrawDockspace();
    DrawMenuBar();

    // Draw all panels
    for (auto& panel : m_Panels)
        panel->Draw(m_Ctx);

    // Render
    ImGui::Render();
    int fb_w, fb_h;
    SDL_GL_GetDrawableSize(m_Window, &fb_w, &fb_h);
    glViewport(0, 0, fb_w, fb_h);
    glClearColor(0.10f, 0.10f, 0.10f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(m_Window);

    return m_Running;
}

// ---------------------------------------------------------------------------
void App::Shutdown()
{
    SessionLoader::Close(m_Ctx);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (m_GlCtx)  SDL_GL_DeleteContext(m_GlCtx);
    if (m_Window) SDL_DestroyWindow(m_Window);
    SDL_Quit();
}

// ---------------------------------------------------------------------------
void App::DrawDockspace()
{
    // Full-screen invisible host window for the dockspace
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    constexpr ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoTitleBar        |
        ImGuiWindowFlags_NoCollapse        |
        ImGuiWindowFlags_NoResize          |
        ImGuiWindowFlags_NoMove            |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus        |
        ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.f, 0.f));
    ImGui::Begin("##DockHost", nullptr, host_flags);
    ImGui::PopStyleVar(3);

    ImGui::DockSpace(ImGui::GetID("MainDockspace"),
                     ImVec2(0.f, 0.f),
                     ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
}

// ---------------------------------------------------------------------------
void App::DrawMenuBar()
{
    if (ImGui::BeginMainMenuBar()) {

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open session…", "Ctrl+O"))
                m_Ctx.RequestOpenSession = true;

            ImGui::Separator();

            if (ImGui::MenuItem("Close session", nullptr, false, m_Ctx.HasSession()))
                m_Ctx.RequestCloseSession = true;

            ImGui::Separator();

            if (ImGui::MenuItem("Quit", "Alt+F4"))
                m_Running = false;

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            for (auto& panel : m_Panels)
                ImGui::MenuItem(panel->Name());
            ImGui::EndMenu();
        }

        // Session path shown right-aligned in the menu bar
        if (m_Ctx.HasSession()) {
            const float avail = ImGui::GetContentRegionAvail().x;
            const std::string label = "  " + m_Ctx.SessionPath;
            const float tw = ImGui::CalcTextSize(label.c_str()).x;
            if (tw < avail) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - tw - 8.f);
                ImGui::TextDisabled("%s", label.c_str());
            }
        }

        ImGui::EndMainMenuBar();
    }

    // -----------------------------------------------------------------------
    // Simple open-session dialog (no native file dialog — user types the path)
    // -----------------------------------------------------------------------
    if (m_Ctx.RequestOpenSession) {
        ImGui::OpenPopup("Open session");
        m_Ctx.RequestOpenSession = false;
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560.f, 0.f), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Open session", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        static char path_buf[1024]{};
        ImGui::Text("Session directory path:");
        ImGui::SetNextItemWidth(-1.f);
        const bool confirmed = ImGui::InputText(
            "##path", path_buf, sizeof(path_buf),
            ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Spacing();
        bool ok     = ImGui::Button("Open",   ImVec2(100.f, 0.f)) || confirmed;
        ImGui::SameLine();
        bool cancel = ImGui::Button("Cancel", ImVec2(100.f, 0.f));

        if (ok) {
            SessionLoader::Open(path_buf, m_Ctx);
            path_buf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        if (cancel)
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

} // namespace viewer
