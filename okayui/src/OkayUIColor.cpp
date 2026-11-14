// OkayUI colour picker: ColorPicker4 / ColorPicker3 (saturation / value square, hue bar, alpha bar, current and original preview,
// hex field, R G B A cells) and the hex text conversions. Built only on the public OkayUI calls (draw list, InvisibleButton, TextField,
// DragInt4), so it sits beside OkayUI.cpp without touching its internals. See okay/UI/OkayUI.hpp and docs/own_ui_toolkit.md section 4.33.
#include "okay/UI/OkayUI.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace OkayUI {

namespace {

float Clamp01(float v) { return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f; }   // NaN -> 0
Vec2 V(float x, float y) { return Vec2{x, y}; }
unsigned B8(float f) { return (unsigned)(Clamp01(f) * 255.0f + 0.5f); }
Color32 Rgb(float r, float g, float b, float a = 1.0f) { return RGBA(B8(r), B8(g), B8(b), B8(a)); }

// What one picker remembers between frames: the hue and saturation (a grey colour has none of its own), the colour as last written
// (to tell an outside change from our own), the colour when the picker appeared (the "original" half of the preview) and the hex text.
struct PickState {
    unsigned id = 0; bool used = false;
    float h = 0, s = 0, v = 0;
    float last[4] = {-1, -1, -1, -1};
    float orig[4] = {0, 0, 0, 1};
    long long frame = -10;
    char hex[16] = {0};
};
constexpr int kSlots = 16;
PickState g_ps[kSlots];
int g_psNext = 0;
PickState& StateFor(unsigned id) {
    for (PickState& p : g_ps) if (p.used && p.id == id) return p;
    PickState& p = g_ps[g_psNext];
    g_psNext = (g_psNext + 1) % kSlots;
    p = PickState();
    p.used = true; p.id = id;
    return p;
}

void Checker(const DrawList& dl, float x, float y, float w, float h) {
    if (w <= 0.0f || h <= 0.0f) return;
    const Color32 light = RGBA(204, 204, 204), dark = RGBA(153, 153, 153);
    dl.AddRectFilled(V(x, y), V(x + w, y + h), light);
    const float cell = std::max(4.0f, std::floor(std::min(w, h) / 4.0f));
    for (float cy = 0.0f; cy < h; cy += cell) {
        const int row = (int)(cy / cell);
        for (float cx = (row & 1) ? 0.0f : cell; cx < w; cx += 2.0f * cell)
            dl.AddRectFilled(V(x + cx, y + cy), V(x + std::min(cx + cell, w), y + std::min(cy + cell, h)), dark);
    }
}

} // namespace

void ColorPickerReset() { for (PickState& p : g_ps) p = PickState(); g_psNext = 0; }   // OkayUI::ResetState calls this

void ColorToHex(const float rgba[4], bool alpha, char* out, int cap) {
    if (!out || cap <= 0) return;
    out[0] = 0;
    if (!rgba) return;
    char tmp[16];
    if (alpha) std::snprintf(tmp, sizeof tmp, "#%02X%02X%02X%02X", B8(rgba[0]), B8(rgba[1]), B8(rgba[2]), B8(rgba[3]));
    else std::snprintf(tmp, sizeof tmp, "#%02X%02X%02X", B8(rgba[0]), B8(rgba[1]), B8(rgba[2]));
    std::snprintf(out, (size_t)cap, "%s", tmp);
}

bool ColorFromHex(const char* t, float rgba[4]) {
    if (!t || !rgba) return false;
    if (*t == '#') ++t;
    unsigned v[8]; int n = 0;
    for (; *t; ++t, ++n) {
        if (n >= 8) return false;
        const char ch = *t;
        if (ch >= '0' && ch <= '9') v[n] = (unsigned)(ch - '0');
        else if (ch >= 'a' && ch <= 'f') v[n] = (unsigned)(ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'F') v[n] = (unsigned)(ch - 'A' + 10);
        else return false;
    }
    if (n != 6 && n != 8) return false;
    for (int i = 0; i < n / 2; ++i) rgba[i] = (float)(v[2 * i] * 16u + v[2 * i + 1]) / 255.0f;
    return true;
}

