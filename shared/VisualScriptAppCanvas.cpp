// Visual Script app (VisualScriptApp.hpp): the canvas and the Add Block list. The canvas draws the shown graph's comment frames,
// wires (flow wires white, value wires in their type's colour, through their knots), blocks (a header in the block's category
// colour, flow plugs as triangles, value plugs as circles, the setting under the header) and a minimap; it pans (left-drag on
// empty space, middle or right drag, the wheel in trackpad mode), zooms about the pointer, selects (click, Ctrl / Shift+click,
// Shift or Ctrl+drag a box), moves blocks (snapped to the grid when let go), wires a plug to a plug (the model's rules refuse
// what does not fit, in plain words), and a wire dropped on empty space opens the list with the blocks that take it. A right
// click on empty space opens the list too; on a block, its menu. The rules are the model's (VisualScriptModel.hpp,
// VsIdeModel.hpp, VisualScriptEasyOps.hpp); this file draws and reads the pointer.
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
float TextW(float size, const char* t) { return UI::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, t).x; }
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
        if (const vsed::VsEditNode* n = g.find(s.focusNode))
            s.pan = vsed::VsPoint(W * 0.45f - n->x * scale(), H * 0.4f - n->y * scale());
        s.focusNode = -1;
    }
    UI::BareNextWindow();
    if (UI::Begin((cfg.idPrefix + "_canvas").c_str(), X, Y, W, H)) {
        const auto& dl = UI::GetWindowDrawList();
        const UI::Input& in = UI::GetFrameInput();
        const UI::Vec2 m = UI::GetMousePos();
        const bool overlay = modal || palOpen || nodeMenu >= 0;
        const bool hovered = canvas.in(m.x, m.y) && UI::IsWindowHovered() && !overlay;
        if (!overlay && (UI::IsMouseClicked(0) || UI::IsMouseClicked(1) || UI::IsMouseClicked(2))) canvasFocused = hovered;
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

        // ---- the comment frames ----
        const float fs = std::clamp(UI::GetFontSize() * z, 7.0f, 30.0f);
        int commentHit = -1;
        for (int ci = 0; ci < (int)g.comments.size(); ++ci) {
            const vsed::VsComment& c = g.comments[(std::size_t)ci];
            const UI::Vec2 a = toScreen(c.x, c.y), b = toScreen(c.x + c.w, c.y + c.h);
            static const UI::Color32 tints[] = {Rgb(90, 110, 140, 60), Rgb(90, 150, 100, 60), Rgb(170, 130, 70, 60),
                                                Rgb(160, 80, 90, 60), Rgb(120, 90, 160, 60), Rgb(110, 110, 110, 60)};
            dl->AddRectFilled(a, b, tints[(std::size_t)std::clamp(c.color, 0, 5)], 6.0f);
            dl->AddRect(a, b, Rgb(150, 160, 180, 110), 6.0f, 1.0f);
            dl->AddText(UI::GetFont(), fs, UI::Vec2(a.x + 8, a.y + 4), kText, c.text.c_str());
            if (m.x >= a.x && m.x < b.x && m.y >= a.y && m.y < a.y + fs + 8) commentHit = ci;
        }

        // ---- the blocks' layout (sizes from the Density table, like the editor's window) ----
        const oked::scriptui::Metrics dm = oked::scriptui::Current();
        const float headerH = dm.headerH * z, rowH = dm.rowH * z, pinR = std::max(3.0f, dm.pinR * z), pad = dm.nodePad * z;
        blockRects.clear();
        plugs.clear();
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
            nwi.zoom = z; nwi.titleW = TextW(fs, title.c_str()); nwi.inLabelW = inW; nwi.outLabelW = outW;
            nwi.hasDataIn = !d->dataIn.empty(); nwi.boxW = 0.0f;
            const float w = oked::scriptui::NodeWidth(dm, nwi);
            const int left = (int)d->dataIn.size() + (d->execIn ? 1 : 0), right = (int)(d->dataOut.size() + d->execOut.size());
            const float paramH = d->param ? rowH : 0.0f;
            const float h = headerH + paramH + rowH * (float)std::max(1, std::max(left, right)) + pad;
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
            for (const UI::Vec2& k : knots) { curve(from, k, col, thick); dl->AddCircleFilled(k, 4.0f * std::max(0.8f, z), col); from = k; }
            curve(from, b, col, thick);
        };
        auto knotScreen = [&](float wx, float wy) { return toScreen(wx, wy); };

        // ---- the wires, behind the blocks ----
        for (const vsed::VsExecLink& l : g.execLinks) {
            const Plug* a = plugAt(l.from, true, true, l.pin);
            const Plug* b = plugAt(l.to, true, false, 0);
            if (!a || !b) continue;
            const bool hot = s.sel.count(l.from) || s.sel.count(l.to);
            wireThrough(UI::Vec2(a->x, a->y), UI::Vec2(b->x, b->y), vsed::VsKnotPoints(g, true, l.from, l.pin, l.to, knotScreen),
                        hot ? Rgb(255, 255, 255) : Rgb(205, 208, 218), (hot ? 3.2f : 2.4f) * std::max(0.7f, z));
        }
        for (const vsed::VsDataLink& l : g.dataLinks) {
            const Plug* a = plugAt(l.producer, false, true, l.outPin);
            const Plug* b = plugAt(l.consumer, false, false, l.inPin);
            if (!a || !b) continue;
            const bool hot = s.sel.count(l.producer) || s.sel.count(l.consumer);
            const unsigned col = vsed::VsPinColor(vsed::VsOutPinType(g, l.producer, l.outPin));
            wireThrough(UI::Vec2(a->x, a->y), UI::Vec2(b->x, b->y), vsed::VsKnotPoints(g, false, l.consumer, l.inPin, 0, knotScreen),
                        hot ? Lift(col, 0.35f) : (UI::Color32)col, (hot ? 3.0f : 2.2f) * std::max(0.7f, z));
        }

        // ---- the blocks ----
        const vsed::VsGraphDiff& diff = vsed::VsIdeDiffNow(s);
        const std::set<int> added = diff.addedIds(), changed = diff.changedIds();
        const vsed::VsDebugView& dv = vsed::DebugView();
        std::set<int> problemNodes;
        for (const auto& is : s.issues) if (is.severity == 0 && is.node >= 0) problemNodes.insert(is.node);
        for (const auto& n : g.nodes) {
            const vsed::VsDef* d = vsed::VsDefFor(g, n);
            const auto rit = blockRects.find(n.id);
            if (!d || rit == blockRects.end()) continue;
            const Rect& r = rit->second;
            if (r.x1 < X || r.x0 > X + W || r.y1 < Y || r.y0 > Y + H) continue;   // off screen
            const UI::Vec2 p0(r.x0, r.y0), p1(r.x1, r.y1);
            const float round = dm.round * z;
            const bool sel = s.sel.count(n.id) || s.selected == n.id;
            dl->AddRectFilled(UI::Vec2(p0.x + 3, p0.y + 4), UI::Vec2(p1.x + 3, p1.y + 4), Rgb(0, 0, 0, 70), round);
            dl->AddRectFilled(p0, p1, kBody, round);
            dl->AddRectFilled(p0, UI::Vec2(p1.x, p0.y + headerH), vsed::CatColor(d->cat), round);
            const std::string title = vsed::VsNodeTitle(n, *d);
            dl->AddText(UI::GetFont(), fs, UI::Vec2(p0.x + pad, p0.y + (headerH - fs) * 0.5f), Rgb(250, 250, 252),
                        Fit(title, std::max(10.0f, (r.x1 - r.x0) - pad * 2)).c_str());
            if (d->param) {
                const std::string arg = n.arg.empty() ? std::string(d->paramHint ? d->paramHint : "") : n.arg;
                dl->AddRectFilled(UI::Vec2(p0.x + pad, p0.y + headerH + 2), UI::Vec2(p1.x - pad, p0.y + headerH + rowH - 2), Rgb(30, 32, 38), 3.0f);
                dl->AddText(UI::GetFont(), fs, UI::Vec2(p0.x + pad + 4, p0.y + headerH + (rowH - fs) * 0.5f), n.arg.empty() ? kDim : kText,
                            Fit(arg, (r.x1 - r.x0) - pad * 2 - 8).c_str());
            }
            UI::Color32 outline = sel ? kSelect : kLine;
            float thick = sel ? 2.0f : 1.0f;
            if (problemNodes.count(n.id)) { outline = kError; thick = 2.0f; }
            if (added.count(n.id)) { outline = kAdd; thick = 2.0f; }
            else if (changed.count(n.id)) { outline = kWarn; thick = 2.0f; }
            if (dv.stopped && dv.key == g.path && dv.node == n.id) { outline = Rgb(255, 210, 76); thick = 3.0f; }
            dl->AddRect(p0, p1, outline, round, thick);
            if (!g.path.empty() && okay::vs::VsDebugger::Get().HasBreakpoint(g.path, n.id))
                dl->AddCircleFilled(UI::Vec2(p0.x - 2, p0.y - 2), 6.0f * std::max(0.8f, z), Rgb(220, 60, 60));
            if (!g.path.empty() && vsed::Bookmarks().has(g.path, n.id))
                dl->AddRectFilled(UI::Vec2(p1.x - 10 * z, p0.y - 4 * z), UI::Vec2(p1.x - 3 * z, p0.y + 8 * z), kAccent);
            // the plugs and their names
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
                    if (vsed::VsTryWire(s, to->node, to->pin)) notice = "Wired";
                } else if (to && to->exec != s.dragExec && to->node != s.dragNode) {
                    s.refuseMsg = s.dragExec ? vsed::VsPlainFlowToValue() : vsed::VsPlainValueToFlow();
                    s.refuseTime = 5.0f;
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
            if (p && UI::IsKeyDown(UI::KeyId_Alt)) {   // Alt+click a plug: take its wires away
                g.unlinkPin(p->node, p->exec ? 0 : 1, p->output ? 1 : 0, p->pin);
            } else if (p) {
                s.dragging = true; s.dragExec = p->exec; s.dragFromOutput = p->output; s.dragNode = p->node; s.dragPin = p->pin;
            } else if (b >= 0) {
                if (in.ctrl) { if (s.sel.count(b)) s.sel.erase(b); else s.sel.insert(b); }
                else if (in.shift) s.sel.insert(b);
                else if (!s.sel.count(b)) s.sel = {b};
                s.selected = b;
                nodeDrag = b; dragLastX = m.x; dragLastY = m.y;
                if (UI::IsMouseDoubleClicked(0)) frameAll(true);
            } else if (commentHit >= 0) {
                commentDrag = commentHit; dragLastX = m.x; dragLastY = m.y;
                if (UI::IsMouseDoubleClicked(0)) {
                    s.renameComment = commentHit;
                    std::snprintf(s.renameBuf, sizeof s.renameBuf, "%s", g.comments[(std::size_t)commentHit].text.c_str());
                    renameBox = true;
                }
            } else {
                leftOnEmpty = true;
                boxing = in.shift || in.ctrl;   // Shift or Ctrl + drag: a selection box; a plain drag pans
                if (boxing) s.boxStart = vsed::VsPoint(m.x, m.y);
            }
        }
        if (nodeDrag >= 0) {
            if (UI::IsMouseDown(0)) {
                const float dx = (m.x - dragLastX) / S, dy = (m.y - dragLastY) / S;
                if (dx != 0.0f || dy != 0.0f) {
                    for (auto& n : g.nodes) if (s.sel.count(n.id)) { n.x += dx; n.y += dy; }
                    g.dirty = true;
                }
                dragLastX = m.x; dragLastY = m.y;
            } else {
                const bool moved = std::fabs(m.x - pressX) + std::fabs(m.y - pressY) > 3.0f;
                if (moved && settings.snapGrid)
                    for (auto& n : g.nodes) if (s.sel.count(n.id)) { n.x = vsed::VsSnap(n.x, 20.0f); n.y = vsed::VsSnap(n.y, 20.0f); }
                if (!moved && !in.ctrl && !in.shift) s.sel = {nodeDrag};
                nodeDrag = -1;
            }
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
        // the middle and right buttons pan; a right click (not a drag) opens the list on empty space, a block's menu on a block
        if (hovered && (UI::IsMouseClicked(1) || UI::IsMouseClicked(2))) { rightPressX = m.x; rightPressY = m.y; rightMax = 0; }
        if ((in.rightDown || in.middleDown) && canvasFocused && !overlay) {
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
                else { const UI::Vec2 w = toWorld(m.x, m.y); openPalette(m.x, m.y, w.x, w.y); }
            }
            panning = false;
        }
        if (nodeDrag < 0 && commentDrag < 0) { dragLastX = m.x; dragLastY = m.y; }

        // ---- the minimap ----
        if (settings.showMinimap && !g.nodes.empty() && W > 320 && H > 220) {
            float x0 = FLT_MAX, y0 = FLT_MAX, x1 = -FLT_MAX, y1 = -FLT_MAX;
            for (const auto& kv : blockRects) {
                const UI::Vec2 a = toWorld(kv.second.x0, kv.second.y0), b = toWorld(kv.second.x1, kv.second.y1);
                x0 = std::min(x0, a.x); y0 = std::min(y0, a.y); x1 = std::max(x1, b.x); y1 = std::max(y1, b.y);
            }
            const UI::Vec2 va = toWorld(X, Y), vb = toWorld(X + W, Y + H);
            x0 = std::min(x0, va.x); y0 = std::min(y0, va.y); x1 = std::max(x1, vb.x); y1 = std::max(y1, vb.y);
            const float mw = 160.0f, mh = 100.0f, mx0 = X + W - mw - 10, my0 = Y + H - mh - 10;
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
        if (s.missing) {
            dl->AddText(UI::Vec2(X + 24, Y + 24), kError, "This graph file could not be opened.");
            dl->AddText(UI::Vec2(X + 24, Y + 48), kText, g.path.c_str());
        } else if (g.nodes.empty()) {
            const char* a = "An empty graph. Right-click or press Space to add a block.";
            const char* b = "Start with an event (On Start, On Update, On Key), then wire steps after it.";
            dl->AddText(UI::Vec2(X + std::max(16.0f, (W - UI::CalcTextWidth(a)) * 0.5f), Y + H * 0.4f), kText, a);
            dl->AddText(UI::Vec2(X + std::max(16.0f, (W - UI::CalcTextWidth(b)) * 0.5f), Y + H * 0.4f + 26), kDim, b);
        }
        if (s.refuseTime > 0 && !s.refuseMsg.empty()) {
            const float bw = std::min(560.0f, W - 32.0f);
            const UI::Vec2 a(X + 16, Y + H - 70), b(X + 16 + bw, Y + H - 16);
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

    const float lh = UI::GetTextLineHeight() + 6;
    float w = 180;
    for (const auto& it : items) w = std::max(w, UI::CalcTextWidth(it.first.c_str()) + 30);
    const float h = 8;
    float total = h;
    for (const auto& it : items) total += it.first.empty() ? 7 : lh;
    const float x = std::min(menuX, canvas.x1 - w - 4), y = std::min(menuY, std::max(canvas.y0, canvas.y1 - total - 4));
    UI::BareNextWindow();
    if (UI::Begin((cfg.idPrefix + "_nodemenu").c_str(), x, y, w, total)) {
        const auto& dl = UI::GetWindowDrawList();
        dl->AddRectFilled(UI::Vec2(x, y), UI::Vec2(x + w, y + total), kPanel, 4.0f);
        dl->AddRect(UI::Vec2(x, y), UI::Vec2(x + w, y + total), kLine, 4.0f, 1.0f);
        const UI::Vec2 m = UI::GetMousePos();
        float ry = y + 4;
        std::string pick;
        for (const auto& it : items) {
            if (it.first.empty()) { dl->AddLine(UI::Vec2(x + 6, ry + 3), UI::Vec2(x + w - 6, ry + 3), kLine); ry += 7; continue; }
            const bool over = m.x >= x && m.x < x + w && m.y >= ry && m.y < ry + lh;
            if (over) dl->AddRectFilled(UI::Vec2(x + 3, ry), UI::Vec2(x + w - 3, ry + lh), kHover, 3.0f);
            dl->AddText(UI::Vec2(x + 12, ry + 3), kText, it.first.c_str());
            if (over && UI::IsMouseReleased(0)) pick = it.second;
            ry += lh;
        }
        const bool outside = !(m.x >= x && m.x < x + w && m.y >= y && m.y < y + total);
        if (!pick.empty()) {
            const int id = nodeMenu;
            nodeMenu = -1;
            if (pick.rfind("fix:", 0) == 0) { const std::size_t i = (std::size_t)std::atoi(pick.c_str() + 4); if (i < fixes.size()) vsed::VsApplyQuickFix(s.g, fixes[i]); }
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
                else okay::vs::VsDebugger::Get().ToggleBreakpoint(s.g.path, id);
            } else if (pick == "comment") { vsed::VsCommentAround(s, 0, 0); renameBox = true; }
            else {
                if (!vsed::VsRunCommand(s, pick, root())) note("Nothing to do here", 1);
                if (pick == "vs.findReferences" || pick == "vs.watchValues") settings.panel = 2;
            }
        } else if (outside && (UI::IsMouseClicked(0) || UI::IsMouseClicked(1))) nodeMenu = -1;
        if (UI::IsKeyPressed(UI::KeyId_Escape, false)) nodeMenu = -1;
    }
    UI::End();
}

