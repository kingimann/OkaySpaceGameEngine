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

    // ---- Sweep queries must always terminate (the march used to spin forever) ----
    // A NaN reach, a NaN origin, a NaN-sized collider, and a collider so far away that
    // one march step is smaller than the float spacing there (t + step == t).
    {
        Scene s("sweep3");
        GameObject* wall = s.CreateGameObject("Wall");
        wall->transform->localPosition = {10, 0, 0};
        wall->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        s.Start(); s.Update(0.0f);
        Physics3D& phys = s.physics3D();
        RaycastHit3D h = phys.SphereCast(s, {0, 0, 0}, {1, 0, 0}, 0.5f, nan);
        CHECK(!h.hit);                                                  // a NaN reach finds nothing
        h = phys.SphereCast(s, {nan, 0, 0}, {1, 0, 0}, 0.5f, 100.0f);
        CHECK(!h.hit || std::isfinite(h.distance));                     // a NaN origin just returns
        h = phys.SphereCast(s, {0, 0, 0}, {nan, 0, 0}, 0.5f, 100.0f);
        CHECK(!h.hit || std::isfinite(h.distance));

        GameObject* bad = s.CreateGameObject("NanBox");
        bad->transform->localPosition = {20, 0, 0};
        bad->AddComponent<BoxCollider3D>()->size = {nan, nan, nan};
        h = phys.SphereCast(s, {0, 0, 0}, {1, 0, 0}, 0.5f, 100.0f);
        CHECK(h.hit && h.gameObject == wall);                           // the sane wall is still found
        CHECK_NEAR(h.distance, 9.0f, 0.2f);

        Scene distant("far3");
        GameObject* fw = distant.CreateGameObject("FarWall");
        fw->transform->localPosition = {1.0e6f, 0, 0};
        fw->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        distant.Start(); distant.Update(0.0f);
        h = distant.physics3D().SphereCast(distant, {0, 0, 0}, {1, 0, 0}, 0.5f, 2.0e6f);
        CHECK(h.hit && h.gameObject == fw);
        CHECK_NEAR(h.distance, 999999.0f, 1.0f);

        // A near miss out there: the thin sphere is inside the box's bounds slab but clear of the
        // box's edge, so the march must walk the whole stretch. Each step (half the sphere's
        // width) is smaller than the float spacing near 1e6, so `t += step` never moved on and
        // the query spun for ever.
        Scene miss("miss3");
        GameObject* mw = miss.CreateGameObject("MissWall");
        mw->transform->localPosition = {1.0e6f, 0.5095f, 0.5095f};
        mw->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        miss.Start(); miss.Update(0.0f);
        h = miss.physics3D().SphereCast(miss, {0, 0, 0}, {1, 0, 0}, 0.01f, 2.0e6f);
        CHECK(!h.hit);
    }
    {
        Scene s("sweep2");
        GameObject* wall = s.CreateGameObject("Wall");
        wall->transform->localPosition = {10, 0, 0};
        wall->AddComponent<BoxCollider2D>()->size = {1, 1};
        s.Start(); s.Update(0.0f);
        Physics2D& phys = s.physics();
        RaycastHit2D h = phys.CircleCast(s, {0, 0}, {1, 0}, 0.5f, nan);
        CHECK(!h.hit);
        h = phys.CircleCast(s, {nan, 0}, {1, 0}, 0.5f, 100.0f);
        CHECK(!h.hit || std::isfinite(h.distance));

        GameObject* bad = s.CreateGameObject("NanBox");
        bad->transform->localPosition = {20, 0, 0};
        bad->AddComponent<BoxCollider2D>()->size = {nan, nan};
        h = phys.CircleCast(s, {0, 0}, {1, 0}, 0.5f, 100.0f);
        CHECK(h.hit && h.gameObject == wall);
        CHECK_NEAR(h.distance, 9.0f, 0.2f);

        Scene distant("far2");
        GameObject* fw = distant.CreateGameObject("FarWall");
        fw->transform->localPosition = {1.0e6f, 0, 0};
        fw->AddComponent<BoxCollider2D>()->size = {1, 1};
        distant.Start(); distant.Update(0.0f);
        h = distant.physics().CircleCast(distant, {0, 0}, {1, 0}, 0.5f, 2.0e6f);
        CHECK(h.hit && h.gameObject == fw);
        CHECK_NEAR(h.distance, 999999.0f, 1.0f);

    }

    TEST_MAIN_RESULT();
}
