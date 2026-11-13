#include "okay/Render/ConsoleRenderer.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>
#include <cstdio>
#include <iostream>

namespace okay {

ConsoleRenderer::ConsoleRenderer(int width, int height, bool clearScreenEachFrame)
    : m_width(width), m_height(height), m_clearScreen(clearScreenEachFrame),
      m_chars(static_cast<size_t>(width) * height, ' '),
      m_depth(static_cast<size_t>(width) * height, Mathf::Infinity) {}

void ConsoleRenderer::BeginFrame(const Color& clear) {
    // Pick a background glyph from the clear color's luminance. A scene can
    // carry a NaN background colour through the serializer, and Clamp01 passes
    // NaN through (Mathf::Clamp is v<lo ? lo : ...), so a plain cast would read
    // far outside the ramp; a non-finite luminance just means the darkest glyph.
    const char* ramp = " .:-";
    float lum = clear.Luminance();
    int idx = std::isfinite(lum) ? static_cast<int>(Mathf::Clamp01(lum) * 3.0f) : 0;
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
    // A zero or non-finite ortho size would make the division infinite or NaN;
    // the range check below then rejects it before any float-to-int cast (the
    // cast itself is UB for values outside int's range).
    if (!(m_halfWidth > 0.0f) || !(m_halfHeight > 0.0f)) return false;
    float nx = (world.x - m_camPos.x) / m_halfWidth;
    float ny = (world.y - m_camPos.y) / m_halfHeight;
    if (!(nx >= -1.0f && nx <= 1.0f) || !(ny >= -1.0f && ny <= 1.0f)) return false;
    // Device space to character cells (y is flipped: world up = screen up).
    cx = static_cast<int>((nx * 0.5f + 0.5f) * (m_width - 1) + 0.5f);
    cy = static_cast<int>((1.0f - (ny * 0.5f + 0.5f)) * (m_height - 1) + 0.5f);
    return true;
}

void ConsoleRenderer::DrawQuad(const Vec3& center, const Vec2& size,
                               const Color& color, char glyph) {
    if (color.a <= 0.0f) return;
    // Skip degenerate / hostile geometry: a zero or non-finite ortho size makes
    // the divisions below infinite or NaN, and a huge, infinite or NaN corner
    // would make the float-to-int casts undefined behaviour (the later clamps
    // cannot repair a cast that never produced a well-defined int).
    if (!(m_halfWidth > 0.0f) || !(m_halfHeight > 0.0f)) return;
    if (!std::isfinite(center.x) || !std::isfinite(center.y) ||
        !std::isfinite(center.z) ||
        !std::isfinite(size.x) || !std::isfinite(size.y)) return;

    // World-space extents of the quad's corners, converted to cell extents.
    Vec3 minW{center.x - size.x * 0.5f, center.y - size.y * 0.5f, center.z};
    Vec3 maxW{center.x + size.x * 0.5f, center.y + size.y * 0.5f, center.z};

    // Compute cell-space bounds by sampling the two corners (clamped to screen).
    float nxMin = (minW.x - m_camPos.x) / m_halfWidth;
    float nxMax = (maxW.x - m_camPos.x) / m_halfWidth;
    float nyMin = (minW.y - m_camPos.y) / m_halfHeight;
    float nyMax = (maxW.y - m_camPos.y) / m_halfHeight;

    // Device space -> cell space, bounded before the cast: device coordinates
    // beyond [-1, 1] are clamped to the screen edges here rather than after the
    // int conversion, which is what keeps the cast itself well-defined.
    auto toCellX = [&](float nx) {
        if (!std::isfinite(nx)) return 0;
        float v = std::clamp(nx, -1.0f, 1.0f);
        return static_cast<int>((v * 0.5f + 0.5f) * (m_width - 1) + 0.5f);
    };
    auto toCellY = [&](float ny) {
        if (!std::isfinite(ny)) return 0;
        float v = std::clamp(ny, -1.0f, 1.0f);
        return static_cast<int>((1.0f - (v * 0.5f + 0.5f)) * (m_height - 1) + 0.5f);
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
