#pragma once
// Headless pictures for tests: a painter that keeps the pixels (software render, NOT a real window), a helper that renders a
// scene's screen-space UI to <OKAY_SHOT_DIR>/<file> when that variable is set, and a text helper. Written for this engine.
#include "okay/Graphics/Image.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "okay/Components/TextRenderer.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace shot {
using namespace okay;
// A painter that keeps the pixels, so a test can save what the game would draw.
class MemoryPainter : public ui2d::Painter {
public:
    explicit MemoryPainter(Image& image) : m_image(image) {}
    void SetDrawColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) override {
        m_r = r; m_g = g; m_b = b; m_a = a;
    }
    void FillRect(const ui2d::Rect* r) override {
        if (!r || r->w <= 0 || r->h <= 0) return;
        for (int y = r->y; y < r->y + r->h; ++y)
            for (int x = r->x; x < r->x + r->w; ++x) Plot(x, y);
    }
    void DrawRect(const ui2d::Rect* r) override {
        if (!r) return;
        DrawLine(r->x, r->y, r->x + r->w - 1, r->y);
        DrawLine(r->x, r->y, r->x, r->y + r->h - 1);
        DrawLine(r->x + r->w - 1, r->y, r->x + r->w - 1, r->y + r->h - 1);
        DrawLine(r->x, r->y + r->h - 1, r->x + r->w - 1, r->y + r->h - 1);
    }
    void DrawLine(int x0, int y0, int x1, int y1) override {
        int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
        int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
        for (;;) {
            Plot(x0, y0);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
    void DrawPoint(int x, int y) override { Plot(x, y); }
    void SetClipRect(const ui2d::Rect* r) override { m_clipOn = r != nullptr; if (r) m_clip = *r; }
    void GetClipRect(ui2d::Rect* r) override { if (r) *r = m_clip; }
    bool IsClipEnabled() override { return m_clipOn; }
    void SetScale(float sx, float sy) override { m_sx = sx > 0 ? sx : 1; m_sy = sy > 0 ? sy : 1; }
    ui2d::Tex LoadTexture(const std::string&) override { return nullptr; }
    ui2d::Tex FontAtlas(TtfFont*) override { return nullptr; }
    void QueryTexture(ui2d::Tex, int* w, int* h) override { if (w) *w = 0; if (h) *h = 0; }
    void SetTextureColorMod(ui2d::Tex, std::uint8_t, std::uint8_t, std::uint8_t) override {}
    void SetTextureAlphaMod(ui2d::Tex, std::uint8_t) override {}
    void Copy(ui2d::Tex, const ui2d::Rect*, const ui2d::Rect*) override {}
    void CopyF(ui2d::Tex, const ui2d::Rect*, const ui2d::FRect*) override {}
    void CopyEx(ui2d::Tex, const ui2d::Rect*, const ui2d::Rect*, double, const ui2d::Point*, int) override {}
    void CopyExF(ui2d::Tex, const ui2d::Rect*, const ui2d::FRect*, double, const ui2d::FPoint*, int) override {}
    void Geometry(ui2d::Tex, const ui2d::Vertex*, int, const int*, int) override {}

private:
    void Plot(int x, int y) {
        x = (int)std::lround((double)x * m_sx);
        y = (int)std::lround((double)y * m_sy);
        if (x < 0 || y < 0 || x >= m_image.Width() || y >= m_image.Height()) return;
        if (m_clipOn && (x < m_clip.x || y < m_clip.y || x >= m_clip.x + m_clip.w || y >= m_clip.y + m_clip.h)) return;
        const float a = m_a / 255.0f;
        const std::size_t i = (static_cast<std::size_t>(y) * m_image.Width() + x) * 4;
        std::uint8_t* p = m_image.Data() + i;
        p[0] = (std::uint8_t)(m_r * a + p[0] * (1.0f - a));
        p[1] = (std::uint8_t)(m_g * a + p[1] * (1.0f - a));
        p[2] = (std::uint8_t)(m_b * a + p[2] * (1.0f - a));
        p[3] = 255;
    }
    Image& m_image;
    std::uint8_t m_r = 255, m_g = 255, m_b = 255, m_a = 255;
    bool m_clipOn = false;
    ui2d::Rect m_clip{};
    float m_sx = 1, m_sy = 1;
};

// Software render of a scene's screen-space text (NOT a real window) to `<OKAY_SHOT_DIR>/<file>` when that variable is set.
bool ShotTo(Scene& scene, const std::string& file) {
    const char* dir = std::getenv("OKAY_SHOT_DIR");
    if (!dir) return true;
    UICanvas::Set(1280, 720);
    scene.uiFont = "pixel";
    scene.Update(1.0f / 60.0f);
    Image image(1280, 720);
    image.Fill(Color{0.07f, 0.08f, 0.12f, 1.0f});
    MemoryPainter painter(image);
    ui2d::UIRenderOptions opt;
    opt.interactive = false;
    ui2d::RenderSceneUI(scene, painter, 1280, 720, opt);
    return image.SavePNG(std::string(dir) + "/" + file);
}
void AddText(Scene& s, const char* name, float x, float y, const std::string& text, float px = 1.5f, Color c = Color{0.85f, 0.9f, 1.0f, 1.0f}) {
    GameObject* g = s.CreateGameObject(name);
    auto* t = g->AddComponent<TextRenderer>();
    t->screenSpace = true; t->screenPos = {x, y}; t->size = {1200, 400}; t->vcenter = false; t->pixelSize = px; t->color = c; t->text = text;
}
std::string Pad(std::string s, size_t n) { if (s.size() < n) s.append(n - s.size(), ' '); return s; }

} // namespace shot
