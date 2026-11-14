// Global illumination foundation: the CPU ray tracer (BVH), the path-tracing probe
// baker, L2 spherical harmonics, the light probe volume (interpolation, ambient
// lookup, serialization) and hostile-input handling. All CPU; nothing here needs a GPU.
#include "test_framework.hpp"
#include "okay/Render/GiBaker.hpp"
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace okay;

namespace {

GiScene EmptyScene(const Vec3& sky) {
    GiScene s; s.sky.top = s.sky.horizon = s.sky.bottom = sky; return s;
}

// Closed room (box) whose six faces share one material.
GiScene Room(float albedo, float emit) {
    GiScene s = EmptyScene({0, 0, 0});
    int m = s.AddMaterial({{albedo, albedo, albedo}, {emit, emit, emit}});
    s.AddBox({-5, -5, -5}, {5, 5, 5}, m);
    return s;
}

LightProbeVolume OneProbe(const Vec3& at) {
    LightProbeVolume v; v.boundsMin = v.boundsMax = at; v.Resize(1, 1, 1); return v;
}

float Frand(GiRng& r, float lo, float hi) { return lo + (hi - lo) * r.Float(); }

std::vector<GiTri> RandomSoup(GiRng& r, int n, float extent) {
    std::vector<GiTri> t;
    for (int i = 0; i < n; ++i) {
        Vec3 c{Frand(r, -extent, extent), Frand(r, -extent, extent), Frand(r, -extent, extent)};
        auto off = [&]() { return Vec3{Frand(r, -2, 2), Frand(r, -2, 2), Frand(r, -2, 2)}; };
        t.push_back({c + off(), c + off(), c + off(), 0});
    }
    return t;
}

void TestBvhVsBrute() {
    RUN_SUITE("gi bvh vs brute force");
    GiRng r(7);
    auto soup = RandomSoup(r, 400, 10.0f);
    // Poison: degenerate, NaN and infinite triangles must be ignored, not crash.
    soup.push_back({{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, 0});
    soup.push_back({{1, 1, 1}, {2, 2, 2}, {3, 3, 3}, 0});                 // collinear
    soup.push_back({{NAN, 0, 0}, {1, 0, 0}, {0, 1, 0}, 0});
    soup.push_back({{INFINITY, 0, 0}, {1, 0, 0}, {0, 1, 0}, 0});
    GiBvh bvh; bvh.Build(soup);
    CHECK(bvh.TriangleCount() == 400);
    int hits = 0, mism = 0, anyMism = 0;
    for (int i = 0; i < 3000; ++i) {
        GiRay ray;
        ray.o = {Frand(r, -12, 12), Frand(r, -12, 12), Frand(r, -12, 12)};
        ray.d = GiTracer::UniformSphere(r.Float(), r.Float());
        if (i % 7 == 0) ray.d = {1, 0, 0};                                 // axis aligned (zero components)
        if (i % 11 == 0) ray.tMax = Frand(r, 0.5f, 8.0f);
        GiHit a, b;
        const bool ha = bvh.Closest(ray, a), hb = GiBvh::BruteClosest(soup, ray, b);
        if (ha != hb) ++mism;
        else if (ha) { ++hits; if (std::fabs(a.t - b.t) > 1e-3f * (1.0f + b.t)) ++mism; }
        if (bvh.Any(ray) != hb) ++anyMism;
    }
    CHECK(mism == 0);
    CHECK(anyMism == 0);
    CHECK(hits > 100);          // the test is not vacuous
    GiBvh empty; empty.Build({});
    GiHit h; GiRay ray; ray.o = {0, 0, 0}; ray.d = {0, 1, 0};
    CHECK(!empty.Closest(ray, h));
    CHECK(!empty.Any(ray));
}

void TestFurnace() {
    RUN_SUITE("gi furnace: closed room converges to E/(1-a)");
    struct Case { float albedo, emit; };
    for (Case c : {Case{0.5f, 0.2f}, Case{0.8f, 0.1f}, Case{0.0f, 0.3f}}) {
        GiScene s = Room(c.albedo, c.emit);
        LightProbeVolume v = OneProbe({0.5f, -1.0f, 2.0f});
        GiBakeSettings set; set.samplesPerProbe = 4096; set.maxBounces = 120; set.seed = 3; set.maxRadiance = 1e9f;
        CHECK(GiBake(s, v, set) == GiBakeResult::Done);
        const float expect = c.emit / (1.0f - c.albedo);
        for (Vec3 n : {Vec3{0, 1, 0}, Vec3{0, -1, 0}, Vec3{1, 0, 0}, Vec3{0.3f, 0.5f, -0.8f}}) {
            Vec3 a = ShAmbient(v.probes[0], n.Normalized());
            CHECK_NEAR(a.x, expect, 0.04 * expect + 1e-4);
            CHECK_NEAR(a.y, expect, 0.04 * expect + 1e-4);
        }
    }
}

void TestEnergy() {
    RUN_SUITE("gi energy conservation");
    // Uniform sky, no emitters: no path may return more than the sky radiance.
    GiScene s = EmptyScene({1, 1, 1});
    int m = s.AddMaterial({{0.9f, 0.9f, 0.9f}, {0, 0, 0}});
    s.AddBox({-2, -2, -2}, {2, 2, 2}, m);
    s.AddBox({6, -1, -1}, {8, 1, 1}, m);
    GiBvh bvh; bvh.Build(s.tris);
    GiBakeSettings set; set.maxBounces = 8;
    GiTracer tr(s, bvh, set);
    GiRng rng(11);
    float worst = 0.0f;
    for (int i = 0; i < 4000; ++i) {
        Vec3 o{Frand(rng, 2.5f, 5.5f), Frand(rng, -1.5f, 1.5f), Frand(rng, -1.5f, 1.5f)};
        Vec3 r = tr.Radiance(o, GiTracer::UniformSphere(rng.Float(), rng.Float()), rng);
        worst = std::fmax(worst, std::fmax(r.x, std::fmax(r.y, r.z)));
    }
    CHECK(worst <= 1.0f + 1e-4f);

    // Empty scene, uniform sky L: ambient is L from every direction.
    GiScene sky = EmptyScene({0.3f, 0.5f, 0.7f});
    LightProbeVolume v = OneProbe({0, 0, 0});
    GiBakeSettings st; st.samplesPerProbe = 4096;
    CHECK(GiBake(sky, v, st) == GiBakeResult::Done);
    Vec3 a = ShAmbient(v.probes[0], {0, 1, 0});
    CHECK_NEAR(a.x, 0.3, 0.006); CHECK_NEAR(a.y, 0.5, 0.006); CHECK_NEAR(a.z, 0.7, 0.006);

    // Sun on a big floor: the floor reflects albedo*S*cos, so the ambient for a
    // downward normal is about that (L2 SH smooths the hemisphere edge).
    GiScene fl = EmptyScene({0, 0, 0});
    int fm = fl.AddMaterial({{0.5f, 0.5f, 0.5f}, {0, 0, 0}});
    fl.AddTriangle({-3000, 0, -3000}, {-3000, 0, 3000}, {3000, 0, -3000}, fm);
    fl.AddTriangle({3000, 0, 3000}, {3000, 0, -3000}, {-3000, 0, 3000}, fm);
    LightSample sun; sun.type = 0; sun.dir = Vec3{-0.3f, -1.0f, 0.2f}.Normalized(); sun.color = {2, 2, 2};
    fl.lights.push_back(sun);
    LightProbeVolume fv = OneProbe({0, 1, 0});
    GiBakeSettings fs; fs.samplesPerProbe = 2048; fs.maxBounces = 2;
    CHECK(GiBake(fl, fv, fs) == GiBakeResult::Done);
    const float cosT = -sun.dir.y;
    const float expect = 0.5f * 2.0f * cosT;
    Vec3 down = ShAmbient(fv.probes[0], {0, -1, 0});
    CHECK_NEAR(down.x, expect, 0.08 * expect);
    Vec3 up = ShAmbient(fv.probes[0], {0, 1, 0});
    CHECK(up.x < 0.2f * expect);
    // Total bounced energy is bounded by what arrived: <= albedo * S * cos.
    CHECK(down.x <= expect * 1.08f);
}

void TestShadowRay() {
    RUN_SUITE("gi direct light shadows");
    GiScene s = EmptyScene({0, 0, 0});
    int m = s.AddMaterial({});
    LightSample pt; pt.type = 1; pt.pos = {0, 5, 0}; pt.range = 20; pt.color = {1, 1, 1};
    s.lights.push_back(pt);
    GiBvh open; open.Build(s.tris);
    GiBakeSettings set;
    GiTracer t0(s, open, set);
    Vec3 lit = t0.Direct({0, 0, 0}, {0, 1, 0});
    CHECK(lit.x > 0.1f);
    s.AddBox({-1, 2, -1}, {1, 3, 1}, m);
    GiBvh blocked; blocked.Build(s.tris);
    GiTracer t1(s, blocked, set);
    CHECK(t1.Direct({0, 0, 0}, {0, 1, 0}).x == 0.0f);
    CHECK(t1.Direct({8, 0, 0}, {0, 1, 0}).x > 0.0f);        // off to the side, no blocker
    CHECK(t0.Direct({0, 0, 0}, {0, -1, 0}).x == 0.0f);      // facing away
}

void TestSh() {
    RUN_SUITE("gi spherical harmonics");
    const int N = 64;
    auto project = [&](auto fn) {
        ShRGB sh; const float w = 4.0f * kGiPi / (float)(N * N);
        for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j) {
            Vec3 d = GiTracer::UniformSphere((i + 0.5f) / N, (j + 0.5f) / N);
            ShAddSample(sh, d, fn(d), w);
        }
        return sh;
    };
    // Constant and linear radiance are inside L1: exact.
    ShRGB c = project([](const Vec3&) { return Vec3{0.4f, 0.4f, 0.4f}; });
    CHECK_NEAR(ShAmbient(c, {0, 1, 0}).x, 0.4, 1e-3);
    CHECK_NEAR(ShEvalRadiance(c, {0.6f, 0, 0.8f}).x, 0.4, 1e-3);
    ShRGB g = project([](const Vec3& d) { float v = 0.5f + 0.5f * d.y; return Vec3{v, v, v}; });
    for (Vec3 n : {Vec3{0, 1, 0}, Vec3{0, -1, 0}, Vec3{1, 0, 0}}) {
        const float e = 0.5f + 0.5f * (2.0f / 3.0f) * n.y;
        CHECK_NEAR(ShAmbient(g, n).x, e, 2e-3);
        CHECK_NEAR(ShEvalRadiance(g, n).x, 0.5f + 0.5f * n.y, 2e-3);
    }
    // A quadratic function (inside L2) reconstructs exactly.
    ShRGB q = project([](const Vec3& d) { float v = d.x * d.z; return Vec3{v, v, v}; });
    CHECK_NEAR(ShEvalRadiance(q, Vec3{0.6f, 0, 0.8f}).x, 0.48, 3e-3);
    // Hemisphere step: ambient error bounded (L2 irradiance is accurate to a few percent).
    ShRGB st = project([](const Vec3& d) { float v = d.y > 0 ? 1.0f : 0.0f; return Vec3{v, v, v}; });
    CHECK_NEAR(ShAmbient(st, {0, 1, 0}).x, 1.0, 0.06);
    CHECK_NEAR(ShAmbient(st, {0, -1, 0}).x, 0.0, 0.06);
    CHECK_NEAR(ShAmbient(st, {1, 0, 0}).x, 0.5, 0.04);
    // Ambient never negative (ringing clamp).
    CHECK(ShAmbient(st, {0, -1, 0}).x >= 0.0f);
}


// The SH constants were derived by hand; check them numerically.
void TestShConstants() {
    RUN_SUITE("gi SH constants (orthonormality, cosine convolution)");
    const int N = 400;
    static double G[kShCoeffs][kShCoeffs]; for (auto& r : G) for (double& x : r) x = 0;
    const double w = 4.0 * 3.14159265358979323846 / ((double)N * N);
    for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j) {
        Vec3 d = GiTracer::UniformSphere((i + 0.5f) / N, (j + 0.5f) / N);
        float b[kShCoeffs]; ShBasis(d, b);
        for (int p = 0; p < kShCoeffs; ++p) for (int q = 0; q < kShCoeffs; ++q) G[p][q] += (double)b[p] * b[q] * w;
    }
    for (int p = 0; p < kShCoeffs; ++p) for (int q = 0; q < kShCoeffs; ++q)
        CHECK_NEAR(G[p][q], p == q ? 1.0 : 0.0, 2e-3);
    // ShAmbient must equal (1/pi) * integral of radiance * max(0, n.w) for random SH radiance.
    GiRng r(21);
    for (int trial = 0; trial < 5; ++trial) {
        ShRGB sh; for (auto& c : sh.c) c = {Frand(r, -1, 1), 0, 0};
        sh.c[0].x = 3.0f;                                    // keep it positive enough
        Vec3 n = GiTracer::UniformSphere(r.Float(), r.Float());
        double acc = 0;
        for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j) {
            Vec3 d = GiTracer::UniformSphere((i + 0.5f) / N, (j + 0.5f) / N);
            double c = Vec3::Dot(n, d); if (c <= 0) continue;
            acc += ShEvalRadiance(sh, d).x * c * w;
        }
        CHECK_NEAR(ShAmbient(sh, n).x, acc / 3.14159265358979323846, 4e-3);
    }
}

