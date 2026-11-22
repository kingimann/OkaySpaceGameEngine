#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/DollyPath.hpp"
#include "okay/Components/Spline.hpp"
#include "okay/Math/Curves.hpp"
#include "okay/Modeling/Curve.hpp"
#include "okay/Render/MeshHash.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// Grok's #422 finding 6: the Spline component, Modeling/Curve, DollyPath and the DOTween path each wrote their own curve
// formula next to Math/Curves. They now call Math/Curves. This test proves the change is safe:
//   1. each old formula (kept below exactly as it was) and the Math/Curves function it was replaced by give the SAME
//      BITS on 200,000 random inputs (Bezier and its tangent; the uniform Catmull-Rom DollyPath used). Equal bits mean
//      SplineMesh and CurveSweep regenerate exactly the same meshes for saved scenes.
//   2. the DOTween path used the same Catmull-Rom polynomial with its terms added in another order, so it can differ in
//      the last bits of a float: the test measures the largest difference and holds it under 1e-5 of the point spread.
//   3. it prints a hash of real outputs (Spline samples, CurveSweep and CurvePipe meshes, DollyPath and a running
//      DOTween path) so a run before and a run after the change can be compared on one machine.
using namespace okay;

namespace {

// ---- the old copies, as they were ---------------------------------------------------------------------------------
Vec3 OldSplineBez(const Vec3* c, float u) {          // engine/src/Components/Spline.cpp
    const float v = 1.0f - u;
    return c[0] * (v * v * v) + c[1] * (3.0f * v * v * u) + c[2] * (3.0f * v * u * u) + c[3] * (u * u * u);
}
Vec3 OldSplineBezD(const Vec3* c, float u) {
    const float v = 1.0f - u;
    return (c[1] - c[0]) * (3.0f * v * v) + (c[2] - c[1]) * (6.0f * v * u) + (c[3] - c[2]) * (3.0f * u * u);
}
Vec3 OldModelingBez(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, float t) {   // Modeling/Curve.cpp
    const float u = 1.0f - t;
    return a * (u * u * u) + b * (3.0f * u * u * t) + c * (3.0f * u * t * t) + d * (t * t * t);
}
Vec3 OldDollyCatmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float u) {   // DollyPath.hpp
    float u2 = u * u, u3 = u2 * u;
    return 0.5f * ((2.0f * p1)
                   + u  * (p2 - p0)
                   + u2 * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3)
                   + u3 * (3.0f * p1 - 3.0f * p2 + p3 - p0));
}
Vec3 OldTweenCatmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float s) {   // Tween.cpp Path
    const float s2 = s * s, s3 = s2 * s;
    return (p1 * 2.0f + (p2 - p0) * s + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * s2
            + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * s3) * 0.5f;
}

bool SameBits(const Vec3& a, const Vec3& b) { return std::memcmp(&a, &b, sizeof(Vec3)) == 0; }

struct Lcg {
    std::uint32_t s;
    float Next(float lo, float hi) { s = s * 1664525u + 1013904223u; return lo + (hi - lo) * (float)(s >> 8) / 16777216.0f; }
    Vec3 V(float r) { return Vec3(Next(-r, r), Next(-r, r), Next(-r, r)); }
};

std::uint64_t HashVecs(const std::vector<Vec3>& v, std::uint64_t h = meshhash::kSeed) {
    return v.empty() ? h : meshhash::Bytes(v.data(), v.size() * sizeof(Vec3), h);
}

} // namespace

