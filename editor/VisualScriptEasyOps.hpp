#pragma once
// Easier use of the canvas: small functions that take a graph and return the changed graph (no ImGui, no clicks),
// so tests can run them. The canvas calls them; they hold the rules:
//   - a block added from the popup joins the selected / last block when the plugs fit, and lands beside it
//     without covering another block (VsPlaceBeside, VsAutoWire),
//   - a block dropped on a wire splits that wire in two (VsInsertOnExecWire, VsInsertOnDataWire),
//   - Delete on a block in a chain reconnects its neighbours; Shift+Delete cuts instead (VsDeleteBlock),
//   - a recipe lands beside what is already there (VsInsertRecipe).
// This header is included from inside VisualScriptEditor.hpp, after the graph model (VsEditGraph, FindDef,
// VsWireOk, VsPasteText), because it uses them. Written for this project from its own graph rules.
// Layout numbers are in the same stored units as VsEditNode::x / y (the canvas multiplies them by 1.4).
#include "VisualScriptAutoConnect.hpp"
#include "VisualScriptChoices.hpp"
#include "VisualScriptPlainWords.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace okay::editor::vsed {

struct VsBoxF { float x = 0, y = 0, w = 0, h = 0; };

/// A rough size of a block on the canvas, in stored units. Real sizes depend on the font and zoom, so this is
/// a little on the big side: a new block is then never put on top of another one.
inline constexpr float kVsEstWidth = 200.0f;
inline constexpr float kVsGapX = 70.0f;
inline constexpr float kVsGapY = 26.0f;

inline VsBoxF VsEstBox(const VsEditGraph& g, const VsEditNode& n) {
    (void)g;
    const VsDef* d = FindDef(n.type);
    std::size_t left = 0, right = 0;
    if (d) {
        left = d->dataIn.size() + (d->execIn ? 1 : 0);
        right = d->dataOut.size() + d->execOut.size();
    }
    const float rows = (float)std::max<std::size_t>(1, std::max(left, right));
    return {n.x, n.y, kVsEstWidth, 46.0f + 20.0f * rows + (d && d->param ? 22.0f : 0.0f)};
}

inline bool VsBoxesOverlap(const VsBoxF& a, const VsBoxF& b, float margin = 8.0f) {
    return a.x < b.x + b.w + margin && b.x < a.x + a.w + margin && a.y < b.y + b.h + margin && b.y < a.y + a.h + margin;
}

inline bool VsSpotFree(const VsEditGraph& g, const VsBoxF& box, int ignoreId = -1) {
    for (const VsEditNode& n : g.nodes) {
        if (n.id == ignoreId) continue;
        if (VsBoxesOverlap(box, VsEstBox(g, n))) return false;
    }
    return true;
}

/// Where a new block of type `type` goes so it sits to the right of `anchorId` and covers nothing. Tries the
/// same height first, then lower down, then one column further right.
inline std::pair<float, float> VsPlaceBeside(const VsEditGraph& g, int anchorId, const char* type, int selfId = -1) {
    const VsEditNode* a = nullptr;
    for (const VsEditNode& n : g.nodes) if (n.id == anchorId) a = &n;
    if (!a) return {40.0f, 40.0f};
    const VsBoxF ab = VsEstBox(g, *a);
    VsEditNode probe; probe.id = selfId; probe.type = type ? type : "";
    float x = ab.x + ab.w + kVsGapX;
    float y = ab.y;
    for (int col = 0; col < 6; ++col) {
        float yy = ab.y;
        for (int row = 0; row < 14; ++row) {
            probe.x = x; probe.y = yy;
            const VsBoxF pb = VsEstBox(g, probe);
            if (VsSpotFree(g, pb, selfId)) return {x, yy};
            yy += pb.h + kVsGapY;
        }
        x += kVsEstWidth + kVsGapX;
    }
    return {x, y};
}

/// Top-left for a group of blocks that should land next to what is already there: under the existing blocks,
/// lined up with the leftmost one. An empty graph gives the usual starting corner.
inline std::pair<float, float> VsFreeCorner(const VsEditGraph& g) {
    if (g.nodes.empty()) return {40.0f, 40.0f};
    float minX = 1e9f, maxY = -1e9f;
    for (const VsEditNode& n : g.nodes) {
        const VsBoxF b = VsEstBox(g, n);
        minX = std::min(minX, b.x);
        maxY = std::max(maxY, b.y + b.h);
    }
    return {minX, maxY + 60.0f};
}

