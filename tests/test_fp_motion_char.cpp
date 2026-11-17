// The first-person hands on a real player (Templates::AddFirstPersonPlayer on a floor): the owner's arm copy is
// stepped frame by frame through idle, walk, sprint, jump, land, crouch and named events, and its position and
// rotation IN THE CAMERA must stay continuous (no pop), the held item must stay in the hand, a wall in front must
// pull the hands back and never let them cross the near plane, Reduce Motion must scale the motion, and nothing of
// the viewmodel shows in third person. Written from the engine's own components; no outside code.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace okay;

namespace {
const float kDt = 1.0f / 60.0f;

struct Rig {
    Scene scene{"FpMotionChar"};
    GameObject* player = nullptr;
    Character* ch = nullptr;
    FirstPersonHands* hands = nullptr;
    FirstPersonController* fp = nullptr;
    Camera* cam = nullptr;
    ProceduralObjectHolding* hold = nullptr;
    GameObject* floor = nullptr;
    explicit Rig(bool withPhysics = true) {
        floor = scene.CreateGameObject("Floor");
        floor->transform->localPosition = {0, -0.5f, 0};
        floor->AddComponent<BoxCollider3D>()->size = {200, 1, 200};
        floor->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        player = Templates::AddFirstPersonPlayer(scene, {0, 0.1f, 0});
        ch = player->GetComponent<Character>();
        hands = player->GetComponent<FirstPersonHands>();
        fp = player->GetComponent<FirstPersonController>();
        hold = player->GetComponent<ProceduralObjectHolding>();
        if (!hold) hold = player->AddComponent<ProceduralObjectHolding>();
        for (const auto& o : scene.Objects())
            if (auto* c = o->GetComponent<Camera>()) if (o->IsSelfOrDescendantOf(player)) cam = c;
        (void)withPhysics;
        scene.Start();
        Input::FeedKeys({});
        for (int i = 0; i < 120; ++i) scene.Update(kDt);
    }
    void Step(std::vector<char> keys = {}) { Input::FeedKeys(keys); scene.Update(kDt); }
    // The arm copy in the camera's own space: where it sits and how it is turned.
    bool ArmInCamera(Vec3& pos, Quat& rot) const {
        GameObject* a = ch->OwnerViewArm();
        if (!a || !a->active || !a->transform || !cam || !cam->transform) return false;
        pos = cam->transform->InverseTransformPoint(a->transform->Position());
        rot = cam->transform->Rotation().Conjugate() * a->transform->Rotation();
        return true;
    }
};

Vec3 cam_local(const Rig& r, GameObject* g) { return r.cam->transform->InverseTransformPoint(g->transform->Position()); }

struct Jumps {
    float maxPos = 0.0f, maxRotDeg = 0.0f;
    int frames = 0, popped = 0, atFrame = 0, n = 0;
    Vec3 pp; Quat pr; bool have = false;
    void Sample(const Rig& r) {
        Vec3 p; Quat q;
        if (!r.ArmInCamera(p, q)) { have = false; return; }
        if (have) {
            maxPos = std::fmax(maxPos, (p - pp).Magnitude());
            const float d = std::fmin(1.0f, std::fabs(pp.x * 0 + (Quat::Dot(q, pr))));
            const float deg = 2.0f * std::acos(d) * 57.29578f;
            if (deg > maxRotDeg) atFrame = n;
            if (std::getenv("FPM_DEBUG") && deg > 2.5f) std::printf("    sample %d: %.2f deg, %.4f m\n", n, deg, (p - pp).Magnitude());
            maxRotDeg = std::fmax(maxRotDeg, deg);
        }
        pp = p; pr = q; have = true; ++frames; ++n;
    }
};
float AngleBetween(const Quat& a, const Quat& b) {
    const float d = std::fmin(1.0f, std::fabs(Quat::Dot(a, b)));
    return 2.0f * std::acos(d) * 57.29578f;
}
}

