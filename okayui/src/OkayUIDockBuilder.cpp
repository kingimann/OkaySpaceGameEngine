// DockBuilder subset and SetNextWindowDockID over the OkayUI dock layout (see OkayUIDockBuilder.hpp for the design).
// Written from what the calls are for: a plan tree of nodes with caller-chosen ids that is turned into a DockLayout text blob
// (the layout's own save format) and loaded, so the layout's checks and repairs apply to whatever the plan holds.
#include "okay/UI/OkayUIDockBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace OkayUI {

namespace {

struct PNode {
    DockId id = 0, parent = 0, child[2] = {0, 0};
    bool split = false, horizontal = true, finished = false, wantSet = false;
    float ratio = 0.5f;                  // split: share of child[0]
    std::vector<std::string> windows;    // leaf
    Vec2 want{0, 0};                     // SetNodeSize, as given
    Vec2 size{0, 0};                     // computed at Finish from the root size
    int layoutNode = 0;                  // the DockLayout node it became (0: none, dropped as empty)
};
struct State {
    std::vector<PNode> nodes;
    DockId nextAuto = 0x0D0C0001u;
    bool pending = false; DockId pendId = 0; Cond pendCond = Cond_Always;
    DockId spaceId = 0;                  // SetDockSpaceId
};
State& S() { static State s; return s; }

PNode* Find(DockId id) { if (id == 0) return nullptr; for (PNode& n : S().nodes) if (n.id == id) return &n; return nullptr; }
DockId NewId() { State& s = S(); while (s.nextAuto == 0 || Find(s.nextAuto)) ++s.nextAuto; return s.nextAuto++; }
DockId RootOf(DockId id) {
    for (int guard = 0; guard < 128; ++guard) { const PNode* n = Find(id); if (!n) return 0; if (n->parent == 0) return n->id; id = n->parent; }
    return 0;
}
float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
bool Good(float v) { return std::isfinite(v) && std::fabs(v) < 1.0e7f; }

std::string CutName(const char* w) {
    std::string s = w ? w : "";
    if ((int)s.size() > DockLayout::kMaxNameLen) s.resize((size_t)DockLayout::kMaxNameLen);
    return s;
}
std::string EscapeName(const std::string& s) {   // the layout text format: bytes <= 0x20, '%' and 0x7f as %XX
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) { if (c <= 0x20 || c == '%' || c == 0x7f) { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; } else o += (char)c; }
    return o;
}
DockId FirstLeaf(DockId id) {
    for (int guard = 0; guard < 128; ++guard) { const PNode* n = Find(id); if (!n) return 0; if (!n->split) return n->id; id = n->child[0]; }
    return 0;
}
void Collect(DockId id, std::vector<DockId>& out, int depth = 0) {
    const PNode* n = Find(id);
    if (!n || depth > 128) return;
    out.push_back(id);
    if (n->split) { const DockId a = n->child[0], b = n->child[1]; Collect(a, out, depth + 1); Collect(b, out, depth + 1); }
}
void RemoveWindowEverywhere(const std::string& w) {
    for (PNode& n : S().nodes) n.windows.erase(std::remove(n.windows.begin(), n.windows.end(), w), n.windows.end());
}
/// The DockLayout leaf a plan node stands for now (the first leaf under it when it is a split), 0 when it is not in the layout.
int LiveLeaf(const PNode& n) {
    if (n.layoutNode <= 0) { return 0; }
    const DockLayout& L = GetDockLayout();
    const DockLayout::Node* ln = L.Find(n.layoutNode);
    for (int guard = 0; ln && guard < 128; ++guard) { if (!ln->IsSplit()) return ln->id; ln = L.Find(ln->child[0]); }
    return 0;
}

