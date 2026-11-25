#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <SDL.h>
#include "OkayUIInternal.hpp"
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUICompat.hpp"
#include "okay/UI/OkayUIInternals.hpp"
#include "okay/Platform/Platform.hpp"
namespace OkayUI {
using namespace ui_core;


// ---- Launcher-style building blocks ---------------------------------------------
namespace ui_core {
float g_textScaleStack[8];
int   g_textScaleTop = 0;
struct CardSave { float x, y, w; float* height; float ox, contentW; };
CardSave g_cards[8];
int      g_ncards = 0;
// A pleasant, stable colour per name (avatar backgrounds).
void nameColor(const char* name, unsigned char out[4]) {
    static const unsigned char pal[8][3] = {{66, 133, 244}, {67, 181, 129}, {235, 137, 52}, {171, 92, 230},
                                            {230, 82, 120}, {38, 166, 191}, {214, 170, 44}, {108, 120, 230}};
    unsigned h = 2166136261u;   // dup-allow: moved verbatim from OkayUI.cpp (O1 split), the avatar colour hash
    if (name) for (const char* p = name; *p; ++p) h = (h ^ (unsigned char)*p) * 16777619u;   // dup-allow: moved verbatim from OkayUI.cpp (O1 split)
    const unsigned char* c = pal[h % 8];
    out[0] = c[0]; out[1] = c[1]; out[2] = c[2]; out[3] = 255;
}
// The first character of `name` (upper-cased ASCII), as a C string.
void initialOf(const char* name, char out[8]) {
    out[0] = '?'; out[1] = 0;
    if (!name) return;
    while (*name == ' ' || *name == '@') ++name;
    if (!*name) return;
    const unsigned char c = (unsigned char)*name;
    if (c < 0x80) { out[0] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c); out[1] = 0; return; }
    int n = 1; while (n < 4 && (((unsigned char)name[n]) & 0xC0) == 0x80) ++n;   // a whole UTF-8 character
    for (int i = 0; i < n; ++i) out[i] = name[i];
    out[n] = 0;
}
void drawAvatar(float x, float y, float d, const char* name) {
    unsigned char c[4]; nameColor(name, c);
    fillCircle(x + d * 0.5f, y + d * 0.5f, d * 0.5f, c, 28);
    char ini[8]; initialOf(name, ini);
    const float prev = g_theme.textScale;
    g_theme.textScale = d * 0.5f / 8.0f;
    const unsigned char white[4] = {255, 255, 255, 255};
    drawText(x + (d - labelW(ini)) * 0.5f, y + (d - textH()) * 0.5f, ini, g_theme.textScale, white);
    g_theme.textScale = prev;
}
} // namespace

void PushTextScale(float scale) {
    if (g_textScaleTop < 8) g_textScaleStack[g_textScaleTop++] = g_theme.textScale;
    g_theme.textScale = g_theme.textScale * (scale > 0.1f ? scale : 0.1f);
}
void PopTextScale() { if (g_textScaleTop > 0) g_theme.textScale = g_textScaleStack[--g_textScaleTop]; }

void Heading(const char* text) {
    if (!g_lay.active || !text) return;
    PushTextScale(1.625f);   // 16px -> 26px (a baked size: crisp)
    const float tw = labelW(text), th = textH();
    float x, y; place(tw, th + 12.0f, x, y);
    drawText(x, y, text, g_theme.textScale, g_theme.text);
    rrect(x, y + th + 5.0f, tw, 3.0f, 1.5f, g_theme.accent);
    PopTextScale();
}

void Caption(const char* text, unsigned char r, unsigned char g, unsigned char b) {
    if (!g_lay.active || !text) return;
    PushTextScale(0.8125f);  // 13px
    const float tw = labelW(text), th = textH();
    float x, y; place(tw, th + 2.0f, x, y);
    const unsigned char c[4] = {r, g, b, 255};
    drawText(x, y + 1.0f, text, g_theme.textScale, c);
    PopTextScale();
}

void TextWrapped(const char* text) {
    if (!g_lay.active || !text) return;
    wrapLines(text, availW(), g_theme.text);
}

