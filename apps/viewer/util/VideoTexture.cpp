/// @file util/VideoTexture.cpp
#include "VideoTexture.hpp"

#include <SDL_opengl.h>
#include <algorithm>
#include <cstring>

namespace viewer::util {

// ---------------------------------------------------------------------------
void VideoTexture::Upload(const void* rgb24, int w, int h)
{
    if (w <= 0 || h <= 0 || !rgb24) return;

    // (Re-)create the texture if dimensions changed or first call
    if (m_TexId == 0 || w != m_Width || h != m_Height) {
        Release();
        glGenTextures(1, &m_TexId);
        m_Width  = w;
        m_Height = h;
    }

    glBindTexture(GL_TEXTURE_2D, m_TexId);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Upload RGB24 (no padding — tightly packed)
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
                 w, h, 0,
                 GL_RGB, GL_UNSIGNED_BYTE,
                 rgb24);

    glBindTexture(GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------
void VideoTexture::Release()
{
    if (m_TexId) {
        glDeleteTextures(1, &m_TexId);
        m_TexId  = 0;
        m_Width  = 0;
        m_Height = 0;
    }
}

// ---------------------------------------------------------------------------
ImVec2 VideoTexture::FitSize(float maxW, float maxH) const noexcept
{
    if (m_Width <= 0 || m_Height <= 0) return { maxW, maxH };

    const float srcW = static_cast<float>(m_Width);
    const float srcH = static_cast<float>(m_Height);
    const float scale = std::min(maxW / srcW, maxH / srcH);
    return { srcW * scale, srcH * scale };
}

} // namespace viewer::util
