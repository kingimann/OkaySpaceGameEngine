#pragma once
// Walk-versus-run measuring for the built-in blocky character, shared by test_walk_vs_run and
// walk_vs_run_strips. It drives a real Character through DriveLocomotion the way the controllers do (60 frames a
// second, the body moved a little each frame, the speed measured from the position change) and reads what the
// viewer sees: the legs' cadence, how far a step reaches, whether both feet leave the ground (flight) or both
// stand on it (double support), how far the arms swing, how bent the elbows are, how far the chest leans and how
// much the hips bob. Feet are read from the skinned mesh (the lowest vertex of each foot bone), not from any
// gait formula, so the numbers hold whatever formula lays the stride out.
//
// Two ways to pose the body, because the engine has both:
//   animator = true   what a new project gets (Templates::BuildPlayerBody): the Character with the default
//                     Animator graph (HumanoidLibrary::EnsureAnimator); the legs play the baked clip library.
//   animator = false  the Character alone, posed by its own procedural stride (NPCs, scripts, no Animator).
//
// Written from the general facts of human gait (cadence, step length, stance, flight, arm swing); no code or
// table from another project.
#include <Okay.hpp>
#include "okay/Animation/HumanoidLibrary.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace walkrun {
using namespace okay;

constexpr float kDt = 1.0f / 60.0f;

struct GaitStats {
    float speed = 0.0f;          ///< ground speed driven, m/s
    bool  sprint = false;        ///< the run key held
    int   anim = 0;              ///< state at the end: 2 walk, 3 run
    float runStyle = 0.0f;       ///< how much run the procedural stride shows (0 pure walk .. 1 pure run); -1 with an Animator
    float stepsPerMin = 0.0f;    ///< counted from the feet touching the ground
    float phaseSpm = 0.0f;       ///< the gait phase's cadence (procedural only; 0 with an Animator)
    float stepLength = 0.0f;     ///< metres of ground per step = speed / steps per second
    float flight = 0.0f;         ///< fraction of the time both feet are off the ground
    float doubleSupport = 0.0f;  ///< fraction of the time both feet are on it
    float armSwing = 0.0f;       ///< degrees, half the left upper arm's range over the cycle
    float elbow = 0.0f;          ///< degrees, mean elbow bend
    float lean = 0.0f;           ///< degrees, mean forward lean of the chest
    float bounce = 0.0f;         ///< metres, hip height range over the cycle
    float footSlip = 0.0f;       ///< m/s, median ground slip of a planted foot
    float slipP90 = 0.0f;        ///< m/s, the 90th percentile of it
    float lowFoot = 0.0f;        ///< metres, deepest a foot goes below the ground
    bool  ok = false;
};

