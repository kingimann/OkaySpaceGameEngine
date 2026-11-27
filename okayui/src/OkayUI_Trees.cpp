#include "OkayUI_ContextState.hpp"
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

namespace {
// The open tree nodes' indent guides: where each line starts (under the chevron). Reset when a frame begins or the stack runs off.
struct TreeGuide { float x, y; };
TreeGuide g_treeGuide[16];
int g_treeTop = 0;
// How far each open node indented its children (TreePop takes the same amount back): 16 pixels, or the style's IndentSpacing in Dear
// ImGui's look (StyleVar_FramedWidgets). Past 64 levels the plain 16 is assumed.
float g_treeIndent[64];
int g_treeIndentTop = 0;
void treeIndentPush(float w) { if (g_treeIndentTop < 64) g_treeIndent[g_treeIndentTop] = w; ++g_treeIndentTop; g_lay.indent += w; }
float treeIndentPop() { if (g_treeIndentTop <= 0) return 16.0f; --g_treeIndentTop; return g_treeIndentTop < 64 ? g_treeIndent[g_treeIndentTop] : 16.0f; }
}
void ui_core::treeGuidesReset() { g_treeTop = 0; g_treeIndentTop = 0; }

namespace {
// A tree node's row (both looks): placed at the cursor `h` tall (from the content edge with SpanFullWidth), and its clicks: a release over
// it toggles a node with children (only on the arrow, ending at `arrowEnd`, with OpenOnArrow; a double click with OpenOnDoubleClick).
struct TreeRow { float x, y, w, h; bool inside; };
TreeRow treeRowPlace(int id, unsigned flags, float h, float arrowEnd, bool* open, bool leaf) {
    TreeRow r; r.h = h;
    place(fullW(), h, r.x, r.y);
    r.w = fullW();
    if (flags & TreeNode_SpanFullWidth) { r.x = g_lay.ox; r.w = g_lay.contentW; g_lay.prevX = r.x; g_lay.prevW = r.w; }
    r.inside = hit(r.x, r.y, r.w, h);
    g_lastId = id; if (r.inside) g_hot = id;
    const bool onArrowOnly = (flags & TreeNode_OpenOnArrow) != 0, onDbl = (flags & TreeNode_OpenOnDoubleClick) != 0;
    bool doToggle = false;
    if (g_active == id) {
        if (g_released) {
            if (r.inside && !leaf) {
                const bool arrow = g_pressX < arrowEnd;          // where the press went down
                if (onDbl) { if (onArrowOnly && arrow) doToggle = true; }   // double click opens it elsewhere; with OpenOnArrow the arrow also takes one click
                else if (!onArrowOnly || arrow) doToggle = true;
            }
            g_active = 0;
        }
    } else if (r.inside && g_pressed) {
        g_active = id;
        if (!leaf && onDbl && g_dblClick) doToggle = true;
    }
    if (doToggle) { *open = !*open; g_toggledId = id; }
    return r;
}

// Dear ImGui's tree node, as it documents and draws it (StyleVar_FramedWidgets, the editor and launcher on OkayUI): an unframed node is one
// text line tall; a framed one (TreeNode_Framed) is a frame tall with the header colour behind it. The arrow is a
// filled triangle FramePadding.x in, the label starts GetTreeNodeToLabelSpacing() (the font size and twice FramePadding.x) from the node's
// left edge, a selected or hovered node is filled with the header colours, children indent by the style's IndentSpacing.
bool treeNodeFramedLook(const char* label, unsigned flags, int id, bool* open, bool leaf) {
    const bool framed = (flags & TreeNode_Framed) != 0;
    const float th = textH(), h = framed ? rowH() : th, s = g_theme.textScale;
    const float padX = framePadXNow();
    const float ax = leftEdge();
    const TreeRow row = treeRowPlace(id, flags, h, ax + th + padX * 2.0f, open, leaf);
    const float x = row.x, y = row.y, w = row.w; const bool inside = row.inside;
    const bool selected = (flags & TreeNode_Selected) != 0;
    const int bg = (g_active == id && inside) ? Col_HeaderActive : (inside ? Col_HeaderHovered : ((selected || framed) ? Col_Header : -1));
    if (bg >= 0) { unsigned char tmp[4]; quad(x, y, w, h, styleColor(bg, tmp)); }
    const float cy = y + h * 0.5f;
    const unsigned char* tc = g_theme.text;
    if (flags & TreeNode_Bullet) fillCircle(ax + padX + th * 0.5f, cy, th * 0.2f, tc);
    else if (!leaf) {   // a filled triangle a little under the font size: right when closed, down when open
        const float r = th * 0.35f, ccx = ax + padX + th * 0.5f;
        if (*open) tri(ccx - r, cy - r * 0.6f, ccx + r, cy - r * 0.6f, ccx, cy + r * 0.8f, tc);
        else tri(ccx - r * 0.6f, cy - r, ccx - r * 0.6f, cy + r, ccx + r * 0.8f, cy, tc);
    }
    drawText(ax + th + padX * (framed ? 3.0f : 2.0f), y + (h - th) * 0.5f, VisLabel(label).b, s, tc);
    return leaf || *open;
}
}


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
    // A section header: a raised band with a chevron that turns as it opens, the label in the text colour; hover and press
    // ease in. Designs with accent headers put a short accent bar on its left edge.
    unsigned char st[4];
    const unsigned char* bg = stateColor(id, inside, g_active == id && inside, pick(Col_Header, g_theme.raised),
                                         pick(Col_HeaderHovered, g_theme.bgHover), pick(Col_HeaderActive, g_theme.bgDown), st);
    const float s = g_theme.textScale, rd = rnd();
    controlBox(x, y, w, h, rd, bg);
    if (g_theme.accentHeaders) rrect(x, y + 4.0f, 3.0f, h - 8.0f, 1.5f, g_theme.accent);
    chevron(x + 14.0f, y + h * 0.5f, 9.0f, *open, inside ? g_theme.text : pick(Col_TextDisabled, g_theme.textDim));
    drawText(x + (g_look ? 28.0f : 8.0f + fCharW() * s * 2.0f), y + (h - textH()) * 0.5f, VisLabel(label).b, s, g_theme.text);
    return *open;
}

