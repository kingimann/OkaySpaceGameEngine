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
#include "okay/Graphics/TtfFont.hpp"
// ---- Capture (OkayUICapture.hpp) ------------------------------------------------------------------------------------------------
// The shapes made between BeginCapture and EndCapture are cut out of the frame's batch (main and overlay layer) and handed to the
// caller, the same way a hidden measuring window drops its shapes again (see End): the batch is rewound to where it was. Window
// records made in between are dropped too (they would point at indices that no longer hold their shapes); the windows stay in the
// hit-test list, so the mouse treats them as before.
#include "okay/UI/OkayUICapture.hpp"
namespace OkayUI {
using namespace ui_core;
namespace {
struct CaptureMark {
    int depth = 0;
    long long frame = -1;                 // the frame it began in (a capture left open does not reach into the next frame)
    int nv = 0, ni = 0, nspans = 0;       // the main batch
    bool haveLast = false; DrawSpan last{};   // the span before, which pushSpan may overwrite in place
    bool clipOn = false; PxRect clip{0, 0, 0, 0}; Tex tex = nullptr;
    int nwrec = 0, wrecOpen = -1;
    int onv = 0, oni = 0, novSpans = 0;   // the overlay batch
    bool haveOvLast = false; OverlaySpan ovLast{};
    bool keepOverlay = false;             // Capture_KeepOverlay: the overlay batch is left alone
};
CaptureMark g_cap;
long long captureFrameNow() { return (long long)g_frame; }
} // namespace

void BeginCapture(int flags) {
    if (g_cap.depth > 0 && g_cap.frame != captureFrameNow()) g_cap.depth = 0;   // left open last frame: forget it
    if (g_cap.depth++ > 0) return;
    g_cap.frame = captureFrameNow();
    g_cap.keepOverlay = (flags & Capture_KeepOverlay) != 0;
    g_cap.nv = g_nv; g_cap.ni = g_ni; g_cap.nspans = g_nspans;
    g_cap.haveLast = g_nspans > 0; if (g_cap.haveLast) g_cap.last = g_spans[g_nspans - 1];
    g_cap.clipOn = g_curClipOn; g_cap.clip = g_curClipRect; g_cap.tex = g_curTex;
    g_cap.nwrec = g_nwrec; g_cap.wrecOpen = g_wrecOpen;
    g_cap.onv = g_onv; g_cap.oni = g_oni; g_cap.novSpans = g_novSpans;
    g_cap.haveOvLast = g_novSpans > 0; if (g_cap.haveOvLast) g_cap.ovLast = g_ovSpans[g_novSpans - 1];
}

bool Capturing() { return g_cap.depth > 0 && g_cap.frame == captureFrameNow(); }
namespace ui_core { void captureFrameEnd() { g_cap.depth = 0; } }

void EndCapture(CapturedGeometry& out) {
    out.Clear();
    if (!Capturing()) { g_cap.depth = 0; return; }
    if (--g_cap.depth > 0) return;
    // ---- the main layer: vertices [nv, g_nv), indices [ni, g_ni), split by the spans in force
    const int v0 = g_cap.nv, i0 = g_cap.ni;
    for (int i = v0; i < g_nv; ++i) {
        DrawVertex d; std::memcpy(&d, &g_verts[i], sizeof d);
        out.vertices.push_back(d);
    }
    auto addCmd = [&](int from, int to, bool on, const PxRect& r, Tex t) {
        if (to <= from) return;
        DrawCmd c;
        c.idxOffset = from; c.idxCount = to - from;
        c.clipOn = on; c.clipX = r.x; c.clipY = r.y; c.clipW = r.w; c.clipH = r.h;
        c.texture = (const void*)t;
        out.cmds.push_back(c);
    };
    {
        // the state at i0: the last span starting at or before it (before every span: no clip, the flat texture)
        bool on = false; PxRect r{0, 0, 0, 0}; Tex t = g_defTex;
        int k = 0;
        for (; k < g_nspans && g_spans[k].startIdx <= i0; ++k) { on = g_spans[k].on; r = g_spans[k].rect; t = g_spans[k].tex; }
        int from = i0;
        auto flushTo = [&](int to) {
            const int a = (int)out.indices.size();
            for (int j = from; j + 3 <= to; j += 3) {
                const int x = g_idx[j] - v0, y = g_idx[j + 1] - v0, z = g_idx[j + 2] - v0;
                const int nvCap = g_nv - v0;
                if (x < 0 || y < 0 || z < 0 || x >= nvCap || y >= nvCap || z >= nvCap) continue;
                out.indices.push_back(x); out.indices.push_back(y); out.indices.push_back(z);
            }
            addCmd(a, (int)out.indices.size(), on, r, t);
        };
        for (; k < g_nspans; ++k) {
            const int s = g_spans[k].startIdx;
            if (s >= g_ni) break;
            flushTo(s); from = s;
            on = g_spans[k].on; r = g_spans[k].rect; t = g_spans[k].tex;
        }
        flushTo(g_ni);
    }
    // ---- the overlay layer: after the main shapes, never clipped (as mergeOverlay appends it); left in the frame with KeepOverlay
    out.overlayFrom = (int)out.cmds.size();
    if (!g_cap.keepOverlay) {
        const int ov0 = g_cap.onv, oi0 = g_cap.oni, base = (int)out.vertices.size();
        for (int i = ov0; i < g_onv; ++i) {
            DrawVertex d; std::memcpy(&d, &g_ovVerts[i], sizeof d);
            out.vertices.push_back(d);
        }
        Tex t = g_defTex;
        int k = 0;
        for (; k < g_novSpans && g_ovSpans[k].startIdx <= oi0; ++k) t = g_ovSpans[k].tex;
        const PxRect none{0, 0, 0, 0};
        int from = oi0;
        auto flushTo = [&](int to) {
            const int a = (int)out.indices.size();
            for (int j = from; j + 3 <= to; j += 3) {
                const int x = g_ovIdx[j] - ov0, y = g_ovIdx[j + 1] - ov0, z = g_ovIdx[j + 2] - ov0;
                const int n = g_onv - ov0;
                if (x < 0 || y < 0 || z < 0 || x >= n || y >= n || z >= n) continue;
                out.indices.push_back(base + x); out.indices.push_back(base + y); out.indices.push_back(base + z);
            }
            addCmd(a, (int)out.indices.size(), false, none, t);
        };
        for (; k < g_novSpans; ++k) {
            const int s = g_ovSpans[k].startIdx;
            if (s >= g_oni) break;
            flushTo(s); from = s; t = g_ovSpans[k].tex;
        }
        flushTo(g_oni);
    }
    // ---- rewind the frame to where BeginCapture found it
    g_nv = g_cap.nv; g_ni = g_cap.ni; g_nspans = g_cap.nspans;
    if (g_cap.haveLast && g_nspans > 0) g_spans[g_nspans - 1] = g_cap.last;
    g_curClipOn = g_cap.clipOn; g_curClipRect = g_cap.clip; g_curTex = g_cap.tex;
    g_nwrec = g_cap.nwrec; g_wrecOpen = g_cap.wrecOpen;
    if (!g_cap.keepOverlay) {
        g_onv = g_cap.onv; g_oni = g_cap.oni; g_novSpans = g_cap.novSpans;
        if (g_cap.haveOvLast && g_novSpans > 0) g_ovSpans[g_novSpans - 1] = g_cap.ovLast;
    }
}

bool GetTexturePixels(const void* texture, TexturePixels& out) {
    out = TexturePixels();
    if (!texture) return false;
    if (texture == (const void*)kAtlasTex) {
        if (!g_ttf || !g_ttf->ok || !g_ttf->rgba) return false;
        out.rgba = g_ttf->rgba; out.w = g_ttf->w; out.h = g_ttf->h; out.generation = 1;   // baked once
        return true;
    }
    for (int i = 0; i < g_nfb; ++i)
        if (texture == (const void*)g_fbFonts[i]) {
            const okay::TtfFont* f = g_fbFonts[i];
            if (!f || !f->Valid()) return false;
            const okay::Image& a = f->Atlas();
            if (a.Width() <= 0 || a.Height() <= 0) return false;
            out.rgba = a.Data(); out.w = a.Width(); out.h = a.Height(); out.generation = (unsigned)f->Generation() + 1u;
            return true;
        }
    return false;
}

bool IsHostTexture(const void* texture) {
    if (!texture || texture == (const void*)kAtlasTex) return false;
    for (int i = 0; i < g_nfb; ++i) if (texture == (const void*)g_fbFonts[i]) return false;
    return true;
}

} // namespace OkayUI

// ---- What belongs to one context (OkayUI_ContextState.hpp: each context keeps its own copy of these) ----
namespace OkayUI {
OKAYUI_CONTEXT_STATE(
    OKAYUI_CTX_VAR(g_cap))
} // namespace OkayUI
