// OkaySpace Player — the standalone runtime that runs a built game.
//
//   okay-player [scene.okayscene]
//
// With no argument it loads "game.okayscene" next to the executable. The editor's
// "Build Game" writes that file and copies this player beside it, so a shipped
// game is just <Game>.exe + game.okayscene (+ any assets).
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <Okay.hpp>
#include "okay/Render/GLRenderer.hpp"    // optional GPU (OpenGL) 3D renderer
#include "okay/Render/D3D11Renderer.hpp" // optional GPU (Direct3D 11) 3D renderer (Windows)
#ifdef OKAY_HAVE_OKAYUI
#include "okay/UI/OkayUI.hpp"
#include "OkayScriptUIBridge.hpp"
#include "OkayTestPanel.hpp"
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace okay;

static SDL_Point W2S(const Vec3& p, const Vec3& camPos, float scale, int w, int h) {
    return SDL_Point{(int)(w * 0.5f + (p.x - camPos.x) * scale),
                     (int)(h * 0.5f - (p.y - camPos.y) * scale)};
}

// Fill a UI shape (rectangle / rounded / circle / pill) into screen rect `r`,
// scanline by scanline so any silhouette uses one code path. Supports a linear
// gradient (top->bottom, or left->right when `horizontal`); pass equal colors for
// a flat fill. `op` is the canvas master opacity.
static void FillUIShape(SDL_Renderer* ren, const SDL_Rect& r, UIShape shape, float radius,
                        const Color& top, const Color& bottom, bool gradient, bool horizontal,
                        float op) {
    if (r.w <= 0 || r.h <= 0) return;
    auto lerp = [](const Color& a, const Color& b, float t) {
        return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                     a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
    };
    for (int row = 0; row < r.h; ++row) {
        float x0, x1;
        if (!UIShapeRowSpan(shape, (float)r.w, (float)r.h, radius, row, x0, x1)) continue;
        if (!gradient) {
            SDL_SetRenderDrawColor(ren, (Uint8)(top.r * 255), (Uint8)(top.g * 255),
                                   (Uint8)(top.b * 255), (Uint8)(top.a * 255 * op));
            SDL_Rect span{r.x + (int)x0, r.y + row, (int)(x1 - x0) + 1, 1};
            SDL_RenderFillRect(ren, &span);
        } else if (!horizontal) {
            float t = r.h > 1 ? (float)row / (r.h - 1) : 0.0f;
            Color c = lerp(top, bottom, t);
            SDL_SetRenderDrawColor(ren, (Uint8)(c.r * 255), (Uint8)(c.g * 255),
                                   (Uint8)(c.b * 255), (Uint8)(c.a * 255 * op));
            SDL_Rect span{r.x + (int)x0, r.y + row, (int)(x1 - x0) + 1, 1};
            SDL_RenderFillRect(ren, &span);
        } else {
            // Horizontal gradient: step across the span pixel-cluster by cluster.
            int ix0 = (int)x0, ix1 = (int)x1;
            for (int x = ix0; x <= ix1; ++x) {
                float t = r.w > 1 ? (float)x / (r.w - 1) : 0.0f;
                Color c = lerp(top, bottom, t);
                SDL_SetRenderDrawColor(ren, (Uint8)(c.r * 255), (Uint8)(c.g * 255),
                                       (Uint8)(c.b * 255), (Uint8)(c.a * 255 * op));
                SDL_Rect px{r.x + x, r.y + row, 1, 1};
                SDL_RenderFillRect(ren, &px);
            }
        }
    }
}

// Draw a drop shadow for a UI shape. softness == 0 is a crisp shadow; softness > 0
// fakes a blur by stacking a few expanding, fading copies into a soft penumbra.
static void FillUIShadow(SDL_Renderer* ren, const SDL_Rect& r, UIShape shape, float radius,
                         const Color& color, float softness, float op) {
    if (softness <= 0.0f) {
        FillUIShape(ren, r, shape, radius, color, color, false, false, op);
        return;
    }
    const int layers = 5;
    for (int k = layers; k >= 1; --k) {
        float grow = softness * (float)k / layers;
        SDL_Rect r2{r.x - (int)grow, r.y - (int)grow, r.w + (int)(2 * grow), r.h + (int)(2 * grow)};
        Color c = color; c.a = color.a * (0.6f / layers);     // accumulate toward the edge
        FillUIShape(ren, r2, shape, radius + grow, c, c, false, false, op);
    }
    FillUIShape(ren, r, shape, radius, color, color, false, false, op);   // solid core
}

// A stable, distinct color for each non-zero tile id (no palette is stored).
static SDL_Color TileColor(int id) {
    unsigned h = (unsigned)id * 2654435761u;
    return SDL_Color{(Uint8)(80 + (h & 0x7F)), (Uint8)(80 + ((h >> 8) & 0x7F)),
                     (Uint8)(80 + ((h >> 16) & 0x7F)), 255};
}

// Draw a world-space axis-aligned quad (used for tiles and particles).
static void FillWorldQuad(SDL_Renderer* r, const Vec3& center, float wWorld, float hWorld,
                          const Vec3& camPos, float scale, int w, int h, SDL_Color col) {
    SDL_Point c = W2S(center, camPos, scale, w, h);
    int hw = (int)(wWorld * 0.5f * scale), hh = (int)(hWorld * 0.5f * scale);
    SDL_Rect rect{c.x - hw, c.y - hh, hw * 2 > 0 ? hw * 2 : 1, hh * 2 > 0 ? hh * 2 : 1};
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    SDL_RenderFillRect(r, &rect);
}

// --- Small raster helpers for the minimap (filled circle / ring / triangle). ----
static void MMFillCircle(SDL_Renderer* r, int cx, int cy, int rad, SDL_Color col) {
    if (rad < 1) rad = 1;
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    for (int dy = -rad; dy <= rad; ++dy) {
        int dx = (int)std::sqrt((double)(rad * rad - dy * dy));
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}
static void MMDrawRing(SDL_Renderer* r, int cx, int cy, int rad, int width, SDL_Color col) {
    if (rad < 1) rad = 1; if (width < 1) width = 1;
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    const int seg = 64;
    int prevx = cx + rad, prevy = cy;
    for (int i = 1; i <= seg; ++i) {
        float a = (float)i / seg * 6.2831853f;
        int x = cx + (int)(std::cos(a) * rad), y = cy + (int)(std::sin(a) * rad);
        for (int w2 = 0; w2 < width; ++w2) SDL_RenderDrawLine(r, prevx, prevy + w2, x, y + w2);
        prevx = x; prevy = y;
    }
}
static void MMFillTriangle(SDL_Renderer* r, SDL_Point a, SDL_Point b, SDL_Point c, SDL_Color col) {
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    int minY = std::min({a.y, b.y, c.y}), maxY = std::max({a.y, b.y, c.y});
    auto edgeX = [](SDL_Point p, SDL_Point q, int y, float& x) -> bool {
        if (p.y == q.y) return false;
        if (y < std::min(p.y, q.y) || y > std::max(p.y, q.y)) return false;
        float t = (float)(y - p.y) / (float)(q.y - p.y);
        x = p.x + t * (q.x - p.x); return true;
    };
    for (int y = minY; y <= maxY; ++y) {
        float xs[3]; int n = 0; float xv;
        if (edgeX(a, b, y, xv)) xs[n++] = xv;
        if (edgeX(b, c, y, xv)) xs[n++] = xv;
        if (edgeX(c, a, y, xv)) xs[n++] = xv;
        if (n >= 2) {
            int x0 = (int)std::min(xs[0], xs[1]), x1 = (int)std::max(xs[0], xs[1]);
            SDL_RenderDrawLine(r, x0, y, x1, y);
        }
    }
}

// Upload a TTF glyph atlas as an SDL texture (once, cached) for the player.
static SDL_Texture* TtfAtlasTexture(SDL_Renderer* r, okay::TtfFont* f) {
    if (!f || !f->Valid()) return nullptr;
    static std::unordered_map<okay::TtfFont*, SDL_Texture*> cache;
    auto it = cache.find(f);
    if (it != cache.end()) return it->second;
    const okay::Image& a = f->Atlas();
    SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888,
                                         SDL_TEXTUREACCESS_STATIC, a.Width(), a.Height());
    if (tex) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
        SDL_UpdateTexture(tex, nullptr, a.Data(), a.Width() * 4);
    }
    cache[f] = tex;
    return tex;
}

// Draw a string with a TTF font (a textured glyph quad per character), tinted by col.
static void DrawTtfText(SDL_Renderer* r, okay::TtfFont* f, const std::string& text,
                        float ox, float oy, float px, SDL_Color col, float letterSp, float lineSp) {
    SDL_Texture* tex = TtfAtlasTexture(r, f);
    if (!tex) return;
    SDL_SetTextureColorMod(tex, col.r, col.g, col.b);
    SDL_SetTextureAlphaMod(tex, col.a);
    const float s = px * (float)okay::Font8x8::Height / f->BakeHeight();
    const float baseline = f->Ascent((float)okay::Font8x8::Height) * px;
    float penX = ox, penY = oy;
    for (char ch : text) {
        if (ch == '\n') { penY += f->LineHeight((float)okay::Font8x8::Height) * px + lineSp * px; penX = ox; continue; }
        const okay::TtfFont::Glyph* g = f->Get(ch);
        if (!g) { penX += px * 4.0f; continue; }
        int gw = g->x1 - g->x0, gh = g->y1 - g->y0;
        if (gw > 0 && gh > 0) {
            SDL_Rect src{g->x0, g->y0, gw, gh};
            SDL_FRect dst{penX + g->xoff * s, penY + baseline + g->yoff * s, gw * s, gh * s};
            SDL_RenderCopyF(r, tex, &src, &dst);
        }
        penX += g->xadvance * s + letterSp * px;
    }
}

// Scene-wide default UI font (from Scene::uiFont): DrawText falls back to it when no
// explicit font is given. Set around the screen-UI pass and cleared after, so world
// text without its own font keeps the bitmap font (matching the editor preview).
static okay::TtfFont* g_uiDefaultFont = nullptr;

// Draw a string with the built-in 8x8 font as filled rects, top-left at (ox, oy)
// in screen pixels, each font pixel `px` screen pixels wide. When `ttf` is loaded,
// the TTF atlas is used instead (same sizing space).
static void DrawText(SDL_Renderer* r, const std::string& text, float ox, float oy,
                     float px, SDL_Color col, float letterSp = 0.0f, float lineSp = 0.0f,
                     bool italic = false, bool gradient = false, SDL_Color col2 = SDL_Color{0, 0, 0, 0},
                     okay::TtfFont* ttf = nullptr) {
    if (px < 1.0f) px = 1.0f;
    if (!ttf) ttf = g_uiDefaultFont;   // scene-wide default UI font (set during the UI pass)
    if (ttf && ttf->Valid()) { DrawTtfText(r, ttf, text, ox, oy, px, col, letterSp, lineSp); return; }
    if (!gradient) SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    const float slant = italic ? 0.30f : 0.0f;   // px shift per row up from the baseline
    float cx = ox;
    for (char ch : text) {
        if (ch == '\n') { oy += (Font8x8::Height + 1 + lineSp) * px; cx = ox; continue; }
        for (int y = 0; y < Font8x8::Height; ++y) {
            if (gradient) {   // top row uses col, bottom row uses col2
                float t = (float)y / (float)(Font8x8::Height - 1);
                SDL_SetRenderDrawColor(r, (Uint8)(col.r + (col2.r - col.r) * t),
                                       (Uint8)(col.g + (col2.g - col.g) * t),
                                       (Uint8)(col.b + (col2.b - col.b) * t),
                                       (Uint8)(col.a + (col2.a - col.a) * t));
            }
            float sx = (Font8x8::Height - 1 - y) * slant * px;   // italic: higher rows lean right
            for (int x = 0; x < Font8x8::Width; ++x)
                if (Font8x8::Pixel(ch, x, y)) {
                    SDL_Rect cell{(int)(cx + x * px + sx), (int)(oy + y * px),
                                  (int)px + 1, (int)px + 1};
                    SDL_RenderFillRect(r, &cell);
                }
        }
        cx += (Font8x8::Width + 1 + letterSp) * px; // inter-glyph gap + letter spacing
    }
}

// Load (and cache) a sprite texture. Returns nullptr if the image can't be read,
// in which case the caller falls back to a flat colored quad. A null cache entry
// is stored for misses so we don't retry decoding a bad path every frame.
static SDL_Texture* GetTexture(SDL_Renderer* r, const std::string& path,
                               const std::string& baseDir,
                               std::unordered_map<std::string, SDL_Texture*>& cache) {
    if (path.empty()) return nullptr;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;

    Image img;
    if (!img.Load(path) && !img.Load(baseDir + path)) {
        cache[path] = nullptr; // remember the miss
        return nullptr;
    }
    SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888,
                                         SDL_TEXTUREACCESS_STATIC, img.Width(), img.Height());
    if (tex) {
        SDL_UpdateTexture(tex, nullptr, img.Data(), img.Width() * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }
    cache[path] = tex;
    return tex;
}

// Full-screen loading-screen overlay (background + centred title + tip + progress bar).
static void DrawLoadingScreen(SDL_Renderer* r, okay::LoadingScreen& ls, const std::string& baseDir,
                              std::unordered_map<std::string, SDL_Texture*>& cache) {
    int W = 0, H = 0; SDL_GetRendererOutputSize(r, &W, &H);
    if (W <= 0 || H <= 0) return;
    float a = ls.Alpha(); a = a < 0 ? 0 : (a > 1 ? 1 : a);
    const Uint8 A = (Uint8)(255.0f * a);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Rect full{0, 0, W, H};
    SDL_Texture* bgTex = ls.backgroundImage.empty() ? nullptr : GetTexture(r, ls.backgroundImage, baseDir, cache);
    if (bgTex) { SDL_SetTextureAlphaMod(bgTex, A); SDL_RenderCopy(r, bgTex, nullptr, &full); }
    else if (ls.gradientBackground) {
        const Color& b0 = ls.backgroundColor; const Color& b1 = ls.backgroundColor2;
        const int bands = 64;
        for (int i = 0; i < bands; ++i) {
            float t = (float)i / (bands - 1);
            SDL_SetRenderDrawColor(r, (Uint8)((b0.r + (b1.r - b0.r) * t) * 255),
                                   (Uint8)((b0.g + (b1.g - b0.g) * t) * 255),
                                   (Uint8)((b0.b + (b1.b - b0.b) * t) * 255), A);
            SDL_Rect band{0, (int)(H * i / (float)bands), W, H / bands + 1};
            SDL_RenderFillRect(r, &band);
        }
    }
    else {
        const Color& b = ls.backgroundColor;
        SDL_SetRenderDrawColor(r, (Uint8)(b.r * 255), (Uint8)(b.g * 255), (Uint8)(b.b * 255), A);
        SDL_RenderFillRect(r, &full);
    }
    auto sc = [](const Color& c, Uint8 al) {
        return SDL_Color{(Uint8)(c.r * 255), (Uint8)(c.g * 255), (Uint8)(c.b * 255), al};
    };
    const float adv = (float)(Font8x8::Width + 1);
    if (ls.showTitle && !ls.title.empty()) {
        float px = H * 0.006f * ls.titleScale; if (px < 2.0f) px = 2.0f;
        float tw = ls.title.size() * adv * px;
        DrawText(r, ls.title, (W - tw) * 0.5f, H * ls.titleY, px, sc(ls.titleColor, A));
    }
    if (!ls.CurrentTip().empty()) {
        float px = H * 0.0032f * ls.tipScale; if (px < 1.0f) px = 1.0f;
        float tw = ls.CurrentTip().size() * adv * px;
        DrawText(r, ls.CurrentTip(), (W - tw) * 0.5f, H * ls.tipY, px, sc(ls.tipColor, A));
    }
    if (ls.showBar) {
        int bw = (int)(W * ls.barWidth), bh = (int)(H * ls.barHeight); if (bh < 4) bh = 4;
        int bx = (W - bw) / 2, by = (int)(H * ls.barY);
        SDL_Rect bgr{bx, by, bw, bh};
        UIShape shp = ls.barRadius > 0.5f ? UIShape::Rounded : UIShape::Rectangle;
        FillUIShape(r, bgr, shp, ls.barRadius, ls.barBackground, ls.barBackground, false, false, a);
        SDL_Rect fr{bx, by, (int)(bw * ls.Progress()), bh};
        if (fr.w > 0) FillUIShape(r, fr, shp, ls.barRadius, ls.barFill, ls.barFill, false, false, a);
        if (ls.barBorder) {
            SDL_SetRenderDrawColor(r, (Uint8)(ls.barBorderColor.r*255), (Uint8)(ls.barBorderColor.g*255),
                                   (Uint8)(ls.barBorderColor.b*255), A);
            SDL_RenderDrawRect(r, &bgr);
        }
        if (ls.showPercent) {
            char pc[8]; std::snprintf(pc, sizeof(pc), "%d%%", (int)(ls.Progress() * 100.0f + 0.5f));
            float px = H * 0.0030f; if (px < 1.0f) px = 1.0f;
            DrawText(r, pc, bx + bw + 8, by + (bh - (Font8x8::Height) * px) * 0.5f, px, sc(ls.textColor, A));
        }
    }
    if (ls.showSpinner) {
        float ccx = W * 0.5f, ccy = H * ls.spinnerY, rad = H * ls.spinnerRadius;
        float ds = ls.spinnerDotSize * (H / 720.0f); if (ds < 2.0f) ds = 2.0f;
        const int dots = 8;
        float phase = ls.Elapsed() * ls.spinnerSpeed;
        for (int i = 0; i < dots; ++i) {
            float ang = (float)i / dots * 6.2831853f;
            float dx = ccx + std::cos(ang) * rad, dy = ccy + std::sin(ang) * rad;
            float bright = 0.25f + 0.75f * (0.5f + 0.5f * std::cos(ang - phase));   // chase
            SDL_SetRenderDrawColor(r, (Uint8)(ls.spinnerColor.r*255), (Uint8)(ls.spinnerColor.g*255),
                                   (Uint8)(ls.spinnerColor.b*255), (Uint8)(A * bright));
            SDL_Rect d{(int)(dx - ds*0.5f), (int)(dy - ds*0.5f), (int)ds, (int)ds};
            SDL_RenderFillRect(r, &d);
        }
    }
}

