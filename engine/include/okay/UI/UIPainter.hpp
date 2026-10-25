#pragma once
// The drawing surface the in-game UI renderer paints on. The engine has no SDL
// dependency, so the UI pass (UIRenderer.hpp) draws through this interface and each
// host supplies a backend: the player wraps its SDL_Renderer (UIPainterSDL.hpp).
//
// The calls deliberately mirror the subset of the SDL_Renderer API the UI pass was
// written against (same argument order, pointer-or-null rects, draw colour state,
// per-texture colour/alpha mods), so an SDL backend is a straight pass-through and
// the pass draws exactly what it drew when it lived in the player.
#include <cstdint>
#include <string>

namespace okay {
class TtfFont;

namespace ui2d {

struct Rect   { int x, y, w, h; };
struct FRect  { float x, y, w, h; };
struct Point  { int x, y; };
struct FPoint { float x, y; };
struct RGBA   { std::uint8_t r, g, b, a; };
/// Same layout as SDL_Vertex.
struct Vertex { FPoint position; RGBA color; FPoint tex_coord; };

/// Flip flags for CopyEx/CopyExF (same values as SDL_RendererFlip).
enum : int { FlipNone = 0, FlipH = 1, FlipV = 2 };

/// A backend texture handle (an SDL_Texture* for the SDL painter). Null = none.
using Tex = void*;

/// SDL_IntersectRect semantics: true and the overlap in `out` when the rects
/// overlap; false (and an empty `out`) when they don't or either is empty.
inline bool IntersectRect(const Rect* a, const Rect* b, Rect* out) {
    if (a->w <= 0 || a->h <= 0 || b->w <= 0 || b->h <= 0) { out->w = out->h = 0; return false; }
    int x0 = a->x > b->x ? a->x : b->x;
    int y0 = a->y > b->y ? a->y : b->y;
    int x1 = (a->x + a->w) < (b->x + b->w) ? (a->x + a->w) : (b->x + b->w);
    int y1 = (a->y + a->h) < (b->y + b->h) ? (a->y + a->h) : (b->y + b->h);
    out->x = x0; out->y = y0; out->w = x1 - x0; out->h = y1 - y0;
    if (out->w <= 0 || out->h <= 0) { out->w = out->h = 0; return false; }
    return true;
}

class Painter {
public:
    virtual ~Painter() = default;

    // Colour state + untextured primitives (SDL_SetRenderDrawColor, SDL_RenderFillRect...).
    virtual void SetDrawColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) = 0;
    virtual void FillRect(const Rect* r) = 0;
    virtual void DrawRect(const Rect* r) = 0;
    virtual void DrawLine(int x0, int y0, int x1, int y1) = 0;
    virtual void DrawPoint(int x, int y) = 0;

    // Clipping and scale (logical units; the clip rect is in the scaled space).
    virtual void SetClipRect(const Rect* r) = 0;   // null = no clip
    virtual void GetClipRect(Rect* r) = 0;
    virtual bool IsClipEnabled() = 0;
    virtual void SetScale(float sx, float sy) = 0;

    // Textures. LoadTexture resolves an asset path (cached by the backend; null when
    // it can't be read). FontAtlas returns the glyph atlas texture of a TTF font.
    virtual Tex  LoadTexture(const std::string& path) = 0;
    virtual Tex  FontAtlas(TtfFont* font) = 0;
    virtual void QueryTexture(Tex t, int* w, int* h) = 0;   // SDL_QueryTexture without format/access
    virtual void SetTextureColorMod(Tex t, std::uint8_t r, std::uint8_t g, std::uint8_t b) = 0;
    virtual void SetTextureAlphaMod(Tex t, std::uint8_t a) = 0;
    virtual void Copy(Tex t, const Rect* src, const Rect* dst) = 0;
    virtual void CopyF(Tex t, const Rect* src, const FRect* dst) = 0;
    virtual void CopyEx(Tex t, const Rect* src, const Rect* dst, double angle,
                        const Point* center, int flip) = 0;
    virtual void CopyExF(Tex t, const Rect* src, const FRect* dst, double angle,
                         const FPoint* center, int flip) = 0;
    virtual void Geometry(Tex t, const Vertex* verts, int numVerts, const int* indices, int numIndices) = 0;
};

} // namespace ui2d
} // namespace okay
