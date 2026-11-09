// Exact damped springs (Math/Spring.hpp): ringing, critical and overdamped motion,
// the same result for any step length, huge and non-finite steps, retargeting,
// vectors, angles and rotations.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <limits>

using namespace okay;

// Step a spring `n` times of `dt`.
static void Run(float& x, float& v, float target, float k, float c, float dt, int n) {
    for (int i = 0; i < n; ++i) Spring::Step(x, v, target, k, c, dt);
}

int main() {
    RUN_SUITE("spring");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    // --- The three regimes ---
    {   // Rings: overshoots the target, then settles on it.
        float x = 0, v = 0, peak = 0;
        for (int i = 0; i < 600; ++i) { Spring::Step(x, v, 1.0f, 100.0f, 4.0f, 0.01f); peak = std::max(peak, x); }
        CHECK(peak > 1.2f);
        CHECK_NEAR(x, 1.0f, 1e-3f);
        CHECK_NEAR(v, 0.0f, 1e-3f);
    }
    {   // Critical (damping = 2 sqrt k): never overshoots, arrives.
        float x = 0, v = 0, peak = 0;
        for (int i = 0; i < 600; ++i) { Spring::Step(x, v, 1.0f, 100.0f, 20.0f, 0.01f); peak = std::max(peak, x); }
        CHECK(peak <= 1.0f + 1e-5f);
        CHECK_NEAR(x, 1.0f, 1e-4f);
    }
    {   // Overdamped: no overshoot either, but slower than critical.
        float xc = 0, vc = 0, xo = 0, vo = 0, peak = 0;
        for (int i = 0; i < 40; ++i) {
            Spring::Step(xc, vc, 1.0f, 100.0f, 20.0f, 0.01f);
            Spring::Step(xo, vo, 1.0f, 100.0f, 60.0f, 0.01f);
            peak = std::max(peak, xo);
        }
        CHECK(peak <= 1.0f + 1e-5f);
        CHECK(xo < xc);
        float x = xo, v = vo;
        Run(x, v, 1.0f, 100.0f, 60.0f, 0.01f, 2000);
        CHECK_NEAR(x, 1.0f, 1e-3f);
    }
    {   // The default spring state: position starts at rest and chases its target.
        Spring s;
        s.target = 2.0f;
        for (int i = 0; i < 500; ++i) s.Update(0.01f);
        CHECK(s.AtRest(1e-2f) && std::fabs(s.position - 2.0f) < 1e-2f);
        s.Snap(7.0f);
        CHECK(s.position == 7.0f && s.target == 7.0f && s.velocity == 0.0f && s.AtRest());
        Spring crit; crit.SetCritical(0.5f);
        CHECK_NEAR(crit.damping, 2.0f * std::sqrt(crit.stiffness), 1e-3f);
        crit.target = 1.0f;
        float peak = 0;
        for (int i = 0; i < 400; ++i) { crit.Update(0.01f); peak = std::max(peak, crit.position); }
        CHECK(peak <= 1.0f + 1e-5f && crit.AtRest(1e-3f));
    }

    // --- Exact: one big step equals many small ones, in every regime ---
    {
        const float damps[] = {3.0f, 20.0f, 70.0f};   // ringing, critical, overdamped for k = 100
        for (float c : damps) {
            float x1 = -2.0f, v1 = 5.0f, x2 = -2.0f, v2 = 5.0f;
            Spring::Step(x1, v1, 3.0f, 100.0f, c, 1.0f);
            Run(x2, v2, 3.0f, 100.0f, c, 0.01f, 100);
            CHECK_NEAR(x1, x2, 1e-4f);
            CHECK_NEAR(v1, v2, 1e-3f);
            float x3 = -2.0f, v3 = 5.0f;
            Run(x3, v3, 3.0f, 100.0f, c, 0.5f, 2);
            CHECK_NEAR(x1, x3, 2e-5f);
        }
    }
    {   // A minute-long hitch and absurd steps land on the target at rest: no blow-up.
        const float steps[] = {60.0f, 1.0e3f, 1.0e9f, 1.0e30f, std::numeric_limits<float>::max()};
        for (float dt : steps) {
            float x = -50.0f, v = 300.0f;
            Spring::Step(x, v, 4.0f, 5000.0f, 3.0f, dt);       // very stiff, barely damped
            CHECK(std::isfinite(x) && std::isfinite(v));
            CHECK_NEAR(x, 4.0f, 1e-3f);
            float y = -50.0f, w = 300.0f;
            Spring::Step(y, w, 4.0f, 5000.0f, 4000.0f, dt);    // overdamped
            CHECK(std::isfinite(y) && std::isfinite(w));
            CHECK_NEAR(y, 4.0f, 1e-2f);
        }
        // Far more stable than integrating the force: the same stiffness diverges by Euler.
        float ex = 0, ev = 0;
        for (int i = 0; i < 20; ++i) { ev += (-5000.0f * (ex - 1.0f) - 3.0f * ev) * 0.1f; ex += ev * 0.1f; }
        CHECK(std::fabs(ex) > 1.0e6f);
        float sx = 0, sv = 0;
        Run(sx, sv, 1.0f, 5000.0f, 3.0f, 0.1f, 100);
        CHECK_NEAR(sx, 1.0f, 1e-3f);
    }

    // --- Bad steps and bad values ---
    {
        for (float dt : {0.0f, -1.0f, nan, inf, -inf}) {
            float x = 1.5f, v = -2.0f;
            Spring::Step(x, v, 9.0f, 100.0f, 10.0f, dt);
            CHECK(x == 1.5f && v == -2.0f);
        }
        float x = 1.0f, v = 2.0f;
        Spring::Step(x, v, nan, 100.0f, 10.0f, 0.1f);          // nothing to chase: untouched
        CHECK(x == 1.0f && v == 2.0f);
        Spring::Step(x, v, inf, 100.0f, 10.0f, 0.1f);
        CHECK(x == 1.0f && v == 2.0f);
        float bx = nan, bv = 3.0f;
        Spring::Step(bx, bv, 5.0f, 100.0f, 10.0f, 0.1f);       // a broken value snaps to the target
        CHECK(bx == 5.0f && bv == 0.0f);
        float cx = 0.0f, cv = inf;
        Spring::Step(cx, cv, 5.0f, 100.0f, 10.0f, 0.1f);
        CHECK(cx == 5.0f && cv == 0.0f);
        // Bad constants: NaN stiffness / damping behave like none.
        float px = 0.0f, pv = 1.0f;
        Spring::Step(px, pv, 0.0f, nan, nan, 0.5f);
        CHECK_NEAR(px, 0.5f, 1e-6f);                           // coasts at its velocity
        CHECK_NEAR(pv, 1.0f, 1e-6f);
    }

    // --- No spring: friction only (a glide), or constant velocity with no damping ---
    {
        float x = 0.0f, v = 10.0f;
        Spring::Step(x, v, 100.0f, 0.0f, 2.0f, 1.0f);
        CHECK_NEAR(v, 10.0f * std::exp(-2.0f), 1e-4f);
        CHECK_NEAR(x, 10.0f * (1.0f - std::exp(-2.0f)) / 2.0f, 1e-4f);
        float y = 3.0f, w = -1.0f;
        Spring::Step(y, w, 0.0f, -5.0f, 0.0f, 2.0f);
        CHECK_NEAR(y, 1.0f, 1e-6f);
        CHECK_NEAR(w, -1.0f, 1e-6f);
    }

    // --- Retargeting keeps the motion smooth: position and velocity are continuous ---
    {
        float x = 0, v = 0;
        Run(x, v, 1.0f, 150.0f, 18.0f, 0.01f, 12);
        const float xBefore = x, vBefore = v;
        Spring::Step(x, v, -3.0f, 150.0f, 18.0f, 1e-6f);        // the target jumps; a tiny step later nothing jumped
        CHECK_NEAR(x, xBefore, 1e-4f);
        CHECK_NEAR(v, vBefore, 0.01f);
        Run(x, v, -3.0f, 150.0f, 18.0f, 0.01f, 600);
        CHECK_NEAR(x, -3.0f, 1e-3f);
    }

    // --- Critical damping through a smooth time, next to SmoothDamp ---
    {
        float x = 0, v = 0, sx = 0, sv = 0;
        for (int i = 0; i < 25; ++i) {                          // 0.25 s of a 0.25 s smooth time
            Spring::StepCritical(x, v, 1.0f, 0.25f, 0.01f);
            sx = Mathf::SmoothDamp(sx, 1.0f, sv, 0.25f, 0.01f);
        }
        CHECK_NEAR(x, sx, 0.01f);
        CHECK(x > 0.5f && x < 1.0f);
        CHECK_NEAR(v, sv, 0.2f);
        // ... and a floor on the time (no divide by zero).
        float y = 0, w = 0;
        Spring::StepCritical(y, w, 1.0f, 0.0f, 0.1f);
        CHECK(std::isfinite(y) && std::isfinite(w));
        Spring::StepCritical(y, w, 1.0f, -5.0f, 0.1f);
        CHECK(std::isfinite(y) && std::isfinite(w));
    }

    // --- Vectors follow per component ---
    {
        Vec2 p{0, 0}, v{0, 0};
        for (int i = 0; i < 400; ++i) Spring::StepVec2(p, v, Vec2{3, -4}, 100.0f, 20.0f, 0.01f);
        CHECK_NEAR(p.x, 3.0f, 1e-3f); CHECK_NEAR(p.y, -4.0f, 1e-3f);
        Vec3 q{1, 1, 1}, w{0, 0, 0};
        for (int i = 0; i < 400; ++i) Spring::StepVec3(q, w, Vec3{0, 5, -2}, 100.0f, 20.0f, 0.01f);
        CHECK_NEAR(q.x, 0.0f, 1e-3f); CHECK_NEAR(q.y, 5.0f, 1e-3f); CHECK_NEAR(q.z, -2.0f, 1e-3f);
        Vec3 r{0, 0, 0}, rv{0, 0, 0};
        Spring::StepCriticalVec3(r, rv, Vec3{1, 2, 3}, 0.2f, 100.0f);
        CHECK_NEAR(r.x, 1.0f, 1e-3f); CHECK_NEAR(r.z, 3.0f, 1e-3f);
        Vec2 s{0, 0}, sv{0, 0};
        Spring::StepCriticalVec2(s, sv, Vec2{2, 4}, 0.2f, 100.0f);
        CHECK_NEAR(s.y, 4.0f, 1e-3f);
    }

    // --- Angles take the short way round ---
    {
        float a = 350.0f, v = 0.0f, peak = 350.0f;
        for (int i = 0; i < 200; ++i) { Spring::StepAngle(a, v, 10.0f, 100.0f, 20.0f, 0.01f); peak = std::max(peak, a); }
        CHECK(peak < 371.0f);                                   // went up through 360, not back down through 180
        CHECK_NEAR(Mathf::DeltaAngle(a, 10.0f), 0.0f, 1e-2f);
        float b = 10.0f, bv = 0.0f;
        for (int i = 0; i < 200; ++i) Spring::StepAngle(b, bv, 350.0f, 100.0f, 20.0f, 0.01f);
        CHECK(b < 10.0f && b > -11.0f);
        float c = nan, cv = 0.0f;
        Spring::StepAngle(c, cv, 90.0f, 100.0f, 20.0f, 0.1f);
        CHECK(c == 90.0f);
    }

    // --- Rotations: along the shortest arc, any step, no overshoot when critical ---
    {
        const Quat target = Quat::Euler(0.0f, 120.0f, 0.0f);
        Quat q = Quat::Identity;
        Vec3 w{0, 0, 0};
        float prev = Quat::Angle(q, target);
        bool monotonic = true;
        for (int i = 0; i < 300; ++i) {
            Spring::StepQuatCritical(q, w, target, 0.3f, 0.01f);
            const float ang = Quat::Angle(q, target);
            if (ang > prev + 0.1f) monotonic = false;   // (acos of a float dot is good to ~0.04 degrees)
            prev = ang;
        }
        CHECK(monotonic);
        CHECK(Quat::Angle(q, target) < 0.5f);
        CHECK_NEAR(Quat::Dot(q, q), 1.0f, 1e-4f);               // stays a unit rotation
        // The opposite sign of the same target is the same rotation: same path.
        const Quat neg{-target.x, -target.y, -target.z, -target.w};
        Quat q2 = Quat::Identity; Vec3 w2{0, 0, 0};
        for (int i = 0; i < 300; ++i) Spring::StepQuatCritical(q2, w2, neg, 0.3f, 0.01f);
        CHECK(Quat::Angle(q2, target) < 0.5f);
        // The short way: from 170 degrees about Y to -170 passes through 180, not through 0.
        Quat from = Quat::Euler(0.0f, 170.0f, 0.0f), to = Quat::Euler(0.0f, -170.0f, 0.0f);
        Vec3 wv{0, 0, 0};
        Spring::StepQuatCritical(from, wv, to, 0.5f, 0.05f);
        CHECK(Quat::Angle(from, Quat::Euler(0.0f, 180.0f, 0.0f)) < 10.0f);   // moved toward 180, not back through 0
        CHECK(Quat::Angle(from, to) < 20.0f);
        // A long hitch lands on the target.
        Quat big = Quat::Identity; Vec3 bw{1, 2, 3};
        Spring::StepQuat(big, bw, target, 400.0f, 5.0f, 1.0e6f);
        CHECK(Quat::Angle(big, target) < 0.1f);
        CHECK_NEAR(bw.x, 0.0f, 1e-3f);
        // A step that is not a number changes nothing.
        Quat keep = Quat::Euler(10, 20, 30); Vec3 kw{1, 1, 1};
        Spring::StepQuat(keep, kw, target, 100.0f, 10.0f, nan);
        CHECK(Quat::Angle(keep, Quat::Euler(10, 20, 30)) < 0.1f && kw.x == 1.0f);
        // Already at the target: stays there.
        Quat at = target; Vec3 aw{0, 0, 0};
        Spring::StepQuat(at, aw, target, 100.0f, 10.0f, 0.1f);
        CHECK(Quat::Angle(at, target) < 0.1f);
    }

    // --- Deterministic: the same inputs give the same bits ---
    {
        float a = 0, av = 0, b = 0, bv = 0;
        for (int i = 0; i < 1000; ++i) {
            Spring::Step(a, av, 1.0f, 90.0f, 7.0f, 0.0137f);
            Spring::Step(b, bv, 1.0f, 90.0f, 7.0f, 0.0137f);
        }
        CHECK(a == b && av == bv);
    }

    TEST_MAIN_RESULT();
}
