#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "OkayUIInternal.hpp"
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUICompat.hpp"

namespace OkayUI {
using namespace ui_core;

// ---- Columns -------------------------------------------------------------------
void Columns(int count) {
    if (!g_lay.active || count < 1) return;
    if (count > 16) count = 16;
    g_col.active = true; g_col.count = count; g_col.index = 0;
    g_col.x0 = g_lay.ox; g_col.fullW = g_lay.contentW;
    g_col.rowTop = g_lay.cy; g_col.rowBottom = g_lay.cy;
    g_lay.ox = g_col.x0;
    g_lay.contentW = g_col.fullW / count - 6.0f;   // small gutter between columns
    g_lay.cy = g_col.rowTop;
}

void NextColumn() {
    if (!g_lay.active || !g_col.active) return;
    if (g_lay.cy > g_col.rowBottom) g_col.rowBottom = g_lay.cy;   // track the tallest column
    ++g_col.index;
    if (g_col.index >= g_col.count) {          // past the last column: start a new row
        g_col.index = 0;
        g_col.rowTop = g_col.rowBottom;
    }
    const float colW = g_col.fullW / g_col.count;
    g_lay.ox = g_col.x0 + g_col.index * colW;
    g_lay.contentW = colW - 6.0f;
    g_lay.cy = g_col.rowTop;
    g_lay.pendingSameLine = false;
}

void EndColumns() {
    if (!g_lay.active || !g_col.active) return;
    if (g_lay.cy > g_col.rowBottom) g_col.rowBottom = g_lay.cy;
    g_lay.ox = g_col.x0;
    g_lay.contentW = g_col.fullW;
    g_lay.cy = g_col.rowBottom;
    g_col.active = false;
}

// ---- Tables --------------------------------------------------------------------
namespace ui_core {
// Left edge of column `i` (sum of prior column widths from the table origin), before any scrolling.
float tblColX(int i) {
    float cx = g_tbl.x0;
    for (int k = 0; k < i && k < g_tbl.count; ++k) cx += *g_tbl.w[k];
    return cx;
}
inline bool tblScrolls() { return g_tbl.scrollX || g_tbl.scrollY; }
inline bool tblColFrozen(int i) { return g_tbl.scrollX && i < g_tbl.freezeCols; }
// Where column `i` is on the screen.
inline float tblRealX(int i) { return tblColX(i) - ((g_tbl.scrollX && i >= g_tbl.freezeCols) ? g_tbl.sx : 0.0f); }
constexpr float kBarThick = 10.0f;
// A scrolling table's bars: StyleVar_ScrollbarSize when pushed (then inside the one-pixel outer border), else kBarThick.
inline float tblBar() { return g_sbSize > 0.0f ? g_sbSize : kBarThick; }

// Clip to a part of the scrolling box: the rows band (frozen rows: the whole box height; body rows: below the frozen rows), and the
// column band (frozen columns, the scrolling columns, or the full width for a row background). Only changes the clip when it differs.
void tblSetClip(bool frozenRow, int colBand /*0 frozen cols, 1 scrolling cols, 2 full*/) {
    if (!tblScrolls()) return;
    const float hbar = g_tbl.needH ? kBarThick : 0.0f;
    const float top = frozenRow ? g_tbl.oy : g_tbl.frozenBottom, bottom = g_tbl.oy + g_tbl.oh - hbar;
    float left = g_tbl.ox, right = g_tbl.ox + g_tbl.viewW;
    if (colBand == 0) right = g_tbl.ox + std::min(g_tbl.frozenW, g_tbl.viewW);
    else if (colBand == 1) left = g_tbl.ox + std::min(g_tbl.frozenW, g_tbl.viewW);
    const float r[4] = {left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top)};
    if (g_tbl.clipOn && r[0] == g_tbl.clipR[0] && r[1] == g_tbl.clipR[1] && r[2] == g_tbl.clipR[2] && r[3] == g_tbl.clipR[3]) return;
    if (g_tbl.clipOn) PopClipRect();
    PushClipRect(r[0], r[1], r[2], r[3]);
    g_tbl.clipOn = true; for (int i = 0; i < 4; ++i) g_tbl.clipR[i] = r[i];
}
// The clip of column `col`'s cell in the current row. A body row far outside the box is "culled": it keeps the plain full-width body clip, so
// its cells draw nowhere and the alternating frozen / scrolling clips do not pile up spans.
void tblCellClip(int col) {
    if (!tblScrolls()) return;
    const float rowRealTop = g_tbl.rowTop + g_tbl.shiftY;
    const bool culled = !g_tbl.rowFrozen && (rowRealTop + rowH() * 4.0f < g_tbl.frozenBottom || rowRealTop > g_tbl.oy + g_tbl.oh);
    tblSetClip(g_tbl.rowFrozen, culled ? 2 : (tblColFrozen(col) ? 0 : 1));
}
// The row clipper. Is the row whose top is at screen y `top` (and about `h` tall) wholly outside what can be seen? Then its cells still
// lay out (so the row heights, widths and ids stay exact) but nothing is emitted for them: no quads, no glyphs, so a table of thousands
// of rows neither fills the vertex batch nor costs time. Below the visible band is certain (rows only grow downward); above it allows
// four row heights of slack for a row taller than its remembered height. Frozen rows and overlay content are never culled.
bool tblRowOutside(float top, float h) {
    if (g_toOverlay || !g_rowClipper) return false;
    float clipTop, clipBot;
    if (tblScrolls()) {
        if (g_tbl.rowFrozen) return false;
        clipTop = g_tbl.frozenBottom; clipBot = g_tbl.oy + g_tbl.oh;
    } else if (g_clipTop > g_clipBase) {
        const PxRect& c = g_clipStack[g_clipTop - 1]; clipTop = (float)c.y; clipBot = (float)(c.y + c.h);
    } else if (g_dispH > 0.0f) {
        clipTop = 0.0f; clipBot = g_dispH;
    } else return false;
    return top > clipBot + 1.0f || top + 4.0f * h < clipTop;
}
// Point the layout at the current cell (column g_tbl.index at row g_tbl.rowTop).
void tblEnterCell() {
    const float pad = g_cellPadX;
    const float cx = tblRealX(g_tbl.index);
    tblCellClip(g_tbl.index);
    g_lay.ox = cx + pad;
    g_lay.contentW = *g_tbl.w[g_tbl.index] - 2.0f * pad;
    if (g_lay.contentW < 8.0f) g_lay.contentW = 8.0f;
    g_lay.cy = g_tbl.rowTop + g_tbl.shiftY + (g_cellPadY > 0.0f ? g_cellPadY : 0.0f);
    g_lay.indent = 0.0f;
    g_lay.pendingSameLine = false;
    g_lay.prevX = g_lay.ox; g_lay.prevY = g_lay.cy; g_lay.prevW = 0.0f; g_lay.prevH = 0.0f;
}

// Size the columns of a flag-style table from its TableSetupColumn calls: fixed columns take their pixel width, the rest share what is left by weight.
inline bool tblHidden(int i) { return i >= 0 && i < 16 && ((g_tbl.hidden >> i) & 1u) != 0; }
// A column mask kept in a float state slot. It is only a mask when it is a whole number from 0 to 0xFFFF (16 columns at most);
// anything else (never set, NaN, or a slot another widget wrote to) counts as unset, so it is never cast to unsigned out of range.
inline bool tblStoredMask(float v, unsigned& out) {
    if (!(v >= 0.0f && v <= 65535.0f)) return false;   // also false for NaN
    const unsigned m = (unsigned)v;
    if ((float)m != v) return false;
    out = m;
    return true;
}
void tblFinishSetup() {
    if (g_tbl.setupDone) return;
    g_tbl.setupDone = true;
    {   // which columns are hidden: remembered per table; the first time, the Col_DefaultHide ones. The last visible column cannot be hidden.
        g_tbl.pHidden = floatState(g_tbl.id ^ 0x7a00, -1.0f);
        unsigned stored = 0;
        if (!tblStoredMask(*g_tbl.pHidden, stored)) {
            for (int i = 0; i < g_tbl.nSetup && i < 16; ++i) if ((g_tbl.sFlags[i] & Col_DefaultHide) && !(g_tbl.sFlags[i] & Col_NoHide)) stored |= 1u << i;
            *g_tbl.pHidden = (float)stored;
        }
        unsigned m = stored & ((1u << g_tbl.count) - 1u);
        for (int i = 0; i < g_tbl.nSetup && i < 16; ++i) if (g_tbl.sFlags[i] & Col_NoHide) m &= ~(1u << i);
        if (m == ((1u << g_tbl.count) - 1u)) m &= ~1u;
        g_tbl.hidden = m; *g_tbl.pHidden = (float)m;
    }
    const float avail = g_tbl.scrollX && g_tbl.innerWidth > 0.0f ? g_tbl.innerWidth : g_tbl.fullW;
    float fixed = 0.0f, wsum = 0.0f; int nstretch = 0;
    for (int i = 0; i < g_tbl.count; ++i) {
        if (tblHidden(i)) continue;
        if (i < g_tbl.nSetup && (g_tbl.sFlags[i] & Col_WidthFixed) && g_tbl.sVal[i] > 0.0f) fixed += g_tbl.sVal[i];
        else { wsum += (i < g_tbl.nSetup && g_tbl.sVal[i] > 0.0f) ? g_tbl.sVal[i] : 1.0f; ++nstretch; }
    }
    const float rest = std::max(avail - fixed, 24.0f * (float)nstretch);
    float sum = 0.0f;
    for (int i = 0; i < g_tbl.count; ++i) {
        float seed;
        if (tblHidden(i)) { g_tbl.zeroW[i] = 0.0f; g_tbl.w[i] = &g_tbl.zeroW[i]; continue; }
        if (i < g_tbl.nSetup && (g_tbl.sFlags[i] & Col_WidthFixed) && g_tbl.sVal[i] > 0.0f) seed = g_tbl.sVal[i];
        else seed = rest * ((i < g_tbl.nSetup && g_tbl.sVal[i] > 0.0f) ? g_tbl.sVal[i] : 1.0f) / (wsum > 0.0f ? wsum : 1.0f);
        g_tbl.w[i] = floatState(g_tbl.id ^ (0x7100 + i), seed);
        if (*g_tbl.w[i] < 12.0f) *g_tbl.w[i] = 12.0f;
        sum += *g_tbl.w[i];
    }
    {   // columns shown again this frame: they come back at the width they had, the others give that back (so hide and show is exact)
        float* prevM = floatState(g_tbl.id ^ 0x7a02, -1.0f);
        unsigned prevStored = 0;
        const unsigned prev = tblStoredMask(*prevM, prevStored) ? (prevStored & ((1u << g_tbl.count) - 1u)) : g_tbl.hidden;
        const unsigned newly = prev & ~g_tbl.hidden;
        *prevM = (float)g_tbl.hidden;
        if (newly && !g_tbl.scrollX) {
            float total = 0.0f, others = 0.0f;
            for (int i = 0; i < g_tbl.count; ++i) { if (tblHidden(i)) continue; if ((newly >> i) & 1u) total += *g_tbl.w[i]; else others += *g_tbl.w[i]; }
            if (others > 0.0f && g_tbl.fullW > total + 24.0f) {
                const float f = (g_tbl.fullW - total) / others;
                for (int i = 0; i < g_tbl.count; ++i) if (!tblHidden(i) && !((newly >> i) & 1u)) *g_tbl.w[i] *= f;
            }
            sum = 0.0f;
            for (int i = 0; i < g_tbl.count; ++i) if (!tblHidden(i)) sum += *g_tbl.w[i];
        }
    }
    if (g_tbl.scrollX) {   // a box that scrolls sideways keeps its column widths
        g_tbl.contentW = sum;
        g_tbl.needH = sum > g_tbl.viewW + 0.5f;
        g_tbl.fullW = sum;
        g_tbl.frozenW = 0.0f;
        for (int i = 0; i < g_tbl.freezeCols && i < g_tbl.count; ++i) g_tbl.frozenW += *g_tbl.w[i];
        if (g_tbl.frozenW > g_tbl.viewW * 0.9f) g_tbl.frozenW = 0.0f, g_tbl.freezeCols = 0;   // frozen columns that fill the box would leave nothing to scroll
    } else if (sum > 0.0f) {
        const float f = g_tbl.fullW / sum; for (int i = 0; i < g_tbl.count; ++i) if (!tblHidden(i)) *g_tbl.w[i] *= f;
    }
    // the sort keys: the DefaultSort columns the first time the table is seen
    if ((g_tbl.flags & Table_Sortable) && g_tbl.store && !g_tbl.store->inited) {
        TblStore& t = *g_tbl.store;
        t.inited = true; t.sortN = 0;
        for (int i = 0; i < g_tbl.nSetup && t.sortN < kTblSortKeys; ++i)
            if ((g_tbl.sFlags[i] & Col_DefaultSort) && !(g_tbl.sFlags[i] & Col_NoSort)) { t.sortCol[t.sortN] = i; t.sortDesc[t.sortN] = (g_tbl.sFlags[i] & Col_PreferSortDescending) != 0; ++t.sortN; }
        t.dirty = t.sortN > 0;
    }
}
// Sorting by a header click (ImGui's rules): a plain click makes the column the only key (or flips it if it already is the only key,
// and with tristate removes it after descending); Ctrl+click with SortMulti adds it as a further key or flips / removes it.
void tblSortClick(int col) {
    TblStore& t = *g_tbl.store;
    const bool multi = (g_tbl.flags & Table_SortMulti) != 0 && g_io.keyCtrl, tri = (g_tbl.flags & Table_SortTristate) != 0;
    const bool pref = (g_tbl.sFlags[col] & Col_PreferSortDescending) != 0;
    int at = -1;
    for (int k = 0; k < t.sortN; ++k) if (t.sortCol[k] == col) at = k;
    auto removeKey = [&](int k) { for (int j = k; j + 1 < t.sortN; ++j) { t.sortCol[j] = t.sortCol[j + 1]; t.sortDesc[j] = t.sortDesc[j + 1]; } --t.sortN; };
    if (multi) {
        if (at < 0) { if (t.sortN < kTblSortKeys) { t.sortCol[t.sortN] = col; t.sortDesc[t.sortN] = pref; ++t.sortN; } }
        else if (t.sortDesc[at] == pref && !(tri && t.sortDesc[at] != pref)) t.sortDesc[at] = !pref;       // first click flipped it
        else if (tri) removeKey(at);
        else t.sortDesc[at] = !t.sortDesc[at];
    } else if (at >= 0 && t.sortN == 1) {
        if (t.sortDesc[at] == pref) t.sortDesc[at] = !pref;
        else if (tri) t.sortN = 0;
        else t.sortDesc[at] = pref;
    } else { t.sortN = 1; t.sortCol[0] = col; t.sortDesc[0] = pref; }
    t.dirty = true;
}
// The header bar with bold column labels (and sort arrows); moves the row cursor below it.
void tblDrawHeader(const char* const* labels, bool interactive) {
    const float hh = rowH();
    const float rowY = g_tbl.rowTop;                   // headers are never scrolled vertically
    const bool frozen = g_tbl.rowsDone < g_tbl.freezeRows || !g_tbl.scrollY;
    if (tblScrolls()) tblSetClip(true, 2);
    const float fullL = g_tbl.scrollX ? g_tbl.ox : g_tbl.x0, fullW2 = g_tbl.scrollX ? g_tbl.viewW : g_tbl.fullW;
    if (colorIsSet(Col_TableHeaderBg)) quad(fullL, rowY, fullW2, hh, g_theme.colors[Col_TableHeaderBg]);   // a header colour the program set
    quad(fullL, rowY + hh - 1.0f, fullW2, 1.0f, pick(Col_TableBorderStrong, g_theme.border));   // hairline under dim headers
    const Font* prevF = g_font; g_font = &kFontBold;
    for (int i = 0; i < g_tbl.count; ++i) {
        if (tblHidden(i)) continue;
        const float cx = tblRealX(i), cw = *g_tbl.w[i];
        if (tblScrolls()) tblSetClip(true, tblColFrozen(i) ? 0 : 1);
        const bool sortable = interactive && (g_tbl.flags & Table_Sortable) && g_tbl.store && i < g_tbl.nSetup && !(g_tbl.sFlags[i] & Col_NoSort);
        bool hot = false, down = false;
        if (sortable) {
            const int hid = g_tbl.id ^ (0x7c00 + i);
            const float hitW = cw - (i + 1 < g_tbl.count ? 4.0f : 0.0f);      // the border's resize handle keeps its few pixels
            hot = hit(cx, rowY, hitW, hh);
            if (hot) g_hot = hid;
            if (g_active == hid) { if (g_released) { if (hot) tblSortClick(i); g_active = 0; } else down = true; }
            else if (hot && g_pressed) { g_active = hid; g_focusClaimed = true; down = true; }
            if (hot || down) quad(cx, rowY, cw, hh, pick(down ? Col_HeaderActive : Col_HeaderHovered, down ? g_theme.bgDown : g_theme.bgHover));
        }
        if (labels[i] && labels[i][0] && !(i < g_tbl.nSetup && (g_tbl.sFlags[i] & Col_NoHeaderLabel)))
            drawText(cx + 4.0f, rowY + (hh - textH()) * 0.5f, labels[i], g_theme.textScale, pick(Col_TextDisabled, g_theme.textDim));
        if (sortable) {
            const TblStore& t = *g_tbl.store;
            for (int k = 0; k < t.sortN; ++k) if (t.sortCol[k] == i) {
                const float ax = cx + cw - 14.0f, ay = rowY + hh * 0.5f, r = 4.0f;
                chevronDir(ax, ay, r * 2.0f, t.sortDesc[k] ? 1 : 3, g_theme.text);
                if (t.sortN > 1) { char d[4]; std::snprintf(d, sizeof d, "%d", k + 1); drawText(ax - r - 12.0f, rowY + (hh - textH()) * 0.5f, d, g_theme.textScale * 0.7f, g_theme.text); }
            }
        }
    }
    g_font = prevF;
    (void)frozen;
    g_tbl.rowTop += hh;
    g_tbl.rowBottom = g_tbl.rowTop;
    g_tbl.rowStart = g_tbl.rowTop;
    g_tbl.headerBottom = g_tbl.rowTop;
    g_tbl.headerDrawn = true;
    ++g_tbl.rowsDone;
}
// Close the row in progress: the lowest point its cells reached becomes the row's bottom; its height is remembered for the next frame's band.
void tblFinishRow() {
    const float cyLogical = g_lay.cy - g_tbl.shiftY + (g_cellPadY >= 0.0f && g_tbl.index >= 0 ? g_cellPadY - g_lay.spacingY : 0.0f);
    if (cyLogical > g_tbl.rowBottom) g_tbl.rowBottom = cyLogical;
    if (g_tbl.rowTop + g_tbl.minRowH > g_tbl.rowBottom) g_tbl.rowBottom = g_tbl.rowTop + g_tbl.minRowH;
}
void tblRememberRow() {
    if (g_tbl.store && g_tbl.rowIndex >= 0 && g_tbl.rowIndex < kTblRows) g_tbl.store->rowH[g_tbl.rowIndex] = std::max(0.0f, g_tbl.rowBottom - g_tbl.rowTop);
}
} // namespace

