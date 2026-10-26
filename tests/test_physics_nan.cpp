// One body going NaN must not poison the world (item 8): NaN fails every AABB
// rejection test, and the solver used to scale its correction by 0 for statics
// (NaN * 0 = NaN), so a static floor — and everything resting on it — went NaN.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <limits>

using namespace okay;

static bool Fin(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
static bool Fin(const Quat& q) { return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w); }

int main() {
    RUN_SUITE("physics_nan");
    const float nan = std::numeric_limits<float>::quiet_NaN();

    // ---- 3D ----
    for (int mode = 0; mode < 2; ++mode) {   // 0: NaN position, 1: NaN velocity
        Scene s("n3");
        GameObject* floor = s.CreateGameObject("Floor");
        floor->AddComponent<BoxCollider3D>()->size = {20, 1, 20};
        floor->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;

        GameObject* good = s.CreateGameObject("Good");
        good->transform->localPosition = {5, 0.9f, 0};
        good->AddComponent<BoxCollider3D>();
        good->AddComponent<Rigidbody3D>();

        GameObject* bad = s.CreateGameObject("Bad");
        bad->transform->localPosition = {0, 0.9f, 0};
        bad->AddComponent<SphereCollider3D>();
        auto* brb = bad->AddComponent<Rigidbody3D>();
        s.Start();
        s.Update(1.0f / 60.0f);
        if (mode == 0) bad->transform->localPosition = {nan, nan, nan};
        else brb->velocity = {nan, nan, nan};
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
        CHECK(Fin(floor->transform->localPosition));
        CHECK(Fin(floor->transform->localRotation));
        CHECK(Fin(good->transform->localPosition));
        CHECK(Fin(good->GetComponent<Rigidbody3D>()->velocity));
        CHECK(good->transform->localPosition.y > 0.0f);   // still standing on the floor
    }

    // ---- 2D ----
    for (int mode = 0; mode < 2; ++mode) {
        Scene s("n2");
        GameObject* floor = s.CreateGameObject("Floor");
        floor->AddComponent<BoxCollider2D>()->size = {20, 1};
        floor->AddComponent<Rigidbody2D>()->bodyType = Rigidbody2D::BodyType::Static;

        GameObject* good = s.CreateGameObject("Good");
        good->transform->localPosition = {5, 0.9f, 0};
        good->AddComponent<BoxCollider2D>();
        good->AddComponent<Rigidbody2D>();

        GameObject* bad = s.CreateGameObject("Bad");
        bad->transform->localPosition = {0, 0.9f, 0};
        bad->AddComponent<BoxCollider2D>();
        auto* brb = bad->AddComponent<Rigidbody2D>();
        s.Start();
        s.Update(1.0f / 60.0f);
        if (mode == 0) bad->transform->localPosition = {nan, nan, 0};
        else brb->velocity = {nan, nan};
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
        CHECK(Fin(floor->transform->localPosition));
        CHECK(Fin(floor->transform->localRotation));
        CHECK(Fin(good->transform->localPosition));
        CHECK(std::isfinite(good->GetComponent<Rigidbody2D>()->velocity.x));
        CHECK(good->transform->localPosition.y > 0.0f);
    }

    // ---- Joints: a NaN body jointed to a STATIC body must not poison it
    //      (its share of the correction is err * 0, which is NaN for a NaN err). ----
    for (int mode = 0; mode < 3; ++mode) {   // Distance, Spring, Pin
        {
            Scene s("j3");
            GameObject* anchor = s.CreateGameObject("Anchor3");
            anchor->transform->localPosition = {0, 5, 0};
            anchor->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
            GameObject* bob = s.CreateGameObject("Bob3");
            bob->transform->localPosition = {0, 3, 0};
            auto* rb = bob->AddComponent<Rigidbody3D>();
            auto* j = bob->AddComponent<Joint3D>();
            j->connectedBody = "Anchor3";
            j->mode = (decltype(j->mode))mode;
            s.Start();
            s.Update(1.0f / 60.0f);
            bob->transform->localPosition = {nan, nan, nan};
            rb->velocity = {nan, nan, nan};
            for (int i = 0; i < 5; ++i) s.Update(1.0f / 60.0f);
            CHECK(Fin(anchor->transform->localPosition));
            CHECK(Fin(anchor->GetComponent<Rigidbody3D>()->velocity));
        }
        {
            Scene s("j2");
            GameObject* anchor = s.CreateGameObject("Anchor2");
            anchor->transform->localPosition = {0, 5, 0};
            anchor->AddComponent<Rigidbody2D>()->bodyType = Rigidbody2D::BodyType::Static;
            GameObject* bob = s.CreateGameObject("Bob2");
            bob->transform->localPosition = {0, 3, 0};
            auto* rb = bob->AddComponent<Rigidbody2D>();
            auto* j = bob->AddComponent<Joint2D>();
            j->connectedBody = "Anchor2";
            j->mode = (decltype(j->mode))mode;
            s.Start();
            s.Update(1.0f / 60.0f);
            bob->transform->localPosition = {nan, nan, 0};
            rb->velocity = {nan, nan};
            for (int i = 0; i < 5; ++i) s.Update(1.0f / 60.0f);
            CHECK(Fin(anchor->transform->localPosition));
            CHECK(std::isfinite(anchor->GetComponent<Rigidbody2D>()->velocity.x));
        }
    }

    TEST_MAIN_RESULT();
}