int EmitNode(DockId id, Vec2 size, bool known, std::vector<std::string>& lines, int& next, int depth) {
    PNode* n = Find(id);
    if (!n || depth > 64) return 0;
    if (known) n->size = size;
    n->layoutNode = 0;
    char buf[96];
    if (!n->split) {
        if (n->windows.empty()) return 0;
        const int lid = next++;
        std::string s = "node " + std::to_string(lid) + " leaf 0";
        for (const std::string& w : n->windows) s += " " + EscapeName(w);
        lines.push_back(s);
        n->layoutNode = lid;
        return lid;
    }
    float r = n->ratio;
    Vec2 s0{0, 0}, s1{0, 0};
    bool k = false;
    const bool horiz = n->horizontal;
    const DockId a = n->child[0], b = n->child[1];
    if (known) {
        const float total = (horiz ? size.x : size.y) - DockLayout::kSplitter;
        if (total > 1.0f) {
            const PNode* c0 = Find(a); const PNode* c1 = Find(b);
            if (c0 && c0->wantSet) r = (horiz ? c0->want.x : c0->want.y) / total;
            else if (c1 && c1->wantSet) r = 1.0f - (horiz ? c1->want.x : c1->want.y) / total;
            r = Clamp(Good(r) ? r : 0.5f, 0.02f, 0.98f);
            s0 = horiz ? Vec2{total * r, size.y} : Vec2{size.x, total * r};
            s1 = horiz ? Vec2{total * (1.0f - r), size.y} : Vec2{size.x, total * (1.0f - r)};
            k = true;
        }
    }
    n->ratio = r;
    const int lid = next++;
    const int l0 = EmitNode(a, s0, k, lines, next, depth + 1);
    const int l1 = EmitNode(b, s1, k, lines, next, depth + 1);
    n = Find(id);   // (the vector does not grow while emitting, but stay safe)
    if (!n) return 0;
    if (l0 == 0 && l1 == 0) return 0;
    if (l0 == 0 || l1 == 0) { n->layoutNode = l0 ? l0 : l1; return n->layoutNode; }   // an empty half is dropped; the other takes the space
    std::snprintf(buf, sizeof buf, "%.9g", (double)r);
    lines.push_back("node " + std::to_string(lid) + " split " + (horiz ? "h " : "v ") + buf + " " + std::to_string(l0) + " " + std::to_string(l1));
    n->layoutNode = lid;
    return lid;
}

} // namespace

DockId DockBuilderAddNode(DockId id, int) {
    if (id == 0) id = NewId();
    else if (Find(id)) return 0;
    PNode n; n.id = id;
    S().nodes.push_back(n);
    return id;
}

void DockBuilderRemoveNode(DockId id) {
    PNode* n = Find(id);
    if (!n) return;
    const bool wasFinished = n->finished;
    const DockId parent = n->parent;
    std::vector<DockId> gone; Collect(id, gone);
    if (parent == 0) {
        S().nodes.erase(std::remove_if(S().nodes.begin(), S().nodes.end(), [&](const PNode& p) { return std::find(gone.begin(), gone.end(), p.id) != gone.end(); }), S().nodes.end());
        if (wasFinished) { GetDockLayout().Clear(); }
        return;
    }
    PNode* p = Find(parent);
    if (!p) return;
    const DockId sibId = p->child[0] == id ? p->child[1] : p->child[0], grand = p->parent;
    PNode* sib = Find(sibId);
    if (sib) sib->parent = grand;
    if (PNode* g = Find(grand)) { if (g->child[0] == parent) g->child[0] = sibId; else if (g->child[1] == parent) g->child[1] = sibId; }
    gone.push_back(parent);
    S().nodes.erase(std::remove_if(S().nodes.begin(), S().nodes.end(), [&](const PNode& q) { return std::find(gone.begin(), gone.end(), q.id) != gone.end(); }), S().nodes.end());
}

bool SetLiveNodeSize(int lid, Vec2 size) {
    if (lid <= 0 || !Good(size.x) || !Good(size.y) || size.x < 0.0f || size.y < 0.0f) return false;
    DockLayout& L = GetDockLayout();
    const DockLayout::Node* ln = L.Find(lid);
    if (!ln || ln->parent == 0) return false;                 // the root: the area is the host's (DockSpace)
    const DockLayout::Node* pn = L.Find(ln->parent);
    if (!pn || !pn->IsSplit() || !(pn->rect.w > 0.0f)) return false;   // not laid out yet
    const float total = (pn->horizontal ? pn->rect.w : pn->rect.h) - DockLayout::kSplitter;
    if (!(total > 1.0f)) return false;
    const float share = (pn->horizontal ? size.x : size.y) / total;
    return L.SetRatio(pn->id, pn->child[0] == lid ? share : 1.0f - share);
}

