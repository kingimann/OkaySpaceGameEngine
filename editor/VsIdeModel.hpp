#pragma once
// The Visual Script IDE commands on a graph tab, with no drawing: arrange (align, distribute, straighten, auto-arrange), collapse
// to function, bookmarks, find references (the Navigator's state), watch values, fix all; the per-tab IDE state (watches, drawn
// block sizes, knots being dragged), knots, and the Compare diff. Run by the Visual Script app (shared/VisualScriptApp). Written
// for this project.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace okay::editor::vsed {

/// What the IDE tools keep per graph tab (kept beside VsEditState, keyed by its tab id).
struct VsIdeTab {
    VsWatchList watches;
    std::map<int, std::pair<float, float>> sizes;   // each block's size as last drawn, in stored units
    std::string pendingCmd;                         // a command picked in a menu this frame, run after the node loop
    int knotDrag = -1;                              // index into g.knots being dragged
};
inline std::map<int, VsIdeTab>& VsIdeTabs() { static std::map<int, VsIdeTab> m; return m; }
inline VsIdeTab& VsIde(const VsEditState& st) { return VsIdeTabs()[st.tabId]; }

/// The Graph Navigator window: references, bookmarks, watches.
struct VsNavigator {
    bool open = false, focusSearch = false;
    int tab = -1;                         // a tab to bring to the front next frame: 0 references, 1 bookmarks, 2 watches
    char text[160] = "";
    int kind = 0;                         // VsFindWhat
    std::vector<VsRefHit> hits;
    std::string searched, root;
};
inline VsNavigator& Navigator() { static VsNavigator n; return n; }

/// A block's box in stored units: its drawn size when it has been drawn, else the estimate.
inline VsBoxF VsLiveBox(const VsEditState& st, const VsEditNode& n) {
    const VsIdeTab& t = VsIde(st);
    const auto it = t.sizes.find(n.id);
    if (it != t.sizes.end()) return {n.x, n.y, it->second.first, it->second.second};
    return VsEstBox(st.g, n);
}

/// The row a plug sits on in its block (0 = the first row under the header). Inputs: flow first, then values.
/// Outputs: flow outputs first, then value outputs. The canvas lays rows out the same way on both sides.
inline int VsPinRow(const VsDef& d, bool exec, bool output, int pin) {
    if (!output) return exec ? 0 : (d.execIn ? 1 : 0) + pin;
    return exec ? pin : (int)d.execOut.size() + pin;
}

/// Straighten wires: each selected block that a wire reaches from another selected block (or, with one block selected,
/// from anything) moves up or down so that wire is level. Returns how many blocks moved.
inline int VsStraightenSelection(VsEditGraph& g, const std::set<int>& sel, float rowH) {
    int moved = 0;
    std::set<int> done;
    auto level = [&](int src, int srcRow, int dst, int dstRow) {
        if (src == dst || done.count(dst) || !sel.count(dst) || (sel.size() > 1 && !sel.count(src))) return;
        VsEditNode* s = g.find(src); VsEditNode* d = g.find(dst);
        if (!s || !d) return;
        const float ny = VsStraightenY(s->y, srcRow, d->y, dstRow, rowH);
        if (std::fabs(ny - d->y) > 0.01f) { d->y = ny; ++moved; g.dirty = true; }
        done.insert(dst);
    };
    for (const VsExecLink& l : g.execLinks) {
        const VsEditNode* s = g.find(l.from); const VsEditNode* d = g.find(l.to);
        const VsDef* sd = s ? VsDefFor(g, *s) : nullptr; const VsDef* dd = d ? VsDefFor(g, *d) : nullptr;
        if (sd && dd) level(l.from, VsPinRow(*sd, true, true, l.pin), l.to, VsPinRow(*dd, true, false, 0));
    }
    for (const VsDataLink& l : g.dataLinks) {
        const VsEditNode* s = g.find(l.producer); const VsEditNode* d = g.find(l.consumer);
        const VsDef* sd = s ? VsDefFor(g, *s) : nullptr; const VsDef* dd = d ? VsDefFor(g, *d) : nullptr;
        if (sd && dd) level(l.producer, VsPinRow(*sd, false, true, l.outPin), l.consumer, VsPinRow(*dd, false, false, l.inPin));
    }
    return moved;
}

