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


bool CollapsingHeader(const char* label, bool defaultOpen);
bool CollapsingHeader(const char* label) { return CollapsingHeader(label, false); }
bool CollapsingHeader(const char* label, bool defaultOpen) {
    if (!g_lay.active) return false;
    const int id = hashLabel(label);
    bool* open = openState(id, defaultOpen);
    if (g_nextOpen >= 0) { *open = g_nextOpen != 0; g_nextOpen = -1; }
    const float h = rowH(), w = fullW();
    float x, y; place(w, h, x, y);
    const bool inside = hit(x, y, w, h);
    g_lastId = id; if (inside) g_hot = id;
    if (g_active == id) { if (g_released) { if (inside) { *open = !*open; g_toggledId = id; } g_active = 0; } }
    else if (inside && g_pressed) g_active = id;
    const unsigned char* bg = (g_active == id && inside) ? pick(Col_HeaderActive, g_theme.bgDown) : (inside ? pick(Col_HeaderHovered, g_theme.bgHover) : pick(Col_Header, g_theme.track));
    const float s = g_theme.textScale;
    if (g_look) {
        rrect(x, y, w, h, rnd(), inside ? pick(Col_HeaderHovered, g_theme.bgHover) : pick(Col_Header, g_theme.bg));
        chevron(x + 14.0f, y + h * 0.5f, 9.0f, *open, pick(Col_TextDisabled, g_theme.textDim));
        drawText(x + 28.0f, y + (h - textH()) * 0.5f, VisLabel(label).b, s, g_theme.text);
        return *open;
    }
    quad(x, y, w, h, bg);
    drawText(x + 8.0f, y + (h - textH()) * 0.5f, *open ? "-" : "+", s, g_theme.text);   // expander glyph
    drawText(x + 8.0f + fCharW() * s * 2.0f, y + (h - textH()) * 0.5f, VisLabel(label).b, s, g_theme.text);
    return *open;
}

bool TreeNodeEx(const char* label, unsigned flags) {
    if (!g_lay.active) return false;
    const int id = hashLabel(label);
    const bool leaf = (flags & TreeNode_Leaf) != 0;
    bool* open = openState(id, (flags & TreeNode_DefaultOpen) != 0);
    if (g_nextOpen >= 0) { *open = g_nextOpen != 0; g_nextOpen = -1; }
    const float h = rowH(), s = g_theme.textScale;
    const float ax = leftEdge();                                  // where the arrow sits: at the indent
    float x, y; place(fullW(), h, x, y);
    float w = fullW();
    if (flags & TreeNode_SpanFullWidth) { x = g_lay.ox; w = g_lay.contentW; g_lay.prevX = x; g_lay.prevW = w; }
    const bool inside = hit(x, y, w, h);
    g_lastId = id; if (inside) g_hot = id;
    const float arrowW = std::max(24.0f, h);
    const bool onArrowOnly = (flags & TreeNode_OpenOnArrow) != 0, onDbl = (flags & TreeNode_OpenOnDoubleClick) != 0;
    bool doToggle = false;
    if (g_active == id) {
        if (g_released) {
            if (inside && !leaf) {
                const bool arrow = g_pressX < ax + arrowW;          // where the press went down
                if (onDbl) { if (onArrowOnly && arrow) doToggle = true; }   // double click opens it elsewhere; with OpenOnArrow the arrow also takes one click
                else if (!onArrowOnly || arrow) doToggle = true;
            }
            g_active = 0;
        }
    } else if (inside && g_pressed) {
        g_active = id;
        if (!leaf && onDbl && g_dblClick) doToggle = true;
    }
    if (doToggle) { *open = !*open; g_toggledId = id; }
    const bool shown = leaf || *open;
    if ((flags & TreeNode_Framed) != 0) quad(x, y, w, h, pick((g_active == id && inside) ? Col_HeaderActive : (inside ? Col_HeaderHovered : Col_Header), g_theme.track));
    if ((flags & TreeNode_Selected) != 0) {
        unsigned char sel[4] = { g_theme.accent[0], g_theme.accent[1], g_theme.accent[2], 90 };
        quad(x, y, w, h, sel);
    }
    const bool bullet = (flags & TreeNode_Bullet) != 0;
    if (g_look) {
        unsigned char tmp[4]; const unsigned char white[4] = {255, 255, 255, 255};
        if (inside && !(flags & TreeNode_Framed)) rrect(x, y, w, h, 6.0f, pick(Col_HeaderHovered, withAlpha(white, 0.06f, tmp)));
        if (bullet) fillCircle(ax + 11.0f, y + h * 0.5f, 3.0f, g_theme.text);
        else if (!leaf) chevron(ax + 11.0f, y + h * 0.5f, 8.0f, *open, pick(Col_TextDisabled, g_theme.textDim));
        drawText(ax + 24.0f, y + (h - textH()) * 0.5f, VisLabel(label).b, s, g_theme.text);
    } else {
        if (inside && !(flags & TreeNode_Framed)) quad(x, y, w, h, pick(Col_HeaderHovered, g_theme.bgHover));
        if (bullet) fillCircle(ax + 4.0f + fCharW() * s * 0.5f, y + h * 0.5f, 3.0f, g_theme.text);
        else if (!leaf) drawText(ax + 4.0f, y + (h - textH()) * 0.5f, *open ? "v" : ">", s, g_theme.text);
        drawText(ax + 4.0f + fCharW() * s * 1.5f, y + (h - textH()) * 0.5f, VisLabel(label).b, s, g_theme.text);
    }
    if (shown && !(flags & TreeNode_NoTreePushOnOpen)) g_lay.indent += 16.0f;   // children indent until TreePop()
    return shown;
}
bool TreeNode(const char* label) { return TreeNodeEx(label, 0); }

void SetNextItemOpen(bool open) { g_nextOpen = open ? 1 : 0; }
bool IsItemToggledOpen() { return g_toggledId != 0 && g_toggledId == g_lastId; }

void TreePop() {
    if (!g_lay.active) return;
    g_lay.indent -= 16.0f;
    if (g_lay.indent < 0.0f) g_lay.indent = 0.0f;
}

} // namespace OkayUI