bool TreeNodeEx(const char* label, unsigned flags) {
    if (!g_lay.active) return false;
    const int id = hashLabel(label);
    const bool leaf = (flags & TreeNode_Leaf) != 0;
    bool* open = openState(id, (flags & TreeNode_DefaultOpen) != 0);
    if (g_nextOpen >= 0) { *open = g_nextOpen != 0; g_nextOpen = -1; }
    if (g_framedWidgets) {
        const bool shown = treeNodeFramedLook(label, flags, id, open, leaf);
        if (shown && !(flags & TreeNode_NoTreePushOnOpen)) treeIndentPush(styleIndentSpacing());
        return shown;
    }
    const float h = rowH(), s = g_theme.textScale;
    const float ax = leftEdge();                                  // where the arrow sits: at the indent
    const TreeRow row = treeRowPlace(id, flags, h, ax + std::max(24.0f, h), open, leaf);
    const float x = row.x, y = row.y, w = row.w; const bool inside = row.inside;
    const bool shown = leaf || *open;
    if ((flags & TreeNode_Framed) != 0) quad(x, y, w, h, pick((g_active == id && inside) ? Col_HeaderActive : (inside ? Col_HeaderHovered : Col_Header), g_theme.track));
    if ((flags & TreeNode_Selected) != 0) {
        unsigned char sel[4] = { g_theme.accent[0], g_theme.accent[1], g_theme.accent[2], 90 };
        quad(x, y, w, h, sel);
    }
    const bool bullet = (flags & TreeNode_Bullet) != 0;
    // A row with a soft rounded hover, a chevron (a dot for a bullet node) at the indent and the label after it. The label's
    // place is the text grid of the bitmap font or 24 pixels with TrueType text, as before (layouts do not move).
    {
        unsigned char tmp[4];
        const float over = anim(id, 0, inside);
        if (over > 0.0f && !(flags & TreeNode_Framed)) rrect(x, y, w, h, g_theme.radiusSmall, pick(Col_HeaderHovered, withAlpha(g_theme.ink, 0.07f * over, tmp)));
        const float cx = g_look ? ax + 11.0f : ax + 4.0f + fCharW() * s * 0.5f;
        if (bullet) fillCircle(cx, y + h * 0.5f, 3.0f, g_theme.textDim);
        else if (!leaf) chevron(cx, y + h * 0.5f, 8.0f, *open, inside ? g_theme.text : pick(Col_TextDisabled, g_theme.textDim));
        drawText(g_look ? ax + 24.0f : ax + 4.0f + fCharW() * s * 1.5f, y + (h - textH()) * 0.5f, VisLabel(label).b, s, g_theme.text);
    }
    if (shown && !(flags & TreeNode_NoTreePushOnOpen) && g_treeTop < 16) { g_treeGuide[g_treeTop] = {g_look ? ax + 11.0f : ax + 4.0f + fCharW() * s * 0.5f, y + h}; ++g_treeTop; }
    if (shown && !(flags & TreeNode_NoTreePushOnOpen)) treeIndentPush(16.0f);   // children indent until TreePop()
    return shown;
}
bool TreeNode(const char* label) { return TreeNodeEx(label, 0); }

void SetNextItemOpen(bool open) { g_nextOpen = open ? 1 : 0; }
bool IsItemToggledOpen() { return g_toggledId != 0 && g_toggledId == g_lastId; }

void TreePop() {
    if (!g_lay.active) return;
    if (g_treeTop > 0) {   // the indent guide: a faint line from under the node's chevron down to its last child
        --g_treeTop;
        const TreeGuide& gd = g_treeGuide[g_treeTop];
        const float bottom = g_lay.cy - g_lay.spacingY * 0.5f;
        if (bottom > gd.y + 2.0f && gd.y > -1e6f) { unsigned char c[4]; quad(gd.x - 0.5f, gd.y, 1.0f, bottom - gd.y, withAlpha(g_theme.textFaint, 0.45f, c)); }
    }
    g_lay.indent -= treeIndentPop();
    if (g_lay.indent < 0.0f) g_lay.indent = 0.0f;
}

} // namespace OkayUI

// ---- What belongs to one context (OkayUI_ContextState.hpp: each context keeps its own copy of these) ----
namespace OkayUI {
OKAYUI_CONTEXT_STATE(
    OKAYUI_CTX_VAR(g_treeGuide),
    OKAYUI_CTX_VAR(g_treeTop),
    OKAYUI_CTX_VAR(g_treeIndent),
    OKAYUI_CTX_VAR(g_treeIndentTop))
} // namespace OkayUI
