// A scene saved while the player is crouched / prone reloads in that stance: the
// standing collider is saved (so the body keeps its real height), and the stance
// with it, so a player saved under low cover isn't stood up inside it.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <string>
#include <vector>

using namespace okay;

namespace {
const float kDt = 1.0f / 60.0f;

GameObject* Block(Scene& s, const char* n, Vec3 p, Vec3 size) {
    auto* o = s.CreateGameObject(n);
    o->transform->localPosition = p;
    o->AddComponent<BoxCollider3D>()->size = size;
    o->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    return o;
}
void Step(Scene& s, std::vector<char> keys, int n) {
    for (int i = 0; i < n; ++i) { Input::FeedKeys(keys); s.Update(kDt); }
}

// Crouch (or go prone) under a low cover, save, reload: the reloaded player is in
// the same stance, the right height, and on the floor (not pushed into it).
template <class C>
void UnderCover(void (*tpl)(Scene&), char stanceKey, typename C::Stance want, float headroom) {
    Scene a("save"); tpl(a);
    GameObject* p = a.Find("Player");
    CHECK(p != nullptr);
    if (!p) return;
    a.Start();
    Step(a, {}, 60);                                   // land (the template spawns 1 m up)
    Step(a, {stanceKey}, 1); Step(a, {}, 30);
    // Low cover right over the player: `headroom` m between the floor and its underside.
    const Vec3 at = p->transform->Position();
    Block(a, "Cover", {at.x, at.y + headroom + 0.5f, at.z}, {4.0f, 1.0f, 4.0f});
    Step(a, {}, 60);
    CHECK(p->GetComponent<C>()->stance() == want);
    const float liveH = p->GetComponent<BoxCollider3D>()->size.y;
    const float liveY = p->transform->Position().y;

    std::string text = SceneSerializer::Serialize(a);
    Scene b("load");
    CHECK(SceneSerializer::Deserialize(b, text));
    GameObject* q = b.Find("Player");
    CHECK(q != nullptr);
    if (!q) return;
    auto* c = q->GetComponent<C>();
    auto* box = q->GetComponent<BoxCollider3D>();
    b.Start();
    Step(b, {}, 60);
    CHECK(c->stance() == want);                                   // was Stand
    CHECK_NEAR(box->size.y, liveH, 1e-3f);                        // was the standing 1.8 inside the cover
    CHECK(std::fabs(q->transform->Position().y - liveY) < 0.02f); // was pushed 0.4 m into the floor
    CHECK_NEAR(c->stanceBody().standH, 1.8f, 1e-3f);              // still knows its real standing height

    // Crawl out from under the cover and stand up: the full-height body comes back.
    Step(b, {stanceKey}, 1);
    Step(b, {'w'}, 150); Step(b, {}, 30);
    CHECK(c->stance() == C::Stance::Stand);
    CHECK_NEAR(box->size.y, 1.8f, 1e-3f);
}
} // namespace

int main() {
    RUN_SUITE("stance_save");

    UnderCover<FirstPersonController>(Templates::FPS, Input::KeyCtrl, FirstPersonController::Stance::Crouch, 1.4f);
    UnderCover<ThirdPersonController>(Templates::ThirdPerson, Input::KeyCtrl, ThirdPersonController::Stance::Crouch, 1.4f);
    UnderCover<FirstPersonController>(Templates::FPS, 'z', FirstPersonController::Stance::Prone, 0.8f);
    UnderCover<ThirdPersonController>(Templates::ThirdPerson, 'z', ThirdPersonController::Stance::Prone, 0.8f);

    // Standing saves are unchanged: no stance restored, full body.
    {
        Scene a("stand"); Templates::FPS(a);
        a.Start(); Step(a, {}, 30);
        std::string text = SceneSerializer::Serialize(a);
        Scene b("stand2");
        CHECK(SceneSerializer::Deserialize(b, text));
        b.Start(); Step(b, {}, 30);
        GameObject* q = b.Find("Player");
        CHECK(q && q->GetComponent<FirstPersonController>()->stance() == FirstPersonController::Stance::Stand);
        CHECK(q && std::fabs(q->GetComponent<BoxCollider3D>()->size.y - 1.8f) < 1e-3f);
    }

    TEST_MAIN_RESULT();
}