bool BeginTable(const char* id, int columns, const char* const* headers) {
    return BeginTable(id, columns, headers, nullptr);
}

float CalcTextWidth(const char* text) { return labelW(text); }

bool BeginTable(const char* id, int columns, const char* const* headers, const float* weights) {
    if (!g_lay.active || columns < 1) return false;
    if (columns > 16) columns = 16;
    const int tid = hashLabel(id);
    g_tbl = TableState();
    g_tbl.active = true; g_tbl.id = tid; g_tbl.count = columns; g_tbl.index = -1;
    g_tbl.x0 = g_lay.ox; g_tbl.fullW = g_lay.contentW;
    g_tbl.rowTop = g_lay.cy; g_tbl.rowBottom = g_lay.cy; g_tbl.rowStart = g_lay.cy;
    g_tbl.topY = g_lay.cy; g_tbl.flags = 0; g_tbl.rowIndex = -1; g_tbl.setupDone = true; g_tbl.nSetup = 0;

    // Fetch/seed persisted column widths, then rescale so they exactly fill fullW
    // (keeps the table snug when the window is resized).
    float sum = 0.0f, wsum = 0.0f;
    if (weights) for (int i = 0; i < columns; ++i) wsum += weights[i] > 0.0f ? weights[i] : 0.0f;
    for (int i = 0; i < columns; ++i) {
        const float seed = (weights && wsum > 0.0f) ? g_tbl.fullW * std::max(weights[i], 0.0f) / wsum : g_tbl.fullW / columns;
        g_tbl.w[i] = floatState(tid ^ (0x7100 + i), seed);
        if (*g_tbl.w[i] < 12.0f) *g_tbl.w[i] = 12.0f;
        sum += *g_tbl.w[i];
    }
    if (sum > 0.0f) { float f = g_tbl.fullW / sum; for (int i = 0; i < columns; ++i) *g_tbl.w[i] *= f; }

    // Optional header row: a bar with bold column labels.
    if (headers) {
        const float hh = rowH();
        if (colorIsSet(Col_TableHeaderBg)) quad(g_tbl.x0, g_tbl.rowTop, g_tbl.fullW, hh, g_theme.colors[Col_TableHeaderBg]);
        quad(g_tbl.x0, g_tbl.rowTop + hh - 1.0f, g_tbl.fullW, 1.0f, pick(Col_TableBorderStrong, g_theme.border));   // hairline under dim headers
        const Font* prevF = g_font; g_font = &kFontBold;
        for (int i = 0; i < columns; ++i) {
            float cx = tblColX(i);
            if (headers[i] && headers[i][0])
                drawText(cx + 4.0f, g_tbl.rowTop + (hh - textH()) * 0.5f, headers[i], g_theme.textScale, pick(Col_TextDisabled, g_theme.textDim));
        }
        g_font = prevF;
        g_tbl.rowTop += hh;
        g_tbl.rowBottom = g_tbl.rowTop;
        g_tbl.rowStart = g_tbl.rowTop;
    }
    g_tbl.headerBottom = g_tbl.rowTop;
    return true;
}

