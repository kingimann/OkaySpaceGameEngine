// Visual Script app (VisualScriptApp.hpp): the canvas and the Add Block list. The canvas draws the shown graph's comment frames,
// wires (flow wires white, value wires in their type's colour, through their knots), blocks (a header in the block's category
// colour, flow plugs as triangles, value plugs as circles, the setting under the header) and a minimap; it pans (left-drag on
// empty space, middle or right drag, the wheel in trackpad mode), zooms about the pointer, selects (click, Ctrl / Shift+click,
// Shift or Ctrl+drag a box), moves blocks (snapped to the grid when let go; a loose block let go on a wire goes into it), wires a
// plug to a plug (the model's rules refuse what does not fit, in plain words), and a wire dropped on empty space opens the list
// with the blocks that take it. A double-click on a wire opens the list to put a block on it (Shift: a knot there); knots are
// dragged and right-clicked away; comment frames are dragged, resized by their corner and coloured from their menu. Each block
// shows its plain sentence, its badge (Ready, Needs input, Error), its first problem or the part it needs (with Add in the
// editor), and a plug's tip under the pointer. While the game runs: the blocks that ran flash, the path it took is lit, values sit
// beside the plugs and a line under each block says what it did. A right click on empty space opens the list too; on a block,
// its menu (with Connect to...). The rules are the model's (VisualScriptModel.hpp, VsIdeModel.hpp, VisualScriptEasyOps.hpp,
// VsEditorActions.hpp, VsDebugWatch.hpp); this file draws and reads the pointer.
//
// Written from general node-graph editor behaviour and the OkayUI drawing interface; no code, names, icons or look from any other
// editor (rule 19a).
#include "VisualScriptAppImpl.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace vsapp {

namespace {
constexpr float kLayout = 1.4f;   // the canvas draws stored units 1.4 times larger (the editor's window does the same)

UI::Color32 Lift(unsigned c, float t) {   // a colour moved towards white by t
    const int r = (int)(c & 0xff), g = (int)((c >> 8) & 0xff), b = (int)((c >> 16) & 0xff);
    return Rgb(r + (int)((255 - r) * t), g + (int)((255 - g) * t), b + (int)((255 - b) * t));
}
// `t`'s width drawn at `size` pixels: the UI font's own measure scaled (Font::CalcTextSizeA is a fixed-width estimate, too wide for
// the proportional UI font, which made blocks wider than their text)
float TextW(float size, const char* t) { const float base = UI::GetFontSize(); return base > 0.0f ? UI::CalcTextWidth(t) * size / base : 0.0f; }
}  // namespace

float VisualScriptApp::Impl::scale() const { return kLayout * vsed::State().zoom; }
UI::Vec2 VisualScriptApp::Impl::toScreen(float wx, float wy) const {
    const vsed::VsEditState& s = vsed::State();
    return UI::Vec2(canvas.x0 + s.pan.x + wx * scale(), canvas.y0 + s.pan.y + wy * scale());
}
UI::Vec2 VisualScriptApp::Impl::toWorld(float sx, float sy) const {
    const vsed::VsEditState& s = vsed::State();
    return UI::Vec2((sx - canvas.x0 - s.pan.x) / scale(), (sy - canvas.y0 - s.pan.y) / scale());
}

void VisualScriptApp::Impl::frameAll(bool selectionOnly) {
    vsed::VsEditState& s = st();
    float x0 = FLT_MAX, y0 = FLT_MAX, x1 = -FLT_MAX, y1 = -FLT_MAX;
    for (const auto& n : s.g.nodes) {
        if (selectionOnly && !s.sel.count(n.id)) continue;
        const vsed::VsBoxF b = vsed::VsLiveBox(s, n);
        x0 = std::min(x0, b.x); y0 = std::min(y0, b.y); x1 = std::max(x1, b.x + b.w); y1 = std::max(y1, b.y + b.h);
    }
    const float cw = std::max(100.0f, canvas.x1 - canvas.x0), ch = std::max(100.0f, canvas.y1 - canvas.y0);
    if (x0 > x1) { s.zoom = 1.0f; s.pan = vsed::VsPoint(40, 40); return; }
    const float z = std::clamp(std::min((cw - 80.0f) / ((x1 - x0) * kLayout), (ch - 80.0f) / ((y1 - y0) * kLayout)), 0.28f, 1.2f);
    s.zoom = z;
    s.pan = vsed::VsPoint(cw * 0.5f - (x0 + x1) * 0.5f * kLayout * z, ch * 0.5f - (y0 + y1) * 0.5f * kLayout * z);
}

// ---- the canvas --------------------------------------------------------------------------------------------------------------