bool PrimaryButton(const char* label) {
    if (!g_lay.active || !label) return false;
    const int id = hashLabel(label);
    const float w = visW(label) + 40.0f, h = rowH();
    float x, y; place(w, h, x, y);
    const bool inside = hit(x, y, w, h);
    g_lastId = id; if (inside) g_hot = id;
    bool clicked = false;
    if (g_active == id) { if (g_released) { if (inside) clicked = true; g_active = 0; } }
    else if (inside && g_pressed) g_active = id;
    unsigned char c[4] = {g_theme.accent[0], g_theme.accent[1], g_theme.accent[2], 255};
    if (g_active == id && inside) { c[0] = (unsigned char)(c[0] * 0.85f); c[1] = (unsigned char)(c[1] * 0.85f); c[2] = (unsigned char)(c[2] * 0.85f); }
    else if (inside) { c[0] = (unsigned char)(c[0] + (255 - c[0]) * 0.12f); c[1] = (unsigned char)(c[1] + (255 - c[1]) * 0.12f); c[2] = (unsigned char)(c[2] + (255 - c[2]) * 0.12f); }
    rrect(x, y, w, h, rnd(), c);
    const unsigned char white[4] = {255, 255, 255, 255};
    drawText(x + (w - visW(label)) * 0.5f, y + (h - textH()) * 0.5f, VisLabel(label).b, g_theme.textScale, white);
    return clicked;
}

void Badge(const char* text, unsigned char r, unsigned char g, unsigned char b) {
    if (!g_lay.active || !text) return;
    PushTextScale(0.8125f);
    const float tw = labelW(text), h = textH() + 8.0f, w = tw + 16.0f;
    g_theme.textScale = g_textScaleStack[g_textScaleTop - 1];   // the row height of normal text...
    const float row = rowH();
    g_theme.textScale *= 0.8125f;
    float x, y; place(w, row, x, y);
    y += (row - h) * 0.5f;                                      // ...with the pill centred on it
    unsigned char c[4] = {r, g, b, 255};
    if (!r && !g && !b) { c[0] = g_theme.accent[0]; c[1] = g_theme.accent[1]; c[2] = g_theme.accent[2]; }
    rrect(x, y, w, h, h * 0.5f, c);
    const unsigned char white[4] = {255, 255, 255, 255};
    drawText(x + 8.0f, y + 3.5f, text, g_theme.textScale, white);
    PopTextScale();
}

void Avatar(const char* name, float size) {
    if (!g_lay.active) return;
    const float d = size > 0.0f ? size : rowH();
    float x, y; place(d, d, x, y);
    drawAvatar(x, y, d, name);
}

void CardFillColor(unsigned char out[4]) {
    // The card is a step up from the panel: a little lighter on a dark theme (4% toward the text colour; Slate gives
    // {38, 41, 49}), near white on a light one.
    const unsigned char* pn = g_theme.panel;
    const bool lightPanel = pn[0] * 0.2126f + pn[1] * 0.7152f + pn[2] * 0.0722f > 140.0f;
    for (int k = 0; k < 3; ++k)
        out[k] = (unsigned char)(lightPanel ? pn[k] + (255 - pn[k]) * 0.85f : pn[k] + (g_theme.text[k] - pn[k]) * 0.04f + 0.5f);
    out[3] = 255;
}

void BeginCard(const char* id) {
    if (!g_lay.active || g_ncards >= 8) return;
    const int cid = hashLabel(id ? id : "card");
    float* height = floatState(cid ^ 0xca7d, 0.0f);
    const float x = cursorX(), w = availW();
    float px, py; place(w, 0.0f, px, py);   // reserve the line; content follows inside
    g_lay.cy = py;                           // (no spacing yet: the card grows to fit)
    const float pad = 14.0f, rad = g_look ? 10.0f : rnd();
    if (*height > 0.0f) {
        unsigned char raised[4];
        CardFillColor(raised);
        rrect(px, py, w, *height, rad, g_theme.border);
        rrect(px + 1.0f, py + 1.0f, w - 2.0f, *height - 2.0f, rad > 1.0f ? rad - 1.0f : 0.0f, g_look ? raised : g_theme.bg);
    }
    CardSave& c = g_cards[g_ncards++];
    c.x = px; c.y = py; c.w = w; c.height = height; c.ox = g_lay.ox; c.contentW = g_lay.contentW;
    (void)x;
    g_lay.ox = px + pad - g_lay.indent; g_lay.contentW = w - 2.0f * pad + g_lay.indent;
    g_lay.cy = py + pad; g_lay.cx = g_lay.ox;
    g_lay.prevX = g_lay.ox; g_lay.prevY = g_lay.cy; g_lay.prevW = 0.0f; g_lay.prevH = 0.0f;
}