bool BeginTable(const char* id, int columns, TableFlags flags) {
    return BeginTable(id, columns, flags, Vec2{}, 0.0f);
}

bool BeginTable(const char* id, int columns, TableFlags flags, Vec2 outerSize, float innerWidth) {
    if (!g_lay.active || columns < 1) return false;
    if (columns > 16) columns = 16;
    const int tid = hashLabel(id);
    g_tbl = TableState();
    g_tbl.active = true; g_tbl.id = tid; g_tbl.count = columns; g_tbl.index = -1;
    g_tbl.x0 = g_lay.ox; g_tbl.fullW = g_lay.contentW;
    g_tbl.rowTop = g_tbl.rowBottom = g_tbl.rowStart = g_tbl.headerBottom = g_tbl.topY = g_lay.cy;
    g_tbl.flags = (int)flags; g_tbl.rowIndex = -1; g_tbl.nSetup = 0; g_tbl.setupDone = false;
    if (flags & Table_Sortable) { g_tbl.store = tblStoreFor(tid); g_tbl.store->lastFrame = g_frameCount; }
    else if (flags & Table_RowBg) { g_tbl.store = tblStoreFor(tid); g_tbl.store->lastFrame = g_frameCount; }
    g_tbl.scrollX = (flags & Table_ScrollX) != 0; g_tbl.scrollY = (flags & Table_ScrollY) != 0;
    if (g_tbl.scrollX || g_tbl.scrollY) {
        g_tbl.pSX = floatState(tid ^ 0x7800, 0.0f); g_tbl.pSY = floatState(tid ^ 0x7801, 0.0f);
        g_tbl.pMaxY = floatState(tid ^ 0x7802, 0.0f); g_tbl.pH = floatState(tid ^ 0x7803, rowH() * 3.0f);
        const float ow = (outerSize.x > 0.0f && std::isfinite(outerSize.x)) ? outerSize.x : availW();
        float oh;
        if (outerSize.y > 0.0f && std::isfinite(outerSize.y)) oh = outerSize.y;
        else if (g_tbl.scrollY) oh = std::max(rowH() * 4.0f, g_lay.bottom - g_lay.cy);
        else oh = *g_tbl.pH + (g_tbl.scrollX ? kBarThick : 0.0f);
        float bx, by; place(ow, oh, bx, by);
        g_tbl.saved = g_lay;                  // the cursor is already below the box; the cells work from its top
        g_lay.ox = bx; g_lay.cy = by; g_lay.contentW = ow; g_lay.indent = 0.0f; g_lay.pendingSameLine = false;
        g_tbl.ox = bx; g_tbl.oy = by; g_tbl.ow = ow; g_tbl.oh = oh;
        g_tbl.needV = g_tbl.scrollY && *g_tbl.pMaxY > 0.0f;
        g_tbl.viewW = ow - (g_tbl.needV ? tblBar() + (g_sbSize > 0.0f ? 1.0f : 0.0f) : 0.0f);
        g_tbl.innerWidth = innerWidth > 0.0f && std::isfinite(innerWidth) ? innerWidth : 0.0f;
        g_tbl.x0 = bx; g_tbl.fullW = g_tbl.viewW;
        g_tbl.rowTop = g_tbl.rowBottom = g_tbl.rowStart = g_tbl.headerBottom = g_tbl.topY = by;
        g_tbl.sx = *g_tbl.pSX; g_tbl.sy = *g_tbl.pSY;
        g_tbl.frozenBottom = by;
        // the wheel (Shift+wheel for the sideways direction) scrolls while the pointer is over the box
        if (g_in.wheel != 0.0f && !g_wheelUsed && hit(bx, by, ow, oh)) {
            const bool sideways = g_tbl.scrollX && (g_io.keyShift || !g_tbl.scrollY);
            if (sideways) { g_tbl.sx -= g_in.wheel * rowH() * 2.0f; g_wheelUsed = true; }
            else if (g_tbl.scrollY && *g_tbl.pMaxY > 0.0f) { g_tbl.sy -= g_in.wheel * rowH(); g_wheelUsed = true; }
        }
        g_tbl.sy = std::max(0.0f, std::min(g_tbl.sy, *g_tbl.pMaxY));
        g_tbl.sx = std::max(0.0f, g_tbl.sx);
    }
    return true;
}