int main() {
    RUN_SUITE("curve_formulas");

    // ---- 1. the formulas: same bits -------------------------------------------------------------------------------------
    {
        Lcg rng{12345u};
        int bez = 0, bezD = 0, model = 0, dolly = 0, n = 0;
        for (int i = 0; i < 200000; ++i) {
            const float scale = i % 4 == 0 ? 1000.0f : (i % 4 == 1 ? 1.0f : 50.0f);
            Vec3 c[4] = {rng.V(scale), rng.V(scale), rng.V(scale), rng.V(scale)};
            const float u = i % 97 == 0 ? (float)(i % 2) : rng.Next(0.0f, 1.0f);   // the ends too
            ++n;
            bez   += SameBits(OldSplineBez(c, u), Curves::CubicBezier(c[0], c[1], c[2], c[3], u));
            bezD  += SameBits(OldSplineBezD(c, u), Curves::CubicBezierTangent(c[0], c[1], c[2], c[3], u));
            model += SameBits(OldModelingBez(c[0], c[1], c[2], c[3], u), Curves::CubicBezier(c[0], c[1], c[2], c[3], u));
            dolly += SameBits(OldDollyCatmullRom(c[0], c[1], c[2], c[3], u), Curves::CatmullRomUniform(c[0], c[1], c[2], c[3], u));
        }
        std::printf("  same bits out of %d: Spline Bezier %d, its tangent %d, Modeling Bezier %d, DollyPath Catmull-Rom %d\n",
                    n, bez, bezD, model, dolly);
        CHECK(bez == n); CHECK(bezD == n); CHECK(model == n); CHECK(dolly == n);
    }
    // ---- 2. the DOTween path's formula: the same curve, rounding apart ----------------------------------------------------
    {
        Lcg rng{777u};
        double worst = 0.0; int same = 0, n = 0;
        for (int i = 0; i < 200000; ++i) {
            const float scale = i % 2 ? 100.0f : 1.0f;
            const Vec3 p0 = rng.V(scale), p1 = rng.V(scale), p2 = rng.V(scale), p3 = rng.V(scale);
            const float s = rng.Next(0.0f, 1.0f);
            const Vec3 a = OldTweenCatmullRom(p0, p1, p2, p3, s), b = Curves::CatmullRomUniform(p0, p1, p2, p3, s);
            ++n; same += SameBits(a, b);
            worst = std::max(worst, (double)(a - b).Magnitude() / (double)scale);
        }
        std::printf("  DOTween path: %d of %d exactly equal; largest difference %.3g of the point spread\n", same, n, worst);
        CHECK(worst < 1e-5);
    }

    // ---- 3. real outputs, hashed (compare a run before the change with a run after) ---------------------------------------
    std::uint64_t splineH = meshhash::kSeed, sweepH = meshhash::kSeed, dollyH = meshhash::kSeed, tweenH = meshhash::kSeed;
    {
        Scene scene;
        for (int type = 0; type < 2; ++type)
            for (int closed = 0; closed < 2; ++closed) {
                GameObject* go = scene.CreateGameObject("Spline");
                auto* sp = go->AddComponent<Spline>();
                sp->type = type ? Spline::Type::CatmullRom : Spline::Type::Bezier;
                sp->closed = closed != 0;
                const Vec3 pts[] = {{0, 0, 0}, {4, 1, 2}, {8, 0, -3}, {12, 3, 1}, {15, -2, 6}};
                for (const Vec3& p : pts) sp->AddPoint(p);
                sp->points[2].mode = SplinePoint::Mode::Linear;
                sp->points[3].mode = SplinePoint::Mode::Free;
                sp->points[3].tangentOut = Vec3(1, 2, 0.5f); sp->points[3].tangentIn = Vec3(-2, 0, 1);
                std::vector<Vec3> out;
                for (int k = 0; k <= 256; ++k) {
                    const float t = (float)k / 256.0f;
                    out.push_back(sp->EvaluatePosition(t)); out.push_back(sp->EvaluateTangent(t)); out.push_back(sp->EvaluateUp(t));
                }
                const float len = sp->SampleAtDistance(1e9f).distance;
                for (int k = 0; k <= 64; ++k) { const SplineSample s = sp->SampleAtDistance(len * (float)k / 64.0f); out.push_back(s.position); out.push_back(s.tangent); out.push_back(s.right); }
                splineH = HashVecs(out, splineH);
            }
    }
    {
        const modeling::BezierCurve c = modeling::CurveThrough({{0, 0, 0}, {2, 1, 0}, {4, 0, 2}, {5, 3, 4}, {3, 5, 6}}, false, 0.5f);
        const modeling::BezierCurve cc = modeling::CurveThrough({{0, 0, 0}, {3, 0, 1}, {3, 2, 4}, {-1, 1, 3}}, true, 0.7f);
        sweepH = HashVecs(modeling::SampleCurve(c, 9), sweepH);
        sweepH = HashVecs(modeling::CurvePipe(c, 0.3f, 10, 8, true).vertices, sweepH);
        sweepH = HashVecs(modeling::CurveSweep(cc, {{-0.5f, 0}, {0.5f, 0}, {0.3f, 0.4f}}, true, 6, 0.5f, true).vertices, sweepH);
        sweepH = HashVecs(modeling::CurveRibbon(c, 0.8f, 7, true).vertices, sweepH);
    }
    {
        Scene scene;
        for (int looped = 0; looped < 2; ++looped) {
            GameObject* go = scene.CreateGameObject("Rail");
            auto* dp = go->AddComponent<DollyPath>();
            dp->waypoints = {{0, 0, 0}, {5, 2, 1}, {9, 0, 7}, {3, -1, 12}, {-4, 3, 8}};
            dp->looped = looped != 0;
            std::vector<Vec3> out;
            for (int k = 0; k <= 300; ++k) out.push_back(dp->EvaluatePosition((float)k / 300.0f));
            dollyH = HashVecs(out, dollyH);
        }
    }
    {
        Scene scene;
        GameObject* go = scene.CreateGameObject("Mover");
        DOTween::Path(go, {{0, 0, 0}, {2, 3, 1}, {5, 1, 4}, {7, 4, 2}}, 2.0f, DOTween::PathType::CatmullRom);
        std::vector<Vec3> out;
        for (int i = 0; i < 25; ++i) { scene.Update(0.07f); out.push_back(go->transform->localPosition); }
        tweenH = HashVecs(out, tweenH);
    }
    std::printf("  output hashes: spline %016llx  sweep %016llx  dolly %016llx  tween %016llx\n",
                (unsigned long long)splineH, (unsigned long long)sweepH, (unsigned long long)dollyH, (unsigned long long)tweenH);

    TEST_MAIN_RESULT();
}
