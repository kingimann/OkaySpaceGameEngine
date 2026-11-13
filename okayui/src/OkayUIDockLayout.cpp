// DockLayout: the data model behind OkayUI docking (see okay/UI/OkayUIDock.hpp). No drawing, no input.
#include "okay/UI/OkayUIDock.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace OkayUI {

namespace {
bool Finite(float v) { return std::isfinite(v) && std::fabs(v) < 1.0e9f; }
bool GoodName(const std::string& s) { return !s.empty() && (int)s.size() <= DockLayout::kMaxNameLen; }
std::string CutName(const std::string& s) { return (int)s.size() > DockLayout::kMaxNameLen ? s.substr(0, (size_t)DockLayout::kMaxNameLen) : s; }
float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

std::string Escape(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (c <= 0x20 || c == '%' || c == 0x7f) { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
        else o += (char)c;
    }
    return o;
}
int HexVal(char c) { return c >= '0' && c <= '9' ? c - '0' : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1)); }
std::string Unescape(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() + 0 && HexVal(s[i + 1]) >= 0 && HexVal(s[i + 2]) >= 0) { o += (char)(HexVal(s[i + 1]) * 16 + HexVal(s[i + 2])); i += 2; }
        else o += s[i];
    }
    return o;
}
bool ParseFloat(const std::string& t, float* out) {
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str() || *end) return false;
    if (!std::isfinite(v) || std::fabs(v) > 1.0e9) return false;
    *out = (float)v;
    return true;
}
bool ParseInt(const std::string& t, int* out) {
    char* end = nullptr;
    const long long v = std::strtoll(t.c_str(), &end, 10);
    if (end == t.c_str() || *end || v < 1 || v > (1 << 30)) return false;
    *out = (int)v;
    return true;
}
Rect SaneRect(Rect r, int stagger) {
    if (!Finite(r.x) || !Finite(r.y)) { r.x = 60.0f + 24.0f * (float)(stagger % 10); r.y = 60.0f + 24.0f * (float)(stagger % 10); }
    if (!Finite(r.w) || !Finite(r.h)) { r.w = 320.0f; r.h = 240.0f; }
    r.x = Clamp(r.x, -1.0e6f, 1.0e6f); r.y = Clamp(r.y, -1.0e6f, 1.0e6f);
    r.w = Clamp(r.w, 32.0f, 20000.0f); r.h = Clamp(r.h, 32.0f, 20000.0f);
    return r;
}
} // namespace

struct DockLayout::Raw {
    struct N { int id = 0; bool split = false, horiz = true; float ratio = 0.5f; int c0 = 0, c1 = 0; int active = 0; std::vector<std::string> tabs; };
    struct F { std::string name; Rect rect; };
    int root = 0;
    std::vector<N> nodes;
    std::vector<F> floats;
};

DockLayout::DockLayout() { Clear(); }

void DockLayout::Clear() {
    nodes_.clear(); floating_.clear(); mins_.clear();
    Node n; n.id = 1;
    nodes_.push_back(n);
    root_ = 1; nextId_ = 2;
    ++version_;
}

int DockLayout::FindIndex(int id) const {
    for (size_t i = 0; i < nodes_.size(); ++i) if (nodes_[i].id == id) return (int)i;
    return -1;
}
const DockLayout::Node* DockLayout::Find(int id) const { const int i = FindIndex(id); return i < 0 ? nullptr : &nodes_[(size_t)i]; }
DockLayout::Node* DockLayout::Mut(int id) { const int i = FindIndex(id); return i < 0 ? nullptr : &nodes_[(size_t)i]; }
int DockLayout::NewId() { return nextId_++; }

