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

namespace OkayUI {
using namespace ui_core;


bool Button(int id, float x, float y, float w, float h, const char* label) {
    const bool inside = hit(x, y, w, h);
    g_lastId = id; if (inside) g_hot = id;

    // Hot/active (Casey-style) interaction: arm on press-inside, fire on
    // release-inside. A release outside cancels without firing.
    bool clicked = false;
    if (g_active == id) {
        if (g_released) { if (inside) clicked = true; g_active = 0; }
    } else if (inside && g_pressed) {
        g_active = id;
    }

    const unsigned char* bg = g_theme.bg;
    if (g_active == id && inside) bg = g_look ? g_theme.accent : g_theme.bgDown;
    else if (inside)             bg = g_theme.bgHover;

    // Rounded border frame, then the inset fill.
    const float rd = rnd();
    rrect(x, y, w, h, rd, g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) rrect(x + b, y + b, w - 2.0f * b, h - 2.0f * b, rd > b ? rd - b : 0.0f, bg);

    // Centered label (what follows "##" is only part of the id).
    const VisLabel vis(label);
    if (vis.b[0]) {
        const float s  = g_theme.textScale;
        const float tw = visW(label);
        const float th = fH() * s;
        if (tw > w + 16.0f) {   // a label much wider than its button is shortened with "..." (a clip rectangle does not reach the overlay layer that popups draw in)
            char cut[160]; const VisLabel vl(label); std::size_t n = 0;
            while (vl.b[n] && n < sizeof cut - 4) {
                std::size_t m = n + 1; while (vl.b[m] && ((unsigned char)vl.b[m] & 0xC0) == 0x80) ++m;   // whole UTF-8 characters
                std::memcpy(cut, vl.b, m); cut[m] = '.'; cut[m + 1] = '.'; cut[m + 2] = '.'; cut[m + 3] = 0;
                if (labelW(cut) > w - 8.0f) break;
                n = m;
            }
            std::memcpy(cut, vl.b, n); cut[n] = '.'; cut[n + 1] = '.'; cut[n + 2] = '.'; cut[n + 3] = 0;
            drawText(x + 4.0f, y + (h - th) * 0.5f, cut, s, g_theme.text);
        } else {
            // StyleVar_ButtonTextAlign is honoured with StyleVar_FramedWidgets (Dear ImGui's look); OkayUI's own buttons centre their label
            const float tx = (g_btnAlignX == 0.5f || !g_framedWidgets) ? x + (w - tw) * 0.5f : x + 12.0f + std::max(0.0f, w - 24.0f - tw) * g_btnAlignX;
            drawText(tx, y + (h - th) * 0.5f, vis.b, s, g_theme.text);
        }
    }
    return clicked;
}

void Label(float x, float y, const char* text) {
    if (text && *text) drawText(x, y, text, g_theme.textScale, g_theme.text);
}

void ui_core::panelColored(float x, float y, float w, float h, const unsigned char* fill) {
    const float rd = rnd();
    rrect(x, y, w, h, rd, g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) rrect(x + b, y + b, w - 2.0f * b, h - 2.0f * b, rd > b ? rd - b : 0.0f, fill);
}
void Panel(float x, float y, float w, float h) { panelColored(x, y, w, h, pick(Col_WindowBg, g_theme.panel)); }

bool Checkbox(int id, float x, float y, float size, const char* label, bool* value) {
    if (!value) return false;
    // The label is part of the target (clicking the text toggles, like every desktop toolkit).
    const bool inside = hit(x, y, size + (label && *label ? 8.0f + visW(label) : 0.0f), size);
    g_lastId = id; if (inside) g_hot = id;

    bool changed = false;
    if (g_active == id) {
        if (g_released) { if (inside) { *value = !*value; changed = true; } g_active = 0; }
    } else if (inside && g_pressed) {
        g_active = id;
    }

    if (g_framedWidgets) {   // Dear ImGui's box: a rounded frame in the field colours (the accent at 45% once ticked, 55% under the
                             // pointer) and the tick in Col_CheckMark
        unsigned char tint[4];
        const unsigned char* fb = *value ? withAlpha(g_theme.accent, inside ? 0.55f : 0.45f, tint)
                                         : (g_active == id && inside) ? pick(Col_FrameBgActive, g_theme.bgDown)
                                         : inside ? pick(Col_FrameBgHovered, g_theme.bgHover) : pick(Col_FrameBg, g_theme.field);
        rrect(x, y, size, size, rnd(), fb);
        if (*value) framedTick(x, y, size, pick(Col_CheckMark, g_theme.accent));
        if (label && *label) drawText(x + size + 8.0f, y + (size - textH()) * 0.5f, label, g_theme.textScale, g_theme.text);
        return changed;
    }
    const unsigned char* bg = g_theme.bg;
    if (g_active == id && inside) bg = g_theme.bgDown;
    else if (inside)             bg = g_theme.bgHover;

    // Rounded box frame + fill.
    const float rd = rnd() * 0.6f;
    rrect(x, y, size, size, rd, g_theme.border);
    const float b = g_theme.borderPx;
    if (size > 2.0f * b) rrect(x + b, y + b, size - 2.0f * b, size - 2.0f * b, rd > b ? rd - b : 0.0f, bg);
    // Check mark: a centered accent square when ticked (launcher look: an accent box with a tick).
    if (*value && g_look) {
        rrect(x, y, size, size, rd, pick(Col_CheckMark, g_theme.accent));
        const unsigned char white[4] = {255, 255, 255, 255};
        tickMark(x, y, size, white);
    } else if (*value) {
        const float pad = size * 0.28f;
        rrect(x + pad, y + pad, size - 2.0f * pad, size - 2.0f * pad, rd * 0.5f, pick(Col_CheckMark, g_theme.accent));
    }
    // Label to the right, vertically centered against the box.
    if (label && *label) {
        const float s  = g_theme.textScale;
        const float th = fH() * s;
        drawText(x + size + 8.0f, y + (size - th) * 0.5f, VisLabel(label).b, s, g_theme.text);
    }
    return changed;
}

bool ToggleSwitch(int id, float x, float y, float w, float h, const char* label, bool* value) {
    if (!value || w <= 0.0f || h <= 0.0f) return false;
    const bool inside = hit(x, y, w + (label && *label ? 8.0f + visW(label) : 0.0f), h);
    g_lastId = id; if (inside) g_hot = id;

    bool changed = false;
    if (g_active == id) {
        if (g_released) { if (inside) { *value = !*value; changed = true; } g_active = 0; }
    } else if (inside && g_pressed) {
        g_active = id;
    }

    // Pill track: dim when off, accent when on; knob slides between the two ends.
    const float rd = h * 0.5f;
    const unsigned char* track = *value ? g_theme.accent : g_theme.bg;
    rrect(x, y, w, h, rd, g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) rrect(x + b, y + b, w - 2.0f * b, h - 2.0f * b, rd > b ? rd - b : 0.0f, track);
    const float pad = h * 0.14f;
    const float kd  = h - 2.0f * pad;                          // knob diameter
    const float kx  = *value ? (x + w - pad - kd) : (x + pad); // slid to the on/off end
    const unsigned char* knob = (inside || g_active == id) ? g_theme.text : g_theme.bgHover;
    rrect(kx, y + pad, kd, kd, kd * 0.5f, knob);
    // Label to the right of the switch, vertically centered.
    if (label && *label) {
        const float s = g_theme.textScale, th = fH() * s;
        drawText(x + w + 8.0f, y + (h - th) * 0.5f, VisLabel(label).b, s, g_theme.text);
    }
    return changed;
}

bool Slider(int id, float x, float y, float w, float h, float* value, float minV, float maxV) {
    if (!value || w <= 0.0f || maxV <= minV) return false;
    const bool inside = hit(x, y, w, h);
    g_lastId = id; if (inside) g_hot = id;

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

    // Rounded groove, then the filled portion, then a circular handle.
    const float t = clamp01((*value - minV) / (maxV - minV));
    if (g_look) {   // thin field-coloured groove, accent fill, white knob
        const float gy2 = y + h * 0.5f - 2.5f;
        rrect(x, gy2, w, 5.0f, 2.5f, g_theme.field);
        if (w * t > 0.0f) rrect(x, gy2, w * t, 5.0f, 2.5f, g_theme.accent);
        const float kr = h * 0.32f;
        float kcx = x + w * t;
        if (kcx < x + kr) kcx = x + kr;
        if (kcx > x + w - kr) kcx = x + w - kr;
        const unsigned char ring[4] = {0, 0, 0, 60};
        fillCircle(kcx, y + h * 0.5f + 1.0f, kr + 1.0f, ring);
        const unsigned char knob[4] = {245, 247, 252, 255};
        fillCircle(kcx, y + h * 0.5f, kr, (g_active == id || inside) ? pick(Col_SliderGrabActive, g_theme.text) : pick(Col_SliderGrab, knob));
        return changed;
    }
    const float gy = y + h * 0.5f - 3.0f;     // 6px groove, vertically centered
    rrect(x, gy, w, 6.0f, 3.0f, g_theme.track);
    if (w * t > 0.0f) rrect(x, gy, w * t, 6.0f, 3.0f, g_theme.accent);
    const float hr = h * 0.42f;                // handle radius
    float hcx = x + w * t;
    if (hcx < x + hr) hcx = x + hr;
    if (hcx > x + w - hr) hcx = x + w - hr;
    const unsigned char* hc = (g_active == id || inside) ? pick(Col_SliderGrabActive, g_theme.text) : pick(Col_SliderGrab, g_theme.accent);
    fillCircle(hcx, y + h * 0.5f, hr, g_theme.border);
    fillCircle(hcx, y + h * 0.5f, hr - 1.5f, hc);
    return changed;
}

void ProgressBar(float x, float y, float w, float h, float t) {
    t = clamp01(t);
    if (g_look) {   // a pill: field track, accent fill
        const float bh = h > 10.0f ? 10.0f : h, by = y + (h - bh) * 0.5f;
        rrect(x, by, w, bh, bh * 0.5f, g_theme.field);
        if (w * t >= bh * 0.5f) rrect(x, by, w * t, bh, bh * 0.5f, g_theme.accent);
        return;
    }
    const float rd = rnd();
    rrect(x, y, w, h, rd, g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) {
        const float ir = rd > b ? rd - b : 0.0f;
        rrect(x + b, y + b, w - 2.0f * b, h - 2.0f * b, ir, g_theme.track);
        const float iw = (w - 2.0f * b) * t;
        if (iw > 0.0f) rrect(x + b, y + b, iw, h - 2.0f * b, ir, g_theme.accent);
    }
}

bool RadioButton(int id, float x, float y, float size, const char* label, int* value, int option) {
    if (!value) return false;
    const bool inside = hit(x, y, size + (label && *label ? 8.0f + visW(label) : 0.0f), size);
    g_lastId = id; if (inside) g_hot = id;

    bool changed = false;
    if (g_active == id) {
        if (g_released) { if (inside && *value != option) { *value = option; changed = true; } g_active = 0; }
    } else if (inside && g_pressed) {
        g_active = id;
    }

    const unsigned char* bg = g_theme.bg;
    if (g_active == id && inside) bg = g_theme.bgDown;
    else if (inside)             bg = g_theme.bgHover;

    // Circular radio (distinct from the square checkbox).
    const float cx = x + size * 0.5f, cy = y + size * 0.5f, rr = size * 0.5f;
    fillCircle(cx, cy, rr, g_theme.border);
    fillCircle(cx, cy, rr - g_theme.borderPx, bg);
    // Selected indicator: a smaller centered accent dot.
    if (*value == option) fillCircle(cx, cy, size * 0.22f, pick(Col_CheckMark, g_theme.accent));
    if (label && *label) {
        const float s  = g_theme.textScale;
        const float th = fH() * s;
        drawText(x + size + 8.0f, y + (size - th) * 0.5f, VisLabel(label).b, s, g_theme.text);
    }
    return changed;
}

bool Tab(int id, float x, float y, float w, float h, const char* label, int* current, int index) {
    if (!current) return false;
    const bool inside = hit(x, y, w, h);
    g_lastId = id; if (inside) g_hot = id;

    if (g_active == id) {
        if (g_released) { if (inside) *current = index; g_active = 0; }
    } else if (inside && g_pressed) {
        g_active = id;
    }

    const bool selected = (*current == index);
    if (g_look) {   // launcher pill tabs: accent when selected, faint white otherwise
        unsigned char tmp[4];
        const unsigned char white[4] = {255, 255, 255, 255};
        const unsigned char* fill = selected ? g_theme.accent : withAlpha(white, inside ? 0.10f : 0.05f, tmp);
        rrect(x, y, w, h, h * 0.5f, fill);
        if (label && *label) {
            const float s = g_theme.textScale, tw = visW(label), th = fH() * s;
            drawText(x + (w - tw) * 0.5f, y + (h - th) * 0.5f, VisLabel(label).b, s, g_theme.text);
        }
        return *current == index;
    }
    const unsigned char* bg = selected ? pick(Col_TabActive, g_theme.bg) : pick(Col_Tab, g_theme.track);
    if (!selected && inside) bg = pick(Col_TabHovered, g_theme.bgHover);

    quad(x, y, w, h, g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) quad(x + b, y + b, w - 2.0f * b, h - 2.0f * b, bg);
    if (selected) quad(x, y, w, 3.0f, g_theme.accent);   // accent strip on the active tab
    if (label && *label) {
        const float s  = g_theme.textScale;
        const float tw = visW(label);
        const float th = fH() * s;
        drawText(x + (w - tw) * 0.5f, y + (h - th) * 0.5f, VisLabel(label).b, s, g_theme.text);
    }
    return *current == index;
}


} // namespace OkayUI