// The generator and seed mixer are our own; check their statistics.
void TestRng() {
    RUN_SUITE("gi rng quality");
    GiRng r(1);
    const int N = 262144; int bucket[256] = {}; double mean = 0, corr = 0, prev = 0;
    for (int i = 0; i < N; ++i) { float f = r.Float(); CHECK(f >= 0.0f && f < 1.0f); bucket[(int)(f * 256)]++; mean += f; corr += (f - 0.5) * (prev - 0.5); prev = f; }
    double chi = 0, e = N / 256.0; for (int b : bucket) chi += (b - e) * (b - e) / e;
    CHECK(chi < 340.0);                       // 255 degrees of freedom; 340 is far beyond 99.9%
    CHECK_NEAR(mean / N, 0.5, 0.005);
    CHECK_NEAR(corr / N, 0.0, 0.003);         // lag-1 covariance (variance would be 1/12)
    // Avalanche: flipping one input bit flips about half of the output bits.
    double flips = 0; int cnt = 0;
    for (uint64_t x = 1; x < 2000; x += 7) for (int bit = 0; bit < 64; bit += 5) {
        flips += __builtin_popcountll(GiMix64(x) ^ GiMix64(x ^ (1ULL << bit))); ++cnt;
    }
    CHECK_NEAR(flips / cnt, 32.0, 1.5);
    // Neighbouring probe seeds give different streams.
    CHECK(GiMixSeed(5, 0) != GiMixSeed(5, 1));
    CHECK(GiRng(GiMixSeed(5, 0)).Next() != GiRng(GiMixSeed(5, 1)).Next());
}