int DockLayout::NodeOf(const std::string& w) const {
    for (const Node& n : nodes_) for (const std::string& t : n.tabs) if (t == w) return n.id;
    return 0;
}
const DockLayout::Floating* DockLayout::FindFloating(const std::string& w) const {
    for (const Floating& f : floating_) if (f.name == w) return &f;
    return nullptr;
}
bool DockLayout::IsFloating(const std::string& w) const { return FindFloating(w) != nullptr; }
bool DockLayout::Contains(const std::string& w) const { return NodeOf(w) != 0 || IsFloating(w); }
int DockLayout::WindowCount() const {
    int n = (int)floating_.size();
    for (const Node& nd : nodes_) n += (int)nd.tabs.size();
    return n;
}
std::vector<std::string> DockLayout::Windows() const {
    std::vector<std::string> out;
    // tree order from the root (guarded against a broken tree), then anything left in nodes_, then floating
    std::vector<int> stack{root_};
    std::vector<int> seen;
    while (!stack.empty() && seen.size() <= nodes_.size()) {
        const int id = stack.back(); stack.pop_back();
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) continue;
        const Node* n = Find(id);
        if (!n) continue;
        seen.push_back(id);
        if (n->IsSplit()) { stack.push_back(n->child[1]); stack.push_back(n->child[0]); }
        else for (const std::string& t : n->tabs) out.push_back(t);
    }
    for (const Node& n : nodes_) if (std::find(seen.begin(), seen.end(), n.id) == seen.end()) for (const std::string& t : n.tabs) out.push_back(t);
    for (const Floating& f : floating_) out.push_back(f.name);
    return out;
}
int DockLayout::LeafAt(float x, float y) const {
    for (const Node& n : nodes_) {
        if (n.IsSplit()) continue;
        if (x >= n.rect.x && y >= n.rect.y && x < n.rect.x + n.rect.w && y < n.rect.y + n.rect.h) return n.id;
    }
    return 0;
}

// ---- edits -----------------------------------------------------------------------------------------------------------

bool DockLayout::AddFloating(const std::string& w, Rect rect) {
    if (!GoodName(w) || Contains(w) || WindowCount() >= kMaxWindows) return false;
    Floating f; f.name = w; f.rect = SaneRect(rect, (int)floating_.size()); f.dirty = true;
    floating_.push_back(f);
    ++version_;
    return true;
}

void DockLayout::CollapseIfEmpty(int leaf) {
    const Node* l = Find(leaf);
    if (!l || l->IsSplit() || !l->tabs.empty() || l->parent == 0) return;
    const int pid = l->parent;
    const Node* p = Find(pid);
    if (!p || !p->IsSplit()) return;
    const int sib = p->child[0] == leaf ? p->child[1] : p->child[0];
    const int gid = p->parent;
    if (Node* s = Mut(sib)) s->parent = gid;
    if (gid == 0) root_ = sib;
    else if (Node* g = Mut(gid)) { if (g->child[0] == pid) g->child[0] = sib; else g->child[1] = sib; }
    nodes_.erase(nodes_.begin() + FindIndex(leaf));
    nodes_.erase(nodes_.begin() + FindIndex(pid));
}

bool DockLayout::RemoveWindowFromTree(const std::string& w, int* sourceLeaf) {
    const int id = NodeOf(w);
    if (!id) return false;
    Node* n = Mut(id);
    size_t idx = 0;
    while (idx < n->tabs.size() && n->tabs[idx] != w) ++idx;
    n->tabs.erase(n->tabs.begin() + (long)idx);
    if ((int)idx < n->active) --n->active;
    if (n->active >= (int)n->tabs.size()) n->active = std::max(0, (int)n->tabs.size() - 1);
    if (sourceLeaf) *sourceLeaf = id;
    CollapseIfEmpty(id);
    return true;
}