void VisualScriptApp::Impl::drawCanvas(float X, float Y, float W, float H, bool modal) {
    canvas = {X, Y, X + W, Y + H};
    vsed::VsEditState& s = st();
    vsed::VsEditGraph& g = s.g;
    // Frame All / Frame Selection: fitted with the blocks' drawn sizes, so a graph never seen yet is fitted again once it is drawn
    static const int kRefit = 2;
    if (s.frameAll || s.frameSel) { frameAll(s.frameSel && !s.frameAll); refit = s.frameSel && !s.frameAll ? -kRefit : kRefit; s.frameAll = s.frameSel = false; }
    else if (refit != 0) { frameAll(refit < 0); refit += refit < 0 ? 1 : -1; }
    if (s.focusNode >= 0) {
        if (const vsed::VsEditNode* n = g.find(s.focusNode)) {
            s.zoom = std::max(s.zoom, 0.8f);   // readable
            s.pan = vsed::VsPoint(W * 0.4f - n->x * scale(), H * 0.4f - n->y * scale());
        }
        s.focusNode = -1;
    }
    // the "What is this?" card of the selected block, in the canvas's bottom-right corner (drawn over it after the canvas)
    helpCard = Rect{};
    {
        const vsed::VsEditNode* sn = g.find(s.selected);
        if (sn && vsed::VsDefFor(g, *sn) && !s.missing && W > 360 && H > 220) {
            const bool open = vsed::VsPrefs().helpCardOpen;
            const float below = settings.showMinimap && !g.nodes.empty() ? 110.0f : 0.0f;   // the minimap keeps the corner
            const float cw = std::min(360.0f, std::max(220.0f, W - 32.0f)), chh = open ? std::max(120.0f, (H - below) * 0.6f) : 34.0f;
            helpCard = {X + W - cw - 14.0f, Y + H - below - chh - 14.0f, X + W - 14.0f, Y + H - below - 14.0f};
        }
    }
    wiredThisFrame = false;
    UI::BareNextWindow();
    if (UI::Begin((cfg.idPrefix + "_canvas").c_str(), X, Y, W, H)) {
        const auto& dl = UI::GetWindowDrawList();
        const UI::Input& in = UI::GetFrameInput();
        const UI::Vec2 m = UI::GetMousePos();
        const bool overlay = modal || palOpen || nodeMenu >= 0 || tabMenu >= 0 || commentMenu >= 0 || connectNode >= 0;
        bool overCard = overCanvasCard(m.x, m.y);
        for (const Rect& r : startCards) if (r.in(m.x, m.y)) overCard = true;
        if (startEmpty.in(m.x, m.y) || startNever.in(m.x, m.y)) overCard = true;
        const bool hovered = canvas.in(m.x, m.y) && UI::IsWindowHovered() && !overlay && !overCard;
        if (!overlay && (UI::IsMouseClicked(0) || UI::IsMouseClicked(1) || UI::IsMouseClicked(2))) canvasFocused = canvas.in(m.x, m.y) && UI::IsWindowHovered();
        dl->PushClipRect(UI::Vec2(X, Y), UI::Vec2(X + W, Y + H));
        dl->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + H), kCanvas);

        // ---- zoom and the wheel ----
        if (hovered && in.wheel != 0.0f) {
            const vsed::VsWheelResult wr = vsed::VsWheel(0.0f, in.wheel, in.ctrl, in.shift, settings.scrollPans);
            s.pan.x += wr.panX; s.pan.y += wr.panY;
            if (wr.zoomSteps != 0.0f) {
                const UI::Vec2 before = toWorld(m.x, m.y);
                s.zoom = std::clamp(s.zoom * std::pow(1.12f, wr.zoomSteps), 0.28f, 2.2f);
                s.pan = vsed::VsPoint(m.x - X - before.x * scale(), m.y - Y - before.y * scale());
            }
        }
        const float z = s.zoom, S = scale();
        drawnScale = S;

        // ---- the grid ----
        {
            const float step = 20.0f * S;
            if (step >= 6.0f) {
                int i = (int)std::floor(-s.pan.x / step);
                for (float x = std::fmod(s.pan.x, step); x < W; x += step, ++i)
                    dl->AddLine(UI::Vec2(X + x, Y), UI::Vec2(X + x, Y + H), (i % 5 == 0) ? kGridBold : kGrid);
                int j = (int)std::floor(-s.pan.y / step);
                for (float y = std::fmod(s.pan.y, step); y < H; y += step, ++j)
                    dl->AddLine(UI::Vec2(X, Y + y), UI::Vec2(X + W, Y + y), (j % 5 == 0) ? kGridBold : kGrid);
            }
        }

        // ---- the comment frames: drag the title bar, a corner grip resizes, a right click opens the frame's menu ----
        const float fs = std::clamp(UI::GetFontSize() * z, 7.0f, 30.0f);
        int commentHit = -1, gripHit = -1;
        grips.clear();
        commentRects.clear();
        for (int ci = 0; ci < (int)g.comments.size(); ++ci) {
            const vsed::VsComment& c = g.comments[(std::size_t)ci];
            const UI::Vec2 a = toScreen(c.x, c.y), b = toScreen(c.x + c.w, c.y + c.h);
            static const UI::Color32 tints[] = {Rgb(90, 110, 140, 60), Rgb(90, 150, 100, 60), Rgb(170, 130, 70, 60),
                                                Rgb(160, 80, 90, 60), Rgb(120, 90, 160, 60), Rgb(110, 110, 110, 60)};
            static const UI::Color32 edges[] = {Rgb(90, 110, 140, 200), Rgb(80, 130, 90, 200), Rgb(150, 110, 60, 200),
                                                Rgb(140, 70, 80, 200), Rgb(110, 80, 150, 200), Rgb(100, 100, 106, 200)};
            const std::size_t col = (std::size_t)(((c.color % 6) + 6) % 6);
            dl->AddRectFilled(a, b, tints[col], 6.0f);
            dl->AddRect(a, b, edges[col], 6.0f, 1.0f);
            dl->AddText(UI::GetFont(), fs, UI::Vec2(a.x + 8, a.y + 4), kText, c.text.c_str());
            const float gs = std::max(8.0f, 14.0f * z);
            dl->AddTriangleFilled(UI::Vec2(b.x - gs, b.y - 2), UI::Vec2(b.x - 2, b.y - gs), UI::Vec2(b.x - 2, b.y - 2), edges[col]);
            grips.push_back({ci, UI::Vec2(b.x - gs * 0.4f, b.y - gs * 0.4f)});
            commentRects.push_back({a.x, a.y, b.x, b.y});
            if (m.x >= b.x - gs && m.x < b.x && m.y >= b.y - gs && m.y < b.y) gripHit = ci;
            else if (m.x >= a.x && m.x < b.x && m.y >= a.y && m.y < a.y + fs + 8) commentHit = ci;
        }

        // ---- the blocks' layout (sizes from the Density table, like the editor's window) ----
        const oked::scriptui::Metrics dm = oked::scriptui::Current();
        const float headerH = dm.headerH * z, rowH = dm.rowH * z, pinR = std::max(3.0f, dm.pinR * z), pad = dm.nodePad * z;
        const float badgeW = TextW(fs, "Needs input") + 10.0f * z;
        blockRects.clear();
        plugs.clear();
        sentences.clear(); badges.clear(); liveLines.clear();
        okay::editor::EditorState* e = ed();
        okay::GameObject* owner = e ? vsed::VsGraphOwner(*e, g.path) : nullptr;
        std::map<int, std::pair<std::string, bool>> bands;   // a block's message band: the first problem, or the part it needs (true)
        for (const auto& is : s.issues)
            if (is.node >= 0 && (!bands.count(is.node) || is.severity == 0)) bands[is.node] = {vsed::VsPlainIssue(is.message), false};
        vsed::VsIdeTab& ide = vsed::VsIde(s);
        for (const auto& n : g.nodes) {
            const vsed::VsDef* d = vsed::VsDefFor(g, n);
            if (!d) continue;
            const std::string title = vsed::VsNodeTitle(n, *d);
            float inW = 0, outW = 0;
            for (const char* l : d->dataIn) inW = std::max(inW, TextW(fs, l ? l : ""));
            for (const char* l : d->dataOut) outW = std::max(outW, TextW(fs, l ? l : ""));
            for (const char* l : d->execOut) outW = std::max(outW, TextW(fs, l ? l : ""));
            oked::scriptui::NodeWidthIn nwi;
            nwi.zoom = z; nwi.titleW = TextW(fs, title.c_str()); nwi.inLabelW = inW; nwi.outLabelW = outW; nwi.badgeW = badgeW;
            nwi.hasDataIn = !d->dataIn.empty(); nwi.boxW = 0.0f;
            const float w = oked::scriptui::NodeWidth(dm, nwi);
            if (owner && !bands.count(n.id)) {   // the editor: the object running the graph lacks the part this block needs
                const vsed::VsReqGap gap = vsed::VsRequirementGap(owner, vsed::VsNodeDocFor(n.type), vsed::VsTargetSet(g, n, *d));
                if (gap.missing) bands[n.id] = {"Needs " + gap.addId, true};
            }
            const float bandH = bands.count(n.id) ? (bands[n.id].second && vsed::VsAddComponentById ? rowH * 1.9f : rowH * 0.9f) : 0.0f;
            const int left = (int)d->dataIn.size() + (d->execIn ? 1 : 0), right = (int)(d->dataOut.size() + d->execOut.size());
            const float paramH = d->param ? rowH : 0.0f;
            const float h = headerH + paramH + rowH * (float)std::max(1, std::max(left, right)) + pad + bandH;
            const UI::Vec2 p0 = toScreen(n.x, n.y);
            blockRects[n.id] = {p0.x, p0.y, p0.x + w, p0.y + h};
            ide.sizes[n.id] = {w / S, h / S};   // the IDE's arrange tools use the drawn sizes
            const float top = p0.y + headerH + paramH;
            auto row = [&](int r) { return top + rowH * ((float)r + 0.5f); };
            if (d->execIn) plugs.push_back({n.id, true, false, 0, p0.x, row(vsed::VsPinRow(*d, true, false, 0))});
            for (int i = 0; i < (int)d->dataIn.size(); ++i) plugs.push_back({n.id, false, false, i, p0.x, row(vsed::VsPinRow(*d, false, false, i))});
            for (int i = 0; i < (int)d->execOut.size(); ++i) plugs.push_back({n.id, true, true, i, p0.x + w, row(vsed::VsPinRow(*d, true, true, i))});
            for (int i = 0; i < (int)d->dataOut.size(); ++i) plugs.push_back({n.id, false, true, i, p0.x + w, row(vsed::VsPinRow(*d, false, true, i))});
        }
        auto plugAt = [&](int node, bool exec, bool output, int pin) -> const Plug* {
            for (const Plug& p : plugs) if (p.node == node && p.exec == exec && p.output == output && p.pin == pin) return &p;
            return nullptr;
        };
        auto curve = [&](UI::Vec2 a, UI::Vec2 b, UI::Color32 col, float thick) {
            const float dx = std::max(30.0f * z, std::fabs(b.x - a.x) * 0.5f);
            dl->AddBezierCubic(a, UI::Vec2(a.x + dx, a.y), UI::Vec2(b.x - dx, b.y), b, col, thick);
        };
        auto wireThrough = [&](UI::Vec2 a, UI::Vec2 b, const std::vector<UI::Vec2>& knots, UI::Color32 col, float thick) {
            UI::Vec2 from = a;
            for (const UI::Vec2& k : knots) { curve(from, k, col, thick); from = k; }
            curve(from, b, col, thick);
        };
        auto knotScreen = [&](float wx, float wy) { return toScreen(wx, wy); };
        // every wire with both plugs on screen (dropping a block on a wire, double-clicking one, the value under the pointer)
        std::vector<vsed::VsWireEnds> ends;
        for (int i = 0; i < (int)g.execLinks.size(); ++i) {
            const vsed::VsExecLink& l = g.execLinks[(std::size_t)i];
            const Plug* a = plugAt(l.from, true, true, l.pin); const Plug* b = plugAt(l.to, true, false, 0);
            if (a && b) ends.push_back({true, i, a->x, a->y, b->x, b->y});
        }
        for (int i = 0; i < (int)g.dataLinks.size(); ++i) {
            const vsed::VsDataLink& l = g.dataLinks[(std::size_t)i];
            const Plug* a = plugAt(l.producer, false, true, l.outPin); const Plug* b = plugAt(l.consumer, false, false, l.inPin);
            if (a && b) ends.push_back({false, i, a->x, a->y, b->x, b->y});
        }
        const bool showLive = live && settings.liveValues;
        std::set<int> ranNow;
        if (showLive) for (const auto& kv : s.flash) if (kv.second > 0.0f) ranNow.insert(kv.first);
        const vsed::VsLitWires lit = showLive ? vsed::VsLitPath(g, live, ranNow) : vsed::VsLitWires{};
        const int altWire = (hovered && UI::IsKeyDown(UI::KeyId_Alt) && !s.dragging) ? vsed::VsNearestWire(ends, m.x, m.y, z, 9.0f) : -1;

        // ---- the wires, behind the blocks (the path the run took is lit; Alt over a wire marks the one a click takes away) ----
        for (std::size_t wi = 0; wi < ends.size(); ++wi) {
            const vsed::VsWireEnds& we = ends[wi];
            const UI::Vec2 a(we.ax, we.ay), b(we.bx, we.by);
            if ((int)wi == altWire) { curve(a, b, Rgb(255, 120, 100), 4.0f * std::max(0.7f, z)); continue; }
            if (we.exec) {
                const vsed::VsExecLink& l = g.execLinks[(std::size_t)we.index];
                const bool hot = s.sel.count(l.from) || s.sel.count(l.to);
                const bool ran = showLive && we.index < (int)lit.exec.size() && lit.exec[(std::size_t)we.index];
                wireThrough(a, b, vsed::VsKnotPoints(g, true, l.from, l.pin, l.to, knotScreen),
                            ran ? Rgb(255, 205, 90) : hot ? Rgb(255, 255, 255) : Rgb(205, 208, 218), (hot || ran ? 3.2f : 2.4f) * std::max(0.7f, z));
            } else {
                const vsed::VsDataLink& l = g.dataLinks[(std::size_t)we.index];
                const bool hot = s.sel.count(l.producer) || s.sel.count(l.consumer);
                const bool ran = showLive && we.index < (int)lit.data.size() && lit.data[(std::size_t)we.index];
                const bool ok = vsed::VsWireOk(g, l.consumer, l.inPin, l.producer, l.outPin);
                const unsigned col = vsed::VsPinColor(vsed::VsOutPinType(g, l.producer, l.outPin));
                wireThrough(a, b, vsed::VsKnotPoints(g, false, l.consumer, l.inPin, 0, knotScreen),
                            !ok ? Rgb(255, 80, 70) : (hot || ran) ? Lift(col, 0.35f) : (UI::Color32)col, (hot || ran ? 3.0f : 2.2f) * std::max(0.7f, z));
            }
        }
        // ---- the knots: dots on the wires; drag one to move it, right-click it to take it away ----
        knotsDrawn.clear();
        int knotHit = -1;
        {
            const float kr = 6.0f * std::max(0.8f, z);
            for (int i = 0; i < (int)g.knots.size(); ++i) {
                const UI::Vec2 p = toScreen(g.knots[(std::size_t)i].x, g.knots[(std::size_t)i].y);
                knotsDrawn.push_back(p);
                const bool h = hovered && std::fabs(m.x - p.x) <= kr + 3 && std::fabs(m.y - p.y) <= kr + 3;
                if (h) knotHit = i;
                dl->AddCircleFilled(p, kr, h || ide.knotDrag == i ? Rgb(255, 255, 255) : Rgb(205, 208, 218));
                dl->AddCircle(p, kr, Rgb(30, 32, 40), 0, 1.5f);
            }
            if (knotHit >= 0 && ide.knotDrag < 0) UI::SetTooltip("Knot: drag to move, right-click to remove");
            if (ide.knotDrag >= 0 && ide.knotDrag < (int)g.knots.size()) {
                if (UI::IsMouseDown(0)) {
                    const UI::Vec2 w = toWorld(m.x, m.y);
                    g.knots[(std::size_t)ide.knotDrag].x = w.x; g.knots[(std::size_t)ide.knotDrag].y = w.y; g.dirty = true;
                } else ide.knotDrag = -1;
            } else ide.knotDrag = -1;
        }

        // ---- the blocks ----
        const vsed::VsGraphDiff& diff = vsed::VsIdeDiffNow(s);
        const std::set<int> added = diff.addedIds(), changed = diff.changedIds();
        const vsed::VsDebugView& dv = vsed::DebugView();
        std::set<int> problemNodes;
        for (const auto& is : s.issues) if (is.severity == 0 && is.node >= 0) problemNodes.insert(is.node);
        int addComponentHit = -1;   // a block's Add button under the pointer
        for (const auto& n : g.nodes) {
            const vsed::VsDef* d = vsed::VsDefFor(g, n);
            const auto rit = blockRects.find(n.id);
            if (!d || rit == blockRects.end()) continue;
            const Rect& r = rit->second;
            if (r.x1 < X || r.x0 > X + W || r.y1 < Y || r.y0 > Y + H) continue;   // off screen
            const UI::Vec2 p0(r.x0, r.y0), p1(r.x1, r.y1);
            const float round = dm.round * z;
            const bool sel = s.sel.count(n.id) || s.selected == n.id;
            const float fl = showLive && s.flash.count(n.id) ? s.flash.at(n.id) : 0.0f;
            if (fl > 0.0f)   // it ran just now: a glow that fades
                dl->AddRect(UI::Vec2(p0.x - 4 * z, p0.y - 4 * z), UI::Vec2(p1.x + 4 * z, p1.y + 4 * z), Rgb(255, 200, 80, (int)(220 * fl)), round + 4 * z, 3.0f * z);
            if (live && live->LastErrorNode() == n.id && !live->LastError().empty())   // where the running graph's error happened
                dl->AddRect(UI::Vec2(p0.x - 5 * z, p0.y - 5 * z), UI::Vec2(p1.x + 5 * z, p1.y + 5 * z), Rgb(255, 90, 80), round + 5 * z, 3.0f * z);
            dl->AddRectFilled(UI::Vec2(p0.x + 3, p0.y + 4), UI::Vec2(p1.x + 3, p1.y + 4), Rgb(0, 0, 0, 70), round);
            dl->AddRectFilled(p0, p1, kBody, round);
            dl->AddRectFilled(p0, UI::Vec2(p1.x, p0.y + headerH), vsed::CatColor(d->cat), round);
            const std::string title = vsed::VsNodeTitle(n, *d);
            dl->AddText(UI::GetFont(), fs, UI::Vec2(p0.x + pad, p0.y + 6.0f * z), Rgb(250, 250, 252),
                        Fit(title, std::max(10.0f, (r.x1 - r.x0) - pad * 2 - badgeW)).c_str());
            // the badge (Ready, Needs input, Error) and, under the title, the block's plain sentence or what it needs
            std::string miss;
            const vsed::VsReady ready = vsed::VsBlockReadiness(g, s.issues, n, *d, miss);
            const char* badge = ready == vsed::VsReady::Missing ? "Needs input" : vsed::VsReadyName(ready);
            badges[n.id] = badge;
            {
                const float bw = TextW(fs, badge) + 10.0f * z;
                const UI::Vec2 b0(p1.x - pad * 0.5f - bw, p0.y + 5.0f * z), b1(p1.x - pad * 0.5f, p0.y + 5.0f * z + fs + 4.0f * z);
                dl->AddRectFilled(b0, b1, ready == vsed::VsReady::Error ? Rgb(150, 48, 44) : ready == vsed::VsReady::Missing ? Rgb(140, 96, 32) : Rgb(36, 110, 72), 4.0f * z);
                dl->AddText(UI::GetFont(), fs, UI::Vec2(b0.x + 5.0f * z, b0.y + 2.0f * z), Rgb(255, 248, 240), badge);
                const bool needs = ready == vsed::VsReady::Missing && !miss.empty();
                const std::string line = needs ? miss : vsed::VsPlainLineCached(*d);
                sentences[n.id] = line;
                if (headerH >= fs * 2.2f && !line.empty())
                    dl->AddText(UI::GetFont(), fs * 0.9f, UI::Vec2(p0.x + pad, p0.y + 10.0f * z + fs), needs ? Rgb(255, 196, 140) : Rgb(230, 236, 244, 210),
                                Fit(line, (r.x1 - r.x0) - pad * 2).c_str());
            }
            if (d->param) {
                const std::string arg = (d->param >= 3 && d->param <= 5) ? vsed::VsStepLine(n) : n.arg.empty() ? std::string(d->paramHint ? d->paramHint : "") : n.arg;
                const bool keyArg = vsed::IsKeyNode(n.type) && !n.arg.empty();
                dl->AddRectFilled(UI::Vec2(p0.x + pad, p0.y + headerH + 2), UI::Vec2(p1.x - pad, p0.y + headerH + rowH - 2), Rgb(30, 32, 38), 3.0f);
                dl->AddText(UI::GetFont(), fs, UI::Vec2(p0.x + pad + 4, p0.y + headerH + (rowH - fs) * 0.5f), n.arg.empty() ? kDim : kText,
                            Fit(keyArg ? "Key: " + vsed::VsKeyLabel(n.arg) : arg, (r.x1 - r.x0) - pad * 2 - 8).c_str());
            }
            UI::Color32 outline = sel ? kSelect : kLine;
            float thick = sel ? 2.0f : 1.0f;
            if (problemNodes.count(n.id)) { outline = kError; thick = 2.0f; }
            if (added.count(n.id)) { outline = kAdd; thick = 2.0f; }
            else if (changed.count(n.id)) { outline = kWarn; thick = 2.0f; }
            const bool pausedHere = dv.stopped && dv.key == g.path && dv.node == n.id;
            if (pausedHere) { outline = Rgb(255, 210, 76); thick = 3.0f; }
            dl->AddRect(p0, p1, outline, round, thick);
            if (!g.path.empty() && okay::vs::VsDebugger::Get().HasBreakpoint(g.path, n.id))
                dl->AddCircleFilled(UI::Vec2(p0.x - 2, p0.y - 2), 6.0f * std::max(0.8f, z), Rgb(220, 60, 60));
            if (pausedHere) {   // the game is frozen right before this step
                const float ay = p0.y + headerH * 0.5f, ax = p0.x - 16.0f * z;
                dl->AddTriangleFilled(UI::Vec2(ax - 10 * z, ay - 8 * z), UI::Vec2(ax - 10 * z, ay + 8 * z), UI::Vec2(ax + 2 * z, ay), Rgb(255, 205, 70));
            }
            if (!g.path.empty() && vsed::Bookmarks().has(g.path, n.id))
                dl->AddRectFilled(UI::Vec2(p1.x - 10 * z, p0.y - 4 * z), UI::Vec2(p1.x - 3 * z, p0.y + 8 * z), kAccent);
            // the plugs and their names
            const okay::vs::VsNode* ln = showLive ? vsed::VsLiveNode(live, n.id) : nullptr;
            for (const Plug& p : plugs) {
                if (p.node != n.id) continue;
                const char* label = p.exec ? (p.output ? d->execOut[(std::size_t)p.pin] : "") :
                                             (p.output ? d->dataOut[(std::size_t)p.pin] : d->dataIn[(std::size_t)p.pin]);
                if (p.exec) {
                    const float t = pinR * 1.1f;
                    dl->AddTriangleFilled(UI::Vec2(p.x - t * 0.7f, p.y - t), UI::Vec2(p.x - t * 0.7f, p.y + t), UI::Vec2(p.x + t * 0.9f, p.y), Rgb(235, 236, 240));
                } else {
                    const unsigned col = vsed::VsPinColor(p.output ? vsed::VsOutPinType(g, n.id, p.pin) : vsed::VsInPinType(g, n.id, p.pin));
                    const bool wired = p.output || vsed::VsInputWired(g, n.id, p.pin);
                    if (wired) dl->AddCircleFilled(UI::Vec2(p.x, p.y), pinR, col);
                    else dl->AddCircle(UI::Vec2(p.x, p.y), pinR, col, 0, 2.0f);
                    if (!p.output && !wired && vsed::VsPinNeedsInput(*d, p.pin) && !vsed::VsPinFilled(g, n, p.pin))   // needed and empty: a red ring
                        dl->AddCircle(UI::Vec2(p.x, p.y), pinR + 3.0f * z, Rgb(255, 80, 70), 0, 2.0f * z);
                    if (p.output && ln && p.pin < (int)ln->lastOut.size()) {   // the value it gave last, in a bubble beside the plug
                        const std::string v = vsed::VsLiveValueText(ln->lastOut[(std::size_t)p.pin]);
                        const float vw = TextW(fs, v.c_str());
                        const UI::Vec2 bp(p.x + pinR + 6 * z, p.y - fs * 0.5f - 2 * z);
                        dl->AddRectFilled(bp, UI::Vec2(bp.x + vw + 10 * z, bp.y + fs + 4 * z), Rgb(20, 60, 34, 230), 4 * z);
                        dl->AddText(UI::GetFont(), fs, UI::Vec2(bp.x + 5 * z, bp.y + 2 * z), Rgb(150, 240, 170), v.c_str());
                    }
                }
                if (label && *label) {
                    std::string text = label;
                    if (!p.exec && !p.output && !vsed::VsInputWired(g, n.id, p.pin)) {
                        const auto pd = n.pinDefault.find(p.pin);
                        if (pd != n.pinDefault.end() && !pd->second.empty()) text += " = " + pd->second;
                    }
                    const float tw = TextW(fs, text.c_str());
                    const float tx = p.output ? p.x - pinR * 2 - tw : p.x + pinR * 2;
                    dl->AddText(UI::GetFont(), fs, UI::Vec2(tx, p.y - fs * 0.5f), kText, text.c_str());
                }
            }
            // the message band at the bottom: the block's first problem, or the part it needs with an Add button (the editor)
            const auto band = bands.find(n.id);
            if (band != bands.end()) {
                const bool needsPart = band->second.second;
                const float bandH = needsPart && vsed::VsAddComponentById ? rowH * 1.9f : rowH * 0.9f;
                const float my = p1.y - bandH;
                dl->AddRectFilled(UI::Vec2(p0.x + 2.0f * z, my), UI::Vec2(p1.x - 2.0f * z, p1.y - 2.0f * z),
                                  needsPart || !problemNodes.count(n.id) ? Rgb(120, 96, 40, 220) : Rgb(120, 48, 46, 230), round);
                dl->AddText(UI::GetFont(), fs, UI::Vec2(p0.x + pad, my + 4.0f * z), Rgb(255, 236, 228), Fit(band->second.first, (r.x1 - r.x0) - pad * 2).c_str());
                if (UI::IsMouseHoveringRect(UI::Vec2(p0.x, my), p1) && hovered) UI::SetTooltip("%s", band->second.first.c_str());
                if (needsPart && vsed::VsAddComponentById && owner) {
                    const vsed::VsReqGap gap = vsed::VsRequirementGap(owner, vsed::VsNodeDocFor(n.type), false);
                    const std::string lbl = gap.addLabel.empty() ? std::string("Add") : gap.addLabel;
                    const UI::Vec2 a0(p0.x + pad, my + fs + 8.0f * z), a1(a0.x + TextW(fs, lbl.c_str()) + 16.0f * z, a0.y + fs + 8.0f * z);
                    const bool over = m.x >= a0.x && m.x < a1.x && m.y >= a0.y && m.y < a1.y;
                    dl->AddRectFilled(a0, a1, over ? Rgb(150, 110, 50) : Rgb(110, 80, 36), 4.0f * z);
                    dl->AddText(UI::GetFont(), fs, UI::Vec2(a0.x + 8.0f * z, a0.y + 4.0f * z), kText, lbl.c_str());
                    if (over) addComponentHit = n.id;
                }
            }
            // what the running game did with it, under the block
            if (ln && ln->execCount > 0) {
                const std::string said = vsed::VsBlockSaid(g, live, n, *d);
                liveLines[n.id] = said;
                if (!said.empty()) {
                    const float sw = std::min((r.x1 - r.x0) + 40.0f * z, TextW(fs, said.c_str()) + 16.0f * z);
                    const UI::Vec2 q0(p0.x, p1.y + 6.0f * z), q1(p0.x + sw, p1.y + 12.0f * z + fs);
                    const float fade = 0.55f + 0.45f * fl;
                    dl->AddRectFilled(q0, q1, Rgb(26, 64, 40, (int)(235 * fade)), 6.0f * z);
                    dl->PushClipRect(q0, q1);
                    dl->AddText(UI::GetFont(), fs, UI::Vec2(q0.x + 8.0f * z, q0.y + 3.0f * z), Rgb(190, 245, 205, (int)(255 * fade)), said.c_str());
                    dl->PopClipRect();
                }
            }
        }

        // ---- what is under the pointer ----
        auto plugUnder = [&](float x, float y, float reach) -> const Plug* {
            const Plug* best = nullptr;
            float bestD = reach * reach;
            for (const Plug& p : plugs) {
                const float dd = (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y);
                if (dd < bestD) { bestD = dd; best = &p; }
            }
            return best;
        };
        auto blockUnder = [&](float x, float y) {
            int hit = -1;
            for (const auto& n : g.nodes) { const auto it = blockRects.find(n.id); if (it != blockRects.end() && it->second.in(x, y)) hit = n.id; }   // the last drawn is on top
            return hit;
        };
        const float reach = std::max(10.0f, pinR * 2.6f);
        // a plug's tip: what it means, what to plug in, an example value
        if (hovered && !s.dragging && nodeDrag < 0 && !leftOnEmpty)
            if (const Plug* p = plugUnder(m.x, m.y, reach)) {
                const vsed::VsEditNode* pn = g.find(p->node);
                const vsed::VsDef* pd = pn ? vsed::VsDefFor(g, *pn) : nullptr;
                if (pd) {
                    std::string tip = vsed::VsPinTip(*pd, !p->output, p->exec, p->pin);
                    if (!p->exec && !p->output && vsed::VsPinNeedsInput(*pd, p->pin) && !vsed::VsPinFilled(g, *pn, p->pin))
                        tip += "\nRequired: " + vsed::VsMissingHint(*pd, p->pin);
                    tip += "\nDrag to wire. Alt+click takes its wires away.";
                    UI::SetTooltip("%s", tip.c_str());
                }
            }
        // a value wire under the pointer while the game runs: the value that went through it last
        if (hovered && live && !s.dragging && altWire < 0) {
            const int wi = vsed::VsNearestWire(ends, m.x, m.y, z, 7.0f);
            if (wi >= 0 && !ends[(std::size_t)wi].exec) {
                const std::string v = vsed::VsWireValueText(g, live, ends[(std::size_t)wi].index);
                UI::SetTooltip("%s", v.empty() ? "No value has gone through this wire yet." : ("Value: " + v).c_str());
            }
        }

        // ---- a wire being dragged ----
        if (s.dragging) {
            const Plug* from = plugAt(s.dragNode, s.dragExec, s.dragFromOutput, s.dragPin);
            if (from) {
                const UI::Vec2 a(from->x, from->y);
                if (s.dragFromOutput) curve(a, m, Rgb(255, 255, 255, 170), 2.6f * std::max(0.7f, z));
                else curve(m, a, Rgb(255, 255, 255, 170), 2.6f * std::max(0.7f, z));
                for (const Plug& p : plugs)   // the plugs it could go to, green when it fits
                    if (p.exec == s.dragExec && p.output != s.dragFromOutput && p.node != s.dragNode) {
                        const bool fits = vsed::VsPlugAccepts(g, s.dragExec, s.dragFromOutput, s.dragNode, s.dragPin, p.node, p.pin);
                        dl->AddCircle(UI::Vec2(p.x, p.y), pinR + 3, fits ? Rgb(90, 230, 130) : Rgb(245, 90, 80, 200), 0, 2.0f);
                    }
            }
            if (!UI::IsMouseDown(0)) {
                s.dragging = false;
                const Plug* to = plugUnder(m.x, m.y, std::max(26.0f, 30.0f * z));
                if (to && to->exec == s.dragExec && to->output != s.dragFromOutput && to->node != s.dragNode) {
                    if (vsed::VsTryWire(s, to->node, to->pin)) { notice = "Wired"; wiredThisFrame = true; }
                } else if (to && to->exec != s.dragExec && to->node != s.dragNode) {
                    s.refuseMsg = s.dragExec ? vsed::VsPlainFlowToValue() : vsed::VsPlainValueToFlow();
                    s.refuseTime = 5.0f;
                    s.offerOn = false;
                } else if (blockUnder(m.x, m.y) < 0 && canvas.in(m.x, m.y)) {   // empty space: the list of blocks that take it
                    s.linkPending = true; s.linkExec = s.dragExec; s.linkFromOutput = s.dragFromOutput;
                    s.linkNode = s.dragNode; s.linkPin = s.dragPin;
                    const UI::Vec2 w = toWorld(m.x, m.y);
                    openPalette(m.x, m.y, w.x, w.y);
                }
            }
        }

        // ---- presses ----
        if (hovered && UI::IsMouseClicked(0) && !s.dragging) {
            pressX = m.x; pressY = m.y;
            const Plug* p = plugUnder(m.x, m.y, reach);
            const int b = blockUnder(m.x, m.y);
            if (addComponentHit >= 0 && owner) {   // a block's "Add <part>"
                const vsed::VsReqGap gap = vsed::VsRequirementGap(owner, vsed::VsNodeDocFor(g.find(addComponentHit)->type), false);
                if (gap.missing && vsed::VsAddComponentById(owner, gap.addId.c_str())) note("Added " + gap.addId + " to " + owner->name);
            } else if (knotHit >= 0) {
                ide.knotDrag = knotHit;
            } else if (gripHit >= 0) {   // a comment frame's corner grip (it may lie over a block or a plug the frame holds)
                resizeComment = gripHit; dragLastX = m.x; dragLastY = m.y;
            } else if (altWire >= 0) {   // Alt+click a wire: that wire only goes
                const vsed::VsWireEnds& we = ends[(std::size_t)altWire];
                if (we.exec) g.execLinks.erase(g.execLinks.begin() + we.index); else g.dataLinks.erase(g.dataLinks.begin() + we.index);
                g.dirty = true;
            } else if (p && UI::IsKeyDown(UI::KeyId_Alt)) {   // Alt+click a plug: take its wires away
                g.unlinkPin(p->node, p->exec ? 0 : 1, p->output ? 1 : 0, p->pin);
            } else if (p) {
                s.dragging = true; s.dragExec = p->exec; s.dragFromOutput = p->output; s.dragNode = p->node; s.dragPin = p->pin;
            } else if (b >= 0) {
                if (in.ctrl) { if (s.sel.count(b)) s.sel.erase(b); else s.sel.insert(b); }
                else if (in.shift) s.sel.insert(b);
                else if (!s.sel.count(b)) s.sel = {b};
                s.selected = b;
                nodeDrag = b; dragLastX = m.x; dragLastY = m.y; nodeMoved = false;
                if (UI::IsMouseDoubleClicked(0)) frameAll(true);
            } else if (commentHit >= 0) {
                commentDrag = commentHit; dragLastX = m.x; dragLastY = m.y;
                if (UI::IsMouseDoubleClicked(0)) {
                    s.renameComment = commentHit;
                    std::snprintf(s.renameBuf, sizeof s.renameBuf, "%s", g.comments[(std::size_t)commentHit].text.c_str());
                    renameBox = true;
                }
            } else {
                const int wi = UI::IsMouseDoubleClicked(0) ? vsed::VsNearestWire(ends, m.x, m.y, z, 12.0f) : -1;
                if (wi >= 0 && in.shift) {   // Shift+double-click a wire: a knot where you clicked
                    const UI::Vec2 w = toWorld(m.x, m.y);
                    vsed::VsAddKnot(g, vsed::VsRefOfWire(g, ends[(std::size_t)wi].exec, ends[(std::size_t)wi].index), w.x, w.y);
                } else if (wi >= 0) {   // double-click a wire: the list opens, and the block picked goes onto that wire
                    const vsed::VsWireRef ref = vsed::VsRefOfWire(g, ends[(std::size_t)wi].exec, ends[(std::size_t)wi].index);
                    s.wireInsOn = ref.on; s.wireInsExec = ref.exec; s.wireInsA = ref.a; s.wireInsB = ref.b; s.wireInsC = ref.c;
                    s.linkPending = false;
                    s.sel.clear(); s.selected = -1;
                    const UI::Vec2 w = toWorld(m.x, m.y);
                    if (ref.on) { openPalette(m.x, m.y, w.x, w.y); s.wireInsOn = true; }
                } else {
                    leftOnEmpty = true;
                    boxing = in.shift || in.ctrl;   // Shift or Ctrl + drag: a selection box; a plain drag pans
                    if (boxing) s.boxStart = vsed::VsPoint(m.x, m.y);
                }
            }
        }
        if (hovered && UI::IsMouseClicked(1) && knotHit >= 0) {   // right-click a knot: it goes
            g.knots.erase(g.knots.begin() + knotHit); g.dirty = true; ide.knotDrag = -1;
            rightMax = 1e9f;   // and no menu opens
        }
        if (nodeDrag >= 0) {
            if (UI::IsMouseDown(0)) {
                const float dx = (m.x - dragLastX) / S, dy = (m.y - dragLastY) / S;
                if (dx != 0.0f || dy != 0.0f) {
                    for (auto& n : g.nodes) if (s.sel.count(n.id)) { n.x += dx; n.y += dy; }
                    g.dirty = true;
                    nodeMoved = true;
                }
                dragLastX = m.x; dragLastY = m.y;
            } else {
                const bool moved = std::fabs(m.x - pressX) + std::fabs(m.y - pressY) > 3.0f;
                if (moved && settings.snapGrid)
                    for (auto& n : g.nodes) if (s.sel.count(n.id)) { n.x = vsed::VsSnap(n.x, 20.0f); n.y = vsed::VsSnap(n.y, 20.0f); }
                if (!moved && !in.ctrl && !in.shift) s.sel = {nodeDrag};
                // a block with no wires let go with its middle (or its flow plug) on a wire: the wire goes through it
                if (moved && s.sel.size() == 1 && vsed::VsIsLoose(g, nodeDrag) && blockRects.count(nodeDrag)) {
                    const Rect& rc = blockRects[nodeDrag];
                    std::vector<vsed::VsWireEnds> others;
                    for (const vsed::VsWireEnds& we : ends) {
                        const bool touches = we.exec ? (g.execLinks[(std::size_t)we.index].from == nodeDrag || g.execLinks[(std::size_t)we.index].to == nodeDrag)
                                                     : (g.dataLinks[(std::size_t)we.index].producer == nodeDrag || g.dataLinks[(std::size_t)we.index].consumer == nodeDrag);
                        if (!touches) others.push_back(we);
                    }
                    const int hit = vsed::VsNearestWire(others, (rc.x0 + rc.x1) * 0.5f, (rc.y0 + rc.y1) * 0.5f, z, 26.0f * z);
                    if (hit >= 0 && vsed::VsSplitWire(g, vsed::VsRefOfWire(g, others[(std::size_t)hit].exec, others[(std::size_t)hit].index), nodeDrag)) {
                        s.lastUsed = nodeDrag;
                        note("Put the block on the wire");
                    }
                }
                nodeDrag = -1;
                nodeMoved = false;
            }
        }
        if (resizeComment >= 0) {
            if (UI::IsMouseDown(0) && resizeComment < (int)g.comments.size()) {
                vsed::VsComment& c = g.comments[(std::size_t)resizeComment];
                c.w = std::max(80.0f, c.w + (m.x - dragLastX) / S);
                c.h = std::max(50.0f, c.h + (m.y - dragLastY) / S);
                g.dirty = true;
                dragLastX = m.x; dragLastY = m.y;
            } else resizeComment = -1;
        }
        if (commentDrag >= 0) {
            if (UI::IsMouseDown(0) && commentDrag < (int)g.comments.size()) {
                vsed::VsComment& c = g.comments[(std::size_t)commentDrag];
                const float dx = (m.x - dragLastX) / S, dy = (m.y - dragLastY) / S;
                if (dx != 0.0f || dy != 0.0f) {
                    for (auto& n : g.nodes)   // the blocks inside the frame move with it
                        if (n.x >= c.x && n.x < c.x + c.w && n.y >= c.y && n.y < c.y + c.h) { n.x += dx; n.y += dy; }
                    c.x += dx; c.y += dy; g.dirty = true;
                }
                dragLastX = m.x; dragLastY = m.y;
            } else commentDrag = -1;
        }
        if (leftOnEmpty) {
            if (UI::IsMouseDown(0)) {
                if (boxing) {
                    const UI::Vec2 a(std::min(s.boxStart.x, m.x), std::min(s.boxStart.y, m.y)), b(std::max(s.boxStart.x, m.x), std::max(s.boxStart.y, m.y));
                    dl->AddRectFilled(a, b, Rgb(86, 156, 214, 40));
                    dl->AddRect(a, b, kAccent, 0.0f, 1.0f);
                } else {
                    vsed::VsPanFrame pf;
                    pf.hovered = true; pf.leftDown = true;
                    pf.leftDragging = std::fabs(m.x - pressX) + std::fabs(m.y - pressY) > vsed::kVsClickPixels;
                    pf.dx = m.x - dragLastX; pf.dy = m.y - dragLastY; pf.pressedOnBackground = true;
                    const vsed::VsPanOut po = vsed::VsPanStep(pf);
                    if (po.pans) { s.pan.x += po.dx; s.pan.y += po.dy; }
                }
            } else {
                const bool moved = std::fabs(m.x - pressX) + std::fabs(m.y - pressY) > vsed::kVsClickPixels;
                if (boxing && moved) {
                    const float ax = std::min(s.boxStart.x, m.x), ay = std::min(s.boxStart.y, m.y), bx = std::max(s.boxStart.x, m.x), by = std::max(s.boxStart.y, m.y);
                    if (!in.ctrl && !in.shift) s.sel.clear();
                    for (const auto& kv : blockRects)
                        if (kv.second.x1 >= ax && kv.second.x0 <= bx && kv.second.y1 >= ay && kv.second.y0 <= by) s.sel.insert(kv.first);
                    s.selected = s.sel.empty() ? -1 : *s.sel.begin();
                } else if (!moved) { s.sel.clear(); s.selected = -1; }
                leftOnEmpty = boxing = false;
            }
        }
        // the middle and right buttons pan; a right click (not a drag) opens the list on empty space, a block's menu on a block,
        // a comment frame's menu on its title bar
        if (hovered && (UI::IsMouseClicked(1) || UI::IsMouseClicked(2)) && rightMax < 1e8f) { rightPressX = m.x; rightPressY = m.y; rightMax = 0; }
        if ((in.rightDown || in.middleDown) && canvasFocused && !overlay && rightMax < 1e8f) {
            rightMax = std::max(rightMax, std::fabs(m.x - rightPressX) + std::fabs(m.y - rightPressY));
            vsed::VsPanFrame pf;
            pf.hovered = true; pf.middleDown = in.middleDown; pf.middleDragging = in.middleDown && rightMax > 0.0f;
            pf.rightDown = in.rightDown; pf.rightDragging = in.rightDown && rightMax > vsed::kVsClickPixels; pf.rightMaxDrag = rightMax;
            pf.dx = m.x - dragLastX; pf.dy = m.y - dragLastY;
            const vsed::VsPanOut po = vsed::VsPanStep(pf);
            if (po.pans) { s.pan.x += po.dx; s.pan.y += po.dy; panning = true; }
        }
        if (hovered && UI::IsMouseReleased(1)) {
            if (rightMax <= vsed::kVsClickPixels) {
                const int b = blockUnder(m.x, m.y);
                if (b >= 0) { nodeMenu = b; menuX = m.x; menuY = m.y; if (!s.sel.count(b)) s.sel = {b}; s.selected = b; }
                else if (commentHit >= 0 || gripHit >= 0) { commentMenu = commentHit >= 0 ? commentHit : gripHit; popX = m.x; popY = m.y; }
                else { const UI::Vec2 w = toWorld(m.x, m.y); openPalette(m.x, m.y, w.x, w.y); }
            }
            panning = false;
            if (rightMax >= 1e8f) rightMax = 0;
        }
        if (nodeDrag < 0 && commentDrag < 0 && resizeComment < 0) { dragLastX = m.x; dragLastY = m.y; }

        // ---- drops from the editor: an object from the Hierarchy becomes a Find Object block, or fills a plug's slot ----
        UI::SetCursorScreenPos(UI::Vec2(X, Y));
        UI::InvisibleButton((cfg.idPrefix + "_canvasdrop").c_str(), W, H);
        if (UI::BeginDragDropTarget()) {
            const UI::Vec2 w = toWorld(m.x, m.y);
            if (const UI::DragPayload* p = UI::AcceptDragDropPayload("OBJECT_REF")) {
                const std::string ref((const char*)p->data, (std::size_t)std::max(0, p->size));
                const std::string value = ref.c_str();
                const Plug* pl = plugUnder(m.x, m.y, reach);
                const vsed::VsEditNode* pn = pl ? g.find(pl->node) : nullptr;
                const vsed::VsDef* pd = pn ? vsed::VsDefFor(g, *pn) : nullptr;
                if (pl && pd && !pl->exec && !pl->output &&
                    vsed::VsPinKind(pn->type, pl->pin, vsed::VsInPinType(g, pn->id, pl->pin), pd->dataIn[(std::size_t)pl->pin]) == okay::vs::VsRefKind::Object) {
                    g.unlinkPin(pn->id, 1, 0, pl->pin);
                    g.find(pl->node)->pinDefault[pl->pin] = value;
                    g.dirty = true;
                } else if (!value.empty()) {
                    const int nid = g.add("FindObject", w.x, w.y);
                    if (vsed::VsEditNode* nn = g.find(nid)) nn->arg = value;
                    s.sel = {nid}; s.selected = nid; g.dirty = true;
                }
            }
            if (const UI::DragPayload* p = UI::AcceptDragDropPayload("VS_INSPECTOR")) {   // a property dragged from the Inspector
                const std::string raw((const char*)p->data, (std::size_t)std::max(0, p->size));
                vsed::VsPlaceInspectorDrop(g, s, w.x, w.y, raw.c_str());
            }
            UI::EndDragDropTarget();
        }

        // ---- the minimap (the help card sits above it) ----
        if (settings.showMinimap && !g.nodes.empty() && W > 320 && H > 220) {
            float x0 = FLT_MAX, y0 = FLT_MAX, x1 = -FLT_MAX, y1 = -FLT_MAX;
            for (const auto& kv : blockRects) {
                const UI::Vec2 a = toWorld(kv.second.x0, kv.second.y0), b = toWorld(kv.second.x1, kv.second.y1);
                x0 = std::min(x0, a.x); y0 = std::min(y0, a.y); x1 = std::max(x1, b.x); y1 = std::max(y1, b.y);
            }
            const UI::Vec2 va = toWorld(X, Y), vb = toWorld(X + W, Y + H);
            x0 = std::min(x0, va.x); y0 = std::min(y0, va.y); x1 = std::max(x1, vb.x); y1 = std::max(y1, vb.y);
            const float mw = 160.0f, mh = 100.0f;
            const float mx0 = X + W - mw - 10, my0 = Y + H - mh - 10;
            const float k = std::min(mw / std::max(1.0f, x1 - x0), mh / std::max(1.0f, y1 - y0));
            dl->AddRectFilled(UI::Vec2(mx0, my0), UI::Vec2(mx0 + mw, my0 + mh), Rgb(18, 19, 23, 220), 4.0f);
            auto mp = [&](float wx, float wy) { return UI::Vec2(mx0 + (wx - x0) * k, my0 + (wy - y0) * k); };
            for (const auto& n : g.nodes) {
                const auto it = blockRects.find(n.id);
                if (it == blockRects.end()) continue;
                const UI::Vec2 a = toWorld(it->second.x0, it->second.y0), b = toWorld(it->second.x1, it->second.y1);
                const vsed::VsDef* d = vsed::VsDefFor(g, n);
                dl->AddRectFilled(mp(a.x, a.y), mp(b.x, b.y), d ? (UI::Color32)vsed::CatColor(d->cat) : kDim);
            }
            dl->AddRect(mp(va.x, va.y), mp(vb.x, vb.y), kText, 0.0f, 1.0f);
            if (hovered && UI::IsMouseClicked(0) && m.x >= mx0 && m.x < mx0 + mw && m.y >= my0 && m.y < my0 + mh) {
                leftOnEmpty = false;   // a click on the minimap jumps there (it is not a pan)
                const float wx = x0 + (m.x - mx0) / k, wy = y0 + (m.y - my0) / k;
                s.pan = vsed::VsPoint(W * 0.5f - wx * S, H * 0.5f - wy * S);
            }
        }

        // ---- the words on the canvas ----
        bool hasEvent = false;
        for (const auto& n : g.nodes) if (!vsed::VsEntryName(n.type, n.arg).empty() || vsed::VsStartsOnItsOwn(n.type)) { hasEvent = true; break; }
        s.needsEvent = !g.nodes.empty() && !hasEvent;
        if (s.missing) {
            startCards.clear();
            dl->AddText(UI::Vec2(X + 24, Y + 24), kError, "This graph file could not be opened.");
            dl->AddText(UI::Vec2(X + 24, Y + 48), kText, g.path.c_str());
        } else if (vsed::VsStartVisible((int)g.nodes.size(), vsed::VsPrefs().hideStart, s.startedEmpty, s.missing)) {
            drawStartCards(X, Y, W, H, overlay);
        } else {
            startCards.clear(); startEmpty = startNever = Rect{};
            if (g.nodes.empty()) {
                const char* a = "An empty graph. Right-click or press Space to add a block, or Graph > Pick a Recipe.";
                const char* b = "Start with an event (On Start, On Update, On Key), then wire steps after it.";
                dl->AddText(UI::Vec2(X + std::max(16.0f, (W - UI::CalcTextWidth(a)) * 0.5f), Y + H * 0.4f), kText, a);
                dl->AddText(UI::Vec2(X + std::max(16.0f, (W - UI::CalcTextWidth(b)) * 0.5f), Y + H * 0.4f + 26), kDim, b);
            } else if (s.needsEvent) {
                const std::string msg = "Nothing runs yet. Add a block that starts with On (On Start, On Key Pressed) or a Quick block, and plug the first step into it.";
                const UI::Vec2 b0(X + 18.0f, Y + 16.0f), b1(X + 18.0f + std::min(W - 40.0f, UI::CalcTextWidth(msg.c_str()) + 24.0f), Y + 16.0f + UI::GetTextLineHeight() + 16.0f);
                dl->AddRectFilled(b0, b1, Rgb(64, 48, 24, 230), 8.0f);
                dl->AddText(UI::Vec2(b0.x + 12.0f, b0.y + 8.0f), Rgb(245, 214, 150), Fit(msg, b1.x - b0.x - 24.0f).c_str());
            }
        }
        if (!s.refHint.empty()) dl->AddText(UI::Vec2(X + 8, Y + 8 + (s.needsEvent ? 40.0f : 0.0f)), Rgb(255, 140, 115), s.refHint.c_str());
        if (live && settings.liveValues) {   // the running object, and its error (the block where it happened is outlined)
            const std::string lv = "LIVE: " + liveName;
            dl->AddCircleFilled(UI::Vec2(X + W - UI::CalcTextWidth(lv.c_str()) - 26, Y + 12 + UI::GetTextLineHeight() * 0.5f), 4.0f, Rgb(115, 230, 140));
            dl->AddText(UI::Vec2(X + W - UI::CalcTextWidth(lv.c_str()) - 16, Y + 12), Rgb(115, 230, 140), lv.c_str());
            if (!live->LastError().empty()) {
                std::string er = "Error: " + live->LastError();
                if (live->ErrorCount() > 1) er += "  (x" + std::to_string(live->ErrorCount()) + ")";
                dl->AddText(UI::GetFont(), UI::GetFontSize(), UI::Vec2(X + 18, Y + H - 64 - UI::GetTextLineHeight() * 2), Rgb(255, 120, 110), er.c_str(), nullptr,
                            std::max(200.0f, W - 36.0f));
            }
        }
        if (s.refuseTime > 0 && !s.refuseMsg.empty()) {
            const float bw = std::min(560.0f, W - 32.0f);
            const UI::Vec2 a(X + 16, Y + H - 120), b(X + 16 + bw, Y + H - 66);
            dl->AddRectFilled(a, b, Rgb(64, 22, 22, 245), 4.0f);
            dl->AddRect(a, b, Rgb(220, 90, 80), 4.0f, 1.0f);
            dl->AddText(UI::Vec2(a.x + 10, a.y + 8), kText, Fit(s.refuseMsg, bw - (s.offerOn ? 150.0f : 20.0f)).c_str());
            if (s.offerOn && !s.offerType.empty()) {
                const UI::Vec2 f0(b.x - 136, a.y + 22), f1(b.x - 10, a.y + 46);
                const bool over = m.x >= f0.x && m.x < f1.x && m.y >= f0.y && m.y < f1.y;
                dl->AddRectFilled(f0, f1, over ? kHover : kTabOn, 4.0f);
                dl->AddText(UI::Vec2(f0.x + 10, f0.y + 4), kText, vsed::VsPlainFixIt());
                if (over && UI::IsMouseClicked(0) && !overlay) { vsed::VsAcceptConvertOffer(s); leftOnEmpty = false; }
            }
        }
        drawHintBar(X, Y, W, H);
        dl->PopClipRect();
    }
    UI::End();
}

