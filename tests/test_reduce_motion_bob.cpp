// Reduce Motion (Accessibility) and the first-person camera: the walking head bob and sway and the landing dip are
// turned off, footstep events still fire, and turning the setting on or off mid-walk eases the bob instead of snapping
// the view. Written from the engine's own FirstPersonController behaviour (the docs/accessibility.md promise that
// reduce motion removes camera sway); no other project's code was used.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>

using namespace okay;

namespace {
const float kDt = 1.0f / 60.0f;

struct Rig {
    Scene scene{"reduce_motion_bob"};
    GameObject* player = nullptr;
    FirstPersonController* fp = nullptr;
    Transform* cam = nullptr;
    int steps = 0;
    float landImpact = 0.0f;
    explicit Rig(float startY) {
        GameObject* floor = scene.CreateGameObject("Floor");
        floor->transform->localPosition = {0, -0.5f, 0};
        floor->AddComponent<BoxCollider3D>()->size = {80, 1, 80};
        floor->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        player = scene.CreateGameObject("Player");
        player->transform->localPosition = {0, startY, 0};
        player->AddComponent<Rigidbody3D>();
        auto* box = player->AddComponent<BoxCollider3D>();
        box->size = {0.6f, 1.8f, 0.6f};
        box->offset = {0, 0.9f, 0};
        fp = player->AddComponent<FirstPersonController>();
        fp->lookSmoothing = 0.0f;
        fp->lockCursor = false;
        fp->onFootstep = [this](bool) { ++steps; };
        fp->onLand = [this](float impact) { landImpact = impact; };
        GameObject* eye = scene.CreateGameObject("Eye");
        eye->AddComponent<Camera>();
        eye->transform->SetParent(player->transform, false);
        eye->transform->localPosition = {0, 1.6f, 0};
        cam = eye->transform;
        scene.Start();
        Input::FeedKeys({});
    }
    float EyeOffset() const { return cam->localPosition.y - fp->standEyeHeight; }
};

// Walk forward; the largest eye offset after the bob has settled in, and the footsteps counted.
struct Walk { float bob = 0.0f; int steps = 0; };
Walk WalkFor(Rig& r, int frames, int settle = 20) {
    Walk w;
    const int before = r.steps;
    Input::FeedKeys({'w'});
    for (int i = 0; i < frames; ++i) {
        r.scene.Update(kDt);
        if (i >= settle) w.bob = std::fmax(w.bob, std::fabs(r.EyeOffset()));
    }
    w.steps = r.steps - before;
    return w;
}

// Drop from a height; the deepest dip of the eye below its standing height after touching down.
float DropDip(bool reduce) {
    Accessibility::SetReduceMotion(reduce);
    Rig r(6.0f);
    float dip = 0.0f;
    bool landed = false;
    for (int i = 0; i < 150; ++i) {
        r.scene.Update(kDt);
        if (r.landImpact > 0.0f) landed = true;
        if (landed) dip = std::fmax(dip, -r.EyeOffset());
    }
    CHECK(landed);
    std::printf("  drop (reduce motion %s): impact %.2f, deepest dip %.4f\n", reduce ? "on " : "off", r.landImpact, dip);
    return dip;
}
} // namespace

int main() {
    RUN_SUITE("reduce_motion_bob");
    Accessibility::Reset();

    {   // the bob with reduce motion off (the default), then on
        Accessibility::SetReduceMotion(false);
        Rig r(0.2f);
        for (int i = 0; i < 20; ++i) r.scene.Update(kDt);
        const Walk normal = WalkFor(r, 60);
        std::printf("  walk, reduce motion off: bob %.4f, %d steps\n", normal.bob, normal.steps);
        CHECK(normal.bob > 0.008f);
        CHECK(normal.steps >= 2);

        // Switched on mid-walk: the bob eases out, without a jump bigger than the bob itself.
        Accessibility::SetReduceMotion(true);
        float prev = r.EyeOffset(), jump = 0.0f;
        Input::FeedKeys({'w'});
        for (int i = 0; i < 30; ++i) {
            r.scene.Update(kDt);
            jump = std::fmax(jump, std::fabs(r.EyeOffset() - prev));
            prev = r.EyeOffset();
        }
        std::printf("  turned on mid-walk: largest one-frame move %.4f\n", jump);
        CHECK(jump < normal.bob);
        const Walk calm = WalkFor(r, 60, 0);
        std::printf("  walk, reduce motion on: bob %.5f, %d steps\n", calm.bob, calm.steps);
        CHECK(calm.bob < 0.001f);
        CHECK(calm.steps >= 2);                     // footstep sounds still play

        // Turned off again: the bob comes back.
        Accessibility::SetReduceMotion(false);
        const Walk back = WalkFor(r, 60);
        std::printf("  walk, reduce motion off again: bob %.4f\n", back.bob);
        CHECK(back.bob > 0.008f);
    }

    {   // a fresh controller that starts with reduce motion on never bobs
        Accessibility::SetReduceMotion(true);
        Rig r(0.2f);
        for (int i = 0; i < 20; ++i) r.scene.Update(kDt);
        const Walk w = WalkFor(r, 80, 0);
        CHECK(w.bob < 0.001f);
        CHECK(w.steps >= 2);
    }

    {   // the landing dip
        const float normal = DropDip(false);
        const float calm = DropDip(true);
        CHECK(normal > 0.02f);
        CHECK(calm < 0.002f);
    }

    Accessibility::Reset();
    TEST_MAIN_RESULT();
}