/// Find references to `q` and show them in the Navigator.
inline void VsNavigatorSearch(const VsRefQuery& q, const std::string& root) {
    VsNavigator& nav = Navigator();
    std::map<std::string, std::string> open;
    for (const auto& t : Session().tabs) if (t && !t->g.path.empty()) open[t->g.path] = t->g.toText();
    nav.hits = VsFindRefs(root, q, open);
    nav.searched = std::string(VsFindWhatLabel(q.kind)) + " '" + q.name + "'";
    nav.root = root;
    nav.kind = (int)q.kind;
    std::snprintf(nav.text, sizeof nav.text, "%s", q.name.c_str());
    nav.open = true; nav.tab = 0;
}

/// Show block `node` of graph `path`: this tab, an open tab, or the file opened in a new tab.
inline void VsJumpTo(const std::string& path, int node) {
    VsEditState& cur = State();
    if (path.empty() || cur.g.path != path) {
        if (path.empty()) return;
        OpenFile(path);
    }
    VsEditState& st = State();
    if (node >= 0 && st.g.find(node)) { st.focusNode = node; st.sel = {node}; st.selected = node; }
}

/// Run a Visual Script command (VsCommands(), VsIdeProviders.hpp) on a graph tab. `root` is the project folder the
/// reference search looks in. True when the command did something.
inline bool VsRunCommand(VsEditState& st, const std::string& id, const std::string& root = "") {
    VsEditGraph& g = st.g;
    std::set<int> sel = st.sel;
    if (st.selected >= 0 && g.find(st.selected)) sel.insert(st.selected);
    const float rowH = oked::scriptui::Current().rowH / 1.4f;   // a plug row in stored units (the canvas draws at x1.4)
    auto boxes = [&](std::vector<int>& ids, std::vector<float>& xs, std::vector<float>& ys, std::vector<float>& ws, std::vector<float>& hs) {
        for (const auto& n : g.nodes) if (sel.count(n.id)) {
            const VsBoxF b = VsLiveBox(st, n);
            ids.push_back(n.id); xs.push_back(n.x); ys.push_back(n.y); ws.push_back(b.w); hs.push_back(b.h);
        }
    };
    auto put = [&](const std::vector<int>& ids, const std::vector<float>& xs, const std::vector<float>& ys) {
        for (std::size_t i = 0; i < ids.size(); ++i) if (VsEditNode* n = g.find(ids[i])) { n->x = xs[i]; n->y = ys[i]; }
        g.dirty = true;
    };
    static const std::map<std::string, VsAlign> kAlign = {
        {"vs.alignLeft", VsAlign::Left}, {"vs.alignRight", VsAlign::Right}, {"vs.alignTop", VsAlign::Top},
        {"vs.alignBottom", VsAlign::Bottom}, {"vs.alignCenter", VsAlign::CenterX}, {"vs.alignMiddle", VsAlign::CenterY}};
    if (auto a = kAlign.find(id); a != kAlign.end()) {
        if (sel.size() < 2) return false;
        std::vector<int> ids; std::vector<float> xs, ys, ws, hs;
        boxes(ids, xs, ys, ws, hs);
        VsAlignNodes(xs, ys, ws, hs, a->second);
        put(ids, xs, ys);
        return true;
    }
    if (id == "vs.distributeH" || id == "vs.distributeV") {
        if (sel.size() < 3) return false;
        std::vector<int> ids; std::vector<float> xs, ys, ws, hs;
        boxes(ids, xs, ys, ws, hs);
        VsDistributeNodes(xs, ys, ws, hs, id == "vs.distributeH");
        put(ids, xs, ys);
        return true;
    }
    if (id == "vs.straighten") return VsStraightenSelection(g, sel, rowH) > 0;
    if (id == "vs.autoArrange") {
        if (sel.size() < 2) return false;
        std::vector<int> ids; std::vector<float> xs, ys, ws, hs;
        boxes(ids, xs, ys, ws, hs);
        std::vector<VsArrangeEdge> edges;
        for (const auto& l : g.execLinks) if (sel.count(l.from) && sel.count(l.to)) edges.push_back({l.from, l.to});
        for (const auto& l : g.dataLinks) if (sel.count(l.producer) && sel.count(l.consumer)) edges.push_back({l.producer, l.consumer});
        const auto pos = VsAutoArrange(ids, xs, ys, ws, hs, edges, 60.0f, 24.0f);
        for (std::size_t i = 0; i < ids.size(); ++i) { xs[i] = pos[i].first; ys[i] = pos[i].second; }
        put(ids, xs, ys);
        return true;
    }
    if (id == "vs.collapseToFunction") {
        const VsCollapseResult r = VsCollapseToFunction(g, sel);
        if (!r.ok) { Log("Collapse to Function: " + r.why, 1); return false; }
        st.sel = {r.callNode}; st.selected = r.callNode; st.focusNode = r.callNode;
        Log("Made function '" + r.name + "' (" + std::to_string(r.params) + " in, " + std::to_string(r.returns) +
            " out). The Call Function block runs it; its body is below the graph.");
        return true;
    }
    if (id == "vs.toggleBookmark") {
        if (g.path.empty()) { Log("Save the graph to a file first: bookmarks are kept per graph file.", 1); return false; }
        if (sel.empty()) return false;
        for (int n : sel) {
            const VsEditNode* en = g.find(n);
            const VsDef* d = en ? VsDefFor(g, *en) : nullptr;
            Bookmarks().toggle(g.path, n, d && d->title ? d->title : en ? en->type : std::string());
        }
        SaveBookmarks();
        return true;
    }
    if (id == "vs.nextBookmark" || id == "vs.prevBookmark") {
        Bookmarks().prune(g.path, g);
        const int to = Bookmarks().next(g.path, st.selected, id == "vs.nextBookmark");
        if (to < 0) return false;
        st.sel = {to}; st.selected = to; st.focusNode = to;
        return true;
    }
    if (id == "vs.findReferences") {
        const VsEditNode* n = g.find(st.selected >= 0 ? st.selected : (sel.empty() ? -1 : *sel.begin()));
        if (!n) { Navigator().open = true; Navigator().focusSearch = true; Navigator().tab = 0; return false; }
        VsNavigatorSearch(VsRefQueryFor(*n), root);
        return true;
    }
    if (id == "vs.findInGraphs") { Navigator().open = true; Navigator().tab = 0; Navigator().focusSearch = true; Navigator().root = root; return true; }
    if (id == "vs.watchValues") {
        bool any = false;
        for (int n : sel) {
            const VsEditNode* en = g.find(n);
            const VsDef* d = en ? VsDefFor(g, *en) : nullptr;
            if (!d) continue;
            for (int p = 0; p < (int)d->dataOut.size(); ++p) { VsIde(st).watches.toggle(n, p); any = true; }
        }
        if (any) { Navigator().open = true; Navigator().tab = 2; }
        return any;
    }
    if (id == "vs.fixAll") {
        int fixed = 0;
        for (int round = 0; round < 8; ++round) {   // a fix can change what the checker finds: re-check between rounds
            const std::vector<okay::vs::VsIssue> issues = okay::vs::ValidateGraphText(g.toText(), "");
            const auto fixes = VsQuickFixesFor(g, issues);
            if (fixes.empty()) break;
            if (VsApplyQuickFix(g, fixes.front())) ++fixed; else break;
        }
        return fixed > 0;
    }
    return false;
}