// ---- a block's right-click menu ---------------------------------------------------------------------------------------------------

void VisualScriptApp::Impl::drawNodeMenu() {
    vsed::VsEditState& s = st();
    const vsed::VsEditNode* n = s.g.find(nodeMenu);
    if (!n) { nodeMenu = -1; return; }
    std::vector<std::pair<std::string, std::string>> items;   // title, command ("" = a separator)
    const std::vector<vsed::VsQuickFix> fixes = vsed::VsQuickFixesFor(s.g, s.issues, n->id);
    for (std::size_t i = 0; i < fixes.size(); ++i) items.push_back({"Fix: " + fixes[i].title, "fix:" + std::to_string(i)});
    if (!fixes.empty()) items.push_back({"", ""});
    items.push_back({"Connect to...", "connect"});
    items.push_back({"Duplicate", "dup"});
    items.push_back({"Copy", "copy"});
    items.push_back({"Delete", "delete"});
    items.push_back({"Break All Wires", "unlink"});
    items.push_back({"", ""});
    const bool marked = !s.g.path.empty() && vsed::Bookmarks().has(s.g.path, n->id);
    items.push_back({marked ? "Remove Bookmark" : "Add Bookmark", "vs.toggleBookmark"});
    const bool bp = !s.g.path.empty() && okay::vs::VsDebugger::Get().HasBreakpoint(s.g.path, n->id);
    items.push_back({bp ? "Remove Breakpoint" : "Add Breakpoint", "bp"});
    items.push_back({"Find References", "vs.findReferences"});
    items.push_back({"Watch Values", "vs.watchValues"});
    items.push_back({"Straighten Wire", "vs.straighten"});
    if (vsed::VsCanCollapse(s.g, s.sel.empty() ? std::set<int>{n->id} : s.sel, nullptr)) items.push_back({"Collapse to Function", "vs.collapseToFunction"});
    items.push_back({"Comment Around Selection", "comment"});
    std::vector<std::string> rows;
    for (const auto& it : items) rows.push_back(it.first);
    bool close = false;
    const int picked = menuBox("_nodemenu", menuX, menuY, rows, close);
    const int id = nodeMenu;
    if (close) nodeMenu = -1;
    if (picked < 0) return;
    const std::string pick = items[(std::size_t)picked].second;
    if (pick.rfind("fix:", 0) == 0) { const std::size_t i = (std::size_t)std::atoi(pick.c_str() + 4); if (i < fixes.size()) vsed::VsApplyQuickFix(s.g, fixes[i]); }
    else if (pick == "connect") { connectNode = id; popX = menuX; popY = menuY; }
    else if (pick == "dup") { const std::string t = vsed::VsSelectionText(s.g, s.sel); s.sel = vsed::VsPasteText(s.g, t, n->x + 40.0f, n->y + 40.0f); }
    else if (pick == "copy") UI::SetClipboardText(vsed::VsSelectionText(s.g, s.sel).c_str());
    else if (pick == "delete") { vsed::VsDeleteBlocks(s.g, s.sel, false); s.sel.clear(); s.selected = -1; }
    else if (pick == "unlink") {
        if (const vsed::VsDef* d = vsed::VsDefFor(s.g, *n)) {
            if (d->execIn) s.g.unlinkPin(id, 0, 0, 0);
            for (int i = 0; i < (int)d->execOut.size(); ++i) s.g.unlinkPin(id, 0, 1, i);
            for (int i = 0; i < (int)d->dataIn.size(); ++i) s.g.unlinkPin(id, 1, 0, i);
            for (int i = 0; i < (int)d->dataOut.size(); ++i) s.g.unlinkPin(id, 1, 1, i);
        }
    } else if (pick == "bp") {
        if (s.g.path.empty()) note("Save the graph to a file first: breakpoints are kept per graph file.", 1);
        else if (!vsed::VsCanBreakAt(*n)) note("Breakpoints go on steps (blocks the flow runs through). A value block runs when a step reads it.", 1);
        else okay::vs::VsDebugger::Get().ToggleBreakpoint(s.g.path, id);
    } else if (pick == "comment") { vsed::VsCommentAround(s, 0, 0); renameBox = true; }
    else {
        if (!vsed::VsRunCommand(s, pick, root())) note("Nothing to do here", 1);
        if (pick == "vs.findReferences" || pick == "vs.watchValues") settings.panel = 2;
    }
}

