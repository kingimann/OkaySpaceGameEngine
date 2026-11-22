#include "test_framework.hpp"
#include "okay/Animation/DynamicCharacter.hpp"
#include "okay/Animation/RagdollPhysics.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

using namespace okay;
using namespace okay::dyn;

// DynamicCharacter (the ActiveRagdoll's brain) on its own, with a hand-made
// 1.8 m rig on flat ground: standing, balance under pushes (stays up below a
// threshold, falls above), recovery time, get-up from face up and face down,
// hostile inputs, a broken skeleton, and a scripted sequence that must give
// the same numbers twice.
namespace {
enum { Hips = 0, Torso, Head, LUpperArm, LForeArm, LHand, RUpperArm, RForeArm, RHand,
       LThigh, LShin, LFoot, RThigh, RShin, RFoot };

RigInput MakeRig() {
    RigInput r;
    const Vec3 p[kBones] = {
        {0, 1.00f, 0}, {0, 1.25f, 0}, {0, 1.56f, 0},
        {0.20f, 1.45f, 0}, {0.21f, 1.17f, 0}, {0.21f, 0.92f, 0},
        {-0.20f, 1.45f, 0}, {-0.21f, 1.17f, 0}, {-0.21f, 0.92f, 0},
        {0.10f, 0.95f, 0}, {0.10f, 0.52f, 0.01f}, {0.10f, 0.09f, 0},
        {-0.10f, 0.95f, 0}, {-0.10f, 0.52f, 0.01f}, {-0.10f, 0.09f, 0},
    };
    for (int b = 0; b < kBones; ++b) { r.pos[b] = p[b]; r.rot[b] = Quat::Identity; }
    r.forward = Vec3{0, 0, 1};
    r.groundY = 0.0f;
    r.mass = 70.0f;
    return r;
}

struct Sim {
    RigInput rig = MakeRig();
    SimpleWorld world;
    DynamicCharacter c;
    float t = 0.0f;
    explicit Sim(RagdollMode m = RagdollMode::Reactive) {
        c.mode = m;
        CHECK(c.Build(rig));
    }
    void Run(float seconds) {
        for (float e = 0.0f; e < seconds - 1e-5f; e += 1.0f / 60.0f) {
            c.SetAnimationPose(rig.pos, rig.rot);
            c.Tick(1.0f / 60.0f, &world);
            t += 1.0f / 60.0f;
        }
    }
    bool AllFinite() {
        for (int b = 0; b < kBones; ++b) {
            const BodyState s = c.Body(b);
            const float v[] = {s.position.x, s.position.y, s.position.z, s.rotation.x, s.rotation.y, s.rotation.z, s.rotation.w};
            for (float x : v) if (!std::isfinite(x)) return false;
            if (std::fabs(s.position.x) > 1e4f || std::fabs(s.position.y) > 1e4f || std::fabs(s.position.z) > 1e4f) return false;
        }
        return true;
    }
    float MaxGap() {
        auto* x = static_cast<XpbdRagdoll*>(c.Physics());
        float g = 0.0f;
        for (int j = 0; j < x->JointCount(); ++j) g = std::fmax(g, x->JointGap(j));
        return g;
    }
    // Lay the body on its back (faceUp) or front, head toward +Z / -Z.
    void LayDown(bool faceUp) {
        c.Push(c.Body(Hips).position, Vec3::Zero);   // physics on
        const Quat turn = Quat::AngleAxis(faceUp ? -90.0f : 90.0f, Vec3::Right);
        const Vec3 pivot{0, 0, 0};
        for (int b = 0; b < kBones; ++b) {
            BodyState s = c.Physics()->State(b);
            s.position = pivot + turn * (s.position - pivot) + Vec3::Up * 0.16f;
            s.rotation = (turn * s.rotation).Normalized();
            s.velocity = s.angularVelocity = Vec3::Zero;
            c.Physics()->SetState(b, s);
        }
        c.ChangeState(CharState::Down);
    }
};

const char* S(CharState s) { return StateName(s); }
}