/// True while a knot is being dragged (the canvas then does not pan or box-select).
inline bool VsIdeKnotBusy(const VsEditState& st) { return VsIde(st).knotDrag >= 0; }

/// Add a knot at stored-unit point (x, y) on the wire `ref` names (Shift+double-click on a wire).
inline void VsAddKnot(VsEditGraph& g, const VsWireRef& ref, float x, float y) {
    if (!ref.on) return;
    VsKnot k; k.exec = ref.exec; k.a = ref.a; k.b = ref.b; k.c = ref.exec ? ref.c : 0; k.x = x; k.y = y;
    g.knots.push_back(k);
    g.dirty = true;
}

/// What changed since the earlier save the Compare window is open on (empty when it is closed or on another graph).
inline const VsGraphDiff& VsIdeDiffNow(const VsEditState& st) {
    static const VsGraphDiff none;
    static std::string s_key; static VsGraphDiff s_diff;   // worked out again only when either side changes
    const VsDiffView& v = DiffView();
    if (!v.open || v.path != st.g.path) return none;
    std::string now = st.g.toText();
    std::string key = v.beforeText + '\x01' + now;
    if (key != s_key) { s_diff = VsDiffGraphTexts(v.beforeText, now); s_key = std::move(key); }
    return s_diff;
}