LightProbeVolume RandomVolume(GiRng& r, int x, int y, int z) {
    LightProbeVolume v; v.boundsMin = {-4, 0, -4}; v.boundsMax = {4, 6, 4}; v.Resize(x, y, z);
    for (auto& p : v.probes) for (auto& c : p.c) c = {Frand(r, -1, 1), Frand(r, -1, 1), Frand(r, -1, 1)};
    return v;
}

void TestInterpolation() {
    RUN_SUITE("gi probe interpolation");
    GiRng r(5);
    LightProbeVolume v = RandomVolume(r, 5, 4, 6);
    // At a probe position the blend is exactly that probe.
    for (int i = 0; i < 20; ++i) {
        int x = (int)(r.Float() * 5) % 5, y = (int)(r.Float() * 4) % 4, z = (int)(r.Float() * 6) % 6;
        ShRGB s = v.SampleShAt(v.ProbePosition(x, y, z));
        const ShRGB& e = v.probes[v.Index(x, y, z)];
        for (int k = 0; k < kShCoeffs; ++k) CHECK_NEAR(s.c[k].y, e.c[k].y, 1e-4);
    }
    // Midway between two probes is their average.
    {
        Vec3 a = v.ProbePosition(1, 1, 1), b = v.ProbePosition(2, 1, 1);
        ShRGB m = v.SampleShAt((a + b) * 0.5f);
        for (int k = 0; k < kShCoeffs; ++k)
            CHECK_NEAR(m.c[k].x, 0.5f * (v.probes[v.Index(1, 1, 1)].c[k].x + v.probes[v.Index(2, 1, 1)].c[k].x), 1e-4);
    }
    // Continuity: tiny steps along a line across many cell borders never jump.
    float worst = 0.0f; Vec3 prev{0, 0, 0}; bool first = true;
    const int steps = 20000;
    for (int i = 0; i <= steps; ++i) {
        float t = (float)i / steps;
        Vec3 p{-4.0f + 8.0f * t, 0.3f + 5.4f * t, 3.9f - 7.8f * t};
        Vec3 a = ShAmbient(v.SampleShAt(p), Vec3{0.2f, 0.9f, 0.1f}.Normalized());
        if (!first) worst = std::fmax(worst, std::fabs(a.x - prev.x));
        prev = a; first = false;
    }
    CHECK(worst < 0.02f);       // Lipschitz: a 4e-4 step moves the result a little, never a cell's worth
    // Outside the box the sample clamps to the edge (no extrapolation blow-up).
    ShRGB out = v.SampleShAt({100, 100, 100}), edge = v.SampleShAt({4, 6, 4});
    for (int k = 0; k < kShCoeffs; ++k) CHECK_NEAR(out.c[k].x, edge.c[k].x, 1e-5);
    // A one-probe axis sits mid-box.
    LightProbeVolume one = RandomVolume(r, 1, 1, 1);
    CHECK_NEAR(one.ProbePosition(0, 0, 0).y, 3.0, 1e-5);
    ShRGB o1 = one.SampleShAt({2, 5, -3});
    CHECK_NEAR(o1.c[0].x, one.probes[0].c[0].x, 1e-6);
}

