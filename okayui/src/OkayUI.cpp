#include "okay/UI/OkayUI.hpp"
#include "okay/Graphics/Font.hpp"   // okay::Font8x8 — a no-STL C API over a bitmap font
#include <SDL.h>

// All rendering goes through SDL_RenderGeometry (SDL >= 2.0.18), which is exactly
// the call Dear ImGui's SDL_Renderer backend uses — so OkayUI shares ImGui's GPU
// pipeline (Direct3D 11 on Windows, Metal on macOS, OpenGL on Linux) and can never
// conflict with it. No STL is used here: fixed C arrays, C strings, plain ints.

namespace OkayUI {
namespace {

// Fixed-capacity geometry batch. Text is drawn as one small quad per lit font
// pixel, so size generously for several labelled buttons per frame; on overflow we
// silently drop further geometry rather than grow/allocate.
constexpr int kMaxVerts = 20000;
constexpr int kMaxIdx   = 30000;
SDL_Vertex g_verts[kMaxVerts];
int        g_nv = 0;
int        g_idx[kMaxIdx];
int        g_ni = 0;

Input g_in;                      // this frame's input
bool  g_prevDown = false;        // last frame's mouse-down (for edge detection)
bool  g_pressed  = false;        // mouse went down this frame
bool  g_released = false;        // mouse went up this frame
int   g_hot = 0, g_active = 0;   // hovered id / pressed-and-holding id (0 = none)
int   g_focus = 0;               // keyboard-focused widget id (TextField); 0 = none
bool  g_focusClaimed = false;    // a widget took the click this frame (keep focus)
unsigned g_frame = 0;            // frame counter (drives the text caret blink)
Theme g_theme;

// Length of a C string without pulling in <cstring> (keeps the toolkit STL/libc-light).
inline int cstrlen(const char* s) { int n = 0; if (s) while (s[n]) ++n; return n; }

inline SDL_Color toColor(const unsigned char c[4]) {
    SDL_Color sc; sc.r = c[0]; sc.g = c[1]; sc.b = c[2]; sc.a = c[3]; return sc;
}

// Append an axis-aligned rectangle (two triangles) in a flat color.
void quad(float x, float y, float w, float h, const unsigned char c[4]) {
    if (g_nv + 4 > kMaxVerts || g_ni + 6 > kMaxIdx) return;   // overflow: drop silently
    const SDL_Color sc = toColor(c);
    const int base = g_nv;
    SDL_FPoint uv; uv.x = 0.0f; uv.y = 0.0f;
    SDL_Vertex v;
    v.color = sc; v.tex_coord = uv;
    v.position.x = x;     v.position.y = y;     g_verts[g_nv++] = v;
    v.position.x = x + w; v.position.y = y;     g_verts[g_nv++] = v;
    v.position.x = x + w; v.position.y = y + h; g_verts[g_nv++] = v;
    v.position.x = x;     v.position.y = y + h; g_verts[g_nv++] = v;
    g_idx[g_ni++] = base + 0; g_idx[g_ni++] = base + 1; g_idx[g_ni++] = base + 2;
    g_idx[g_ni++] = base + 0; g_idx[g_ni++] = base + 2; g_idx[g_ni++] = base + 3;
}

// Draw a C string from the 8x8 bitmap font, each lit pixel a `s`x`s` quad.
void drawText(float x, float y, const char* str, float s, const unsigned char c[4]) {
    if (!str) return;
    float penX = x;
    for (const char* p = str; *p; ++p) {
        for (int gy = 0; gy < okay::Font8x8::Height; ++gy)
            for (int gx = 0; gx < okay::Font8x8::Width; ++gx)
                if (okay::Font8x8::Pixel(*p, gx, gy))
                    quad(penX + gx * s, y + gy * s, s, s, c);
        penX += okay::Font8x8::Width * s;
    }
}

inline bool pointIn(float px, float py, float x, float y, float w, float h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

} // namespace

void BeginFrame(const Input& in) {
    g_in       = in;
    g_pressed  =  in.mouseDown && !g_prevDown;
    g_released = !in.mouseDown &&  g_prevDown;
    g_prevDown = in.mouseDown;
    g_hot = 0;
    g_focusClaimed = false;
    ++g_frame;
    g_nv = 0; g_ni = 0;
}

bool Button(int id, float x, float y, float w, float h, const char* label) {
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, w, h);
    if (inside) g_hot = id;

    // Hot/active (Casey-style) interaction: arm on press-inside, fire on
    // release-inside. A release outside cancels without firing.
    bool clicked = false;
    if (g_active == id) {
        if (g_released) { if (inside) clicked = true; g_active = 0; }
    } else if (inside && g_pressed) {
        g_active = id;
    }