inline bool VsIsPureData(const VsDef* d) { return d && !d->execIn && d->execOut.empty(); }

inline bool VsInputWired(const VsEditGraph& g, int node, int pin) {
    for (const VsDataLink& l : g.dataLinks) if (l.consumer == node && l.inPin == pin) return true;
    return false;
}
inline bool VsInputHasValue(const VsEditNode& n, int pin) {
    const auto it = n.pinDefault.find(pin);
    return it != n.pinDefault.end() && !it->second.empty();
}

struct VsWireMade { bool exec = false, data = false; bool any() const { return exec || data; } };

/// Join a just-added block to `anchorId`. The flow (the white "then" plug) first: the anchor's first free "then"
/// plug goes to the new block. If that does not fit, a value: the anchor's value plug into the new block's first
/// free input that takes it, or (for a block that only makes a value) the new block into the anchor's first
/// empty input. Nothing is connected when nothing fits.
inline VsWireMade VsAutoWire(VsEditGraph& g, int anchorId, int newId) {
    VsWireMade made;
    VsEditNode* a = g.find(anchorId);
    VsEditNode* nn = g.find(newId);
    if (!a || !nn || anchorId == newId) return made;
    const VsDef* ad = FindDef(a->type);
    const VsDef* nd = FindDef(nn->type);
    if (!ad || !nd) return made;
    if (nd->execIn && !ad->execOut.empty()) {
        for (int p = 0; p < (int)ad->execOut.size(); ++p) {
            bool used = false;
            for (const VsExecLink& l : g.execLinks) if (l.from == anchorId && l.pin == p) used = true;
            if (used) continue;
            g.linkExec(anchorId, p, newId);
            made.exec = true;
            return made;
        }
    }
    // The anchor feeds the new block.
    if (!ad->dataOut.empty() && (!VsIsPureData(nd) || VsIsPureData(ad))) {
        for (int po = 0; po < (int)ad->dataOut.size(); ++po)
            for (int pi = 0; pi < (int)nd->dataIn.size(); ++pi) {
                if (VsInputWired(g, newId, pi) || VsInputHasValue(*nn, pi)) continue;
                if (!VsWireOk(g, newId, pi, anchorId, po)) continue;
                g.linkData(newId, pi, anchorId, po);
                made.data = true;
                return made;
            }
    }
    // The new value block feeds the anchor.
    if (VsIsPureData(nd) && !nd->dataOut.empty()) {
        for (int pi = 0; pi < (int)ad->dataIn.size(); ++pi) {
            if (VsInputWired(g, anchorId, pi) || VsInputHasValue(*a, pi)) continue;
            for (int po = 0; po < (int)nd->dataOut.size(); ++po) {
                if (!VsWireOk(g, anchorId, pi, newId, po)) continue;
                g.linkData(anchorId, pi, newId, po);
                made.data = true;
                return made;
            }
        }
    }
    return made;
}

/// Which block a new block should join: the selected one, else the one used last. -1 when neither exists.
inline int VsPickAnchor(const VsEditGraph& g, int selected, int lastUsed) {
    for (const VsEditNode& n : g.nodes) if (n.id == selected) return selected;
    for (const VsEditNode& n : g.nodes) if (n.id == lastUsed) return lastUsed;
    return -1;
}

/// One call for the popup: join `newId` to the picked block and, when something was connected, move it beside
/// that block. Returns what was connected.
inline VsWireMade VsConnectAndPlace(VsEditGraph& g, int newId, int selected, int lastUsed) {
    const int anchor = VsPickAnchor(g, selected, lastUsed);
    if (anchor < 0 || anchor == newId) return {};
    const VsWireMade made = VsAutoWire(g, anchor, newId);
    if (made.any()) {
        if (VsEditNode* nn = g.find(newId)) {
            const auto p = VsPlaceBeside(g, anchor, nn->type.c_str(), newId);
            nn->x = p.first; nn->y = p.second;
        }
    }
    return made;
}

/// A block dropped on a flow wire: the wire becomes before -> block -> after. The block needs a "then" in and
/// out, and must not already be on the wire. The new wire takes the old one's place in the list, so the order
/// the wires run in stays the same.
inline bool VsInsertOnExecWire(VsEditGraph& g, int wireIndex, int blockId) {
    if (wireIndex < 0 || wireIndex >= (int)g.execLinks.size()) return false;
    const VsEditNode* b = g.find(blockId);
    const VsDef* d = b ? FindDef(b->type) : nullptr;
    if (!d || !d->execIn || d->execOut.empty()) return false;
    const VsExecLink w = g.execLinks[(std::size_t)wireIndex];
    if (w.from == blockId || w.to == blockId) return false;
    g.execLinks[(std::size_t)wireIndex] = {w.from, w.pin, blockId};
    g.execLinks.insert(g.execLinks.begin() + wireIndex + 1, {blockId, 0, w.to});
    g.dirty = true;
    return true;
}

