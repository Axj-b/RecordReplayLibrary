#pragma once
/// @file util/VideoTexture.hpp
/// @brief Manages a single OpenGL RGB24 texture for video frame preview.
///
/// Usage:
///   VideoTexture tex;
///   tex.Upload(frame_ptr, width, height);   // call whenever frame changes
///   ImGui::Image(tex.TextureId(), tex.FitSize(avail_w, avail_h));

#include <imgui.h>       // ImTextureID
#include <SDL_opengl.h>  // GLuint, GL_*
#include <cstddef>

namespace viewer::util {

class VideoTexture {
public:
    VideoTexture() = default;
    ~VideoTexture() { Release(); }

    VideoTexture(const VideoTexture&)            = delete;
    VideoTexture& operator=(const VideoTexture&) = delete;
    VideoTexture(VideoTexture&&)                 = delete;
    VideoTexture& operator=(VideoTexture&&)      = delete;

    /// Upload (or re-upload) an RGB24 frame.
    /// @param rgb24  Pointer to width * height * 3 bytes (R8G8B8 packed, top-left origin).
    /// @param w      Frame width  in pixels.
    /// @param h      Frame height in pixels.
    void Upload(const void* rgb24, int w, int h);

    /// Delete the GL texture (next Upload() will recreate it).
    void Release();

    /// Returns true when a texture has been successfully uploaded.
    bool IsValid() const noexcept { return m_TexId != 0; }

    /// ImGui-compatible texture ID.
    ImTextureID TextureId() const noexcept {
        return (ImTextureID)(uintptr_t)m_TexId;
    }

    int Width()  const noexcept { return m_Width;  }
    int Height() const noexcept { return m_Height; }

    /// Compute a display size that fits within (maxW × maxH) while keeping
    /// the original aspect ratio.
    ImVec2 FitSize(float maxW, float maxH) const noexcept;

private:
    GLuint m_TexId  = 0;
    int    m_Width  = 0;
    int    m_Height = 0;
};

} // namespace viewer::util
