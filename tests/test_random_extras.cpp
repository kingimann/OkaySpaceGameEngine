// 1.1 Random extras: Chance(percent), Fork, string seeds, ThreadLocal, points and directions,
// ShuffleBag, WeightedTable (with and without replacement) and the dice parser (never throws).
// Fixed seeds throughout, so every statistic below is the same on every run.
#include "test_framework.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Core/RandomExtras.hpp"
#include "okay/Core/Rng.hpp"
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

namespace {
bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
}

int main() {
    RUN_SUITE("random_extras");

    // ---- Chance(percent) ---------------------------------------------------------------------------------
    {
        Random r(5);
        int never = 0, always = 0, over = 0, neg = 0, nan = 0, quarter = 0;
        const float nanf = std::numeric_limits<float>::quiet_NaN();
        for (int i = 0; i < 20000; ++i) {
            never += r.Chance(0.0f); always += r.Chance(100.0f); over += r.Chance(250.0f);
            neg += r.Chance(-5.0f); nan += r.Chance(nanf); quarter += r.Chance(25.0f);
        }
        CHECK(never == 0 && neg == 0 && nan == 0);
        CHECK(always == 20000 && over == 20000);
        CHECK(Near(quarter / 20000.0, 0.25, 0.015));
        Rng pcg(9);
        int hits = 0;
        for (int i = 0; i < 20000; ++i) hits += rnd::Chance(pcg, 10.0f);   // the helper works on both generators
        CHECK(Near(hits / 20000.0, 0.10, 0.012));
    }

    // ---- seeding from text: the same on every platform ----------------------------------------------------------
    {
        CHECK(Random::HashSeed("") == 0xc3817c016ba4ff30ull);             // golden values: FNV-1a 64 over the bytes, then mixed
        CHECK(Random::HashSeed("okay") == 0xce7ad08df62e8992ull);
        CHECK(Random::HashSeed("world-42") == 0x8de7d5cd6f963bdeull);
        CHECK(Random::HashSeed("Okay") == 0x02680d62578f80d8ull);         // case matters
        CHECK(Random::HashSeed("caf\xC3\xA9") == 0x78db240002221039ull);  // bytes, not locale
        Random a = Random::FromString("world-42");
        CHECK(a.NextUInt64() == 14366845453575911669ull);                 // checked against an independent xorshift128+ / SplitMix64 implementation
        CHECK(a.NextUInt64() == 5905909477918327020ull);
        Random b(1); b.SeedFromString("world-42");
        Random c = Random::FromString("world-42");
        for (int i = 0; i < 100; ++i) CHECK(b.NextUInt64() == c.NextUInt64());    // seeding an existing generator = a fresh one
        Random d = Random::FromString("world-43");
        Random e = Random::FromString("world-42");
        CHECK(d.NextUInt64() != e.NextUInt64());
        std::set<std::uint64_t> seeds;
        for (int i = 0; i < 5000; ++i) seeds.insert(Random::HashSeed("seed" + std::to_string(i)));
        CHECK(seeds.size() == 5000);                                      // no collisions among near-identical names
        CHECK(Random::HashSeed(std::string("a\0b", 3)) != Random::HashSeed("a"));   // embedded NUL counts
    }

    // ---- Fork: an independent child stream --------------------------------------------------------------------------
    {
        Random parent(1234);
        Random child = parent.Fork();
        std::set<std::uint64_t> seen;
        for (int i = 0; i < 2000; ++i) seen.insert(child.NextUInt64());
        int shared = 0;
        for (int i = 0; i < 2000; ++i) if (seen.count(parent.NextUInt64())) ++shared;
        CHECK(shared == 0);                                               // the child does not replay the parent
        // deterministic: the same parent state gives the same child
        Random p1(99), p2(99);
        Random c1 = p1.Fork(), c2 = p2.Fork();
        for (int i = 0; i < 50; ++i) CHECK(c1.NextUInt64() == c2.NextUInt64());
        // forking twice gives two different children; each fork moves the parent on by one draw
        Random p3(99);
        Random k1 = p3.Fork(), k2 = p3.Fork();
        CHECK(k1.NextUInt64() != k2.NextUInt64());
        Random ref(99); ref.NextUInt64(); ref.NextUInt64();
        CHECK(p3.NextUInt64() == ref.NextUInt64());
        // the child's values look uniform (mean of 20000 near 0.5)
        Random q(7); Random qc = q.Fork();
        double sum = 0; for (int i = 0; i < 20000; ++i) sum += qc.Value();
        CHECK(Near(sum / 20000.0, 0.5, 0.01));
        // correlation between parent and child draws is nil
        Random pa(31), ca = pa.Fork();
        double sxy = 0, sx = 0, sy = 0, sxx = 0, syy = 0; const int n = 20000;
        for (int i = 0; i < n; ++i) { double x = pa.Value(), y = ca.Value(); sx += x; sy += y; sxy += x * y; sxx += x * x; syy += y * y; }
        const double corr = (sxy / n - (sx / n) * (sy / n)) / std::sqrt((sxx / n - (sx / n) * (sx / n)) * (syy / n - (sy / n) * (sy / n)));
        CHECK(std::fabs(corr) < 0.03);
    }

    // ---- the shared generator is not thread safe; ThreadLocal gives each thread its own -----------------------------------
    {
        Random* mainGen = &Random::ThreadLocal();
        CHECK(mainGen == &Random::ThreadLocal());                         // stable per thread
        CHECK(mainGen != &Random::Shared());
        Random* other = nullptr; std::uint64_t otherFirst = 0;
        std::thread t([&] { other = &Random::ThreadLocal(); otherFirst = other->NextUInt64(); });
        t.join();
        CHECK(other != nullptr && other != mainGen);
        CHECK(otherFirst != 0);
        // threads that draw at once each get a stream (no crash, all different first values)
        std::vector<std::uint64_t> firsts(8, 0);
        std::vector<std::thread> ts;
        for (int i = 0; i < 8; ++i) ts.emplace_back([&, i] { for (int k = 0; k < 1000; ++k) firsts[(std::size_t)i] = Random::ThreadLocal().NextUInt64(); });
        for (auto& th : ts) th.join();
        std::set<std::uint64_t> distinct(firsts.begin(), firsts.end());
        CHECK(distinct.size() == 8);
    }

    // ---- Random::LockShared: threads can share the seeded stream without a data race -------------------------------------------
    {
        constexpr int kThreads = 6, kDraws = 4000;
        // The reference: what kThreads * kDraws draws add up to when one thread makes them all.
        Random::Shared().Seed(777);
        std::uint64_t expected = 0;
        for (int i = 0; i < kThreads * kDraws; ++i) expected += Random::Shared().NextUInt64() >> 8;
        const Random::State after = Random::Shared().GetState();

        Random::Shared().Seed(777);
        std::vector<std::uint64_t> sums((std::size_t)kThreads, 0);
        std::vector<std::thread> ts;
        for (int t = 0; t < kThreads; ++t)
            ts.emplace_back([&, t] {
                for (int k = 0; k < kDraws; ++k) {
                    auto rng = Random::LockShared();
                    sums[(std::size_t)t] += rng->NextUInt64() >> 8;
                }
            });
        for (auto& th : ts) th.join();
        std::uint64_t total = 0;
        for (std::uint64_t s : sums) total += s;
        CHECK(total == expected);                                         // every draw of the one stream was handed out exactly once
        const Random::State now = Random::Shared().GetState();
        CHECK(now.s0 == after.s0 && now.s1 == after.s1);                  // and the stream ends where the sequential one does
        {   // it can be taken again after a scope ends, on the same thread, and gives the same generator
            auto a = Random::LockShared();
            CHECK(&*a == &Random::Shared());
        }
        { auto b = Random::LockShared(); CHECK(b->Range(5, 6) == 5); }
        Random::Shared().Seed(0xC0FFEEull);
    }

    // ---- third pass: cones (weapon spread), triangles, signs ---------------------------------------------------------------------
    {
        Random r(31337);
        const int N = 30000;
        const float half = 0.5f;                                      // about 28.6 degrees
        const float cosMax = std::cos(half);
        const Vec3 axes[] = {Vec3{0, 0, 1}, Vec3{0, 0, -1}, Vec3{1, 0, 0}, Vec3{0, 3, 0}, Vec3{1, 1, 1}, Vec3{0.001f, 0.0f, 0.9999f}};
        for (const Vec3& axis : axes) {
            const Vec3 a = axis.Normalized();
            bool inside = true, unit = true, reachedEdge = false;
            double meanCos = 0.0;
            for (int i = 0; i < N; ++i) {
                const Vec3 d = rnd::DirectionInCone(r, axis, half);
                const float c = Vec3::Dot(d, a);
                if (std::fabs(d.Magnitude() - 1.0f) > 1e-4f) unit = false;
                if (c < cosMax - 1e-4f) inside = false;
                if (c < cosMax + 0.002f) reachedEdge = true;
                meanCos += c;
            }
            CHECK(inside && unit && reachedEdge);
            CHECK(Near(meanCos / N, (1.0 + cosMax) / 2.0, 0.003));   // uniform in cos(theta): even by solid angle
        }
        // no spread is the axis itself; a full sphere has no preferred direction; bad numbers do not poison it
        for (int i = 0; i < 100; ++i) { const Vec3 d = rnd::DirectionInCone(r, Vec3{0, 2, 0}, 0.0f); CHECK(Near(d.y, 1.0, 1e-5) && std::fabs(d.x) < 1e-4f); }
        double zsum = 0; for (int i = 0; i < N; ++i) zsum += rnd::DirectionInCone(r, Vec3{0, 0, 1}, 3.14159265f).z;
        CHECK(Near(zsum / N, 0.0, 0.02));
        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        bool finite = true, plusZ = true;
        for (int i = 0; i < 200; ++i) {
            const Vec3 z0 = rnd::DirectionInCone(r, Vec3{0, 0, 0}, 0.2f);        // no axis: +Z
            const Vec3 z1 = rnd::DirectionInCone(r, Vec3{nan, 0, 1}, 0.2f);
            const Vec3 z2 = rnd::DirectionInCone(r, Vec3{inf, inf, inf}, 0.2f);
            const Vec3 z3 = rnd::DirectionInCone(r, Vec3{0, 0, 1}, nan);         // NaN angle: no spread
            const Vec3 z4 = rnd::DirectionInCone(r, Vec3{0, 0, 1}, -0.2f);       // negative: its size
            const Vec3 z5 = rnd::DirectionInCone(r, Vec3{0, 0, 1}, inf);         // infinity: no spread (not a number)
            for (const Vec3& d : {z0, z1, z2, z3, z4, z5}) if (!std::isfinite(d.x + d.y + d.z)) finite = false;
            if (z0.z < std::cos(0.2f) - 1e-4f || z1.z < std::cos(0.2f) - 1e-4f || z2.z < std::cos(0.2f) - 1e-4f || z4.z < std::cos(0.2f) - 1e-4f) plusZ = false;
            if (z3.z < 0.9999f || z5.z < 0.9999f) plusZ = false;
        }
        CHECK(finite && plusZ);
        // the same seed gives the same spray
        Random s1(5), s2(5);
        CHECK(rnd::DirectionInCone(s1, Vec3{1, 2, 3}, 0.3f).x == rnd::DirectionInCone(s2, Vec3{1, 2, 3}, 0.3f).x);

        // 2D: angle uniform in [-half, half] about the axis
        bool in2 = true; double meanAngle = 0, minA = 9, maxA = -9;
        for (int i = 0; i < N; ++i) {
            const Vec2 d = rnd::DirectionInCone2D(r, Vec2{0, 5}, half);          // axis +Y
            const double ang = std::atan2((double)d.x, (double)d.y);            // angle from +Y
            if (std::fabs(d.Magnitude() - 1.0f) > 1e-4f || std::fabs(ang) > half + 1e-4) in2 = false;
            meanAngle += ang; minA = std::min(minA, ang); maxA = std::max(maxA, ang);
        }
        CHECK(in2 && Near(meanAngle / N, 0.0, 0.01) && minA < -half + 0.01 && maxA > half - 0.01);
        const Vec2 z = rnd::DirectionInCone2D(r, Vec2{0, 0}, 0.0f);
        CHECK(Near(z.x, 1.0, 1e-6) && Near(z.y, 0.0, 1e-6));                      // no axis: +X
        const Vec2 nz = rnd::DirectionInCone2D(r, Vec2{nan, 1}, nan);
        CHECK(Near(nz.x, 1.0, 1e-6) && Near(nz.y, 0.0, 1e-6));

        // triangles: always inside, centroid in the middle, even by area (the part nearest corner A is the 2 x 1.5 box: half of the 6)
        const Vec2 A{0, 0}, B{4, 0}, C{0, 3};
        bool inTri = true; double mx = 0, my = 0; int nearA = 0;
        for (int i = 0; i < N; ++i) {
            const Vec2 p = rnd::PointInTriangle(r, A, B, C);
            if (p.x < -1e-5f || p.y < -1e-5f || p.x / 4.0f + p.y / 3.0f > 1.0f + 1e-5f) inTri = false;
            mx += p.x; my += p.y;
            if ((p - A).Magnitude() < (p - B).Magnitude() && (p - A).Magnitude() < (p - C).Magnitude()) ++nearA;
        }
        CHECK(inTri && Near(mx / N, 4.0 / 3.0, 0.03) && Near(my / N, 1.0, 0.03));
        CHECK(Near(nearA / (double)N, 0.5, 0.012));
        const Vec3 p3 = rnd::PointInTriangle(r, Vec3{1, 1, 1}, Vec3{1, 1, 1}, Vec3{1, 1, 1});   // degenerate: the point
        CHECK(Near(p3.x, 1.0, 1e-5) && Near(p3.y, 1.0, 1e-5) && Near(p3.z, 1.0, 1e-5));
        bool onSeg = true;
        for (int i = 0; i < 200; ++i) { const Vec3 p = rnd::PointInTriangle(r, Vec3{0, 0, 0}, Vec3{2, 0, 0}, Vec3{1, 0, 0}); if (p.x < -1e-5f || p.x > 2.00001f || p.y != 0.0f || p.z != 0.0f) onSeg = false; }
        CHECK(onSeg);

        int plus = 0;
        for (int i = 0; i < N; ++i) { const int sg = rnd::Sign(r); CHECK(sg == 1 || sg == -1); if (sg == 1) ++plus; }
        CHECK(Near(plus / (double)N, 0.5, 0.012));
    }

    // ---- points and directions -------------------------------------------------------------------------------------------------
    {
        Random r(2024);
        const int N = 40000;
        // Direction2D: unit, quadrants even
        int q[4] = {0, 0, 0, 0}; bool unit = true;
        for (int i = 0; i < N; ++i) {
            Vec2 d = rnd::Direction2D(r);
            if (std::fabs(d.Magnitude() - 1.0f) > 1e-4f) unit = false;
            ++q[(d.x >= 0 ? 0 : 1) + (d.y >= 0 ? 0 : 2)];
        }
        CHECK(unit);
        for (int k = 0; k < 4; ++k) CHECK(Near(q[k] / (double)N, 0.25, 0.012));
        // Direction3D: unit, octants even, z uniform (mean 0)
        int o[8] = {}; double zs = 0; unit = true;
        for (int i = 0; i < N * 2; ++i) {
            Vec3 d = rnd::Direction3D(r);
            if (std::fabs(d.Magnitude() - 1.0f) > 1e-4f) unit = false;
            ++o[(d.x >= 0 ? 1 : 0) + (d.y >= 0 ? 2 : 0) + (d.z >= 0 ? 4 : 0)];
            zs += d.z;
        }
        CHECK(unit);
        for (int k = 0; k < 8; ++k) CHECK(Near(o[k] / (double)(N * 2), 0.125, 0.008));
        CHECK(Near(zs / (N * 2), 0.0, 0.01));
        // PointOnCircle / OnSphere: on the rim, around the centre; negative radius counts as positive
        bool onRim = true;
        for (int i = 0; i < 2000; ++i) {
            Vec2 p = rnd::PointOnCircle(r, 3.0f, Vec2{10, -5});
            if (std::fabs((p - Vec2{10, -5}).Magnitude() - 3.0f) > 1e-3f) onRim = false;
            Vec3 s = rnd::PointOnSphere(r, -2.0f, Vec3{1, 2, 3});
            if (std::fabs((s - Vec3{1, 2, 3}).Magnitude() - 2.0f) > 1e-3f) onRim = false;
        }
        CHECK(onRim);
        // PointInCircle: inside, and even by AREA (a quarter of the points within half the radius)
        int inner = 0; bool inside = true;
        for (int i = 0; i < N; ++i) {
            Vec2 p = rnd::PointInCircle(r, 4.0f, Vec2{1, 1});
            const float d = (p - Vec2{1, 1}).Magnitude();
            if (d > 4.0f + 1e-4f) inside = false;
            if (d < 2.0f) ++inner;
        }
        CHECK(inside);
        CHECK(Near(inner / (double)N, 0.25, 0.012));
        // PointInSphere: eighth of the points within half the radius (even by volume)
        inner = 0; inside = true;
        for (int i = 0; i < N; ++i) {
            Vec3 p = rnd::PointInSphere(r, 2.0f);
            const float d = p.Magnitude();
            if (d > 2.0f + 1e-4f) inside = false;
            if (d < 1.0f) ++inner;
        }
        CHECK(inside);
        CHECK(Near(inner / (double)N, 0.125, 0.008));
        // PointInRing: between the radii (given in either order), even by area
        int nearHalf = 0; bool ring = true;
        for (int i = 0; i < N; ++i) {
            Vec2 p = rnd::PointInRing(r, 5.0f, 3.0f);                      // swapped on purpose
            const float d = p.Magnitude();
            if (d < 3.0f - 1e-4f || d > 5.0f + 1e-4f) ring = false;
            if (d * d < 17.0f) ++nearHalf;                                  // area below the middle (r^2 = (9+25)/2)
        }
        CHECK(ring);
        CHECK(Near(nearHalf / (double)N, 0.5, 0.015));
        // boxes and rectangles: in bounds, corners in either order, a flat box is a point
        bool inBox = true; double mx = 0, my = 0, mz = 0;
        for (int i = 0; i < N; ++i) {
            Vec3 p = rnd::PointInBox(r, Vec3{5, 1, -2}, Vec3{-5, 3, 2});   // swapped x and z corners
            if (p.x < -5 || p.x > 5 || p.y < 1 || p.y > 3 || p.z < -2 || p.z > 2) inBox = false;
            mx += p.x; my += p.y; mz += p.z;
            Vec2 f = rnd::PointInRect(r, Vec2{2, 2}, Vec2{-2, -4});
            if (f.x < -2 || f.x > 2 || f.y < -4 || f.y > 2) inBox = false;
        }
        CHECK(inBox);
        CHECK(Near(mx / N, 0.0, 0.1) && Near(my / N, 2.0, 0.03) && Near(mz / N, 0.0, 0.05));
        Vec3 flat = rnd::PointInBox(r, Vec3{1, 2, 3}, Vec3{1, 2, 3});
        CHECK(flat.x == 1.0f && flat.y == 2.0f && flat.z == 3.0f);
        // zero radius is the centre
        Vec2 z = rnd::PointInCircle(r, 0.0f, Vec2{7, 8});
        CHECK(z.x == 7.0f && z.y == 8.0f);
        // the PCG generator works too
        Rng pcg(3);
        Vec2 p = rnd::PointInCircle(pcg, 1.0f);
        CHECK(p.Magnitude() <= 1.0001f);
        CHECK(Near(rnd::Direction3D(pcg).Magnitude(), 1.0, 1e-4));
    }

    // ---- ShuffleBag ---------------------------------------------------------------------------------------------------------------------
    {
        Random r(11);
        ShuffleBag<int> bag;
        CHECK(bag.Empty() && bag.Size() == 0);
        int out = -1;
        CHECK(!bag.TryDraw(r, out) && out == -1);                        // nothing to draw: `out` untouched
        CHECK(bag.Draw(r) == 0);                                         // T() when empty
        bag.Add(1); bag.Add(2); bag.Add(3);
        bag.Add(9, 0); bag.Add(9, -4);                                   // no copies: ignored
        CHECK(bag.Size() == 3 && bag.Entries() == 3 && bag.Remaining() == 3);
        // every round is a permutation, and the same item never comes twice in a row, even across rounds
        std::vector<int> seq;
        for (int i = 0; i < 3 * 400; ++i) seq.push_back(bag.Draw(r));
        bool perms = true, noRepeat = true;
        for (std::size_t k = 0; k < seq.size(); k += 3) {
            std::set<int> s{seq[k], seq[k + 1], seq[k + 2]};
            if (s.size() != 3) perms = false;
        }
        for (std::size_t k = 1; k < seq.size(); ++k) if (seq[k] == seq[k - 1]) noRepeat = false;
        CHECK(perms && noRepeat);
        CHECK(bag.Rounds() == 400);
        // which item opens a round is fair
        std::map<int, int> firsts;
        for (std::size_t k = 0; k < seq.size(); k += 3) ++firsts[seq[k]];
        CHECK(firsts[1] > 90 && firsts[2] > 90 && firsts[3] > 90);
        // remaining counts down within a round
        ShuffleBag<int> b2; b2.Add(1); b2.Add(2); b2.Add(3); b2.Add(4);
        b2.Draw(r); CHECK(b2.Remaining() == 3);
        b2.Draw(r); b2.Draw(r); CHECK(b2.Remaining() == 1);
        b2.Draw(r); CHECK(b2.Remaining() == 4);                          // a full bag again
        // Reset starts a fresh round, forgetting the last item
        b2.Draw(r); b2.Reset(); CHECK(b2.Remaining() == 4);
        // adding mid-round puts the new item into the next shuffle
        ShuffleBag<std::string> sb; sb.Add("a"); sb.Add("b");
        sb.Draw(r); sb.Add("c");
        std::set<std::string> got; for (int i = 0; i < 3; ++i) got.insert(sb.Draw(r));
        CHECK(got.size() == 3);
        // two items strictly alternate
        ShuffleBag<char> two; two.Add('a'); two.Add('b');
        std::string alt; for (int i = 0; i < 400; ++i) alt += two.Draw(r);
        bool alternates = true; for (std::size_t k = 1; k < alt.size(); ++k) if (alt[k] == alt[k - 1]) alternates = false;
        CHECK(alternates);
        // copies: 3 x common, 1 x rare in every round of four
        ShuffleBag<std::string> loot; loot.Add("common", 3); loot.Add("rare", 1);
        CHECK(loot.Size() == 4 && loot.Entries() == 2);
        bool rounds = true, rareTwice = false; int rareFirst = 0; std::string prev;
        for (int k = 0; k < 300; ++k) {
            int commons = 0, rares = 0;
            for (int i = 0; i < 4; ++i) {
                std::string s = loot.Draw(r);
                if (s == "common") ++commons; else ++rares;
                if (i == 0 && s == "rare") ++rareFirst;
                if (s == "rare" && prev == "rare") rareTwice = true;      // a single-copy entry never repeats back to back
                prev = s;
            }
            if (commons != 3 || rares != 1) rounds = false;
        }
        CHECK(rounds);
        CHECK(!rareTwice);
        CHECK(rareFirst > 30 && rareFirst < 100);                        // the rare still opens a fair share of rounds (not pinned to the front)
        // a single entry repeats (there is no choice), and still counts rounds
        ShuffleBag<int> solo; solo.Add(7, 3);
        for (int i = 0; i < 9; ++i) CHECK(solo.Draw(r) == 7);
        CHECK(solo.Rounds() == 3);
        // Clear empties it
        solo.Clear(); CHECK(solo.Empty() && solo.Rounds() == 0);
        // deterministic with a seed, and works with the PCG generator
        Random ra(5), rb(5);
        ShuffleBag<int> x, y; for (int i = 0; i < 6; ++i) { x.Add(i); y.Add(i); }
        for (int i = 0; i < 60; ++i) CHECK(x.Draw(ra) == y.Draw(rb));
        Rng pcg(5); ShuffleBag<int> z; for (int i = 0; i < 6; ++i) z.Add(i);
        std::set<int> perm; for (int i = 0; i < 6; ++i) perm.insert(z.Draw(pcg));
        CHECK(perm.size() == 6);
    }

    // ---- WeightedTable ----------------------------------------------------------------------------------------------------------------------
    {
        Random r(321);
        WeightedTable<std::string> t;
        CHECK(t.DrawIndex(r) == -1 && t.Draw(r) == nullptr);              // empty
        t.Add("a", 1.0f); t.Add("b", 2.0f); t.Add("c", 7.0f);
        CHECK(t.Size() == 3 && Near(t.TotalWeight(), 10.0, 1e-9));
        int hits[3] = {0, 0, 0};
        const int N = 100000;
        for (int i = 0; i < N; ++i) { const std::string* s = t.Draw(r); ++hits[(*s)[0] - 'a']; }
        CHECK(Near(hits[0] / (double)N, 0.1, 0.006));
        CHECK(Near(hits[1] / (double)N, 0.2, 0.006));
        CHECK(Near(hits[2] / (double)N, 0.7, 0.008));
        // weights that cannot be drawn: zero, negative, NaN, infinite
        WeightedTable<int> bad;
        bad.Add(0, 0.0f); bad.Add(1, -3.0f); bad.Add(2, std::numeric_limits<float>::quiet_NaN());
        bad.Add(3, std::numeric_limits<float>::infinity()); bad.Add(4, 5.0f);
        for (int i = 0; i < 2000; ++i) CHECK(bad.DrawIndex(r) == 4);
        CHECK(bad.Weight(1) == 0.0f && bad.Weight(3) == 0.0f);
        WeightedTable<int> none; none.Add(1, 0.0f); none.Add(2, -1.0f);
        CHECK(none.DrawIndex(r) == -1 && none.Draw(r) == nullptr && none.DrawIndexWithout(r) == -1 && none.Remaining() == 0);
        // SetWeight changes the odds
        bad.SetWeight(0, 5.0f);
        int zeroHits = 0; for (int i = 0; i < 4000; ++i) zeroHits += bad.DrawIndex(r) == 0;
        CHECK(Near(zeroHits / 4000.0, 0.5, 0.05));
        bad.SetWeight(99, 1.0f); bad.SetWeight(-1, 1.0f);                 // out of range: ignored
        // a tiny weight next to a huge one is still reachable and never out of range
        WeightedTable<int> wide; wide.Add(0, 1e-3f); wide.Add(1, 1e6f);
        for (int i = 0; i < 5000; ++i) { int k = wide.DrawIndex(r); CHECK(k == 0 || k == 1); }
        // without replacement: each entry once, then dry; Refill brings them back
        WeightedTable<int> d;
        for (int i = 0; i < 6; ++i) d.Add(i, (float)(i + 1));
        d.Add(99, 0.0f);                                                  // never drawable
        CHECK(d.Remaining() == 6);
        std::set<int> got;
        for (int i = 0; i < 6; ++i) { int k = d.DrawIndexWithout(r); CHECK(k >= 0 && k < 6); got.insert(k); }
        CHECK(got.size() == 6 && d.Remaining() == 0 && Near(d.TotalWeight(), 0.0, 1e-9));
        CHECK(d.DrawIndexWithout(r) == -1 && d.DrawWithout(r) == nullptr);
        CHECK(d.DrawIndex(r) >= 0);                                       // with replacement ignores "used"
        d.Refill();
        CHECK(d.Remaining() == 6 && Near(d.TotalWeight(), 21.0, 1e-9));
        // DrawDistinct: n different entries, fewer when the table runs out
        std::vector<int> three = d.DrawDistinct(r, 3);
        CHECK(three.size() == 3 && std::set<int>(three.begin(), three.end()).size() == 3);
        std::vector<int> rest = d.DrawDistinct(r, 10);
        CHECK(rest.size() == 3);
        CHECK(d.DrawDistinct(r, 2).empty());
        CHECK(d.DrawDistinct(r, 0).empty() && d.DrawDistinct(r, -4).empty());
        // the first pick without replacement follows the weights too
        int first[6] = {};
        const int M = 60000;
        for (int i = 0; i < M; ++i) { d.Refill(); ++first[d.DrawIndexWithout(r)]; }
        for (int k = 0; k < 6; ++k) CHECK(Near(first[k] / (double)M, (k + 1) / 21.0, 0.008));
        // heavier entries tend to come earlier in a full draw
        double avgPos[6] = {};
        for (int i = 0; i < 20000; ++i) { d.Refill(); for (int p = 0; p < 6; ++p) avgPos[d.DrawIndexWithout(r)] += p; }
        CHECK(avgPos[5] < avgPos[0]);
        // deterministic with the PCG generator as well
        Rng a(8), b(8);
        for (int i = 0; i < 200; ++i) CHECK(t.DrawIndex(a) == t.DrawIndex(b));
    }

    // ---- dice: valid notation ---------------------------------------------------------------------------------------------------------------
    {
        struct Case { const char* text; int min, max; double avg; const char* norm; };
        const Case cases[] = {
            {"2d6+3", 5, 15, 10.0, "2d6+3"},
            {"d20", 1, 20, 10.5, "1d20"},
            {"D20", 1, 20, 10.5, "1d20"},
            {"1d20-2", -1, 18, 8.5, "1d20-2"},
            {"3d8", 3, 24, 13.5, "3d8"},
            {"2D6 + 3", 5, 15, 10.0, "2d6+3"},
            {"  2d6  ", 2, 12, 7.0, "2d6"},
            {"10", 10, 10, 10.0, "10"},
            {"-3", -3, -3, -3.0, "-3"},
            {"+5", 5, 5, 5.0, "5"},
            {"-1d6", -6, -1, -3.5, "-1d6"},
            {"1d6+1d4+2", 4, 12, 8.0, "1d6+1d4+2"},
            {"1d6-1d4", -3, 5, 1.0, "1d6-1d4"},
            {"d%", 1, 100, 50.5, "1d100"},
            {"4d6kh3", 3, 18, 12.2446, "4d6kh3"},
            {"4d6k3", 3, 18, 12.2446, "4d6kh3"},
            {"2d20kl1", 1, 20, 7.175, "2d20kl1"},
            {"2d20kh1", 1, 20, 13.825, "2d20kh1"},
            {"3d6kh3", 3, 18, 10.5, "3d6"},                               // keeping all of them is the plain roll
            {"1000d100000", 1000, 100000000, 50000500.0, "1000d100000"},
        };
        for (const Case& c : cases) {
            DiceParse p = ParseDice(c.text);
            CHECK(p.ok && p.error == DiceError::None && p.message.empty() && p.expr.Valid());
            if (!p.ok) { std::fprintf(stderr, "  case '%s': %s\n", c.text, p.message.c_str()); continue; }
            CHECK(p.expr.Min() == c.min);
            CHECK(p.expr.Max() == c.max);
            CHECK(Near(p.expr.Average(), c.avg, 1e-3));
            CHECK(p.expr.ToString() == c.norm);
        }
        // sixteen terms is the limit
        std::string sixteen = "1d6"; for (int i = 1; i < 16; ++i) sixteen += "+1d6";
        CHECK(ParseDice(sixteen).ok);
        // the worst case total still fits an int
        std::string worst = "1000d100000"; for (int i = 1; i < 16; ++i) worst += "+1000d100000";
        DiceParse w = ParseDice(worst);
        CHECK(w.ok && w.expr.Max() == 1600000000);
        Random r(1);
        const int v = w.expr.Roll(r);
        CHECK(v >= w.expr.Min() && v <= w.expr.Max());
    }

    // ---- dice: errors are reported, never thrown ----------------------------------------------------------------------------------------------
    {
        struct Bad { std::string text; DiceError err; };
        std::string tooLong(300, '1');
        std::string seventeen = "1d6"; for (int i = 1; i < 17; ++i) seventeen += "+1d6";
        const Bad bad[] = {
            {"", DiceError::Empty}, {"   ", DiceError::Empty}, {"\t\n", DiceError::Empty},
            {"d", DiceError::ExpectedSides}, {"2d", DiceError::ExpectedSides}, {"2d+3", DiceError::ExpectedSides},
            {"2d0", DiceError::ZeroSides}, {"0d6", DiceError::ZeroDice},
            {"1001d6", DiceError::TooManyDice}, {"1d100001", DiceError::TooManySides},
            {"2d6+", DiceError::ExpectedNumber}, {"+", DiceError::ExpectedNumber}, {"-", DiceError::ExpectedNumber},
            {"2d6++3", DiceError::UnexpectedCharacter}, {"2d6x", DiceError::UnexpectedCharacter},
            {"abc", DiceError::UnexpectedCharacter}, {"2 d6", DiceError::UnexpectedCharacter}, {"d6d6", DiceError::UnexpectedCharacter},
            {"2d6 3", DiceError::UnexpectedCharacter}, {"2.5d6", DiceError::UnexpectedCharacter}, {"2d6*2", DiceError::UnexpectedCharacter},
            {"2d6kh", DiceError::BadKeep}, {"2d6k", DiceError::BadKeep}, {"2d6kh0", DiceError::BadKeep}, {"2d6kh3", DiceError::BadKeep},
            {"2d6kx2", DiceError::BadKeep},
            {"1234567890d6", DiceError::NumberTooLarge}, {"2d6+9999999", DiceError::NumberTooLarge}, {"1d99999999999", DiceError::NumberTooLarge},
            {tooLong, DiceError::TooLong}, {seventeen, DiceError::TooManyTerms},
        };
        bool never_threw = true;
        for (const Bad& b : bad) {
            try {
                DiceParse p = ParseDice(b.text);
                CHECK(!p.ok);
                CHECK(p.error == b.err);
                if (p.error != b.err) std::fprintf(stderr, "  text '%.40s': got error %d, wanted %d (%s)\n", b.text.c_str(), (int)p.error, (int)b.err, p.message.c_str());
                CHECK(!p.message.empty() && !p.expr.Valid());
                CHECK(p.position >= 0 && (std::size_t)p.position <= b.text.size());
            } catch (...) { never_threw = false; }
        }
        CHECK(never_threw);
        // positions point at the problem
        CHECK(ParseDice("2d6+").position == 4);
        CHECK(ParseDice("2d6x").position == 3);
        CHECK(ParseDice("2d6+abc").position == 4);
        CHECK(ParseDice("2d6+abc").message.find("position 4") != std::string::npos);
        // an invalid expression rolls 0
        Random r(1);
        CHECK(ParseDice("nope").expr.Roll(r) == 0);
        // RollDice: parse + roll with a fallback for a typo
        DiceParse why;
        CHECK(RollDice(r, "2d6+3x", 42, &why) == 42);
        CHECK(!why.ok && why.error == DiceError::UnexpectedCharacter);
        const int ok = RollDice(r, "1d6", 42, &why);
        CHECK(why.ok && ok >= 1 && ok <= 6);
        CHECK(RollDice(r, "bad") == 0);
    }

    // ---- dice: rolling follows the right distribution -------------------------------------------------------------------------------------------------------
    {
        Random r(2718);
        const int N = 200000;
        DiceParse p = ParseDice("2d6+3");
        std::map<int, int> hist; double sum = 0; bool inRange = true;
        for (int i = 0; i < N; ++i) { int v = p.expr.Roll(r); ++hist[v]; sum += v; if (v < 5 || v > 15) inRange = false; }
        CHECK(inRange);
        CHECK(Near(sum / N, 10.0, 0.03));
        CHECK(Near(hist[10] / (double)N, 6.0 / 36.0, 0.005));             // 7 + 3
        CHECK(Near(hist[5] / (double)N, 1.0 / 36.0, 0.003));
        CHECK(Near(hist[15] / (double)N, 1.0 / 36.0, 0.003));
        // keep highest 3 of 4d6: mean 12.2446
        DiceParse kh = ParseDice("4d6kh3");
        sum = 0; for (int i = 0; i < N; ++i) sum += kh.expr.Roll(r);
        CHECK(Near(sum / N, 12.2446, 0.04));
        // advantage / disadvantage on a d20
        DiceParse adv = ParseDice("2d20kh1"), dis = ParseDice("2d20kl1");
        double sa = 0, sd = 0; for (int i = 0; i < N; ++i) { sa += adv.expr.Roll(r); sd += dis.expr.Roll(r); }
        CHECK(Near(sa / N, 13.825, 0.06) && Near(sd / N, 7.175, 0.06));
        // subtraction and constants
        DiceParse sub = ParseDice("1d6-1d4+10");
        sum = 0; for (int i = 0; i < N; ++i) sum += sub.expr.Roll(r);
        CHECK(Near(sum / N, 3.5 - 2.5 + 10.0, 0.03));
        // the PCG generator rolls too
        Rng pcg(4);
        sum = 0; for (int i = 0; i < 50000; ++i) sum += p.expr.Roll(pcg);
        CHECK(Near(sum / 50000, 10.0, 0.06));
        // d1 is always 1
        DiceParse d1 = ParseDice("5d1");
        for (int i = 0; i < 50; ++i) CHECK(d1.expr.Roll(r) == 5);
    }

    // ---- dice: fuzz. Garbage in, never a crash, never an exception, consistent answers ----------------------------------------------------------------------
    {
        Random r(99);
        const std::string alphabet = "0123456789dDkKhHlL+-% \tx.*";
        int parsedOk = 0, parsedBad = 0;
        bool consistent = true, noThrow = true;
        auto check = [&](const std::string& text) {
            try {
                DiceParse p = ParseDice(text);
                if (p.ok) {
                    ++parsedOk;
                    if (!p.expr.Valid() || p.error != DiceError::None || !p.message.empty()) consistent = false;
                    const int roll = p.expr.Roll(r);
                    if (roll < p.expr.Min() || roll > p.expr.Max()) consistent = false;
                    if (p.expr.Min() > p.expr.Max()) consistent = false;
                    if (!(p.expr.Average() >= p.expr.Min() - 1e-6 && p.expr.Average() <= p.expr.Max() + 1e-6)) consistent = false;
                    DiceParse again = ParseDice(p.expr.ToString());          // normalised text parses back to the same thing
                    if (!again.ok || again.expr.ToString() != p.expr.ToString() || again.expr.Min() != p.expr.Min() || again.expr.Max() != p.expr.Max()) consistent = false;
                } else {
                    ++parsedBad;
                    if (p.error == DiceError::None || p.message.empty() || p.expr.Valid()) consistent = false;
                    if (p.position < 0 || (std::size_t)p.position > text.size()) consistent = false;
                }
            } catch (...) { noThrow = false; }
        };
        for (int i = 0; i < 150000; ++i) {                                    // random strings over the notation's alphabet
            std::string s; const int len = (int)r.Below(24);
            for (int k = 0; k < len; ++k) s += alphabet[(std::size_t)r.Below(alphabet.size())];
            check(s);
        }
        const char* seeds[] = {"2d6+3", "4d6kh3", "1d20-2", "d%", "1d6+1d4+2", "2d20kl1"};
        for (int i = 0; i < 100000; ++i) {                                    // mutations of valid strings
            std::string s = seeds[r.Below(6)];
            const int edits = 1 + (int)r.Below(3);
            for (int e = 0; e < edits && !s.empty(); ++e) {
                const std::size_t at = (std::size_t)r.Below(s.size());
                switch (r.Below(3)) {
                    case 0: s.erase(at, 1); break;
                    case 1: s.insert(at, 1, alphabet[(std::size_t)r.Below(alphabet.size())]); break;
                    default: s[at] = alphabet[(std::size_t)r.Below(alphabet.size())]; break;
                }
            }
            check(s);
        }
        for (int i = 0; i < 20000; ++i) {                                     // raw bytes, long ones included
            std::string s; const int len = (int)r.Below(300);
            for (int k = 0; k < len; ++k) s += (char)r.Below(256);
            check(s);
        }
        CHECK(noThrow);
        CHECK(consistent);
        CHECK(parsedOk > 1000 && parsedBad > 1000);                           // both sides of the fuzz were exercised
    }

    TEST_MAIN_RESULT();
}