    const unsigned char* bg = g_theme.bg;
    if (g_active == id && inside) bg = g_theme.bgDown;
    else if (inside)             bg = g_theme.bgHover;

    // Border frame, then the inset fill.
    quad(x, y, w, h, g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) quad(x + b, y + b, w - 2.0f * b, h - 2.0f * b, bg);

    // Centered label.
    if (label && *label) {
        const float s  = g_theme.textScale;
        const float tw = okay::Font8x8::MeasureWidth(label) * s;
        const float th = okay::Font8x8::Height * s;
        drawText(x + (w - tw) * 0.5f, y + (h - th) * 0.5f, label, s, g_theme.text);
    }
    return clicked;
}

void Label(float x, float y, const char* text) {
    if (text && *text) drawText(x, y, text, g_theme.textScale, g_theme.text);
}

void Panel(float x, float y, float w, float h) {
    quad(x, y, w, h, g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) quad(x + b, y + b, w - 2.0f * b, h - 2.0f * b, g_theme.panel);
}

bool Checkbox(int id, float x, float y, float size, const char* label, bool* value) {
    if (!value) return false;
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, size, size);
    if (inside) g_hot = id;

    bool changed = false;
    if (g_active == id) {
        if (g_released) { if (inside) { *value = !*value; changed = true; } g_active = 0; }
    } else if (inside && g_pressed) {
        g_active = id;
    }

    const unsigned char* bg = g_theme.bg;
    if (g_active == id && inside) bg = g_theme.bgDown;
    else if (inside)             bg = g_theme.bgHover;

    // Box frame + fill.
    quad(x, y, size, size, g_theme.border);
    const float b = g_theme.borderPx;
    if (size > 2.0f * b) quad(x + b, y + b, size - 2.0f * b, size - 2.0f * b, bg);
    // Check mark: a centered accent square when ticked.
    if (*value) {
        const float pad = size * 0.28f;
        quad(x + pad, y + pad, size - 2.0f * pad, size - 2.0f * pad, g_theme.accent);
    }
    // Label to the right, vertically centered against the box.
    if (label && *label) {
        const float s  = g_theme.textScale;
        const float th = okay::Font8x8::Height * s;
        drawText(x + size + 8.0f, y + (size - th) * 0.5f, label, s, g_theme.text);
    }
    return changed;
}

bool Slider(int id, float x, float y, float w, float h, float* value, float minV, float maxV) {
    if (!value || w <= 0.0f || maxV <= minV) return false;
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, w, h);
    if (inside) g_hot = id;

    bool changed = false;
    auto setFromMouse = [&]() {
        const float t  = clamp01((g_in.mouseX - x) / w);
        const float nv = minV + t * (maxV - minV);
        if (nv != *value) { *value = nv; changed = true; }
    };
    if (g_active == id) {
        if (!g_in.mouseDown) g_active = 0;   // released -> stop dragging
        else setFromMouse();                 // drag tracks the cursor
    } else if (inside && g_pressed) {
        g_active = id;
        setFromMouse();                      // jump to the click position
    }

    // Groove, then the filled portion, then the handle.
    const float t = clamp01((*value - minV) / (maxV - minV));
    const float gy = y + h * 0.5f - 3.0f;     // 6px groove, vertically centered
    quad(x, gy, w, 6.0f, g_theme.track);
    quad(x, gy, w * t, 6.0f, g_theme.accent);
    const float hw = 10.0f;                    // handle width
    float hx = x + w * t - hw * 0.5f;
    if (hx < x) hx = x; if (hx > x + w - hw) hx = x + w - hw;
    const unsigned char* hc = (g_active == id || inside) ? g_theme.text : g_theme.accent;
    quad(hx, y, hw, h, g_theme.border);
    quad(hx + 1.0f, y + 1.0f, hw - 2.0f, h - 2.0f, hc);
    return changed;
}

void ProgressBar(float x, float y, float w, float h, float t) {
    t = clamp01(t);
    quad(x, y, w, h, g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) {
        quad(x + b, y + b, w - 2.0f * b, h - 2.0f * b, g_theme.track);
        const float iw = (w - 2.0f * b) * t;
        if (iw > 0.0f) quad(x + b, y + b, iw, h - 2.0f * b, g_theme.accent);
    }
}