bool DockLayout::Dock(const std::string& w, int node, DockSide side) {
    if (!GoodName(w) || (int)side < 0 || (int)side > DockSide_Bottom) return false;
    const Node* t = Find(node);
    if (!t || t->IsSplit()) return false;
    if (t->tabs.empty()) side = DockSide_Center;   // only the empty root: nothing to split
    const bool exists = Contains(w);
    if (!exists && WindowCount() >= kMaxWindows) return false;
    const int src = NodeOf(w);
    if (src == node) {
        if (t->tabs.size() <= 1) return false;
        if (side == DockSide_Center) return SetActive(w);
    }
    if (side != DockSide_Center && (int)nodes_.size() + 2 > kMaxNodes) return false;
    if (side == DockSide_Center && (int)t->tabs.size() >= kMaxWindows) return false;
    for (size_t i = 0; i < floating_.size(); ++i) if (floating_[i].name == w) { floating_.erase(floating_.begin() + (long)i); break; }
    RemoveWindowFromTree(w, nullptr);
    if (side == DockSide_Center) {
        Node* leaf = Mut(node);
        leaf->tabs.push_back(w);
        leaf->active = (int)leaf->tabs.size() - 1;
    } else {
        const int nid = NewId(), sid = NewId();
        const int gid = Find(node)->parent;
        Node nn; nn.id = nid; nn.parent = sid; nn.tabs.push_back(w);
        Node ss; ss.id = sid; ss.parent = gid; ss.horizontal = (side == DockSide_Left || side == DockSide_Right); ss.ratio = 0.5f;
        const bool newFirst = (side == DockSide_Left || side == DockSide_Top);
        ss.child[0] = newFirst ? nid : node; ss.child[1] = newFirst ? node : nid;
        nodes_.push_back(nn); nodes_.push_back(ss);
        Mut(node)->parent = sid;
        if (gid == 0) root_ = sid;
        else { Node* g = Mut(gid); if (g->child[0] == node) g->child[0] = sid; else g->child[1] = sid; }
    }
    ++version_;
    return true;
}

bool DockLayout::DockRoot(const std::string& w, DockSide side) {
    if (!GoodName(w) || (int)side < 0 || (int)side > DockSide_Bottom) return false;
    const Node* r = Find(root_);
    if (!r) return false;
    if (!r->IsSplit()) return Dock(w, root_, side);
    if (side == DockSide_Center) return false;
    if (!Contains(w) && WindowCount() >= kMaxWindows) return false;
    if ((int)nodes_.size() + 2 > kMaxNodes) return false;
    for (size_t i = 0; i < floating_.size(); ++i) if (floating_[i].name == w) { floating_.erase(floating_.begin() + (long)i); break; }
    RemoveWindowFromTree(w, nullptr);
    const int old = root_;
    Node* o = Mut(old);
    if (!o->IsSplit() && o->tabs.empty()) { o->tabs.push_back(w); o->active = 0; ++version_; return true; }   // removing it left an empty root
    const bool newFirst = (side == DockSide_Left || side == DockSide_Top);
    const int nid = NewId(), sid = NewId();
    Node nn; nn.id = nid; nn.parent = sid; nn.tabs.push_back(w);
    Node ss; ss.id = sid; ss.parent = 0; ss.horizontal = (side == DockSide_Left || side == DockSide_Right);
    ss.ratio = newFirst ? 0.25f : 0.75f;
    ss.child[0] = newFirst ? nid : old; ss.child[1] = newFirst ? old : nid;
    nodes_.push_back(nn); nodes_.push_back(ss);
    Mut(old)->parent = sid;
    root_ = sid;
    ++version_;
    return true;
}

bool DockLayout::Float(const std::string& w, Rect rect) {
    if (!GoodName(w)) return false;
    const bool exists = Contains(w);
    if (!exists && WindowCount() >= kMaxWindows) return false;
    rect = SaneRect(rect, (int)floating_.size());
    for (Floating& f : floating_) if (f.name == w) { f.rect = rect; f.dirty = true; ++version_; return true; }
    RemoveWindowFromTree(w, nullptr);
    Floating f; f.name = w; f.rect = rect; f.dirty = true;
    floating_.push_back(f);
    ++version_;
    return true;
}

bool DockLayout::Close(const std::string& w) {
    for (size_t i = 0; i < floating_.size(); ++i) if (floating_[i].name == w) { floating_.erase(floating_.begin() + (long)i); ++version_; return true; }
    if (!RemoveWindowFromTree(w, nullptr)) return false;
    ++version_;
    return true;
}

bool DockLayout::SetActive(const std::string& w) {
    const int id = NodeOf(w);
    if (!id) return false;
    Node* n = Mut(id);
    for (size_t i = 0; i < n->tabs.size(); ++i) if (n->tabs[i] == w) { if (n->active != (int)i) { n->active = (int)i; ++version_; } return true; }
    return false;
}

