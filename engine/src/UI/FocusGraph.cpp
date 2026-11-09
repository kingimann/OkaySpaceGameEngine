#include "okay/UI/FocusGraph.hpp"

#include <algorithm>
#include <cmath>

namespace okay {

void FocusGraph::Clear() {
    m_nodes.clear();
    m_traps.clear();
    m_focus = 0;
    m_nextId = 1;
}

int FocusGraph::Add(FocusNode node) {
    if (node.id <= 0) node.id = m_nextId;
    if (node.id >= m_nextId) m_nextId = node.id + 1;
    for (FocusNode& n : m_nodes)
        if (n.id == node.id) { n = node; return node.id; }
    m_nodes.push_back(node);
    return node.id;
}

int FocusGraph::Add(float x, float y, float w, float h, int group) {
    FocusNode n;
    n.x = x; n.y = y; n.w = w; n.h = h;
    n.group = group;
    return Add(n);
}

bool FocusGraph::Remove(int id) {
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [&](const FocusNode& n) { return n.id == id; });
    if (it == m_nodes.end()) return false;
    m_nodes.erase(it);
    if (m_focus == id) m_focus = 0;
    return true;
}

FocusNode* FocusGraph::Find(int id) {
    for (FocusNode& n : m_nodes) if (n.id == id) return &n;
    return nullptr;
}
const FocusNode* FocusGraph::Find(int id) const {
    for (const FocusNode& n : m_nodes) if (n.id == id) return &n;
    return nullptr;
}

bool FocusGraph::SetEnabled(int id, bool enabled) {
    FocusNode* n = Find(id);
    if (!n) return false;
    n->enabled = enabled;
    if (!enabled && m_focus == id) m_focus = 0;   // a disabled widget keeps no focus
    return true;
}

bool FocusGraph::SetLink(int id, FocusDir dir, int target) {
    FocusNode* n = Find(id);
    if (!n || dir == FocusDir::Next || dir == FocusDir::Previous) return false;
    n->link[(int)dir] = target;
    return true;
}

// ---- Traps ---------------------------------------------------------------------------------------
void FocusGraph::PushTrap(int group) {
    m_traps.push_back(Trap{group, m_focus});
    const FocusNode* f = Find(m_focus);
    if (!f || !Usable(*f)) m_focus = First();
}

int FocusGraph::PopTrap() {
    if (m_traps.empty()) return m_focus;
    const int restore = m_traps.back().restore;
    m_traps.pop_back();
    const FocusNode* n = Find(restore);
    m_focus = n && Usable(*n) ? restore : First();
    return m_focus;
}

// ---- Orders --------------------------------------------------------------------------------------
std::vector<int> FocusGraph::Candidates() const {
    std::vector<int> out;
    for (const FocusNode& n : m_nodes) if (Usable(n)) out.push_back(n.id);
    return out;
}

namespace {

// Rows top to bottom (a node joins the row of the first node whose centre line it is within
// half a height of), each row left to right.
std::vector<const FocusNode*> InReadingOrder(std::vector<const FocusNode*> nodes) {
    std::stable_sort(nodes.begin(), nodes.end(), [](const FocusNode* a, const FocusNode* b) { return a->CenterY() < b->CenterY(); });
    std::vector<const FocusNode*> out;
    std::size_t i = 0;
    while (i < nodes.size()) {
        const FocusNode* anchor = nodes[i];
        std::size_t j = i + 1;
        while (j < nodes.size() && std::fabs(nodes[j]->CenterY() - anchor->CenterY()) <= anchor->h * 0.5f) ++j;
        std::stable_sort(nodes.begin() + (std::ptrdiff_t)i, nodes.begin() + (std::ptrdiff_t)j,
                         [](const FocusNode* a, const FocusNode* b) { return a->x < b->x; });
        for (std::size_t k = i; k < j; ++k) out.push_back(nodes[k]);
        i = j;
    }
    return out;
}

} // namespace

std::vector<int> FocusGraph::ReadingOrder() const {
    std::vector<const FocusNode*> usable;
    for (const FocusNode& n : m_nodes) if (Usable(n)) usable.push_back(&n);
    std::vector<int> out;
    for (const FocusNode* n : InReadingOrder(std::move(usable))) out.push_back(n->id);
    return out;
}

std::vector<int> FocusGraph::TabOrder() const {
    std::vector<const FocusNode*> positive, plain;
    for (const FocusNode& n : m_nodes) {
        if (!Usable(n) || n.tabIndex < 0) continue;
        (n.tabIndex > 0 ? positive : plain).push_back(&n);
    }
    std::vector<int> out;
    std::vector<const FocusNode*> reading = InReadingOrder(std::move(positive));
    std::stable_sort(reading.begin(), reading.end(), [](const FocusNode* a, const FocusNode* b) { return a->tabIndex < b->tabIndex; });
    for (const FocusNode* n : reading) out.push_back(n->id);
    for (const FocusNode* n : InReadingOrder(std::move(plain))) out.push_back(n->id);
    return out;
}