void EndCard() {
    if (!g_lay.active || g_ncards <= 0) return;
    CardSave& c = g_cards[--g_ncards];
    const float pad = 14.0f;
    const float bottom = g_lay.cy - g_lay.spacingY + pad;   // last item's bottom + padding
    *c.height = bottom - c.y;
    g_lay.ox = c.ox; g_lay.contentW = c.contentW;
    g_lay.cx = leftEdge();
    g_lay.cy = bottom + g_lay.spacingY;
    g_lay.prevX = c.x; g_lay.prevY = c.y; g_lay.prevW = c.w; g_lay.prevH = *c.height;
    g_lay.pendingSameLine = false;
}

bool ListItem(const char* title, const char* subtitle, const char* right, bool selected) {
    if (!g_lay.active) return false;
    const int id = hashLabel(title ? title : "item");
    const bool two = subtitle && *subtitle;
    const float h = two ? rowH() + textH() * 0.85f : rowH() + 4.0f, w = fullW();
    float x, y; place(w, h, x, y);
    const bool inside = hit(x, y, w, h);
    g_lastId = id; if (inside) g_hot = id;
    bool clicked = false;
    if (g_active == id) { if (g_released) { if (inside) clicked = true; g_active = 0; } }
    else if (inside && g_pressed) g_active = id;
    unsigned char tmp[4]; const unsigned char white[4] = {255, 255, 255, 255};
    if (selected) rrect(x, y, w, h, 8.0f, withAlpha(g_theme.accent, 0.25f, tmp));
    else if (inside) rrect(x, y, w, h, 8.0f, withAlpha(white, 0.06f, tmp));
    const float d = h - 12.0f;
    drawAvatar(x + 6.0f, y + 6.0f, d, title);
    const float tx = x + d + 18.0f, s = g_theme.textScale;
    float rw = 0.0f;
    if (right && *right) { rw = labelW(right); drawText(x + w - rw - 10.0f, y + (h - textH()) * 0.5f, right, s, pick(Col_TextDisabled, g_theme.textDim)); }
    if (two) {
        drawText(tx, y + 6.0f, title ? title : "", s, g_theme.text);
        PushTextScale(0.8125f);
        drawText(tx, y + h - 6.0f - textH() - 1.0f, subtitle, g_theme.textScale, pick(Col_TextDisabled, g_theme.textDim));
        PopTextScale();
    } else drawText(tx, y + (h - textH()) * 0.5f, title ? title : "", s, g_theme.text);
    return clicked;
}

void KeyValue(const char* label, const char* value) {
    if (!g_lay.active) return;
    const float h = rowH(), w = fullW();
    float x, y; place(w, h, x, y);
    const float ty = y + (h - textH()) * 0.5f;
    if (label) drawText(x, ty, VisLabel(label).b, g_theme.textScale, pick(Col_TextDisabled, g_theme.textDim));
    if (value) drawText(x + w - labelW(value), ty, value, g_theme.textScale, g_theme.text);
}

void Spinner(float size) {
    if (!g_lay.active) return;
    const float d = size > 0.0f ? size : rowH();
    float x, y; place(d, d, x, y);
    const float cx = x + d * 0.5f, cy = y + d * 0.5f, r = d * 0.38f;
    const float a0 = (float)(okay::platform::TicksMs32() % 1000u) / 1000.0f * 6.2831853f;
    unsigned char tmp[4];
    for (int i = 0; i < 20; ++i) {   // a 270-degree arc fading toward its tail
        const float t0 = a0 + i * 0.2356f, t1 = t0 + 0.2356f;
        line(cx + r * SDL_cosf(t0), cy + r * SDL_sinf(t0), cx + r * SDL_cosf(t1), cy + r * SDL_sinf(t1),
             d * 0.1f, withAlpha(g_theme.accent, 0.15f + 0.85f * (float)i / 19.0f, tmp));
    }
}


} // namespace OkayUI