bool DockLayout::MoveTab(const std::string& w, int index) {
    const int id = NodeOf(w);
    if (!id) return false;
    Node* n = Mut(id);
    const int cnt = (int)n->tabs.size();
    index = std::max(0, std::min(index, cnt - 1));
    int cur = 0;
    while (n->tabs[(size_t)cur] != w) ++cur;
    if (cur == index) return true;
    const std::string activeName = n->tabs[(size_t)n->active];
    n->tabs.erase(n->tabs.begin() + cur);
    n->tabs.insert(n->tabs.begin() + index, w);
    for (int i = 0; i < cnt; ++i) if (n->tabs[(size_t)i] == activeName) n->active = i;
    ++version_;
    return true;
}

bool DockLayout::SetFloatingRect(const std::string& w, Rect rect, bool markDirty) {
    for (Floating& f : floating_) if (f.name == w) { f.rect = SaneRect(rect, 0); if (markDirty) f.dirty = true; return true; }
    return false;
}
bool DockLayout::TakeDirty(const std::string& w, Rect* rect) {
    for (Floating& f : floating_) if (f.name == w) { if (!f.dirty) return false; f.dirty = false; if (rect) *rect = f.rect; return true; }
    return false;
}

void DockLayout::SetMinSize(const std::string& w, float mw, float mh) {
    if (!GoodName(w) || !Finite(mw) || !Finite(mh)) return;
    mw = Clamp(mw, 0.0f, 4096.0f); mh = Clamp(mh, 0.0f, 4096.0f);
    for (MinSize& m : mins_) if (m.name == w) { m.w = mw; m.h = mh; return; }
    if (mins_.size() < (size_t)kMaxWindows * 2) mins_.push_back({w, mw, mh});
}
float DockLayout::MinWOf(const std::string& w) const { for (const MinSize& m : mins_) if (m.name == w) return m.w; return 0.0f; }
float DockLayout::MinHOf(const std::string& w) const { for (const MinSize& m : mins_) if (m.name == w) return m.h; return 0.0f; }

// ---- geometry --------------------------------------------------------------------------------------------------------

Vec2 DockLayout::MinSizeRec(int id, float tabH, int depth) const {
    const Node* n = Find(id);
    if (!n || depth > kMaxDepth) return Vec2{kMinLeafW, tabH + kMinLeafH};
    if (!n->IsSplit()) {
        float w = kMinLeafW, h = kMinLeafH;
        for (const std::string& t : n->tabs) { w = std::max(w, MinWOf(t)); h = std::max(h, MinHOf(t)); }
        return Vec2{w, tabH + h};
    }
    const Vec2 a = MinSizeRec(n->child[0], tabH, depth + 1), b = MinSizeRec(n->child[1], tabH, depth + 1);
    if (n->horizontal) return Vec2{a.x + b.x + kSplitter, std::max(a.y, b.y)};
    return Vec2{std::max(a.x, b.x), a.y + b.y + kSplitter};
}
Vec2 DockLayout::MinSizeOf(int node) const { return MinSizeRec(node, tabH_, 0); }

