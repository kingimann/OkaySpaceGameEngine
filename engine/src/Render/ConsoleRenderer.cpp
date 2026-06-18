#include "okay/Render/ConsoleRenderer.hpp"
#include "okay/Math/Mathf.hpp"
#include <cstdio>
#include <iostream>

namespace okay {

ConsoleRenderer::ConsoleRenderer(int width, int height, bool clearScreenEachFrame)
    : m_width(width), m_height(height), m_clearScreen(clearScreenEachFrame),
      m_chars(static_cast<size_t>(width) * height, ' '),
      m_depth(static_cast<size_t>(width) * height, Mathf::Infinity) {}

void ConsoleRenderer::BeginFrame(const Color& clear) {
    // Pick a background glyph from the clear color's luminance.
    const char* ramp = " .:-";
    int idx = static_cast<int>(Mathf::Clamp01(clear.Luminance()) * 3.0f);
    m_background = ramp[idx];
    std::fill(m_chars.begin(), m_chars.end(), m_background);
    std::fill(m_depth.begin(), m_depth.end(), Mathf::Infinity);
}

void ConsoleRenderer::SetCamera(const Vec3& position, float orthoSize, float aspect) {
    m_camPos = position;
    m_halfHeight = orthoSize;
    m_halfWidth  = orthoSize * aspect;
}

bool ConsoleRenderer::WorldToCell(const Vec3& world, int& cx, int& cy) const {
    // Map world space (camera-centered) into normalized [-1, 1] device space.
    float nx = (world.x - m_camPos.x) / m_halfWidth;
    float ny = (world.y - m_camPos.y) / m_halfHeight;
    if (nx < -1.0f || nx > 1.0f || ny < -1.0f || ny > 1.0f) return false;
    // Device space to character cells (y is flipped: world up = screen up).
    cx = static_cast<int>((nx * 0.5f + 0.5f) * (m_width - 1) + 0.5f);
    cy = static_cast<int>((1.0f - (ny * 0.5f + 0.5f)) * (m_height - 1) + 0.5f);
    return true;
}

void ConsoleRenderer::DrawQuad(const Vec3& center, const Vec2& size,
                               const Color& color, char glyph) {
    if (color.a <= 0.0f) return;

    // World-space extents of the quad's corners, converted to cell extents.
    Vec3 minW{center.x - size.x * 0.5f, center.y - size.y * 0.5f, center.z};
    Vec3 maxW{center.x + size.x * 0.5f, center.y + size.y * 0.5f, center.z};

    // Compute cell-space bounds by sampling the two corners (clamped to screen).
    float nxMin = (minW.x - m_camPos.x) / m_halfWidth;
    float nxMax = (maxW.x - m_camPos.x) / m_halfWidth;
    float nyMin = (minW.y - m_camPos.y) / m_halfHeight;
    float nyMax = (maxW.y - m_camPos.y) / m_halfHeight;

    auto toCellX = [&](float nx) {
        return static_cast<int>((nx * 0.5f + 0.5f) * (m_width - 1) + 0.5f);
    };
    auto toCellY = [&](float ny) {
        return static_cast<int>((1.0f - (ny * 0.5f + 0.5f)) * (m_height - 1) + 0.5f);
    };

    int x0 = toCellX(nxMin), x1 = toCellX(nxMax);
    int y0 = toCellY(nyMax), y1 = toCellY(nyMin); // y flip swaps min/max
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);

    x0 = std::max(0, x0); y0 = std::max(0, y0);
    x1 = std::min(m_width - 1, x1); y1 = std::min(m_height - 1, y1);
    if (x0 > x1 || y0 > y1) return;

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            size_t i = static_cast<size_t>(y) * m_width + x;
            if (center.z <= m_depth[i]) { // nearer (smaller z) wins
                m_depth[i] = center.z;
                m_chars[i] = glyph;
            }
        }
    }
}

void ConsoleRenderer::EndFrame() {
    std::string out;
    out.reserve(static_cast<size_t>(m_width + 1) * m_height + 16);
    if (m_clearScreen) out += "\033[H\033[2J"; // move home + clear screen
    out += '+';
    out.append(m_width, '-');
    out += "+\n";
    for (int y = 0; y < m_height; ++y) {
        out += '|';
        out.append(&m_chars[static_cast<size_t>(y) * m_width], m_width);
        out += "|\n";
    }
    out += '+';
    out.append(m_width, '-');
    out += "+\n";
    std::fputs(out.c_str(), stdout);
    std::fflush(stdout);
}

} // namespace okay
