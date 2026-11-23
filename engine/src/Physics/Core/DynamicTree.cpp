// Dynamic AABB tree (see DynamicTree.hpp for the papers it was written from).
#include "okay/Physics/Core/DynamicTree.hpp"

#include <algorithm>

namespace okay {
namespace phys {

int DynamicTree::Alloc() {
    if (m_free != kNull) {
        const int id = m_free;
        m_free = m_nodes[(std::size_t)id].parent;
        --m_freeCount;
        m_nodes[(std::size_t)id] = Node{};
        return id;
    }
    m_nodes.push_back(Node{});
    return (int)m_nodes.size() - 1;
}

void DynamicTree::Free(int id) {
    Node& n = m_nodes[(std::size_t)id];
    n.parent = m_free;
    n.child1 = n.child2 = kNull;
    n.height = -1;
    m_free = id;
    ++m_freeCount;
}

int DynamicTree::Create(const Aabb& tight, std::uint64_t user) {
    const int id = Alloc();
    Node& n = m_nodes[(std::size_t)id];
    n.box = tight.Grown(margin);
    n.user = user;
    n.height = 0;
    InsertLeaf(id);
    ++m_leaves;
    return id;
}

void DynamicTree::Destroy(int proxy) {
    if (proxy < 0 || (std::size_t)proxy >= m_nodes.size() || m_nodes[(std::size_t)proxy].height != 0) return;
    RemoveLeaf(proxy);
    Free(proxy);
    --m_leaves;
}

bool DynamicTree::Move(int proxy, const Aabb& tight, const V3& displacement) {
    Node& n = m_nodes[(std::size_t)proxy];
    if (n.box.Contains(tight)) return false;
    RemoveLeaf(proxy);
    Aabb fat = tight.Grown(margin);
    const V3 ahead = displacement * displacementScale;   // stretch the box the way the body is going
    for (int k = 0; k < 3; ++k) {
        if (ahead[k] < 0.0f) fat.lo[k] += ahead[k];
        else fat.hi[k] += ahead[k];
    }
    m_nodes[(std::size_t)proxy].box = fat;
    InsertLeaf(proxy);
    return true;
}

// The sibling whose pairing with `box` adds the least total area to the tree (branch and bound, Bittner et al. 2015):
// cost(S) = area(S u L) + the growth of every ancestor of S. A subtree is skipped when even its cheapest possible
// placement (area(L) plus the growth already paid above it) cannot beat the best found.
int DynamicTree::BestSibling(const Aabb& box) const {
    const float leafArea = box.HalfArea();
    int best = m_root;
    float bestCost = Aabb::Union(m_nodes[(std::size_t)m_root].box, box).HalfArea();
    struct Item { int id; float inherited; };
    std::vector<Item> stack;
    stack.reserve(64);
    stack.push_back({m_root, 0.0f});
    while (!stack.empty()) {
        const Item it = stack.back(); stack.pop_back();
        const Node& n = m_nodes[(std::size_t)it.id];
        const float direct = Aabb::Union(n.box, box).HalfArea();
        const float cost = direct + it.inherited;
        if (cost < bestCost) { bestCost = cost; best = it.id; }
        if (n.child1 == kNull) continue;
        const float childInherited = it.inherited + direct - n.box.HalfArea();
        if (leafArea + childInherited < bestCost) {
            stack.push_back({n.child2, childInherited});
            stack.push_back({n.child1, childInherited});
        }
    }
    return best;
}

void DynamicTree::InsertLeaf(int leaf) {
    if (m_root == kNull) {
        m_root = leaf;
        m_nodes[(std::size_t)leaf].parent = kNull;
        return;
    }
    const Aabb box = m_nodes[(std::size_t)leaf].box;
    const int sibling = BestSibling(box);
    const int oldParent = m_nodes[(std::size_t)sibling].parent;
    const int parent = Alloc();
    {
        Node& p = m_nodes[(std::size_t)parent];
        p.parent = oldParent;
        p.box = Aabb::Union(box, m_nodes[(std::size_t)sibling].box);
        p.height = m_nodes[(std::size_t)sibling].height + 1;
        p.child1 = sibling;
        p.child2 = leaf;
    }
    if (oldParent != kNull) {
        Node& op = m_nodes[(std::size_t)oldParent];
        if (op.child1 == sibling) op.child1 = parent; else op.child2 = parent;
    } else {
        m_root = parent;
    }
    m_nodes[(std::size_t)sibling].parent = parent;
    m_nodes[(std::size_t)leaf].parent = parent;

    // Walk up: refit boxes and heights, and rotate where that shrinks the tree.
    int id = parent;
    while (id != kNull) {
        Node& n = m_nodes[(std::size_t)id];
        const Node& c1 = m_nodes[(std::size_t)n.child1];
        const Node& c2 = m_nodes[(std::size_t)n.child2];
        n.box = Aabb::Union(c1.box, c2.box);
        n.height = 1 + std::max(c1.height, c2.height);
        Rotate(id);
        id = m_nodes[(std::size_t)id].parent;
    }
}

void DynamicTree::RemoveLeaf(int leaf) {
    if (leaf == m_root) { m_root = kNull; return; }
    const int parent = m_nodes[(std::size_t)leaf].parent;
    const int grand = m_nodes[(std::size_t)parent].parent;
    const int sibling = m_nodes[(std::size_t)parent].child1 == leaf ? m_nodes[(std::size_t)parent].child2
                                                                     : m_nodes[(std::size_t)parent].child1;
    if (grand == kNull) {
        m_root = sibling;
        m_nodes[(std::size_t)sibling].parent = kNull;
        Free(parent);
        return;
    }
    Node& g = m_nodes[(std::size_t)grand];
    if (g.child1 == parent) g.child1 = sibling; else g.child2 = sibling;
    m_nodes[(std::size_t)sibling].parent = grand;
    Free(parent);
    int id = grand;
    while (id != kNull) {
        Node& n = m_nodes[(std::size_t)id];
        n.box = Aabb::Union(m_nodes[(std::size_t)n.child1].box, m_nodes[(std::size_t)n.child2].box);
        n.height = 1 + std::max(m_nodes[(std::size_t)n.child1].height, m_nodes[(std::size_t)n.child2].height);
        Rotate(id);
        id = m_nodes[(std::size_t)id].parent;
    }
}

// One local rotation at inner node `id` with children B and C (Kopta et al. 2012): swapping B with a grandchild under C
// (or C with one under B) changes only that child's box. Take the swap that shrinks it most, if any does.
void DynamicTree::Rotate(int id) {
    Node& a = m_nodes[(std::size_t)id];
    if (a.child1 == kNull) return;
    const int b = a.child1, c = a.child2;
    const Node& nb = m_nodes[(std::size_t)b];
    const Node& nc = m_nodes[(std::size_t)c];
    if (nb.height < 1 && nc.height < 1) return;

    // candidates: (moving node, inner node it moves into, which child of that inner node it replaces)
    float bestGain = 0.0f;
    int bestMove = kNull, bestInner = kNull, bestOut = kNull;
    auto consider = [&](int mover, int inner) {
        const Node& in = m_nodes[(std::size_t)inner];
        if (in.child1 == kNull) return;
        const float before = in.box.HalfArea();
        const int kids[2] = {in.child1, in.child2};
        for (int k = 0; k < 2; ++k) {
            const int out = kids[k], stay = kids[1 - k];
            const float after = Aabb::Union(m_nodes[(std::size_t)mover].box, m_nodes[(std::size_t)stay].box).HalfArea();
            const float gain = before - after;
            if (gain > bestGain) { bestGain = gain; bestMove = mover; bestInner = inner; bestOut = out; }
        }
    };
    consider(b, c);
    consider(c, b);
    if (bestMove == kNull) return;

    // swap bestMove (a child of `id`) with bestOut (a child of bestInner)
    Node& na = m_nodes[(std::size_t)id];
    if (na.child1 == bestMove) na.child1 = bestOut; else na.child2 = bestOut;
    m_nodes[(std::size_t)bestOut].parent = id;
    Node& in = m_nodes[(std::size_t)bestInner];
    if (in.child1 == bestOut) in.child1 = bestMove; else in.child2 = bestMove;
    m_nodes[(std::size_t)bestMove].parent = bestInner;
    in.box = Aabb::Union(m_nodes[(std::size_t)in.child1].box, m_nodes[(std::size_t)in.child2].box);
    in.height = 1 + std::max(m_nodes[(std::size_t)in.child1].height, m_nodes[(std::size_t)in.child2].height);
    Node& na2 = m_nodes[(std::size_t)id];
    na2.height = 1 + std::max(m_nodes[(std::size_t)na2.child1].height, m_nodes[(std::size_t)na2.child2].height);
}

float DynamicTree::AreaRatio() const {
    if (m_root == kNull) return 0.0f;
    const float rootArea = m_nodes[(std::size_t)m_root].box.HalfArea();
    if (!(rootArea > 0.0f)) return 0.0f;
    float sum = 0.0f;
    for (const Node& n : m_nodes)
        if (n.height > 0) sum += n.box.HalfArea();
    return sum / rootArea;
}

bool DynamicTree::Validate() const {
    if (m_root == kNull) return m_leaves == 0;
    if (m_nodes[(std::size_t)m_root].parent != kNull) return false;
    int leaves = 0, visited = 0;
    std::vector<int> stack{m_root};
    while (!stack.empty()) {
        const int id = stack.back(); stack.pop_back();
        ++visited;
        const Node& n = m_nodes[(std::size_t)id];
        if (n.height < 0) return false;
        if (n.child1 == kNull) {
            if (n.child2 != kNull || n.height != 0) return false;
            ++leaves;
            continue;
        }
        const Node& c1 = m_nodes[(std::size_t)n.child1];
        const Node& c2 = m_nodes[(std::size_t)n.child2];
        if (c1.parent != id || c2.parent != id) return false;
        if (n.height != 1 + std::max(c1.height, c2.height)) return false;
        if (!n.box.Contains(c1.box) || !n.box.Contains(c2.box)) return false;
        stack.push_back(n.child1);
        stack.push_back(n.child2);
    }
    return leaves == m_leaves && visited + (int)m_freeCount == (int)m_nodes.size();
}

} // namespace phys
} // namespace okay
