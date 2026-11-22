// okay::phys::DynamicTree, the broad phase of our own physics core.
//  - Random inserts, moves and removals keep a valid tree (parents, heights, enclosing boxes).
//  - Box queries and rays find exactly what testing every leaf finds.
//  - A leaf that moves inside its fat box is not re-inserted.
//  - Pair finding from the tree gives exactly the pairs of the sort-and-sweep broad phase (Physics/Broadphase.hpp).
//  - Timing (printed, not checked): pairs per step with sort-and-sweep from scratch versus the incremental tree, for
//    1,000 / 5,000 / 20,000 boxes where a tenth of them move each step.
#include "test_framework.hpp"
#include "physcore_test_rng.hpp"
#include "okay/Physics/Core/DynamicTree.hpp"
#include "okay/Physics/Broadphase.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <cstdint>
#include <set>
#include <vector>

using namespace okay::phys;

namespace {
using Lcg = okaytest::CoreRng;

Aabb RandomBox(Lcg& r, float extent) {
    const V3 c{r.Range(-extent, extent), r.Range(-extent, extent), r.Range(-extent, extent)};
    const V3 h{r.Range(0.1f, 1.0f), r.Range(0.1f, 1.0f), r.Range(0.1f, 1.0f)};
    return {c - h, c + h};
}

double NowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Pairs among the tree's leaves whose TIGHT boxes overlap, from the tree: each leaf queries its fat box.
std::vector<std::pair<std::uint32_t, std::uint32_t>> TreePairs(const DynamicTree& tree, const std::vector<int>& proxy,
                                                               const std::vector<Aabb>& tight) {
    std::vector<std::uint64_t> keys;
    for (std::size_t i = 0; i < proxy.size(); ++i) {
        tree.Query(tight[i], [&](int p) {
            const std::uint32_t j = (std::uint32_t)tree.User(p);
            if (j > i && tight[i].Overlaps(tight[j])) keys.push_back(((std::uint64_t)i << 32) | j);
            return true;
        });
    }
    std::sort(keys.begin(), keys.end());
    std::vector<std::pair<std::uint32_t, std::uint32_t>> out;
    for (auto k : keys) out.emplace_back((std::uint32_t)(k >> 32), (std::uint32_t)k);
    return out;
}

okay::broadphase::Box ToBox(const Aabb& a) {
    okay::broadphase::Box b;
    for (int k = 0; k < 3; ++k) { b.lo[k] = a.lo[k]; b.hi[k] = a.hi[k]; }
    return b;
}
} // namespace

