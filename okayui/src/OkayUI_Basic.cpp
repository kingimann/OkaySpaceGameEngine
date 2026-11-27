#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
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

    // The design's control: its fill eases to the hover colour and on to the pressed one (motion), an outline when the design
    // draws outlines on controls.
    unsigned char st[4];
    const unsigned char* bg = stateColor(id, inside, g_active == id && inside, g_theme.bg, g_theme.bgHover, g_theme.bgDown, st);
    const float rd = rnd();
    controlBox(x, y, w, h, rd, bg);

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
        // the design's box at the frame's size: a light ring round the field colour; ticked, an accent fill with the tick in the
        // on-accent colour (not a heavy filled square)
        unsigned char ring[4];
        const unsigned char* fb = (g_active == id && inside) ? pick(Col_FrameBgActive, g_theme.bgDown)
                                : inside ? pick(Col_FrameBgHovered, g_theme.bgHover) : pick(Col_FrameBg, g_theme.field);
        const float bs = std::min(size, std::max(16.0f, textH() + 4.0f)), bx = x + (size - bs) * 0.5f, by = y + (size - bs) * 0.5f;
        const float rd = std::min(g_theme.radiusSmall, bs * 0.3f);
        rrect(bx, by, bs, bs, rd, *value ? pick(Col_CheckMark, g_theme.accent) : fb);
        if (!*value) ringRect(bx, by, bs, bs, rd, 1.5f, withAlpha(g_theme.ink, inside ? 0.55f : 0.30f, ring));
        else tickMark(bx, by, bs, g_theme.onAccent);
        if (label && *label) drawText(x + size + 8.0f, y + (size - textH()) * 0.5f, label, g_theme.textScale, g_theme.text);
        return changed;
    }
    // An empty box is the field colour inside a ring of the ink (brighter under the pointer); a ticked one fills with the accent
    // (fading in with motion) and carries a tick drawn as two strokes in the on-accent colour.
    const float rd = std::min(g_theme.radiusSmall, size * 0.4f);
    unsigned char st[4], ring[4];
    const unsigned char* fill = stateColor(id, inside, g_active == id && inside, g_theme.field, g_theme.bgHover, g_theme.bgDown, st);
    rrect(x, y, size, size, rd, fill);
    const float on = anim(id, 2, *value);
    if (on < 1.0f) ringRect(x, y, size, size, rd, std::max(1.5f, g_theme.borderPx), withAlpha(g_theme.ink, inside ? 0.55f : 0.32f, ring));
    if (on > 0.0f) {
        unsigned char acc[4];
        rrect(x, y, size, size, rd, withAlpha(pick(Col_CheckMark, g_theme.accent), on, acc));
        unsigned char tick[4];
        tickMark(x, y, size, withAlpha(g_theme.onAccent, on, tick));
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

    // Pill track: the control colour when off, the accent when on (blended with motion); the knob slides between the ends
    // and sits on a small shadow. The knob is the on-accent colour when on, the text colour when off.
    const float rd = h * 0.5f;
    const float on = anim(id, 2, *value);
    unsigned char trk[4];
    const unsigned char* off = inside ? g_theme.bgHover : g_theme.bg;
    mixColor(off, g_theme.accent, on, trk);
    controlBox(x, y, w, h, rd, trk);
    const float pad = h * 0.14f;
    const float kd  = h - 2.0f * pad;                          // knob diameter
    const float kx  = x + pad + (w - 2.0f * pad - kd) * on;    // slid toward the on end
    const unsigned char sh[4] = {0, 0, 0, (unsigned char)(g_theme.lightScheme ? 40 : 70)};
    fillCircle(kx + kd * 0.5f, y + pad + kd * 0.5f + 1.0f, kd * 0.5f + 0.5f, sh);
    unsigned char kc[4];
    mixColor(g_theme.text, g_theme.onAccent, on, kc);
    if (g_theme.lightScheme && on < 0.5f) { kc[0] = kc[1] = kc[2] = 255; }   // a white knob on a light track
    fillCircle(kx + kd * 0.5f, y + pad + kd * 0.5f, kd * 0.5f, kc);
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

    // A thin groove in the sunken colour, the filled part in the accent, and a round thumb with a soft shadow that grows a
    // little under the pointer (motion) and shows an accent halo while dragged.
    const float t = clamp01((*value - minV) / (maxV - minV));
    const float gh = std::max(4.0f, std::floor(h * 0.18f)), gy = y + (h - gh) * 0.5f;
    rrect(x, gy, w, gh, gh * 0.5f, g_theme.track);
    if (g_theme.controlBorders) ringRect(x, gy, w, gh, gh * 0.5f, 1.0f, g_theme.border);   // a groove that must show on any surface
    if (w * t > 0.0f) rrect(x, gy, w * t, gh, gh * 0.5f, g_theme.accent);
    const float over = anim(id, 0, inside || g_active == id);
    const float kr = h * 0.30f + over * h * 0.04f;
    float kcx = x + w * t;
    if (kcx < x + kr) kcx = x + kr;
    if (kcx > x + w - kr) kcx = x + w - kr;
    const float kcy = y + h * 0.5f;
    if (g_active == id) { unsigned char halo[4]; fillCircle(kcx, kcy, kr + 5.0f, withAlpha(g_theme.accent, 0.28f, halo), 28); }
    const unsigned char sh[4] = {0, 0, 0, (unsigned char)(g_theme.lightScheme ? 45 : 80)};
    fillCircle(kcx, kcy + 1.0f, kr + 1.0f, sh);
    const unsigned char knobW[4] = {250, 250, 252, 255};
    const unsigned char* kc = (g_active == id || inside) ? pick(Col_SliderGrabActive, knobW) : pick(Col_SliderGrab, knobW);
    fillCircle(kcx, kcy, kr, kc, 28);
    if (g_theme.controlBorders) ringRect(kcx - kr, kcy - kr, 2.0f * kr, 2.0f * kr, kr, 1.0f, g_theme.border);
    return changed;
}

