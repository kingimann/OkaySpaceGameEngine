#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>

using namespace okay;

// Camera vs foot IK frame order (audit suspicion, 2026-11, "unconfirmed").
// The controllers place the camera in LateUpdate at order -10; FootIK (85) and
// FullBodyIK (100) move the bones after that. The worry: the eye is placed
// before IK lowers the hips, so on a slope or stairs the camera lags the head.
//
// This walks a first- and a third-person character up a ramp and a flight of
// steps with foot IK on, in lockstep with the same scene with foot IK weight 0, and
// checks every frame that:
//  - the camera is in exactly the same place with IK on and off: the camera is
//    placed from the body (capsule origin / orbit pivot), never from a bone, so
//    when IK runs cannot change it and there is no frame of lag to remove;
//  - the camera's offset from the body moves smoothly (no one-frame pop);
//  - the head bone stays steady against the eye (IK eases, it does not pop).
// It is a guard: if a controller ever starts placing the camera from a bone,
// the first check fails and the camera must then run after IK.

namespace {

void Ground(Scene& s, Vec3 pos, Vec3 size, float pitchDeg = 0.0f) {
    GameObject* g = s.CreateGameObject("Ground");
    g->transform->localPosition = pos;
    g->transform->localRotation = Quat::Euler(pitchDeg, 0, 0);
    g->AddComponent<BoxCollider3D>()->size = size;
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
}

void Course(Scene& s) {
    Ground(s, {0, -0.5f, 0}, {40, 1, 40});                           // floor
    Ground(s, {-3.0f, 0.4f, -6.0f}, {3, 0.2f, 8}, 8.0f);            // ramp (left lane)
    for (int k = 0; k < 8; ++k)                                      // stairs (right lane), 15 cm risers
        Ground(s, {3.0f, 0.075f * (k + 1), -3.0f - 0.35f * k}, {3, 0.15f * (k + 1), 0.35f});
}

struct World {
    Scene s{"w"};
    GameObject* player = nullptr;
    Transform* cam = nullptr;
    Transform* head = nullptr;
};

template <typename Ctrl>
void Build(World& w, float laneX, bool ik) {
    Course(w.s);
    auto* camGo = w.s.CreateGameObject("Main Camera");
    w.s.mainCamera = camGo->AddComponent<Camera>();
    w.player = w.s.CreateGameObject("Player");
    w.player->transform->localPosition = {laneX, 0.05f, 2.0f};
    auto* ch = w.player->AddComponent<Character>(); ch->Apply();
    auto* ctrl = w.player->AddComponent<Ctrl>();
    ctrl->footIK = true;
    if constexpr (std::is_same_v<Ctrl, FirstPersonController>) {
        ctrl->lockCursor = false;
        camGo->transform->SetParent(w.player->transform, false);
    }
    w.cam = camGo->transform;
    w.s.Start();
    if (!ik) if (auto* f = w.player->GetComponent<FootIK>()) f->weight = 0.0f;   // same rig, IK solving off
    w.head = ch->Part(2) ? ch->Part(2)->transform : nullptr;
}

template <typename Ctrl>
void Run(const char* name, float laneX) {
    World on, off;
    Build<Ctrl>(on, laneX, true);
    Build<Ctrl>(off, laneX, false);
    CHECK(on.player->GetComponent<FootIK>() != nullptr);
    CHECK(on.head != nullptr);
    if (!on.head) return;
    const float dt = 1.0f / 60.0f;
    auto step = [&](World& w, bool walk) {
        Input::FeedMouse({400, 300}, 0);
        Input::FeedKeys(walk ? std::vector<char>{'W'} : std::vector<char>{});
        w.s.Update(dt);
    };
    for (int i = 0; i < 30; ++i) { step(on, false); step(off, false); }
    for (World* w : {&on, &off})
        if (auto* c = w->player->GetComponent<Character>()) w->head = c->Part(2) ? c->Part(2)->transform : nullptr;
    CHECK(off.head != nullptr);
    if (!off.head) return;
    const float y0 = on.player->transform->Position().y;
    float maxDiff = 0, ikMoved = 0, maxCamStep = 0, maxHeadStep = 0;
    Vec3 prevCam{}, prevHead{}; bool have = false;
    for (int i = 0; i < 300; ++i) {
        step(on, true); step(off, true);
        maxDiff = std::fmax(maxDiff, (on.cam->Position() - off.cam->Position()).Magnitude());
        ikMoved = std::fmax(ikMoved, std::fabs((on.head->Position() - on.player->transform->Position()).y -
                                                (off.head->Position() - off.player->transform->Position()).y));
        const Vec3 camOff = on.cam->Position() - on.player->transform->Position();
        const Vec3 headOff = on.head->Position() - on.cam->Position();
        if (have) {
            maxCamStep = std::fmax(maxCamStep, (camOff - prevCam).Magnitude());
            maxHeadStep = std::fmax(maxHeadStep, (headOff - prevHead).Magnitude());
        }
        prevCam = camOff; prevHead = headOff; have = true;
    }
    const float climbed = on.player->transform->Position().y - y0;
    std::printf("  %s: climbed %.2f m; IK moved the head up to %.3f m; camera IK-on vs IK-off max %.6f m; camera-from-body step max %.4f m; "
                "head-from-camera step max %.4f m\n", name, climbed, ikMoved, maxDiff, maxCamStep, maxHeadStep);
    CHECK(climbed > 0.15f);         // it really went up the slope / stairs
    CHECK(ikMoved > 0.005f);        // foot IK really moved the head (hips lowered)
    CHECK(maxDiff < 1e-4f);         // IK (whenever it runs) never moves the camera
    CHECK(maxCamStep < 0.08f);      // no pop of the camera against the body
    CHECK(maxHeadStep < 0.08f);     // the head eases with the IK, no one-frame jump
}

}  // namespace

int main() {
    RUN_SUITE("camera_after_ik");
    Run<FirstPersonController>("first person, ramp", -3.0f);
    Run<FirstPersonController>("first person, stairs", 3.0f);
    Run<ThirdPersonController>("third person, ramp", -3.0f);
    Run<ThirdPersonController>("third person, stairs", 3.0f);
    TEST_MAIN_RESULT();
}