/// The points a wire passes through (screen), its knots in order along the wire. `toScreen` maps stored units.
template <class ToScreen>
inline auto VsKnotPoints(const VsEditGraph& g, bool exec, int a, int b, int c, ToScreen toScreen) {
    using P = decltype(toScreen(0.0f, 0.0f));   // the UI toolkit's point type
    std::vector<std::pair<float, P>> pts;
    for (const VsKnot& k : g.knots)
        if (k.exec == exec && k.a == a && k.b == b && (!exec || k.c == c)) pts.push_back({k.x, toScreen(k.x, k.y)});
    std::stable_sort(pts.begin(), pts.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    std::vector<P> out;
    for (const auto& p : pts) out.push_back(p.second);
    return out;
}

/// The wire being dragged (st.dragExec / dragFromOutput / dragNode / dragPin) let go on plug `idx` of block `node` (the other
/// side, the same kind): made when it fits. A flow wire into a starting block, a value wire that would go round in a circle or
/// whose types do not fit is refused with a plain sentence in st.refuseMsg (and, when a converter block would fix it, the offer).
/// True when a wire was made.
inline bool VsTryWire(VsEditState& st, int bestNode, int bestIdx) {
    VsEditGraph& g = st.g;
    const std::size_t wiresBefore = g.execLinks.size() + g.dataLinks.size();
    if (st.dragExec) {
        const int dest = st.dragFromOutput ? bestNode : st.dragNode;
        const VsDef* dd = nullptr;
        if (const VsEditNode* dn = g.find(dest)) dd = FindDef(dn->type);
        if (dd && !dd->execIn) {
            st.refuseMsg = VsPlainStartIntoStart();
            st.refuseTime = 5.0f; st.offerOn = false;
        } else if (st.dragFromOutput) g.linkExec(st.dragNode, st.dragPin, bestNode);
        else g.linkExec(bestNode, bestIdx, st.dragNode);
    } else {
        // Refuse a wire whose value can't work in that input (and say why).
        const int cons = st.dragFromOutput ? bestNode : st.dragNode, inP = st.dragFromOutput ? bestIdx : st.dragPin;
        const int prod = st.dragFromOutput ? st.dragNode : bestNode, outP = st.dragFromOutput ? st.dragPin : bestIdx;
        std::string why;
        std::vector<std::pair<int, int>> edges;
        for (const auto& l : g.dataLinks) edges.push_back({l.producer, l.consumer});
        if (VsDataWouldCycle(edges, prod, cons)) {
            st.refuseMsg = VsPlainWouldLoop();
            st.refuseTime = 5.0f; st.offerOn = false;
        } else if (VsWireOk(g, cons, inP, prod, outP, &why)) {
            g.linkData(cons, inP, prod, outP); st.offerOn = false;
        } else {
            const auto offer = VsOfferConvert((char)VsOutPinType(g, prod, outP), (char)VsInPinType(g, cons, inP));
            (void)why;
            st.refuseMsg = VsPlainMismatch(VsOutPinType(g, prod, outP), VsInPinType(g, cons, inP));
            if (offer.type.empty()) {   // no one-click fix: a plain tip when there is one
                const std::string tip = VsPlainMismatchTip(VsOutPinType(g, prod, outP), VsInPinType(g, cons, inP));
                if (!tip.empty()) st.refuseMsg += " " + tip;
            }
            st.refuseTime = 6.0f;
            st.offerOn = !offer.type.empty();
            st.offerCons = cons; st.offerIn = inP; st.offerProd = prod; st.offerOut = outP;
            st.offerType = offer.type; st.offerWhy = offer.label;
        }
    }
    return g.execLinks.size() + g.dataLinks.size() != wiresBefore;
}

/// A comment frame around the selected blocks, or (nothing selected) a small one centred on (cx, cy) in stored units; its name
/// is then being typed (st.renameComment). The new frame's index.
inline int VsCommentAround(VsEditState& st, float cx, float cy) {
    VsEditGraph& g = st.g;
    VsComment c;
    if (!st.sel.empty()) {   // frame the selection
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const auto& n : g.nodes) if (st.sel.count(n.id)) {
            x0 = std::min(x0, n.x); y0 = std::min(y0, n.y);
            x1 = std::max(x1, n.x + 230.0f); y1 = std::max(y1, n.y + 140.0f);
        }
        c.x = x0 - 24.0f; c.y = y0 - 42.0f; c.w = (x1 - x0) + 48.0f; c.h = (y1 - y0) + 64.0f;
    } else {
        c.x = cx - 110.0f; c.y = cy - 70.0f; c.w = 220.0f; c.h = 140.0f;
    }
    g.comments.push_back(c); g.dirty = true;
    st.renameComment = (int)g.comments.size() - 1;
    std::snprintf(st.renameBuf, sizeof st.renameBuf, "%s", c.text.c_str());
    return st.renameComment;
}