void TableSetupColumn(const char* label, int columnFlags, float widthOrWeight, unsigned userId) {
    if (!g_tbl.active || g_tbl.setupDone || g_tbl.nSetup >= g_tbl.count) return;
    if (!std::isfinite(widthOrWeight) || widthOrWeight < 0.0f) widthOrWeight = 0.0f;
    if (widthOrWeight > 100000.0f) widthOrWeight = 100000.0f;
    g_tbl.sLabel[g_tbl.nSetup] = label; g_tbl.sFlags[g_tbl.nSetup] = columnFlags; g_tbl.sVal[g_tbl.nSetup] = widthOrWeight; g_tbl.sUser[g_tbl.nSetup] = userId;
    ++g_tbl.nSetup;
}

void TableSetupScrollFreeze(int cols, int rows) {
    if (!g_tbl.active || g_tbl.setupDone) return;
    g_tbl.freezeCols = std::max(0, std::min(cols, g_tbl.count)); g_tbl.freezeRows = std::max(0, std::min(rows, 32));
}

void TableHeadersRow() {
    g_tbl.rowCulled = false; g_cullRow = false;
    if (!g_lay.active || !g_tbl.active) return;
    tblFinishSetup();
    tblFinishRow();
    g_tbl.rowTop = g_tbl.rowBottom;
    g_tbl.shiftY = 0.0f;
    const char* none[16] = {nullptr};
    for (int i = 0; i < g_tbl.count && i < g_tbl.nSetup; ++i) none[i] = g_tbl.sLabel[i];
    tblDrawHeader(none, true);
    if ((g_tbl.flags & Table_Hideable) && g_tbl.pHidden && g_lay.active && !g_tbl.scrollX && !g_tbl.scrollY) {
        // a right click on the header row opens the show / hide menu (every column with a check box; a NoHide column, and the last visible one, are locked)
        const float hh = rowH(), rowY = g_tbl.rowTop - hh;
        const int pid = g_tbl.id ^ 0x7a01;
        if (g_rPressed && !g_in.blocked && hit(g_tbl.x0, rowY, g_tbl.fullW, hh)) {
            PopupSlot* ps = popupSlot(pid, true);
            if (ps) { ps->open = true; ps->x = g_in.mouseX; ps->y = g_in.mouseY; ps->w = 0.0f; g_openPopupThisFrame = pid; }
        }
        const TableState keep = g_tbl;   // the menu's widgets use the layout, not the table; the table state is restored after
        if (beginPopupBody(pid)) {
            int visible = 0; for (int i = 0; i < keep.count; ++i) if (!((keep.hidden >> i) & 1u)) ++visible;
            for (int i = 0; i < keep.count; ++i) {
                const bool on = !((keep.hidden >> i) & 1u);
                const bool locked = (i < keep.nSetup && (keep.sFlags[i] & Col_NoHide)) || (on && visible <= 1);
                bool v = on;
                BeginDisabled(locked);
                char idb[16]; std::snprintf(idb, sizeof idb, "tc%d", i);
                PushID(idb);   // two columns may share a label: each check box gets its own id
                if (Checkbox((i < keep.nSetup && keep.sLabel[i] && keep.sLabel[i][0]) ? keep.sLabel[i] : "(column)", &v) && v != on) { unsigned m = keep.hidden; if (v) m &= ~(1u << i); else m |= 1u << i; *keep.pHidden = (float)m; }
                PopID();
                EndDisabled();
            }
            EndPopup();
        }
    }
}