inline float Median(std::vector<float> v) {
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
inline float Percentile(std::vector<float> v, float q) {
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    return v[(std::size_t)std::fmin((float)v.size() - 1.0f, q * (float)v.size())];
}

/// One character walking straight ahead. Keep it alive to step it further.
struct Walker {
    Scene scene;
    GameObject* go = nullptr;
    Character* ch = nullptr;
    MeshRenderer* mr = nullptr;
    std::vector<int> bone;
    int feet[2] = {-1, -1};
    float walkSpeed = NaturalGait::WalkSpeed(), runSpeed = NaturalGait::RunSpeed();
    float z = 0.0f;
    bool animator = false;

    explicit Walker(bool withAnimator = false, float heightScale = 1.0f) : scene("walk"), animator(withAnimator) {
        scene.physicsEnabled = false;
        go = scene.CreateGameObject("P");
        ch = go->AddComponent<Character>();
        ch->height = heightScale;
        ch->Apply();
        if (withAnimator) HumanoidLibrary::EnsureAnimator(go);
        ch->BuildRig(bone);
        feet[0] = Character::BoneIndex("l_foot");
        feet[1] = Character::BoneIndex("r_foot");
        mr = go->GetComponent<MeshRenderer>();
        scene.Start();
    }
    /// One frame at ground speed `v` (the way a controller steps): move, report, let the Character animate.
    void Step(float v, bool sprint, float dt = kDt) {
        z -= v * dt;
        go->transform->localPosition = {0.0f, 0.0f, z};
        Character::Locomotion lo;
        lo.velocity = {0.0f, 0.0f, -v};
        lo.facing = {0.0f, 0.0f, -1.0f};
        lo.onGround = true;
        lo.sprint = sprint;
        lo.walkSpeed = walkSpeed;
        lo.runSpeed = runSpeed;
        lo.dt = dt;
        ch->DriveLocomotion(lo);
        scene.Update(dt);
    }
    /// Lowest world height of each foot (metres above the ground plane y = 0); false when the mesh is not built.
    bool FootHeights(float out[2]) const {
        if (!mr || mr->mesh.vertices.size() != bone.size()) return false;
        float lo[2] = {1e9f, 1e9f};
        for (std::size_t i = 0; i < bone.size(); ++i)
            for (int k = 0; k < 2; ++k)
                if (bone[i] == feet[k]) lo[k] = std::fmin(lo[k], go->transform->TransformPoint(mr->mesh.vertices[i]).y);
        out[0] = lo[0]; out[1] = lo[1];
        return lo[0] < 1e8f && lo[1] < 1e8f;
    }
    /// The lowest vertex of a foot, in the world.
    bool FootPoint(int k, Vec3& p) const {
        if (!mr || mr->mesh.vertices.size() != bone.size()) return false;
        float lo = 1e9f;
        for (std::size_t i = 0; i < bone.size(); ++i)
            if (bone[i] == feet[k]) {
                Vec3 w = go->transform->TransformPoint(mr->mesh.vertices[i]);
                if (w.y < lo) { lo = w.y; p = w; }
            }
        return lo < 1e8f;
    }
};

/// Walk (or run) at `speed` for `settle` seconds, then record `seconds` of it.
inline GaitStats Measure(bool animator, float speed, bool sprint, float walkSpeed, float runSpeed, float seconds = 8.0f,
                         float settle = 3.0f) {
    Walker w(animator);
    w.walkSpeed = walkSpeed; w.runSpeed = runSpeed;
    GaitStats st; st.speed = speed; st.sprint = sprint;
    const int settleN = (int)(settle / kDt), recN = (int)(seconds / kDt);
    for (int i = 0; i < settleN; ++i) w.Step(speed, sprint);

    // Hysteresis on the ground contact so a foot hovering near the line does not flicker: down below 1.25 cm,
    // up above 2.5 cm.
    bool planted[2] = {false, false};
    float h0[2];
    if (w.FootHeights(h0)) for (int k = 0; k < 2; ++k) planted[k] = h0[k] < 0.0125f;
    int frames = 0, flightN = 0, doubleN = 0;
    std::vector<float> touchdownTimes;
    float cycles = 0.0f, prevPhase = w.ch->GaitPhase();
    float aMin = 1e9f, aMax = -1e9f, elbowSum = 0.0f, leanSum = 0.0f, yMin = 1e9f, yMax = -1e9f;
    std::vector<float> slips;
    Vec3 prevP[2]; bool prevPlanted[2] = {false, false}, havePrev[2] = {false, false};
    float lowest = 0.0f;
    for (int i = 0; i < recN; ++i) {
        w.Step(speed, sprint);
        float h[2];
        if (!w.FootHeights(h)) continue;
        for (int k = 0; k < 2; ++k) {
            const bool was = planted[k];
            if (planted[k] && h[k] > 0.025f) planted[k] = false;
            else if (!planted[k] && h[k] < 0.0125f) planted[k] = true;
            if (planted[k] && !was) touchdownTimes.push_back(i * kDt);
            lowest = std::fmin(lowest, h[k]);
        }
        ++frames;
        if (!planted[0] && !planted[1]) ++flightN;
        if (planted[0] && planted[1]) ++doubleN;
        // The gait phase, unwrapped (the procedural stride only).
        const float ph = w.ch->GaitPhase();
        float d = ph - prevPhase;
        if (d < -0.5f) d += 1.0f; else if (d > 0.5f) d -= 1.0f;
        cycles += d; prevPhase = ph;
        // The pose the viewer sees.
        const std::vector<Vec3> pose = w.ch->CurrentPose();
        if ((int)pose.size() >= HumanoidMotion::B_COUNT) {
            aMin = std::fmin(aMin, pose[HumanoidMotion::B_LUPARM].x);
            aMax = std::fmax(aMax, pose[HumanoidMotion::B_LUPARM].x);
            elbowSum += -pose[HumanoidMotion::B_LFORE].x;
            leanSum += pose[HumanoidMotion::B_TORSO].x + pose[HumanoidMotion::B_HIPS].x;
        }
        const float hy = w.ch->StanceOffset().y;
        yMin = std::fmin(yMin, hy); yMax = std::fmax(yMax, hy);
        // Slip of a planted foot: how far its lowest vertex moved over the ground since last frame.
        for (int k = 0; k < 2; ++k) {
            Vec3 p;
            if (w.FootPoint(k, p)) {
                if (planted[k] && prevPlanted[k] && havePrev[k])
                    slips.push_back(std::hypot(p.x - prevP[k].x, p.z - prevP[k].z) / kDt);
                prevP[k] = p; havePrev[k] = true; prevPlanted[k] = planted[k];
            }
        }
    }
    if (frames < recN / 2 || touchdownTimes.size() < 4) return st;
    const float T = frames * kDt;
    st.anim = w.ch->anim;
    st.runStyle = animator ? -1.0f : w.ch->RunStyle();
    st.stepsPerMin = (float)(touchdownTimes.size() - 1) / (touchdownTimes.back() - touchdownTimes.front()) * 60.0f;
    st.phaseSpm = animator ? 0.0f : std::fabs(cycles) / T * 2.0f * 60.0f;
    st.stepLength = st.stepsPerMin > 1e-3f ? speed / (st.stepsPerMin / 60.0f) : 0.0f;
    st.flight = (float)flightN / frames;
    st.doubleSupport = (float)doubleN / frames;
    st.armSwing = 0.5f * (aMax - aMin);
    st.elbow = elbowSum / frames;
    st.lean = leanSum / frames;
    st.bounce = yMax - yMin;
    st.footSlip = Median(slips);
    st.slipP90 = Percentile(slips, 0.9f);
    st.lowFoot = lowest;
    st.ok = true;
    return st;
}

inline void PrintHeader() {
    std::printf("  %5s %-5s %-5s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s\n", "m/s", "key", "state", "steps", "step", "flight", "double",
                "arm", "elbow", "lean", "bounce", "slip", "style");
    std::printf("  %5s %-5s %-5s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s\n", "", "", "", "/min", "m", "%", "%", "deg", "deg", "deg", "cm", "m/s", "");
}
inline void PrintRow(const GaitStats& s) {
    if (!s.ok) { std::printf("  %5.2f %-5s  (not measured: too few steps)\n", s.speed, s.sprint ? "shift" : "W"); return; }
    std::printf("  %5.2f %-5s %-5s %6.0f %6.2f %6.1f %6.1f %6.1f %6.1f %6.1f %6.1f %6.2f", s.speed, s.sprint ? "shift" : "W",
                s.anim == 3 ? "run" : (s.anim == 2 ? "walk" : "?"), s.stepsPerMin, s.stepLength, s.flight * 100.0f,
                s.doubleSupport * 100.0f, s.armSwing, s.elbow, s.lean, s.bounce * 100.0f, s.footSlip);
    if (s.runStyle >= 0.0f) std::printf(" %6.2f\n", s.runStyle); else std::printf(" %6s\n", "clips");
}

} // namespace walkrun