void DockLayout::ComputeNode(int id, Rect r, float tabH, int depth) {
    Node* n = Mut(id);
    if (!n || depth > kMaxDepth) return;
    if (!Finite(r.x) || !Finite(r.y) || !Finite(r.w) || !Finite(r.h)) r = Rect{};
    if (r.w < 0.0f) r.w = 0.0f;
    if (r.h < 0.0f) r.h = 0.0f;
    n->rect = r;
    if (!n->IsSplit()) return;
    const bool hz = n->horizontal;
    const int c0 = n->child[0], c1 = n->child[1];
    const float ratio = Finite(n->ratio) ? n->ratio : 0.5f;
    const Vec2 ma = MinSizeRec(c0, tabH, depth + 1), mb = MinSizeRec(c1, tabH, depth + 1);
    const float len = hz ? r.w : r.h;
    const float gap = std::min(kSplitter, len);   // a space smaller than the gap loses the gap, not the children
    const float total = len - gap;
    const float a = hz ? ma.x : ma.y, b = hz ? mb.x : mb.y;
    float sa;
    if (a + b <= total) sa = Clamp(ratio * total, a, total - b);
    else sa = (a + b) > 0.0f ? total * a / (a + b) : total * 0.5f;
    const float sb = total - sa;
    if (hz) { ComputeNode(c0, Rect{r.x, r.y, sa, r.h}, tabH, depth + 1); ComputeNode(c1, Rect{r.x + sa + gap, r.y, sb, r.h}, tabH, depth + 1); }
    else    { ComputeNode(c0, Rect{r.x, r.y, r.w, sa}, tabH, depth + 1); ComputeNode(c1, Rect{r.x, r.y + sa + gap, r.w, sb}, tabH, depth + 1); }
}
void DockLayout::Compute(Rect area, float tabH) {
    tabH_ = Finite(tabH) && tabH > 0.0f ? std::min(tabH, 200.0f) : 24.0f;
    ComputeNode(root_, area, tabH_, 0);
}
Rect DockLayout::ContentRect(int leaf) const {
    const Node* n = Find(leaf);
    if (!n) return Rect{};
    Rect r = n->rect;
    const float th = std::min(tabH_, r.h);
    r.y += th; r.h -= th;
    return r;
}
Rect DockLayout::SplitterRect(int id) const {
    const Node* n = Find(id);
    if (!n || !n->IsSplit()) return Rect{};
    const Node* a = Find(n->child[0]);
    if (!a) return Rect{};
    const float gap = std::min(kSplitter, n->horizontal ? n->rect.w : n->rect.h);
    if (n->horizontal) return Rect{a->rect.x + a->rect.w, n->rect.y, gap, n->rect.h};
    return Rect{n->rect.x, a->rect.y + a->rect.h, n->rect.w, gap};
}
bool DockLayout::SetSplitterPos(int id, float edge) {
    Node* n = Mut(id);
    if (!n || !n->IsSplit() || !Finite(edge)) return false;
    const bool hz = n->horizontal;
    const float len = hz ? n->rect.w : n->rect.h;
    const float total = len - std::min(kSplitter, len);
    if (total <= 1.0f) return false;
    const Vec2 ma = MinSizeRec(n->child[0], tabH_, 1), mb = MinSizeRec(n->child[1], tabH_, 1);
    const float a = hz ? ma.x : ma.y, b = hz ? mb.x : mb.y;
    float sa = edge - (hz ? n->rect.x : n->rect.y);
    if (a + b <= total) sa = Clamp(sa, a, total - b);
    else sa = Clamp(sa, 0.0f, total);
    n->ratio = Clamp(sa / total, 0.01f, 0.99f);
    ++version_;
    return true;
}
bool DockLayout::SetRatio(int id, float ratio) {
    Node* n = Mut(id);
    if (!n || !n->IsSplit() || !Finite(ratio)) return false;
    const float len = n->horizontal ? n->rect.w : n->rect.h;
    if (len - std::min(kSplitter, len) > 1.0f) return SetSplitterPos(id, (n->horizontal ? n->rect.x : n->rect.y) + ratio * (len - std::min(kSplitter, len)));
    n->ratio = Clamp(ratio, 0.01f, 0.99f);
    ++version_;
    return true;
}

// ---- validation ------------------------------------------------------------------------------------------------------

