// DockBuilder: a draft dock layout built in code, committed to a DockLayout (see okay/UI/OkayUIDock.hpp). No drawing, no input.
#include "okay/UI/OkayUIDock.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace OkayUI {

namespace {
const int kMaxId = 1 << 30;
float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
std::string Cut(const std::string& s) { return (int)s.size() > DockLayout::kMaxNameLen ? s.substr(0, (size_t)DockLayout::kMaxNameLen) : s; }
std::string Esc(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (c <= 0x20 || c == '%' || c == 0x7f) { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
        else o += (char)c;
    }
    return o;
}
float SafeRatio(float r) { return std::isfinite(r) ? ClampF(r, 0.01f, 0.99f) : 0.5f; }
} // namespace

DockBuilder::DockBuilder(const DockLayout& from) {
    nodes_.reserve(from.Nodes().size());
    int maxId = 0;
    for (const DockLayout::Node& n : from.Nodes()) {
        Node b;
        b.id = n.id; b.parent = n.parent; b.child[0] = n.child[0]; b.child[1] = n.child[1];
        b.horizontal = n.horizontal; b.ratio = n.ratio; b.windows = n.tabs; b.active = n.active;
        nodes_.push_back(b);
        maxId = std::max(maxId, n.id);
    }
    root_ = from.Root();
    nextId_ = std::min(maxId + 1, kMaxId);
    if (const DockLayout::Node* r = from.Find(root_)) {
        if (std::isfinite(r->rect.w) && std::isfinite(r->rect.h) && r->rect.w > 0 && r->rect.h > 0) rootSize_ = Vec2{r->rect.w, r->rect.h};
    }
}

const DockBuilder::Node* DockBuilder::Find(int id) const {
    if (id <= 0) return nullptr;
    for (const Node& n : nodes_) if (n.id == id) return &n;
    return nullptr;
}
DockBuilder::Node* DockBuilder::FindMut(int id) { return const_cast<Node*>(Find(id)); }

int DockBuilder::Fresh() {
    for (int tries = 0; tries <= (int)nodes_.size() + 1; ++tries) {
        if (nextId_ < 1 || nextId_ > kMaxId) nextId_ = 1;
        const int id = nextId_++;
        if (!Find(id)) return id;
    }
    return 0;
}

int DockBuilder::AddNode(int id) {
    nodes_.clear(); root_ = 0; nextId_ = 1;
    if (id < 0 || id > kMaxId) id = 0;
    if (id == 0) id = Fresh();
    Node n; n.id = id;
    nodes_.push_back(n);
    root_ = id;
    nextId_ = id < kMaxId ? id + 1 : 1;
    return id;
}

bool DockBuilder::RemoveNode(int id) {
    const Node* n = Find(id);
    if (!n) return false;
    if (id == root_) { nodes_.clear(); root_ = 0; return true; }
    const int parent = n->parent;
    const Node* p = Find(parent);
    if (!p) return false;
    const int sibling = p->child[0] == id ? p->child[1] : p->child[0];
    // the subtree of `id`
    std::vector<int> doomed, stack{id};
    while (!stack.empty() && doomed.size() <= nodes_.size()) {
        const int cur = stack.back(); stack.pop_back();
        const Node* c = Find(cur);
        if (!c) continue;
        doomed.push_back(cur);
        if (c->IsSplit()) { stack.push_back(c->child[0]); stack.push_back(c->child[1]); }
    }
    const int grand = p->parent;
    // the sibling takes the parent's place
    if (Node* s = FindMut(sibling)) s->parent = grand;
    if (grand == 0) root_ = sibling;
    else if (Node* g = FindMut(grand)) { for (int k = 0; k < 2; ++k) if (g->child[k] == parent) g->child[k] = sibling; }
    doomed.push_back(parent);
    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [&](const Node& x) { return std::find(doomed.begin(), doomed.end(), x.id) != doomed.end(); }), nodes_.end());
    return true;
}

Vec2 DockBuilder::GetNodeSize(int id) const {
    if (!Find(id)) return Vec2{};
    std::vector<int> path;   // id up to (not including) the root
    for (int cur = id; cur != root_ && (int)path.size() <= DockLayout::kMaxDepth + 2;) {
        const Node* n = Find(cur);
        if (!n || n->parent == 0) return Vec2{};
        path.push_back(cur);
        cur = n->parent;
    }
    Vec2 sz = rootSize_;
    for (size_t i = path.size(); i-- > 0;) {
        const Node* c = Find(path[i]);
        const Node* p = Find(c->parent);
        const float r = SafeRatio(p->ratio);
        const float share = p->child[0] == c->id ? r : 1.0f - r;
        if (p->horizontal) sz.x *= share; else sz.y *= share;
    }
    return sz;
}

