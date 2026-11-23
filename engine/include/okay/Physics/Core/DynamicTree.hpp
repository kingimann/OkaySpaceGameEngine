#pragma once
// ---------------------------------------------------------------------------
// Dynamic AABB tree: the broad phase of our physics core (okay::phys).
//
// A binary tree of axis-aligned boxes. Each leaf holds one collider's "fat" box (its real box grown by `margin`), each
// inner node the union of its two children. A collider that moves only re-enters the tree when its real box leaves its
// fat box, so a body that rests or jiggles costs nothing; a box query or a ray visits only the branches it touches.
//
// Written from the published descriptions only:
//  * bounding volume hierarchies, top-down queries and ray tests: C. Ericson, "Real-Time Collision Detection" (2005), ch. 6;
//  * incremental insertion choosing the sibling with the least added surface area (the surface area heuristic):
//    J. Goldsmith and J. Salmon, "Automatic creation of object hierarchies for ray tracing" (IEEE CG&A, 1987), and the
//    branch-and-bound search for the best sibling of J. Bittner, M. Hapala, V. Havran, "Incremental BVH construction for
//    ray tracing" (Computers & Graphics, 2015);
//  * local tree rotations that shrink the summed area after a change: D. Kopta et al., "Fast, effective BVH updates for
//    animated scenes" (I3D 2012).
// No other engine's code was read or used.
//
// Deterministic: the tree's shape depends only on the order of Create / Move / Destroy calls and their boxes.
// Header has no <mutex>/<thread>; the tree is not thread safe for writes (queries from many threads are fine).
// ---------------------------------------------------------------------------
#include "okay/Physics/Core/PMath.hpp"
#include <cstdint>
#include <vector>

namespace okay {
namespace phys {

class DynamicTree {
public:
    static constexpr int kNull = -1;

    /// How much a leaf's box is grown on every side when (re)inserted. Bigger = fewer re-inserts, more false pairs.
    float margin = 0.1f;
    /// A moving leaf's box is also stretched this many times its last displacement ahead, so a steadily moving body
    /// re-enters the tree less often.
    float displacementScale = 2.0f;

    /// Add a leaf for `tight` (grown by margin); returns its proxy id.
    int Create(const Aabb& tight, std::uint64_t user);
    void Destroy(int proxy);
    /// Tell the tree the leaf's real box is now `tight` after moving by `displacement`. Returns true when the leaf was
    /// re-inserted (its fat box changed), false when the old fat box still holds it.
    bool Move(int proxy, const Aabb& tight, const V3& displacement);

    const Aabb& FatAabb(int proxy) const { return m_nodes[(std::size_t)proxy].box; }
    std::uint64_t User(int proxy) const { return m_nodes[(std::size_t)proxy].user; }
    int LeafCount() const { return m_leaves; }
    int NodeCount() const { return (int)m_nodes.size() - (int)m_freeCount; }
    int Root() const { return m_root; }
    int Height() const { return m_root == kNull ? 0 : m_nodes[(std::size_t)m_root].height; }
    /// Sum of inner-node areas over the root area (a quality number: lower is a better tree).
    float AreaRatio() const;
    /// Check every structural rule (parents, heights, boxes enclose children). For tests; returns false on a broken tree.
    bool Validate() const;

    /// Calls cb(proxy) for every leaf whose fat box overlaps `box`. cb returns false to stop early.
    template <class F> void Query(const Aabb& box, F&& cb) const {
        if (m_root == kNull) return;
        int stack[256];
        std::vector<int> big;
        int sp = 0;
        stack[sp++] = m_root;
        while (sp > 0 || !big.empty()) {
            int id;
            if (!big.empty()) { id = big.back(); big.pop_back(); }
            else id = stack[--sp];
            const Node& n = m_nodes[(std::size_t)id];
            if (!n.box.Overlaps(box)) continue;
            if (n.child1 == kNull) {
                if (!cb(id)) return;
            } else {
                if (sp + 2 <= 256) { stack[sp++] = n.child1; stack[sp++] = n.child2; }
                else { big.push_back(n.child1); big.push_back(n.child2); }
            }
        }
    }

    /// Ray from `o` along `d` (any length) for t in [0, maxT]. cb(proxy, maxT) is called for leaves whose fat box the
    /// ray enters before maxT; it returns the new clip distance (return maxT unchanged to keep going, a smaller value to
    /// clip the search to the nearest hit so far, or a negative value to stop).
    template <class F> void RayCast(const V3& o, const V3& d, float maxT, F&& cb) const {
        if (m_root == kNull) return;
        V3 inv;
        for (int k = 0; k < 3; ++k) inv[k] = d[k] != 0.0f ? 1.0f / d[k] : (d[k] >= 0.0f ? 1e30f : -1e30f);
        std::vector<int> stack;
        stack.reserve(64);
        stack.push_back(m_root);
        while (!stack.empty()) {
            const int id = stack.back(); stack.pop_back();
            const Node& n = m_nodes[(std::size_t)id];
            if (!RayHitsBox(o, d, inv, n.box, maxT)) continue;
            if (n.child1 == kNull) {
                const float t = cb(id, maxT);
                if (t < 0.0f) return;
                maxT = t;
            } else {
                stack.push_back(n.child2);
                stack.push_back(n.child1);
            }
        }
    }

    /// Slab test (Ericson 5.3.3): does the ray enter `b` for some t in [0, maxT]?
    static bool RayHitsBox(const V3& o, const V3& d, const V3& inv, const Aabb& b, float maxT) {
        float t0 = 0.0f, t1 = maxT;
        for (int k = 0; k < 3; ++k) {
            if (d[k] == 0.0f) {
                if (o[k] < b.lo[k] || o[k] > b.hi[k]) return false;
                continue;
            }
            float a = (b.lo[k] - o[k]) * inv[k], c = (b.hi[k] - o[k]) * inv[k];
            if (a > c) { const float s = a; a = c; c = s; }
            if (a > t0) t0 = a;
            if (c < t1) t1 = c;
            if (t0 > t1) return false;
        }
        return true;
    }

private:
    struct Node {
        Aabb box;
        std::uint64_t user = 0;
        int parent = kNull;   // also the next free node while free
        int child1 = kNull, child2 = kNull;
        int height = 0;       // leaf = 0, free = -1
    };
    std::vector<Node> m_nodes;
    int m_root = kNull;
    int m_free = kNull;
    std::size_t m_freeCount = 0;
    int m_leaves = 0;

    int Alloc();
    void Free(int id);
    void InsertLeaf(int leaf);
    void RemoveLeaf(int leaf);
    int BestSibling(const Aabb& box) const;
    void Rotate(int id);
};

} // namespace phys
} // namespace okay