// ---- the Add Block list ---------------------------------------------------------------------------------------------------

void VisualScriptApp::Impl::openPalette(float sx, float sy, float wx, float wy) {
    vsed::VsEditState& s = st();
    s.paletteWorld = vsed::VsPoint(wx, wy);
    palOpen = true; palFocus = true; palBuf[0] = 0; palSel = 0;
    palX = sx; palY = sy;
    palShowAll = false; palSearchAll = false;
    if (s.linkPending) palSide = -3;   // a wire dropped on empty space: the whole list, the blocks that fit it
    palBuiltFor = "\x01";   // made again
    buildPalette();
}

// Every row the list can show: the blocks (not hidden, their module on) and, when the editor gave its step list, the Actions steps
// as rows of their own. Made once; again when the registry or a step list changes size.
void VisualScriptApp::Impl::buildPaletteAll() {
    const auto* act = vsed::StepCatalog ? vsed::StepCatalog(false) : nullptr;
    const auto* cnd = vsed::StepCatalog ? vsed::StepCatalog(true) : nullptr;
    int modOn = 0;
    for (const auto& inf : okay::ModuleRegistry::Get().All()) if (inf.enabled) ++modOn;
    const std::size_t key = vsed::Registry().size() * 1000003u + (act ? act->size() : 0) * 1009u + (cnd ? cnd->size() : 0) * 17u + (std::size_t)modOn;
    if (key == palAllFor) return;
    palAllFor = key;
    palAll.clear();
    for (const vsed::VsDef& d : vsed::Registry()) {
        if (!d.type || okay::vs::VsNodeSuppressed(d.type) || vsed::VsPaletteHidden(d)) continue;
        if (std::strcmp(d.type, "Action") == 0 || std::strcmp(d.type, "Condition") == 0) continue;   // their steps are rows of their own
        PalRow r;
        r.def = &d; r.title = d.title ? d.title : d.type;
        const char* help = vsed::VsNodeHelp(d.type);
        r.help = help ? help : "";
        const okay::vs::VsApiEntry* api = okay::vs::VsApiFind(d.type);
        const vsed::VsPlace place = api ? vsed::VsPlaceForApi(api->apiType.c_str(), api->member.c_str()) : vsed::VsPlaceForNode(d.type);
        r.place = place.category ? place.category : "";
        r.catIndex = vsed::VsPaletteCategoryIndex(place.category);
        r.browse = place.browse;
        r.key = vsed::VsPaletteNodeKey(d.type);
        r.keywords = vsed::VsPaletteKeywords(d);
        r.level = d.level;
        palAll.push_back(std::move(r));
    }
    for (int pass = 0; pass < 2; ++pass) {
        const bool cond = pass == 1;
        const vsed::VsDef* sd = vsed::FindDef(cond ? "Condition" : "Action");
        const auto* cat = cond ? cnd : act;
        if (!sd || !cat) continue;
        for (const vsed::VsStepInfo& si : *cat) {
            if (si.listOnly || vsed::VsPaletteStepIsDuplicate(si.label)) continue;   // the hand-written block is the one row
            PalRow r;
            r.def = sd; r.title = si.label; r.help = si.desc;
            const vsed::VsPlace place = vsed::VsPlaceForStep(si.op.c_str(), si.group.c_str());
            r.place = place.category ? place.category : "";
            r.catIndex = vsed::VsPaletteCategoryIndex(place.category);
            r.browse = place.browse;
            r.key = vsed::VsPaletteStepKey(cond, si.op.c_str());
            r.keywords = vsed::VsPaletteStepKeywords(si, cond);
            r.level = vsed::VsLevelForStep(si.op.c_str(), si.group.c_str());
            r.arg = okay::ActionList::EncodeItem({si.op, {}});
            palAll.push_back(std::move(r));
        }
    }
}