void TestAmbientLookup() {
    RUN_SUITE("gi ambient lookup");
    SceneLights::Clear(); SceneLights::Hemisphere() = false;
    SceneLights::SetAmbientColor({0.25f, 0.25f, 0.25f});
    LightProbeVolume v; v.boundsMin = {0, 0, 0}; v.boundsMax = {10, 10, 10}; v.Resize(2, 2, 2);
    for (auto& p : v.probes) p.c[0] = Vec3{1, 2, 3} * (1.0f / 0.282095f);   // uniform radiance (1,2,3)
    Vec3 in = ProbeAmbientAt(&v, {5, 5, 5}, {0, 1, 0});
    CHECK_NEAR(in.x, 1.0, 1e-3); CHECK_NEAR(in.y, 2.0, 1e-3); CHECK_NEAR(in.z, 3.0, 1e-3);
    Vec3 outside = ProbeAmbientAt(&v, {50, 5, 5}, {0, 1, 0});
    CHECK_NEAR(outside.x, 0.25, 1e-5);                       // falls back to the scene ambient
    CHECK_NEAR(ProbeAmbientAt(nullptr, {5, 5, 5}, {0, 1, 0}).x, 0.25, 1e-5);
    CHECK_NEAR(ProbeAmbientAt(&v, {5, 5, 5}, {0, 0, 0}).x, 1.0, 1e-3);   // zero normal does not NaN
    v.edgeFade = 2.0f;
    CHECK_NEAR(ProbeAmbientAt(&v, {5, 5, 5}, {0, 1, 0}).x, 1.0, 1e-3);   // deep inside: full
    float edge = ProbeAmbientAt(&v, {1, 5, 5}, {0, 1, 0}).x;             // 1 unit in: half way
    CHECK_NEAR(edge, 0.625, 1e-3);
    CHECK_NEAR(ProbeAmbientAt(&v, {0, 5, 5}, {0, 1, 0}).x, 0.25, 1e-3); // on the edge: fallback
    SceneLights::SetAmbient(0.25f);
}