void ProgressBar(float x, float y, float w, float h, float t) {
    t = clamp01(t);
    // A pill: the sunken groove and the accent fill, at most 10 pixels tall, centred in the box.
    const float bh = h > 10.0f ? 10.0f : h, by = y + (h - bh) * 0.5f;
    rrect(x, by, w, bh, bh * 0.5f, g_theme.track);
    if (g_theme.controlBorders) ringRect(x, by, w, bh, bh * 0.5f, 1.0f, g_theme.border);
    if (w * t > 0.0f) rrect(x, by, std::max(w * t, std::min(bh, w)), bh, bh * 0.5f, g_theme.accent);
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

    // A round box: an ink ring around the field colour; the chosen one is an accent disc with a dot in the on-accent colour.
    unsigned char st[4], ring[4];
    const unsigned char* bg = stateColor(id, inside, g_active == id && inside, g_theme.field, g_theme.bgHover, g_theme.bgDown, st);
    const float cx = x + size * 0.5f, cy = y + size * 0.5f, rr = size * 0.5f;
    const bool chosen = *value == option;
    const float on = anim(id, 2, chosen);
    fillCircle(cx, cy, rr, withAlpha(g_theme.ink, inside ? 0.55f : 0.32f, ring), 28);
    fillCircle(cx, cy, rr - std::max(1.5f, g_theme.borderPx), bg, 28);
    if (on > 0.0f) {   // the chosen one: an accent ring and an accent dot with a gap of the field colour between
        unsigned char acc[4];
        const unsigned char* mark = withAlpha(pick(Col_CheckMark, g_theme.accent), on, acc);
        fillCircle(cx, cy, rr, mark, 28);
        fillCircle(cx, cy, rr * 0.66f, bg, 24);
        fillCircle(cx, cy, rr * (0.20f + 0.18f * on), mark, 20);
    }
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
    // Three shapes, chosen by the design (Theme::tabShape): an underline (text only, the chosen tab's label in the text colour
    // over a 2-pixel accent bar, the others dimmer), a pill (the chosen tab filled with the accent) or a segment (the chosen
    // tab a raised chip in a sunken track; the track is drawn by the bar). Hover eases in with motion.
    const float over = anim(id, 0, inside && !selected);
    const float s = g_theme.textScale, tw = visW(label), th = fH() * s;
    unsigned char tmp[4], tc[4];
    const unsigned char* txt = g_theme.text;
    // A tab colour a program set (PushStyleColor / SetStyleColor: Col_Tab, Col_TabHovered, Col_TabActive) fills the whole tab, as asked.
    {
        const int set = selected ? (colorIsSet(Col_TabActive) ? Col_TabActive : 0) : (inside && colorIsSet(Col_TabHovered)) ? Col_TabHovered : (colorIsSet(Col_Tab) ? Col_Tab : 0);
        if (set) rrect(x, y, w, h, g_theme.tabShape == 1 ? h * 0.5f : g_theme.radiusSmall, g_theme.colors[set]);
    }
    switch (g_theme.tabShape) {
        case 1: {   // pill
            if (selected) rrect(x, y, w, h, h * 0.5f, pick(Col_TabActive, g_theme.accent));
            else if (over > 0.0f) rrect(x, y, w, h, h * 0.5f, pick(Col_TabHovered, withAlpha(g_theme.ink, 0.10f * over, tmp)));
            txt = selected ? g_theme.onAccent : mixColor(g_theme.textDim, g_theme.text, over, tc);
            break;
        }
        case 2: {   // segment
            if (selected) {
                elevation(x + 2.0f, y + 2.0f, w - 4.0f, h - 4.0f, g_theme.radiusSmall, g_theme.shadowDepth > 0 ? 1 : 0);
                controlBox(x + 2.0f, y + 2.0f, w - 4.0f, h - 4.0f, g_theme.radiusSmall, pick(Col_TabActive, g_theme.panel));
            } else if (over > 0.0f) rrect(x + 2.0f, y + 2.0f, w - 4.0f, h - 4.0f, g_theme.radiusSmall, pick(Col_TabHovered, withAlpha(g_theme.ink, 0.07f * over, tmp)));
            txt = selected ? g_theme.text : mixColor(g_theme.textDim, g_theme.text, over, tc);
            break;
        }
        default: {  // underline
            if (over > 0.0f) rrect(x + 2.0f, y + 3.0f, w - 4.0f, h - 6.0f, g_theme.radiusSmall, pick(Col_TabHovered, withAlpha(g_theme.ink, 0.06f * over, tmp)));
            if (selected) rrect(x + 6.0f, y + h - 2.0f, w - 12.0f, 2.0f, 1.0f, pick(Col_TabActive, g_theme.accent));
            txt = selected ? g_theme.text : mixColor(g_theme.textDim, g_theme.text, over, tc);
            break;
        }
    }
    if (label && *label) drawText(x + (w - tw) * 0.5f, y + (h - th) * 0.5f, VisLabel(label).b, s, txt);
    return *current == index;
}

} // namespace OkayUI