void VisualScriptApp::Impl::buildPalette() {
    buildPaletteAll();
    vsed::VsEditState& s = st();
    vsed::VsPaletteStore& store = vsed::VsStore();
    okay::vs::PinType dragged = okay::vs::PinType::Any;
    if (s.linkPending && !s.linkExec)
        dragged = s.linkFromOutput ? vsed::VsOutPinType(s.g, s.linkNode, s.linkPin) : vsed::VsInPinType(s.g, s.linkNode, s.linkPin);
    std::string key = std::string(palBuf) + "|" + (s.linkPending ? "w" + std::to_string(s.linkExec) + std::to_string(s.linkFromOutput) + std::to_string((int)dragged) : "") +
                      "|" + std::to_string(palSide) + "|" + std::to_string(store.filter) + std::to_string(store.allNodes) + std::to_string(palShowAll) +
                      std::to_string(palSearchAll) + "|" + std::to_string(store.fav.size()) + "|" + std::to_string(store.recent.size()) + "|" + std::to_string(palAll.size());
    for (const std::string& r : store.recent) key += r;
    if (key == palBuiltFor) return;
    palBuiltFor = key;
    palRows.clear();
    palHidden = 0;
    const std::string q = palBuf;
    const vsed::VsLevelFilter level = (vsed::VsLevelFilter)store.filter;
    auto isFav = [&](const std::string& k) { return std::find(store.fav.begin(), store.fav.end(), k) != store.fav.end(); };
    auto recentRank = [&](const std::string& k) {
        const auto it = std::find(store.recent.begin(), store.recent.end(), k);
        return it == store.recent.end() ? 10000 : (int)(it - store.recent.begin());
    };
    for (const PalRow& base : palAll) {
        const vsed::VsDef& d = *base.def;
        int fit = 0;
        bool fits = true;
        if (s.linkPending) {
            fits = vsed::VsCompatible(d, s.linkExec, s.linkFromOutput, dragged);
            if (!fits && !palShowAll) continue;   // only the blocks that take the wire (unless "Show all")
            fit = fits ? std::max(0, vsed::VsPinFitRank(d, s.linkExec, s.linkFromOutput, dragged)) : 99;
        }
        if (!vsed::VsLevelPasses(base.level, level)) continue;
        const bool hiddenNow = vsed::VsHiddenWhileBrowsing(base.arg.empty() ? d.type : "", store.filter, store.allNodes);
        int score = vsed::VsMatchScore(q, base.title, base.keywords, base.help);
        if (score <= 0) continue;
        if (q.empty()) {   // browsing: the side's group
            if (!base.browse || hiddenNow) continue;
            if (palSide == -4 && !(base.arg.empty() && vsed::VsIsStarter(d.type))) continue;
            if (palSide == -5 && !(base.arg.empty() && okay::vs::FindQuickBlock(d.type))) continue;
            if (palSide == -1 && !isFav(base.key)) continue;
            if (palSide == -2 && recentRank(base.key) == 10000) continue;
            if (palSide >= 0 && base.catIndex != palSide) continue;
        } else if (hiddenNow && !palSearchAll) { ++palHidden; continue; }   // found, but hidden: offered with "Search all blocks"
        if (base.arg.empty()) score = vsed::VsStarterBoost(score, vsed::VsIsStarter(d.type) || okay::vs::FindQuickBlock(d.type), std::strncmp(d.type, "Fn.", 3) == 0);
        if (s.linkPending && !q.empty()) score += vsed::VsWireSearchBoost(fit);
        if (q.empty() && !s.linkPending && palSide == -3 && !(base.arg.empty() && vsed::VsIsStarter(d.type))) score -= 200;   // the starter blocks first
        PalRow r = base;
        r.score = score; r.fit = fit;
        palRows.push_back(std::move(r));
    }
    if (!q.empty())
        std::stable_sort(palRows.begin(), palRows.end(), [&](const PalRow& a, const PalRow& b) {
            return vsed::VsSearchRanksBefore(a.score, a.level, recentRank(a.key), b.score, b.level, recentRank(b.key));
        });
    else if (palSide == -2)
        std::stable_sort(palRows.begin(), palRows.end(), [&](const PalRow& a, const PalRow& b) { return recentRank(a.key) < recentRank(b.key); });
    else if (s.linkPending)
        std::stable_sort(palRows.begin(), palRows.end(), [](const PalRow& a, const PalRow& b) {
            if (a.fit != b.fit) return a.fit < b.fit;   // the dragged type exactly first
            if (a.score != b.score) return a.score > b.score;
            return a.title < b.title;
        });
    else
        std::stable_sort(palRows.begin(), palRows.end(), [](const PalRow& a, const PalRow& b) {
            if (a.score != b.score) return a.score > b.score;
            if (a.catIndex != b.catIndex) return a.catIndex < b.catIndex;
            return a.title < b.title;
        });
    if (palRows.empty() && !q.empty()) {   // nothing matched what was typed: the nearest few names instead of a blank list
        std::vector<const PalRow*> pool;
        std::vector<std::string> titles;
        for (const PalRow& base : palAll) {
            if (!base.browse || !vsed::VsLevelPasses(base.level, level)) continue;
            if (s.linkPending && !palShowAll && !vsed::VsCompatible(*base.def, s.linkExec, s.linkFromOutput, dragged)) continue;
            std::string t = base.title;
            for (char& c : t) c = (char)std::tolower((unsigned char)c);
            pool.push_back(&base); titles.push_back(t);
        }
        std::string needle = q;
        for (char& c : needle) c = (char)std::tolower((unsigned char)c);
        for (int i : vsed::searchnav::NearestIndices(titles, needle, 5)) { PalRow r = *pool[(std::size_t)i]; r.score = 0; palRows.push_back(std::move(r)); }
        palSuggest = !palRows.empty();
    } else palSuggest = false;
    if (palRows.size() > 400) palRows.resize(400);
    palSel = std::clamp(palSel, 0, std::max(0, (int)palRows.size() - 1));
}