bool ColorPicker4(const char* label, float rgba[4], int flags) {
    if (!rgba) return false;
    const bool hasAlpha = (flags & ColorEdit_NoAlpha) == 0;
    const bool alphaBar = hasAlpha && (flags & ColorEdit_NoAlphaBar) == 0;
    float c[4];
    for (int i = 0; i < 3; ++i) c[i] = Clamp01(rgba[i]);
    c[3] = hasAlpha ? Clamp01(rgba[3]) : 1.0f;

    PushID(label ? label : "");
    const float barW = 18.0f, gap = 8.0f, rowH = GetFrameHeight();
    const int bars = 1 + (alphaBar ? 1 : 0);
    float side = GetContentRegionAvail().x - (float)bars * (barW + gap);
    side = std::max(48.0f, std::min(side, rowH * 7.0f));

    InvisibleButton("sv", side, side);
    const Rect rSV = GetItemRect();
    const unsigned id = GetItemID();
    const bool svActive = IsItemActive();
    SameLine(gap);
    InvisibleButton("hue", barW, side);
    const Rect rHue = GetItemRect();
    const bool hueActive = IsItemActive();
    Rect rAlpha; bool alphaActive = false;
    if (alphaBar) { SameLine(gap); InvisibleButton("alpha", barW, side); rAlpha = GetItemRect(); alphaActive = IsItemActive(); }
    const float totalW = side + (float)bars * (barW + gap) - gap;

    PickState& st = StateFor(id);
    const long long now = GetFrameCount();
    if (now < st.frame || st.frame + 1 < now) { for (int i = 0; i < 4; ++i) st.orig[i] = c[i]; st.last[0] = -1.0f; st.hex[0] = 0; }   // the picker just appeared
    st.frame = now;
    // An outside change (a slider, the caller): take the hue and saturation from the colour, except where the colour has none.
    auto resync = [&]() {
        float h, s, v;
        ColorConvertRGBtoHSV(c[0], c[1], c[2], &h, &s, &v);
        if (v <= 1e-6f) { h = st.h; s = st.s; }
        else if (s <= 1e-6f) h = st.h;
        st.h = h; st.s = s; st.v = v;
    };
    bool outside = false;
    for (int i = 0; i < 4; ++i) if (std::fabs(st.last[i] - c[i]) > 1e-6f) outside = true;
    if (outside) resync();

    bool changed = false, colorDrag = false;
    const Vec2 m = GetMousePos();
    if (svActive && IsMouseDown()) {
        colorDrag = true;
        st.s = Clamp01((m.x - rSV.x) / std::max(1.0f, rSV.w));
        st.v = 1.0f - Clamp01((m.y - rSV.y) / std::max(1.0f, rSV.h));
        changed = true;
    }
    if (hueActive && IsMouseDown()) { colorDrag = true; st.h = std::min(0.9999f, Clamp01((m.y - rHue.y) / std::max(1.0f, rHue.h))); changed = true; }
    if (alphaActive && IsMouseDown()) { c[3] = 1.0f - Clamp01((m.y - rAlpha.y) / std::max(1.0f, rAlpha.h)); changed = true; }

    // ---- the picture ----
    const DrawList& dl = GetWindowDrawList();
    const Color32 white = RGBA(255, 255, 255), black = RGBA(0, 0, 0), border = GetColorU32(Col_Border);
    float hr, hg, hb;
    ColorConvertHSVtoRGB(st.h, 1.0f, 1.0f, &hr, &hg, &hb);
    const Color32 hueCol = Rgb(hr, hg, hb);
    const Vec2 sv0 = V(rSV.x, rSV.y), sv1 = V(rSV.x + rSV.w, rSV.y + rSV.h);
    dl.AddRectFilledMultiColor(sv0, sv1, white, hueCol, hueCol, white);                        // white to the hue, left to right
    dl.AddRectFilledMultiColor(sv0, sv1, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0), black, black);   // clear to black, top to bottom
    dl.AddRect(sv0, sv1, border);
    const Vec2 mk = V(rSV.x + st.s * rSV.w, rSV.y + (1.0f - st.v) * rSV.h);
    dl.AddCircle(mk, 6.0f, black, 0, 2.0f);
    dl.AddCircle(mk, 5.0f, white, 0, 1.0f);
    for (int i = 0; i < 6; ++i) {                                                              // the hue bar, six blends
        float r0, g0, b0, r1, g1, b1;
        ColorConvertHSVtoRGB((float)i / 6.0f, 1.0f, 1.0f, &r0, &g0, &b0);
        ColorConvertHSVtoRGB((float)(i + 1) / 6.0f, 1.0f, 1.0f, &r1, &g1, &b1);
        const float y0 = rHue.y + rHue.h * (float)i / 6.0f, y1 = rHue.y + rHue.h * (float)(i + 1) / 6.0f;
        dl.AddRectFilledMultiColor(V(rHue.x, y0), V(rHue.x + rHue.w, y1), Rgb(r0, g0, b0), Rgb(r0, g0, b0), Rgb(r1, g1, b1), Rgb(r1, g1, b1));
    }
    dl.AddRect(V(rHue.x, rHue.y), V(rHue.x + rHue.w, rHue.y + rHue.h), border);
    {
        const float hy = rHue.y + st.h * rHue.h;
        dl.AddRectFilled(V(rHue.x - 2.0f, hy - 2.0f), V(rHue.x + rHue.w + 2.0f, hy + 2.0f), black);
        dl.AddRectFilled(V(rHue.x - 1.0f, hy - 1.0f), V(rHue.x + rHue.w + 1.0f, hy + 1.0f), white);
    }
    float cr, cg, cb;
    ColorConvertHSVtoRGB(st.h, st.s, st.v, &cr, &cg, &cb);
    if (alphaBar) {
        Checker(dl, rAlpha.x, rAlpha.y, rAlpha.w, rAlpha.h);
        dl.AddRectFilledMultiColor(V(rAlpha.x, rAlpha.y), V(rAlpha.x + rAlpha.w, rAlpha.y + rAlpha.h), Rgb(cr, cg, cb, 1.0f), Rgb(cr, cg, cb, 1.0f), Rgb(cr, cg, cb, 0.0f), Rgb(cr, cg, cb, 0.0f));
        dl.AddRect(V(rAlpha.x, rAlpha.y), V(rAlpha.x + rAlpha.w, rAlpha.y + rAlpha.h), border);
        const float ay = rAlpha.y + (1.0f - c[3]) * rAlpha.h;
        dl.AddRectFilled(V(rAlpha.x - 2.0f, ay - 2.0f), V(rAlpha.x + rAlpha.w + 2.0f, ay + 2.0f), black);
        dl.AddRectFilled(V(rAlpha.x - 1.0f, ay - 1.0f), V(rAlpha.x + rAlpha.w + 1.0f, ay + 1.0f), white);
    }

    // ---- the colour as it is now, after the drag ----
    if (colorDrag) { c[0] = cr; c[1] = cg; c[2] = cb; }

    // ---- preview: current on the left, the colour the picker appeared with on the right (click it to go back) ----
    if (!(flags & ColorEdit_NoSidePreview)) {
        const bool clicked = InvisibleButton("preview", totalW, rowH);
        const Rect rp = GetItemRect();
        if (clicked && GetMousePos().x >= rp.x + rp.w * 0.5f) {
            for (int i = 0; i < 4; ++i) c[i] = st.orig[i];
            if (!hasAlpha) c[3] = 1.0f;
            resync();
            changed = true;
        }
        const float half = rp.w * 0.5f;
        const float a0 = hasAlpha ? c[3] : 1.0f, a1 = hasAlpha ? st.orig[3] : 1.0f;
        if (a0 < 1.0f) Checker(dl, rp.x, rp.y, half, rp.h);
        if (a1 < 1.0f) Checker(dl, rp.x + half, rp.y, rp.w - half, rp.h);
        dl.AddRectFilled(V(rp.x, rp.y), V(rp.x + half, rp.y + rp.h), Rgb(c[0], c[1], c[2], a0));
        dl.AddRectFilled(V(rp.x + half, rp.y), V(rp.x + rp.w, rp.y + rp.h), Rgb(st.orig[0], st.orig[1], st.orig[2], a1));
        dl.AddRect(V(rp.x, rp.y), V(rp.x + rp.w, rp.y + rp.h), border);
    }

    // ---- inputs: hex field, then R G B A cells ----
    if (!(flags & ColorEdit_NoInputs)) {
        Dummy(totalW, rowH);
        const Rect rh = GetItemRect();
        const int fid = (int)(id ^ 0x4e5u);
        if (GetKeyboardFocusId() != fid) ColorToHex(c, hasAlpha, st.hex, (int)sizeof st.hex);   // not being typed into: show the colour
        if (TextField(fid, rh.x, rh.y, rh.w, rh.h, st.hex, (int)sizeof st.hex)) {
            float t[4] = {c[0], c[1], c[2], c[3]};
            if (ColorFromHex(st.hex, t)) {
                for (int i = 0; i < (hasAlpha && std::strlen(st.hex) >= 8 ? 4 : 3); ++i) c[i] = t[i];
                resync();
                changed = true;
            }
        }
        int iv[4] = {(int)B8(c[0]), (int)B8(c[1]), (int)B8(c[2]), (int)B8(c[3])};
        PushID("cells");
        SetNextItemWidth(totalW);
        const bool cellsChanged = hasAlpha ? DragInt4("", iv, 0.5f, 0, 255) : DragInt3("", iv, 0.5f, 0, 255);
        PopID();
        if (cellsChanged) {
            for (int i = 0; i < (hasAlpha ? 4 : 3); ++i) c[i] = (float)std::max(0, std::min(255, iv[i])) / 255.0f;
            resync();
            changed = true;
        }
    }
    if (!(flags & ColorEdit_NoLabel) && label && label[0] && !(label[0] == '#' && label[1] == '#')) TextUnformatted(label);
    PopID();

    if (changed) {
        for (int i = 0; i < 3; ++i) rgba[i] = c[i];
        if (hasAlpha) rgba[3] = c[3];
        for (int i = 0; i < 4; ++i) st.last[i] = c[i];
    } else {
        for (int i = 0; i < 4; ++i) st.last[i] = c[i];
    }
    return changed;
}

bool ColorPicker3(const char* label, float rgb[3], int flags) {
    if (!rgb) return false;
    float c[4] = {rgb[0], rgb[1], rgb[2], 1.0f};
    const bool changed = ColorPicker4(label, c, flags | ColorEdit_NoAlpha);
    if (changed) { rgb[0] = c[0]; rgb[1] = c[1]; rgb[2] = c[2]; }
    return changed;
}

} // namespace OkayUI
