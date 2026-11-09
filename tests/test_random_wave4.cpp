// 1.1 fourth pass: Random and RandomExtras audited for NaN, infinite, empty, zero and negative arguments, determinism after a
// state save and restore, and thread use of the documented entry points; plus what an author needed and did not have:
// SampleIndices (k different picks), PoissonDisk2D (blue-noise points) and saving a ShuffleBag or WeightedTable mid-round.
// Fixed seeds, no clocks.
#include "test_framework.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Core/RandomExtras.hpp"
#include "okay/Core/Rng.hpp"
#include "okay/Core/SaveFramework.hpp"
#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kFltMax = std::numeric_limits<float>::max();

bool Finite(Vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }
bool Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool SameState(const Random& a, const Random& b) {
    return a.GetState().s0 == b.GetState().s0 && a.GetState().s1 == b.GetState().s1;
}

}  // namespace

int main() {
    RUN_SUITE("random_wave4");

    // ---- Gaussian with bad arguments: no NaN, no infinity, the same two draws -------------------------------------------------
    {
        Random ref(7);
        ref.Gaussian(0.0f, 1.0f);
        const float bad[] = {kNaN, kInf, -kInf, kFltMax, -kFltMax};
        bool finite = true, steady = true;
        for (float m : bad)
            for (float sd : bad) {
                Random r(7);
                const float v = r.Gaussian(m, sd);
                if (!std::isfinite(v)) finite = false;
                if (!SameState(r, ref)) steady = false;     // always two draws, so a stream stays in step
            }
        CHECK(finite && steady);
        Random r(7);
        CHECK(r.Gaussian(5.0f, kNaN) == 5.0f);              // a NaN spread: the mean
        Random r2(7);
        CHECK(r2.Gaussian(kNaN, 0.0f) == 0.0f);             // a NaN mean: 0
        Random r3(7);
        CHECK(r3.Gaussian(3.0f, 0.0f) == 3.0f && r3.Gaussian(3.0f, -0.0f) == 3.0f);
        Random a(9), b(9);
        CHECK(a.Gaussian(1.0f, 2.0f) == b.Gaussian(1.0f, 2.0f));   // ordinary arguments are unchanged
        Random neg(7), pos(7);
        CHECK(neg.Gaussian(0.0f, -2.0f) == -pos.Gaussian(0.0f, 2.0f));   // a negative spread mirrors (it did and still does)
    }

    // ---- the position helpers: bad numbers are 0, never NaN, and the draws do not depend on the arguments --------------------------------
    {
        Random good(11);
        rnd::PointInCircle(good, 3.0f);
        const float bad[] = {kNaN, kInf, -kInf, 0.0f, -2.0f};
        bool finite = true, steady = true;
        for (float x : bad) {
            Random r1(11), r2(11), r3(11), r4(11), r5(11), r6(11), r7(11);
            if (!Finite(rnd::PointInCircle(r1, x, Vec2{x, 1.0f}))) finite = false;
            if (!Finite(rnd::PointOnCircle(r2, x, Vec2{1.0f, x}))) finite = false;
            if (!Finite(rnd::PointInRing(r3, x, 2.0f, Vec2{x, x}))) finite = false;
            if (!Finite(rnd::PointInSphere(r4, x, Vec3{x, 0, 0}))) finite = false;
            if (!Finite(rnd::PointOnSphere(r5, x, Vec3{0, x, 0}))) finite = false;
            if (!Finite(rnd::PointInRect(r6, Vec2{x, 0}, Vec2{1, x}))) finite = false;
            if (!Finite(rnd::PointInBox(r7, Vec3{x, 0, 0}, Vec3{1, x, x}))) finite = false;
            if (!SameState(r1, good)) steady = false;
        }
        CHECK(finite && steady);
        Random z(3);
        const Vec2 p = rnd::PointInCircle(z, kNaN, Vec2{4, 5});
        CHECK(p.x == 4.0f && p.y == 5.0f);                      // a NaN radius is a radius of 0: the centre
        {   // the largest radius around the origin stays finite
            Random hr(2);
            CHECK(Finite(rnd::PointInCircle(hr, kFltMax)) && Finite(rnd::PointOnCircle(hr, kFltMax)) && Finite(rnd::PointInSphere(hr, kFltMax)) &&
                  Finite(rnd::PointOnSphere(hr, -kFltMax)));
        }
        // a span wider than a float: worked out in double, inside the corners, never infinity
        Random w(5);
        bool inside = true;
        for (int i = 0; i < 200; ++i) {
            const Vec2 q = rnd::PointInRect(w, Vec2{-kFltMax, -kFltMax}, Vec2{kFltMax, kFltMax});
            if (!Finite(q) || std::fabs(q.x) > kFltMax || std::fabs(q.y) > kFltMax) inside = false;
            const Vec3 b = rnd::PointInBox(w, Vec3{kFltMax, 0, 0}, Vec3{-kFltMax, 1, 1});
            if (!Finite(b)) inside = false;
        }
        CHECK(inside);
        // ordinary arguments give the numbers they always gave (a stream someone has seeded and recorded does not shift)
        Random e1(21), e2(21);
        const Vec2 o1 = rnd::PointInRect(e1, Vec2{2, 2}, Vec2{-2, -4});
        const float x = e2.Value(), y = e2.Value();
        CHECK(o1.x == std::fmin(2.0f, -2.0f) + x * std::fabs(-2.0f - 2.0f) && o1.y == std::fmin(2.0f, -4.0f) + y * std::fabs(-4.0f - 2.0f));
        // a huge ring radius no longer overflows in the square
        Random h(1);
        CHECK(Finite(rnd::PointInRing(h, 1e30f, 2e30f)) && Finite(rnd::PointInRing(h, 0.0f, kFltMax)));
    }

    // ---- other entry points with empty, zero, negative and non-finite arguments ----------------------------------------------------------
    {
        Random r(13);
        CHECK(r.Range(5, 5) == 5 && r.Range(5, 3) == 5 && r.Range(INT_MIN, INT_MAX) < INT_MAX);
        CHECK(r.Range(2.0f, 2.0f) == 2.0f);
        const float lo = r.Range(kNaN, 1.0f), hi = r.Range(1.0f, kInf);
        CHECK(lo == 0.0f && hi == 1.0f);                         // documented: a bad limit returns the other / 0
        CHECK(!r.Chance(kNaN) && !r.Chance(0.0f) && !r.Chance(-5.0f) && r.Chance(100.0f) && r.Chance(kInf));
        CHECK(r.Weighted(std::vector<float>()) == -1);
        CHECK(r.Weighted({0.0f, kNaN, -1.0f, kInf}) == 0);       // no usable weight: index 0
        CHECK(r.Weighted({kNaN, 5.0f, kNaN}) == 1);
        std::vector<int> none;
        r.Shuffle(none);
        std::vector<int> one{7};
        r.Shuffle(one);
        CHECK(none.empty() && one.size() == 1 && one[0] == 7);
        CHECK(r.Below(0) == 0 && r.Below(1) == 0);
        Rng pcg(5);
        CHECK(rnd::IndexBelow(pcg, 0) == 0 && rnd::IndexBelow(pcg, 1) == 0);
        CHECK(rnd::DirectionInCone(r, Vec3{kNaN, 0, 0}, kNaN).z > 0.999f);          // a bad axis is +Z, a bad angle 0
        CHECK(rnd::Sign(r) == 1 || rnd::Sign(r) == -1);
    }

    // ---- determinism after saving and restoring the state: every draw, every helper --------------------------------------------------------
    {
        auto tour = [](Random& r) {
            std::vector<double> v;
            v.push_back(r.Value());
            v.push_back(r.Range(-5.0f, 5.0f));
            v.push_back(r.Range(3, 90));
            v.push_back(r.Gaussian(1.0f, 2.0f));
            v.push_back(r.Bool() ? 1.0 : 0.0);
            v.push_back(r.Chance(30.0f) ? 1.0 : 0.0);
            v.push_back(r.Weighted({1.0f, 2.0f, 3.0f}));
            std::vector<int> s{1, 2, 3, 4, 5, 6, 7, 8};
            r.Shuffle(s);
            for (int x : s) v.push_back(x);
            const Vec2 c = r.InsideUnitCircle();
            v.push_back(c.x); v.push_back(c.y);
            const Vec3 d = r.OnUnitSphere();
            v.push_back(d.x); v.push_back(d.z);
            Random child = r.Fork();
            v.push_back((double)child.NextUInt64());
            const Vec2 p = rnd::PointInRing(r, 2.0f, 4.0f);
            v.push_back(p.x);
            const Vec3 q = rnd::DirectionInCone(r, Vec3{0, 1, 0}, 0.5f);
            v.push_back(q.y);
            v.push_back(rnd::SampleIndices(r, 50, 4)[0]);
            v.push_back((double)RollDice(r, "4d6kh3"));
            return v;
        };
        Random r(2024);
        for (int i = 0; i < 17; ++i) r.NextUInt64();
        const Random::State saved = r.GetState();
        const std::vector<double> first = tour(r);
        r.SetState(saved);
        CHECK(tour(r) == first);
        Random other(1);
        other.SetState(saved);
        CHECK(tour(other) == first);                             // another generator put on that state
        r.SetState(Random::State{0, 0});                         // the all-zero state never leaves zero: it is nudged
        CHECK(r.NextUInt64() != 0 || r.NextUInt64() != 0);
        // the loot stream a save game carries: saving and restoring Random::Shared() through the lock
        {
            auto shared = Random::LockShared();
            shared->Seed(99);
        }
        const Random::State s0 = Random::Shared().GetState();
        const std::uint64_t a = Random::Shared().NextUInt64();
        Random::Shared().SetState(s0);
        CHECK(Random::Shared().NextUInt64() == a);
    }

    // ---- threads: ThreadLocal is a stream per thread; LockShared hands out the one stream ------------------------------------------------------
    {
        std::vector<Random*> addr(4, nullptr);
        std::vector<std::vector<std::uint64_t>> got(4);
        std::vector<std::thread> pool;
        std::atomic<int> arrived{0};
        for (int t = 0; t < 4; ++t)
            pool.emplace_back([&, t] {
                Random& mine = Random::ThreadLocal();
                ++arrived;
                while (arrived.load() < 4) std::this_thread::yield();      // all four alive at once: no thread-local block is reused
                addr[(std::size_t)t] = &mine;
                for (int i = 0; i < 2000; ++i) got[(std::size_t)t].push_back(mine.NextUInt64());
            });
        for (std::thread& th : pool) th.join();
        std::set<Random*> distinct(addr.begin(), addr.end());
        CHECK(distinct.size() == 4);
        bool apart = true;
        for (int a = 0; a < 4; ++a) for (int b = a + 1; b < 4; ++b) if (got[(std::size_t)a] == got[(std::size_t)b] || got[(std::size_t)a][0] == got[(std::size_t)b][0]) apart = false;
        CHECK(apart);
        // the same sequential values whoever draws them: 4 threads x 1000 draws from the locked shared stream equal the first 4000 of it
        {
            auto shared = Random::LockShared();
            shared->Seed(31337);
        }
        std::uint64_t expectSum = 0;
        {
            Random ref(31337);
            for (int i = 0; i < 4000; ++i) expectSum += ref.NextUInt64();
        }
        std::atomic<std::uint64_t> sum{0};
        std::vector<std::thread> pool2;
        for (int t = 0; t < 4; ++t)
            pool2.emplace_back([&] {
                for (int i = 0; i < 1000; ++i) { auto rng = Random::LockShared(); sum += rng->NextUInt64(); }
            });
        for (std::thread& th : pool2) th.join();
        CHECK(sum.load() == expectSum);
        // and the state after is the sequential state: a restore from it continues the stream
        Random ref(31337);
        for (int i = 0; i < 4000; ++i) ref.NextUInt64();
        CHECK(SameState(Random::Shared(), ref));
    }

    // ---- SampleIndices: k different picks ------------------------------------------------------------------------------------------------
    {
        Random r(77);
        std::vector<std::size_t> v = rnd::SampleIndices(r, 40, 3);
        CHECK(v.size() == 3 && std::set<std::size_t>(v.begin(), v.end()).size() == 3 && *std::max_element(v.begin(), v.end()) < 40);
        CHECK(rnd::SampleIndices(r, 5, 0).empty() && rnd::SampleIndices(r, 0, 4).empty());
        v = rnd::SampleIndices(r, 6, 100);                                          // more than there are: all of them, shuffled
        CHECK(v.size() == 6 && std::set<std::size_t>(v.begin(), v.end()).size() == 6);
        v = rnd::SampleIndices(r, 1000000000, 5);                                   // a huge range: no list of a billion
        CHECK(v.size() == 5 && std::set<std::size_t>(v.begin(), v.end()).size() == 5 && *std::max_element(v.begin(), v.end()) < 1000000000);
        v = rnd::SampleIndices(r, 20, 20);
        std::vector<std::size_t> sorted = v;
        std::sort(sorted.begin(), sorted.end());
        bool perm = true;
        for (std::size_t i = 0; i < 20; ++i) if (sorted[i] != i) perm = false;
        CHECK(perm && v != sorted);                                                  // a permutation, and not the identity
        // fairness: 2 of 5, each index equally likely and each ordered pair equally likely (20 pairs)
        int hits[5] = {0, 0, 0, 0, 0};
        int pairs[5][5] = {};
        const int N = 40000;
        for (int i = 0; i < N; ++i) {
            const std::vector<std::size_t> p = rnd::SampleIndices(r, 5, 2);
            ++hits[p[0]]; ++hits[p[1]]; ++pairs[p[0]][p[1]];
        }
        bool fair = true;
        for (int i = 0; i < 5; ++i) if (std::fabs(hits[i] / (double)N - 0.4) > 0.02) fair = false;
        for (int i = 0; i < 5; ++i) for (int j = 0; j < 5; ++j) {
            if (i == j) { if (pairs[i][j] != 0) fair = false; }
            else if (std::fabs(pairs[i][j] / (double)N - 0.05) > 0.01) fair = false;
        }
        CHECK(fair);
        // the other path (k * 2 >= n: a partial shuffle of the list): 2 of 4, every ordered pair equally likely
        int pairs4[4][4] = {};
        for (int i = 0; i < 48000; ++i) { const std::vector<std::size_t> p = rnd::SampleIndices(r, 4, 2); ++pairs4[p[0]][p[1]]; }
        bool fair4 = true;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
            if (i == j) { if (pairs4[i][j] != 0) fair4 = false; }
            else if (std::fabs(pairs4[i][j] / 48000.0 - 1.0 / 12.0) > 0.01) fair4 = false;
        }
        CHECK(fair4);
        // the small-share path (Floyd) is fair too: 2 of 12 (k * 2 < n), first pick uniform over 12
        int first[12] = {};
        for (int i = 0; i < 24000; ++i) ++first[rnd::SampleIndices(r, 12, 2)[0] % 12];
        bool even = true;
        for (int i = 0; i < 12; ++i) if (std::fabs(first[i] / 24000.0 - 1.0 / 12.0) > 0.012) even = false;
        CHECK(even);
        // deterministic for a state, and works with the PCG generator too
        Random a(5), b(5);
        CHECK(rnd::SampleIndices(a, 100, 10) == rnd::SampleIndices(b, 100, 10));
        Rng pcg(5);
        const std::vector<std::size_t> w = rnd::SampleIndices(pcg, 100, 10);
        CHECK(w.size() == 10 && std::set<std::size_t>(w.begin(), w.end()).size() == 10);
    }

    // ---- PoissonDisk2D: blue-noise points ------------------------------------------------------------------------------------------------------
    {
        Random r(1234);
        const float W = 100.0f, H = 60.0f, D = 5.0f;
        const std::vector<Vec2> pts = rnd::PoissonDisk2D(r, W, H, D);
        CHECK(pts.size() > 120 && pts.size() < 220);     // about 58% of the hexagonal packing (6000 / (0.866 * 25) = 277: 150 to 170 in practice)
        bool inBounds = true, apart = true;
        for (const Vec2& p : pts) if (!(p.x >= 0.0f && p.x < W && p.y >= 0.0f && p.y < H)) inBounds = false;
        for (std::size_t i = 0; i < pts.size() && apart; ++i)
            for (std::size_t j = i + 1; j < pts.size(); ++j) {
                const double dx = (double)pts[i].x - pts[j].x, dy = (double)pts[i].y - pts[j].y;
                if (dx * dx + dy * dy < (double)D * D) { apart = false; break; }
            }
        CHECK(inBounds && apart);
        // no big gaps: almost every probe point has a sample within 2 * D
        Random probe(99);
        int near2 = 0, total = 3000;
        double worst = 0.0;
        for (int i = 0; i < total; ++i) {
            const float x = probe.Range(0.0f, W), y = probe.Range(0.0f, H);
            double best = 1e30;
            for (const Vec2& p : pts) best = std::min(best, std::sqrt(((double)p.x - x) * ((double)p.x - x) + ((double)p.y - y) * ((double)p.y - y)));
            if (best <= 2.0 * D) ++near2;
            worst = std::max(worst, best);
        }
        CHECK(near2 >= total * 99 / 100 && worst < 2.6 * D);
        // deterministic, and a different state gives a different set
        Random a(1234), b(1234), c(4321);
        CHECK(rnd::PoissonDisk2D(a, W, H, D) == rnd::PoissonDisk2D(b, W, H, D));
        CHECK(rnd::PoissonDisk2D(c, W, H, D) != pts);
        // the origin shifts everything; the cap holds; more attempts pack at least about as tight
        Random o1(5), o2(5);
        const std::vector<Vec2> base = rnd::PoissonDisk2D(o1, 20, 20, 2.0f);
        const std::vector<Vec2> moved = rnd::PoissonDisk2D(o2, 20, 20, 2.0f, 30, 100000, Vec2{100, -50});
        bool shifted = base.size() == moved.size() && !base.empty();
        for (std::size_t i = 0; shifted && i < base.size(); ++i) if (moved[i].x != base[i].x + 100.0f || moved[i].y != base[i].y - 50.0f) shifted = false;
        CHECK(shifted);
        Random cap(5);
        CHECK(rnd::PoissonDisk2D(cap, 100, 100, 1.0f, 30, 10).size() == 10);
        Random one(5);
        CHECK(rnd::PoissonDisk2D(one, 100, 100, 1.0f, 30, 1).size() == 1);
        Random lazy(5), keen(5);
        const std::size_t few = rnd::PoissonDisk2D(lazy, 100, 100, 4.0f, 1).size(), many = rnd::PoissonDisk2D(keen, 100, 100, 4.0f, 60).size();
        CHECK(many > few + 50);                          // giving up after one failed attempt leaves almost nothing (the walk dies at once)
        // a rectangle smaller than the distance holds one point; a strip much thinner than the distance keeps what it finds valid
        Random tiny(5);
        CHECK(rnd::PoissonDisk2D(tiny, 1, 1, 50.0f).size() == 1);
        Random sliver(5);
        const std::vector<Vec2> line = rnd::PoissonDisk2D(sliver, 200, 1, 10.0f, 100);
        bool lineOk = !line.empty();
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (!(line[i].x >= 0.0f && line[i].x < 200.0f && line[i].y >= 0.0f && line[i].y < 1.0f)) lineOk = false;
            for (std::size_t j = i + 1; j < line.size(); ++j)
                if (std::hypot((double)line[i].x - line[j].x, (double)line[i].y - line[j].y) < 10.0 - 1e-4) lineOk = false;
        }
        CHECK(lineOk);
        // bad arguments: nothing, and nothing allocated
        Random bad(5);
        CHECK(rnd::PoissonDisk2D(bad, 0, 10, 1).empty() && rnd::PoissonDisk2D(bad, 10, -1, 1).empty() && rnd::PoissonDisk2D(bad, 10, 10, 0).empty());
        CHECK(rnd::PoissonDisk2D(bad, kNaN, 10, 1).empty() && rnd::PoissonDisk2D(bad, 10, kInf, 1).empty() && rnd::PoissonDisk2D(bad, 10, 10, kNaN).empty());
        CHECK(rnd::PoissonDisk2D(bad, 10, 10, -3).empty() && rnd::PoissonDisk2D(bad, 10, 10, 1, 30, 0).empty());
        CHECK(rnd::PoissonDisk2D(bad, 1e6f, 1e6f, 0.001f).empty());                      // ten billion cells: refused
        CHECK(rnd::PoissonDisk2D(bad, 10, 10, 1, -5).size() >= 1 && rnd::PoissonDisk2D(bad, 10, 10, 1, 100000).size() >= 1);   // attempts are clamped
        CHECK(Finite(rnd::PoissonDisk2D(bad, 10, 10, 1, 30, 100000, Vec2{kNaN, kInf}).front()));   // a bad origin is 0
        // with the PCG generator
        Rng pcg(8);
        const std::vector<Vec2> q = rnd::PoissonDisk2D(pcg, 50, 50, 5.0f);
        CHECK(q.size() > 40);
    }

    // ---- ShuffleBag saved mid-round --------------------------------------------------------------------------------------------------------------
    {
        auto build = [] {
            ShuffleBag<std::string> bag;
            bag.Add("a"); bag.Add("b"); bag.Add("c", 2); bag.Add("d"); bag.Add("e");
            return bag;
        };
        ShuffleBag<std::string> live = build();
        Random rng(555);
        for (int i = 0; i < 9; ++i) live.Draw(rng);                                        // mid-round on purpose (6 per round)
        const ShuffleBag<std::string>::State st = live.SaveState();
        const Random::State rs = rng.GetState();
        std::vector<std::string> expect;
        for (int i = 0; i < 25; ++i) expect.push_back(live.Draw(rng));

        ShuffleBag<std::string> back = build();
        Random rng2(1);
        rng2.SetState(rs);
        CHECK(back.RestoreState(st));
        CHECK(back.Remaining() == st.order.size() && back.Rounds() == st.rounds);
        std::vector<std::string> got;
        for (int i = 0; i < 25; ++i) got.push_back(back.Draw(rng2));
        CHECK(got == expect);                                                              // the bag is where it was: the round finishes the same way
        // without the bag's state the draws differ (the reason this exists)
        ShuffleBag<std::string> fresh = build();
        Random rng3(1);
        rng3.SetState(rs);
        std::vector<std::string> naive;
        for (int i = 0; i < 25; ++i) naive.push_back(fresh.Draw(rng3));
        CHECK(naive != expect);

        // through the save framework
        SaveDocument doc;
        ShuffleBag<std::string> live2 = build();
        Random rng4(9);
        for (int i = 0; i < 4; ++i) live2.Draw(rng4);
        live2.Save(SaveWriter(doc.Block("bag")));
        const Random::State rs4 = rng4.GetState();
        std::vector<std::string> expect2;
        for (int i = 0; i < 12; ++i) expect2.push_back(live2.Draw(rng4));
        ShuffleBag<std::string> loaded = build();
        CHECK(loaded.Load(SaveReader(doc.Block("bag"))));
        Random rng5(1);
        rng5.SetState(rs4);
        std::vector<std::string> got2;
        for (int i = 0; i < 12; ++i) got2.push_back(loaded.Draw(rng5));
        CHECK(got2 == expect2);

        // refusals leave the bag alone
        ShuffleBag<std::string> target = build();
        Random rngT(3);
        target.Draw(rngT);
        const std::size_t remaining = target.Remaining();
        ShuffleBag<std::string>::State bad = st;
        bad.order = {0, 0};                                                                // "a" has one copy
        CHECK(!target.RestoreState(bad) && target.Remaining() == remaining);
        bad = st; bad.order = {99};
        CHECK(!target.RestoreState(bad) && target.Remaining() == remaining);
        bad = st; bad.last = 99;
        CHECK(!target.RestoreState(bad));
        bad = st; bad.last = -2;
        CHECK(!target.RestoreState(bad));
        bad = st; bad.rounds = -1;
        CHECK(!target.RestoreState(bad));
        bad = st; bad.order.assign(7, 2);                                                  // more entries than the bag holds
        CHECK(!target.RestoreState(bad) && target.Remaining() == remaining);
        ShuffleBag<std::string> other;
        other.Add("a"); other.Add("b");                                                    // another shape
        CHECK(!other.Load(SaveReader(doc.Block("bag"))));
        CHECK(!target.Load(SaveReader(doc.Block("nothing"))));                             // no state in the block
        SaveDocument junk;
        SaveWriter jw(junk.Block("bag"));
        jw.Int("entries", 5); jw.Int("slots", 6); jw.String("order", "1 x 2"); jw.Int("rounds", 1); jw.Int64("last", -1);
        CHECK(!target.Load(SaveReader(junk.Block("bag"))) && target.Remaining() == remaining);
        jw.String("order", "99999999999");
        CHECK(!target.Load(SaveReader(junk.Block("bag"))));
        jw.String("order", "");                                                            // an empty round is a valid state
        CHECK(target.Load(SaveReader(junk.Block("bag"))) && target.Remaining() == 6);      // (a fresh round: Remaining reports the full bag)
        ShuffleBag<int> empty;
        CHECK(empty.RestoreState(ShuffleBag<int>::State()));
    }

    // ---- WeightedTable saved mid-way ------------------------------------------------------------------------------------------------------------------
    {
        auto build = [] {
            WeightedTable<std::string> t;
            t.Add("potion", 8); t.Add("sword", 1); t.Add("shield", 2); t.Add("gem", 0.5f); t.Add("junk", 0);
            return t;
        };
        WeightedTable<std::string> live = build();
        Random rng(31);
        live.DrawWithout(rng); live.DrawWithout(rng);
        const std::string mask = live.DrawnMask();
        CHECK(mask.size() == 5 && std::count(mask.begin(), mask.end(), '1') == 2);
        const Random::State rs = rng.GetState();
        const std::vector<int> expect = live.DrawDistinct(rng, 5);

        WeightedTable<std::string> back = build();
        CHECK(back.RestoreDrawn(mask) && back.DrawnMask() == mask);
        Random rng2(1);
        rng2.SetState(rs);
        CHECK(back.DrawDistinct(rng2, 5) == expect);

        SaveDocument doc;
        WeightedTable<std::string> live2 = build();
        Random rng3(4);
        live2.DrawWithout(rng3);
        live2.Save(SaveWriter(doc.Block("t")));
        WeightedTable<std::string> loaded = build();
        CHECK(loaded.Load(SaveReader(doc.Block("t"))) && loaded.DrawnMask() == live2.DrawnMask());
        CHECK(loaded.Drawn(loaded.DrawnMask().find('1') == std::string::npos ? -1 : (int)loaded.DrawnMask().find('1')));
        CHECK(!loaded.Drawn(-1) && !loaded.Drawn(99));

        WeightedTable<std::string> target = build();
        CHECK(!target.RestoreDrawn("101") && !target.RestoreDrawn("1010x") && !target.RestoreDrawn("") && target.DrawnMask() == "00000");
        WeightedTable<std::string> small;
        small.Add("x", 1);
        CHECK(!small.Load(SaveReader(doc.Block("t"))) && !small.Load(SaveReader(doc.Block("none"))));
        WeightedTable<int> none;
        CHECK(none.RestoreDrawn("") && none.DrawnMask().empty());
    }

    TEST_MAIN_RESULT();
}
