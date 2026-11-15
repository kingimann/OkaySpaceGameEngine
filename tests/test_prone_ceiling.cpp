// Prone under a low ceiling, for the first- and third-person controllers: going prone shrinks the body, a stand-up
// (or crouch) asked for under a slab lower than that stance's height is held back, and in toggle mode it happens on
// its own once the player is out from under the slab. Prone itself (keys, speed, eye height, saving) is covered by
// `stance`; this file covers the ceiling rule the controllers' comments promise. Written for this engine.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>

using namespace okay;

namespace {
const float kDt = 1.0f / 60.0f;

void Static(Scene& s, const char* name, Vec3 pos, Vec3 size) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = pos;
    g->AddComponent<BoxCollider3D>()->size = size;
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
}

template <typename Ctrl>
struct Rig {
    Scene scene{"prone_ceiling"};
    GameObject* player = nullptr;
    Ctrl* ctrl = nullptr;
    BoxCollider3D* box = nullptr;
    Rig() {
        Static(scene, "Floor", {0, -0.5f, 0}, {60, 1, 60});
        // A slab whose underside is 0.9 above the floor, over x in [-3, 3]: room to crawl, not to crouch or stand.
        Static(scene, "Slab", {0, 1.0f, 0}, {6, 0.2f, 6});
        player = scene.CreateGameObject("Player");
        player->transform->localPosition = {8, 0.05f, 0};
        player->AddComponent<Rigidbody3D>();
        box = player->AddComponent<BoxCollider3D>();
        box->size = {0.6f, 1.8f, 0.6f};
        box->offset = {0, 0.9f, 0};
        ctrl = player->AddComponent<Ctrl>();
        ctrl->toggleStance = true;
        ctrl->lockCursor = false;
        GameObject* eye = scene.CreateGameObject("Eye");
        eye->AddComponent<Camera>();
        eye->transform->SetParent(player->transform, false);
        eye->transform->localPosition = {0, 1.6f, 0};
        scene.Start();
        Tick({}, 20);
    }
    void Tick(const std::vector<char>& keys, int frames) {
        for (int i = 0; i < frames; ++i) { Input::FeedKeys(keys); scene.Update(kDt); }
    }
    void Tap(char k) { Tick({k}, 1); Tick({}, 1); }
    void PlaceAt(float x) {
        player->transform->SetPosition({x, player->transform->Position().y, 0});
        if (auto* rb = player->GetComponent<Rigidbody3D>()) rb->velocity = Vec3{0, 0, 0};
        Tick({}, 30);
    }
    int StanceNow() const { return (int)ctrl->stance(); }   // 0 stand, 1 crouch, 2 prone
};

template <typename Ctrl>
void Check(const char* who) {
    Rig<Ctrl> r;
    using St = typename Ctrl::Stance;
    CHECK(r.ctrl->stance() == St::Stand);
    const float standH = r.box->size.y;
    r.Tap(r.ctrl->proneKey);
    r.Tick({}, 90);
    CHECK(r.ctrl->stance() == St::Prone);
    const float proneH = r.box->size.y;
    std::printf("  %s: body %.2f standing, %.2f prone\n", who, standH, proneH);
    CHECK(proneH < 0.9f && proneH < standH * 0.5f);     // low enough to fit under the slab
    r.PlaceAt(0.0f);                                     // crawl in under the slab
    CHECK(r.ctrl->stance() == St::Prone);
    r.Tap(r.ctrl->proneKey);                             // ask to stand up: no room
    r.Tick({}, 60);
    std::printf("  %s: under the slab after asking to stand: stance %d, body %.2f\n", who, r.StanceNow(), r.box->size.y);
    CHECK(r.ctrl->stance() == St::Prone);
    CHECK(r.box->size.y < 0.9f);
    r.Tap(r.ctrl->crouchKey);                            // crouch does not fit either
    r.Tick({}, 60);
    CHECK(r.ctrl->stance() == St::Prone);
    r.Tap(r.ctrl->crouchKey);                            // back to asking for Stand (toggle)
    r.Tick({}, 10);
    r.PlaceAt(8.0f);                                     // out from under it: the stand-up happens on its own
    r.Tick({}, 90);
    std::printf("  %s: in the open: stance %d, body %.2f\n", who, r.StanceNow(), r.box->size.y);
    CHECK(r.ctrl->stance() == St::Stand);
    CHECK(r.box->size.y > standH - 0.05f);
}
} // namespace

int main() {
    RUN_SUITE("prone_ceiling");
    Check<FirstPersonController>("first person");
    Check<ThirdPersonController>("third person");
    TEST_MAIN_RESULT();
}