/// "Fix it for me" on a refused value wire: the converter block st.offerType goes between the two plugs. False when there is no offer.
inline bool VsAcceptConvertOffer(VsEditState& st) {
    if (!st.offerOn || st.offerType.empty()) return false;
    VsEditGraph& g = st.g;
    const VsEditNode* prod = g.find(st.offerProd);
    const VsEditNode* cons = g.find(st.offerCons);
    const float x = prod && cons ? (prod->x + cons->x) * 0.5f : 80.0f;
    const float y = prod && cons ? (prod->y + cons->y) * 0.5f + 40.0f : 80.0f;
    const int id = g.add(st.offerType, x, y);
    g.linkData(id, 0, st.offerProd, st.offerOut);
    g.linkData(st.offerCons, st.offerIn, id, 0);
    st.offerOn = false; st.refuseMsg.clear(); st.refuseTime = 0;
    return true;
}

/// Add a block picked in the Add Block list at st.paletteWorld: selected; put on the wire it was asked for (st.wireInsOn),
/// wired to the plug it was dragged from (st.linkPending), or joined to the selected block or the one added last when a plug
/// fits. The editor's palette and the Visual Script app's both add blocks this way. The new block's id (-1: refused).
inline int VsAddFromPalette(VsEditState& st, const VsDef& d, const std::string& arg = std::string()) {
    VsEditGraph& g = st.g;
    const int prevSel = st.selected;   // the block this one should join, when nothing else says where it goes
    int nid = g.add(d.type, st.paletteWorld.x, st.paletteWorld.y);
    if (!arg.empty()) if (VsEditNode* nn = g.find(nid)) nn->arg = arg;
    st.selected = nid; st.sel = {nid};
    if (st.wireInsOn) {
        VsWireRef ref; ref.on = true; ref.exec = st.wireInsExec; ref.a = st.wireInsA; ref.b = st.wireInsB; ref.c = st.wireInsC;
        st.wireInsOn = false;
        if (!VsSplitWire(g, ref, nid)) {
            g.removeNode(nid); st.sel.clear(); st.selected = -1;
            st.refuseMsg = VsPlainDoesNotFitWire();
            st.refuseTime = 4.0f; st.offerOn = false;
            return -1;
        }
    } else if (!st.linkPending) VsConnectAndPlace(g, nid, prevSel, st.lastUsed);
    st.lastUsed = nid;
    if (st.linkPending) {
        // The plug of exactly the dragged type first, then one that takes anything, then one that converts.
        VsConnectPendingWire(g, nid, st.linkExec, st.linkFromOutput, st.linkNode, st.linkPin);
        // Drop it beside the pin rather than under the cursor tip.
        if (VsEditNode* nn = g.find(nid)) { if (!st.linkFromOutput) nn->x -= 170.0f; }
        st.linkPending = false;
    } else {
        VsAutoHook(g, nid);   // joins the last node (or a close one) when it fits, so no wire to draw
    }
    return nid;
}

}  // namespace okay::editor::vsed
