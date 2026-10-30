#include "UIPreviewPainter.hpp"
#include "okay/Graphics/TtfFont.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace oked {

using namespace okay::ui2d;

UIPreviewPainter::UIPreviewPainter(ImDrawList* dl, ImVec2 origin, SDL_Renderer* renderer, TextureLoader loader)
    : m_dl(dl), m_origin(origin), m_renderer(renderer), m_loader(loader) {}

UIPreviewPainter::~UIPreviewPainter() {
    if (m_clipPushed) m_dl->PopClipRect();
}

void UIPreviewPainter::SetDrawColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    m_color = IM_COL32(r, g, b, a);
}

void UIPreviewPainter::FillPx(float x, float y, float w, float h) {
    if (w <= 0.0f || h <= 0.0f) return;
    m_dl->AddRectFilled(P(x, y), P(x + w, y + h), m_color);
}

void UIPreviewPainter::FillRect(const okay::ui2d::Rect* r) {
    if (r) FillPx((float)r->x, (float)r->y, (float)r->w, (float)r->h);
}

// SDL outlines the rect's border pixels (inside the rect), one pixel wide.
void UIPreviewPainter::DrawRect(const okay::ui2d::Rect* r) {
    if (!r || r->w <= 0 || r->h <= 0) return;
    const float x = (float)r->x, y = (float)r->y, w = (float)r->w, h = (float)r->h;
    FillPx(x, y, w, 1.0f);
    if (h > 1.0f) FillPx(x, y + h - 1.0f, w, 1.0f);
    if (h > 2.0f) { FillPx(x, y + 1.0f, 1.0f, h - 2.0f); if (w > 1.0f) FillPx(x + w - 1.0f, y + 1.0f, 1.0f, h - 2.0f); }
}

// SDL lines include both end pixels. Axis-aligned ones (the scanline fills of
// circles and triangles) become exact pixel rects; diagonals an ImGui line.
void UIPreviewPainter::DrawLine(int x0, int y0, int x1, int y1) {
    if (y0 == y1) { FillPx((float)std::min(x0, x1), (float)y0, (float)(std::abs(x1 - x0) + 1), 1.0f); return; }
    if (x0 == x1) { FillPx((float)x0, (float)std::min(y0, y1), 1.0f, (float)(std::abs(y1 - y0) + 1)); return; }
    m_dl->AddLine(P(x0 + 0.5f, y0 + 0.5f), P(x1 + 0.5f, y1 + 0.5f), m_color, m_scale);
}

void UIPreviewPainter::DrawPoint(int x, int y) { FillPx((float)x, (float)y, 1.0f, 1.0f); }

// One clip at a time, like SDL: replace the previous one (intersected with whatever
// clip the caller already pushed on the draw list, i.e. the preview panel).
void UIPreviewPainter::SetClipRect(const okay::ui2d::Rect* r) {
    if (m_clipPushed) { m_dl->PopClipRect(); m_clipPushed = false; }
    m_clipOn = r != nullptr;
    if (!r) return;
    m_clip = *r;
    m_dl->PushClipRect(P((float)r->x, (float)r->y), P((float)(r->x + r->w), (float)(r->y + r->h)), true);
    m_clipPushed = true;
}

void UIPreviewPainter::GetClipRect(okay::ui2d::Rect* r) {
    if (r) *r = m_clipOn ? m_clip : okay::ui2d::Rect{0, 0, 0, 0};
}

void UIPreviewPainter::SetScale(float sx, float) { m_scale = sx > 0.0f ? sx : 1.0f; }

Tex UIPreviewPainter::LoadTexture(const std::string& path) {
    return (m_loader && !path.empty()) ? (Tex)m_loader(path) : nullptr;
}

Tex UIPreviewPainter::FontAtlas(okay::TtfFont* font) {
    return (Tex)PreviewFontAtlas(m_renderer, font);
}

SDL_Texture* PreviewFontAtlas(SDL_Renderer* r, okay::TtfFont* f) {
    if (!r || !f || !f->Valid()) return nullptr;
    struct Up { SDL_Texture* tex = nullptr; std::uint32_t gen = 0; int w = 0, h = 0; };
    static std::unordered_map<okay::TtfFont*, Up> cache;
    static std::vector<std::pair<SDL_Texture*, int>> retired;   // texture, the frame it was replaced in
    const int frame = ImGui::GetFrameCount();
    for (std::size_t i = 0; i < retired.size();) {
        if (frame - retired[i].second < 2) { ++i; continue; }
        SDL_DestroyTexture(retired[i].first);
        retired[i] = retired.back();
        retired.pop_back();
    }
    Up& u = cache[f];
    if (u.tex && u.gen == f->Generation()) return u.tex;
    const okay::Image& a = f->Atlas();
    if (u.tex && (u.w != a.Width() || u.h != a.Height())) { retired.push_back({u.tex, frame}); u.tex = nullptr; }
    if (!u.tex) {
        u.tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, a.Width(), a.Height());
        if (!u.tex) return nullptr;
        SDL_SetTextureBlendMode(u.tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(u.tex, SDL_ScaleModeLinear);
        u.w = a.Width();
        u.h = a.Height();
    }
    SDL_UpdateTexture(u.tex, nullptr, a.Data(), a.Width() * 4);   // glyphs only get added: queued text stays right
    u.gen = f->Generation();
    return u.tex;
}

void UIPreviewPainter::QueryTexture(Tex t, int* w, int* h) {
    int tw = 0, th = 0;
    if (t) SDL_QueryTexture((SDL_Texture*)t, nullptr, nullptr, &tw, &th);
    if (w) *w = tw;
    if (h) *h = th;
}