std::vector<uint8_t> BakeSmall(uint64_t seed) {
    GiScene s = Room(0.6f, 0.0f);
    LightSample pt; pt.type = 1; pt.pos = {0, 3, 0}; pt.range = 30; pt.color = {1, 0.9f, 0.8f};
    s.lights.push_back(pt);
    LightProbeVolume v; v.boundsMin = {-3, -3, -3}; v.boundsMax = {3, 3, 3}; v.Resize(3, 3, 3);
    GiBakeSettings set; set.samplesPerProbe = 64; set.maxBounces = 3; set.seed = seed;
    GiBake(s, v, set);
    return v.Serialize();
}

void TestDeterminism() {
    RUN_SUITE("gi baker determinism, progress, cancel");
    CHECK(BakeSmall(42) == BakeSmall(42));
    CHECK(!(BakeSmall(42) == BakeSmall(43)));

    // Order independence: bake probes backwards one by one, same bytes as GiBake.
    GiScene s = Room(0.6f, 0.0f);
    LightSample pt; pt.type = 1; pt.pos = {0, 3, 0}; pt.range = 30; pt.color = {1, 0.9f, 0.8f};
    s.lights.push_back(pt);
    LightProbeVolume v; v.boundsMin = {-3, -3, -3}; v.boundsMax = {3, 3, 3}; v.Resize(3, 3, 3);
    GiBakeSettings set; set.samplesPerProbe = 64; set.maxBounces = 3; set.seed = 42;
    GiBvh bvh; bvh.Build(s.tris); GiTracer tr(s, bvh, set);
    for (int z = 2; z >= 0; --z) for (int y = 2; y >= 0; --y) for (int x = 2; x >= 0; --x) {
        int i = v.Index(x, y, z);
        GiRng rng(GiMixSeed(set.seed, (uint64_t)i), (uint64_t)i);
        v.probes[i] = GiBakeProbe(tr, set, v.ProbePosition(x, y, z), rng);
    }
    CHECK(v.Serialize() == BakeSmall(42));

    // Progress is monotonic and ends at 1; returning false cancels.
    LightProbeVolume pv; pv.Resize(2, 2, 2);
    std::vector<float> seen;
    GiBakeSettings quick; quick.samplesPerProbe = 8; quick.maxBounces = 1;
    GiScene es = EmptyScene({1, 1, 1});
    CHECK(GiBake(es, pv, quick, [&](float f) { seen.push_back(f); return true; }) == GiBakeResult::Done);
    CHECK(seen.size() == 8);
    for (size_t i = 1; i < seen.size(); ++i) CHECK(seen[i] > seen[i - 1]);
    CHECK_NEAR(seen.back(), 1.0, 1e-6);
    int calls = 0;
    LightProbeVolume cv; cv.Resize(2, 2, 2);
    CHECK(GiBake(es, cv, quick, [&](float) { return ++calls < 3; }) == GiBakeResult::Cancelled);
    CHECK(calls == 3);
    LightProbeVolume bad; bad.nx = 0;
    CHECK(GiBake(es, bad, quick) == GiBakeResult::BadInput);
    // Meshes can be added through a transform.
    Mesh m; m.vertices = {{0, 0, 0}, {0, 0, 1}, {1, 0, 0}}; m.triangles = {0, 1, 2, 0, 1, 9};
    GiScene ms; ms.AddMesh(m, Mat4::Translate({0, 2, 0}), 0);
    CHECK(ms.tris.size() == 1);                              // the out-of-range index triangle is skipped
    CHECK_NEAR(ms.tris[0].a.y, 2.0, 1e-5);
}

