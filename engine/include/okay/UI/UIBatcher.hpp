#pragma once
// ui2d::BatchPainter: a Painter that turns every UI draw call (rects, lines, texture
// copies, geometry) into triangles in one vertex buffer and hands them to a backend
// painter as a few Geometry calls, one per run of the same clip and atlas page.
//
// Everything it draws comes from one atlas: page 0 holds font glyph atlases and is
// sampled linearly, page 1 holds UI images and is sampled nearest (as plain image
// textures are), and each page carries a white texel for untextured shapes, so a
// rect and the text on it share a texture. Images too big for a page fall back to
// their own texture. Keep one BatchPainter alive across frames (it owns the atlas
// and the reusable buffers) and call Flush() after each RenderSceneUI.
#include "okay/UI/UIPainter.hpp"
#include "okay/Graphics/Image.hpp"
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {
namespace ui2d {

class BatchPainter final : public Painter {
public:
    /// Reads an image file for the atlas (the default reads `path` as given).
    using ImageLoader = std::function<bool(const std::string& path, Image& out)>;

    explicit BatchPainter(Painter& backend, ImageLoader loader = {});

    /// Submit the pending triangles to the backend. Call after each pass.
    void Flush();
    /// False when the backend has no atlas support: draws are passed straight through.
    bool Batching() const { return !m_passthrough; }

    void SetDrawColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) override;
    void FillRect(const Rect* r) override;
    void DrawRect(const Rect* r) override;
    void DrawLine(int x0, int y0, int x1, int y1) override;
    void DrawPoint(int x, int y) override;

    void SetClipRect(const Rect* r) override;
    void GetClipRect(Rect* r) override;
    bool IsClipEnabled() override { return m_clipOn; }
    void SetScale(float sx, float sy) override;

    Tex  LoadTexture(const std::string& path) override;
    Tex  FontAtlas(TtfFont* font) override;
    void QueryTexture(Tex t, int* w, int* h) override;
    void SetTextureColorMod(Tex t, std::uint8_t r, std::uint8_t g, std::uint8_t b) override;
    void SetTextureAlphaMod(Tex t, std::uint8_t a) override;
    void Copy(Tex t, const Rect* src, const Rect* dst) override;
    void CopyF(Tex t, const Rect* src, const FRect* dst) override;
    void CopyEx(Tex t, const Rect* src, const Rect* dst, double angle, const Point* center, int flip) override;
    void CopyExF(Tex t, const Rect* src, const FRect* dst, double angle, const FPoint* center, int flip) override;
    void Geometry(Tex t, const Vertex* v, int nv, const int* idx, int ni) override;
    void MissingTexture(const std::string& path, const Rect& where) override { m_b.MissingTexture(path, where); }

private:
    // A texture handed to the renderer: a region of an atlas page, or (too big for a
    // page) the backend's own texture. Handles point into m_entries (stable).
    struct Entry {
        int page = -1;            // -1 = own backend texture
        int x = 0, y = 0, w = 0, h = 0;
        Tex own = nullptr;
        RGBA mod{255, 255, 255, 255};
        unsigned version = 0;     // font atlas version last copied into the page
    };
    struct Page {
        Image img;
        int shelfX = 0, shelfY = 0, shelfH = 0;
        Tex tex = nullptr;
        bool dirty = true;
        int whiteX = 0, whiteY = 0;   // centre of a white 3x3 block
    };

    bool EnsurePage(int page) { return m_pages[page].tex ? true : CreatePage(page); }   // false = pass-through
    bool CreatePage(int page);
    bool Place(int page, int w, int h, int& x, int& y);
    void Blit(Page& pg, const Image& src, int x, int y);
    void UseTexture(int page);                       // flush if the pending batch is another page
    void ApplyBackendClip();                         // sync the backend's clip to ours (scaled)
    void Quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, RGBA c);
    void WhiteQuad(float x, float y, float w, float h);
    void Image4(Entry& e, const Rect* src, float x, float y, float w, float h, double angle,
                const FPoint* center, int flip);
    void ForwardCopy(Entry& e, const Rect* src, float x, float y, float w, float h, double angle,
                     const FPoint* center, int flip);
    float U(int page, float px) const { return px / (float)m_pages[page].img.Width(); }
    float V(int page, float py) const { return py / (float)m_pages[page].img.Height(); }

    Painter& m_b;
    ImageLoader m_load;
    bool m_passthrough = false;
    Page m_pages[2];
    std::deque<Entry> m_entries;
    std::unordered_map<std::string, Entry*> m_byPath;   // null = could not load
    std::unordered_map<TtfFont*, Entry*> m_byFont;
    std::vector<Vertex> m_v;
    std::vector<int> m_i;
    std::vector<Vertex> m_scaled;         // geometry on a texture of its own, scaled
    int m_batchPage = -1;                 // page of the pending triangles (-1 = none)
    RGBA m_color{255, 255, 255, 255};
    float m_scale = 1.0f;
    bool m_clipOn = false;   Rect m_clip{0, 0, 0, 0};      // logical, as the renderer set it
    bool m_bClipOn = false;  Rect m_bClip{0, 0, 0, 0};     // what the backend has (pixels)
};

} // namespace ui2d
} // namespace okay