bool TableSetColumnEnabled(int column, bool enabled) {
    if (!g_tbl.active || column < 0 || column >= g_tbl.count) return false;
    tblFinishSetup();
    if (!g_tbl.pHidden) return false;
    if (column < g_tbl.nSetup && (g_tbl.sFlags[column] & Col_NoHide) && !enabled) return false;
    unsigned m = g_tbl.hidden;
    if (tblStoredMask(*g_tbl.pHidden, m)) m &= (1u << g_tbl.count) - 1u;   // the pending set (a change made earlier this frame counts)
    else m = g_tbl.hidden;
    if (enabled) m &= ~(1u << column); else m |= 1u << column;
    if (m == ((1u << g_tbl.count) - 1u)) return false;   // never hide them all
    *g_tbl.pHidden = (float)m;                           // takes effect next frame (this frame's widths are set)
    return true;
}
bool TableGetColumnIsEnabled(int column) { return g_tbl.active && column >= 0 && column < g_tbl.count && !tblHidden(column); }
int TableGetColumnCount() { return g_tbl.active ? g_tbl.count : 0; }
const char* TableGetColumnName(int column) {
    if (!g_tbl.active) return "";
    if (column < 0) column = g_tbl.index;
    return (column >= 0 && column < g_tbl.nSetup && g_tbl.sLabel[column]) ? g_tbl.sLabel[column] : "";
}
int TableGetColumnFlags(int column) {
    if (!g_tbl.active) return 0;
    if (column < 0) column = g_tbl.index;
    return (column >= 0 && column < g_tbl.nSetup) ? g_tbl.sFlags[column] : 0;
}
int TableGetHoveredColumn() {
    if (!g_tbl.active || g_in.blocked || g_tbl.count <= 0) return -1;
    tblFinishSetup();                                    // the widths exist once the columns are set up
    const float top = g_tbl.scrollX || g_tbl.scrollY ? g_tbl.oy : g_tbl.topY;
    const float bottom = std::max(g_tbl.rowBottom, g_lay.cy);
    if (g_in.mouseY < top || g_in.mouseY > bottom) return -1;
    for (int i = 0; i < g_tbl.count; ++i) {
        if (tblHidden(i)) continue;
        const float x = tblRealX(i);
        if (g_in.mouseX >= x && g_in.mouseX < x + *g_tbl.w[i]) return i;
    }
    return -1;
}

