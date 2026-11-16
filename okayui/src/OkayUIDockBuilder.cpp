#include "okay/UI/OkayUIDockBuilder.hpp"

#include <algorithm>
#include <cmath>

namespace OkayUI {

DockBuilderNode* DockBuilderTree::Mut(unsigned id) {
    for (DockBuilderNode& n : nodes_) if (n.id == id) return &n;
    return nullptr;
}
const DockBuilderNode* DockBuilderTree::GetNode(unsigned id) const {
    for (const DockBuilderNode& n : nodes_) if (n.id == id) return &n;
    return nullptr;
}
unsigned DockBuilderTree::NewId() {
    while (next_ == 0 || GetNode(next_)) ++next_;
    return next_++;
}

void DockBuilderTree::RemoveRec(unsigned id) {
    const DockBuilderNode* n = GetNode(id);
    if (!n) return;
    const unsigned c0 = n->child[0], c1 = n->child[1];
    if (c0) RemoveRec(c0);
    if (c1) RemoveRec(c1);
    for (size_t i = 0; i < nodes_.size(); ++i) if (nodes_[i].id == id) { nodes_.erase(nodes_.begin() + (long)i); break; }
}

unsigned DockBuilderTree::AddNode(unsigned id) {
    if (id == 0) id = NewId();
    if (GetNode(id)) RemoveNode(id);
    if ((int)nodes_.size() >= kMaxNodes) return 0;
    DockBuilderNode n; n.id = id;
    nodes_.push_back(n);
    return id;
}

void DockBuilderTree::RemoveNode(unsigned id) {
    const DockBuilderNode* n = GetNode(id);
    if (!n) return;
    const unsigned parent = n->parent;
    RemoveRec(id);
    if (!parent) return;
    DockBuilderNode* p = Mut(parent);
    if (!p) return;
    const unsigned sib = p->child[0] == id ? p->child[1] : p->child[0];
    // the sibling takes the parent's place: the parent becomes what the sibling is
    DockBuilderNode* s = Mut(sib);
    if (!s) { p->child[0] = p->child[1] = 0; return; }
    const DockBuilderNode sc = *s;
    p->child[0] = sc.child[0]; p->child[1] = sc.child[1]; p->dir = sc.dir; p->ratio = sc.ratio; p->windows = sc.windows;
    for (int k = 0; k < 2; ++k) if (p->child[k]) { if (DockBuilderNode* c = Mut(p->child[k])) c->parent = p->id; }
    for (size_t i = 0; i < nodes_.size(); ++i) if (nodes_[i].id == sib) { nodes_.erase(nodes_.begin() + (long)i); break; }
}

void DockBuilderTree::SetNodeSize(unsigned id, Vec2 size) {
    if (DockBuilderNode* n = Mut(id)) {
        n->size = Vec2{ std::isfinite(size.x) ? size.x : 0.0f, std::isfinite(size.y) ? size.y : 0.0f };
    }
}

unsigned DockBuilderTree::SplitNode(unsigned node, int dir, float ratio, unsigned* outSide, unsigned* outOpposite) {
    if (outSide) *outSide = 0;
    if (outOpposite) *outOpposite = 0;
    if (!GetNode(node) || dir < Dir_Left || dir > Dir_Down || (int)nodes_.size() + 2 > kMaxNodes) return 0;
    if (!std::isfinite(ratio)) ratio = 0.5f;
    ratio = std::min(0.95f, std::max(0.05f, ratio));
    const unsigned sideId = NewId();
    DockBuilderNode side; side.id = sideId; side.parent = node;
    const unsigned oppId = NewId();
    DockBuilderNode opp = *GetNode(node);   // the opposite part takes over what the node held
    opp.id = oppId; opp.parent = node; opp.size = Vec2{};
    nodes_.push_back(side);
    nodes_.push_back(opp);
    for (int k = 0; k < 2; ++k) if (opp.child[k]) { if (DockBuilderNode* c = Mut(opp.child[k])) c->parent = oppId; }
    DockBuilderNode* n = Mut(node);
    n->windows.clear();
    n->child[0] = sideId; n->child[1] = oppId; n->dir = dir; n->ratio = ratio;
    if (outSide) *outSide = sideId;
    if (outOpposite) *outOpposite = oppId;
    return sideId;
}

bool DockBuilderTree::DockWindow(const std::string& window, unsigned node) {
    if (window.empty()) return false;
    DockBuilderNode* n = Mut(node);
    if (!n || n->IsSplit()) return false;
    for (DockBuilderNode& o : nodes_) {
        auto it = std::find(o.windows.begin(), o.windows.end(), window);
        if (it != o.windows.end()) { o.windows.erase(it); break; }
    }
    n = Mut(node);
    n->windows.push_back(window);
    return true;
}

unsigned DockBuilderTree::NodeOfWindow(const std::string& window) const {
    for (const DockBuilderNode& n : nodes_) if (std::find(n.windows.begin(), n.windows.end(), window) != n.windows.end()) return n.id;
    return 0;
}

bool DockBuilderTree::Validate(std::string* why) const {
    auto bad = [&](const char* m) { if (why) *why = m; return false; };
    if ((int)nodes_.size() > kMaxNodes) return bad("too many nodes");
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const DockBuilderNode& n = nodes_[i];
        if (n.id == 0) return bad("node id 0");
        for (size_t j = i + 1; j < nodes_.size(); ++j) if (nodes_[j].id == n.id) return bad("duplicate id");
        if ((n.child[0] == 0) != (n.child[1] == 0)) return bad("half a split");
        if (n.IsSplit()) {
            if (!n.windows.empty()) return bad("a split holds windows");
            if (!(n.ratio > 0.0f && n.ratio < 1.0f)) return bad("bad ratio");
            for (int k = 0; k < 2; ++k) { const DockBuilderNode* c = GetNode(n.child[k]); if (!c || c->parent != n.id) return bad("bad child link"); }
        }
        if (n.parent) { const DockBuilderNode* p = GetNode(n.parent); if (!p || (p->child[0] != n.id && p->child[1] != n.id)) return bad("bad parent link"); }
    }
    for (size_t i = 0; i < nodes_.size(); ++i)
        for (const std::string& w : nodes_[i].windows)
            for (size_t j = i; j < nodes_.size(); ++j) {
                int count = 0;
                for (const std::string& v : nodes_[j].windows) if (v == w) ++count;
                if (j == i ? count > 1 : count > 0) return bad("a window in two places");
            }
    return true;
}