bool RadioButton(int id, float x, float y, float size, const char* label, int* value, int option) {
    if (!value) return false;
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, size, size);
    if (inside) g_hot = id;

    bool changed = false;
    if (g_active == id) {
        if (g_released) { if (inside && *value != option) { *value = option; changed = true; } g_active = 0; }
    } else if (inside && g_pressed) {
        g_active = id;
    }

    const unsigned char* bg = g_theme.bg;
    if (g_active == id && inside) bg = g_theme.bgDown;
    else if (inside)             bg = g_theme.bgHover;

    quad(x, y, size, size, g_theme.border);
    const float b = g_theme.borderPx;
    if (size > 2.0f * b) quad(x + b, y + b, size - 2.0f * b, size - 2.0f * b, bg);
    // Selected indicator: a smaller centered accent dot (distinct from the checkbox).
    if (*value == option) {
        const float pad = size * 0.32f;
        quad(x + pad, y + pad, size - 2.0f * pad, size - 2.0f * pad, g_theme.accent);
    }
    if (label && *label) {
        const float s  = g_theme.textScale;
        const float th = okay::Font8x8::Height * s;
        drawText(x + size + 8.0f, y + (size - th) * 0.5f, label, s, g_theme.text);
    }
    return changed;
}

bool Tab(int id, float x, float y, float w, float h, const char* label, int* current, int index) {
    if (!current) return false;
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, w, h);
    if (inside) g_hot = id;

    if (g_active == id) {
        if (g_released) { if (inside) *current = index; g_active = 0; }
    } else if (inside && g_pressed) {
        g_active = id;
    }

    const bool selected = (*current == index);
    const unsigned char* bg = selected ? g_theme.bg : g_theme.track;
    if (!selected && inside) bg = g_theme.bgHover;

    quad(x, y, w, h, g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) quad(x + b, y + b, w - 2.0f * b, h - 2.0f * b, bg);
    if (selected) quad(x, y, w, 3.0f, g_theme.accent);   // accent strip on the active tab
    if (label && *label) {
        const float s  = g_theme.textScale;
        const float tw = okay::Font8x8::MeasureWidth(label) * s;
        const float th = okay::Font8x8::Height * s;
        drawText(x + (w - tw) * 0.5f, y + (h - th) * 0.5f, label, s, g_theme.text);
    }
    return *current == index;
}

bool TextField(int id, float x, float y, float w, float h, char* buf, int cap) {
    if (!buf || cap < 1) return false;
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, w, h);
    if (inside) g_hot = id;
    if (inside && g_pressed) { g_focus = id; g_focusClaimed = true; }
    const bool focused = (g_focus == id);

    // Apply keystrokes when focused (the host only forwards text/backspace it wants
    // OkayUI to consume, e.g. when ImGui isn't capturing the keyboard).
    bool changed = false;
    if (focused) {
        if (g_in.text && g_in.text[0]) {
            int len = cstrlen(buf);
            for (const char* p = g_in.text; *p && len < cap - 1; ++p) buf[len++] = *p;
            buf[len] = '\0';
            changed = true;
        }
        if (g_in.backspace) {
            int len = cstrlen(buf);
            if (len > 0) { buf[len - 1] = '\0'; changed = true; }
        }
    }

    // Box (focused gets a subtly lighter fill + accent border hint).
    quad(x, y, w, h, focused ? g_theme.accent : g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) quad(x + b, y + b, w - 2.0f * b, h - 2.0f * b, g_theme.track);

    // Show the trailing characters that fit (so the caret stays visible) — this
    // avoids needing a scissor rect across the batched geometry.
    const float s     = g_theme.textScale;
    const float charW = okay::Font8x8::Width * s;
    const float th    = okay::Font8x8::Height * s;
    const float pad   = 6.0f;
    const float innerW = w - 2.0f * pad;
    const int   maxCh  = charW > 0.0f ? (int)(innerW / charW) : 0;
    const int   len    = cstrlen(buf);
    const char* shown  = (maxCh > 0 && len > maxCh) ? buf + (len - maxCh) : buf;
    const float tx = x + pad, ty = y + (h - th) * 0.5f;
    drawText(tx, ty, shown, s, g_theme.text);

    // Blinking caret at the end of the shown text when focused.
    if (focused && (g_frame / 30u) % 2u == 0u) {
        const int shownLen = cstrlen(shown);
        const float cx = tx + shownLen * charW;
        if (cx < x + w - 2.0f) quad(cx, ty, 2.0f, th, g_theme.text);
    }
    return changed;
}

void EndFrame(SDL_Renderer* r) {
    if (g_released) g_active = 0;   // safety: clear if the active button wasn't drawn
    if (g_pressed && !g_focusClaimed) g_focus = 0;   // click on empty space drops keyboard focus
    if (!r || g_nv == 0 || g_ni == 0) return;
    // Save/restore the renderer's blend mode so we never disturb ImGui or anything
    // else that draws through the same SDL_Renderer.
    SDL_BlendMode prev = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(r, &prev);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(r, nullptr, g_verts, g_nv, g_idx, g_ni);
    SDL_SetRenderDrawBlendMode(r, prev);
}

Theme& Style() { return g_theme; }

} // namespace OkayUI
