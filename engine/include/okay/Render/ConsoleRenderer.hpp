#pragma once
#include "okay/Render/Renderer.hpp"
#include <vector>
#include <string>

namespace okay {

/// A dependency-free renderer that rasterizes the scene into an ASCII frame
/// buffer and prints it to the terminal. It lets the whole engine run and be
/// observed in headless environments without a GPU or windowing system.
class ConsoleRenderer : public IRenderer {
public:
    ConsoleRenderer(int width, int height, bool clearScreenEachFrame = true);

    void BeginFrame(const Color& clear) override;
    void EndFrame() override;
    void SetCamera(const Vec3& position, float orthoSize, float aspect) override;
    void DrawQuad(const Vec3& center, const Vec2& size,
                  const Color& color, char glyph = '#') override;
    Viewport GetViewport() const override { return {m_width, m_height}; }

    /// Access the rendered character buffer (row-major), useful for tests.
    const std::vector<char>& FrameBuffer() const { return m_chars; }

private:
    struct Cell { char glyph; float depth; };
    bool WorldToCell(const Vec3& world, int& cx, int& cy) const;

    int   m_width;
    int   m_height;
    bool  m_clearScreen;
    Vec3  m_camPos{0, 0, 0};
    float m_halfHeight = 5.0f; // world units
    float m_halfWidth  = 5.0f;

    std::vector<char>  m_chars;
    std::vector<float> m_depth;
    char m_background = ' ';
};

} // namespace okay