void FixCrc(std::vector<uint8_t>& b) {
    uint32_t c = LightProbeVolume::Crc32(b.data(), b.size() - 4);
    for (int i = 0; i < 4; ++i) b[b.size() - 4 + i] = (uint8_t)(c >> (8 * i));
}

void TestSerialization() {
    RUN_SUITE("gi serialization");
    GiRng r(9);
    LightProbeVolume v = RandomVolume(r, 3, 2, 4);
    v.boundsMin = {-1, -2, -3}; v.boundsMax = {4, 5, 6};
    std::vector<uint8_t> bytes = v.Serialize();
    LightProbeVolume back; std::string err;
    CHECK(LightProbeVolume::Deserialize(bytes, back, &err));
    CHECK(back.nx == 3 && back.ny == 2 && back.nz == 4);
    CHECK(back.boundsMin == v.boundsMin && back.boundsMax == v.boundsMax);
    CHECK(back.probes.size() == v.probes.size());
    bool same = true;
    for (size_t i = 0; i < v.probes.size(); ++i)
        for (int k = 0; k < kShCoeffs; ++k) same = same && v.probes[i].c[k] == back.probes[i].c[k];
    CHECK(same);
    CHECK(back.Serialize() == bytes);

    // Every truncation fails and leaves the output alone.
    LightProbeVolume keep = v;
    for (size_t n = 0; n < bytes.size(); ++n) {
        LightProbeVolume o = keep;
        CHECK(!LightProbeVolume::Deserialize(bytes.data(), n, o, &err));
        CHECK(o.nx == keep.nx);
    }
    // Extra trailing bytes fail.
    { auto b = bytes; b.push_back(0); LightProbeVolume o; CHECK(!LightProbeVolume::Deserialize(b, o)); }
    // Any single bit flip is caught by the checksum.
    int missed = 0;
    for (size_t i = 0; i < bytes.size(); i += 7) {
        auto b = bytes; b[i] ^= (uint8_t)(1u << (i % 8));
        LightProbeVolume o; if (LightProbeVolume::Deserialize(b, o)) ++missed;
    }
    CHECK(missed == 0);
    // Bad headers with a repaired checksum: still rejected, without a big allocation.
    auto hostile = [&](size_t off, uint32_t val) {
        auto b = bytes; for (int i = 0; i < 4; ++i) b[off + i] = (uint8_t)(val >> (8 * i));
        FixCrc(b); LightProbeVolume o; return LightProbeVolume::Deserialize(b, o);
    };
    CHECK(!hostile(0, 0x12345678));            // magic
    CHECK(!hostile(4, 99));                    // version
    CHECK(!hostile(8, 0));                     // zero grid
    CHECK(!hostile(8, 0xFFFFFFFFu));           // huge grid
    CHECK(!hostile(12, 0x7FFFFFFFu));
    CHECK(!hostile(16, 5));                    // size mismatch
    CHECK(!hostile(20, 0x7FC00000u));          // NaN bound
    CHECK(!hostile(20, 0x7F800000u));          // +inf bound
    { // min > max
        LightProbeVolume w = v; w.boundsMin = {5, 0, 0}; w.boundsMax = {1, 1, 1};
        auto b = w.Serialize(); LightProbeVolume o; CHECK(!LightProbeVolume::Deserialize(b, o));
    }
    // NaN coefficient with a valid checksum.
    { auto b = bytes; size_t off = 44; uint32_t nan = 0x7FC00000u;
      for (int i = 0; i < 4; ++i) b[off + i] = (uint8_t)(nan >> (8 * i));
      FixCrc(b); LightProbeVolume o; CHECK(!LightProbeVolume::Deserialize(b, o)); }
    LightProbeVolume o2; CHECK(!LightProbeVolume::Deserialize(nullptr, 0, o2, &err));
    CHECK(!err.empty());
    // Resize refuses absurd sizes.
    LightProbeVolume big; CHECK(!big.Resize(100000, 100000, 100000)); CHECK(!big.Resize(0, 1, 1));
}