// ---- the Add Block list ---------------------------------------------------------------------------------------------------

void VisualScriptApp::Impl::openPalette(float sx, float sy, float wx, float wy) {
    vsed::VsEditState& s = st();
    s.paletteWorld = vsed::VsPoint(wx, wy);
    palOpen = true; palFocus = true; palBuf[0] = 0; palSel = 0;
    palX = sx; palY = sy;
    palBuiltFor = "\x01";   // made again
    buildPalette();
}

void VisualScriptApp::Impl::buildPalette() {
    vsed::VsEditState& s = st();
    okay::vs::PinType dragged = okay::vs::PinType::Any;
    if (s.linkPending && !s.linkExec)
        dragged = s.linkFromOutput ? vsed::VsOutPinType(s.g, s.linkNode, s.linkPin) : vsed::VsInPinType(s.g, s.linkNode, s.linkPin);
    const std::string key = std::string(palBuf) + "|" + (s.linkPending ? "w" + std::to_string(s.linkExec) + std::to_string(s.linkFromOutput) + std::to_string((int)dragged) : "");
    if (key == palBuiltFor) return;
    palBuiltFor = key;
    palRows.clear();
    const std::string q = palBuf;
    for (const vsed::VsDef& d : vsed::Registry()) {
        if (!d.type || okay::vs::VsNodeSuppressed(d.type) || vsed::VsPaletteHidden(d)) continue;
        if (std::strcmp(d.type, "Action") == 0 || std::strcmp(d.type, "Condition") == 0) continue;   // the steps list's own rows
        int fit = 0;
        if (s.linkPending) {
            if (!vsed::VsCompatible(d, s.linkExec, s.linkFromOutput, dragged)) continue;   // only the blocks that take the wire
            fit = vsed::VsPinFitRank(d, s.linkExec, s.linkFromOutput, dragged);
        }
        const std::string title = d.title ? d.title : d.type;
        const char* help = vsed::VsNodeHelp(d.type);
        int score = vsed::VsMatchScore(q, title, vsed::VsNodeSearchText(d.type), help ? help : "");
        if (score <= 0) continue;
        score = vsed::VsStarterBoost(score, vsed::VsIsStarter(d.type), std::strncmp(d.type, "Fn.", 3) == 0);
        if (s.linkPending) score += vsed::VsWireSearchBoost(fit);
        if (q.empty() && !s.linkPending && !vsed::VsIsStarter(d.type)) score -= 200;   // nothing typed: the starter blocks first
        PalRow r;
        r.def = &d; r.score = score; r.fit = fit; r.title = title; r.help = help ? help : "";
        const vsed::VsPlace place = vsed::VsPlaceForNode(d.type);
        r.place = place.category ? place.category : "";
        palRows.push_back(std::move(r));
    }
    std::stable_sort(palRows.begin(), palRows.end(), [](const PalRow& a, const PalRow& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.title < b.title;
    });
    if (palRows.size() > 300) palRows.resize(300);
    palSel = std::clamp(palSel, 0, std::max(0, (int)palRows.size() - 1));
}