// A Minecraft-style hotbar (+ openable backpack) drawn from an Inventory.
static void DrawInventoryUI(SDL_Renderer* r, okay::InventoryUI& ui, const std::string& baseDir,
                            std::unordered_map<std::string, SDL_Texture*>& cache) {
    okay::Inventory* inv = ui.Inv();
    int W = 0, H = 0; SDL_GetRendererOutputSize(r, &W, &H);
    if (W <= 0 || H <= 0) return;
    const int n = ui.hotbarSlots < 1 ? 1 : ui.hotbarSlots;
    // Resolution-independent UI: scale by the window height vs the authored reference,
    // so the inventory keeps the same on-screen proportion in a small built-game window
    // as it does in the (usually larger) editor Game view. Unity's "Scale With Screen Size".
    const float uiScale = (ui.scaleToScreen && ui.referenceHeight > 1.0f) ? (float)H / ui.referenceHeight : 1.0f;
    const float sz = ui.slotSize * uiScale, gap = ui.slotGap * uiScale;
    const float marginX = ui.marginX * uiScale, marginY = ui.marginY * uiScale, panelPad = ui.panelPad * uiScale;
    auto setc = [&](const Color& c, int a = -1) {
        SDL_SetRenderDrawColor(r, (Uint8)(c.r * 255), (Uint8)(c.g * 255), (Uint8)(c.b * 255),
                               (Uint8)(a < 0 ? c.a * 255 : a));
    };
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const float cr = ui.cornerRadius;
    const UIShape shp = cr > 0.5f ? UIShape::Rounded : UIShape::Rectangle;
    // Draw one slot at (x,y) for inventory stack index `idx`, highlighted if selected.
    auto slot = [&](float x, float y, int idx, bool sel) {
        float b = (ui.borderWidth < 0 ? 0 : ui.borderWidth) + (sel ? 1.0f : 0.0f);
        SDL_Rect outer{(int)x, (int)y, (int)sz, (int)sz};
        Color bc = sel ? ui.selectedColor : ui.slotBorder;
        if (!sel && inv && idx >= 0 && idx < (int)inv->slots.size() && !inv->slots[idx].item.empty())
            if (const Color* rc = ui.RarityOf(inv->slots[idx].item)) bc = *rc;   // rarity tint
        FillUIShape(r, outer, shp, cr, bc, bc, false, false, 1.0f);
        SDL_Rect inner{(int)(x + b), (int)(y + b), (int)(sz - 2 * b), (int)(sz - 2 * b)};
        float ir = cr > 0.5f ? (cr - b < 0 ? 0 : cr - b) : 0.0f;
        FillUIShape(r, inner, ir > 0.5f ? UIShape::Rounded : UIShape::Rectangle, ir, ui.slotColor, ui.slotColor, false, false, 1.0f);
        // Count shown in the slot: the source of a split-drag still shows its remainder
        // (a whole-stack drag lifts everything, so the source draws empty).
        int shown = (inv && idx >= 0 && idx < (int)inv->slots.size()) ? inv->slots[idx].count : 0;
        if (idx == ui.dragIndex) shown -= (ui.dragAmount > 0 ? ui.dragAmount : shown);
        if (shown > 0 && inv && idx >= 0 && idx < (int)inv->slots.size() && !inv->slots[idx].item.empty()) {
            const auto& it = inv->slots[idx];
            SDL_Texture* icon = ui.iconFolder.empty() ? nullptr
                              : GetTexture(r, ui.iconFolder + it.item + ".png", baseDir, cache);
            if (icon) { int ins = (int)(ui.iconInset * uiScale); SDL_Rect d{inner.x + ins, inner.y + ins, inner.w - 2 * ins, inner.h - 2 * ins}; SDL_RenderCopy(r, icon, nullptr, &d); }
            else if (ui.showNames) {
                std::string nm = it.item.substr(0, ui.nameChars < 1 ? 1 : (std::size_t)ui.nameChars);
                float px = (sz - 8.0f) / ((float)nm.size() * (Font8x8::Width + 1)) * ui.labelScale;
                if (px < 1.0f) px = 1.0f; if (px > sz * 0.05f) px = sz * 0.05f;   // fit the slot
                DrawText(r, nm, x + 4, y + 5, px,
                         SDL_Color{(Uint8)(ui.textColor.r*255),(Uint8)(ui.textColor.g*255),(Uint8)(ui.textColor.b*255),255});
            }
            if (ui.showCounts && shown > 1) {
                std::string cs = std::to_string(shown);
                float px = sz * 0.038f * ui.labelScale; if (px < 1.0f) px = 1.0f;
                float tw = cs.size() * (Font8x8::Width + 1) * px;
                DrawText(r, cs, x + sz - tw - 3, y + sz - (Font8x8::Height + 1) * px - 3, px,
                         SDL_Color{(Uint8)(ui.countColor.r*255),(Uint8)(ui.countColor.g*255),(Uint8)(ui.countColor.b*255),255});
            }
        }
    };
    // Layout: hotbar docked to the bottom (or top), nudged by margins; the backpack
    // grows away from the docked edge.
    const float rowW = n * sz + (n - 1) * gap;
    const float hx = (W - rowW) * 0.5f + marginX;
    const float hy = ui.anchorTop ? marginY : H - sz - marginY;
    auto bpY = [&](int row) {
        return ui.anchorTop ? hy + sz + 14.0f + row * (sz + gap)
                            : hy - 14.0f - (ui.backpackRows - row) * (sz + gap);
    };
    auto panel = [&](float x, float y, float w, float h) {
        SDL_Rect rc{(int)x, (int)y, (int)w, (int)h};
        FillUIShape(r, rc, cr > 0.5f ? UIShape::Rounded : UIShape::Rectangle, cr + 2.0f,
                    ui.panelColor, ui.panelColor, false, false, 1.0f);
    };
    // Backpack (when open): darken, then the grid.
    if (ui.open && ui.backpackRows > 0) {
        if (ui.darkenWhenOpen) { setc(Color::FromBytes(0, 0, 0, 150)); SDL_Rect full{0, 0, W, H}; SDL_RenderFillRect(r, &full); }
        if (ui.showPanel) {
            float t = bpY(0), b = bpY(ui.backpackRows - 1);
            float top = t < b ? t : b, bot = (t > b ? t : b) + sz, pad = panelPad;
            panel(hx - pad, top - pad, rowW + 2 * pad, (bot - top) + 2 * pad);
        }
        for (int row = 0; row < ui.backpackRows; ++row)
            for (int c = 0; c < n; ++c)
                slot(hx + c * (sz + gap), bpY(row), n + row * n + c, false);
    }
    if (ui.showPanel) panel(hx - panelPad, hy - panelPad, rowW + 2 * panelPad, sz + 2 * panelPad);
    for (int i = 0; i < n; ++i) slot(hx + i * (sz + gap), hy, i, i == ui.selected);
    // Hotbar slot numbers (1–9) in the top-left corner.
    if (ui.slotNumbers) {
        float px = sz * 0.030f * ui.labelScale; if (px < 1.0f) px = 1.0f;
        for (int i = 0; i < n && i < 9; ++i)
            DrawText(r, std::to_string(i + 1), hx + i * (sz + gap) + 3, hy + 2, px,
                     SDL_Color{(Uint8)(ui.numberColor.r*255),(Uint8)(ui.numberColor.g*255),(Uint8)(ui.numberColor.b*255),255});
    }
    // Tooltip helper: a small panel near the cursor with the item's name + count.
    okay::Vec2 mpos = okay::Input::MousePosition();
    auto drawTip = [&](const std::string& label) {
        if (!ui.showTooltips || label.empty()) return;
        float px = sz * 0.030f * ui.labelScale; if (px < 1.0f) px = 1.0f;
        float tw = label.size() * (Font8x8::Width + 1) * px, th = Font8x8::Height * px, pad = 6.0f;
        SDL_Rect bg{(int)(mpos.x + 14), (int)(mpos.y + 14), (int)(tw + pad * 2), (int)(th + pad * 2)};
        FillUIShape(r, bg, UIShape::Rounded, 4.0f, ui.tooltipColor, ui.tooltipColor, false, false, 1.0f);
        DrawText(r, label, bg.x + pad, bg.y + pad, px,
                 SDL_Color{(Uint8)(ui.tooltipText.r*255),(Uint8)(ui.tooltipText.g*255),(Uint8)(ui.tooltipText.b*255),255});
    };
    auto slotLabel = [&](const Inventory::Slot& it) {
        return it.item + (it.count > 1 ? " x" + std::to_string(it.count) : std::string());
    };
    // Hotbar tooltip on hover (shown even when the backpack is closed).
    if (inv && ui.showTooltips && !ui.open && ui.dragIndex < 0)
        for (int i = 0; i < n && i < (int)inv->slots.size(); ++i) {
            float sx = hx + i * (sz + gap);
            if (mpos.x >= sx && mpos.x < sx + sz && mpos.y >= hy && mpos.y < hy + sz &&
                !inv->slots[i].item.empty()) { drawTip(slotLabel(inv->slots[i])); break; }
        }
    // Minecraft-style held-item name above (or below) the hotbar.
    if (ui.showSelectedName) {
        std::string nm = ui.SelectedItem();
        if (!nm.empty()) {
            float px = 2.0f * ui.labelScale * uiScale; if (px < 1.0f) px = 1.0f;
            float tw = nm.size() * (Font8x8::Width + 1) * px;
            float ty = ui.anchorTop ? hy + sz + panelPad + 4.0f : hy - (Font8x8::Height + 1) * px - panelPad - 4.0f;
            DrawText(r, nm, hx + rowW * 0.5f - tw * 0.5f, ty, px,
                     SDL_Color{(Uint8)(ui.textColor.r*255),(Uint8)(ui.textColor.g*255),(Uint8)(ui.textColor.b*255),255});
        }
    }

    // Drag-and-drop (only while the backpack is open). Pick a slot under the cursor,
    // drop it on the NEAREST slot (forgiving — gaps/imperfect aim still land cleanly).
    if (!ui.open || !ui.dragItems) { ui.dragIndex = -1; ui.dragAmount = 0; return; }
    auto slotXY = [&](int idx, float& sx, float& sy) {
        if (idx < n) { sx = hx + idx * (sz + gap); sy = hy; }
        else { int b = idx - n; sx = hx + (b % n) * (sz + gap); sy = bpY(b / n); }
    };
    const int slotCount = n + ui.backpackRows * n;
    // Which slot a point picks: the slot it's actually inside (so hovering over a
    // slot always picks THAT slot), else — for gaps / imperfect aim — the slot whose
    // centre is nearest, within a slot's reach. Containment wins so the cursor sitting
    // between two rows never snaps to the row above the one it's visually over.
    auto nearest = [&](float px, float py) -> int {
        for (int idx = 0; idx < slotCount; ++idx) {
            float sx, sy; slotXY(idx, sx, sy);
            if (px >= sx && px < sx + sz && py >= sy && py < sy + sz) return idx;
        }
        int best = -1; float bestD = (sz + gap) * (sz + gap) * 1.1f;
        for (int idx = 0; idx < slotCount; ++idx) {
            float sx, sy; slotXY(idx, sx, sy);
            float dx = px - (sx + sz * 0.5f), dy = py - (sy + sz * 0.5f);
            float d = dx * dx + dy * dy;
            if (d < bestD) { bestD = d; best = idx; }
        }
        return best;
    };
    okay::Vec2 mp = okay::Input::MousePosition();
    auto hilite = [&](int idx, const Color& c) {
        if (idx < 0) return; float sx, sy; slotXY(idx, sx, sy);
        SDL_Rect hr{(int)sx, (int)sy, (int)sz, (int)sz};
        FillUIShape(r, hr, cr > 0.5f ? UIShape::Rounded : UIShape::Rectangle, cr, c, c, false, false, 1.0f);
    };
    // Highlight the target slot: the one under the cursor while hovering, or the
    // nearest slot the dragged item will drop into.
    if (ui.dragIndex < 0) {
        int hi = nearest(mp.x, mp.y);
        hilite(hi, ui.hoverColor);
        if (inv && hi >= 0 && hi < (int)inv->slots.size() && !inv->slots[hi].item.empty())
            drawTip(slotLabel(inv->slots[hi]));
    }
    else { int t = nearest(mp.x, mp.y);
        if (t >= 0 && t != ui.dragIndex)
            hilite(t, Color{ui.selectedColor.r, ui.selectedColor.g, ui.selectedColor.b, 0.40f}); }
    if (ui.dragIndex < 0) {
        int idx = nearest(mp.x, mp.y);
        bool onItem = idx >= 0 && idx < (int)inv->slots.size() && !inv->slots[idx].item.empty();
        if (onItem && okay::Input::GetMouseButtonDown(0)) {
            if (ui.shiftQuickMove && okay::Input::GetKey(okay::Input::KeyShift)) ui.QuickMove(idx);
            else { ui.dragIndex = idx; ui.dragAmount = 0; }           // drag the whole stack
        } else if (onItem && ui.splitRightClick && okay::Input::GetMouseButtonDown(1) && inv->slots[idx].count > 1) {
            ui.dragIndex = idx; ui.dragAmount = ui.splitHalf ? inv->slots[idx].count / 2 : 1;   // pick up half (or one)
        }
    }
    // Drop when the held button is released (either button — you held one to drag).
    if (ui.dragIndex >= 0 && (okay::Input::GetMouseButtonUp(0) || okay::Input::GetMouseButtonUp(1))) {
        ui.ApplyDrag(ui.dragIndex, nearest(mp.x, mp.y));
        ui.dragIndex = -1; ui.dragAmount = 0;
    }
    if (ui.dragIndex >= 0 && ui.dragIndex < (int)inv->slots.size()) {
        const auto& it = inv->slots[ui.dragIndex];
        float x = mp.x - sz * 0.5f, y = mp.y - sz * 0.5f;
        slot(x, y, -2, false);   // an empty slot frame floating under the cursor
        SDL_Texture* icon = ui.iconFolder.empty() ? nullptr : GetTexture(r, ui.iconFolder + it.item + ".png", baseDir, cache);
        if (icon) { SDL_Rect d{(int)x + 4, (int)y + 4, (int)sz - 8, (int)sz - 8}; SDL_RenderCopy(r, icon, nullptr, &d); }
        else {
            std::string nm = it.item.substr(0, ui.nameChars < 1 ? 1 : (std::size_t)ui.nameChars);
            float px = (sz - 8.0f) / ((float)nm.size() * (Font8x8::Width + 1)) * ui.labelScale;
            if (px < 1.0f) px = 1.0f; if (px > sz * 0.05f) px = sz * 0.05f;
            DrawText(r, nm, x + 4, y + 5, px,
                     SDL_Color{(Uint8)(ui.textColor.r*255),(Uint8)(ui.textColor.g*255),(Uint8)(ui.textColor.b*255),255});
        }
        // The carried count (the split amount, or the whole stack).
        int carried = ui.dragAmount > 0 ? ui.dragAmount : it.count;
        if (ui.showCounts && carried > 1) {
            std::string ccs = std::to_string(carried);
            float px = sz * 0.038f * ui.labelScale; if (px < 1.0f) px = 1.0f;
            float tw = ccs.size() * (Font8x8::Width + 1) * px;
            DrawText(r, ccs, x + sz - tw - 3, y + sz - (Font8x8::Height + 1) * px - 3, px,
                     SDL_Color{(Uint8)(ui.countColor.r*255),(Uint8)(ui.countColor.g*255),(Uint8)(ui.countColor.b*255),255});
        }
    }
}