namespace {
const std::string* FirstWin(const DockBuilderTree& t, unsigned id, int depth) {
    const DockBuilderNode* n = t.GetNode(id);
    if (!n || depth > 64) return nullptr;
    if (!n->IsSplit()) return n->windows.empty() ? nullptr : &n->windows[0];
    if (const std::string* w = FirstWin(t, n->child[1], depth + 1)) return w;   // the opposite part's first window holds the leaf
    return FirstWin(t, n->child[0], depth + 1);
}
DockSide SideOf(int dir) {
    switch (dir) { case Dir_Left: return DockSide_Left; case Dir_Up: return DockSide_Top; case Dir_Down: return DockSide_Bottom; default: return DockSide_Right; }
}
}

// Fill the layout under `id`, whose first window already sits in `leaf`.
bool DockBuilderTree::Realize(unsigned id, int leaf, DockLayout& out, int depth) const {
    const DockBuilderNode* n = GetNode(id);
    if (!n || depth > 64) return false;
    if (!n->IsSplit()) {
        for (size_t i = 1; i < n->windows.size(); ++i) out.Dock(n->windows[i], leaf, DockSide_Center);
        if (!n->windows.empty()) out.SetActive(n->windows[0]);
        return true;
    }
    const std::string* wa = FirstWin(*this, n->child[0], depth + 1);
    const std::string* wb = FirstWin(*this, n->child[1], depth + 1);
    if (!wa) return Realize(n->child[1], leaf, out, depth + 1);        // nothing on the side: the opposite part is the whole node
    if (!wb) return Realize(n->child[0], leaf, out, depth + 1);
    if (!out.Dock(*wa, leaf, SideOf(n->dir))) return false;
    const int sideLeaf = out.NodeOf(*wa);
    const DockLayout::Node* sl = out.Find(sideLeaf);
    if (!sl) return false;
    const int split = sl->parent;
    const bool sideFirst = (n->dir == Dir_Left || n->dir == Dir_Up);
    out.SetRatio(split, sideFirst ? n->ratio : 1.0f - n->ratio);
    return Realize(n->child[0], sideLeaf, out, depth + 1) && Realize(n->child[1], leaf, out, depth + 1);
}

bool DockBuilderTree::Build(unsigned root, DockLayout& out) const {
    if (!GetNode(root)) return false;
    DockLayout tmp;
    const std::string* first = FirstWin(*this, root, 0);
    if (first) {
        if (!tmp.AddFloating(*first, Rect{}) || !tmp.Dock(*first, tmp.Root(), DockSide_Center)) return false;
        if (!Realize(root, tmp.Root(), tmp, 0)) return false;
    }
    if (!tmp.Validate()) return false;
    out = tmp;
    return true;
}

// ---- the context calls -----------------------------------------------------------------------------------------------
namespace { DockBuilderTree g_builder; }
DockBuilderTree& GetDockBuilderTree() { return g_builder; }
unsigned DockBuilderAddNode(unsigned id, int) { return g_builder.AddNode(id); }
void DockBuilderRemoveNode(unsigned id) { g_builder.RemoveNode(id); }
void DockBuilderSetNodeSize(unsigned id, Vec2 size) { g_builder.SetNodeSize(id, size); }
unsigned DockBuilderSplitNode(unsigned node, int dir, float ratio, unsigned* outSide, unsigned* outOpposite) { return g_builder.SplitNode(node, dir, ratio, outSide, outOpposite); }
void DockBuilderDockWindow(const char* window, unsigned node) { if (window) g_builder.DockWindow(window, node); }
const DockBuilderNode* DockBuilderGetNode(unsigned id) { return g_builder.GetNode(id); }
void DockBuilderFinish(unsigned root) { g_builder.Build(root, GetDockLayout()); }

} // namespace OkayUI