void TableNextRow(int, float minRowHeight) { TableNextRow(); if (g_tbl.active && std::isfinite(minRowHeight) && minRowHeight > 0.0f) g_tbl.minRowH = minRowHeight; }
TableSortSpecs* TableGetSortSpecs() {
    static TableColumnSortSpecs arr[kTblSortKeys];
    static TableSortSpecs out;
    if (!g_tbl.active || !(g_tbl.flags & Table_Sortable) || !g_tbl.store) return nullptr;
    tblFinishSetup();
    const TblStore& t = *g_tbl.store;
    for (int k = 0; k < t.sortN; ++k) { arr[k].columnIndex = t.sortCol[k]; arr[k].sortOrder = k; arr[k].descending = t.sortDesc[k]; arr[k].columnUserId = t.sortCol[k] < g_tbl.nSetup ? g_tbl.sUser[t.sortCol[k]] : 0; arr[k].ColumnUserID = arr[k].columnUserId; arr[k].ColumnIndex = arr[k].columnIndex; arr[k].SortOrder = k; arr[k].SortDirection = arr[k].descending ? 2 : 1; }
    out.specs = arr; out.specsCount = t.sortN; out.specsDirty = t.dirty; out.SpecsDirty = t.dirty;
    out.Specs = arr; out.SpecsCount = t.sortN;
    g_sortSpecsFor = g_tbl.id; g_sortSpecsOut = &out;
    return &out;
}

float TableGetScrollX() { return g_tbl.active && g_tbl.scrollX ? g_tbl.sx : 0.0f; }
float TableGetScrollY() { return g_tbl.active && g_tbl.scrollY ? g_tbl.sy : 0.0f; }
void TableSetScroll(float x, float y) {
    if (!g_tbl.active || !(g_tbl.scrollX || g_tbl.scrollY)) return;
    if (g_tbl.scrollX && std::isfinite(x)) g_tbl.sx = std::max(0.0f, x);
    if (g_tbl.scrollY && std::isfinite(y)) g_tbl.sy = std::max(0.0f, std::min(y, *g_tbl.pMaxY));
}

void SetTableRowClipping(bool on) { g_rowClipper = on; }
bool TableRowIsVisible() { return !(g_tbl.active && g_tbl.rowCulled); }
bool TableSetColumnIndex(int column) {
    if (!g_lay.active || !g_tbl.active || column < 0 || column >= g_tbl.count) return false;
    g_tbl.index = column - 1;
    return TableNextColumn();
}
int TableGetColumnIndex() { return g_tbl.active ? g_tbl.index : -1; }
int TableGetRowIndex() { return g_tbl.active ? g_tbl.rowIndex : -1; }

void TableNextRow() {
    if (!g_lay.active || !g_tbl.active) return;
    tblFinishSetup();
    tblFinishRow();
    tblRememberRow();
    g_tbl.minRowH = 0.0f;
    g_tbl.rowTop = g_tbl.rowBottom;   // next row starts below the tallest cell so far
    g_tbl.index = -1;
    ++g_tbl.rowIndex;
    // scrolling boxes: rows after the frozen ones are shifted by the scroll and clipped below the frozen part
    g_tbl.rowFrozen = !g_tbl.scrollY || g_tbl.rowsDone < g_tbl.freezeRows;
    if (!g_tbl.rowFrozen && !g_tbl.bodyStarted) { g_tbl.bodyStarted = true; g_tbl.frozenBottom = g_tbl.rowTop; }
    g_tbl.shiftY = (g_tbl.scrollY && !g_tbl.rowFrozen) ? -g_tbl.sy : 0.0f;
    ++g_tbl.rowsDone;
    const float bandH = (g_tbl.store && g_tbl.rowIndex < kTblRows && g_tbl.store->rowH[g_tbl.rowIndex] > 0.0f) ? g_tbl.store->rowH[g_tbl.rowIndex] : rowH();
    g_tbl.rowBgH = bandH;
    const float by = g_tbl.rowTop + g_tbl.shiftY;
    g_tbl.rowCulled = tblRowOutside(by, bandH); g_cullRow = g_tbl.rowCulled;
    const float bx = g_tbl.scrollX ? g_tbl.ox : g_tbl.x0, bw = g_tbl.scrollX ? g_tbl.viewW : g_tbl.fullW;
    if (tblScrolls()) tblSetClip(g_tbl.rowFrozen, 2);
    if ((g_tbl.flags & Table_RowBg) && (g_tbl.rowIndex & 1)) {   // banded rows: drawn first so the cells go on top
        unsigned char tmp[4];
        quad(bx, by, bw, bandH, pick(Col_TableRowBgAlt, withAlpha(g_theme.ink, 0.045f, tmp)));
    } else if ((g_tbl.flags & Table_RowBg) && colorIsSet(Col_TableRowBg)) {
        quad(bx, by, bw, bandH, g_theme.colors[Col_TableRowBg]);
    }
    if ((g_tbl.flags & Table_BordersInnerH) && g_tbl.rowTop > g_tbl.rowStart + 0.5f) quad(bx, by, bw, 1.0f, pick(Col_TableBorderLight, g_theme.border));
}

