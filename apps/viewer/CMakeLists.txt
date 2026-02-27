cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    message(STATUS "viewer: SDL2 not found via find_package — trying FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        sdl2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-2.30.10
        GIT_SHALLOW    TRUE
    )
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON  CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(sdl2)
    # Alias to the canonical target name
    if(NOT TARGET SDL2::SDL2)
        add_library(SDL2::SDL2 ALIAS SDL2-static)
    endif()
endif()

find_package(OpenGL REQUIRED)

# ImGui (docking branch — required for DockSpace)
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

# Build ImGui as a static library (it ships only sources, no CMakeLists)
add_library(imgui_recplay STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_recplay PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_recplay PUBLIC SDL2::SDL2 OpenGL::GL)

# Suppress warnings in third-party sources
if(MSVC)
    target_compile_options(imgui_recplay PRIVATE /W0)
else()
    target_compile_options(imgui_recplay PRIVATE -w)
endif()

# ---------------------------------------------------------------------------
# Viewer application
# ---------------------------------------------------------------------------

set(VIEWER_SOURCES
    main.cpp
    App.hpp
    App.cpp
    AppContext.hpp
    SessionLoader.hpp
    SessionLoader.cpp
    util/FormatUtil.hpp
    util/HexView.hpp
    panels/IPanel.hpp
    panels/SessionInfoPanel.hpp
    panels/SessionInfoPanel.cpp
    panels/ChannelListPanel.hpp
    panels/ChannelListPanel.cpp
    panels/TimelinePanel.hpp
    panels/TimelinePanel.cpp
    panels/MessageInspectorPanel.hpp
    panels/MessageInspectorPanel.cpp
    panels/AnnotationsPanel.hpp
    panels/AnnotationsPanel.cpp
)

add_executable(viewer WIN32 ${VIEWER_SOURCES})

# Include the viewer directory so local headers resolve cleanly
target_include_directories(viewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(viewer PRIVATE recplay imgui_recplay SDL2::SDL2 OpenGL::GL)

target_compile_features(viewer PRIVATE cxx_std_17)

# WIN32 subsystem needs SDL2main on Windows
if(WIN32 AND TARGET SDL2::SDL2main)
    target_link_libraries(viewer PRIVATE SDL2::SDL2main)
endif()

set_target_properties(viewer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