void TestFuzz() {
    RUN_SUITE("gi fuzz");
    GiRng r(1234);
    // Random bytes, and mutated valid files with a repaired checksum.
    LightProbeVolume v = RandomVolume(r, 2, 2, 2);
    const auto valid = v.Serialize();
    int accepted = 0;
    for (int i = 0; i < 4000; ++i) {
        std::vector<uint8_t> b;
        if (i % 2 == 0) { b.resize(r.Next() % 600); for (auto& x : b) x = (uint8_t)r.Next(); }
        else {
            b = valid;
            int edits = 1 + r.Next() % 4;
            for (int e = 0; e < edits; ++e) b[r.Next() % (b.size() - 4)] = (uint8_t)r.Next();
            FixCrc(b);
        }
        LightProbeVolume o;
        if (LightProbeVolume::Deserialize(b, o)) {
            ++accepted;
            CHECK(o.Valid());
            Vec3 a = ShAmbient(o.SampleShAt({0, 1, 0}), {0, 1, 0});     // usable, finite
            CHECK(std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z));
        }
    }
    CHECK(accepted >= 0);
    // Hostile triangles and rays into the BVH and the baker.
    for (int round = 0; round < 30; ++round) {
        auto soup = RandomSoup(r, 1 + r.Next() % 60, 5.0f);
        for (auto& t : soup) {
            if (r.Next() % 6 == 0) t.a.x = NAN;
            if (r.Next() % 9 == 0) t.b = t.a;
            if (r.Next() % 13 == 0) t.c.y = 1e30f;
            if (r.Next() % 17 == 0) t.material = -5 + (int)(r.Next() % 100);
        }
        GiBvh bvh; bvh.Build(soup);
        for (int i = 0; i < 50; ++i) {
            GiRay ray; ray.o = {Frand(r, -9, 9), Frand(r, -9, 9), Frand(r, -9, 9)};
            ray.d = (i % 5 == 0) ? Vec3{0, 0, 0} : GiTracer::UniformSphere(r.Float(), r.Float());
            if (i % 13 == 0) ray.o.x = NAN;
            GiHit h; bvh.Closest(ray, h); bvh.Any(ray);
        }
        GiScene sc; sc.tris = soup; sc.materials.push_back({});
        LightProbeVolume pv; pv.boundsMin = {-3, -3, -3}; pv.boundsMax = {3, 3, 3}; pv.Resize(2, 2, 1);
        GiBakeSettings set; set.samplesPerProbe = 16; set.maxBounces = 3;
        CHECK(GiBake(sc, pv, set) == GiBakeResult::Done);
        for (auto& p : pv.probes) for (auto& c : p.c) CHECK(std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z));
    }
    // Weird sample positions.
    LightProbeVolume sv = RandomVolume(r, 3, 3, 3);
    for (Vec3 p : {Vec3{NAN, 0, 0}, Vec3{INFINITY, -INFINITY, 1e30f}, Vec3{-1e30f, 0, 0}}) {
        Vec3 a = ProbeAmbientAt(&sv, p, {0, 1, 0}, {0.1f, 0.1f, 0.1f});
        CHECK(std::isfinite(a.x));
    }
}