void DockBuilderSetNodeSize(DockId id, Vec2 size) {
    PNode* n = Find(id);
    if (!n) { SetLiveNodeSize(DockLayoutNodeOf(id), size); return; }   // a node the user made, or the DockSpace root
    if (!Good(size.x) || !Good(size.y) || size.x < 0.0f || size.y < 0.0f) return;
    n->want = size; n->wantSet = true;
    const int lid = LiveLeaf(*n) ? n->layoutNode : 0;
    if (lid <= 0) return;
    SetLiveNodeSize(lid, size);
}

DockId DockBuilderSplitNode(DockId id, DockSide dir, float sizeRatio, DockId* outAt, DockId* outOpp) {
    PNode* n = Find(id);
    if (!n || n->split || dir == DockSide_Center || !Good(sizeRatio)) return 0;
    const float r = Clamp(sizeRatio, 0.02f, 0.98f);
    const bool newFirst = dir == DockSide_Left || dir == DockSide_Top;
    const DockId newId = NewId(), restId = NewId();   // the new node and the remaining one (their order in the tree depends on `dir`)
    PNode first, second;
    PNode& fresh = newFirst ? first : second; PNode& rest = newFirst ? second : first;
    fresh.id = newId; rest.id = restId;
    rest.windows = n->windows;            // windows already in the node stay on the remaining side
    rest.wantSet = false;
    first.parent = second.parent = id;
    n->windows.clear(); n->split = true;
    n->horizontal = (dir == DockSide_Left || dir == DockSide_Right);
    n->ratio = newFirst ? r : 1.0f - r;
    n->child[0] = first.id; n->child[1] = second.id;
    n->layoutNode = 0;
    S().nodes.push_back(first); S().nodes.push_back(second);
    if (outAt) *outAt = newId;
    if (outOpp) *outOpp = restId;
    return newId;
}

void DockBuilderDockWindow(const char* window, DockId node) {
    if (!window || !*window) return;
    PNode* n = Find(FirstLeaf(node));
    if (!n) {   // a node of the live layout that no plan made (user split, or the DockSpace root)
        DockLayout& L = GetDockLayout();
        const DockLayout::Node* ln = L.Find(DockLayoutNodeOf(node));
        for (int guard = 0; ln && guard < 128 && ln->IsSplit(); ++guard) ln = L.Find(ln->child[0]);
        if (ln) L.Dock(CutName(window), ln->id, DockSide_Center);
        return;
    }
    const std::string w = CutName(window);
    RemoveWindowEverywhere(w);
    n = Find(FirstLeaf(node));
    n->windows.push_back(w);
    if (n->finished) {
        const int leaf = LiveLeaf(*n);
        if (leaf > 0) GetDockLayout().Dock(w, leaf, DockSide_Center);
    }
}

bool DockBuilderFinish(DockId id) {
    const DockId rootId = RootOf(id);
    PNode* root = Find(rootId);
    if (!root) return false;
    std::vector<std::string> lines;
    int next = 1;
    const bool known = root->wantSet && root->want.x > 0.0f && root->want.y > 0.0f;
    const int rootLayout = EmitNode(rootId, root->want, known, lines, next, 0);
    std::vector<DockId> tree; Collect(rootId, tree);
    if (rootLayout == 0) return false;
    // windows the layout holds that this plan does not name stay, floating where they were
    DockLayout& L = GetDockLayout();
    std::vector<std::string> planned;
    for (const DockId t : tree) if (const PNode* p = Find(t)) planned.insert(planned.end(), p->windows.begin(), p->windows.end());
    struct Keep { std::string name; Rect rect; };
    std::vector<Keep> keep;
    for (const std::string& w : L.Windows()) {
        if (std::find(planned.begin(), planned.end(), w) != planned.end()) continue;
        Rect r{60.0f, 60.0f, 320.0f, 240.0f};
        if (const DockLayout::Floating* f = L.FindFloating(w)) r = f->rect;
        else if (const DockLayout::Node* ln = L.Find(L.NodeOf(w))) { if (ln->rect.w > 0.0f) r = Rect{ln->rect.x + 30.0f, ln->rect.y + 30.0f, 320.0f, 240.0f}; }
        keep.push_back({w, r});
    }
    std::string blob = "okaydock 1\nroot " + std::to_string(rootLayout) + "\n";
    for (const std::string& l : lines) blob += l + "\n";
    if (!DockLoadLayout(blob, nullptr)) return false;
    for (const Keep& k : keep) GetDockLayout().AddFloating(k.name, k.rect);
    for (const DockId t : tree) if (PNode* p = Find(t)) p->finished = true;
    return true;
}

