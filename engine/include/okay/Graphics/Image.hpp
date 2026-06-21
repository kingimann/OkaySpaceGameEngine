#pragma once
#include "okay/Render/Color.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

/// CPU-side image: a tightly packed RGBA8 pixel buffer. Loads common formats
/// (PNG, JPG, BMP, TGA, ...) via stb_image and can save PNG. The editor and the
/// player turn these into GPU textures for sprites; tests use them headlessly.
class Image {
public:
    Image() = default;
    Image(int width, int height) : m_w(width), m_h(height),
        m_pixels(static_cast<std::size_t>(width) * height * 4, 0) {}

    int Width() const { return m_w; }
    int Height() const { return m_h; }
    bool Valid() const { return m_w > 0 && m_h > 0 && m_pixels.size() == static_cast<std::size_t>(m_w) * m_h * 4; }

    /// Raw RGBA bytes (row-major, 4 bytes per pixel).
    const std::uint8_t* Data() const { return m_pixels.data(); }
    std::uint8_t*       Data()       { return m_pixels.data(); }
    const std::vector<std::uint8_t>& Pixels() const { return m_pixels; }

    Color GetPixel(int x, int y) const {
        if (x < 0 || y < 0 || x >= m_w || y >= m_h) return Color(0, 0, 0, 0);
        std::size_t i = (static_cast<std::size_t>(y) * m_w + x) * 4;
        return Color(m_pixels[i] / 255.0f, m_pixels[i + 1] / 255.0f,
                     m_pixels[i + 2] / 255.0f, m_pixels[i + 3] / 255.0f);
    }
    void SetPixel(int x, int y, const Color& c) {
        if (x < 0 || y < 0 || x >= m_w || y >= m_h) return;
        std::size_t i = (static_cast<std::size_t>(y) * m_w + x) * 4;
        m_pixels[i]     = static_cast<std::uint8_t>(Clamp8(c.r));
        m_pixels[i + 1] = static_cast<std::uint8_t>(Clamp8(c.g));
        m_pixels[i + 2] = static_cast<std::uint8_t>(Clamp8(c.b));
        m_pixels[i + 3] = static_cast<std::uint8_t>(Clamp8(c.a));
    }
    void Fill(const Color& c) {
        for (int y = 0; y < m_h; ++y)
            for (int x = 0; x < m_w; ++x) SetPixel(x, y, c);
    }

    /// Sample by normalized UV (0..1), clamped to the edges. Nearest-neighbour.
    Color Sample(float u, float v) const {
        if (m_w <= 0 || m_h <= 0) return Color(1, 1, 1, 1);
        u = u < 0 ? 0 : (u > 1 ? 1 : u);
        v = v < 0 ? 0 : (v > 1 ? 1 : v);
        int x = (int)(u * (m_w - 1) + 0.5f), y = (int)(v * (m_h - 1) + 0.5f);
        return GetPixel(x, y);
    }

    /// Load any stb-supported image as RGBA. Returns false (and leaves the image
    /// empty) on failure; `error` receives a message if provided.
    bool Load(const std::string& path, std::string* error = nullptr);
    /// Save as a PNG. Returns false on failure.
    bool SavePNG(const std::string& path) const;

    /// Adopt an existing RGBA buffer.
    void Set(int width, int height, std::vector<std::uint8_t> rgba) {
        m_w = width; m_h = height; m_pixels = std::move(rgba);
    }

private:
    static int Clamp8(float v) {
        int i = static_cast<int>(v * 255.0f + 0.5f);
        return i < 0 ? 0 : i > 255 ? 255 : i;
    }
    int m_w = 0, m_h = 0;
    std::vector<std::uint8_t> m_pixels;
};

} // namespace okay