bool DockLayout::Validate(std::string* why) const {
    auto fail = [&](const std::string& m) { if (why) *why = m; return false; };
    if ((int)nodes_.size() > kMaxNodes || nodes_.empty()) return fail("node count");
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].id <= 0 || nodes_[i].id >= nextId_) return fail("bad id");
        for (size_t j = i + 1; j < nodes_.size(); ++j) if (nodes_[j].id == nodes_[i].id) return fail("duplicate id");
    }
    const Node* r = Find(root_);
    if (!r) return fail("no root");
    if (r->parent != 0) return fail("root has a parent");
    std::vector<int> visited;
    std::vector<std::pair<int, int>> stack{{root_, 0}};
    while (!stack.empty()) {
        const auto [id, depth] = stack.back(); stack.pop_back();
        if (depth > kMaxDepth + 2) return fail("too deep");
        if (std::find(visited.begin(), visited.end(), id) != visited.end()) return fail("node reached twice");
        const Node* n = Find(id);
        if (!n) return fail("missing child");
        visited.push_back(id);
        if (n->IsSplit()) {
            if (n->child[0] == 0 || n->child[1] == 0 || n->child[0] == n->child[1]) return fail("split children");
            if (!n->tabs.empty()) return fail("split with tabs");
            if (!(n->ratio > 0.0f && n->ratio < 1.0f) || !std::isfinite(n->ratio)) return fail("ratio");
            for (int k = 0; k < 2; ++k) {
                const Node* c = Find(n->child[k]);
                if (!c) return fail("missing child");
                if (c->parent != id) return fail("parent link");
                stack.push_back({n->child[k], depth + 1});
            }
        } else {
            if (n->child[0] != 0 || n->child[1] != 0) return fail("leaf with children");
            if (n->tabs.empty() && id != root_) return fail("empty leaf");
            if (n->tabs.empty() ? n->active != 0 : (n->active < 0 || n->active >= (int)n->tabs.size())) return fail("active tab");
        }
    }
    if (visited.size() != nodes_.size()) return fail("unreachable node");
    std::vector<std::string> names;
    auto add = [&](const std::string& s) {
        if (!GoodName(s)) return false;
        names.push_back(s);
        return true;
    };
    for (const Node& n : nodes_) for (const std::string& t : n.tabs) if (!add(t)) return fail("window name");
    for (const Floating& f : floating_) {
        if (!add(f.name)) return fail("window name");
        if (!Finite(f.rect.x) || !Finite(f.rect.y) || !Finite(f.rect.w) || !Finite(f.rect.h)) return fail("floating rect");
    }
    if ((int)names.size() > kMaxWindows) return fail("window count");
    std::sort(names.begin(), names.end());
    for (size_t i = 1; i < names.size(); ++i) if (names[i] == names[i - 1]) return fail("window listed twice");
    return true;
}

// ---- repair and text -------------------------------------------------------------------------------------------------