int main() {
    RUN_SUITE("fp_motion_char");
    Accessibility::SetReduceMotion(false);

    // The setup works: arm shown, motion layer running.
    {
        Rig r;
        Vec3 p; Quat q;
        CHECK(r.ch && r.hands && r.fp && r.cam);
        CHECK(r.ArmInCamera(p, q));
        CHECK(r.ch->CurrentFpGrip() == FpGripKind::Empty);
    }

    // Continuity through every state the hands go through. The look does not move, so the arm in the camera must not jump.
    {
        Rig r;
        Jumps j;
        auto run = [&](int n, std::vector<char> keys) { for (int i = 0; i < n; ++i) { r.Step(keys); j.Sample(r); } };
        run(120, {});                                  // idle
        run(180, {'w'});                               // start walking
        run(180, {'w', (char)Input::KeyShift});        // sprint on
        run(120, {'w'});                               // sprint off
        run(90, {});                                   // stop
        run(60, {'w'});
        run(30, {' '});                                // jump while walking
        run(120, {'w'});                               // fall and land
        run(90, {(char)Input::KeyCtrl});               // crouch
        run(90, {});                                   // stand
        run(10, {});
        std::printf("  walk/sprint/jump/crouch on a real player: %d frames, max step %.4f m, %.2f deg (at sample %d)\n", j.frames, j.maxPos, j.maxRotDeg, j.atFrame);
        CHECK(j.frames > 800);   // the hand is hidden for part of a sprint
        CHECK(j.maxPos < 0.045f);
        CHECK(j.maxRotDeg < 7.0f);
    }

    // Named events through the character, and the same names the sandbox can use.
    {
        Rig r;
        for (int i = 0; i < 30; ++i) r.Step();
        CHECK(r.ch->TriggerFpEvent("swing"));
        CHECK(r.ch->TriggerFpEvent("dig"));
        CHECK(!r.ch->TriggerFpEvent("not an event"));
        CHECK(r.ch->FpMotion().Playing(FpEventKind::Swing));
        Jumps j;
        Vec3 p0; Quat q0; r.ArmInCamera(p0, q0);
        float reach = 0.0f;
        for (int i = 0; i < 40; ++i) { r.Step(); j.Sample(r); Vec3 p; Quat q; if (r.ArmInCamera(p, q)) reach = std::fmax(reach, (p - p0).Magnitude()); }
        CHECK(reach > 0.04f);
        CHECK(!r.ch->FpMotion().AnyPlaying());
        CHECK(j.maxPos < 0.06f);   // a quick kick turns about a pivot: a smooth bell of about 0.05 m a frame at its fastest, never one jump
        CHECK(j.maxRotDeg < 7.0f);
        for (int i = 0; i < 90; ++i) r.Step();
        Vec3 p1; Quat q1; r.ArmInCamera(p1, q1);
        CHECK((p1 - p0).Magnitude() < 0.01f);
    }

    // Held items: grip kind from the item, hand and item stay together, equip plays by itself, bob follows the hands.
    {
        Rig r;
        GameObject* gun = r.scene.CreateGameObject("Rifle");
        gun->transform->localPosition = {0.6f, 0.6f, -1.0f};
        gun->transform->localScale = {0.1f, 0.12f, 0.8f};
        gun->AddComponent<MeshRenderer>();
        gun->AddComponent<HoldableObject>();
        for (int i = 0; i < 10; ++i) r.Step();
        r.hold->Pickup(gun);
        CHECK(r.hold->IsHolding());
        Jumps j;
        float maxSep = 0.0f, minSep = 1e9f;
        bool equipSeen = false;
        for (int i = 0; i < 240; ++i) {
            r.Step(i > 100 ? std::vector<char>{'w'} : std::vector<char>{});
            if (i > 60) j.Sample(r);   // the first frames are the pickup itself: one hand pose becomes two
            if (r.ch->FpMotion().Playing(FpEventKind::Equip)) equipSeen = true;
            GameObject* item = nullptr;
            for (const auto& o : r.scene.Objects()) if (o->name == "FP_OwnerItem" && o->active) item = o.get();
            GameObject* arm = r.ch->OwnerViewArm();
            if (item && arm && i > 60) {
                const float sep = (cam_local(r, item) - cam_local(r, arm)).Magnitude();
                maxSep = std::fmax(maxSep, sep); minSep = std::fmin(minSep, sep);
            }
        }
        CHECK(equipSeen);
        CHECK(r.ch->CurrentFpGrip() == FpGripKind::Rifle);
        std::printf("  rifle held: item to arm distance %.4f..%.4f, max step %.4f m / %.2f deg\n", minSep, maxSep, j.maxPos, j.maxRotDeg);
        CHECK(maxSep - minSep < 0.003f);                // the item stays in the hand through the bob and the walk
        CHECK(j.maxPos < 0.06f);
        CHECK(j.maxRotDeg < 9.0f);
        r.hands->itemActions = false;   // otherwise the right mouse button (not pressed) sets the aim each frame
        r.ch->SetFpAim(true);
        for (int i = 0; i < 60; ++i) r.Step();
        CHECK(r.ch->FpAiming());
        CHECK(r.ch->FpMotion().AimWeight() > 0.9f);
        r.ch->SetFpAim(false);
        r.hold->Drop();
        for (int i = 0; i < 20; ++i) r.Step();
        CHECK(r.ch->CurrentFpGrip() == FpGripKind::Empty);
    }

    // A wall right in front: the hands come back and down, and nothing crosses the near plane.
    {
        Rig r;
        for (int i = 0; i < 20; ++i) r.Step();
        Vec3 p0; Quat q0; r.ArmInCamera(p0, q0);
        const float free = r.ch->FpWallPull();
        GameObject* wall = r.scene.CreateGameObject("Wall");
        const Vec3 eye = r.cam->transform->Position();
        const Vec3 fwd = r.cam->transform->Rotation() * Vec3{0, 0, -1};
        wall->transform->localPosition = eye + fwd * 0.62f;
        wall->transform->localRotation = r.cam->transform->Rotation();
        wall->AddComponent<BoxCollider3D>()->size = {6, 6, 0.2f};
        wall->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        Jumps j;
        for (int i = 0; i < 90; ++i) { r.Step(); j.Sample(r); }
        Vec3 p1; Quat q1; r.ArmInCamera(p1, q1);
        std::printf("  wall: pull %.2f -> %.2f, hand moved %.3f m / %.1f deg, max step %.4f\n", free, r.ch->FpWallPull(), (p1 - p0).Magnitude(), AngleBetween(q0, q1), j.maxPos);
        CHECK(r.ch->FpWallPull() > 0.5f);
        CHECK(p1.y < p0.y - 0.02f);
        CHECK(j.maxPos < 0.03f);
        // The nearest point of the arm, hand and forearm is never closer than the near-plane guard.
        float nearest = 1e9f;
        for (GameObject* g : {r.ch->OwnerViewArm()}) {
            if (!g) continue;
            for (Transform* c : g->transform->Children()) { (void)c; }
        }
        for (const auto& o : r.scene.Objects()) {
            if (!o->firstPersonViewmodel || !o->active || o->name == "Player") continue;
            auto* mr = o->GetComponent<MeshRenderer>();
            if (!mr || !o->IsSelfOrDescendantOf(r.player) || o->name.rfind("FP_", 0) != 0) continue;
            for (const Vec3& v : mr->mesh.vertices)
                nearest = std::fmin(nearest, -r.cam->transform->InverseTransformPoint(o->transform->TransformPoint(v)).z);
        }
        CHECK(nearest > 0.1f);
        wall->active = false;
        for (int i = 0; i < 90; ++i) r.Step();
        CHECK(r.ch->FpWallPull() < 0.05f);
    }

    // Reduce Motion scales the motion on the real player.
    {
        auto swing = [&](bool reduce) {
            Accessibility::SetReduceMotion(reduce);
            Rig r;
            Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
            for (int i = 0; i < 160; ++i) r.Step({'w'});
            for (int i = 0; i < 180; ++i) {
                r.Step({'w'});
                Vec3 p; Quat q;
                if (!r.ArmInCamera(p, q)) continue;
                lo = Vec3{std::fmin(lo.x, p.x), std::fmin(lo.y, p.y), std::fmin(lo.z, p.z)};
                hi = Vec3{std::fmax(hi.x, p.x), std::fmax(hi.y, p.y), std::fmax(hi.z, p.z)};
            }
            return (hi - lo).Magnitude();
        };
        const float on = swing(false), off = swing(true);
        std::printf("  arm range while walking: normal %.4f m, reduce motion %.4f m\n", on, off);
        Accessibility::SetReduceMotion(false);
        CHECK(on > 0.01f);
        CHECK(off < on * 0.5f);
    }

    // Third person: no viewmodel is drawn for the owner.
    {
        Rig r;
        auto* view = r.player->GetComponent<PlayerView>();
        CHECK(view != nullptr);
        if (view) {
            view->SetView(1);
            for (int i = 0; i < 120; ++i) r.Step();
            GameObject* arm = r.ch->OwnerViewArm();
            CHECK(!arm || !arm->active);
            for (const auto& o : r.scene.Objects())
                if (o->firstPersonViewmodel && o->name.rfind("FP_", 0) == 0) CHECK(!o->active || o->name == "FP_ViewAnchor");
        }
    }
    TEST_MAIN_RESULT();
}