int FocusGraph::First() const {
    const std::vector<int> order = TabOrder();
    if (!order.empty()) return order.front();
    const std::vector<int> any = ReadingOrder();   // only nodes Tab skips: arrows still reach them
    return any.empty() ? 0 : any.front();
}

int FocusGraph::Last() const {
    const std::vector<int> order = TabOrder();
    if (!order.empty()) return order.back();
    const std::vector<int> any = ReadingOrder();
    return any.empty() ? 0 : any.back();
}

// ---- Moving -----------------------------------------------------------------------------------------
int FocusGraph::Directional(const FocusNode& from, FocusDir dir, const FocusOptions& o) const {
    const float dx = dir == FocusDir::Left ? -1.0f : dir == FocusDir::Right ? 1.0f : 0.0f;
    const float dy = dir == FocusDir::Up ? -1.0f : dir == FocusDir::Down ? 1.0f : 0.0f;

    const int link = from.link[(int)dir];
    if (link == -1) return from.id;
    if (link > 0) {
        const FocusNode* n = Find(link);
        if (n && Usable(*n) && n->id != from.id) return link;
    }

    int best = 0;
    float bestScore = 0.0f;
    for (const FocusNode& n : m_nodes) {
        if (n.id == from.id || !Usable(n)) continue;
        const float s = FocusScore(from, n, dx, dy, o.sidewaysWeight);
        if (s < 0.0f) continue;
        if (!best || s < bestScore) { best = n.id; bestScore = s; }
    }
    if (best) return best;

    const FocusWrap wrap = dy != 0.0f ? o.vertical : o.horizontal;
    if (wrap == FocusWrap::None) return from.id;
    if (wrap == FocusWrap::ReadingOrder) {
        const std::vector<int> order = ReadingOrder();
        if (order.empty()) return from.id;
        return (dir == FocusDir::Down || dir == FocusDir::Right) ? order.front() : order.back();
    }
    // Edge: look at the others as if they sat one layout further on, so the one at the far edge
    // that lines up best with `from` comes out nearest.
    float lo = 1e30f, hi = -1e30f;
    for (const FocusNode& n : m_nodes) {
        if (!Usable(n)) continue;
        const float c = dy != 0.0f ? n.CenterY() : n.CenterX();
        lo = c < lo ? c : lo;
        hi = c > hi ? c : hi;
    }
    const float shift = (hi - lo) + 2.0f;
    for (const FocusNode& n : m_nodes) {
        if (n.id == from.id || !Usable(n)) continue;
        FocusNode moved = n;
        moved.x += dx * shift;
        moved.y += dy * shift;
        const float s = FocusScore(from, moved, dx, dy, o.sidewaysWeight);
        if (s < 0.0f) continue;
        if (!best || s < bestScore) { best = n.id; bestScore = s; }
    }
    return best ? best : from.id;
}

int FocusGraph::Move(int from, FocusDir dir, const FocusOptions& o) const {
    bool any = false;
    for (const FocusNode& n : m_nodes) if (Usable(n)) { any = true; break; }
    if (!any) return 0;
    const FocusNode* f = Find(from);
    if (!f || !Usable(*f)) return (dir == FocusDir::Up || dir == FocusDir::Previous) ? Last() : First();

    if (dir == FocusDir::Next || dir == FocusDir::Previous) {
        const std::vector<int> order = TabOrder();
        if (order.empty()) return from;
        const auto it = std::find(order.begin(), order.end(), from);
        if (it == order.end()) return dir == FocusDir::Next ? order.front() : order.back();
        const std::ptrdiff_t i = it - order.begin() + (dir == FocusDir::Next ? 1 : -1);
        if (i >= 0 && i < (std::ptrdiff_t)order.size()) return order[(std::size_t)i];
        const bool wraps = o.tabWraps && (o.vertical != FocusWrap::None || o.horizontal != FocusWrap::None);
        if (!wraps) return from;
        return dir == FocusDir::Next ? order.front() : order.back();
    }
    return Directional(*f, dir, o);
}

bool FocusGraph::SetFocus(int id) {
    const FocusNode* n = Find(id);
    if (!n || !Usable(*n)) return false;
    m_focus = id;
    return true;
}

int FocusGraph::Navigate(FocusDir dir, const FocusOptions& options) {
    const int to = Move(m_focus, dir, options);
    m_focus = to;
    return m_focus;
}

} // namespace okay