/// A block dropped on a value wire: producer -> block -> consumer, when the block has an input that takes the
/// value and an output that fits where it went. Otherwise the graph is left as it was.
inline bool VsInsertOnDataWire(VsEditGraph& g, int wireIndex, int blockId) {
    if (wireIndex < 0 || wireIndex >= (int)g.dataLinks.size()) return false;
    const VsEditNode* b = g.find(blockId);
    const VsDef* d = b ? FindDef(b->type) : nullptr;
    if (!d) return false;
    const VsDataLink w = g.dataLinks[(std::size_t)wireIndex];
    if (w.consumer == blockId || w.producer == blockId) return false;
    int pin = -1, out = -1;
    for (int pi = 0; pi < (int)d->dataIn.size() && pin < 0; ++pi)
        if (!VsInputWired(g, blockId, pi) && VsWireOk(g, blockId, pi, w.producer, w.outPin)) pin = pi;
    for (int po = 0; po < (int)d->dataOut.size() && out < 0; ++po)
        if (VsWireOk(g, w.consumer, w.inPin, blockId, po)) out = po;
    if (pin < 0 || out < 0) return false;
    g.dataLinks.erase(g.dataLinks.begin() + wireIndex);
    g.linkData(blockId, pin, w.producer, w.outPin);
    g.linkData(w.consumer, w.inPin, blockId, out);
    return true;
}

/// True when the block has no wires at all (only such a block is split onto a wire when it is dropped).
inline bool VsIsLoose(const VsEditGraph& g, int id) {
    for (const VsExecLink& l : g.execLinks) if (l.from == id || l.to == id) return false;
    for (const VsDataLink& l : g.dataLinks) if (l.consumer == id || l.producer == id) return false;
    return true;
}

/// Delete one block. Normally the blocks before and after it are joined (flow, and a value passing straight
/// through); with `cut` (Shift held) they are left apart. Returns true when neighbours were joined.
inline bool VsDeleteBlock(VsEditGraph& g, int id, bool cut) {
    bool joined = false;
    if (!cut && g.find(id)) {
        // Flow: only for a block with one way out (a Branch has several, so nothing is guessed).
        std::vector<VsExecLink> in, out;
        std::set<int> outPins;
        for (const VsExecLink& l : g.execLinks) {
            if (l.to == id && l.from != id) in.push_back(l);
            if (l.from == id && l.to != id) { out.push_back(l); outPins.insert(l.pin); }
        }
        if (outPins.size() == 1 && !in.empty()) {
            for (const VsExecLink& i : in)
                for (const VsExecLink& o : out)
                    if (i.from != o.to) {
                        g.linkExec(i.from, i.pin, o.to);
                        joined = true;
                    }
        }
        // Value: a block with exactly one wired input hands that producer to whoever read its first output.
        std::vector<VsDataLink> feeds;
        for (const VsDataLink& l : g.dataLinks) if (l.consumer == id) feeds.push_back(l);
        if (feeds.size() == 1) {
            const VsDataLink f = feeds[0];
            std::vector<VsDataLink> readers;
            for (const VsDataLink& l : g.dataLinks) if (l.producer == id && l.outPin == 0) readers.push_back(l);
            for (const VsDataLink& r : readers)
                if (r.consumer != f.producer && VsWireOk(g, r.consumer, r.inPin, f.producer, f.outPin)) {
                    g.linkData(r.consumer, r.inPin, f.producer, f.outPin);
                    joined = true;
                }
        }
    }
    g.removeNode(id);
    return joined;
}

/// Delete several blocks (ids in order); each one joins its neighbours unless `cut`.
inline int VsDeleteBlocks(VsEditGraph& g, const std::set<int>& ids, bool cut) {
    int joined = 0;
    for (int id : ids) if (VsDeleteBlock(g, id, cut)) ++joined;
    return joined;
}

/// A recipe's blocks, already wired, placed next to what is already on the canvas. Returns the new ids.
inline std::set<int> VsInsertRecipe(VsEditGraph& g, const std::string& recipeGraph) {
    const auto c = VsFreeCorner(g);
    return VsPasteText(g, recipeGraph, c.first, c.second);
}