// A DayZ/Unturned grid inventory: multi-cell items, drag-and-drop within the grid.
static void DrawGridInventory(SDL_Renderer* r, okay::GridInventoryUI& ui, const std::string& baseDir,
                              std::unordered_map<std::string, SDL_Texture*>& cache) {
    okay::GridInventory* inv = ui.Inv();
    if (!inv || !inv->open) { ui.dragIndex = -1; return; }
    int W = 0, H = 0; SDL_GetRendererOutputSize(r, &W, &H);
    if (W <= 0 || H <= 0) return;
    const float cs = ui.cellSize, gp = ui.gap;
    float gridW = inv->cols * cs + (inv->cols - 1) * gp;
    float gridH = inv->rows * cs + (inv->rows - 1) * gp;
    float ox = (W - gridW) * 0.5f, oy = (H - gridH) * 0.5f + 10.0f;
    auto setc = [&](const Color& c, int a = -1) {
        SDL_SetRenderDrawColor(r, (Uint8)(c.r * 255), (Uint8)(c.g * 255), (Uint8)(c.b * 255), (Uint8)(a < 0 ? c.a * 255 : a));
    };
    auto sc = [&](const Color& c) { return SDL_Color{(Uint8)(c.r * 255), (Uint8)(c.g * 255), (Uint8)(c.b * 255), 255}; };
    const float cr = ui.cornerRadius;
    auto fill = [&](float x, float y, float w, float h, const Color& c, float rad) {
        SDL_Rect rc{(int)x, (int)y, (int)w, (int)h};
        FillUIShape(r, rc, rad > 0.5f ? UIShape::Rounded : UIShape::Rectangle, rad, c, c, false, false, 1.0f);
    };
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    if (ui.darkenWhenOpen) { setc(Color::FromBytes(0, 0, 0, 150)); SDL_Rect full{0, 0, W, H}; SDL_RenderFillRect(r, &full); }
    okay::Vec2 mp = okay::Input::MousePosition();
    int cx = (int)std::floor((mp.x - ox) / (cs + gp));
    int cy = (int)std::floor((mp.y - oy) / (cs + gp));
    // Panel + a title bar strip.
    fill(ox - 12, oy - 38, gridW + 24, gridH + 50, ui.panelColor, cr + 2);
    fill(ox - 12, oy - 38, gridW + 24, 28, ui.titleBar, cr + 2);
    DrawText(r, inv->title, ox, oy - 31, 2.0f, sc(ui.textColor));
    if (ui.showWeight) {
        char wb[64];
        if (inv->weightLimit > 0.0f) std::snprintf(wb, sizeof(wb), "%.1f / %.0f kg", inv->TotalWeight(), inv->weightLimit);
        else std::snprintf(wb, sizeof(wb), "%.1f kg", inv->TotalWeight());
        float px = 2.0f, tw = (float)std::strlen(wb) * (Font8x8::Width + 1) * px;
        DrawText(r, wb, ox + gridW - tw, oy - 31, px, sc(inv->OverWeight() ? ui.overweightColor : ui.textColor));
    }
    for (int y = 0; y < inv->rows; ++y)
        for (int x = 0; x < inv->cols; ++x)
            fill(ox + x * (cs + gp), oy + y * (cs + gp), cs, cs, ui.cellColor, cr);
    // While dragging, tint the target footprint green (fits) / red (blocked).
    if (ui.dragIndex >= 0 && ui.dragIndex < (int)inv->items.size()) {
        const auto& it = inv->items[ui.dragIndex];
        int tx = cx - ui.grabX, ty = cy - ui.grabY;
        bool ok = inv->CanPlace(tx, ty, it.w, it.h, ui.dragIndex);
        for (int yy = 0; yy < it.h; ++yy)
            for (int xx = 0; xx < it.w; ++xx) {
                int gxc = tx + xx, gyc = ty + yy;
                if (gxc < 0 || gyc < 0 || gxc >= inv->cols || gyc >= inv->rows) continue;
                fill(ox + gxc * (cs + gp), oy + gyc * (cs + gp), cs, cs, ok ? ui.dropOk : ui.dropBad, cr);
            }
    }
    auto drawItem = [&](const okay::GridItem& it, float px0, float py0, bool ghost, bool hover) {
        float w = it.w * cs + (it.w - 1) * gp, h = it.h * cs + (it.h - 1) * gp;
        fill(px0, py0, w, h, ghost ? Color{ui.itemColor.r, ui.itemColor.g, ui.itemColor.b, 0.6f} : ui.itemColor, cr);
        SDL_Rect box{(int)px0, (int)py0, (int)w, (int)h};
        const Color* rar = ui.RarityOf(it.name);
        setc(rar ? *rar : ui.itemBorder, ghost ? 170 : 255); SDL_RenderDrawRect(r, &box);
        if (hover) fill(px0, py0, w, h, ui.hoverColor, cr);
        SDL_Texture* icon = ui.iconFolder.empty() ? nullptr : GetTexture(r, ui.iconFolder + it.name + ".png", baseDir, cache);
        if (icon) { SDL_SetTextureAlphaMod(icon, ghost ? 160 : 255); SDL_Rect d{box.x + 4, box.y + 4, box.w - 8, box.h - 8}; SDL_RenderCopy(r, icon, nullptr, &d); }
        else DrawText(r, it.name.substr(0, 8), box.x + 4, box.y + 5, 1.4f, sc(ui.textColor));
        if (it.count > 1) {
            std::string cstr = std::to_string(it.count); float px = 1.4f;
            float tw = cstr.size() * (Font8x8::Width + 1) * px;
            DrawText(r, cstr, box.x + box.w - tw - 3, box.y + box.h - (Font8x8::Height + 1) * px - 2, px, SDL_Color{255, 255, 255, 255});
        }
    };
    int hovered = (ui.dragIndex < 0 && cx >= 0 && cy >= 0 && cx < inv->cols && cy < inv->rows) ? inv->ItemAtCell(cx, cy) : -1;
    for (int i = 0; i < (int)inv->items.size(); ++i) {
        if (i == ui.dragIndex) continue;
        const auto& it = inv->items[i];
        drawItem(it, ox + it.x * (cs + gp), oy + it.y * (cs + gp), false, i == hovered);
    }
    // Rotate the held item 90° (swap its footprint) — the drop still validates the fit.
    if (ui.rotateKey && ui.dragIndex >= 0 && ui.dragIndex < (int)inv->items.size() &&
        okay::Input::GetKeyDown(ui.rotateKey)) {
        auto& it = inv->items[ui.dragIndex];
        std::swap(it.w, it.h);
        if (ui.grabX >= it.w) ui.grabX = it.w - 1;
        if (ui.grabY >= it.h) ui.grabY = it.h - 1;
    }
    // Hover tooltip: item name, footprint and weight.
    if (ui.showTooltips && hovered >= 0 && hovered < (int)inv->items.size()) {
        const auto& it = inv->items[hovered];
        char tip[96];
        std::snprintf(tip, sizeof(tip), "%s  %dx%d%s  %.1fkg", it.name.c_str(), it.w, it.h,
                      it.count > 1 ? ("  x" + std::to_string(it.count)).c_str() : "", it.weight * it.count);
        float px = 1.4f, tw = (float)std::strlen(tip) * (Font8x8::Width + 1) * px, pad = 6.0f;
        fill(mp.x + 14, mp.y + 14, tw + pad * 2, Font8x8::Height * px + pad * 2, ui.tooltipColor, 4.0f);
        DrawText(r, tip, mp.x + 14 + pad, mp.y + 14 + pad, px, sc(ui.tooltipText));
    }
    // Drag-and-drop: pick an item under the cursor, drop it where it fits.
    if (ui.dragIndex < 0 && okay::Input::GetMouseButtonDown(0)) {
        int idx = (cx >= 0 && cy >= 0 && cx < inv->cols && cy < inv->rows) ? inv->ItemAtCell(cx, cy) : -1;
        if (idx >= 0) { ui.dragIndex = idx; ui.grabX = cx - inv->items[idx].x; ui.grabY = cy - inv->items[idx].y; }
    }
    if (ui.dragIndex >= 0 && okay::Input::GetMouseButtonUp(0)) {
        inv->PlaceAt(ui.dragIndex, cx - ui.grabX, cy - ui.grabY);   // no-op if it doesn't fit
        ui.dragIndex = -1;
    }
    if (ui.dragIndex >= 0 && ui.dragIndex < (int)inv->items.size()) {
        const auto& it = inv->items[ui.dragIndex];
        drawItem(it, mp.x - ui.grabX * (cs + gp) - cs * 0.5f, mp.y - ui.grabY * (cs + gp) - cs * 0.5f, true, false);
    }
}

// Crafting bench panel: lists recipes, tints by affordability, crafts on click.
static void DrawCrafting(SDL_Renderer* r, okay::CraftingStation& cs) {
    if (!cs.open) return;
    okay::Inventory* inv = cs.Inv();
    int W = 0, H = 0; SDL_GetRendererOutputSize(r, &W, &H);
    if (W <= 0 || H <= 0) return;
    const float cr = cs.cornerRadius;
    auto sc = [&](const Color& c) { return SDL_Color{(Uint8)(c.r*255),(Uint8)(c.g*255),(Uint8)(c.b*255),255}; };
    auto fill = [&](float x, float y, float w, float h, const Color& c, float rad) {
        SDL_Rect rc2{(int)x, (int)y, (int)w, (int)h};
        FillUIShape(r, rc2, rad > 0.5f ? UIShape::Rounded : UIShape::Rectangle, rad, c, c, false, false, 1.0f);
    };
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    int n = (int)cs.recipes.size();
    float pw = cs.width, ph = n * cs.rowHeight + 8.0f;
    float px = (W - pw) * 0.5f + cs.marginX, py = (H - ph) * 0.5f + cs.marginY;
    fill(px, py - 26, pw, ph + 26, cs.panelColor, cr + 2);
    fill(px, py - 26, pw, 24, cs.titleBar, cr + 2);
    DrawText(r, cs.title, px + 8, py - 20, 1.6f, sc(cs.textColor));
    okay::Vec2 mp = okay::Input::MousePosition();
    for (int i = 0; i < n; ++i) {
        const auto& rec = cs.recipes[i];
        float ry = py + i * cs.rowHeight + 4.0f;
        bool can = inv && cs.CanCraft(rec, *inv);
        bool hover = mp.x >= px + 4 && mp.x < px + pw - 4 && mp.y >= ry && mp.y < ry + cs.rowHeight - 2;
        fill(px + 4, ry, pw - 8, cs.rowHeight - 2, can ? cs.canColor : cs.cantColor, cr);
        if (hover) fill(px + 4, ry, pw - 8, cs.rowHeight - 2, cs.hoverColor, cr);
        std::string out = rec.output + (rec.outputCount > 1 ? " x" + std::to_string(rec.outputCount) : std::string());
        DrawText(r, out, px + 10, ry + 4, 1.4f, sc(cs.textColor));
        std::string ins;
        for (std::size_t k = 0; k < rec.inputs.size(); ++k) { if (k) ins += ", "; ins += std::to_string(rec.inputs[k].count) + " " + rec.inputs[k].item; }
        float tw = ins.size() * (Font8x8::Width + 1) * 1.0f;
        DrawText(r, ins, px + pw - 10 - tw, ry + 6, 1.0f, sc(cs.textColor));
        if (hover && can && okay::Input::GetMouseButtonDown(0)) cs.Craft(i);
    }
}

// A lootable chest: chest contents on top, your inventory below. Click a stack to
// move the whole thing to the other side (take loot / stash items).
static void DrawChest(SDL_Renderer* r, okay::ChestInventory& ch, const std::string& baseDir,
                      std::unordered_map<std::string, SDL_Texture*>& cache) {
    if (!ch.open) return;
    okay::Inventory* cinv = ch.Inv(); okay::Inventory* pinv = ch.PlayerInv();
    if (!cinv) return;
    int W = 0, H = 0; SDL_GetRendererOutputSize(r, &W, &H); if (W <= 0 || H <= 0) return;
    const float cr = ch.cornerRadius;
    // Chest grid uses the chest's size; the player grid uses the player's OWN inventory
    // UI size/columns — so resizing the chest doesn't resize your inventory.
    float cs = ch.slotSize, cg = ch.gap; int cc = ch.cols < 1 ? 1 : ch.cols;
    okay::InventoryUI* pui = ch.PlayerUI();
    float ps = pui ? pui->slotSize : cs, pg = pui ? pui->slotGap : cg;
    int   pc = pui ? (pui->hotbarSlots < 1 ? 1 : pui->hotbarSlots) : cc;
    int chestN = cinv->capacity > 0 ? cinv->capacity : (int)cinv->slots.size();
    int playN  = pinv ? (pinv->capacity > 0 ? pinv->capacity : (int)pinv->slots.size()) : 0;
    int chestRows = (chestN + cc - 1) / cc, playRows = playN > 0 ? (playN + pc - 1) / pc : 0;
    float chestW = cc * cs + (cc - 1) * cg, playW = pc * ps + (pc - 1) * pg;
    float panelW = chestW > playW ? chestW : playW;
    float chestH = chestRows * (cs + cg), playH = playRows * (ps + pg);
    float totalH = chestH + 30.0f + playH;
    float panelX = (W - panelW) * 0.5f, topY = (H - totalH) * 0.5f + 16.0f;
    float chestOx = panelX + (panelW - chestW) * 0.5f, playOx = panelX + (panelW - playW) * 0.5f;
    float chestGy = topY, playGy = topY + chestH + 24.0f;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    if (ch.darkenWhenOpen) { SDL_SetRenderDrawColor(r, 0, 0, 0, 150); SDL_Rect f{0, 0, W, H}; SDL_RenderFillRect(r, &f); }
    auto sc = [&](const Color& c) { return SDL_Color{(Uint8)(c.r*255),(Uint8)(c.g*255),(Uint8)(c.b*255),255}; };
    auto fill = [&](float x, float y, float w, float h, const Color& c, float rad) {
        SDL_Rect rc2{(int)x, (int)y, (int)w, (int)h};
        FillUIShape(r, rc2, rad > 0.5f ? UIShape::Rounded : UIShape::Rectangle, rad, c, c, false, false, 1.0f);
    };
    okay::Vec2 mp = okay::Input::MousePosition();
    fill(panelX - 12, topY - 34, panelW + 24, totalH + 46, ch.panelColor, cr + 2);
    fill(panelX - 12, topY - 34, panelW + 24, 26, ch.titleBar, cr + 2);
    DrawText(r, ch.title, panelX, topY - 28, 1.6f, sc(ch.textColor));
    int hovSide = -1, hovIdx = -1;   // which grid + slot the cursor is over
    auto grid = [&](okay::Inventory* inv, float gx, float gy, float sz, float gp, int cols, int count, int side) {
        for (int i = 0; i < count; ++i) {
            float x = gx + (i % cols) * (sz + gp), y = gy + (i / cols) * (sz + gp);
            fill(x, y, sz, sz, ch.slotBorder, cr);
            fill(x + 2, y + 2, sz - 4, sz - 4, ch.slotColor, cr > 2 ? cr - 2 : 0);
            bool hov = mp.x >= x && mp.x < x + sz && mp.y >= y && mp.y < y + sz;
            if (hov) { hovSide = side; hovIdx = i; fill(x, y, sz, sz, ch.hoverColor, cr); }
            bool dragged = (side == ch.dragSide && i == ch.dragIndex);   // hide the lifted item
            if (!dragged && inv && i < (int)inv->slots.size() && !inv->slots[i].item.empty()) {
                const auto& it = inv->slots[i];
                SDL_Texture* icon = ch.iconFolder.empty() ? nullptr : GetTexture(r, ch.iconFolder + it.item + ".png", baseDir, cache);
                if (icon) { SDL_Rect d{(int)x+4, (int)y+4, (int)sz-8, (int)sz-8}; SDL_RenderCopy(r, icon, nullptr, &d); }
                else DrawText(r, it.item.substr(0, 5), x + 4, y + 5, 1.0f, sc(ch.textColor));
                if (it.count > 1) { std::string c = std::to_string(it.count); float px = 1.2f;
                    float tw = c.size() * (Font8x8::Width + 1) * px;
                    DrawText(r, c, x + sz - tw - 3, y + sz - (Font8x8::Height + 1) * px - 2, px, SDL_Color{255,255,255,255}); }
            }
        }
    };
    grid(cinv, chestOx, chestGy, cs, cg, cc, chestN, 0);
    DrawText(r, "Your inventory", playOx, topY + chestH + 6, 1.2f, sc(ch.textColor));
    if (pinv) grid(pinv, playOx, playGy, ps, pg, pc, playN, 1);
    auto sideInv = [&](int side) { return side == 0 ? cinv : pinv; };
    float sz = ch.dragSide == 1 ? ps : cs;   // floating-item size = source grid's slot size
    // Start a drag by pressing on a non-empty slot.
    if (ch.dragSide < 0 && hovIdx >= 0 && okay::Input::GetMouseButtonDown(0)) {
        okay::Inventory* inv = sideInv(hovSide);
        if (inv && hovIdx < (int)inv->slots.size() && !inv->slots[hovIdx].item.empty()) {
            ch.dragSide = hovSide; ch.dragIndex = hovIdx;
        }
    }
    // Drop onto the slot under the cursor (move / swap / merge), or cancel if outside.
    if (ch.dragSide >= 0 && okay::Input::GetMouseButtonUp(0)) {
        if (hovIdx >= 0) okay::ChestInventory::MoveStack(sideInv(ch.dragSide), ch.dragIndex, sideInv(hovSide), hovIdx);
        ch.dragSide = -1; ch.dragIndex = -1;
    }
    // The lifted item floats under the cursor.
    if (ch.dragSide >= 0) {
        okay::Inventory* inv = sideInv(ch.dragSide);
        if (inv && ch.dragIndex < (int)inv->slots.size() && !inv->slots[ch.dragIndex].item.empty()) {
            const auto& it = inv->slots[ch.dragIndex];
            float x = mp.x - sz * 0.5f, y = mp.y - sz * 0.5f;
            fill(x, y, sz, sz, ch.slotBorder, cr);
            fill(x + 2, y + 2, sz - 4, sz - 4, ch.slotColor, cr > 2 ? cr - 2 : 0);
            SDL_Texture* icon = ch.iconFolder.empty() ? nullptr : GetTexture(r, ch.iconFolder + it.item + ".png", baseDir, cache);
            if (icon) { SDL_Rect d{(int)x+4, (int)y+4, (int)sz-8, (int)sz-8}; SDL_RenderCopy(r, icon, nullptr, &d); }
            else DrawText(r, it.item.substr(0, 5), x + 4, y + 5, 1.0f, sc(ch.textColor));
            if (it.count > 1) { std::string c = std::to_string(it.count); float px = 1.2f;
                float tw = c.size() * (Font8x8::Width + 1) * px;
                DrawText(r, c, x + sz - tw - 3, y + sz - (Font8x8::Height + 1) * px - 2, px, SDL_Color{255,255,255,255}); }
        } else { ch.dragSide = -1; ch.dragIndex = -1; }
    }
}