bool DockBuilder::SetNodeSize(int id, Vec2 size) {
    Node* n = FindMut(id);
    if (!n || !std::isfinite(size.x) || !std::isfinite(size.y) || size.x <= 0 || size.y <= 0 || size.x > 1.0e7f || size.y > 1.0e7f) return false;
    if (id == root_) { rootSize_ = size; return true; }
    Node* p = FindMut(n->parent);
    if (!p) return false;
    const Vec2 ps = GetNodeSize(p->id);
    const float axis = p->horizontal ? ps.x : ps.y;
    if (!(axis > 0)) return false;
    const float want = p->horizontal ? size.x : size.y;
    const float frac = ClampF(want / axis, 0.01f, 0.99f);
    p->ratio = p->child[0] == id ? frac : 1.0f - frac;
    return true;
}

int DockBuilder::SplitNode(int node, DockSide side, float ratio, int* outSide, int* outOpposite) {
    if (outSide) *outSide = 0;
    if (outOpposite) *outOpposite = 0;
    Node* n = FindMut(node);
    if (!n || n->IsSplit() || side == DockSide_Center || side < 0 || side > DockSide_Bottom) return 0;
    if ((int)nodes_.size() + 2 > DockLayout::kMaxNodes) return 0;
    int depth = 0;
    for (const Node* c = n; c && c->parent && depth <= DockLayout::kMaxDepth + 2; c = Find(c->parent)) ++depth;
    if (depth + 1 >= DockLayout::kMaxDepth) return 0;
    ratio = SafeRatio(ratio);
    const int a = Fresh();
    const int b = Fresh();
    if (a == 0 || b == 0 || a == b) return 0;
    const bool newFirst = side == DockSide_Left || side == DockSide_Top;
    Node nn, old;
    nn.id = newFirst ? a : b;
    old.id = newFirst ? b : a;
    old.windows = n->windows; old.active = n->active;
    nn.parent = old.parent = node;
    n->windows.clear(); n->active = 0;
    n->horizontal = side == DockSide_Left || side == DockSide_Right;
    n->ratio = newFirst ? ratio : 1.0f - ratio;
    n->child[0] = newFirst ? nn.id : old.id;
    n->child[1] = newFirst ? old.id : nn.id;
    nodes_.push_back(nn);
    nodes_.push_back(old);
    if (outSide) *outSide = nn.id;
    if (outOpposite) *outOpposite = old.id;
    return nn.id;
}

bool DockBuilder::Detach(const std::string& window) {
    for (Node& n : nodes_) {
        auto it = std::find(n.windows.begin(), n.windows.end(), window);
        if (it == n.windows.end()) continue;
        const int idx = (int)(it - n.windows.begin());
        n.windows.erase(it);
        if (n.active > idx || n.active >= (int)n.windows.size()) n.active = std::max(0, std::min(n.active - (n.active > idx ? 1 : 0), (int)n.windows.size() - 1));
        return true;
    }
    return false;
}

bool DockBuilder::DockWindow(const std::string& window0, int node) {
    if (window0.empty()) return false;
    const std::string window = Cut(window0);
    Node* n = FindMut(node);
    if (!n || n->IsSplit()) return false;
    if (NodeOf(window) == 0 && (int)Windows().size() >= DockLayout::kMaxWindows) return false;
    if (NodeOf(window) == node) return true;
    Detach(window);
    n = FindMut(node);
    n->windows.push_back(window);
    if (n->active < 0 || n->active >= (int)n->windows.size()) n->active = 0;
    return true;
}

bool DockBuilder::UndockWindow(const std::string& window) { return Detach(Cut(window)); }

bool DockBuilder::SetActive(const std::string& window0) {
    const std::string window = Cut(window0);
    for (Node& n : nodes_) {
        auto it = std::find(n.windows.begin(), n.windows.end(), window);
        if (it != n.windows.end()) { n.active = (int)(it - n.windows.begin()); return true; }
    }
    return false;
}

int DockBuilder::NodeOf(const std::string& window) const {
    const std::string w = Cut(window);
    for (const Node& n : nodes_) if (std::find(n.windows.begin(), n.windows.end(), w) != n.windows.end()) return n.id;
    return 0;
}

std::vector<std::string> DockBuilder::Windows() const {
    std::vector<std::string> out;
    std::vector<int> stack{root_};
    int guard = 0;
    while (!stack.empty() && guard++ <= (int)nodes_.size() * 2 + 4) {
        const int id = stack.back(); stack.pop_back();
        const Node* n = Find(id);
        if (!n) continue;
        if (n->IsSplit()) { stack.push_back(n->child[1]); stack.push_back(n->child[0]); }
        else for (const std::string& w : n->windows) out.push_back(w);
    }
    return out;
}