/// Distance from a point to the curved wire the canvas draws between two plugs (same curve as the canvas:
/// a cubic whose handles are half the horizontal gap, at least 50 * zoom). Used to find the wire under the mouse.
inline float VsCurveDistance(float px, float py, float ax, float ay, float bx, float by, float zoom = 1.0f) {
    const float dx = std::max(50.0f * zoom, std::fabs(bx - ax) * 0.5f);
    const float c1x = ax + dx, c1y = ay, c2x = bx - dx, c2y = by;
    float best = 1.0e18f;
    for (int i = 0; i <= 24; ++i) {
        const float t = i / 24.0f, u = 1.0f - t;
        const float x = u * u * u * ax + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * bx;
        const float y = u * u * u * ay + 3 * u * u * t * c1y + 3 * u * t * t * c2y + t * t * t * by;
        best = std::min(best, (x - px) * (x - px) + (y - py) * (y - py));
    }
    return std::sqrt(best);
}

/// Blocks a flow wire can reach from `id` (so a fix never wires a block back into its own chain).
inline std::set<int> VsDownstream(const VsEditGraph& g, int id) {
    std::set<int> seen;
    std::vector<int> stack{id};
    while (!stack.empty()) {
        const int n = stack.back(); stack.pop_back();
        for (const VsExecLink& l : g.execLinks)
            if (l.from == n && seen.insert(l.to).second) stack.push_back(l.to);
    }
    return seen;
}

/// "This block is not connected to anything yet": plug it after the nearest block that has a free "then" plug
/// (using the same rule as a new block, VsAutoConnectFrom), else after the first starting block with a free plug.
inline bool VsFixNotConnected(VsEditGraph& g, int id) {
    const VsEditNode* n = g.find(id);
    const VsDef* d = n ? FindDef(n->type) : nullptr;
    if (!d || !d->execIn) return false;
    for (const VsExecLink& l : g.execLinks) if (l.to == id) return false;   // already has something before it
    const std::set<int> below = VsDownstream(g, id);
    std::vector<VsLayoutNode> lay;
    for (const VsEditNode& o : g.nodes) {
        if (o.id == id || below.count(o.id)) continue;
        const VsDef* od = FindDef(o.type);
        if (!od || od->execOut.empty()) continue;
        bool used = false;
        for (const VsExecLink& l : g.execLinks) if (l.from == o.id && l.pin == 0) used = true;
        const VsBoxF b = VsEstBox(g, o);
        lay.push_back({o.id, b.x, b.y, b.w, b.h, true, used});
    }
    int from = VsAutoConnectFrom(lay, n->x, n->y, true, 100000.0f, 100000.0f);
    if (from < 0)
        for (const VsLayoutNode& c : lay)
            if (!c.flowOutUsed) { const VsEditNode* o = g.find(c.id); const VsDef* od = o ? FindDef(o->type) : nullptr; if (od && !od->execIn) { from = c.id; break; } }
    if (from < 0) return false;
    g.linkExec(from, 0, id);
    return true;
}

/// A wire whose plugs do not fit (consumer's input `inPin`): put the one block between them that turns one into
/// the other (the same offer the wire-refusal button makes). False when there is no single obvious block.
inline bool VsFixMismatch(VsEditGraph& g, int consumer, int inPin) {
    int idx = -1;
    for (int i = 0; i < (int)g.dataLinks.size(); ++i)
        if (g.dataLinks[(std::size_t)i].consumer == consumer && g.dataLinks[(std::size_t)i].inPin == inPin) idx = i;
    if (idx < 0) return false;
    const VsDataLink w = g.dataLinks[(std::size_t)idx];
    const VsConvertOffer offer = VsOfferConvert((char)VsOutPinType(g, w.producer, w.outPin), (char)VsInPinType(g, consumer, inPin));
    if (offer.type.empty()) return false;
    const VsEditNode* p = g.find(w.producer);
    const VsEditNode* c = g.find(consumer);
    if (!p || !c) return false;
    const int id = g.add(offer.type, (p->x + c->x) * 0.5f, (p->y + c->y) * 0.5f + 40.0f);
    if (VsInsertOnDataWire(g, idx, id)) return true;
    g.removeNode(id);
    return false;
}