int main(int argc, char** argv) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // Resolve where the game files live (beside the executable).
    std::string baseDir;
    { char* base = SDL_GetBasePath(); baseDir = base ? base : ""; if (base) SDL_free(base); }

    // Build settings (written by the editor's Build Settings) live in an optional
    // game.okayconfig beside the exe: window size/title/flags, the scene list (so
    // load_scene_index works) and which scene starts. Sensible defaults if absent.
    struct GameConfig {
        std::string title = "OkaySpace Game";
        int  width = 960, height = 600;
        bool fullscreen = false, borderless = false, resizable = true, vsync = true;
        bool showCursor = true, quitOnEscape = true, showFps = false;
        int  fpsCap = 0;
        float volume = 1.0f;
        bool lockCursor = false, perPixel = false, shadows = false, bloom = false, ssao = false, fxaa = true;
        int  antialias = 1;
        bool gpu = true;   // try the GPU (D3D11/OpenGL) 3D renderer; fall back to software
        std::string startup;
        std::vector<std::string> scenes;
    } cfg;
    {
        std::ifstream cf(baseDir + "game.okayconfig");
        std::string line;
        while (std::getline(cf, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if      (k == "title")      cfg.title = v;
            else if (k == "width")      cfg.width = std::atoi(v.c_str());
            else if (k == "height")     cfg.height = std::atoi(v.c_str());
            else if (k == "fullscreen") cfg.fullscreen = std::atoi(v.c_str()) != 0;
            else if (k == "borderless") cfg.borderless = std::atoi(v.c_str()) != 0;
            else if (k == "resizable")  cfg.resizable = std::atoi(v.c_str()) != 0;
            else if (k == "vsync")      cfg.vsync = std::atoi(v.c_str()) != 0;
            else if (k == "cursor")     cfg.showCursor = std::atoi(v.c_str()) != 0;
            else if (k == "fps_cap")    cfg.fpsCap = std::atoi(v.c_str());
            else if (k == "quit_on_escape") cfg.quitOnEscape = std::atoi(v.c_str()) != 0;
            else if (k == "volume")     cfg.volume = (float)std::atof(v.c_str());
            else if (k == "show_fps")   cfg.showFps = std::atoi(v.c_str()) != 0;
            else if (k == "lock_cursor") cfg.lockCursor = std::atoi(v.c_str()) != 0;
            else if (k == "perpixel")   cfg.perPixel = std::atoi(v.c_str()) != 0;
            else if (k == "shadows")    cfg.shadows = std::atoi(v.c_str()) != 0;
            else if (k == "bloom")      cfg.bloom = std::atoi(v.c_str()) != 0;
            else if (k == "ssao")       cfg.ssao = std::atoi(v.c_str()) != 0;
            else if (k == "fxaa")       cfg.fxaa = std::atoi(v.c_str()) != 0;
            else if (k == "antialias")  cfg.antialias = std::atoi(v.c_str());
            else if (k == "gpu")        cfg.gpu = std::atoi(v.c_str()) != 0;
            else if (k == "startup")    cfg.startup = v;
            else if (k == "scene")      cfg.scenes.push_back(v);
        }
    }
    // Register the build's scenes so scripts can load_scene_index / load_next.
    SceneManager::ClearScenes();
    for (const std::string& s : cfg.scenes) SceneManager::AddScene(baseDir + s);

    // Resolve the scene path: CLI arg > config startup > game.okayscene.
    std::string scenePath;
    if (argc > 1) scenePath = argv[1];
    else if (!cfg.startup.empty()) scenePath = baseDir + cfg.startup;
    else scenePath = baseDir + "game.okayscene";

    // Persistent prefs (high scores, settings) live beside the scene file.
    std::string prefsPath = baseDir + "game.okayprefs";
    Prefs::Load(prefsPath);
    DataAsset::SetBaseDir(baseDir);   // resolve .okaydata assets beside the game

    Scene scene("Game");
    std::string err;
    if (!SceneSerializer::LoadFromFile(scene, scenePath, &err)) {
        SDL_Log("Could not load %s: %s", scenePath.c_str(), err.c_str());
        // Keep running with an empty scene rather than failing outright.
    }

    Uint32 winFlags = SDL_WINDOW_ALLOW_HIGHDPI;
    if (cfg.resizable)  winFlags |= SDL_WINDOW_RESIZABLE;
    if (cfg.fullscreen) winFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (cfg.borderless) winFlags |= SDL_WINDOW_BORDERLESS;
    if (!cfg.showCursor) SDL_ShowCursor(SDL_DISABLE);
    std::string title = !cfg.title.empty() ? cfg.title : scene.Name();
    SDL_Window* window = SDL_CreateWindow(
        title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        cfg.width, cfg.height, winFlags);
    Uint32 renFlags = SDL_RENDERER_ACCELERATED | (cfg.vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, renFlags);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);

    // Apply the build's graphics/quality settings to the renderer + cursor.
    PerPixelLighting() = cfg.perPixel;
    ShadowsEnabled()   = cfg.shadows;
    BloomEnabled()     = cfg.bloom;
    SSAOEnabled()      = cfg.ssao;
    FXAAEnabled()      = cfg.fxaa;
    if (cfg.lockCursor) Cursor::Capture(true);
    SDL_StartTextInput();   // deliver SDL_TEXTINPUT events for UI input fields

    // Optional GPU 3D renderer — the same hardware path the editor's Scene view uses,
    // now shipped with the game. Picks the best backend for this machine (Direct3D 11
    // on Windows, OpenGL elsewhere); each renders the scene to an offscreen target and
    // reads back RGBA8, a drop-in for the software rasterizer. If a GPU backend can't
    // be created (or a frame fails repeatedly), the player silently uses software, so
    // a build never fails to display 3D. Toggle in Build Game > GPU renderer.
    okay::GLRenderer*    glRenderer = nullptr;   // OpenGL backend (any platform)
#if defined(_WIN32)
    okay::D3D11Renderer* d3dRenderer = nullptr;  // Direct3D 11 backend (Windows)
#endif
    SDL_Window*          glWindow = nullptr;      // hidden context window for the GL path
    SDL_GLContext        glCtx = nullptr;
    bool                 glReady = false, d3dReady = false;
#ifndef __EMSCRIPTEN__
    if (cfg.gpu) {
#if defined(_WIN32)
        d3dRenderer = new okay::D3D11Renderer();
        if (d3dRenderer->Init()) d3dReady = true;
        else { delete d3dRenderer; d3dRenderer = nullptr; }
#endif
        // A 3.2 compatibility context: core FBOs/MSAA + our #version 120 shaders.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        glWindow = SDL_CreateWindow("okay-gl", 0, 0, 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (glWindow) {
            glCtx = SDL_GL_CreateContext(glWindow);
            if (glCtx) {
                SDL_GL_MakeCurrent(glWindow, glCtx);
                if (okay::GLRenderer::LoadGL((okay::GLRenderer::GLGetProc)SDL_GL_GetProcAddress)) {
                    glRenderer = new okay::GLRenderer();
                    glReady = true;
                }
                SDL_GL_MakeCurrent(nullptr, nullptr);
            }
        }
    }
#endif
    // Reused scratch buffer for GPU downscale/readback (mirrors the editor's path).
    std::vector<std::uint32_t> mesh3DGpu;
    int gpuFails = 0;   // self-heal: after repeated GPU failures, fall back for good

    SDL_AudioSpec want{}, have{};
    want.freq = 44100; want.format = AUDIO_F32SYS; want.channels = 1; want.samples = 1024;
    SDL_AudioDeviceID audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audioDev) SDL_PauseAudioDevice(audioDev, 0);
    AudioMixer::masterVolume = cfg.volume;   // global mix scale from build settings

    // baseDir (resolved above) is where the game files live, for relative paths.
    std::unordered_map<std::string, SDL_Texture*> textureCache;
    // Z-buffered 3D: meshes are rasterized into this texture each frame so
    // overlapping faces occlude correctly, then blitted under the 2D/UI layers.
    Raster mesh3D;
    SDL_Texture* mesh3DTex = nullptr;
    int mesh3DW = 0, mesh3DH = 0;

    // Load any WAV clips referenced by AudioSources, resampled to the mix rate.
    for (const auto& up : scene.Objects()) {
        auto* au = up->GetComponent<AudioSource>();
        if (!au || au->clipPath.empty()) continue;
        AudioClip wav;
        if (wav.LoadWAV(au->clipPath) || wav.LoadWAV(baseDir + au->clipPath))
            au->clip = wav.Resampled(44100);
    }

    // Load any .OBJ models referenced by MeshRenderers (resolve next to the exe).
    for (const auto& up : scene.Objects()) {
        auto* mr = up->GetComponent<MeshRenderer>();
        if (!mr || mr->meshPath.empty()) continue;
        bool ok = false;
        Mesh m = Mesh::LoadOBJ(mr->meshPath, &ok);
        if (!ok || m.vertices.empty()) m = Mesh::LoadOBJ(baseDir + mr->meshPath, &ok);
        if (ok && !m.vertices.empty()) mr->mesh = m;
    }

    scene.Start();

    // Open the first connected game controller, if any.
    SDL_GameController* pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }

    bool running = true;
#ifdef OKAY_HAVE_OKAYUI
    bool g_testUI = false;            // F1 toggles the OkayUI "Test UI" overlay
    char g_uiText[32] = {0}; bool g_uiBack = false;
    // Let game scripts draw OkayUI via the ui_* builtins.
    static okay::OkayUIScriptBridge g_uiBridge;
    okay::SetScriptUI(&g_uiBridge);
#endif
    Uint64 last = SDL_GetPerformanceCounter();
    auto frame = [&]() {
        Uint64 fStart = SDL_GetPerformanceCounter();
        Input::ClearTypedText();                 // collect this frame's typed chars
#ifdef OKAY_HAVE_OKAYUI
        g_uiText[0] = '\0'; g_uiBack = false;
#endif
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
#ifdef OKAY_HAVE_OKAYUI
            if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_F1) g_testUI = !g_testUI;
            if (g_testUI) {
                if (e.type == SDL_TEXTINPUT) SDL_strlcat(g_uiText, e.text.text, sizeof(g_uiText));
                if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_BACKSPACE) g_uiBack = true;
            }
#endif
            if (e.type == SDL_CONTROLLERDEVICEADDED && !pad)
                pad = SDL_GameControllerOpen(e.cdevice.which);
            if (e.type == SDL_TEXTINPUT) Input::FeedText(e.text.text);   // real characters
            if (e.type == SDL_MOUSEWHEEL) Input::FeedMouseWheel((float)e.wheel.y);
            // Esc quits only when no input field is focused (otherwise it cancels it).
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
                bool typing = false;
                for (const auto& up : scene.Objects())
                    if (auto* f = up->GetComponent<UIInputField>()) if (f->focused) typing = true;
                if (!typing && cfg.quitOnEscape) running = false;
            }
        }

        // Feed keyboard into the engine Input.
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        std::vector<char> down;
        for (char c = 'a'; c <= 'z'; ++c)
            if (ks[SDL_GetScancodeFromKey(c)]) down.push_back(c);
        for (char c = '0'; c <= '9'; ++c)
            if (ks[SDL_GetScancodeFromKey(c)]) down.push_back(c);
        if (ks[SDL_SCANCODE_SPACE]) down.push_back(' ');
        if (ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT]) down.push_back(Input::KeyShift); // sprint
        if (ks[SDL_SCANCODE_LCTRL]  || ks[SDL_SCANCODE_RCTRL])  down.push_back(Input::KeyCtrl);  // crouch
        // Editing keys for text fields (held-state; edge-detected by the field).
        if (ks[SDL_SCANCODE_BACKSPACE]) down.push_back((char)8);
        if (ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_KP_ENTER]) down.push_back('\r');
        if (ks[SDL_SCANCODE_ESCAPE]) down.push_back((char)27);
        // Map arrow keys onto WASD so arrow-key movement just works.
        if (ks[SDL_SCANCODE_UP])    down.push_back('w');
        if (ks[SDL_SCANCODE_LEFT])  down.push_back('a');
        if (ks[SDL_SCANCODE_DOWN])  down.push_back('s');
        if (ks[SDL_SCANCODE_RIGHT]) down.push_back('d');

        // Read the gamepad (first connected controller).
        Vec2 padAxis; unsigned padMask = 0;
        if (pad) {
            float lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
            float ly = -SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
            if (lx > -0.18f && lx < 0.18f) lx = 0.0f; // stick deadzone
            if (ly > -0.18f && ly < 0.18f) ly = 0.0f;
            padAxis = {lx, ly};
            auto bit = [&](SDL_GameControllerButton b, int id) {
                if (SDL_GameControllerGetButton(pad, b)) padMask |= 1u << id;
            };
            bit(SDL_CONTROLLER_BUTTON_A, 0); bit(SDL_CONTROLLER_BUTTON_B, 1);
            bit(SDL_CONTROLLER_BUTTON_X, 2); bit(SDL_CONTROLLER_BUTTON_Y, 3);
            bit(SDL_CONTROLLER_BUTTON_BACK, 4); bit(SDL_CONTROLLER_BUTTON_START, 5);
            bit(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 6); bit(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 7);
            bit(SDL_CONTROLLER_BUTTON_DPAD_UP, 8); bit(SDL_CONTROLLER_BUTTON_DPAD_DOWN, 9);
            bit(SDL_CONTROLLER_BUTTON_DPAD_LEFT, 10); bit(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 11);
            // Fold the stick / d-pad into WASD so movement scripts work on a pad.
            if (lx > 0.4f || (padMask & (1u << 11))) down.push_back('d');
            if (lx < -0.4f || (padMask & (1u << 10))) down.push_back('a');
            if (ly > 0.4f || (padMask & (1u << 8)))  down.push_back('w');
            if (ly < -0.4f || (padMask & (1u << 9)))  down.push_back('s');
            if (padMask & (1u << 0)) down.push_back(' '); // A -> space (jump/confirm)
        }
        Input::FeedKeys(down);
        Input::FeedGamepad(padAxis, padMask);

        // An open inventory is modal: free the cursor (even in FPS/locked mode) so you
        // can point at and drag items, and show it — just like Minecraft/DayZ.
        bool invModal = false, itemDragging = false;
        for (const auto& up : scene.Objects()) {
            if (!up) continue;
            if (auto* iu = up->GetComponent<InventoryUI>()) {
                if (iu->open && iu->dragItems) invModal = true;
                if (iu->dragIndex >= 0) itemDragging = true;
            }
            if (auto* gi = up->GetComponent<GridInventory>()) { if (gi->open) invModal = true; }
            if (auto* gu = up->GetComponent<GridInventoryUI>()) { if (gu->dragIndex >= 0) itemDragging = true; }
            if (auto* cs = up->GetComponent<CraftingStation>()) { if (cs->open) invModal = true; }
            if (auto* ce = up->GetComponent<ChestInventory>()) { if (ce->open) invModal = true; }
        }
        Input::SetUICaptured(invModal);   // controllers pause look/move while a bag is open

        // Apply the game's requested cursor state (Unity-style Cursor lock/visibility).
        static Vec2 s_virtualMouse{0, 0};
        bool locked = Cursor::IsLocked() && !invModal;
        // Unity-style hand-off: when a bag opens (mouselook was locked) free the pointer
        // AT SCREEN CENTRE, and when it closes warp back to centre before re-locking, so
        // the cursor always appears in the middle and the look anchor resets cleanly.
        static bool s_prevInvModal = false;
        if (invModal != s_prevInvModal && Cursor::IsLocked()) {
            int ww = 0, wh = 0; SDL_GetWindowSize(window, &ww, &wh);
            SDL_WarpMouseInWindow(window, ww / 2, wh / 2);
        }
        s_prevInvModal = invModal;
        SDL_SetRelativeMouseMode(locked ? SDL_TRUE : SDL_FALSE);
        // While actively dragging a stack, hide the OS arrow: the dragged icon (drawn
        // centred on the mouse) becomes the pointer. The arrow's hotspot sits at its
        // tip — ~half a slot above where it looks like it's pointing — which biased the
        // nearest-slot drop to the slot above. Hiding it makes the icon centre the only
        // reference, so "drop where the item is" lines up with the slot under it.
        bool showCursor = (Cursor::visible || invModal) && !itemDragging;
        SDL_ShowCursor(showCursor ? SDL_ENABLE : SDL_DISABLE);

        // Feed the mouse (position in pixels + left/right/middle button state). When
        // locked we accumulate relative motion into a virtual position so the
        // controllers' look deltas keep working with a hidden, centred cursor.
        int mx, my; Uint32 mb;
        if (locked) {
            int rx, ry; mb = SDL_GetRelativeMouseState(&rx, &ry);
            s_virtualMouse.x += (float)rx; s_virtualMouse.y += (float)ry;
        } else {
            int ax, ay; mb = SDL_GetMouseState(&ax, &ay);
            // SDL_GetMouseState is in window POINTS, but the UI is drawn in renderer
            // PIXELS (the window is High-DPI). Scale so UI hit-testing — inventory
            // slots, buttons — lines up with what's on screen.
            int ww = 0, wh = 0, ow = 0, oh = 0;
            SDL_GetWindowSize(window, &ww, &wh);
            SDL_GetRendererOutputSize(renderer, &ow, &oh);
            float sx = ww > 0 ? (float)ow / ww : 1.0f, sy = wh > 0 ? (float)oh / wh : 1.0f;
            s_virtualMouse = Vec2{ax * sx, ay * sy};
        }
        unsigned mask = 0;
        if (mb & SDL_BUTTON(SDL_BUTTON_LEFT))   mask |= 1u << 0;
        if (mb & SDL_BUTTON(SDL_BUTTON_RIGHT))  mask |= 1u << 1;
        if (mb & SDL_BUTTON(SDL_BUTTON_MIDDLE)) mask |= 1u << 2;
        Input::FeedMouse(s_virtualMouse, mask);

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((now - last) / (double)SDL_GetPerformanceFrequency());
        last = now;
        if (dt > 0.1f) dt = 0.1f;

        // Publish the render-target size so anchored UI widgets and their
        // hit-tests (run inside scene.Update) agree on where things sit.
        { int cw, ch; SDL_GetRendererOutputSize(renderer, &cw, &ch);
          UICanvas::Set((float)cw, (float)ch);
          // Make the world-space UI projector live during scene.Update too, so a
          // world-space button's Contains() hit-tests where it actually appears
          // on screen (the draw pass re-installs the same projector each frame).
          UIWorld().active = false;
          if (Camera* wcam = scene.mainCamera) {
              UIWorld().active = true;
              UIWorld().screenW = (float)cw; UIWorld().screenH = (float)ch;
              if (wcam->gameObject && wcam->gameObject->transform)
                  UIWorld().right = wcam->gameObject->transform->Right();
              UIWorld().project = [wcam, cw, ch](const Vec3& p, Vec2& out, float& depth) {
                  return wcam->WorldToScreen(p, (float)cw, (float)ch, out, &depth);
              };
              UIWorld().rectOf = [cw, ch](GameObject* go, Vec2& o, Vec2& sz) {
                  return GetUIScreenRect(go, (float)cw, (float)ch, o, sz);
              };
          } }

        // Auto-size scroll views from their children before updating, so the wheel
        // and scrollbar use the real content extent (not a stale hand-set value).
        for (const auto& up : scene.Objects())
            if (auto* sv = up->GetComponent<UIScrollView>())
                if (sv->autoContent) {
                    float ch = ScrollViewContentHeight(up.get());
                    sv->contentHeight = ch > sv->size.y ? ch : sv->size.y;
                }

