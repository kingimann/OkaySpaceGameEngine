// Walk dynamics: the built-in character driven headless through DriveLocomotion like a controller (fake input).
//   1. Every bone is a smooth curve over the stride at every walk and run speed. Before the fix the knees of a run
//      (and of a walk faster than 2.4 m/s) snapped 3 to 7 degrees within 1/4000 of a cycle at every foot lift-off,
//      a visible kink once per step (the hip-height test in test_gait_continuity only covered the hips, 1 cm slack).
//   2. Pressed against a wall (the controller wants to move, the body does not) the body idles; stopping dead goes
//      to idle at once and the planted foot does not skate.
//   3. A planted foot stays put on the ground at steady speeds, and while strafing and backpedalling.
// Written from the shape of the stride (docs/gameplay/walk_vs_run.md); no code from another project.
#include "test_framework.hpp"
#include "walk_vs_run_measure.hpp"
#include <cstdio>
using namespace walkrun;

namespace {
struct Probe {
    Walker w;
    float x = 0, z = 0;
    explicit Probe(bool animator) : w(animator) {}
    // One frame: the body really moves by `move` (m/s, world), the controller wants `want`, facing -Z.
    void Step(Vec3 move, Vec3 want, bool sprint = false) {
        x += move.x * kDt; z += move.z * kDt;
        w.go->transform->localPosition = {x, 0.0f, z};
        Character::Locomotion lo;
        lo.velocity = want; lo.facing = {0, 0, -1}; lo.onGround = true; lo.sprint = sprint;
        lo.walkSpeed = w.walkSpeed; lo.runSpeed = w.runSpeed; lo.dt = kDt;
        w.ch->DriveLocomotion(lo);
        w.scene.Update(kDt);
    }
};

// Slip of a planted foot measured on ONE vertex held from touchdown (the lowest-vertex switch between toe and heel is
// not slip): m/s samples for the frames where the foot was planted this frame and the one before.
struct SlipTracker {
    int vert[2] = {-1, -1}; Vec3 prev[2]; bool was[2] = {false, false};
    void Frame(Walker& w, std::vector<float>& out) {
        float h[2]; if (!w.FootHeights(h)) return;
        auto at = [&](int k) { return w.go->transform->TransformPoint(w.mr->mesh.vertices[vert[k]]); };
        for (int k = 0; k < 2; ++k) {
            const bool pl = h[k] < 0.02f;
            if (pl && !was[k]) {   // touchdown: pick the lowest vertex now
                float lo = 1e9f; vert[k] = -1;
                for (std::size_t i = 0; i < w.bone.size(); ++i)
                    if (w.bone[i] == w.feet[k]) {
                        float y = w.go->transform->TransformPoint(w.mr->mesh.vertices[i]).y;
                        if (y < lo) { lo = y; vert[k] = (int)i; }
                    }
            } else if (pl && was[k] && vert[k] >= 0) {
                Vec3 p = at(k);
                out.push_back(std::hypot(p.x - prev[k].x, p.z - prev[k].z) / kDt);
            }
            if (pl && vert[k] >= 0) prev[k] = at(k);
            was[k] = pl;
        }
    }
};
}

int main() {
    RUN_SUITE("walk_dynamics");
    {   // 1. continuity of every bone over the stride, forwards and backpedalling
        const int N = 4000;
        for (float v : {0.5f, 1.0f, 1.5f, 2.0f, 2.4f, 3.0f, 4.5f, 5.5f, 8.0f})
            for (int back = 0; back < 2; ++back) {
                HumanoidMotion::Inputs in{};
                in.speed = v; in.runW = HumanoidMotion::JogWeight(v);
                const int state = in.runW >= 0.5f ? 3 : 2;
                std::vector<Vec3> a(HumanoidMotion::B_COUNT), b(HumanoidMotion::B_COUNT);
                float worst = 0.0f; int wb = -1;
                for (int i = 0; i <= N; ++i) {
                    in.phase = back ? 1.0f - (float)i / N : (float)i / N;
                    HumanoidMotion::Pose(state, 0.0f, in, b);
                    if (i > 0)
                        for (int k = 0; k < HumanoidMotion::B_COUNT; ++k) {
                            const float d = std::fmax(std::fabs(b[k].x - a[k].x),
                                                      std::fmax(std::fabs(b[k].y - a[k].y), std::fabs(b[k].z - a[k].z)));
                            if (d > worst) { worst = d; wb = k; }
                        }
                    a = b;
                }
                std::printf("  v=%.1f %s: biggest bone step per 1/%d cycle %.3f deg (bone %d)\n", v, back ? "back" : "fwd", N, worst, wb);
                CHECK(worst < 1.0f);   // a smooth curve is under 0.6; the snap was 3 to 7
            }
    }
    for (int animator = 0; animator < 2; ++animator) {
        std::printf("== %s\n", animator ? "Animator" : "procedural");
        {   // 2a. against a wall: wants 2 m/s, the body does not move
            Probe p(animator);
            for (int i = 0; i < 120; ++i) p.Step({0, 0, 0}, {0, 0, -2});
            std::printf("  wall: anim=%d moveSpeed=%.3f\n", p.w.ch->anim, p.w.ch->moveSpeed);
            if (!animator) CHECK(p.w.ch->anim == 1);
            CHECK(p.w.ch->moveSpeed < 0.05f);
        }
        {   // 2b. walking, then stopping dead: idle on the first still frame, no skating on the feet
            Probe p(animator); SlipTracker t; std::vector<float> s;
            for (int i = 0; i < 180; ++i) p.Step({0, 0, -2}, {0, 0, -2});
            p.Step({0, 0, 0}, {0, 0, 0});
            if (!animator) CHECK(p.w.ch->anim == 1);
            for (int i = 0; i < 90; ++i) { p.Step({0, 0, 0}, {0, 0, 0}); t.Frame(p.w, s); }
            std::printf("  stop dead: planted slip median %.3f m/s (n=%zu)\n", Median(s), s.size());
            CHECK(Median(s) < 0.1f);
        }
        // 3. a planted foot stays put (median slip a small share of the ground speed) at steady speeds
        for (float v : {1.0f, 2.0f, 3.0f, 5.5f}) {
            Probe p(animator); SlipTracker t; std::vector<float> s;
            for (int i = 0; i < 180; ++i) p.Step({0, 0, -v}, {0, 0, -v}, v > 2.3f);
            for (int i = 0; i < 300; ++i) { p.Step({0, 0, -v}, {0, 0, -v}, v > 2.3f); t.Frame(p.w, s); }
            std::printf("  steady %.1f: planted slip median %.3f m/s (n=%zu)\n", v, Median(s), s.size());
            CHECK(s.size() > 100);
            CHECK(Median(s) < 0.1f * v + 0.1f);
        }
        for (float ang : {90.0f, -90.0f, 180.0f, 135.0f}) {   // strafing and backpedalling at 1.4 m/s
            Probe p(animator); SlipTracker t; std::vector<float> s;
            const float a = ang * 3.14159265f / 180.0f;
            const Vec3 d{std::sin(a), 0, -std::cos(a)};
            for (int i = 0; i < 180; ++i) p.Step(d * 1.4f, d * 1.4f);
            for (int i = 0; i < 300; ++i) { p.Step(d * 1.4f, d * 1.4f); t.Frame(p.w, s); }
            std::printf("  dir %.0f at 1.4: planted slip median %.3f m/s (n=%zu)\n", ang, Median(s), s.size());
            CHECK(s.size() > 100);
            CHECK(Median(s) < 0.25f);
        }
    }
    TEST_MAIN_RESULT();
}