std::string DockBuilder::Text() const {
    std::string s = "okaydock 1\n";
    // a node is live when it holds a window or has a live child; a split with one live child is replaced by that child
    struct Rec {
        const DockBuilder& b;
        bool Live(int id, int depth) const {
            const Node* n = b.Find(id);
            if (!n || depth > DockLayout::kMaxDepth + 2) return false;
            return n->IsSplit() ? (Live(n->child[0], depth + 1) || Live(n->child[1], depth + 1)) : !n->windows.empty();
        }
        int Effective(int id, int depth) const {   // the id that stands for this subtree after collapsing
            const Node* n = b.Find(id);
            if (!n || !n->IsSplit()) return id;
            const bool a = Live(n->child[0], depth + 1), c = Live(n->child[1], depth + 1);
            if (a && c) return id;
            return a ? Effective(n->child[0], depth + 1) : (c ? Effective(n->child[1], depth + 1) : id);
        }
        void Emit(int id, int depth, std::string& out) const {
            const Node* n = b.Find(id);
            if (!n || !Live(id, depth) || depth > DockLayout::kMaxDepth + 2) return;
            char buf[48];
            if (n->IsSplit()) {
                const bool a = Live(n->child[0], depth + 1), c = Live(n->child[1], depth + 1);
                if (!(a && c)) { Emit(a ? n->child[0] : n->child[1], depth + 1, out); return; }
                std::snprintf(buf, sizeof buf, "%.9g", (double)SafeRatio(n->ratio));
                out += "node " + std::to_string(n->id) + " split " + (n->horizontal ? "h " : "v ") + buf + " " +
                       std::to_string(Effective(n->child[0], depth + 1)) + " " + std::to_string(Effective(n->child[1], depth + 1)) + "\n";
                Emit(n->child[0], depth + 1, out);
                Emit(n->child[1], depth + 1, out);
            } else {
                out += "node " + std::to_string(n->id) + " leaf " + std::to_string(std::max(0, n->active));
                for (const std::string& w : n->windows) out += " " + Esc(w);
                out += "\n";
            }
        }
    } rec{*this};
    if (root_ != 0 && rec.Live(root_, 0)) {
        s += "root " + std::to_string(rec.Effective(root_, 0)) + "\n";
        rec.Emit(root_, 0, s);
    }
    return s;
}

bool DockBuilder::FromText(const std::string& text, int* repairs) {
    DockLayout tmp;
    if (!tmp.Load(text, repairs)) return false;
    *this = DockBuilder(tmp);
    return true;
}

bool DockBuilder::Finish(DockLayout& layout) const {
    DockLayout fresh;
    int repairs = 0;
    if (!fresh.Load(Text(), &repairs) || !fresh.Validate()) return false;
    // what the old layout had that the draft does not mention stays, floating
    struct Keep { std::string name; Rect rect; };
    std::vector<Keep> keep;
    for (const std::string& w : layout.Windows()) {
        if (fresh.Contains(w)) continue;
        Keep k; k.name = w; k.rect = Rect{40.0f, 40.0f, 320.0f, 240.0f};
        if (const DockLayout::Floating* f = layout.FindFloating(w)) k.rect = f->rect;
        else if (const DockLayout::Node* n = layout.Find(layout.NodeOf(w))) k.rect = Rect{n->rect.x + 24.0f, n->rect.y + 24.0f, std::max(n->rect.w, 160.0f), std::max(n->rect.h, 120.0f)};
        keep.push_back(k);
    }
    for (const Keep& k : keep) fresh.AddFloating(k.name, k.rect);
    if (!fresh.Validate()) return false;
    return layout.Load(fresh.Save());
}

bool DockBuilder::Finish() const { return Finish(GetDockLayout()); }

// ---- the context draft behind the DockBuilder* names ----------------------------------------------------------------------------------
namespace {
DockBuilder g_draft;
bool        g_inProgress = false;
DockBuilder g_snap;                  // the live layout as a draft, for DockBuilderGetNode when nothing is being built
unsigned    g_snapVersion = ~0u;
void Ensure() { if (!g_inProgress) { g_draft = DockBuilder(GetDockLayout()); g_inProgress = true; } }
} // namespace

void DockBuilderReset() { g_draft = DockBuilder(); g_inProgress = false; g_snap = DockBuilder(); g_snapVersion = ~0u; }
int  DockBuilderAddNode(int id, int) { g_inProgress = true; return g_draft.AddNode(id); }
void DockBuilderRemoveNode(int id) {
    Ensure();
    if (id == g_draft.Root() || id == 0) g_draft.RemoveNode(g_draft.Root());
    else g_draft.RemoveNode(id);
}
void DockBuilderSetNodeSize(int id, Vec2 size) { Ensure(); g_draft.SetNodeSize(id, size); }
int  DockBuilderSplitNode(int node, DockSide dir, float ratio, int* outAtDir, int* outAtOpposite) { Ensure(); return g_draft.SplitNode(node, dir, ratio, outAtDir, outAtOpposite); }
void DockBuilderDockWindow(const char* window, int node) { Ensure(); if (window) g_draft.DockWindow(window, node); }
void DockBuilderFinish(int) { if (!g_inProgress) return; g_draft.Finish(); g_inProgress = false; }
const DockBuilder::Node* DockBuilderGetNode(int id) {
    if (g_inProgress) return g_draft.GetNode(id);
    const DockLayout& l = GetDockLayout();
    if (g_snapVersion != l.Version()) { g_snap = DockBuilder(l); g_snapVersion = l.Version(); }
    return g_snap.GetNode(id);
}

} // namespace OkayUI
