// QA regression tests for PR #250 (locomotion, stance body, 2.5D upgrades, winding).
// Each block reproduces a bug QA reported on the PR; they fail until it's fixed.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <vector>

#ifndef CHECK_EQ
#define CHECK_EQ(a, b) CHECK((a) == (b))
#endif

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
double SignedVolume(const Mesh& m) {
    double v = 0;
    for (std::size_t i = 0; i + 2 < m.triangles.size(); i += 3) {
        Vec3 a = m.vertices[m.triangles[i]], b = m.vertices[m.triangles[i + 1]], c = m.vertices[m.triangles[i + 2]];
        v += Vec3::Dot(a, Vec3::Cross(b, c)) / 6.0;
    }
    return v;
}
template <class C>
void HoldModePadTap(void (*tpl)(Scene&)) {
    Scene s("pad"); tpl(s);
    auto* c = s.Find("Player")->GetComponent<C>();
    c->toggleStance = false;
    s.Start(); Step(s, {}, 30);
    for (int i = 0; i < 3; ++i) { Input::FeedKeys({}); Input::FeedGamepad({0, 0}, 1u << 1); s.Update(kDt); }
    Input::FeedGamepad({0, 0}, 0);
    Step(s, {}, 30);
    CHECK(c->stance() == C::Stance::Crouch);   // tap = crouch (it reverted to Stand the next frame)
    Input::FeedGamepad({0, 0}, 0);
}
} // namespace

int main() {
    RUN_SUITE("qa_pr250");

    // ---- FP: jumping out of a slide under low cover must not stand up into it ----
    {
        Scene s("slide"); Templates::FPS(s);
        GameObject* p = s.Find("Player");
        auto* box = p->GetComponent<BoxCollider3D>();
        const Vec3 st = p->transform->Position();
        Block(s, "Bar", {st.x, st.y + 1.9f, st.z - 6.0f}, {8.0f, 1.0f, 3.0f});   // 1.4 m of headroom underneath
        s.Start(); Step(s, {}, 60);
        Step(s, {'w', Input::KeyShift}, 30);
        Step(s, {'w', Input::KeyShift, Input::KeyCtrl}, 1);
        for (int i = 0; i < 120 && p->transform->Position().z - st.z > -5.2f; ++i) Step(s, {'w', Input::KeyShift}, 1);
        CHECK(p->transform->Position().z - st.z < -5.0f);   // under the bar
        Step(s, {'w', ' '}, 1);
        Step(s, {}, 10);
        CHECK(box->size.y < 1.4f);                            // still fits under the bar
    }

    // ---- Gamepad stance tap works with Toggle Stance off ----
    HoldModePadTap<FirstPersonController>(Templates::FPS);
    HoldModePadTap<ThirdPersonController>(Templates::ThirdPerson);

    // ---- Saving while crouched must not bake the crouched collider into the scene ----
    {
        Scene a("save"); Templates::FPS(a);
        a.Start(); Step(a, {}, 30);
        Step(a, {Input::KeyCtrl}, 1); Step(a, {}, 10);
        std::string text = SceneSerializer::Serialize(a);
        Scene b("load");
        CHECK(SceneSerializer::Deserialize(b, text));
        auto* box = b.Find("Player") ? b.Find("Player")->GetComponent<BoxCollider3D>() : nullptr;
        CHECK(box != nullptr);
        if (box) CHECK_NEAR(box->size.y, 1.8f, 1e-3f);
    }

    // ---- Capsule body: prone must not lift the player off the ground ----
    {
        Scene s("cap");
        Block(s, "Ground", {0, -0.5f, 0}, {40, 1, 40});
        GameObject* p = s.CreateGameObject("Player");
        p->transform->localPosition = {0, 0.02f, 0};
        p->AddComponent<Character>()->Apply();
        p->AddComponent<Rigidbody3D>();
        auto* col = p->AddComponent<CapsuleCollider3D>();
        col->radius = 0.4f; col->height = 1.8f; col->offset = {0, 0.9f, 0};
        p->AddComponent<ThirdPersonController>();
        s.Start(); Step(s, {}, 60);
        const float y0 = p->transform->Position().y;
        Step(s, {'z'}, 1); Step(s, {}, 60);
        CHECK(std::fabs(p->transform->Position().y - y0) < 0.02f);   // rose 0.10 m
        Step(s, {'z'}, 1); Step(s, {}, 120);
        CHECK(std::fabs(p->transform->Position().y - y0) < 0.02f);   // and stayed up after standing
    }

    // ---- 2.5D: the jump pose starts on the jump frame ----
    {
        Scene s("p25jump"); Templates::Platformer25D(s);
        auto* ch = s.Find("Player")->GetComponent<Character>();
        s.Start(); Step(s, {}, 30);
        Step(s, {' '}, 1); Step(s, {}, 2);
        CHECK_EQ(ch->anim, 5);                                     // was idle for ~8 frames
    }

    // ---- 2.5D: catching a low ledge must not "hang" while standing on the floor ----
    {
        Scene s("p25ledge"); Templates::Platformer25D(s);
        GameObject* p = s.Find("Player");
        auto* pc = p->GetComponent<Platformer25DController>();
        pc->layer = 1; p->transform->localPosition = {21.0f, 0.05f, -1.5f};
        s.Start(); Step(s, {}, 30);
        Step(s, {'a'}, 5);
        for (int f = 0; f < 40 && !pc->IsHanging(); ++f) Step(s, {' ', 'e', 'a'}, 1);
        if (pc->IsHanging()) CHECK(p->transform->Position().y > 0.05f);   // hung at y = 0, feet on the ground
    }

    // ---- 2.5D: an idle layer hop isn't the run cycle ----
    {
        Scene s("p25hop"); Templates::Platformer25D(s);
        GameObject* p = s.Find("Player");
        auto* pc = p->GetComponent<Platformer25DController>();
        auto* ch = p->GetComponent<Character>();
        s.Start(); Step(s, {}, 60);
        CHECK(pc->SwitchLayer(pc->layer + 1));
        bool ran = false;
        for (int i = 0; i < 40; ++i) { Step(s, {}, 1); ran = ran || ch->anim == 3; }
        CHECK(!ran);
    }

    // ---- Winding: the fixed primitives are outward, and so is Lathe (Barrel, Windmill) ----
    {
        CHECK(SignedVolume(Mesh::Sphere()) > 0.0);
        CHECK(SignedVolume(Mesh::Cylinder()) > 0.0);
        CHECK(SignedVolume(Mesh::Capsule()) > 0.0);
        CHECK(SignedVolume(Mesh::Torus()) > 0.0);
        CHECK(SignedVolume(Mesh::Lathe({{0.001f, 0}, {0.4f, 0.2f}, {0.4f, 0.8f}, {0.001f, 1}}, 16)) > 0.0);
        CHECK(SignedVolume(Mesh::FromName("Barrel")) > 0.0);
        CHECK(SignedVolume(Mesh::FromName("Windmill")) > 0.0);
    }

    TEST_MAIN_RESULT();
}
