#include "test_framework.hpp"
#include "okay/Animation/DynamicCharacter.hpp"
#include "okay/Animation/RagdollPhysics.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
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

// The example scene (examples/active_ragdoll/active_ragdoll.okayscene) is made here, so the shipped file
// and the checks below agree. OKAY_WRITE_ACTIVE_RAGDOLL_EXAMPLE=1 writes it into the source tree.
const char* kExampleScript =
    "// Active Ragdoll example. Keys: J / L push left / right, U / O shove hard (O knocks it off the ledge),\n"
    "// I push it back, F shoot (each shot hits another spot), E blast, T trip, G get up, K kill, R revive,\n"
    "// P powered on / off (always physical, following the animation with its muscles).\n"
    "var shots = 0\n"
    "var powered = 0\n"
    "function update(dt) {\n"
    "  if (key_down(\"j\")) { active_ragdoll_push(-40, 0, 0) }\n"
    "  if (key_down(\"l\")) { active_ragdoll_push(40, 0, 0) }\n"
    "  if (key_down(\"u\")) { active_ragdoll_push(-110, 0, 0) }\n"
    "  if (key_down(\"o\")) { active_ragdoll_push(170, 0, 0) }\n"
    "  if (key_down(\"i\")) { active_ragdoll_push(0, 0, -45) }\n"
    "  if (key_down(\"f\")) {\n"
    "    var y = 1.35\n"
    "    if (shots == 1) { y = 0.55 }\n"
    "    if (shots == 2) { y = 1.7 }\n"
    "    if (shots == 3) { y = 1.05 }\n"
    "    active_ragdoll_shoot(0, 2.6, 6.5, pos_x(), pos_y() + y - 2.6, pos_z() - 6.5, 30)\n"
    "    shots = shots + 1\n"
    "    if (shots > 3) { shots = 0 }\n"
    "  }\n"
    "  if (key_down(\"e\")) { active_ragdoll_explode(pos_x() + 0.6, pos_y(), pos_z() + 0.8, 900, 3) }\n"
    "  if (key_down(\"t\")) { active_ragdoll(\"knockdown\") }\n"
    "  if (key_down(\"g\")) { active_ragdoll(\"getup\") }\n"
    "  if (key_down(\"k\")) { active_ragdoll(\"die\") }\n"
    "  if (key_down(\"r\")) { active_ragdoll(\"revive\") }\n"
    "  if (key_down(\"p\")) {\n"
    "    powered = 1 - powered\n"
    "    if (powered == 1) { active_ragdoll(\"powered\") } else { active_ragdoll(\"reactive\") }\n"
    "  }\n"
    "  set_text(\"Active Ragdoll: \" + active_ragdoll_state() + \"     J / L push   U / O shove (O: off the ledge)   I push back   F shoot   E blast   T trip   G get up   K kill   R revive   P powered\")\n"
    "}\n";