/// The one-click fix for a finding of the graph checker, when there is an obvious one: a plug that gets the wrong
/// kind of value (a block that turns one into the other goes between), or a block nothing reaches (it is plugged
/// in after the nearest free block). False, graph unchanged, otherwise.
inline bool VsFixIssue(VsEditGraph& g, const std::string& message, int node, int pin, int pinDir) {
    if (node < 0) return false;
    if (pin >= 0 && pinDir == 0 && VsPlainIssue(message).rfind("This plug needs", 0) == 0) return VsFixMismatch(g, node, pin);
    if (VsPlainIssue(message) == VsPlainNotConnected()) return VsFixNotConnected(g, node);
    return false;
}
/// Would VsFixIssue do something? (Tried on a copy, so the button only shows when it will work.)
inline bool VsFixAvailable(const VsEditGraph& g, const std::string& message, int node, int pin, int pinDir) {
    VsEditGraph copy = g;
    return VsFixIssue(copy, message, node, pin, pinDir);
}

/// While a wire is dragged from one plug: would the plug `node`/`idx` (on the opposite side, same kind) take it?
/// Flow goes into a block that has a "then" input; a value must be the right kind and must not make a circle.
inline bool VsPlugAccepts(const VsEditGraph& g, bool exec, bool dragFromOutput, int dragNode, int dragPin, int node, int idx) {
    if (node == dragNode) return false;
    if (exec) {
        if (!dragFromOutput) return true;                       // dragging from an input: any "then" output will do
        const VsEditNode* dn = VsFind(g, node);
        const VsDef* dd = dn ? FindDef(dn->type) : nullptr;
        return dd && dd->execIn;
    }
    const int cons = dragFromOutput ? node : dragNode, inP = dragFromOutput ? idx : dragPin;
    const int prod = dragFromOutput ? dragNode : node, outP = dragFromOutput ? dragPin : idx;
    std::vector<std::pair<int, int>> edges;
    for (const VsDataLink& l : g.dataLinks) edges.push_back({l.producer, l.consumer});
    return VsWireOk(g, cons, inP, prod, outP) && !VsDataWouldCycle(edges, prod, cons);
}

/// One wire as the canvas draws it: which kind, its place in the graph's list, and where its two plugs are on screen.
struct VsWireEnds { bool exec = true; int index = 0; float ax = 0, ay = 0, bx = 0, by = 0; };

/// The wire nearest to a point (within maxDist pixels), as an index into `wires`; -1 when none is close enough.
inline int VsNearestWire(const std::vector<VsWireEnds>& wires, float px, float py, float zoom, float maxDist) {
    int best = -1;
    float bestD = maxDist;
    for (int i = 0; i < (int)wires.size(); ++i) {
        const VsWireEnds& w = wires[(std::size_t)i];
        const float d = VsCurveDistance(px, py, w.ax, w.ay, w.bx, w.by, zoom);
        if (d <= bestD) { bestD = d; best = i; }
    }
    return best;
}

/// A wire named by what it joins (stays valid while other wires come and go; an index would not).
struct VsWireRef {
    bool on = false, exec = true;
    int a = 0, b = 0, c = 0;   // exec: from, pin, to.  value: consumer, inPin, (unused)
};
inline VsWireRef VsRefOfWire(const VsEditGraph& g, bool exec, int index) {
    VsWireRef r;
    if (exec && index >= 0 && index < (int)g.execLinks.size()) {
        const VsExecLink& l = g.execLinks[(std::size_t)index];
        r = {true, true, l.from, l.pin, l.to};
    } else if (!exec && index >= 0 && index < (int)g.dataLinks.size()) {
        const VsDataLink& l = g.dataLinks[(std::size_t)index];
        r = {true, false, l.consumer, l.inPin, 0};
    }
    return r;
}

/// Put block `blockId` on the wire `ref` names. False (graph unchanged) when the wire is gone or the block does not fit.
inline bool VsSplitWire(VsEditGraph& g, const VsWireRef& ref, int blockId) {
    if (!ref.on) return false;
    if (ref.exec) {
        for (int i = 0; i < (int)g.execLinks.size(); ++i) {
            const VsExecLink& l = g.execLinks[(std::size_t)i];
            if (l.from == ref.a && l.pin == ref.b && l.to == ref.c) return VsInsertOnExecWire(g, i, blockId);
        }
        return false;
    }
    for (int i = 0; i < (int)g.dataLinks.size(); ++i) {
        const VsDataLink& l = g.dataLinks[(std::size_t)i];
        if (l.consumer == ref.a && l.inPin == ref.b) return VsInsertOnDataWire(g, i, blockId);
    }
    return false;
}

} // namespace okay::editor::vsed