int main() {
    RUN_SUITE("active_ragdoll");

    // ---- Build: a good rig builds, broken ones are refused ----
    {
        DynamicCharacter c;
        CHECK(c.Build(MakeRig()));
        CHECK(c.Physics()->BodyCount() == 15);
        CHECK(c.Physics()->JointCount() == 14);
        CHECK(std::fabs(c.Height() - 1.79f) < 0.05f);
        RigInput bad = MakeRig();
        bad.pos[LShin] = bad.pos[LThigh];                         // a zero-length thigh
        CHECK(!c.Build(bad));
        CHECK(!c.Built());
        bad = MakeRig();
        bad.pos[Head].y = std::numeric_limits<float>::quiet_NaN();
        CHECK(!c.Build(bad));
        bad = MakeRig();
        for (int b = 0; b < kBones; ++b) bad.pos[b] = Vec3::Zero; // everything on one point
        CHECK(!c.Build(bad));
        bad = MakeRig();
        bad.rot[Torso] = Quat{0, 0, 0, 0};                       // no rotation at all
        CHECK(!c.Build(bad));
        // Calls on an unbuilt character do nothing and do not crash.
        c.Push(Vec3::Zero, Vec3{1, 0, 0});
        c.Explosion(Vec3::Zero, 100.0f, 2.0f);
        c.Tick(1.0f / 60.0f, nullptr);
        CHECK(c.State() == CharState::Animated);
    }

    // ---- Standing: powered, no push, stays up on its own for 4 s ----
    {
        Sim s(RagdollMode::Powered);
        s.Run(4.0f);
        std::cout << "  powered stand: state " << S(s.c.State()) << " pelvis " << s.c.PelvisHeight()
                  << " / " << s.c.StandingHeight() << " lean " << s.c.TorsoLean() << "\n";
        CHECK(s.c.State() == CharState::Balancing);
        CHECK(s.c.PelvisHeight() > 0.9f * s.c.StandingHeight());
        CHECK(s.c.TorsoLean() < 10.0f);
        CHECK(s.c.Falls() == 0);
        CHECK(s.MaxGap() < 0.02f);
    }

    // ---- A small push: stays up, steps if it must, back to the animation ----
    float recovery = -1.0f;
    {
        Sim s;
        s.Run(0.5f);
        CHECK(s.c.State() == CharState::Animated);
        s.c.Push(s.c.Body(Torso).position, Vec3{0, 0, 35.0f});   // 35 N s at the chest (0.5 m/s for 70 kg)
        CHECK(s.c.State() == CharState::Balancing);
        float t = 0.0f;
        while (t < 6.0f && s.c.State() != CharState::Animated) { s.Run(1.0f / 60.0f); t += 1.0f / 60.0f; }
        recovery = s.c.LastRecoveryTime();
        std::cout << "  small push: state " << S(s.c.State()) << " falls " << s.c.Falls() << " steps "
                  << s.c.StepsTaken() << " recovery " << recovery << " s\n";
        CHECK(s.c.Falls() == 0);
        CHECK(s.c.State() == CharState::Animated);
        CHECK(recovery > 0.0f && recovery < 3.0f);
    }

    // ---- A push that needs a step: still stays up ----
    {
        Sim s;
        s.Run(0.2f);
        s.c.Push(s.c.Body(Torso).position, Vec3{0, 0, 70.0f});   // 1 m/s
        int maxSteps = 0;
        for (int i = 0; i < 360 && s.c.State() != CharState::Animated; ++i) {
            s.Run(1.0f / 60.0f);
            maxSteps = std::max(maxSteps, s.c.StepsTaken());
        }
        std::cout << "  step push: state " << S(s.c.State()) << " falls " << s.c.Falls() << " steps " << maxSteps << "\n";
        CHECK(s.c.Falls() == 0);
        CHECK(maxSteps >= 1);
        CHECK(s.c.State() == CharState::Animated);
    }

    // ---- A big push: falls, lands, lies down ----
    {
        Sim s;
        s.Run(0.2f);
        s.c.Push(s.c.Body(Torso).position, Vec3{0, 0, 400.0f});
        s.Run(2.5f);
        std::cout << "  big push: state " << S(s.c.State()) << " falls " << s.c.Falls() << " pelvis "
                  << s.c.PelvisHeight() << "\n";
        CHECK(s.c.Falls() == 1);
        CHECK(s.c.State() == CharState::Down || s.c.State() == CharState::GettingUp);
        CHECK(s.c.Body(Hips).position.y < 0.5f);
        CHECK(s.MaxGap() < 0.03f);
        float lowest = 1e9f;
        for (int b = 0; b < kBones; ++b) lowest = std::fmin(lowest, s.c.Body(b).position.y - s.c.BodyRadius(b));
        CHECK(lowest > -0.05f);                                   // nothing sank into the floor
    }

    // ---- Get up from lying face up and face down ----
    for (int faceUp = 0; faceUp < 2; ++faceUp) {
        Sim s;
        s.Run(0.1f);
        s.LayDown(faceUp == 1);
        float t = 0.0f;
        bool sawGettingUp = false;
        while (t < 10.0f && s.c.State() != CharState::Animated) {
            s.Run(1.0f / 60.0f); t += 1.0f / 60.0f;
            sawGettingUp = sawGettingUp || s.c.State() == CharState::GettingUp;
        }
        std::cout << "  get up " << (faceUp ? "face up" : "face down") << ": state " << S(s.c.State())
                  << " getUps " << s.c.GetUps() << " faceDown " << s.c.FaceDown() << " time " << t << "\n";
        CHECK(sawGettingUp);
        CHECK(s.c.FaceDown() == (faceUp == 0));
        CHECK(s.c.GetUps() >= 1);
        CHECK(s.c.State() == CharState::Animated);
    }

    // ---- Brace: falling forward, the hands reach the ground before the head ----
    {
        Sim s;
        s.Run(0.2f);
        s.c.Push(s.c.Body(Torso).position, Vec3{0, 0, 250.0f});
        float handT = -1.0f, headT = -1.0f, t = 0.0f;
        while (t < 3.0f) {
            s.Run(1.0f / 60.0f); t += 1.0f / 60.0f;
            if (handT < 0.0f && (s.c.BodyTouching(LHand) || s.c.BodyTouching(RHand) || s.c.BodyTouching(LForeArm) || s.c.BodyTouching(RForeArm))) handT = t;
            if (headT < 0.0f && s.c.BodyTouching(Head)) headT = t;
        }
        std::cout << "  brace: hands down at " << handT << " s, head at " << headT << " s\n";
        CHECK(handT > 0.0f);
        CHECK(headT < 0.0f || handT < headT);
    }

    // ---- Hostile inputs: no NaN, no explosion ----
    {
        Sim s;
        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        s.Run(0.2f);
        s.c.Push(Vec3{nan, 0, 0}, Vec3{1, 1, 1});
        s.c.Push(Vec3::Zero, Vec3{inf, 0, 0});
        s.c.Push(s.c.Body(Head).position, Vec3{1e12f, -1e12f, 3e11f});   // absurd
        s.c.Shot(s.c.Body(LShin).position, Vec3{0, 0, 0}, 1e30f);
        s.c.Explosion(s.c.Body(Hips).position, 1e9f, 5.0f);
        s.c.Explosion(Vec3::Zero, nan, 2.0f);
        s.c.Explosion(Vec3::Zero, 10.0f, -3.0f);
        Vec3 badPos[kBones]; Quat badRot[kBones];
        for (int b = 0; b < kBones; ++b) { badPos[b] = Vec3{nan, nan, nan}; badRot[b] = Quat{0, 0, 0, 0}; }
        for (int i = 0; i < 120; ++i) {
            s.c.SetAnimationPose(badPos, badRot);                         // ignored
            s.c.Tick(i % 7 == 0 ? 10.0f : (i % 5 == 0 ? -1.0f : 1.0f / 60.0f), &s.world);
        }
        s.c.Tick(nan, &s.world);
        s.Run(3.0f);
        std::cout << "  hostile: finite " << s.AllFinite() << " resets " << s.c.Resets() << " gap " << s.MaxGap()
                  << " state " << S(s.c.State()) << "\n";
        CHECK(s.AllFinite());
        CHECK(s.MaxGap() < 0.1f);
        for (int b = 0; b < kBones; ++b) CHECK(s.c.Body(b).velocity.Magnitude() <= 40.0f + 1e-3f);
        // Zero and negative mass are clamped, not divided by.
        RigInput r = MakeRig();
        r.mass = 0.0f;
        DynamicCharacter z;
        CHECK(z.Build(r));
        z.Push(z.Body(Torso).position, Vec3{0, 0, 5});
        for (int i = 0; i < 120; ++i) { z.SetAnimationPose(r.pos, r.rot); z.Tick(1.0f / 60.0f, &s.world); }
        bool fin = true;
        for (int b = 0; b < kBones; ++b) { const Vec3 p = z.Body(b).position; fin = fin && std::isfinite(p.x + p.y + p.z); }
        CHECK(fin);
        r.mass = -50.0f;
        CHECK(z.Build(r));
        // No world at all: it just falls, finite.
        Sim nw;
        nw.c.Knockdown();
        for (int i = 0; i < 60; ++i) { nw.c.SetAnimationPose(nw.rig.pos, nw.rig.rot); nw.c.Tick(1.0f / 60.0f, nullptr); }
        CHECK(nw.AllFinite());
        CHECK(nw.c.Body(Hips).position.y < 0.0f);
    }

    // ---- Determinism: the same scripted sequence twice gives the same bits ----
    {
        auto script = [](Sim& s) {
            for (int i = 0; i < 480; ++i) {
                if (i == 30) s.c.Push(s.c.Body(Torso).position + Vec3{0.05f, 0, 0}, Vec3{10.0f, 0, 50.0f});
                if (i == 90) s.c.Shot(s.c.Body(RShin).position, Vec3{-1, 0, 0.2f}, 25.0f);
                if (i == 150) s.c.Explosion(s.c.Body(Hips).position + Vec3{0.5f, -0.5f, -0.3f}, 900.0f, 3.0f);
                s.Run(1.0f / 60.0f);
            }
        };
        Sim a, b;
        script(a);
        script(b);
        bool same = true;
        for (int i = 0; i < kBones; ++i) {
            const BodyState x = a.c.Body(i), y = b.c.Body(i);
            same = same && std::memcmp(&x.position, &y.position, sizeof(Vec3)) == 0
                        && std::memcmp(&x.rotation, &y.rotation, sizeof(Quat)) == 0;
        }
        std::cout << "  determinism: same " << same << " state " << S(a.c.State()) << " falls " << a.c.Falls() << "\n";
        CHECK(same);
        CHECK(a.c.State() == b.c.State());
        CHECK(a.c.Falls() >= 1);
    }

    // ---- Shots: a ray finds the struck body; a leg shot buckles it ----
    {
        Sim s;
        s.Run(0.2f);
        Vec3 hit;
        const int b = s.c.Raycast(Vec3{0, 1.35f, -3.0f}, Vec3{0, 0, 1}, 10.0f, &hit);
        CHECK(b == Torso);
        CHECK(std::fabs(hit.z - (-0.13f)) < 0.06f);
        CHECK(s.c.Raycast(Vec3{3, 1.35f, -3.0f}, Vec3{0, 0, 1}, 10.0f) == -1);
        s.c.Shot(s.c.Body(LThigh).position, Vec3{0, 0, 1}, 20.0f);
        CHECK(s.c.Relax(LShin) > 0.9f);
        CHECK(s.c.State() == CharState::Balancing);
    }

    // ---- Death goes limp and stays down; revive gets up ----
    {
        Sim s;
        s.Run(0.2f);
        s.c.Die();
        s.Run(3.0f);
        CHECK(s.c.State() == CharState::Dead);
        CHECK(s.c.Body(Hips).position.y < 0.5f);
        s.c.Revive();
        float t = 0.0f;
        while (t < 10.0f && s.c.State() != CharState::Animated) { s.Run(1.0f / 60.0f); t += 1.0f / 60.0f; }
        CHECK(s.c.State() == CharState::Animated);
    }
    std::cout << "  recovery time after a 35 N s push: " << recovery << " s\n";
    TEST_MAIN_RESULT();
}