void BuildExample(Scene& s) {
    auto box = [&](const char* name, Vec3 pos, Vec3 scale, Color col) {
        GameObject* g = s.CreateGameObject(name);
        g->transform->localPosition = pos;
        g->transform->localScale = scale;
        auto* mr = g->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube(1.0f);
        mr->color = col;
        g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        return g;
    };
    GameObject* sun = s.CreateGameObject("Sun");
    sun->transform->localRotation = Quat::Euler(50.0f, -30.0f, 0.0f);
    sun->AddComponent<Light>()->intensity = 1.1f;
    GameObject* cam = s.CreateGameObject("Main Camera");
    cam->transform->localPosition = {0.0f, 2.6f, 6.5f};
    cam->transform->localRotation = Quat::AngleAxis(-12.0f, Vec3::Right);
    cam->AddComponent<Camera>();
    box("Ground", {0, -0.5f, 0}, {40, 1, 40}, Color::FromBytes(92, 130, 80));
    box("Platform (the ledge is on the right)", {0, 0.5f, 0}, {3.2f, 1, 2.4f}, Color::FromBytes(150, 150, 155));
    box("Wall (it can grab this)", {-1.0f, 2.0f, 0}, {0.3f, 2, 2.4f}, Color::FromBytes(160, 90, 70));
    GameObject* d = s.CreateGameObject("Dummy");
    d->transform->localPosition = {0.0f, 1.0f, 0.0f};
    d->transform->localRotation = Quat::AngleAxis(180.0f, Vec3::Up);   // face the camera
    d->AddComponent<Character>()->Apply();
    d->AddComponent<HealthStat>();
    d->AddComponent<ActiveRagdoll>();
    auto* tr = d->AddComponent<TextRenderer>();
    tr->screenSpace = true;
    tr->screenPos = {12.0f, 12.0f};
    tr->size = {1200.0f, 24.0f};
    tr->pixelSize = 1.5f;
    tr->text = "Active Ragdoll";
    auto* sc = d->AddComponent<ScriptComponent>();
    sc->SetSource(kExampleScript);
}
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

    // ---- The threshold: up to 50 N s (0.7 m/s) it stays up in every direction, at 200 N s (2.9 m/s) it falls ----
    {
        const Vec3 dirs[4] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}};
        for (float push : {35.0f, 50.0f, 200.0f}) {
            int fell = 0, upAgain = 0;
            for (const Vec3& d : dirs) {
                Sim s;
                s.Run(0.2f);
                s.c.Push(s.c.Body(Torso).position, d * push);
                for (int i = 0; i < 720 && !(s.c.State() == CharState::Animated && i > 30); ++i) s.Run(1.0f / 60.0f);
                fell += s.c.Falls() > 0 ? 1 : 0;
                upAgain += (s.c.Falls() > 0 && s.c.GetUps() == 1 && s.c.State() == CharState::Animated) ? 1 : 0;
            }
            std::cout << "  threshold: " << push << " N s, fell in " << fell << " of 4 directions, got up " << upAgain << "\n";
            CHECK(push < 100.0f ? fell == 0 : fell == 4);
            CHECK(upAgain == fell);                               // every fall ends standing again within 12 s
        }
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
        float lowestPelvis = 9.0f;
        for (int i = 0; i < 150; ++i) { s.Run(1.0f / 60.0f); lowestPelvis = std::fmin(lowestPelvis, s.c.Body(Hips).position.y); }
        std::cout << "  big push: state " << S(s.c.State()) << " falls " << s.c.Falls() << " lowest pelvis "
                  << lowestPelvis << "\n";
        CHECK(s.c.Falls() == 1);
        CHECK(s.c.State() == CharState::Down || s.c.State() == CharState::GettingUp);
        CHECK(lowestPelvis < 0.4f);
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

    // ---- Protect head: falling backwards, the hands come up beside the head ----
    {
        Sim s;
        s.Run(0.2f);
        s.c.Push(s.c.Body(Torso).position, Vec3{0, 0, -250.0f});   // the rig faces +Z: this is backwards
        float closest = 9.0f;
        for (int i = 0; i < 180; ++i) {
            s.Run(1.0f / 60.0f);
            if (s.c.State() != CharState::Falling) continue;
            closest = std::fmin(closest, std::fmax(Vec3::Distance(s.c.Body(LHand).position, s.c.Body(Head).position),
                                                   Vec3::Distance(s.c.Body(RHand).position, s.c.Body(Head).position)));
        }
        std::cout << "  protect head: both hands within " << closest << " m of the head while falling\n";
        CHECK(s.c.Falls() == 1);
        CHECK(closest < 0.45f);                                   // standing, they hang 0.85 m away
    }

    // ---- Grab: stumbling toward a wall, a hand grips it ----
    {
        Sim s;
        s.world.boxes.push_back({Vec3{-1.0f, 0.0f, -1.0f}, Vec3{-0.6f, 2.5f, 1.0f}});   // a wall 0.4 m from the right shoulder
        s.Run(0.2f);
        s.c.Push(s.c.Body(Torso).position, Vec3{-90.0f, 0, 0});
        bool gripped = false;
        for (int i = 0; i < 240; ++i) { s.Run(1.0f / 60.0f); gripped = gripped || s.c.Grabbing(); }
        std::cout << "  grab: gripped the wall " << gripped << ", state " << S(s.c.State()) << "\n";
        CHECK(gripped);
        CHECK(!s.c.Grabbing() || s.c.State() == CharState::Balancing || s.c.State() == CharState::Falling);
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

    // ---- The component on a real Character in a scene ----
    {
        Scene sc{"ar"};
        auto* g = sc.CreateGameObject("Ground");
        g->transform->localPosition = {0, -0.5f, 0};
        g->AddComponent<BoxCollider3D>()->size = {40, 1, 40};
        GameObject* body = sc.CreateGameObject("Body");
        auto* ch = body->AddComponent<Character>();
        ch->Apply();
        auto* hp = body->AddComponent<HealthStat>();
        auto* npc = body->AddComponent<NPCController>();
        npc->behavior = (int)NPCController::Behavior::Idle;
        auto* ar = body->AddComponent<ActiveRagdoll>();
        int changes = 0;
        ar->onStateChanged = [&](dyn::CharState, dyn::CharState) { ++changes; };
        sc.Start();
        auto run = [&](float sec) { for (float e = 0; e < sec - 1e-5f; e += 1.0f / 60.0f) sc.Update(1.0f / 60.0f); };
        run(0.5f);
        CHECK(ar->Ready());
        CHECK(ar->State() == CharState::Animated);
        CHECK(ar->Core().Height() > 1.0f && ar->Core().Height() < 2.5f);
        std::cout << "  scene: height " << ar->Core().Height() << " pelvis " << ar->Core().StandingHeight()
                  << " ground " << ar->Core().GroundY() << "\n";
        // A shove: physical, the controller handed over, then back to the animation and the controller.
        ar->Push(Vec3{0, 0, -35.0f});   // the blocky Character faces -Z: a push from behind
        run(1.0f / 60.0f);
        CHECK(ar->IsPhysical());
        CHECK(!npc->enabled);
        float t = 0.0f;
        while (t < 6.0f && ar->State() != CharState::Animated) { run(1.0f / 60.0f); t += 1.0f / 60.0f; }
        std::cout << "  scene shove: state " << ar->StateName() << " falls " << ar->Core().Falls() << " after " << t << " s\n";
        CHECK(ar->State() == CharState::Animated);
        CHECK(npc->enabled);
        CHECK(changes >= 2);
        // A hard shove: down, up again, the object moved under the body.
        const Vec3 root0 = body->transform->Position();
        ar->Push(Vec3{0, 0, 400.0f});
        t = 0.0f;
        bool wasDown = false;
        while (t < 12.0f && !(wasDown && ar->State() == CharState::Animated)) {
            run(1.0f / 60.0f); t += 1.0f / 60.0f;
            wasDown = wasDown || ar->State() == CharState::Down;
            CHECK(ar->State() == CharState::Animated || ar->State() == CharState::Blending || !npc->enabled);
        }
        std::cout << "  scene knockdown: state " << ar->StateName() << " getUps " << ar->Core().GetUps()
                  << " moved " << Vec3::Distance(root0, body->transform->Position()) << " m after " << t << " s\n";
        CHECK(wasDown);
        CHECK(ar->State() == CharState::Animated);
        CHECK(Vec3::Distance(root0, body->transform->Position()) > 0.2f);
        CHECK(npc->enabled);
        // Damage pushes, death goes limp, revive gets up.
        hp->Damage(20.0f, body->transform->Position() + Vec3{-3, 1.2f, 0});
        run(1.0f / 60.0f);
        CHECK(ar->IsPhysical());
        hp->Damage(1000.0f, body->transform->Position() + Vec3{-3, 1.2f, 0});
        run(2.0f);
        CHECK(ar->State() == CharState::Dead);
        hp->Revive();
        t = 0.0f;
        while (t < 10.0f && ar->State() != CharState::Animated) { run(1.0f / 60.0f); t += 1.0f / 60.0f; }
        CHECK(ar->State() == CharState::Animated);
        // Scripts reach it by name.
        CHECK(ar->Trigger("knockdown"));
        run(0.1f);
        CHECK(ar->State() == CharState::Falling || ar->State() == CharState::Down);
        CHECK(!ar->Trigger("dance"));
        // Shooting: a ray from the front hits the body.
        run(8.0f);
        const Vec3 chest = ar->Core().Body(1).position;
        CHECK(ar->Shoot(chest + Vec3{0, 0, -5}, Vec3{0, 0, 1}, 20.0f));
        CHECK(!ar->Shoot(chest + Vec3{5, 0, -5}, Vec3{0, 0, 1}, 20.0f));
    }

    // ---- A Ragdoll and an ActiveRagdoll on one object: the ActiveRagdoll takes over, only one reacts ----
    {
        Scene sc{"ar3"};
        auto* g = sc.CreateGameObject("Ground");
        g->transform->localPosition = {0, -0.5f, 0};
        g->AddComponent<BoxCollider3D>()->size = {40, 1, 40};
        GameObject* body = sc.CreateGameObject("Body");
        body->AddComponent<Character>()->Apply();
        auto* hp = body->AddComponent<HealthStat>();
        auto* old = body->AddComponent<Ragdoll>();
        auto* ar = body->AddComponent<ActiveRagdoll>();
        sc.Start();
        for (int i = 0; i < 30; ++i) sc.Update(1.0f / 60.0f);
        CHECK(ar->Ready());
        CHECK(!old->enabled);                                     // switched off, with a log line
        CHECK(ar->TookOverRagdoll());
        hp->Damage(10.0f, body->transform->Position() + Vec3{-3, 1.2f, 0});
        sc.Update(1.0f / 60.0f);
        CHECK(!old->IsReacting());                                // only the ActiveRagdoll flinches
        CHECK(ar->IsPhysical());
        hp->Damage(1000.0f, body->transform->Position() + Vec3{-3, 1.2f, 0});
        for (int i = 0; i < 60; ++i) sc.Update(1.0f / 60.0f);
        CHECK(!old->IsActive());                                  // the old ragdoll did not go limp too
        CHECK(ar->State() == CharState::Dead);
        CHECK(body->GetComponent<Character>()->enabled);          // nothing switched the animation off under it
    }

    // ---- A broken skeleton in a scene: the component stays off, nothing crashes ----
    {
        Scene sc{"ar2"};
        GameObject* body = sc.CreateGameObject("NotAHumanoid");
        auto* ar = body->AddComponent<ActiveRagdoll>();
        sc.Start();
        for (int i = 0; i < 30; ++i) sc.Update(1.0f / 60.0f);
        CHECK(!ar->Ready());
        ar->Push(Vec3{0, 0, 100});
        CHECK(!ar->Shoot(Vec3::Zero, Vec3{0, 0, 1}, 10.0f));
        CHECK(!ar->Trigger("knockdown"));
    }

    // ---- The example scene: the shipped file is the one built above, and it does what it says ----
    {
        const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/active_ragdoll/active_ragdoll.okayscene";
        if (std::getenv("OKAY_WRITE_ACTIVE_RAGDOLL_EXAMPLE")) {
            Scene b{"Active Ragdoll"};
            BuildExample(b);
            std::string err;
            CHECK(SceneSerializer::SaveToFile(b, path, &err));
            std::cout << "  wrote " << path << " " << err << "\n";
        }
        Scene sc{"example"};
        std::string err;
        CHECK(SceneSerializer::LoadFromFile(sc, path, &err));
        GameObject* d = sc.Find("Dummy");
        CHECK(d != nullptr);
        auto* ar = d ? d->GetComponent<ActiveRagdoll>() : nullptr;
        CHECK(ar != nullptr);
        CHECK(d && d->GetComponent<ScriptComponent>() != nullptr);
        if (ar) {
            sc.Start();
            auto run = [&](float sec) { for (float e = 0; e < sec - 1e-5f; e += 1.0f / 60.0f) sc.Update(1.0f / 60.0f); };
            run(0.5f);
            CHECK(ar->Ready());
            CHECK(ar->State() == CharState::Animated);
            // J: a small push toward the wall. Stays up.
            ar->Push(Vec3{-40, 0, 0});
            float t = 0.0f;
            while (t < 6.0f && ar->State() != CharState::Animated) { run(1.0f / 60.0f); t += 1.0f / 60.0f; }
            std::cout << "  example J: " << ar->StateName() << " falls " << ar->Core().Falls() << " grabbed " << ar->Core().Grabbing() << "\n";
            CHECK(ar->Core().Falls() == 0);
            // F: a shot from the camera hits the chest.
            const Vec3 o{0, 2.6f, 6.5f};
            const Vec3 at = d->transform->Position() + Vec3{0, 1.35f, 0};
            CHECK(ar->Shoot(o, at - o, 30.0f));
            t = 0.0f;
            run(0.2f);
            while (t < 12.0f && ar->State() != CharState::Animated) { run(1.0f / 60.0f); t += 1.0f / 60.0f; }
            CHECK(ar->State() == CharState::Animated);
            run(0.5f);
            // O: a hard shove to the right, off the ledge onto the ground, and up again down there.
            const int fallsBefore = ar->Core().Falls();
            ar->Push(Vec3{170, 0, 0});
            t = 0.0f;
            float lowest = 9.0f;
            while (t < 15.0f) {
                run(1.0f / 60.0f); t += 1.0f / 60.0f;
                lowest = std::fmin(lowest, ar->Core().Body(0).position.y);
                if (ar->State() == CharState::Animated && t > 1.0f) break;
            }
            std::cout << "  example O: " << ar->StateName() << " falls " << ar->Core().Falls() - fallsBefore
                      << " lowest pelvis " << lowest << " root " << d->transform->Position().x << ", " << d->transform->Position().y << " after " << t << " s\n";
            CHECK(ar->Core().Falls() > fallsBefore);
            CHECK(lowest < 0.6f);                         // it reached the ground below the ledge
            CHECK(ar->State() == CharState::Animated);    // and got up there
            CHECK(d->transform->Position().x > 1.6f);     // off the platform
        }
    }
    std::cout << "  recovery time after a 35 N s push: " << recovery << " s\n";
    TEST_MAIN_RESULT();
}