void VisualScriptApp::Impl::toggleFavourite(std::size_t index) {
    if (index >= palRows.size()) return;
    vsed::VsPaletteStore& store = vsed::VsStore();
    const std::string& k = palRows[index].key;
    const auto it = std::find(store.fav.begin(), store.fav.end(), k);
    if (it == store.fav.end()) store.fav.push_back(k); else store.fav.erase(it);
    store.save();
    palBuiltFor = "\x01";
}

bool VisualScriptApp::Impl::pickPalette(std::size_t index) {
    if (index >= palRows.size() || !palRows[index].def) return false;
    vsed::VsEditState& s = st();
    {   // a new Quick block fills itself in from the scene: the selected object, its neighbours, the last key (the editor)
        okay::vs::QbContext& qc = vsed::VsQuickContext();
        qc.objectNames.clear();
        qc.selfName.clear();
        if (okay::editor::EditorState* e = ed()) {
            for (const auto& up : e->scene().Objects()) if (up) qc.objectNames.push_back(up->name);
            qc.selfName = e->selected() ? e->selected()->name : std::string();
        }
    }
    const PalRow row = palRows[index];
    const int id = vsed::VsAddFromPalette(s, *row.def, row.arg);
    palOpen = false;
    s.linkPending = false;
    canvasFocused = true;
    if (id < 0) { note(s.refuseMsg, 1); return false; }
    vsed::VsPaletteStore& store = vsed::VsStore();
    vsed::searchnav::RecentPush(store.recent, row.key);   // newest first, no repeats, the last 8
    store.save();
    note("Added " + row.title);
    return true;
}