DockNodeInfo DockBuilderGetNodeInfo(DockId id) {
    DockNodeInfo info;
    const PNode* n = Find(id);
    if (!n) return info;
    info.found = true; info.isRoot = n->parent == 0; info.parent = n->parent;
    info.isSplit = n->split; info.isLeaf = !n->split;
    info.child[0] = n->child[0]; info.child[1] = n->child[1];
    info.horizontal = n->horizontal; info.ratio = n->ratio; info.size = n->size;
    info.windows = n->windows;
    if (n->finished && n->layoutNode > 0) {
        const DockLayout& L = GetDockLayout();
        if (const DockLayout::Node* ln = L.Find(n->layoutNode)) {
            info.live = true;
            if (ln->rect.w > 0.0f && ln->rect.h > 0.0f) info.size = Vec2{ln->rect.w, ln->rect.h};
            if (!ln->IsSplit()) info.windows = ln->tabs;
            if (ln->IsSplit()) info.ratio = ln->ratio;
        }
    }
    return info;
}

void SetNextWindowDockID(DockId id, Cond cond) { State& s = S(); s.pending = true; s.pendId = id; s.pendCond = cond; }

DockId GetWindowDockID(const char* window) {
    if (!window || !*window) return 0;
    const int ln = GetDockLayout().NodeOf(CutName(window));
    if (ln == 0) return 0;
    return DockNodeIdOf(ln);
}

DockId DockNodeIdOf(int ln) {
    if (ln <= 0) return 0;
    const State& s = S();
    if (s.spaceId != 0 && GetDockLayout().Root() == ln) return s.spaceId;   // the id the host gave DockSpace wins over a plan's id for the same node
    for (const PNode& n : s.nodes) if (n.finished && n.layoutNode == ln) return n.id;
    return kDockDerivedBase + (DockId)ln;
}
int DockLayoutNodeOf(DockId id) {
    if (id == 0) return 0;
    const State& s = S();
    const DockLayout& L = GetDockLayout();
    for (const PNode& n : s.nodes) if (n.id == id) return n.finished && n.layoutNode > 0 && L.Find(n.layoutNode) ? n.layoutNode : 0;
    if (s.spaceId == id) return L.Root();
    if (id > kDockDerivedBase && id <= kDockDerivedBase + (DockId)DockLayout::kMaxNodes * 64u) { const int ln = (int)(id - kDockDerivedBase); return L.Find(ln) ? ln : 0; }
    return 0;
}
void SetDockSpaceId(DockId id) { S().spaceId = id; }

void DockBuilderDropNext() { S().pending = false; }

void DockBuilderApplyNext(const std::string& name) {
    State& s = S();
    if (!s.pending) return;
    s.pending = false;
    DockLayout& L = GetDockLayout();
    const bool in = L.Contains(name);
    if (s.pendCond != Cond_Always && in) return;
    if (s.pendId == 0) {
        if (in && !L.IsFloating(name)) {
            Rect r{60.0f, 60.0f, 320.0f, 240.0f};
            if (const DockLayout::Node* ln = L.Find(L.NodeOf(name))) if (ln->rect.w > 0.0f) r = Rect{ln->rect.x + 30.0f, ln->rect.y + 30.0f, 320.0f, 240.0f};
            L.Float(name, r);
        }
        return;
    }
    int leaf = 0;
    if (const PNode* n = Find(s.pendId)) leaf = LiveLeaf(*n);
    else {   // a node of the live layout that no plan made
        const DockLayout::Node* ln = L.Find(DockLayoutNodeOf(s.pendId));
        for (int guard = 0; ln && guard < 128 && ln->IsSplit(); ++guard) ln = L.Find(ln->child[0]);
        if (ln) leaf = ln->id;
    }
    if (leaf <= 0 || L.NodeOf(name) == leaf) return;
    L.Dock(name, leaf, DockSide_Center);
}

void DockBuilderReset() { State& s = S(); s = State(); }

} // namespace OkayUI
