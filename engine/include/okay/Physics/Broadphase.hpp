#pragma once
// ---------------------------------------------------------------------------
// Broad phase: which pairs of colliders have overlapping boxes. Shared by Physics2D and Physics3D.
//
// Before this, each physics step tested every collider against every other (n * (n - 1) / 2 pairs, each one
// behind several checks), so 2,500 colliders cost about three million pair visits per step even when
// almost nothing touched. Pairs() finds the same pairs by sorting the boxes along their widest axis and
// sweeping: boxes that cannot overlap on that axis are never compared.
//
// The result is EXACTLY what the old double loop would have visited, in the same order:
//   * the same overlap test (touching counts as overlapping; separated on any axis means no pair),
//   * only pairs with both ends marked usable (the old loop skipped the others without side effects),
//   * sorted by (i, j) with i < j, which is the order `for i { for j > i }` found them in.
// The solver applies impulses in pair order, so keeping the order is what keeps every body's motion
// bit-for-bit the same. PairsBrute() is the old loop, kept as the reference for the tests
// (tests/test_broadphase.cpp) and as a switch (ForceBruteForce) to compare whole scenes.
//
// Written from scratch from the general sort-and-sweep (sweep and prune) idea; no other code was used.
// Header only; no <mutex>, <thread> or other headers the Windows build cannot use.
// ---------------------------------------------------------------------------
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace okay {
namespace broadphase {

/// An axis-aligned box. 2D callers leave z at 0.
struct Box {
    float lo[3] = {0, 0, 0};
    float hi[3] = {0, 0, 0};
};

using PairList = std::vector<std::pair<std::uint32_t, std::uint32_t>>;

/// True when the boxes are apart on some axis (the old test: `aMax < bMin || aMin > bMax`, per axis).
inline bool Separated(const Box& a, const Box& b) {
    for (int k = 0; k < 3; ++k)
        if (a.hi[k] < b.lo[k] || a.lo[k] > b.hi[k]) return true;
    return false;
}

/// Debug switch: when true, Pairs() runs the old all-pairs loop. For tests that compare a whole scene both ways.
inline bool& ForceBruteForce() { static bool v = false; return v; }

/// Every (i, j), i < j, with ok[i] and ok[j] whose boxes overlap, in the order of the plain double loop.
inline PairList PairsBrute(const std::vector<Box>& boxes, const std::vector<char>& ok) {
    PairList out;
    const std::size_t n = boxes.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (!ok[i]) continue;
        for (std::size_t j = i + 1; j < n; ++j) {
            if (!ok[j]) continue;
            if (Separated(boxes[i], boxes[j])) continue;
            out.emplace_back((std::uint32_t)i, (std::uint32_t)j);
        }
    }
    return out;
}

/// The same list as PairsBrute, found by sort and sweep. `ok` marks boxes that may pair (finite ones); the others are
/// ignored. Boxes must hold finite numbers where ok is set.
inline PairList Pairs(const std::vector<Box>& boxes, const std::vector<char>& ok) {
    if (ForceBruteForce()) return PairsBrute(boxes, ok);
    const std::size_t n = boxes.size();
    std::vector<std::uint32_t> order;
    order.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        if (ok[i]) order.push_back((std::uint32_t)i);
    PairList out;
    if (order.size() < 2) return out;

    // The axis where the boxes are most spread out has the fewest overlaps along it.
    int axis = 0;
    {
        double best = -1.0;
        for (int k = 0; k < 3; ++k) {
            double sum = 0.0, sum2 = 0.0;
            for (std::uint32_t i : order) { const double c = (double)boxes[i].lo[k]; sum += c; sum2 += c * c; }
            const double m = sum / (double)order.size();
            const double var = sum2 / (double)order.size() - m * m;
            if (var > best) { best = var; axis = k; }
        }
    }

    std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
        const float la = boxes[a].lo[axis], lb = boxes[b].lo[axis];
        return la < lb || (la == lb && a < b);
    });

    std::vector<std::uint64_t> keys;
    for (std::size_t p = 0; p < order.size(); ++p) {
        const std::uint32_t i = order[p];
        const Box& bi = boxes[i];
        for (std::size_t q = p + 1; q < order.size(); ++q) {
            const std::uint32_t j = order[q];
            const Box& bj = boxes[j];
            if (bj.lo[axis] > bi.hi[axis]) break;   // sorted by lo: everything after is further along the axis
            if (Separated(bi, bj)) continue;
            const std::uint32_t lo = i < j ? i : j, hi = i < j ? j : i;
            keys.push_back(((std::uint64_t)lo << 32) | hi);
        }
    }
    std::sort(keys.begin(), keys.end());
    out.reserve(keys.size());
    for (std::uint64_t k : keys) out.emplace_back((std::uint32_t)(k >> 32), (std::uint32_t)(k & 0xFFFFFFFFu));
    return out;
}

} // namespace broadphase
} // namespace okay
