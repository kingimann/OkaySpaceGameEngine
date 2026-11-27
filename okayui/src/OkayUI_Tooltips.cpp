#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include "OkayUIInternal.hpp"
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUICompat.hpp"

namespace OkayUI {
using namespace ui_core;

namespace ui_core {
// A tooltip box next to the cursor: several lines ("\n"), kept inside the display (flipped to the left / above when it would run off).
// A tooltip is drawn at the end of the frame, on top of everything (as Dear ImGui's tooltip window is): drawn where it was asked for, a
// tooltip inside an open list was painted over by the list's later rows, and it switched the overlay off for them, so they went under
// the list (stage 9: the First Person Hands presets lost their last three rows while one was hovered). The last one asked for in a
// frame is the one shown, as Dear ImGui's SetTooltip replaces an earlier one. The text scale it was asked with is kept.
struct PendingTip { char text[1024]; float scale; bool on; };
PendingTip g_pendingTip = {{0}, 1.0f, false};
void paintTooltipBox(const char* text);
void drawTooltipBox(const char* text) {
    if (!text || !*text) return;
    std::snprintf(g_pendingTip.text, sizeof g_pendingTip.text, "%s", text);
    g_pendingTip.scale = g_theme.textScale;
    g_pendingTip.on = true;
}
void flushTooltip() {
    if (!g_pendingTip.on) return;
    g_pendingTip.on = false;
    const float was = g_theme.textScale;
    g_theme.textScale = g_pendingTip.scale;
    paintTooltipBox(g_pendingTip.text);
    g_theme.textScale = was;
}
void dropTooltip() { g_pendingTip.on = false; }
void paintTooltipBox(const char* text) {
    if (!text || !*text) return;
    const float s = g_theme.textScale, th = textH(), lineGap = 2.0f;
    int lines = 1; float tw = 0.0f;
    {   // measure: the widest line and the line count
        char line[256]; int n = 0;
        for (const char* c = text;; ++c) {
            if (*c == '\n' || *c == 0) {
                line[n] = 0; tw = std::max(tw, labelW(line));
                if (*c == 0) break;
                ++lines; n = 0;
            } else if (n < 255) line[n++] = *c;
        }
    }
    const float pad = g_look ? 8.0f : 6.0f;
    const float bw = tw + pad * 2.0f, bh = th * (float)lines + lineGap * (float)(lines - 1) + pad * 2.0f;
    float bx = g_in.mouseX + 14.0f, by = g_in.mouseY + 14.0f;
    if (g_in.displayW > 0.0f && bx + bw > g_in.displayW - 2.0f) bx = std::max(2.0f, g_in.mouseX - 10.0f - bw);   // flip to the left of the cursor
    if (g_in.displayH > 0.0f && by + bh > g_in.displayH - 2.0f) by = std::max(2.0f, g_in.mouseY - 10.0f - bh);   // and above it
    g_toOverlay = true;
    {   // a small card in the design's tooltip colours, on a light shadow; an outline when the design draws strong outlines
        const float rd = std::min(g_theme.radiusSmall + 2.0f, bh * 0.5f);
        elevation(bx, by, bw, bh, rd, g_theme.shadowDepth > 1 ? 1 : g_theme.shadowDepth);
        rrect(bx, by, bw, bh, rd, g_theme.tooltipBg);
        if (g_theme.borderPx >= 2.0f) ringRect(bx, by, bw, bh, rd, 1.0f, g_theme.border);
    }
    char line[256]; int n = 0; float ly = by + pad;
    for (const char* c = text;; ++c) {
        if (*c == '\n' || *c == 0) {
            line[n] = 0; drawText(bx + pad, ly, line, s, g_theme.tooltipText);
            ly += th + lineGap; n = 0;
            if (*c == 0) break;
        } else if (n < 255) line[n++] = *c;
    }
    g_toOverlay = false;
}
} // namespace

void Tooltip(const char* text) {
    if (!g_lay.active || !text || !*text || g_in.blocked) return;
    // Only when the most recent item is hovered.
    if (!pointIn(g_in.mouseX, g_in.mouseY, g_lay.prevX, g_lay.prevY, g_lay.prevW, g_lay.prevH)) return;
    drawTooltipBox(text);
}

void SetTooltip(const char* fmt, ...) {
    if (!g_lay.active || !fmt || g_in.blocked) return;
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    drawTooltipBox(buf);   // unconditional: the caller decides when (usually `if (IsItemHovered()) SetTooltip(...)`)
}

// ---- Tooltip scope: BeginTooltip / EndTooltip --------------------------------------------------------------------------
// The widgets between the two calls are laid out into the overlay batch at a provisional spot next to the cursor, with input
// switched off. EndTooltip measures what they covered (their group box), moves their vertices to the final spot (flipped to the
// left / above like SetTooltip when it would run off the display), draws the box and moves its indices in front of the content.
namespace {
struct TipScope {
    bool open = false;
    int nested = 0;                 // BeginTooltip calls made while one is open: they draw into the outer one
    LayoutState lay;                // the layout to restore
    int grpTop = 0, lastId = 0; bool blocked = false, posOverride = false, toOverlay = false, discard = false;
    float nextItemW = -1.0f;
    int startV = 0, startI = 0, spanBase = 0;
};
TipScope g_tip;
} // namespace

bool BeginTooltip() {
    if (g_tip.open) { ++g_tip.nested; return true; }
    if (!g_lay.active || g_grpTop >= 8) return false;
    TipScope& t = g_tip;
    t.open = true; t.nested = 0; t.lay = g_lay; t.grpTop = g_grpTop; t.lastId = g_lastId; t.blocked = g_in.blocked;
    t.posOverride = g_posOverride; t.toOverlay = g_toOverlay; t.nextItemW = g_nextItemW;
    t.discard = g_in.blocked;            // input is blocked (a window covers the pointer): the tooltip is not shown, its widgets still run
    t.spanBase = g_novSpans;
    g_toOverlay = true;
    bindTexture(g_defTex);               // the box (drawn last, placed first) is flat: a flat span at the start of the tooltip
    t.startV = g_onv; t.startI = g_oni;
    g_in.blocked = true; g_posOverride = false; g_nextItemW = -1.0f;
    const float pad = g_look ? 8.0f : 6.0f;
    const float maxW = g_in.displayW > 0.0f ? std::max(120.0f, std::min(480.0f, g_in.displayW - 40.0f)) : 480.0f;
    g_lay.winClip = false;
    g_lay.ox = g_in.mouseX + 14.0f + pad; g_lay.oy = g_in.mouseY + 14.0f + pad;
    g_lay.cx = g_lay.ox; g_lay.cy = g_lay.oy; g_lay.contentW = maxW; g_lay.indent = 0.0f;
    g_lay.bottom = 1e7f; g_lay.maxRight = 0.0f; g_lay.pendingSameLine = false;
    g_lay.prevX = g_lay.ox; g_lay.prevY = g_lay.oy; g_lay.prevW = 0.0f; g_lay.prevH = 0.0f;
    g_grp[g_grpTop++] = GroupBox();      // the tooltip's own bounding box
    return true;
}

void EndTooltip() {
    if (!g_tip.open) return;
    TipScope& t = g_tip;
    if (t.nested > 0) { --t.nested; return; }
    const GroupBox box = g_grp[--g_grpTop];
    const float pad = g_look ? 8.0f : 6.0f;
    if (!box.any || t.discard) {         // nothing to show: drop whatever was drawn
        g_onv = t.startV; g_oni = t.startI; g_novSpans = t.spanBase;
    } else {
        // A text row is rowH tall (the text plus padding of its own), so the vertical inset is what is left of `pad` after that
        // padding: a one-line tooltip then has the same box as SetTooltip.
        const float padY = std::max(0.0f, pad - (rowH() - textH()) * 0.5f);
        const float bw = (box.x1 - box.x0) + pad * 2.0f, bh = (box.y1 - box.y0) + padY * 2.0f;
        float bx = g_in.mouseX + 14.0f, by = g_in.mouseY + 14.0f;
        if (g_in.displayW > 0.0f && bx + bw > g_in.displayW - 2.0f) bx = std::max(2.0f, g_in.mouseX - 10.0f - bw);
        if (g_in.displayH > 0.0f && by + bh > g_in.displayH - 2.0f) by = std::max(2.0f, g_in.mouseY - 10.0f - bh);
        const float dx = bx + pad - box.x0, dy = by + padY - box.y0;
        for (int i = t.startV; i < g_onv; ++i) { g_ovVerts[i].position.x += dx; g_ovVerts[i].position.y += dy; }
        const int contentEndI = g_oni, spansBefore = g_novSpans;
        g_toOverlay = true;
        // A tooltip with widgets in it is a popover (its widgets draw in the page's text colours).
        { const unsigned char bgc[4] = {g_theme.overlay[0], g_theme.overlay[1], g_theme.overlay[2], 250}; popoverBox(bx, by, bw, bh, bgc); }
        const int boxN = g_oni - contentEndI;
        g_novSpans = spansBefore;        // the box takes the flat span at the start of the tooltip
        if (boxN > 0 && contentEndI > t.startI) {
            std::rotate(g_ovIdx + t.startI, g_ovIdx + contentEndI, g_ovIdx + g_oni);   // box indices first, then the content's
            // The content's texture changes moved with it: shift them past the box, and keep a flat span at the start.
            for (int i = t.spanBase; i < g_novSpans; ++i) if (g_ovSpans[i].startIdx >= t.startI) g_ovSpans[i].startIdx += boxN;
            int first = t.spanBase; while (first < g_novSpans && g_ovSpans[first].startIdx < t.startI) ++first;
            // after the shift no span starts at startI any more (the flat one from BeginTooltip was shifted too): put a flat one back
            if (g_novSpans < 32) {
                for (int i = g_novSpans; i > first; --i) g_ovSpans[i] = g_ovSpans[i - 1];
                g_ovSpans[first] = OverlaySpan{t.startI, g_defTex}; ++g_novSpans;
            }
        }
    }
    g_lay = t.lay; g_grpTop = t.grpTop; g_lastId = t.lastId; g_in.blocked = t.blocked; g_posOverride = t.posOverride;
    g_nextItemW = t.nextItemW; g_toOverlay = t.toOverlay;
    { const bool keep = g_toOverlay; g_toOverlay = true; bindTexture(g_defTex); g_toOverlay = keep; }   // whatever the overlay draws next starts flat again
    t.open = false;
}

} // namespace OkayUI