#ifdef OKAY_HAVE_OKAYUI
        // Start the OkayUI frame BEFORE the scene updates, so scripts' ui_* builtins
        // (via the installed ScriptUIBridge) draw into this frame. Flushed after the
        // game is rendered (see EndFrame near the present), so UI sits on top.
        {
            int mx, my; Uint32 mb = SDL_GetMouseState(&mx, &my);
            OkayUI::Input ui;
            ui.mouseX = (float)mx; ui.mouseY = (float)my;
            ui.mouseDown = (mb & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
            ui.text = g_uiText[0] ? g_uiText : nullptr;
            ui.backspace = g_uiBack;
            OkayUI::BeginFrame(ui);
        }
#endif
        // Drive global Time so ElapsedTime()/DeltaTime()/timeScale work, then
        // advance the scene by the scaled delta (timeScale 0 = paused).
        Time::Step(dt);
        scene.Update(Time::DeltaTime());

        // Keyboard / gamepad menu navigation (arrows/WASD + Enter/Space/A).
        NavigateUI(scene);

        if (audioDev) {
            int n = (int)(dt * 44100.0f); if (n > 8192) n = 8192;
            if (n > 0) {
                if (scene.mainCamera)   // listener for 3D/spatial audio sources
                    AudioMixer::SetListener(scene.mainCamera->gameObject->transform->Position());
                std::vector<float> ab(n);
                AudioMixer::Render(scene, ab.data(), n);
                SDL_QueueAudio(audioDev, ab.data(), (Uint32)(n * sizeof(float)));
            }
        }

        int w, h; SDL_GetRendererOutputSize(renderer, &w, &h);
        // Scroll-view membership: offset a widget's origin by its owning Scroll
        // View's scroll and clip drawing to the viewport, so scrollable content
        // shows and hides correctly in the built game. Call once per UI widget
        // right after resolving its origin; widgets outside a scroll view reset
        // the clip to none.
        auto enterScroll = [&](GameObject* g, Vec2& o) {
            if (UIScrollView* psv = OwningScrollView(g)) {
                Vec2 vp = UIResolveOrigin(psv->gameObject, (float)w, (float)h);
                o.y -= psv->scroll;
                SDL_Rect clip{(int)vp.x, (int)vp.y, (int)psv->size.x, (int)psv->size.y};
                SDL_RenderSetClipRect(renderer, &clip);
            } else {
                SDL_RenderSetClipRect(renderer, nullptr);
            }
        };
        Camera* cam = scene.mainCamera;
        Color bg = cam ? cam->backgroundColor : Color::Black;
        SDL_SetRenderDrawColor(renderer, (Uint8)(bg.r * 255), (Uint8)(bg.g * 255),
                               (Uint8)(bg.b * 255), 255);
        SDL_RenderClear(renderer);

        bool perspective = cam && cam->projection == Camera::Projection::Perspective;
        // A camera set to "Solid Color" clear flags suppresses the skybox (the
        // background color from the clear above shows through) — matches the editor.
        bool solidClear = cam && cam->clearFlags == Camera::ClearFlags::SolidColor;

        // Skybox: the same vertical sky gradient the editor previews, baked into
        // the scene's render settings so a built game looks identical. Drawn first
        // (behind everything) for 3D/perspective scenes. Painted as solid-color
        // horizontal strips (SDL_RenderFillRect) rather than SDL_RenderGeometry so
        // it renders on every driver — RenderGeometry can silently no-op on some
        // GPUs/backends, which left the game showing a black sky.
        if (perspective && scene.renderSettings.skybox && !solidClear && w > 0 && h > 0) {
            const auto& rs = scene.renderSettings;
            auto lerp = [](const Color& a, const Color& b, float t) {
                return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                             a.b + (b.b - a.b) * t, 1.0f};
            };
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            const int strips = h < 128 ? h : 128;     // smooth enough, cheap
            for (int s = 0; s < strips; ++s) {
                float t = (float)s / (float)(strips - 1);   // 0 (top) .. 1 (bottom)
                // Two-stop gradient: top->horizon for the upper half, horizon->bottom below.
                Color c = (t < 0.5f) ? lerp(rs.skyTop, rs.skyHorizon, t * 2.0f)
                                     : lerp(rs.skyHorizon, rs.skyBottom, (t - 0.5f) * 2.0f);
                SDL_SetRenderDrawColor(renderer, (Uint8)(c.r * 255), (Uint8)(c.g * 255),
                                       (Uint8)(c.b * 255), 255);
                int y0 = (int)((float)s / strips * h);
                int y1 = (int)((float)(s + 1) / strips * h);
                SDL_Rect rrect{0, y0, w, (y1 > y0 ? y1 - y0 : 1)};
                SDL_RenderFillRect(renderer, &rrect);
            }
        }

        if (perspective) {
            // Z-buffered software render so overlapping faces occlude correctly,
            // then blit it under the 2D/UI layers (transparent where no geometry).
            Vec3 camPos = (cam && cam->transform) ? cam->transform->Position() : Vec3::Zero;
            Mat4 vp = cam->ProjectionMatrix(h > 0 ? (float)w / h : 1.0f) * cam->ViewMatrix();
            if (w > 0 && h > 0) {
                ApplySceneLight(scene);                 // a Light object aims the shading
                const GameObject* ignore = cam ? cam->ignoreObject : nullptr;
                const std::uint32_t* px = nullptr;
                // GPU path first: D3D11 (Windows) then OpenGL, each rendering to an
                // offscreen target and reading back RGBA8. A self-heal counter disables
                // the GPU path after repeated failures so the game is never stuck.
                if (cfg.gpu && (d3dReady || glReady) && gpuFails < 3) {
#if defined(_WIN32)
                    if (!px && d3dReady && d3dRenderer)
                        px = d3dRenderer->RenderToPixels(scene, vp, camPos, w, h, 4,
                                                         0.0f, 0.0f, 0.0f, 0.0f, ignore);
#endif
                    if (!px && glReady && glRenderer && glWindow && glCtx) {
                        if (SDL_GL_MakeCurrent(glWindow, glCtx) == 0) {
                            px = glRenderer->RenderToPixels(scene, vp, camPos, w, h, 4,
                                                            0.0f, 0.0f, 0.0f, 0.0f, ignore);
                            SDL_GL_MakeCurrent(nullptr, nullptr);
                        }
                    }
                    if (!px) ++gpuFails; else gpuFails = 0;
                    (void)mesh3DGpu;
                }
                // Software fallback: native-resolution z-buffered raster (FXAA handles
                // edge AA). 2x supersampling is 4x the pixels — off by default.
                static std::vector<std::uint32_t> mesh3DDown;
                if (!px)
                    px = RenderMeshesSS(mesh3D, mesh3DDown, scene, vp, camPos, w, h,
                                        cfg.antialias < 1 ? 1 : cfg.antialias, ignore);
                if (px) {
                    if (!mesh3DTex || mesh3DW != w || mesh3DH != h) {
                        if (mesh3DTex) SDL_DestroyTexture(mesh3DTex);
                        mesh3DTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                                      SDL_TEXTUREACCESS_STREAMING, w, h);
                        SDL_SetTextureBlendMode(mesh3DTex, SDL_BLENDMODE_BLEND);
                        mesh3DW = w; mesh3DH = h;
                    }
                    SDL_UpdateTexture(mesh3DTex, nullptr, px, w * 4);
                    SDL_RenderCopy(renderer, mesh3DTex, nullptr, nullptr);
                }
            }
        } else {
            float ortho = cam ? cam->orthographicSize : 5.0f;
            Vec3 camPos = (cam && cam->transform) ? cam->transform->Position() : Vec3::Zero;
            float scale = h / (2.0f * ortho);
            // Gather active sprites and draw back-to-front by sortOrder (stable,
            // so same-order sprites keep scene order). Enables layered 2D scenes.
            std::vector<GameObject*> sprites;
            for (const auto& up : scene.Objects())
                if (up->active && up->GetComponent<SpriteRenderer>()) sprites.push_back(up.get());
            std::stable_sort(sprites.begin(), sprites.end(), [](GameObject* a, GameObject* b) {
                return a->GetComponent<SpriteRenderer>()->sortOrder <
                       b->GetComponent<SpriteRenderer>()->sortOrder;
            });
            for (GameObject* obj : sprites) {
                auto* sr = obj->GetComponent<SpriteRenderer>();
                // Rotate/scale the sprite quad through the full transform so 2D
                // games can spin and skew sprites, not just place axis-aligned ones.
                Mat4 model = obj->transform->LocalToWorldMatrix();
                float hx = sr->size.x * 0.5f, hy = sr->size.y * 0.5f;
                Vec3 corners[4] = {{-hx, -hy, 0}, {hx, -hy, 0}, {hx, hy, 0}, {-hx, hy, 0}};
                SDL_Color col{(Uint8)(sr->color.r * 255), (Uint8)(sr->color.g * 255),
                              (Uint8)(sr->color.b * 255), (Uint8)(sr->color.a * 255)};
                // Texture coords map the image upright onto the quad corners
                // (corner 3 = top-left in world = texture uvMin). Honors the
                // sprite's uv sub-region so sprite sheets / atlases work.
                SDL_Texture* tex = GetTexture(renderer, sr->texture, baseDir, textureCache);
                float u0 = sr->uvMin.x, v0 = sr->uvMin.y, u1 = sr->uvMax.x, v1 = sr->uvMax.y;
                if (sr->flipX) std::swap(u0, u1);
                if (sr->flipY) std::swap(v0, v1);
                const SDL_FPoint uv[4] = {{u0, v1}, {u1, v1}, {u1, v0}, {u0, v0}};
                SDL_Vertex vtx[4];
                for (int k = 0; k < 4; ++k) {
                    Vec3 wpos = model.MultiplyPoint(corners[k]);
                    SDL_Point s = W2S(wpos, camPos, scale, w, h);
                    vtx[k] = SDL_Vertex{{(float)s.x, (float)s.y}, col, uv[k]};
                }
                const int idx[6] = {0, 1, 2, 0, 2, 3};
                SDL_RenderGeometry(renderer, tex, vtx, 4, idx, 6);
            }

            // Drop-zone highlight: tint a zone while a valid item hovers it.
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            for (const auto& up : scene.Objects()) {
                auto* dz = up->GetComponent<DropZone>();
                auto* sr = up->GetComponent<SpriteRenderer>();
                if (!dz || !sr || !up->active || !dz->IsHovered()) continue;
                Vec3 ls = up->transform->LossyScale();
                FillWorldQuad(renderer, up->transform->Position(),
                              sr->size.x * ls.x, sr->size.y * ls.y,
                              camPos, scale, w, h, SDL_Color{255, 255, 255, 70});
            }

            // Tilemaps: draw each non-empty cell as a colored quad.
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            for (const auto& up : scene.Objects()) {
                auto* tm = up->GetComponent<Tilemap>();
                if (!tm || !up->active || UIHidden(up.get())) continue;
                for (int y = 0; y < tm->Height(); ++y)
                    for (int x = 0; x < tm->Width(); ++x) {
                        int id = tm->GetTile(x, y);
                        if (id == 0) continue;
                        FillWorldQuad(renderer, tm->CellToWorld(x, y), tm->tileSize,
                                      tm->tileSize, camPos, scale, w, h, TileColor(id));
                    }
            }

            // Particle systems: draw each live particle as a small fading quad.
            for (const auto& up : scene.Objects()) {
                auto* ps = up->GetComponent<ParticleSystem>();
                if (!ps || !up->active || UIHidden(up.get())) continue;
                for (const auto& p : ps->Particles()) {
                    if (!p.alive) continue;
                    SDL_Color col{(Uint8)(p.color.r * 255), (Uint8)(p.color.g * 255),
                                  (Uint8)(p.color.b * 255), (Uint8)(p.color.a * 255)};
                    FillWorldQuad(renderer, p.position, p.size, p.size,
                                  camPos, scale, w, h, col);
                }
            }
        }

        // World-space text only (sits with the 2D scene). Screen-space HUD text is
        // drawn LATER (after the UI widgets) so labels sit ON TOP of panels instead
        // of being hidden behind them — see the screen-space text pass below.
        {
            float ortho = cam ? cam->orthographicSize : 5.0f;
            Vec3 camPos = (cam && cam->transform) ? cam->transform->Position() : Vec3::Zero;
            float scale = h / (2.0f * ortho);
            for (const auto& up : scene.Objects()) {
                auto* tr = up->GetComponent<TextRenderer>();
                if (!tr || !up->active || tr->screenSpace || UIHidden(up.get())) continue;
                float op = UIOpacity(up.get());   // canvas master fade
                SDL_Color col{(Uint8)(tr->color.r * 255), (Uint8)(tr->color.g * 255),
                              (Uint8)(tr->color.b * 255), (Uint8)(tr->color.a * 255 * op)};
                SDL_Color sh{(Uint8)(tr->shadowColor.r * 255), (Uint8)(tr->shadowColor.g * 255),
                             (Uint8)(tr->shadowColor.b * 255), (Uint8)(tr->shadowColor.a * 255 * op)};
                SDL_Color ol{(Uint8)(tr->outlineColor.r * 255), (Uint8)(tr->outlineColor.g * 255),
                             (Uint8)(tr->outlineColor.b * 255), (Uint8)(tr->outlineColor.a * 255 * op)};
                SDL_Point o = W2S(up->transform->Position(), camPos, scale, w, h);
                float px = tr->pixelSize * scale;
                okay::TtfFont* fnt = tr->Font();
                if (tr->shadow)
                    DrawText(renderer, tr->text, o.x + tr->shadowOffset.x * px,
                             o.y + tr->shadowOffset.y * px, px, sh, 0, 0, false, false, SDL_Color{0,0,0,0}, fnt);
                if (tr->outline) {
                    DrawText(renderer, tr->text, o.x - px, o.y, px, ol, 0, 0, false, false, SDL_Color{0,0,0,0}, fnt);
                    DrawText(renderer, tr->text, o.x + px, o.y, px, ol, 0, 0, false, false, SDL_Color{0,0,0,0}, fnt);
                    DrawText(renderer, tr->text, o.x, o.y - px, px, ol, 0, 0, false, false, SDL_Color{0,0,0,0}, fnt);
                    DrawText(renderer, tr->text, o.x, o.y + px, px, ol, 0, 0, false, false, SDL_Color{0,0,0,0}, fnt);
                }
                DrawText(renderer, tr->text, (float)o.x, (float)o.y, px, col, 0, 0, false, false, SDL_Color{0,0,0,0}, fnt);
                if (tr->bold) DrawText(renderer, tr->text, (float)o.x + px, (float)o.y, px, col, 0, 0, false, false, SDL_Color{0,0,0,0}, fnt);
            }
        }

        // In-game UI (screen space), drawn on top of everything. Iterate widgets in
        // Canvas sort-order (higher draws on top), then by hierarchy pre-order so a
        // child layers above its parent and sibling reordering (bring-to-front/back)
        // takes effect — all within the existing per-type passes.
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        // Single UI pass: every widget's draw-block is queued as an item, then sorted
        // by Canvas sortOrder, each object's uiDrawOrder override (else its default
        // type layer), and hierarchy pre-order. The layer constants below are the
        // historic per-type pass order, so with no overrides the result is identical
        // to the old type-grouped passes — but a non-zero uiDrawOrder now lets a
        // widget layer against widgets of any other type. A drop target queues two
        // items (background behind, highlight above panels).
        enum UIK { K_DropBg = 0, K_Image, K_Panel, K_DropHi, K_Scroll, K_Progress,
                   K_Radial, K_Minimap, K_Slider, K_Stepper, K_Rating, K_Toggle, K_Tabs, K_Button,
                   K_Text, K_WorldUI, K_Dropdown, K_Input, K_Crosshair, K_FocusRing, K_Tooltip };
        std::vector<UIDrawItem> uiItems;
        for (std::size_t _qi = 0; _qi < scene.Objects().size(); ++_qi) {
            GameObject* g = scene.Objects()[_qi].get();
            if (!g) continue;
            auto add = [&](int k) { uiItems.push_back(UIDrawItem{_qi, k, k}); };
            if (g->GetComponent<UIDropTarget>())    { add(K_DropBg); add(K_DropHi); }
            if (g->GetComponent<UIImage>())          add(K_Image);
            if (g->GetComponent<UIPanel>())          add(K_Panel);
            if (g->GetComponent<UIScrollView>())     add(K_Scroll);
            if (g->GetComponent<UIProgressBar>())    add(K_Progress);
            if (g->GetComponent<UIRadialProgress>()) add(K_Radial);
            if (g->GetComponent<Minimap>())          add(K_Minimap);
            if (g->GetComponent<UISlider>())         add(K_Slider);
            if (g->GetComponent<UIStepper>())        add(K_Stepper);
            if (g->GetComponent<UIRating>())         add(K_Rating);
            if (g->GetComponent<UIToggle>())         add(K_Toggle);
            if (g->GetComponent<UITabs>())           add(K_Tabs);
            if (g->GetComponent<UIButton>())         add(K_Button);
            if (g->GetComponent<TextRenderer>())     add(K_Text);
            if (g->GetComponent<WorldUI>())          add(K_WorldUI);
            if (g->GetComponent<UIDropdown>())       add(K_Dropdown);
            if (g->GetComponent<UIInputField>())     add(K_Input);
            if (g->GetComponent<Crosshair>())        add(K_Crosshair);
            if (IsUIFocused(g))                      add(K_FocusRing);
            if (g->GetComponent<UITooltip>())        add(K_Tooltip);
        }
        uiItems = SortUIDrawItems(scene.Objects(), std::move(uiItems));
        // World-space Canvas projection: hand the shared UI layout helpers a camera
        // projector for this frame so any widget under a world-space Canvas renders
        // (and hit-tests) in 3D. Cleared after the pass; screen-space UI is untouched.
        UIWorld().active = false;
        if (Camera* wcam = scene.mainCamera) {
            int ww = w, hh = h;
            UIWorld().active = true;
            UIWorld().screenW = (float)ww; UIWorld().screenH = (float)hh;
            if (wcam->gameObject && wcam->gameObject->transform)
                UIWorld().right = wcam->gameObject->transform->Right();
            UIWorld().project = [wcam, ww, hh](const Vec3& p, Vec2& out, float& depth) {
                return wcam->WorldToScreen(p, (float)ww, (float)hh, out, &depth);
            };
            UIWorld().rectOf = [ww, hh](GameObject* go, Vec2& o, Vec2& sz) {
                return GetUIScreenRect(go, (float)ww, (float)hh, o, sz);
            };
        }
        g_uiDefaultFont = okay::GetFont(scene.uiFont);   // scene-wide default for this pass
        for (const UIDrawItem& _it : uiItems) {
            const auto& up = scene.Objects()[_it.index];
            if (_it.kind == K_DropBg) {   // drop-target slot backgrounds (behind items)
            auto* dt = up->GetComponent<UIDropTarget>();
            if (!dt || !up->active || !dt->drawBackground || UIHidden(up.get())) continue;
            Vec2 o, sz; if (!GetUIScreenRect(up.get(), (float)w, (float)h, o, sz)) continue;
            SDL_Rect r{(int)o.x, (int)o.y, (int)sz.x, (int)sz.y};
            SDL_SetRenderDrawColor(renderer, (Uint8)(dt->background.r*255), (Uint8)(dt->background.g*255),
                                   (Uint8)(dt->background.b*255), (Uint8)(dt->background.a*255));
            SDL_RenderFillRect(renderer, &r);
            for (int b = 0; b < (int)dt->borderWidth; ++b) {   // outline
                SDL_Rect br{r.x+b, r.y+b, r.w-2*b, r.h-2*b};
                SDL_SetRenderDrawColor(renderer, (Uint8)(dt->borderColor.r*255), (Uint8)(dt->borderColor.g*255),
                                       (Uint8)(dt->borderColor.b*255), (Uint8)(dt->borderColor.a*255));
                SDL_RenderDrawRect(renderer, &br);
            }
        }
            else if (_it.kind == K_Image) {   // images (logos/icons) first
            auto* im = up->GetComponent<UIImage>();
            if (!im || !up->active || UIHidden(up.get())) continue;
            float op = UIOpacity(up.get());   // canvas master fade
            Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect r{(int)o.x, (int)o.y, (int)im->size.x, (int)im->size.y};
            // Radial/linear fill: shrink the drawn rect to fillAmount along an axis.
            float fox, foy, fw, fh;
            im->FilledRect(im->size.x, im->size.y, fox, foy, fw, fh);
            SDL_Rect fr{(int)(o.x + fox), (int)(o.y + foy), (int)fw, (int)fh};
            bool filled = im->fillMode != UIImage::FillMode::None;
            SDL_Texture* tex = GetTexture(renderer, im->texture, baseDir, textureCache);
            if (tex) {
                SDL_SetTextureColorMod(tex, (Uint8)(im->color.r * 255), (Uint8)(im->color.g * 255),
                                       (Uint8)(im->color.b * 255));
                SDL_SetTextureAlphaMod(tex, (Uint8)(im->color.a * 255 * op));
                if (im->nineSlice && im->border > 0.0f) {
                    int tw = 0, th = 0; SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
                    int sb = (int)im->border;                       // source border
                    int dbx = (int)im->border, dby = (int)im->border;  // dest border (clamped)
                    if (dbx * 2 > r.w) dbx = r.w / 2;
                    if (dby * 2 > r.h) dby = r.h / 2;
                    // Column x's (src and dst) and row y's: left | middle | right.
                    int sx[4] = {0, sb, tw - sb, tw};
                    int sy[4] = {0, sb, th - sb, th};
                    int dx[4] = {r.x, r.x + dbx, r.x + r.w - dbx, r.x + r.w};
                    int dy[4] = {r.y, r.y + dby, r.y + r.h - dby, r.y + r.h};
                    for (int cy = 0; cy < 3; ++cy)
                        for (int cx = 0; cx < 3; ++cx) {
                            SDL_Rect s{sx[cx], sy[cy], sx[cx + 1] - sx[cx], sy[cy + 1] - sy[cy]};
                            SDL_Rect d{dx[cx], dy[cy], dx[cx + 1] - dx[cx], dy[cy + 1] - dy[cy]};
                            if (s.w > 0 && s.h > 0 && d.w > 0 && d.h > 0)
                                SDL_RenderCopy(renderer, tex, &s, &d);
                        }
                } else if (filled) {                        // reveal a proportional slice
                    int tw = 0, th = 0; SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
                    SDL_Rect src{
                        (int)(im->size.x > 0 ? fox / im->size.x * tw : 0),
                        (int)(im->size.y > 0 ? foy / im->size.y * th : 0),
                        (int)(im->size.x > 0 ? fw  / im->size.x * tw : tw),
                        (int)(im->size.y > 0 ? fh  / im->size.y * th : th)};
                    SDL_RenderCopy(renderer, tex, &src, &fr);
                } else {
                    SDL_RenderCopy(renderer, tex, nullptr, &r);
                }
            } else {                                        // no image -> colored shape fill
                const SDL_Rect& rr = filled ? fr : r;
                FillUIShape(renderer, rr, im->shape, im->cornerRadius,
                            im->color, im->color, false, false, op);
            }
        }
            else if (_it.kind == K_Panel) {   // panels (backgrounds) first
            auto* pn = up->GetComponent<UIPanel>();
            if (!pn || !up->active || UIHidden(up.get())) continue;
            float op = UIOpacity(up.get());   // canvas master fade
            Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect r{(int)o.x, (int)o.y, (int)pn->size.x, (int)pn->size.y};
            if (pn->shadow) {                               // drop shadow behind (same shape)
                SDL_Rect sh{r.x + (int)pn->shadowOffset.x, r.y + (int)pn->shadowOffset.y, r.w, r.h};
                FillUIShadow(renderer, sh, pn->shape, pn->cornerRadius,
                             pn->shadowColor, pn->shadowSoftness, op);
            }
            if (pn->borderWidth > 0.0f) {                   // border = outer shape, then inner fill
                FillUIShape(renderer, r, pn->shape, pn->cornerRadius,
                            pn->borderColor, pn->borderColor, false, false, op);
                int b = (int)pn->borderWidth;
                SDL_Rect inner{r.x + b, r.y + b, r.w - 2 * b, r.h - 2 * b};
                float innerR = pn->cornerRadius - b; if (innerR < 0.0f) innerR = 0.0f;
                FillUIShape(renderer, inner, pn->shape, innerR,
                            pn->color, pn->colorBottom, pn->useGradient, pn->gradientHorizontal, op);
            } else {
                FillUIShape(renderer, r, pn->shape, pn->cornerRadius,
                            pn->color, pn->colorBottom, pn->useGradient, pn->gradientHorizontal, op);
            }
        }
            else if (_it.kind == K_DropHi) {   // drop-target highlight (drag feedback)
            auto* dt = up->GetComponent<UIDropTarget>();
            if (!dt || !up->active || !dt->showHighlight || !dt->IsHovered()) continue;
            Vec2 o, sz;
            if (!GetUIScreenRect(up.get(), (float)w, (float)h, o, sz)) continue;
            SDL_Rect hr{(int)o.x, (int)o.y, (int)sz.x, (int)sz.y};
            const Color& hc = dt->HasValid() ? dt->highlight : dt->rejectHighlight;  // green-ish vs reject
            SDL_SetRenderDrawColor(renderer, (Uint8)(hc.r * 255), (Uint8)(hc.g * 255),
                                   (Uint8)(hc.b * 255), (Uint8)(hc.a * 255));
            SDL_RenderFillRect(renderer, &hr);
        }
            else if (_it.kind == K_Scroll) {   // scroll-view backgrounds + scrollbar
            auto* sv = up->GetComponent<UIScrollView>();
            if (!sv || !up->active || UIHidden(up.get())) continue;
            float op = UIOpacity(up.get());
            Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
            SDL_Rect box{(int)o.x, (int)o.y, (int)sv->size.x, (int)sv->size.y};
            SDL_SetRenderDrawColor(renderer, (Uint8)(sv->background.r * 255), (Uint8)(sv->background.g * 255),
                                   (Uint8)(sv->background.b * 255), (Uint8)(sv->background.a * 255 * op));
            SDL_RenderFillRect(renderer, &box);
            if (sv->ScrollMax() > 0.0f) {                   // scrollbar track + thumb
                int barW = 6;
                SDL_Rect track{box.x + box.w - barW - 2, box.y + 2, barW, box.h - 4};
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 30);
                SDL_RenderFillRect(renderer, &track);
                float frac = sv->size.y / (sv->contentHeight > 1.0f ? sv->contentHeight : 1.0f);
                int thumbH = (int)(track.h * (frac < 1.0f ? frac : 1.0f));
                int thumbY = track.y + (int)((track.h - thumbH) * sv->Fraction());
                SDL_Rect thumb{track.x, thumbY, barW, thumbH};
                SDL_SetRenderDrawColor(renderer, (Uint8)(sv->barColor.r * 255), (Uint8)(sv->barColor.g * 255),
                                       (Uint8)(sv->barColor.b * 255), (Uint8)(sv->barColor.a * 255 * op));
                SDL_RenderFillRect(renderer, &thumb);
            }
        }
            else if (_it.kind == K_Progress) {   // progress bars
            auto* pb = up->GetComponent<UIProgressBar>();
            if (!pb || !up->active || UIHidden(up.get())) continue;
            float op = UIOpacity(up.get());
            Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect bg{(int)o.x, (int)o.y, (int)pb->size.x, (int)pb->size.y};
            FillUIShape(renderer, bg, pb->shape, pb->cornerRadius,
                        pb->background, pb->background, false, false, op);
            float pfox, pfoy, pfw, pfh; pb->FillRect(pb->size.x, pb->size.y, pfox, pfoy, pfw, pfh);
            SDL_Rect fl{(int)(o.x + pfox), (int)(o.y + pfoy), (int)pfw, (int)pfh};
            bool horiz = pb->fillDir == UIProgressBar::FillDir::LeftRight ||
                         pb->fillDir == UIProgressBar::FillDir::RightLeft;
            FillUIShape(renderer, fl, pb->shape, pb->cornerRadius,
                        pb->fill, pb->fillEnd, pb->gradientFill, horiz, op);
            if (pb->showPercent) {
                char pct[8]; std::snprintf(pct, sizeof(pct), "%d%%", (int)(pb->Fraction() * 100.0f + 0.5f));
                float px = 2.0f;
                float tw = std::strlen(pct) * (Font8x8::Width + 1) * px;
                SDL_Color tc{(Uint8)(pb->textColor.r * 255), (Uint8)(pb->textColor.g * 255),
                             (Uint8)(pb->textColor.b * 255), (Uint8)(pb->textColor.a * 255 * op)};
                DrawText(renderer, pct, o.x + (pb->size.x - tw) * 0.5f,
                         o.y + (pb->size.y - Font8x8::Height * px) * 0.5f, px, tc);
            }
        }
            else if (_it.kind == K_Radial) {   // radial / ring progress
            auto* rp = up->GetComponent<UIRadialProgress>();
            if (!rp || !up->active || UIHidden(up.get())) continue;
            float op = UIOpacity(up.get());
            Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
            enterScroll(up.get(), o);
            int bw = (int)rp->size.x, bh = (int)rp->size.y;
            for (int y = 0; y < bh; ++y)
                for (int x = 0; x < bw; ++x) {
                    int reg = UIRadialProgress::Sample((float)bw, (float)bh, rp->thickness,
                                                       rp->EffectiveStart(), rp->clockwise, rp->value,
                                                       x + 0.5f, y + 0.5f);
                    if (reg == UIRadialProgress::Outside) continue;
                    const Color& c = (reg == UIRadialProgress::Fill) ? rp->fill : rp->background;
                    SDL_SetRenderDrawColor(renderer, (Uint8)(c.r * 255), (Uint8)(c.g * 255),
                                           (Uint8)(c.b * 255), (Uint8)(c.a * 255 * op));
                    SDL_Rect px1{(int)o.x + x, (int)o.y + y, 1, 1};
                    SDL_RenderFillRect(renderer, &px1);
                }
            if (rp->showPercent) {
                char pct[8]; std::snprintf(pct, sizeof(pct), "%d%%", (int)(rp->Fraction() * 100.0f + 0.5f));
                float ps = 2.0f;
                float tw = std::strlen(pct) * (Font8x8::Width + 1) * ps;
                SDL_Color tc{(Uint8)(rp->textColor.r * 255), (Uint8)(rp->textColor.g * 255),
                             (Uint8)(rp->textColor.b * 255), (Uint8)(rp->textColor.a * 255 * op)};
                DrawText(renderer, pct, o.x + (rp->size.x - tw) * 0.5f,
                         o.y + (rp->size.y - Font8x8::Height * ps) * 0.5f, ps, tc);
            }
        }
            else if (_it.kind == K_Minimap) {   // top-down minimap panel
            auto* mm = up->GetComponent<Minimap>();
            if (!mm || !up->active || UIHidden(up.get())) continue;
            float op = UIOpacity(up.get());
            Vec2 o, sz; if (!GetUIScreenRect(up.get(), (float)w, (float)h, o, sz)) continue;
            SDL_Rect box{(int)o.x, (int)o.y, (int)sz.x, (int)sz.y};
            int cx = (int)(o.x + sz.x * 0.5f), cy = (int)(o.y + sz.y * 0.5f);
            int radius = (int)((sz.x < sz.y ? sz.x : sz.y) * 0.5f);
            SDL_Color bgCol{(Uint8)(mm->background.r*255), (Uint8)(mm->background.g*255),
                            (Uint8)(mm->background.b*255), (Uint8)(mm->background.a*255*op)};
            SDL_Color bdCol{(Uint8)(mm->border.r*255), (Uint8)(mm->border.g*255),
                            (Uint8)(mm->border.b*255), (Uint8)(mm->border.a*255*op)};
            int bw = (int)mm->borderWidth; if (bw < 1) bw = 1;
            // Clip everything (grid + blips) to the map rect so nothing bleeds out.
            SDL_Rect prevClip; SDL_RenderGetClipRect(renderer, &prevClip);
            bool hadClip = SDL_RenderIsClipEnabled(renderer);
            if (mm->circular) {
                MMFillCircle(renderer, cx, cy, radius, bgCol);
            } else {
                SDL_SetRenderDrawColor(renderer, bgCol.r, bgCol.g, bgCol.b, bgCol.a);
                SDL_RenderFillRect(renderer, &box);
            }
            SDL_RenderSetClipRect(renderer, &box);
            // Reference grid (rectangular maps only).
            if (mm->showGrid && !mm->circular && mm->gridSpacing > 1.0f) {
                SDL_SetRenderDrawColor(renderer, (Uint8)(mm->gridColor.r*255), (Uint8)(mm->gridColor.g*255),
                                       (Uint8)(mm->gridColor.b*255), (Uint8)(mm->gridColor.a*255*op));
                for (float gx = sz.x*0.5f; gx < sz.x; gx += mm->gridSpacing) {
                    SDL_RenderDrawLine(renderer, (int)(o.x+gx), box.y, (int)(o.x+gx), box.y+box.h);
                    SDL_RenderDrawLine(renderer, (int)(o.x+sz.x-gx), box.y, (int)(o.x+sz.x-gx), box.y+box.h);
                }
                for (float gy = sz.y*0.5f; gy < sz.y; gy += mm->gridSpacing) {
                    SDL_RenderDrawLine(renderer, box.x, (int)(o.y+gy), box.x+box.w, (int)(o.y+gy));
                    SDL_RenderDrawLine(renderer, box.x, (int)(o.y+sz.y-gy), box.x+box.w, (int)(o.y+sz.y-gy));
                }
            }
            // Center world position + heading of the target (for heading-up rotation).
            Vec3 center{0,0,0};
            float mapHeading = 0.0f;
            if (!mm->target.empty()) {
                if (GameObject* tg = scene.Find(mm->target); tg && tg->transform) {
                    center = tg->transform->Position();
                    if (mm->rotateWithTarget) mapHeading = Minimap::HeadingOf(tg->transform->Forward(), mm->useXZ);
                }
            }
            // Draw a marker (square/dot/triangle/arrow) at (px,py).
            auto drawBlip = [&](float px, float py, int half, SDL_Color col,
                                MinimapBlip::Shape shp, float ang) {
                switch (shp) {
                    case MinimapBlip::Shape::Dot:
                        MMFillCircle(renderer, (int)px, (int)py, half, col); break;
                    case MinimapBlip::Shape::Triangle:
                    case MinimapBlip::Shape::Arrow: {
                        SDL_Point tip{(int)(px + std::sin(ang)*half), (int)(py - std::cos(ang)*half)};
                        float ba = 2.4f;
                        SDL_Point l{(int)(px + std::sin(ang-ba)*half), (int)(py - std::cos(ang-ba)*half)};
                        SDL_Point r{(int)(px + std::sin(ang+ba)*half), (int)(py - std::cos(ang+ba)*half)};
                        if (shp == MinimapBlip::Shape::Arrow) {
                            SDL_Point tail{(int)(px - std::sin(ang)*half*0.4f), (int)(py + std::cos(ang)*half*0.4f)};
                            MMFillTriangle(renderer, tip, l, tail, col);
                            MMFillTriangle(renderer, tip, r, tail, col);
                        } else {
                            MMFillTriangle(renderer, tip, l, r, col);
                        }
                    } break;
                    default: {
                        SDL_Rect br{(int)px - half, (int)py - half, half*2, half*2};
                        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
                        SDL_RenderFillRect(renderer, &br);
                    }
                }
            };
            // Plot every MinimapBlip.
            for (const auto& bp : scene.Objects()) {
                if (!bp || !bp->active) continue;
                auto* bl = bp->GetComponent<MinimapBlip>();
                if (!bl || !bp->transform) continue;
                float mx, my;
                bool inside = Minimap::WorldToMapR(*mm, center, bp->transform->Position(), sz.x, sz.y, mapHeading, mx, my);
                if (!inside) {
                    if (!mm->clampBlips) continue;
                    float dx = mx - sz.x*0.5f, dy = my - sz.y*0.5f;
                    float len = std::sqrt(dx*dx + dy*dy); if (len < 1e-3f) continue;
                    float rr = (mm->circular ? radius : (sz.x < sz.y ? sz.x : sz.y) * 0.5f) - mm->blipSize;
                    mx = sz.x*0.5f + dx/len*rr; my = sz.y*0.5f + dy/len*rr;
                }
                int half = (int)(bl->size > 0 ? bl->size : mm->blipSize);
                SDL_Color col{(Uint8)(bl->color.r*255), (Uint8)(bl->color.g*255),
                              (Uint8)(bl->color.b*255), (Uint8)(bl->color.a*255*op)};
                float ang = bl->rotateWithObject ? (Minimap::HeadingOf(bp->transform->Forward(), mm->useXZ) - mapHeading) : 0.0f;
                drawBlip(o.x + mx, o.y + my, half, col, bl->shape, ang);
            }
            // The target marker at the map center.
            {
                int half = (int)(mm->blipSize);
                SDL_Color col{(Uint8)(mm->targetColor.r*255), (Uint8)(mm->targetColor.g*255),
                              (Uint8)(mm->targetColor.b*255), (Uint8)(mm->targetColor.a*255*op)};
                if (mm->playerArrow)
                    drawBlip((float)cx, (float)cy, (int)(half*1.4f), col, MinimapBlip::Shape::Arrow, 0.0f);
                else
                    drawBlip((float)cx, (float)cy, half, col, MinimapBlip::Shape::Square, 0.0f);
            }
            // Restore clip and draw the border on top (rect rings or circle ring).
            if (hadClip) SDL_RenderSetClipRect(renderer, &prevClip);
            else SDL_RenderSetClipRect(renderer, nullptr);
            if (mm->circular) {
                MMDrawRing(renderer, cx, cy, radius, bw, bdCol);
            } else {
                SDL_SetRenderDrawColor(renderer, bdCol.r, bdCol.g, bdCol.b, bdCol.a);
                for (int i = 0; i < bw; ++i) {
                    SDL_Rect e{box.x + i, box.y + i, box.w - 2*i, box.h - 2*i};
                    if (e.w <= 0 || e.h <= 0) break;
                    SDL_RenderDrawRect(renderer, &e);
                }
            }
        }
            else if (_it.kind == K_Crosshair) {   // aim reticle at screen center
            auto* cr = up->GetComponent<Crosshair>();
            if (!cr || !up->active || UIHidden(up.get())) continue;
            float op = UIOpacity(up.get());
            Vec2 o, sz; if (!GetUIScreenRect(up.get(), (float)w, (float)h, o, sz)) continue;
            float cx = o.x + sz.x * 0.5f, cy = o.y + sz.y * 0.5f;
            int th = (int)(cr->thickness > 1 ? cr->thickness : 1);
            // A line drawn as a filled rect, with an optional 1px dark outline under it.
            auto drawLine = [&](int rx, int ry, int rw, int rh) {
                if (cr->outline) {
                    SDL_SetRenderDrawColor(renderer, (Uint8)(cr->outlineColor.r*255), (Uint8)(cr->outlineColor.g*255),
                                           (Uint8)(cr->outlineColor.b*255), (Uint8)(cr->outlineColor.a*255*op));
                    SDL_Rect ol{rx-1, ry-1, rw+2, rh+2};
                    SDL_RenderFillRect(renderer, &ol);
                }
                SDL_SetRenderDrawColor(renderer, (Uint8)(cr->color.r*255), (Uint8)(cr->color.g*255),
                                       (Uint8)(cr->color.b*255), (Uint8)(cr->color.a*255*op));
                SDL_Rect ln{rx, ry, rw, rh};
                SDL_RenderFillRect(renderer, &ln);
            };
            if (cr->showLines) {
                int g0 = (int)(cr->gap + cr->spread), g1 = (int)(cr->gap + cr->spread + cr->length);
                if (cr->lineUp)    drawLine((int)cx - th/2, (int)cy - g1, th, g1 - g0);   // up
                if (cr->lineDown)  drawLine((int)cx - th/2, (int)cy + g0, th, g1 - g0);   // down
                if (cr->lineLeft)  drawLine((int)cx - g1, (int)cy - th/2, g1 - g0, th);   // left
                if (cr->lineRight) drawLine((int)cx + g0, (int)cy - th/2, g1 - g0, th);   // right
            }
            if (cr->circle) {
                int segs = 48; float rr = cr->circleRadius, ct = cr->circleThickness < 1 ? 1 : cr->circleThickness;
                SDL_SetRenderDrawColor(renderer, (Uint8)(cr->circleColor.r*255), (Uint8)(cr->circleColor.g*255),
                                       (Uint8)(cr->circleColor.b*255), (Uint8)(cr->circleColor.a*255*op));
                for (float t = 0; t < ct; t += 1.0f) {
                    float ring = rr + t;
                    for (int i = 0; i < segs; ++i) {
                        float a0 = (float)i / segs * 6.2831853f;
                        SDL_RenderDrawPoint(renderer, (int)(cx + std::cos(a0) * ring), (int)(cy + std::sin(a0) * ring));
                    }
                }
            }
            if (cr->dot) {
                int half = (int)(cr->dotSize * 0.5f); if (half < 1) half = 1;
                if (cr->outline) {
                    SDL_SetRenderDrawColor(renderer, (Uint8)(cr->outlineColor.r*255), (Uint8)(cr->outlineColor.g*255),
                                           (Uint8)(cr->outlineColor.b*255), (Uint8)(cr->outlineColor.a*255*op));
                    SDL_Rect od{(int)cx - half - 1, (int)cy - half - 1, half*2 + 2, half*2 + 2};
                    SDL_RenderFillRect(renderer, &od);
                }
                SDL_SetRenderDrawColor(renderer, (Uint8)(cr->dotColor.r*255), (Uint8)(cr->dotColor.g*255),
                                       (Uint8)(cr->dotColor.b*255), (Uint8)(cr->dotColor.a*255*op));
                SDL_Rect dr{(int)cx - half, (int)cy - half, half*2, half*2};
                SDL_RenderFillRect(renderer, &dr);
            }
        }
            else if (_it.kind == K_Slider) {   // sliders
            auto* sl = up->GetComponent<UISlider>();
            if (!sl || !up->active || UIHidden(up.get())) continue;
            float op = UIOpacity(up.get());
            Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect bg{(int)o.x, (int)o.y, (int)sl->size.x, (int)sl->size.y};
            FillUIShape(renderer, bg, sl->trackShape, sl->cornerRadius,
                        sl->background, sl->background, false, false, op);
            float f = sl->Fraction();
            SDL_Rect fl, kn;
            if (sl->vertical) {
                fl = {(int)o.x, (int)(o.y + sl->size.y * (1.0f - f)), (int)sl->size.x, (int)(sl->size.y * f)};
                int kh = (int)(sl->size.x * sl->knobSize);
                kn = {(int)o.x - 2, (int)(o.y + sl->size.y * (1.0f - f)) - kh / 2, (int)sl->size.x + 4, kh};
            } else {
                fl = {(int)o.x, (int)o.y, (int)(sl->size.x * f), (int)sl->size.y};
                int kw = (int)(sl->size.y * sl->knobSize);
                kn = {(int)(o.x + sl->size.x * f) - kw / 2, (int)o.y - 2, kw, (int)sl->size.y + 4};
            }
            FillUIShape(renderer, fl, sl->trackShape, sl->cornerRadius,
                        sl->fill, sl->fill, false, false, op);
            // The handle: a circle when roundKnob, else a rounded tab.
            FillUIShape(renderer, kn, sl->roundKnob ? UIShape::Circle : UIShape::Rounded,
                        sl->cornerRadius, sl->knob, sl->knob, false, false, op);
            if (sl->showValue) {
                char vbuf[16]; std::snprintf(vbuf, sizeof(vbuf), "%.2f", sl->value);
                float px = 2.0f;
                SDL_Color tc{(Uint8)(sl->textColor.r * 255), (Uint8)(sl->textColor.g * 255),
                             (Uint8)(sl->textColor.b * 255), (Uint8)(sl->textColor.a * 255 * op)};
                DrawText(renderer, vbuf, o.x + sl->size.x + 8.0f,
                         o.y + (sl->size.y - Font8x8::Height * px) * 0.5f, px, tc);
            }
            if (!sl->interactable) { SDL_Rect dr{(int)o.x, (int)o.y, (int)sl->size.x, (int)sl->size.y};
                SDL_SetRenderDrawColor(renderer, 30, 30, 35, 150); SDL_RenderFillRect(renderer, &dr); }
        }
            else if (_it.kind == K_Stepper) {   // numeric steppers
            auto* st = up->GetComponent<UIStepper>();
            if (!st || !up->active || UIHidden(up.get())) continue;
            float op = UIOpacity(up.get());
            Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect bg{(int)o.x, (int)o.y, (int)st->size.x, (int)st->size.y};
            FillUIShape(renderer, bg, st->shape, st->cornerRadius, st->background, st->background, false, false, op);
            float bw = st->ButtonWidth();
            SDL_Rect minusR{(int)o.x, (int)o.y, (int)bw, (int)st->size.y};
            SDL_Rect plusR{(int)(o.x + st->size.x - bw), (int)o.y, (int)bw, (int)st->size.y};
            FillUIShape(renderer, minusR, st->shape, st->cornerRadius, st->button, st->button, false, false, op);
            FillUIShape(renderer, plusR, st->shape, st->cornerRadius, st->button, st->button, false, false, op);
            SDL_Color tc{(Uint8)(st->textColor.r * 255), (Uint8)(st->textColor.g * 255),
                         (Uint8)(st->textColor.b * 255), (Uint8)(st->textColor.a * 255 * op)};
            float px = 2.0f;
            // "-" and "+" glyphs centred on the end buttons.
            DrawText(renderer, "-", o.x + bw * 0.5f - Font8x8::Width * px * 0.5f,
                     o.y + (st->size.y - Font8x8::Height * px) * 0.5f, px, tc);
            DrawText(renderer, "+", o.x + st->size.x - bw * 0.5f - Font8x8::Width * px * 0.5f,
                     o.y + (st->size.y - Font8x8::Height * px) * 0.5f, px, tc);
            char vb[24];
            if (st->wholeNumbers) std::snprintf(vb, sizeof(vb), "%d", (int)st->value);
            else                  std::snprintf(vb, sizeof(vb), "%.2f", st->value);
            float tw = std::strlen(vb) * (Font8x8::Width + 1) * px;
            DrawText(renderer, vb, o.x + (st->size.x - tw) * 0.5f,
                     o.y + (st->size.y - Font8x8::Height * px) * 0.5f, px, tc);
            if (!st->interactable) { SDL_SetRenderDrawColor(renderer, 30, 30, 35, 150); SDL_RenderFillRect(renderer, &bg); }
        }
            else if (_it.kind == K_Rating) {   // star ratings
            auto* rt = up->GetComponent<UIRating>();
            if (!rt || !up->active || UIHidden(up.get()) || rt->count <= 0) continue;
            float op = UIOpacity(up.get());
            Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
            enterScroll(up.get(), o);
            float cw = rt->CellWidth();
            float d = Mathf::Min(cw, rt->size.y);          // star size (square cell)
            for (int i = 0; i < rt->count; ++i) {
                float cx = o.x + i * cw + (cw - d) * 0.5f;
                float cy = o.y + (rt->size.y - d) * 0.5f;
                SDL_Rect star{(int)cx, (int)cy, (int)d, (int)d};
                float f = rt->StarFill(i);              // 0, 0.5 or 1 (uses hover preview)
                FillUIShape(renderer, star, UIShape::Diamond, 0.0f, rt->off, rt->off, false, false, op);
                if (f > 0.0f) {
                    // Reveal the filled color over the left fraction of the star so a
                    // half rating shows a real half-star. Clip to the existing clip so
                    // ratings inside a scroll view stay clipped.
                    SDL_bool wasClip = SDL_RenderIsClipEnabled(renderer);
                    SDL_Rect prev; SDL_RenderGetClipRect(renderer, &prev);
                    SDL_Rect want{star.x, star.y, (int)(star.w * f + 0.5f), star.h}, use = want;
                    if (wasClip) SDL_IntersectRect(&want, &prev, &use);
                    SDL_RenderSetClipRect(renderer, &use);
                    FillUIShape(renderer, star, UIShape::Diamond, 0.0f, rt->on, rt->on, false, false, op);
                    if (wasClip) SDL_RenderSetClipRect(renderer, &prev);
                    else         SDL_RenderSetClipRect(renderer, nullptr);
                }
            }
        }
            else if (_it.kind == K_Toggle) {   // toggles (checkboxes)
            auto* tg = up->GetComponent<UIToggle>();
            if (!tg || !up->active || UIHidden(up.get())) continue;
            float op = UIOpacity(up.get());
            Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect box{(int)o.x, (int)o.y, (int)tg->size.x, (int)tg->size.y};
            float t = tg->AnimT();                          // 0=off..1=on (smoothed)
            if (tg->style == UIToggle::Style::Switch) {     // pill track + sliding knob
                auto mix = [t](const Color& a, const Color& b) {
                    return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                                 a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
                };
                Color trk = mix(tg->boxColor, tg->checkColor);   // cross-fade the track
                FillUIShape(renderer, box, UIShape::Pill, 0.0f, trk, trk, false, false, op);
                int kd = box.h - 4;
                int kx = box.x + 2 + (int)((box.w - kd - 4) * t);   // glide the knob
                SDL_Rect knob{kx, box.y + 2, kd, kd};
                FillUIShape(renderer, knob, UIShape::Circle, 0.0f,
                            tg->knobColor, tg->knobColor, false, false, op);
            } else {
                FillUIShape(renderer, box, UIShape::Rounded, tg->cornerRadius,
                            tg->boxColor, tg->boxColor, false, false, op);
                if (t > 0.01f) {                            // inset check fill (fades in)
                    int pad = (int)(tg->size.x * 0.22f);
                    SDL_Rect chk{box.x + pad, box.y + pad, box.w - 2 * pad, box.h - 2 * pad};
                    Color cc = tg->checkColor; cc.a *= t;
                    FillUIShape(renderer, chk, UIShape::Rounded, tg->cornerRadius * 0.6f,
                                cc, cc, false, false, op);
                }
            }
            float px = 2.0f;
            float tx = o.x + tg->size.x + 8.0f;
            float ty = o.y + (tg->size.y - Font8x8::Height * px) * 0.5f;
            SDL_Color tc{(Uint8)(tg->textColor.r * 255), (Uint8)(tg->textColor.g * 255),
                         (Uint8)(tg->textColor.b * 255), (Uint8)(tg->textColor.a * 255 * op)};
            DrawText(renderer, tg->label, tx, ty, px, tc);
            if (!tg->interactable) { SDL_Rect dr{box.x, box.y, box.w, box.h};
                SDL_SetRenderDrawColor(renderer, 30, 30, 35, 150); SDL_RenderFillRect(renderer, &dr); }
        }
            else if (_it.kind == K_Tabs) {   // segmented tab bars
            auto* tb = up->GetComponent<UITabs>();
            if (!tb || !up->active || UIHidden(up.get()) || tb->Count() <= 0) continue;
            float op = UIOpacity(up.get());
            Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect bar{(int)o.x, (int)o.y, (int)tb->size.x, (int)tb->size.y};
            FillUIShape(renderer, bar, tb->shape, tb->cornerRadius,
                        tb->background, tb->background, false, false, op);
            // Highlight the selected segment (inset a touch so the track frames it).
            float sox, soy, sw, sh; tb->SegmentRect(tb->value, sox, soy, sw, sh);
            SDL_Rect sel{(int)(o.x + sox) + 2, (int)(o.y + soy) + 2, (int)sw - 4, (int)sh - 4};
            FillUIShape(renderer, sel, tb->shape, tb->cornerRadius,
                        tb->selected, tb->selected, false, false, op);
            // Labels centered in each segment.
            float px = 2.0f;
            for (int i = 0; i < tb->Count(); ++i) {
                float ox, oy, sgw, sgh; tb->SegmentRect(i, ox, oy, sgw, sgh);
                const std::string& lbl = tb->tabs[i];
                float tw = lbl.size() * (Font8x8::Width + 1) * px;
                const Color& c = (i == tb->value) ? tb->selectedTextColor : tb->textColor;
                SDL_Color tc{(Uint8)(c.r * 255), (Uint8)(c.g * 255), (Uint8)(c.b * 255), (Uint8)(c.a * 255 * op)};
                DrawText(renderer, lbl, o.x + ox + (sgw - tw) * 0.5f,
                         o.y + oy + (sgh - Font8x8::Height * px) * 0.5f, px, tc);
            }
            if (!tb->interactable) { SDL_SetRenderDrawColor(renderer, 30, 30, 35, 150); SDL_RenderFillRect(renderer, &bar); }
        }
            else if (_it.kind == K_Button) {   // buttons (box, icon, label)
            auto* btn = up->GetComponent<UIButton>();
            if (!btn || !up->active || UIHidden(up.get())) continue;
            float op = UIOpacity(up.get());
            Color bg = btn->DisplayColor();
            // World-aware rect + scale: a 3D button (own object / world canvas)
            // projects through the camera, so its box and label shrink with
            // distance. A screen-space button keeps its design size and k=1,
            // unchanged from before.
            float bk = 1.0f;
            Vec2 o, bsz;
            if (btn->IsWorldSpaceUI()) {
                GetUIScreenRect(up.get(), (float)w, (float)h, o, bsz, &bk);
            } else {
                o = UIResolveOrigin(up.get(), (float)w, (float)h);
                bsz = btn->size;
            }
            enterScroll(up.get(), o);
            SDL_Rect r{(int)o.x, (int)o.y, (int)bsz.x, (int)bsz.y};
            if (btn->hoverScale != 1.0f && (btn->IsHovered() || btn->IsFocused())) {
                int gx = (int)(bsz.x * (btn->hoverScale - 1.0f) * 0.5f);
                int gy = (int)(bsz.y * (btn->hoverScale - 1.0f) * 0.5f);
                r.x -= gx; r.y -= gy; r.w += 2 * gx; r.h += 2 * gy;
            }
            if (btn->shadow) {                              // drop shadow behind (same shape)
                SDL_Rect sh{r.x + (int)btn->shadowOffset.x, r.y + (int)btn->shadowOffset.y, r.w, r.h};
                FillUIShadow(renderer, sh, btn->shape, btn->cornerRadius,
                             btn->shadowColor, btn->shadowSoftness, op);
            }
            if (btn->borderWidth > 0.0f) {                  // border = outer shape, then inner fill
                FillUIShape(renderer, r, btn->shape, btn->cornerRadius,
                            btn->borderColor, btn->borderColor, false, false, op);
                int b = (int)btn->borderWidth;
                SDL_Rect inner{r.x + b, r.y + b, r.w - 2 * b, r.h - 2 * b};
                float innerR = btn->cornerRadius - b; if (innerR < 0.0f) innerR = 0.0f;
                FillUIShape(renderer, inner, btn->shape, innerR, bg, bg, false, false, op);
            } else {
                FillUIShape(renderer, r, btn->shape, btn->cornerRadius, bg, bg, false, false, op);
            }
            // Optional icon (left by default, right when iconRight); the label
            // takes the remaining space. Press shifts content down slightly.
            float shift = btn->PressShift() * bk;
            float isz = (!btn->icon.empty() && btn->iconSize > 0.0f) ? btn->iconSize * bk : 0.0f;
            if (isz > 0.0f) {
                SDL_Texture* itex = GetTexture(renderer, btn->icon, baseDir, textureCache);
                float ix = btn->iconRight ? (o.x + bsz.x - isz - 8.0f * bk) : (o.x + 8.0f * bk);
                SDL_Rect ir{(int)ix, (int)(o.y + (bsz.y - isz) * 0.5f + shift), (int)isz, (int)isz};
                if (itex) { SDL_SetTextureColorMod(itex, 255, 255, 255); SDL_SetTextureAlphaMod(itex, 255);
                            SDL_RenderCopy(renderer, itex, nullptr, &ir); }
            }
            // Center the label at the button's font scale, within the area beside
            // the icon. Skip the built-in label when a child Text object provides it
            // (Unity-style Button→Text) — that child draws itself in the text pass.
            if (!UIButtonTextChild(up.get())) {
                float px = btn->fontScale * bk;
                okay::TtfFont* fnt = okay::GetFont(btn->fontPath);
                float tw = fnt ? fnt->Measure(btn->label.c_str(), (float)Font8x8::Height) * px
                               : btn->label.size() * (Font8x8::Width + 1) * px;
                float left  = o.x + (isz > 0.0f && !btn->iconRight ? isz + 12.0f * bk : 0.0f);
                float right = o.x + bsz.x - (isz > 0.0f && btn->iconRight ? isz + 12.0f * bk : 0.0f);
                float tx = left + ((right - left) - tw) * 0.5f;
                float ty = o.y + (bsz.y - Font8x8::Height * px) * 0.5f + shift;
                Color tcc = btn->CurrentTextColor();
                SDL_Color tc{(Uint8)(tcc.r * 255), (Uint8)(tcc.g * 255), (Uint8)(tcc.b * 255), (Uint8)(tcc.a * 255 * op)};
                DrawText(renderer, btn->label, tx, ty, px, tc, 0, 0, false, false, SDL_Color{0,0,0,0}, fnt);
            }
        }
            else if (_it.kind == K_Text) {   // screen-space text — on top of panels/controls
            auto* tr = up->GetComponent<TextRenderer>();
            if (!tr || !up->active || !tr->screenSpace || UIHidden(up.get())) continue;
            float op = UIOpacity(up.get());   // canvas master fade
            SDL_Color col{(Uint8)(tr->color.r * 255), (Uint8)(tr->color.g * 255),
                          (Uint8)(tr->color.b * 255), (Uint8)(tr->color.a * 255 * op)};
            SDL_Color sh{(Uint8)(tr->shadowColor.r * 255), (Uint8)(tr->shadowColor.g * 255),
                         (Uint8)(tr->shadowColor.b * 255), (Uint8)(tr->shadowColor.a * 255 * op)};
            SDL_Color ol{(Uint8)(tr->outlineColor.r * 255), (Uint8)(tr->outlineColor.g * 255),
                         (Uint8)(tr->outlineColor.b * 255), (Uint8)(tr->outlineColor.a * 255 * op)};
            // In-world text projects through the camera (so it sits in 3D like the
            // other widgets) instead of anchoring to the screen — either under a
            // world-space Canvas, or as a standalone WorldSpaceUI object (its own 3D
            // text). UIResolveOrigin/UIScaleFor are world-aware; pixelSize scales by k.
            Canvas* tcv = OwningCanvas(up.get());
            bool tWorld = (tcv && tcv->worldSpace) || WorldUIRoot(up.get()) != nullptr;
            float p = tr->pixelSize, ls = tr->letterSpacing, lp = tr->lineSpacing;
            float tk = 1.0f;
            if (tWorld) {
                tk = UIScaleFor(up.get(), (float)w, (float)h);
                if (tk <= 0.0f) continue;   // canvas behind the camera
                p *= tk;
            }
            if (tr->background) {                       // label background box
                Vec2 b = UIResolveOrigin(up.get(), (float)w, (float)h);   // parent-relative box
                SDL_Rect br{(int)b.x, (int)b.y, (int)(tr->size.x * tk), (int)(tr->size.y * tk)};
                SDL_SetRenderDrawColor(renderer, (Uint8)(tr->backgroundColor.r * 255),
                                       (Uint8)(tr->backgroundColor.g * 255), (Uint8)(tr->backgroundColor.b * 255),
                                       (Uint8)(tr->backgroundColor.a * 255 * op));
                SDL_RenderFillRect(renderer, &br);
            }
            Vec2 o = tWorld ? UIResolveOrigin(up.get(), (float)w, (float)h)
                            : UITextDrawOrigin(up.get(), tr, (float)w, (float)h);   // parent-relative + align
            std::string disp = tr->DisplayText();
            bool it = tr->italic;
            SDL_Color c2{(Uint8)(tr->colorBottom.r * 255), (Uint8)(tr->colorBottom.g * 255),
                         (Uint8)(tr->colorBottom.b * 255), (Uint8)(tr->colorBottom.a * 255 * op)};
            okay::TtfFont* fnt = tr->Font();
            if (tr->shadow)
                DrawText(renderer, disp, o.x + tr->shadowOffset.x * p,
                         o.y + tr->shadowOffset.y * p, p, sh, ls, lp, it, false, SDL_Color{0,0,0,0}, fnt);
            if (tr->outline) {
                DrawText(renderer, disp, o.x - p, o.y, p, ol, ls, lp, it, false, SDL_Color{0,0,0,0}, fnt);
                DrawText(renderer, disp, o.x + p, o.y, p, ol, ls, lp, it, false, SDL_Color{0,0,0,0}, fnt);
                DrawText(renderer, disp, o.x, o.y - p, p, ol, ls, lp, it, false, SDL_Color{0,0,0,0}, fnt);
                DrawText(renderer, disp, o.x, o.y + p, p, ol, ls, lp, it, false, SDL_Color{0,0,0,0}, fnt);
            }
            DrawText(renderer, disp, o.x, o.y, p, col, ls, lp, it, tr->gradient, c2, fnt);
            if (tr->bold) DrawText(renderer, disp, o.x + p, o.y, p, col, ls, lp, it, tr->gradient, c2, fnt);
        }
            else if (_it.kind == K_WorldUI) {   // in-world UI labels/markers (3D -> screen)
            auto* wu = up->GetComponent<WorldUI>();
            if (!wu || !up->active || UIHidden(up.get()) || !up->transform) continue;
            Camera* mc = scene.mainCamera;
            if (!mc) continue;
            Vec3 wp = up->transform->Position() + wu->worldOffset;
            Vec2 sp; float depth = 0.0f;
            if (!mc->WorldToScreen(wp, (float)w, (float)h, sp, &depth)) continue;   // behind camera
            if (wu->maxDistance > 0.0f && depth > wu->maxDistance) continue;
            float scale = wu->pixelSize;
            if (wu->scaleWithDistance && depth > 0.001f)
                scale = Mathf::Clamp(wu->pixelSize * (wu->refDistance / depth),
                                     wu->pixelSize * wu->minScale, wu->pixelSize * wu->maxScale);
            float op = UIOpacity(up.get());
            float tw = wu->text.size() * (Font8x8::Width + 1) * scale;
            float th = Font8x8::Height * scale;
            float tx = sp.x - tw * 0.5f, ty = sp.y - th * 0.5f;
            if (wu->background.a > 0.001f) {
                SDL_Rect bg{(int)(tx - 5), (int)(ty - 4), (int)(tw + 10), (int)(th + 8)};
                FillUIShape(renderer, bg, UIShape::Rounded, 4.0f, wu->background, wu->background, false, false, op);
            }
            SDL_Color tc{(Uint8)(wu->color.r * 255), (Uint8)(wu->color.g * 255),
                         (Uint8)(wu->color.b * 255), (Uint8)(wu->color.a * 255 * op)};
            DrawText(renderer, wu->text, tx, ty, scale, tc);
            if (wu->bar >= 0.0f) {                          // optional health/progress bar under the text
                float bw = tw > 36.0f ? tw : 36.0f;
                float bx = sp.x - bw * 0.5f, by = ty + th + 3.0f, bh = 4.0f * scale;
                SDL_Rect bgb{(int)bx, (int)by, (int)bw, (int)bh};
                FillUIShape(renderer, bgb, UIShape::Rounded, 2.0f, wu->barBackground, wu->barBackground, false, false, op);
                SDL_Rect fb{(int)bx, (int)by, (int)(bw * Mathf::Clamp01(wu->bar)), (int)bh};
                FillUIShape(renderer, fb, UIShape::Rounded, 2.0f, wu->barColor, wu->barColor, false, false, op);
            }
        }
            else if (_it.kind == K_Dropdown) {   // dropdowns (header + open list)
            auto* dd = up->GetComponent<UIDropdown>();
            if (!dd || !up->active || UIHidden(up.get())) continue;
            Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect hdr{(int)o.x, (int)o.y, (int)dd->size.x, (int)dd->size.y};
            FillUIShape(renderer, hdr, dd->shape, dd->cornerRadius,
                        dd->color, dd->color, false, false, 1.0f);
            float px = 2.0f;
            float ty = o.y + (dd->size.y - Font8x8::Height * px) * 0.5f;
            SDL_Color tc{(Uint8)(dd->textColor.r * 255), (Uint8)(dd->textColor.g * 255),
                         (Uint8)(dd->textColor.b * 255), (Uint8)(dd->textColor.a * 255)};
            SDL_Color htc = dd->HasSelection() ? tc : SDL_Color{150, 152, 158, 255};
            DrawText(renderer, dd->HeaderText(), o.x + 8.0f, ty, px, htc);
            if (dd->open) {
                float top = o.y + dd->size.y;
                for (int i = 0; i < (int)dd->options.size(); ++i) {
                    SDL_Rect orow{(int)o.x, (int)(top + i * dd->size.y), (int)dd->size.x, (int)dd->size.y};
                    const Color& rc = (i == dd->HoveredOption()) ? dd->hoverColor : dd->listColor;
                    SDL_SetRenderDrawColor(renderer, (Uint8)(rc.r * 255), (Uint8)(rc.g * 255),
                                           (Uint8)(rc.b * 255), (Uint8)(rc.a * 255));
                    SDL_RenderFillRect(renderer, &orow);
                    DrawText(renderer, dd->options[i], o.x + 8.0f,
                             orow.y + (dd->size.y - Font8x8::Height * px) * 0.5f, px, tc);
                }
            }
            if (!dd->interactable) { SDL_Rect dr{(int)o.x, (int)o.y, (int)dd->size.x, (int)dd->size.y};
                SDL_SetRenderDrawColor(renderer, 30, 30, 35, 150); SDL_RenderFillRect(renderer, &dr); }
        }
            else if (_it.kind == K_Input) {   // input fields (box + text + caret)
            auto* in = up->GetComponent<UIInputField>();
            if (!in || !up->active || UIHidden(up.get())) continue;
            Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect box{(int)o.x, (int)o.y, (int)in->size.x, (int)in->size.y};
            Color bg = in->CurrentColor();
            // Focus ring: draw the border shape behind, then the field inset over it.
            if (in->focused && in->borderWidth > 0.0f) {
                FillUIShape(renderer, box, in->shape, in->cornerRadius,
                            in->borderColor, in->borderColor, false, false, 1.0f);
                int b = (int)in->borderWidth;
                SDL_Rect inner{box.x + b, box.y + b, box.w - 2 * b, box.h - 2 * b};
                float ir = in->cornerRadius - b; if (ir < 0.0f) ir = 0.0f;
                FillUIShape(renderer, inner, in->shape, ir, bg, bg, false, false, 1.0f);
            } else {
                FillUIShape(renderer, box, in->shape, in->cornerRadius, bg, bg, false, false, 1.0f);
            }
            float px = 2.0f, pad = 6.0f;
            bool empty = in->text.empty();
            std::string full = empty ? in->placeholder : in->DisplayText();
            const Color& txc = empty ? in->placeholderColor : in->textColor;
            // Horizontal scroll: show the tail that fits so the caret stays visible.
            float adv = (Font8x8::Width + 1) * px;
            int fit = adv > 0 ? (int)((in->size.x - pad * 2) / adv) : (int)full.size();
            if (fit < 1) fit = 1;
            std::string shown = ((int)full.size() > fit) ? full.substr(full.size() - fit) : full;
            float ty = o.y + (in->size.y - Font8x8::Height * px) * 0.5f;
            SDL_Color tc{(Uint8)(txc.r * 255), (Uint8)(txc.g * 255), (Uint8)(txc.b * 255), (Uint8)(txc.a * 255)};
            DrawText(renderer, shown, o.x + pad, ty, px, tc);
            if (in->focused && in->CaretVisible()) {        // blinking caret after visible text
                int cx = (int)(o.x + pad + shown.size() * adv);
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 220);
                SDL_RenderDrawLine(renderer, cx, (int)ty, cx, (int)(ty + Font8x8::Height * px));
            }
        }
            else if (_it.kind == K_FocusRing) {   // keyboard/gamepad focus ring
            SDL_RenderSetClipRect(renderer, nullptr);   // overlay — never scroll-clipped
            if (!up->active || !IsUIFocused(up.get())) continue;
            UIRect r = GetUIRect(up.get());
            if (!r.valid || !r.position) continue;
            Vec2 o = ResolveAnchor(r.anchor, *r.position, r.size, (float)w, (float)h);
            SDL_Rect ring{(int)o.x - 2, (int)o.y - 2, (int)r.size.x + 4, (int)r.size.y + 4};
            SDL_SetRenderDrawColor(renderer, 255, 210, 90, 255);
            SDL_RenderDrawRect(renderer, &ring);
            SDL_Rect ring2{ring.x - 1, ring.y - 1, ring.w + 2, ring.h + 2};
            SDL_RenderDrawRect(renderer, &ring2);
        }
            else if (_it.kind == K_Tooltip) {   // tooltips (hover hints)
            SDL_RenderSetClipRect(renderer, nullptr);   // overlay — never scroll-clipped
            auto* tt = up->GetComponent<UITooltip>();
            if (!tt || !up->active || !tt->Ready()) continue;
            Vec2 m = Input::MousePosition();
            float px = 2.0f;
            float tw = tt->text.size() * (Font8x8::Width + 1) * px;
            float th = Font8x8::Height * px;
            SDL_Rect box{(int)(m.x + 14), (int)(m.y + 14), (int)(tw + 12), (int)(th + 10)};
            SDL_SetRenderDrawColor(renderer, (Uint8)(tt->background.r * 255), (Uint8)(tt->background.g * 255),
                                   (Uint8)(tt->background.b * 255), (Uint8)(tt->background.a * 255));
            SDL_RenderFillRect(renderer, &box);
            SDL_SetRenderDrawColor(renderer, (Uint8)(tt->borderColor.r * 255), (Uint8)(tt->borderColor.g * 255),
                                   (Uint8)(tt->borderColor.b * 255), (Uint8)(tt->borderColor.a * 255));
            SDL_RenderDrawRect(renderer, &box);
            SDL_Color tc{(Uint8)(tt->textColor.r * 255), (Uint8)(tt->textColor.g * 255),
                         (Uint8)(tt->textColor.b * 255), (Uint8)(tt->textColor.a * 255)};
            DrawText(renderer, tt->text, m.x + 20, m.y + 19, px, tc);
            }
        }   // end single UI draw pass
        g_uiDefaultFont = nullptr;   // world text / FPS overlay keep the bitmap font
        SDL_RenderSetClipRect(renderer, nullptr);   // end any scroll clipping

        // Optional FPS overlay (top-left) when enabled in build settings.
        if (cfg.showFps) {
            static float fpsSmooth = 0.0f;
            float inst = dt > 0.0001f ? 1.0f / dt : 0.0f;
            fpsSmooth = fpsSmooth <= 0.0f ? inst : fpsSmooth * 0.9f + inst * 0.1f;
            char buf[32]; std::snprintf(buf, sizeof(buf), "FPS %d", (int)(fpsSmooth + 0.5f));
            float px = 2.0f;
            float tw = (float)std::char_traits<char>::length(buf) * (Font8x8::Width + 1) * px;
            SDL_Rect bg{4, 4, (int)(tw + 8), (int)(Font8x8::Height * px + 8)};
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
            SDL_RenderFillRect(renderer, &bg);
            DrawText(renderer, buf, 8, 8, px, SDL_Color{80, 255, 120, 255});
        }
        UIWorld().active = false;   // end of the frame's world-space UI projection