void VisualScriptApp::Impl::drawPalette(float X, float Y, float W, float H) {
    const float pw = std::min(480.0f, W - 20.0f), ph = std::min(440.0f, H - 20.0f);
    const float x = std::clamp(palX, X + 4, X + W - pw - 4), y = std::clamp(palY, Y + 4, Y + H - ph - 4);
    UI::BareNextWindow();
    if (UI::Begin((cfg.idPrefix + "_palette").c_str(), x, y, pw, ph)) {
        const auto& dl = UI::GetWindowDrawList();
        dl->AddRectFilled(UI::Vec2(x, y), UI::Vec2(x + pw, y + ph), kPanel, 6.0f);
        dl->AddRect(UI::Vec2(x, y), UI::Vec2(x + pw, y + ph), kLine, 6.0f, 1.0f);
        vsed::VsEditState& s = st();
        vsed::VsPaletteStore& store = vsed::VsStore();
        UI::SetCursorScreenPos(UI::Vec2(x + 10, y + 8));
        UI::TextUnformatted(s.linkPending ? (palShowAll ? "Add a block (all of them)" : "Add a block that takes this wire") : "Add a block");
        if (s.linkPending) {   // the switch to see everything
            UI::SameLine();
            if (UI::SmallButton(palShowAll ? "Only what fits##vsapp_pfit" : "Show all##vsapp_pfit")) { palShowAll = !palShowAll; palSel = 0; }
        }
        UI::SetCursorScreenPos(UI::Vec2(x + 10, UI::GetCursorScreenPos().y + 2));
        UI::SetNextItemWidth(pw - 20);
        if (palFocus) { UI::SetKeyboardFocusHere(); palFocus = false; }
        const std::string before = palBuf;
        const bool enter = UI::InputTextWithHint("##vsapp_palq", "search: move, wait, key, add...", palBuf, (int)sizeof palBuf, UI::InputText_EnterReturnsTrue);
        if (before != palBuf) { palSel = 0; palSearchAll = false; }
        // the level filter and, with nothing typed, which group the list shows
        UI::SetCursorScreenPos(UI::Vec2(x + 10, UI::GetCursorScreenPos().y + 2));
        for (int i = 0; i < 4; ++i) {
            if (i) UI::SameLine();
            const std::string label = std::string(store.filter == i ? "> " : "") + vsed::VsLevelFilterName((vsed::VsLevelFilter)i) + "##vsapp_plv";
            if (UI::SmallButton(label.c_str())) { store.filter = i; store.save(); palSel = 0; }
        }
        UI::SameLine();
        {
            std::string sideName = palSide == -4 ? "Starter" : palSide == -5 ? "Quick blocks" : palSide == -2 ? "Recent" : palSide == -1 ? "Favourites"
                                 : palSide == -3 ? "Everything" : (palSide < vsed::VsPaletteCategoryCount() ? vsed::kPaletteCategories[palSide].name : "Everything");
            UI::SetNextItemWidth(std::max(80.0f, pw - UI::GetCursorScreenPos().x + x - 14));
            if (UI::BeginCombo("##vsapp_pside", sideName.c_str())) {
                const std::pair<int, const char*> fixed[] = {{-3, "Everything"}, {-4, "Starter"}, {-5, "Quick blocks"}, {-2, "Recent"}, {-1, "Favourites"}};
                for (const auto& f : fixed) if (UI::Selectable((std::string(f.second) + "##ps").c_str(), palSide == f.first)) { palSide = f.first; palSel = 0; UI::CloseCurrentPopup(); }
                for (int c = 0; c < vsed::VsPaletteCategoryCount(); ++c)
                    if (UI::Selectable((std::string(vsed::kPaletteCategories[c].name) + "##pc" + std::to_string(c)).c_str(), palSide == c)) { palSide = c; palSel = 0; UI::CloseCurrentPopup(); }
                UI::EndCombo();
            }
        }
        buildPalette();
        if (palSuggest) {
            UI::SetCursorScreenPos(UI::Vec2(x + 10, UI::GetCursorScreenPos().y + 2));
            UI::TextDisabled("Nothing has that name. Did you mean:");
        }
        const float listY = UI::GetCursorScreenPos().y + 4, helpH = 46.0f;
        const float rowH = UI::GetTextLineHeight() + 6;
        const int fitRows = std::max(1, (int)((y + ph - helpH - listY) / rowH));
        {   // Up / Down / PageUp / PageDown move the highlight; Home / End too while nothing is typed (else they edit the text)
            namespace sn = vsed::searchnav;
            const int count = (int)palRows.size();
            if (UI::IsKeyPressed(UI::KeyId_Down)) palSel = sn::MoveSelection(palSel, count, sn::Move::Down, fitRows);
            if (UI::IsKeyPressed(UI::KeyId_Up)) palSel = sn::MoveSelection(palSel, count, sn::Move::Up, fitRows);
            if (UI::IsKeyPressed(UI::KeyId_PageDown)) palSel = sn::MoveSelection(palSel, count, sn::Move::PageDown, fitRows);
            if (UI::IsKeyPressed(UI::KeyId_PageUp)) palSel = sn::MoveSelection(palSel, count, sn::Move::PageUp, fitRows);
            if (!palBuf[0] && UI::IsKeyPressed(UI::KeyId_Home)) palSel = sn::MoveSelection(palSel, count, sn::Move::Home, fitRows);
            if (!palBuf[0] && UI::IsKeyPressed(UI::KeyId_End)) palSel = sn::MoveSelection(palSel, count, sn::Move::End, fitRows);
            palSel = std::clamp(palSel, 0, std::max(0, count - 1));
        }
        const int first = std::max(0, std::min(palSel - fitRows + 1, (int)palRows.size() - fitRows));
        const UI::Vec2 m = UI::GetMousePos();
        int clicked = -1, starred = -1;
        for (int i = first; i < (int)palRows.size() && i < first + fitRows; ++i) {
            const float ry = listY + rowH * (float)(i - first);
            const bool over = m.x >= x && m.x < x + pw && m.y >= ry && m.y < ry + rowH;
            const PalRow& r = palRows[(std::size_t)i];
            const bool fav = std::find(store.fav.begin(), store.fav.end(), r.key) != store.fav.end();
            const bool overStar = over && m.x >= x + pw - 30;
            if (over && (m.x != palX || m.y != palY) && UI::IsMouseClicked(0)) { if (overStar) starred = i; else clicked = i; }
            if (i == palSel || over) dl->AddRectFilled(UI::Vec2(x + 4, ry), UI::Vec2(x + pw - 4, ry + rowH), i == palSel ? kTabOn : kHover, 3.0f);
            dl->AddCircleFilled(UI::Vec2(x + 16, ry + rowH * 0.5f), 4.0f, r.arg.empty() ? (UI::Color32)vsed::CatColor(r.def->cat) : Rgb(150, 200, 255));
            dl->AddText(UI::Vec2(x + 28, ry + 3), kText, Fit(r.title, pw * 0.55f).c_str());
            if (!r.place.empty()) {
                const std::string pl = Fit(r.place, pw * 0.28f);
                dl->AddText(UI::Vec2(x + pw - 36 - UI::CalcTextWidth(pl.c_str()), ry + 3), kDim, pl.c_str());
            }
            // the favourite star: drawn as a small diamond (filled when it is a favourite)
            const UI::Vec2 c(x + pw - 18, ry + rowH * 0.5f);
            const float rr = 5.0f;
            const UI::Color32 sc = fav ? kWarn : (overStar ? kText : kLine);
            if (fav) dl->AddQuadFilled(UI::Vec2(c.x, c.y - rr), UI::Vec2(c.x + rr, c.y), UI::Vec2(c.x, c.y + rr), UI::Vec2(c.x - rr, c.y), sc);
            else dl->AddQuad(UI::Vec2(c.x, c.y - rr), UI::Vec2(c.x + rr, c.y), UI::Vec2(c.x, c.y + rr), UI::Vec2(c.x - rr, c.y), sc, 1.5f);
            if (overStar) UI::SetTooltip("%s", fav ? "Remove from Favourites" : "Add to Favourites");
        }
        if (palRows.empty()) {
            dl->AddText(UI::Vec2(x + 14, listY + 4), kDim, palBuf[0] ? "No block matches. Try another word." : "Nothing in this group yet.");
        }
        if (palHidden > 0 && !palSearchAll) {   // the search found blocks the level or "everyday blocks" hides
            UI::SetCursorScreenPos(UI::Vec2(x + 14, y + ph - helpH - rowH - 2));
            if (UI::SmallButton(("Search all blocks (" + std::to_string(palHidden) + " more)##vsapp_psall").c_str())) palSearchAll = true;
        }
        if (palSel >= 0 && palSel < (int)palRows.size()) {
            const std::string help = palRows[(std::size_t)palSel].help;
            dl->AddLine(UI::Vec2(x + 6, y + ph - helpH), UI::Vec2(x + pw - 6, y + ph - helpH), kLine);
            dl->AddText(UI::Vec2(x + 10, y + ph - helpH + 6), kDim, Fit(help.empty() ? palRows[(std::size_t)palSel].title : help, pw - 20).c_str());
        }
        const bool outside = !(m.x >= x && m.x < x + pw && m.y >= y && m.y < y + ph);
        if (starred >= 0) toggleFavourite((std::size_t)starred);
        else if (clicked >= 0) pickPalette((std::size_t)clicked);
        else if (enter || UI::IsKeyPressed(UI::KeyId_Enter, false)) pickPalette((std::size_t)std::max(0, palSel));
        else if (UI::IsKeyPressed(UI::KeyId_Escape, false) || (outside && (UI::IsMouseClicked(0) || UI::IsMouseClicked(1)) && (m.x != palX || m.y != palY) &&
                                                               !UI::IsPopupOpen("##vsapp_pside"))) {
            palOpen = false; s.linkPending = false; s.wireInsOn = false;   // dismissed without a pick
        }
    }
    UI::End();
}

}  // namespace vsapp