void UIPreviewPainter::SetTextureColorMod(Tex t, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    auto it = m_mods.try_emplace(t, RGBA{255, 255, 255, 255}).first;
    it->second.r = r; it->second.g = g; it->second.b = b;
}

void UIPreviewPainter::SetTextureAlphaMod(Tex t, std::uint8_t a) {
    m_mods.try_emplace(t, RGBA{255, 255, 255, 255}).first->second.a = a;
}

// Vertex colour modulated by the texture's colour/alpha mod (as SDL does at draw time).
ImU32 UIPreviewPainter::Tint(Tex t, ImU32 c) const {
    auto it = m_mods.find(t);
    if (it == m_mods.end()) return c;
    const RGBA& m = it->second;
    auto ch = [&](int shift, std::uint8_t k) { return (ImU32)((((c >> shift) & 0xFF) * k + 127) / 255) << shift; };
    return ch(IM_COL32_R_SHIFT, m.r) | ch(IM_COL32_G_SHIFT, m.g) | ch(IM_COL32_B_SHIFT, m.b) | ch(IM_COL32_A_SHIFT, m.a);
}

void UIPreviewPainter::Blit(Tex t, const okay::ui2d::Rect* src, float x, float y, float w, float h,
                            double angle, int flip) {
    if (!t || w <= 0.0f || h <= 0.0f) return;
    int tw = 0, th = 0;
    QueryTexture(t, &tw, &th);
    if (tw <= 0 || th <= 0) return;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    if (src) {
        u0 = (float)src->x / tw; v0 = (float)src->y / th;
        u1 = (float)(src->x + src->w) / tw; v1 = (float)(src->y + src->h) / th;
    }
    if (flip & FlipH) std::swap(u0, u1);
    if (flip & FlipV) std::swap(v0, v1);
    const ImTextureID id = (ImTextureID)(intptr_t)t;
    const ImU32 col = Tint(t);
    if (angle == 0.0) {
        m_dl->AddImage(id, P(x, y), P(x + w, y + h), ImVec2(u0, v0), ImVec2(u1, v1), col);
        return;
    }
    const float a = (float)(angle * 3.14159265358979 / 180.0), c = std::cos(a), s = std::sin(a);
    const float cx = x + w * 0.5f, cy = y + h * 0.5f;
    auto rot = [&](float px, float py) {
        const float dx = px - cx, dy = py - cy;
        return P(cx + dx * c - dy * s, cy + dx * s + dy * c);
    };
    m_dl->AddImageQuad(id, rot(x, y), rot(x + w, y), rot(x + w, y + h), rot(x, y + h),
                       ImVec2(u0, v0), ImVec2(u1, v0), ImVec2(u1, v1), ImVec2(u0, v1), col);
}

void UIPreviewPainter::Copy(Tex t, const okay::ui2d::Rect* src, const okay::ui2d::Rect* dst) {
    if (dst) Blit(t, src, (float)dst->x, (float)dst->y, (float)dst->w, (float)dst->h, 0.0, FlipNone);
}

void UIPreviewPainter::CopyF(Tex t, const okay::ui2d::Rect* src, const FRect* dst) {
    if (dst) Blit(t, src, dst->x, dst->y, dst->w, dst->h, 0.0, FlipNone);
}

// The UI pass only rotates about the centre (center == null).
void UIPreviewPainter::CopyEx(Tex t, const okay::ui2d::Rect* src, const okay::ui2d::Rect* dst, double angle,
                              const Point*, int flip) {
    if (dst) Blit(t, src, (float)dst->x, (float)dst->y, (float)dst->w, (float)dst->h, angle, flip);
}

void UIPreviewPainter::CopyExF(Tex t, const okay::ui2d::Rect* src, const FRect* dst, double angle,
                               const FPoint*, int flip) {
    if (dst) Blit(t, src, dst->x, dst->y, dst->w, dst->h, angle, flip);
}

void UIPreviewPainter::Geometry(Tex t, const Vertex* v, int nv, const int* idx, int ni) {
    if (!v || nv <= 0) return;
    const int count = idx ? ni : nv;
    if (count <= 0) return;
    const bool textured = t != nullptr;
    if (textured) m_dl->PushTexture(ImTextureRef((ImTextureID)(intptr_t)t));
    const ImVec2 white = ImGui::GetFontTexUvWhitePixel();
    m_dl->PrimReserve(count, nv);   // may start a new vertex offset: read the base after
    const unsigned base = m_dl->_VtxCurrentIdx;
    for (int i = 0; i < nv; ++i) {
        const ImU32 c = IM_COL32(v[i].color.r, v[i].color.g, v[i].color.b, v[i].color.a);
        m_dl->PrimWriteVtx(P(v[i].position.x, v[i].position.y),
                           textured ? ImVec2(v[i].tex_coord.x, v[i].tex_coord.y) : white,
                           textured ? Tint(t, c) : c);
    }
    for (int i = 0; i < count; ++i) m_dl->PrimWriteIdx((ImDrawIdx)(base + (idx ? idx[i] : i)));
    if (textured) m_dl->PopTexture();
}

void UIPreviewPainter::MissingTexture(const std::string& path, const okay::ui2d::Rect& where) {
    if (where.w <= 0 || where.h <= 0) return;
    const ImVec2 a = P((float)where.x, (float)where.y), b = P((float)(where.x + where.w), (float)(where.y + where.h));
    m_dl->AddRect(a, b, IM_COL32(255, 255, 255, 90));
    m_dl->PushClipRect(a, b, true);
    const std::string label = "missing: " + path;
    m_dl->AddText(ImVec2(a.x + 4.0f, a.y + 4.0f), IM_COL32(255, 150, 120, 220), label.c_str());
    m_dl->PopClipRect();
}

} // namespace oked