bool TableNextColumn() {
    if (!g_lay.active || !g_tbl.active) return false;
    tblFinishSetup();
    if (g_tbl.index >= 0) tblFinishRow();   // remember how tall the cell we're leaving got
    ++g_tbl.index;
    if (g_tbl.index >= g_tbl.count) return false;
    tblEnterCell();
    if (tblHidden(g_tbl.index)) { g_lay.ox = -30000.0f; return false; }   // a hidden column's cell lays out far off screen, so widgets drawn there are never seen or hit
    return true;
}

void TableSetBgColor(TableBgTarget target, Color32 color, int column) {
    if (!g_lay.active || !g_tbl.active || g_tbl.rowIndex < 0) return;
    const unsigned char c[4] = {(unsigned char)(color & 255u), (unsigned char)((color >> 8) & 255u), (unsigned char)((color >> 16) & 255u), (unsigned char)((color >> 24) & 255u)};
    const float by = g_tbl.rowTop + g_tbl.shiftY;
    if (target == TableBg_CellBg) {
        const int col = column >= 0 ? column : g_tbl.index;
        if (col < 0 || col >= g_tbl.count) return;
        if (tblScrolls()) tblSetClip(g_tbl.rowFrozen, tblColFrozen(col) ? 0 : 1);
        quad(tblRealX(col), by, *g_tbl.w[col], g_tbl.rowBgH, c);
    } else if (target == TableBg_RowBg0 || target == TableBg_RowBg1) {
        const float bx = g_tbl.scrollX ? g_tbl.ox : g_tbl.x0, bw = g_tbl.scrollX ? g_tbl.viewW : g_tbl.fullW;
        if (tblScrolls()) tblSetClip(g_tbl.rowFrozen, 2);
        quad(bx, by, bw, g_tbl.rowBgH, c);
    }
    if (tblScrolls() && g_tbl.index >= 0) tblCellClip(g_tbl.index);
}

// Table_BordersOuter: a one-pixel frame in Col_TableBorderStrong around the table (or its scrolling box).
static void tblOuterBorder(float x, float y, float w, float h) {
    if (!(w > 1.0f) || !(h > 1.0f)) return;
    const unsigned char* c = pick(Col_TableBorderStrong, g_theme.border);
    quad(x, y, w, 1.0f, c); quad(x, y + h - 1.0f, w, 1.0f, c);
    quad(x, y + 1.0f, 1.0f, h - 2.0f, c); quad(x + w - 1.0f, y + 1.0f, 1.0f, h - 2.0f, c);
}