int main() {
    RUN_SUITE("physcore tree: random edits keep a valid tree, queries match brute force");
    {
        Lcg r(7);
        DynamicTree tree;
        std::vector<int> proxy;
        std::vector<Aabb> tight;
        for (int step = 0; step < 4000; ++step) {
            const std::uint32_t op = r.Next() % 10;
            if (op < 5 || proxy.empty()) {
                const Aabb b = RandomBox(r, 30.0f);
                proxy.push_back(tree.Create(b, (std::uint64_t)tight.size()));
                tight.push_back(b);
            } else if (op < 8) {
                const std::size_t i = r.Next() % proxy.size();
                if (proxy[i] < 0) continue;
                const V3 d{r.Range(-1, 1), r.Range(-1, 1), r.Range(-1, 1)};
                tight[i] = {tight[i].lo + d, tight[i].hi + d};
                tree.Move(proxy[i], tight[i], d);
            } else {
                const std::size_t i = r.Next() % proxy.size();
                if (proxy[i] < 0) continue;
                tree.Destroy(proxy[i]);
                proxy[i] = -1;
            }
        }
        CHECK(tree.Validate());
        int live = 0;
        for (int p : proxy) live += p >= 0;
        CHECK(tree.LeafCount() == live);
        bool fatHoldsTight = true;
        for (std::size_t i = 0; i < proxy.size(); ++i)
            if (proxy[i] >= 0 && !tree.FatAabb(proxy[i]).Contains(tight[i])) fatHoldsTight = false;
        CHECK(fatHoldsTight);

        // box queries
        bool same = true;
        for (int q = 0; q < 200; ++q) {
            const Aabb qb = RandomBox(r, 30.0f).Grown(r.Range(0.0f, 5.0f));
            std::set<int> got, want;
            tree.Query(qb, [&](int p) { got.insert(p); return true; });
            for (std::size_t i = 0; i < proxy.size(); ++i)
                if (proxy[i] >= 0 && tree.FatAabb(proxy[i]).Overlaps(qb)) want.insert(proxy[i]);
            if (got != want) same = false;
        }
        CHECK(same);

        // rays: every leaf whose fat box the ray enters is reported when the callback never clips
        bool raysSame = true;
        for (int q = 0; q < 200; ++q) {
            const V3 o{r.Range(-40, 40), r.Range(-40, 40), r.Range(-40, 40)};
            const V3 d = Normalize(V3{r.Range(-1, 1), r.Range(-1, 1), r.Range(-1, 1)});
            if (q % 17 == 0) { /* axis-aligned rays hit the d == 0 branch */ }
            const V3 dd = (q % 17 == 0) ? V3{1, 0, 0} : d;
            const float maxT = r.Range(5.0f, 80.0f);
            std::set<int> got, want;
            tree.RayCast(o, dd, maxT, [&](int p, float t) { got.insert(p); return t; });
            V3 inv;
            for (int k = 0; k < 3; ++k) inv[k] = dd[k] != 0.0f ? 1.0f / dd[k] : 1e30f;
            for (std::size_t i = 0; i < proxy.size(); ++i)
                if (proxy[i] >= 0 && DynamicTree::RayHitsBox(o, dd, inv, tree.FatAabb(proxy[i]), maxT)) want.insert(proxy[i]);
            if (got != want) raysSame = false;
        }
        CHECK(raysSame);
        CHECK(tree.Height() < 64);   // a balanced-enough tree for 2,000 leaves
    }

    RUN_SUITE("physcore tree: a small move inside the fat box is not re-inserted");
    {
        DynamicTree tree;
        tree.margin = 0.1f;
        const Aabb b{{0, 0, 0}, {1, 1, 1}};
        const int p = tree.Create(b, 1);
        const V3 d{0.05f, 0, 0};
        CHECK(!tree.Move(p, {b.lo + d, b.hi + d}, d));
        const V3 far{0.5f, 0, 0};
        CHECK(tree.Move(p, {b.lo + far, b.hi + far}, far));
        CHECK(tree.FatAabb(p).Contains({b.lo + far, b.hi + far}));
        // the fat box is stretched along the motion
        CHECK(tree.FatAabb(p).hi.x > b.hi.x + far.x + 0.1f + 0.5f);
        tree.Destroy(p);
        CHECK(tree.LeafCount() == 0 && tree.Validate());
    }

    RUN_SUITE("physcore tree: pairs equal the sort-and-sweep pairs, and timing at 1k / 5k / 20k boxes");
    for (int n : {1000, 5000, 20000}) {
        Lcg r(1234 + (std::uint64_t)n);
        const float extent = 0.75f * std::cbrt((float)n) * 2.0f;   // about the same crowding at every size
        std::vector<Aabb> tight((std::size_t)n);
        std::vector<int> proxy((std::size_t)n);
        DynamicTree tree;
        for (int i = 0; i < n; ++i) { tight[(std::size_t)i] = RandomBox(r, extent); proxy[(std::size_t)i] = tree.Create(tight[(std::size_t)i], (std::uint64_t)i); }
        const int steps = 10;
        double sapMs = 0.0, treeMs = 0.0;
        bool equal = true;
        std::size_t pairCount = 0;
        std::vector<std::uint64_t> fatPairs;   // pairs whose fat boxes overlap, kept between steps
        for (int s = 0; s < steps; ++s) {
            // a tenth of the boxes move a little
            for (int i = s % 10; i < n; i += 10) {
                const V3 d{r.Range(-0.05f, 0.05f), r.Range(-0.08f, 0.0f), r.Range(-0.05f, 0.05f)};
                tight[(std::size_t)i] = {tight[(std::size_t)i].lo + d, tight[(std::size_t)i].hi + d};
            }
            double t0 = NowMs();
            std::vector<okay::broadphase::Box> boxes((std::size_t)n);
            std::vector<char> ok((std::size_t)n, 1);
            for (int i = 0; i < n; ++i) boxes[(std::size_t)i] = ToBox(tight[(std::size_t)i]);
            const auto sap = okay::broadphase::Pairs(boxes, ok);
            double t1 = NowMs();
            // incremental: only leaves that left their fat box look for new partners; known pairs are kept while
            // their fat boxes still overlap (what the physics world does every step)
            std::vector<int> moved;
            for (int i = s % 10; i < n; i += 10)
                if (tree.Move(proxy[(std::size_t)i], tight[(std::size_t)i], V3{})) moved.push_back(i);
            if (s == 0) { moved.clear(); for (int i = 0; i < n; ++i) moved.push_back(i); }
            std::vector<std::uint64_t> fresh;
            for (int i : moved)
                tree.Query(tree.FatAabb(proxy[(std::size_t)i]), [&](int p) {
                    const std::uint32_t j = (std::uint32_t)tree.User(p);
                    if (j != (std::uint32_t)i) {
                        const std::uint32_t lo = std::min((std::uint32_t)i, j), hi = std::max((std::uint32_t)i, j);
                        fresh.push_back(((std::uint64_t)lo << 32) | hi);
                    }
                    return true;
                });
            std::sort(fresh.begin(), fresh.end());
            std::vector<std::uint64_t> merged;
            merged.reserve(fatPairs.size() + fresh.size());
            std::merge(fatPairs.begin(), fatPairs.end(), fresh.begin(), fresh.end(), std::back_inserter(merged));
            merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
            fatPairs.clear();
            std::vector<std::pair<std::uint32_t, std::uint32_t>> tp;
            for (std::uint64_t k : merged) {
                const std::uint32_t a = (std::uint32_t)(k >> 32), b = (std::uint32_t)k;
                if (!tree.FatAabb(proxy[a]).Overlaps(tree.FatAabb(proxy[b]))) continue;
                fatPairs.push_back(k);
                if (tight[a].Overlaps(tight[b])) tp.emplace_back(a, b);
            }
            double t2 = NowMs();
            if (s == 0) { const auto all = TreePairs(tree, proxy, tight); if (all != tp) equal = false; }
            sapMs += t1 - t0;
            treeMs += t2 - t1;
            if (tp != sap) equal = false;
            pairCount = sap.size();
        }
        CHECK(equal);
        CHECK(tree.Validate());
        std::cout << "  " << n << " boxes, " << pairCount << " pairs: sort-and-sweep " << sapMs / steps
                  << " ms/step, tree " << treeMs / steps << " ms/step (height " << tree.Height() << ", area ratio "
                  << tree.AreaRatio() << ")\n";
    }

    TEST_MAIN_RESULT();
}