void TestPerformance() {
    RUN_SUITE("gi performance (printed, loose bound)");
    using clk = std::chrono::steady_clock;
    GiRng r(77);
    auto soup = RandomSoup(r, 20000, 40.0f);
    auto t0 = clk::now();
    GiBvh bvh; bvh.Build(soup);
    double buildMs = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
    t0 = clk::now();
    int hits = 0; const int rays = 200000;
    for (int i = 0; i < rays; ++i) {
        GiRay ray; ray.o = {Frand(r, -40, 40), Frand(r, -40, 40), Frand(r, -40, 40)};
        ray.d = GiTracer::UniformSphere(r.Float(), r.Float());
        GiHit h; hits += bvh.Closest(ray, h);
    }
    double rayMs = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
    GiScene s = Room(0.6f, 0.0f);
    LightProbeVolume v; v.boundsMin = {-4, -4, -4}; v.boundsMax = {4, 4, 4}; v.Resize(4, 4, 4);
    GiBakeSettings set; set.samplesPerProbe = 256; set.maxBounces = 4;
    t0 = clk::now();
    GiBake(s, v, set);
    double bakeMs = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
    std::printf("  PERF: bvh build 20000 tris %.1f ms | %d rays %.1f ms (%.2f Mray/s, %d hits) | bake 64 probes x 256 rays x 4 bounces %.1f ms\n",
                buildMs, rays, rayMs, rays / rayMs / 1000.0, hits, bakeMs);
    CHECK(buildMs < 20000.0 && rayMs < 60000.0 && bakeMs < 60000.0);
}

} // namespace

int main() {
    TestBvhVsBrute();
    TestFurnace();
    TestEnergy();
    TestShadowRay();
    TestSh();
    TestShConstants();
    TestRng();
    TestInterpolation();
    TestAmbientLookup();
    TestDeterminism();
    TestSerialization();
    TestFuzz();
    TestPerformance();
    TEST_MAIN_RESULT();
}