bool DockLayout::Rebuild(Raw& raw, int* repairs) {
    int fixes = 0;
    std::vector<Node> out;
    std::vector<std::string> placed;
    std::vector<char> visited(raw.nodes.size(), 0);
    std::vector<int> usedIds;
    int maxId = 0;
    auto isPlaced = [&](const std::string& s) { return std::find(placed.begin(), placed.end(), s) != placed.end(); };
    auto rawIndex = [&](int id) { if (id <= 0) return -1; for (size_t i = 0; i < raw.nodes.size(); ++i) if (raw.nodes[i].id == id) return (int)i; return -1; };

    struct Ctx { std::vector<Node>& out; std::vector<std::string>& placed; std::vector<char>& visited; std::vector<int>& usedIds; int& maxId; int& fixes; Raw& raw; } cx{out, placed, visited, usedIds, maxId, fixes, raw};
    // recursive lambda via std::function-free helper
    struct Rec {
        Ctx& c; const decltype(rawIndex)& idx;
        int Go(int id, int depth) {
            const int ri = idx(id);
            if (ri < 0 || depth > kMaxDepth || c.visited[(size_t)ri]) { ++c.fixes; return -1; }
            c.visited[(size_t)ri] = 1;
            Raw::N& rn = c.raw.nodes[(size_t)ri];
            Node n;
            if (rn.id > 0 && std::find(c.usedIds.begin(), c.usedIds.end(), rn.id) == c.usedIds.end()) { n.id = rn.id; c.usedIds.push_back(rn.id); c.maxId = std::max(c.maxId, rn.id); }
            if (!rn.split) {
                std::vector<std::string> tabs;
                int active = -1;
                for (size_t k = 0; k < rn.tabs.size(); ++k) {
                    std::string t = rn.tabs[k];
                    if (t.empty()) { ++c.fixes; continue; }
                    if ((int)t.size() > kMaxNameLen) { t = CutName(t); ++c.fixes; }
                    if (std::find(c.placed.begin(), c.placed.end(), t) != c.placed.end() || std::find(tabs.begin(), tabs.end(), t) != tabs.end()) { ++c.fixes; continue; }
                    if ((int)c.placed.size() + (int)tabs.size() >= kMaxWindows) { ++c.fixes; continue; }
                    if ((int)k == rn.active) active = (int)tabs.size();
                    tabs.push_back(t);
                }
                if (tabs.empty()) { if (depth > 0 || !rn.tabs.empty()) ++c.fixes; return -1; }   // an empty root is a normal layout
                for (const std::string& t : tabs) c.placed.push_back(t);
                if (active < 0) { active = 0; if (rn.active != 0) ++c.fixes; }
                n.tabs = tabs; n.active = active;
                c.out.push_back(n);
                return (int)c.out.size() - 1;
            }
            const int a = Go(rn.c0, depth + 1);
            const int b = Go(rn.c1, depth + 1);
            if (a < 0 && b < 0) return -1;
            if (a < 0 || b < 0) { ++c.fixes; return a < 0 ? b : a; }
            n.horizontal = rn.horiz;
            float ratio = rn.ratio;
            if (!std::isfinite(ratio)) { ratio = 0.5f; ++c.fixes; }
            else if (ratio < 0.01f || ratio > 0.99f) { ratio = Clamp(ratio, 0.01f, 0.99f); ++c.fixes; }
            n.ratio = ratio;
            n.child[0] = a; n.child[1] = b;   // temporarily indices into `out`; turned into ids below
            c.out.push_back(n);
            return (int)c.out.size() - 1;
        }
    } rec{cx, rawIndex};
    const int rootIdx = raw.nodes.empty() ? -1 : rec.Go(raw.root, 0);

    // ids: fresh ones for nodes without a valid one, then real child ids and parent links
    int nextId = maxId + 1;
    for (Node& n : out) if (n.id == 0) n.id = nextId++;
    for (Node& n : out) if (n.child[0] != 0 || n.child[1] != 0) {
        const int a = n.child[0], b = n.child[1];
        n.child[0] = out[(size_t)a].id; n.child[1] = out[(size_t)b].id;
    }
    std::vector<Node> fin;
    if (rootIdx >= 0 && (int)out.size() <= kMaxNodes) {
        for (Node& n : out) for (int k = 0; k < 2; ++k) if (n.child[k]) for (Node& c : out) if (c.id == n.child[k]) c.parent = n.id;
        fin = out;
        // the root is the one node with no parent
        for (Node& n : fin) if (n.id == out[(size_t)rootIdx].id) n.parent = 0;
    } else {
        // no usable tree (or too many nodes): every window that was placed goes into one root leaf
        if (rootIdx >= 0) ++fixes;
        Node n; n.id = 1; nextId = 2;
        for (const std::string& s : placed) n.tabs.push_back(s);
        fin.push_back(n);
    }
    const int newRoot = (rootIdx >= 0 && (int)out.size() <= kMaxNodes) ? out[(size_t)rootIdx].id : 1;

    std::vector<Floating> fl;
    auto addFloating = [&](const std::string& name0, Rect r) {
        std::string name = name0;
        if (name.empty()) { ++fixes; return; }
        if ((int)name.size() > kMaxNameLen) { name = CutName(name); ++fixes; }
        if (isPlaced(name)) { ++fixes; return; }
        if ((int)placed.size() >= kMaxWindows) { ++fixes; return; }
        placed.push_back(name);
        Floating f; f.name = name; f.rect = SaneRect(r, (int)fl.size()); f.dirty = true;
        fl.push_back(f);
    };
    for (const Raw::F& f : raw.floats) addFloating(f.name, f.rect);
    // windows that sat in broken parts of the tree are kept, floating
    for (const Raw::N& rn : raw.nodes) for (const std::string& t : rn.tabs) {
        if (t.empty()) continue;
        const std::string name = (int)t.size() > kMaxNameLen ? CutName(t) : t;
        if (!isPlaced(name)) { addFloating(name, Rect{NAN, NAN, NAN, NAN}); }
    }
    nodes_ = fin; floating_ = fl; root_ = newRoot; nextId_ = nextId;
    ++version_;
    if (repairs) *repairs = fixes;
    return true;
}

int DockLayout::Repair() {
    Raw raw;
    raw.root = root_;
    for (const Node& n : nodes_) {
        Raw::N r; r.id = n.id; r.split = n.IsSplit(); r.horiz = n.horizontal; r.ratio = n.ratio; r.c0 = n.child[0]; r.c1 = n.child[1];
        r.active = n.active; r.tabs = n.tabs;
        raw.nodes.push_back(r);
    }
    for (const Floating& f : floating_) raw.floats.push_back({f.name, f.rect});
    int fixes = 0;
    Rebuild(raw, &fixes);
    return fixes;
}