void EndTable() {
    if (!g_lay.active || !g_tbl.active) return;
    g_tbl.rowCulled = false; g_cullRow = false;
    tblFinishSetup();
    tblFinishRow();
    tblRememberRow();
    const float top = g_tbl.scrollX || g_tbl.scrollY ? g_tbl.oy : g_tbl.topY, bottom = g_tbl.rowBottom;
    if (g_tbl.scrollX || g_tbl.scrollY) {
        // the body's own height, the scroll limits, then the borders, the bars and the wheel
        if (g_tbl.clipOn) { PopClipRect(); g_tbl.clipOn = false; }
        const float hbar = g_tbl.needH ? kBarThick : 0.0f;
        const float bodyTop = g_tbl.bodyStarted ? g_tbl.frozenBottom : std::max(g_tbl.frozenBottom, g_tbl.rowBottom);
        const float viewH = g_tbl.oy + g_tbl.oh - hbar - bodyTop;
        const float contentH = std::max(0.0f, g_tbl.rowBottom - bodyTop);
        const float maxY = g_tbl.scrollY ? std::max(0.0f, contentH - viewH) : 0.0f;
        *g_tbl.pMaxY = maxY;
        if (g_tbl.scrollX) *g_tbl.pH = std::max(rowH(), g_tbl.rowBottom - g_tbl.oy);
        else *g_tbl.pH = std::max(rowH(), g_tbl.rowBottom - g_tbl.oy);
        float sy = std::max(0.0f, std::min(g_tbl.sy, maxY)), sx = g_tbl.sx;
        const float maxX = g_tbl.scrollX ? std::max(0.0f, g_tbl.contentW - g_tbl.viewW) : 0.0f;
        sx = std::max(0.0f, std::min(sx, maxX));
        // column borders and the resize handles (clipped to the box; a frozen border stays put)
        const float bTop = g_tbl.oy, bBot = g_tbl.oy + g_tbl.oh - hbar;
        PushClipRect(g_tbl.ox, bTop, g_tbl.viewW, std::max(0.0f, bBot - bTop));
        for (int i = 1; i < g_tbl.count; ++i) {
            int pj = i - 1; while (pj >= 0 && tblHidden(pj)) --pj;
            if (tblHidden(i) || pj < 0) continue;                                  // a hidden column has no border of its own
            const bool noRes = (pj < g_tbl.nSetup && (g_tbl.sFlags[pj] & Col_NoResize)) || (i < g_tbl.nSetup && (g_tbl.sFlags[i] & Col_NoResize));
            const float bx = tblColX(i) - (i > g_tbl.freezeCols || !g_tbl.scrollX ? (g_tbl.scrollX ? sx : 0.0f) : 0.0f);
            if (g_tbl.scrollX && i > g_tbl.freezeCols && (bx < g_tbl.ox + g_tbl.frozenW || bx > g_tbl.ox + g_tbl.viewW)) continue;   // scrolled under the frozen columns or out of the box
            quad(bx - 0.5f, bTop, 1.0f, std::min(bBot - bTop, std::max(0.0f, g_tbl.rowBottom - g_tbl.oy)), pick(Col_TableBorderStrong, g_theme.border));
            const int hid = g_tbl.id ^ (0x7b00 + i);
            const float grab = 4.0f;
            const bool over = !(g_tbl.flags & Table_NoResize) && !noRes && hit(bx - grab, bTop, grab * 2.0f, std::max(0.0f, bBot - bTop));
            if (over) g_hot = hid;
            if (g_active == hid) {
                if (!g_in.mouseDown) g_active = 0;
                else {
                    float dx = g_mouseDX;
                    float a = *g_tbl.w[pj] + dx, b2 = *g_tbl.w[i] - dx;
                    if (g_tbl.scrollX) { if (a >= 20.0f) *g_tbl.w[pj] = a; }   // sideways boxes just grow the column
                    else if (a >= 20.0f && b2 >= 20.0f) { *g_tbl.w[pj] = a; *g_tbl.w[i] = b2; }
                }
            } else if (over && g_pressed) { g_active = hid; g_focusClaimed = true; }
        }
        PopClipRect();
        // the bars
        if (g_tbl.scrollY && maxY > 0.0f) {
            const float bar = tblBar(), in = g_sbSize > 0.0f ? 1.0f : 0.0f;
            const float sbx = g_tbl.ox + g_tbl.ow - bar - in, trackTop = bodyTop, trackH = std::max(8.0f, g_tbl.oy + g_tbl.oh - hbar - bodyTop - in);
            float frac = viewH / (viewH + maxY); if (frac > 1.0f) frac = 1.0f;
            float thumbH = std::min(trackH, std::max(16.0f, frac * trackH));
            const float thumbY = trackTop + (sy / maxY) * (trackH - thumbH);
            const int bid = g_tbl.id ^ 0x7d10;
            const bool over = hit(sbx, thumbY, bar, thumbH);
            if (over) g_hot = bid;
            if (g_active == bid) { if (!g_in.mouseDown) g_active = 0; else if (trackH - thumbH > 0.0f) sy += g_mouseDY * (maxY / (trackH - thumbH)); }
            else if (over && g_pressed) { g_active = bid; g_focusClaimed = true; }
            sy = std::max(0.0f, std::min(sy, maxY));
            scrollbarThumb(sbx, trackTop, trackH, trackTop + (sy / maxY) * (trackH - thumbH), thumbH, g_active == bid, over);
        }
        if (g_tbl.scrollX && maxX > 0.0f) {
            const float sby = g_tbl.oy + g_tbl.oh - kBarThick, trackW = g_tbl.viewW;
            float frac = g_tbl.viewW / g_tbl.contentW; if (frac > 1.0f) frac = 1.0f;
            float thumbW = std::min(trackW, std::max(16.0f, frac * trackW));
            const float thumbX = g_tbl.ox + (sx / maxX) * (trackW - thumbW);
            const int bid = g_tbl.id ^ 0x7d11;
            const bool over = hit(thumbX, sby, thumbW, kBarThick);
            if (over) g_hot = bid;
            if (g_active == bid) { if (!g_in.mouseDown) g_active = 0; else if (trackW - thumbW > 0.0f) sx += g_mouseDX * (maxX / (trackW - thumbW)); }
            else if (over && g_pressed) { g_active = bid; g_focusClaimed = true; }
            sx = std::max(0.0f, std::min(sx, maxX));
            const float tx = g_tbl.ox + (sx / maxX) * (trackW - thumbW);
            if (colorIsSet(Col_ScrollbarBg)) quad(g_tbl.ox, sby, trackW, kBarThick, pick(Col_ScrollbarBg, g_theme.track));
            const bool act = g_active == bid;
            quad(tx + 1.0f, sby + 1.0f, thumbW - 2.0f, kBarThick - 2.0f, act ? pick(Col_ScrollbarGrabActive, g_theme.text) : (over ? pick(Col_ScrollbarGrabHovered, g_theme.text) : pick(Col_ScrollbarGrab, g_theme.accent)));
        }
        *g_tbl.pSX = sx; *g_tbl.pSY = sy;
        if (g_tbl.flags & Table_BordersOuter) tblOuterBorder(g_tbl.ox, g_tbl.oy, g_tbl.ow, g_tbl.oh);
        if (g_sortSpecsFor == g_tbl.id && g_sortSpecsOut && g_tbl.store) g_tbl.store->dirty = g_sortSpecsOut->specsDirty && g_sortSpecsOut->SpecsDirty;
        g_sortSpecsFor = 0; g_sortSpecsOut = nullptr;
        g_lay = g_tbl.saved;                 // the box was placed as one item: the cursor is already below it
        g_lay.pendingSameLine = false;
        g_posOverride = false;
        g_tbl.active = false;
        return;
    }

    // Column borders + draggable resize handles (applied next frame).
    for (int i = 1; i < g_tbl.count; ++i) {
        int pj = i - 1; while (pj >= 0 && tblHidden(pj)) --pj;
        if (tblHidden(i) || pj < 0) continue;                                      // a hidden column has no border of its own
        const bool noRes = (pj < g_tbl.nSetup && (g_tbl.sFlags[pj] & Col_NoResize)) || (i < g_tbl.nSetup && (g_tbl.sFlags[i] & Col_NoResize));
        float bx = tblColX(i);
        quad(bx - 0.5f, top, 1.0f, bottom - top, pick(Col_TableBorderStrong, g_theme.border));
        const int hid = g_tbl.id ^ (0x7b00 + i);
        const float grab = 4.0f;
        const bool over = !(g_tbl.flags & Table_NoResize) && !noRes && hit(bx - grab, top, grab * 2.0f, bottom - top);
        if (over) g_hot = hid;
        if (g_active == hid) {
            if (!g_in.mouseDown) g_active = 0;
            else {
                float dx = g_mouseDX;
                float a = *g_tbl.w[pj] + dx, b = *g_tbl.w[i] - dx;
                if (a >= 20.0f && b >= 20.0f) { *g_tbl.w[pj] = a; *g_tbl.w[i] = b; }
            }
        } else if (over && g_pressed) { g_active = hid; g_focusClaimed = true; }
    }

    if (g_tbl.flags & Table_BordersOuter) tblOuterBorder(g_tbl.x0, top, g_tbl.fullW, bottom - top);
    if (g_sortSpecsFor == g_tbl.id && g_sortSpecsOut && g_tbl.store) g_tbl.store->dirty = g_sortSpecsOut->specsDirty && g_sortSpecsOut->SpecsDirty;
    g_sortSpecsFor = 0; g_sortSpecsOut = nullptr;
    g_lay.ox = g_tbl.x0;
    g_lay.contentW = g_tbl.fullW;
    g_lay.cy = bottom + g_lay.spacingY;
    g_lay.indent = 0.0f;
    g_lay.pendingSameLine = false;
    g_tbl.active = false;
}

} // namespace OkayUI