#ifdef OKAY_HAVE_OKAYUI
        // Scripts drew their UI during scene.Update (via the bridge); add the optional
        // F1 "Test UI" panel on top, then flush the whole OkayUI frame here so it
        // composites above the game.
        if (g_testUI) okay_testui::Panel(24.0f, 24.0f);
        OkayUI::EndFrame(renderer);
        SDL_StartTextInput();   // keep text flowing for OkayUI text fields
#endif
        // Minecraft-style inventory hotbar / backpack (under the loading screen).
        for (const auto& up : scene.Objects()) {
            auto* ui = up ? up->GetComponent<InventoryUI>() : nullptr;
            if (!ui) continue;
            DrawInventoryUI(renderer, *ui, baseDir, textureCache);
            break;
        }
        for (const auto& up : scene.Objects()) {
            auto* gui = up ? up->GetComponent<GridInventoryUI>() : nullptr;
            if (!gui) continue;
            DrawGridInventory(renderer, *gui, baseDir, textureCache);
            break;
        }
        for (const auto& up : scene.Objects()) {
            auto* cs = up ? up->GetComponent<CraftingStation>() : nullptr;
            if (!cs) continue;
            DrawCrafting(renderer, *cs);
            break;
        }
        for (const auto& up : scene.Objects()) {
            auto* ce = up ? up->GetComponent<ChestInventory>() : nullptr;
            if (!ce || !ce->open) continue;
            DrawChest(renderer, *ce, baseDir, textureCache);
            break;
        }
        // Loading-screen overlay: drawn on top of everything (and the UI) when active.
        for (const auto& up : scene.Objects()) {
            auto* ls = up ? up->GetComponent<LoadingScreen>() : nullptr;
            if (!ls || !ls->Active()) continue;
            DrawLoadingScreen(renderer, *ls, baseDir, textureCache);
            break;
        }
        SDL_RenderPresent(renderer);

        // Optional frame-rate cap: sleep the remainder of the frame budget.
        if (cfg.fpsCap > 0) {
            double budget = 1.0 / cfg.fpsCap;
            double freq = (double)SDL_GetPerformanceFrequency();
            double elapsed = (SDL_GetPerformanceCounter() - fStart) / freq;
            double remain = budget - elapsed;
            if (remain > 0.0015) SDL_Delay((Uint32)((remain - 0.0005) * 1000.0));
        }
    };

    // Drive the frame loop: the browser owns the loop on web (a blocking while
    // would freeze the tab), so register the frame with Emscripten there.
#ifdef __EMSCRIPTEN__
    (void)running;
    static auto* s_frame = &frame;
    emscripten_set_main_loop([]() { (*s_frame)(); }, 0, 1);
#else
    while (running) frame();
#endif

    Prefs::Save(prefsPath); // persist any prefs the game set this session

    for (auto& kv : textureCache)
        if (kv.second) SDL_DestroyTexture(kv.second);
    if (mesh3DTex) SDL_DestroyTexture(mesh3DTex);
    if (audioDev) SDL_CloseAudioDevice(audioDev);
#if defined(_WIN32)
    if (d3dRenderer) { d3dRenderer->Destroy(); delete d3dRenderer; }
#endif
    if (glRenderer) {
        if (glWindow && glCtx) SDL_GL_MakeCurrent(glWindow, glCtx);
        glRenderer->Destroy(); delete glRenderer;
        SDL_GL_MakeCurrent(nullptr, nullptr);
    }
    if (glCtx) SDL_GL_DeleteContext(glCtx);
    if (glWindow) SDL_DestroyWindow(glWindow);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