std::string DockLayout::Save() const {
    std::string s = "okaydock 1\n";
    char buf[96];
    s += "root " + std::to_string(root_) + "\n";
    // preorder from the root (so a layout saved, loaded and saved again gives the same text), then any node a broken layout left out
    std::vector<int> order, stack{root_};
    while (!stack.empty() && order.size() <= nodes_.size()) {
        const int id = stack.back(); stack.pop_back();
        if (std::find(order.begin(), order.end(), id) != order.end()) continue;
        const Node* n = Find(id);
        if (!n) continue;
        order.push_back(id);
        if (n->IsSplit()) { stack.push_back(n->child[1]); stack.push_back(n->child[0]); }
    }
    for (const Node& n : nodes_) if (std::find(order.begin(), order.end(), n.id) == order.end()) order.push_back(n.id);
    for (const int oid : order) {
        const Node& n = *Find(oid);
        if (n.IsSplit()) {
            std::snprintf(buf, sizeof buf, "%.9g", (double)n.ratio);
            s += "node " + std::to_string(n.id) + " split " + (n.horizontal ? "h " : "v ") + buf + " " + std::to_string(n.child[0]) + " " + std::to_string(n.child[1]) + "\n";
        } else {
            s += "node " + std::to_string(n.id) + " leaf " + std::to_string(n.active);
            for (const std::string& t : n.tabs) s += " " + Escape(t);
            s += "\n";
        }
    }
    for (const Floating& f : floating_) {
        std::snprintf(buf, sizeof buf, "%.9g %.9g %.9g %.9g", (double)f.rect.x, (double)f.rect.y, (double)f.rect.w, (double)f.rect.h);
        s += std::string("float ") + buf + " " + Escape(f.name) + "\n";
    }
    return s;
}

bool DockLayout::Load(const std::string& blob, int* repairs) {
    if (blob.size() > (1u << 20)) return false;
    std::istringstream in(blob);
    std::string line;
    if (!std::getline(in, line)) return false;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line != "okaydock 1") return false;
    Raw raw;
    int junk = 0;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::vector<std::string> tok;
        std::string t;
        while (ls >> t) { tok.push_back(t); if (tok.size() > 4096) break; }
        if (tok.empty()) continue;
        if (tok[0] == "root" && tok.size() == 2) {
            int id; if (ParseInt(tok[1], &id)) raw.root = id; else ++junk;
        } else if (tok[0] == "node" && tok.size() >= 4 && raw.nodes.size() < 1024) {
            Raw::N n;
            if (!ParseInt(tok[1], &n.id)) n.id = 0;   // a node without a good id gets a fresh one (and cannot be referenced)
            if (tok[2] == "split" && tok.size() == 7) {
                n.split = true; n.horiz = tok[3] != "v";
                if (!ParseFloat(tok[4], &n.ratio)) n.ratio = NAN;
                if (!ParseInt(tok[5], &n.c0)) n.c0 = 0;
                if (!ParseInt(tok[6], &n.c1)) n.c1 = 0;
            } else if (tok[2] == "leaf") {
                n.active = std::max(0, std::atoi(tok[3].c_str()));
                for (size_t i = 4; i < tok.size(); ++i) n.tabs.push_back(Unescape(tok[i]));
            } else { ++junk; continue; }
            raw.nodes.push_back(n);
        } else if (tok[0] == "float" && tok.size() >= 6 && raw.floats.size() < 1024) {
            Raw::F f;
            if (!ParseFloat(tok[1], &f.rect.x)) f.rect.x = NAN;
            if (!ParseFloat(tok[2], &f.rect.y)) f.rect.y = NAN;
            if (!ParseFloat(tok[3], &f.rect.w)) f.rect.w = NAN;
            if (!ParseFloat(tok[4], &f.rect.h)) f.rect.h = NAN;
            f.name = Unescape(tok[5]);
            raw.floats.push_back(f);
        } else ++junk;
    }
    int fixes = 0;
    Rebuild(raw, &fixes);
    if (repairs) *repairs = fixes + junk;
    return true;
}

} // namespace OkayUI