bool VisualScriptApp::Impl::pickPalette(std::size_t index) {
    if (index >= palRows.size() || !palRows[index].def) return false;
    vsed::VsEditState& s = st();
    const int id = vsed::VsAddFromPalette(s, *palRows[index].def);
    palOpen = false;
    s.linkPending = false;
    canvasFocused = true;
    if (id < 0) { note(s.refuseMsg, 1); return false; }
    note("Added " + palRows[index].title);
    return true;
}

void VisualScriptApp::Impl::drawPalette(float X, float Y, float W, float H) {
    const float pw = std::min(420.0f, W - 20.0f), ph = std::min(380.0f, H - 20.0f);
    const float x = std::clamp(palX, X + 4, X + W - pw - 4), y = std::clamp(palY, Y + 4, Y + H - ph - 4);
    UI::BareNextWindow();
    if (UI::Begin((cfg.idPrefix + "_palette").c_str(), x, y, pw, ph)) {
        const auto& dl = UI::GetWindowDrawList();
        dl->AddRectFilled(UI::Vec2(x, y), UI::Vec2(x + pw, y + ph), kPanel, 6.0f);
        dl->AddRect(UI::Vec2(x, y), UI::Vec2(x + pw, y + ph), kLine, 6.0f, 1.0f);
        vsed::VsEditState& s = st();
        UI::SetCursorScreenPos(UI::Vec2(x + 10, y + 8));
        UI::TextUnformatted(s.linkPending ? "Add a block that takes this wire" : "Add a block");
        UI::SetCursorScreenPos(UI::Vec2(x + 10, UI::GetCursorScreenPos().y + 2));
        UI::SetNextItemWidth(pw - 20);
        if (palFocus) { UI::SetKeyboardFocusHere(); palFocus = false; }
        const std::string before = palBuf;
        const bool enter = UI::InputTextWithHint("##vsapp_palq", "search: move, wait, key, add...", palBuf, (int)sizeof palBuf, UI::InputText_EnterReturnsTrue);
        if (before != palBuf) palSel = 0;
        buildPalette();
        if (UI::IsKeyPressed(UI::KeyId_Down)) palSel = std::min(palSel + 1, (int)palRows.size() - 1);
        if (UI::IsKeyPressed(UI::KeyId_Up)) palSel = std::max(palSel - 1, 0);
        const float listY = UI::GetCursorScreenPos().y + 4, helpH = 46.0f;
        const float rowH = UI::GetTextLineHeight() + 6;
        const int fitRows = std::max(1, (int)((y + ph - helpH - listY) / rowH));
        const int first = std::max(0, std::min(palSel - fitRows + 1, (int)palRows.size() - fitRows));
        const UI::Vec2 m = UI::GetMousePos();
        int clicked = -1;
        for (int i = first; i < (int)palRows.size() && i < first + fitRows; ++i) {
            const float ry = listY + rowH * (float)(i - first);
            const bool over = m.x >= x && m.x < x + pw && m.y >= ry && m.y < ry + rowH;
            if (over && (m.x != palX || m.y != palY)) { if (UI::IsMouseClicked(0)) clicked = i; }
            if (i == palSel || over) dl->AddRectFilled(UI::Vec2(x + 4, ry), UI::Vec2(x + pw - 4, ry + rowH), i == palSel ? kTabOn : kHover, 3.0f);
            const PalRow& r = palRows[(std::size_t)i];
            dl->AddCircleFilled(UI::Vec2(x + 16, ry + rowH * 0.5f), 4.0f, vsed::CatColor(r.def->cat));
            dl->AddText(UI::Vec2(x + 28, ry + 3), kText, Fit(r.title, pw * 0.6f).c_str());
            if (!r.place.empty()) {
                const std::string pl = Fit(r.place, pw * 0.3f);
                dl->AddText(UI::Vec2(x + pw - 12 - UI::CalcTextWidth(pl.c_str()), ry + 3), kDim, pl.c_str());
            }
        }
        if (palRows.empty()) dl->AddText(UI::Vec2(x + 14, listY + 4), kDim, "No block matches. Try another word.");
        if (palSel >= 0 && palSel < (int)palRows.size()) {
            const std::string help = palRows[(std::size_t)palSel].help;
            dl->AddLine(UI::Vec2(x + 6, y + ph - helpH), UI::Vec2(x + pw - 6, y + ph - helpH), kLine);
            dl->AddText(UI::Vec2(x + 10, y + ph - helpH + 6), kDim, Fit(help.empty() ? palRows[(std::size_t)palSel].title : help, pw - 20).c_str());
        }
        const bool outside = !(m.x >= x && m.x < x + pw && m.y >= y && m.y < y + ph);
        if (clicked >= 0) pickPalette((std::size_t)clicked);
        else if (enter || UI::IsKeyPressed(UI::KeyId_Enter, false)) pickPalette((std::size_t)std::max(0, palSel));
        else if (UI::IsKeyPressed(UI::KeyId_Escape, false) || (outside && (UI::IsMouseClicked(0) || UI::IsMouseClicked(1)) && (m.x != palX || m.y != palY))) {
            palOpen = false; s.linkPending = false;
        }
    }
    UI::End();
}

}  // namespace vsapp
