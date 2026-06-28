#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// A script that counts 3D collision / trigger callbacks.
class Counter3D : public Behaviour {
public:
    int enter = 0, stay = 0, exit = 0, trigEnter = 0;
    void OnCollisionEnter3D(const Collision3D&) override { ++enter; }
    void OnCollisionStay3D(const Collision3D&) override { ++stay; }
    void OnCollisionExit3D(const Collision3D&) override { ++exit; }
    void OnTriggerEnter3D(Collider3D*) override { ++trigEnter; }
};

int main() {
    RUN_SUITE("physics3d");

    // ---- Gravity integration on a Rigidbody3D ----
    {
        Scene s("g");
        GameObject* o = s.CreateGameObject("Body");
        auto* rb = o->AddComponent<Rigidbody3D>();
        rb->gravityScale = 1.0f;
        s.Start();
        for (int i = 0; i < 10; ++i) s.Update(0.1f);   // 1 second total
        // v = g*t = -9.81; y ~ semi-implicit Euler sum < free-fall analytic.
        CHECK(rb->velocity.y < -9.0f);
        CHECK(o->transform->localPosition.y < 0.0f);
    }

    // ---- A dynamic sphere lands on a static box floor and stops ----
    {
        Scene s("land");
        GameObject* floor = s.CreateGameObject("Floor");
        floor->transform->localPosition = {0, 0, 0};
        auto* fb = floor->AddComponent<BoxCollider3D>();
        fb->size = {20, 1, 20};
        auto* frb = floor->AddComponent<Rigidbody3D>();
        frb->bodyType = Rigidbody3D::BodyType::Static;

        GameObject* ball = s.CreateGameObject("Ball");
        ball->transform->localPosition = {0, 5, 0};
        auto* sc = ball->AddComponent<SphereCollider3D>();
        sc->radius = 0.5f;
        ball->AddComponent<Rigidbody3D>();

        s.Start();
        for (int i = 0; i < 240; ++i) s.Update(1.0f / 60.0f); // 4 seconds

        // Rests on top of the floor: floor top y = 0.5, ball center ~ 0.5 + 0.5.
        CHECK_NEAR(ball->transform->localPosition.y, 1.0f, 0.15f);
        CHECK(ball->transform->localPosition.y > 0.5f); // didn't fall through
    }

    // ---- Trigger fires an enter callback, no physical push ----
    {
        Scene s("trig");
        GameObject* a = s.CreateGameObject("A");
        a->AddComponent<BoxCollider3D>();
        auto* counter = a->AddComponent<Counter3D>();

        GameObject* b = s.CreateGameObject("B");
        b->transform->localPosition = {0.2f, 0, 0};
        auto* bc = b->AddComponent<BoxCollider3D>();
        bc->isTrigger = true;

        s.Start();
        s.Update(1.0f / 60.0f);
        CHECK(counter->trigEnter == 1);
    }

    // ---- Sphere vs sphere overlap query ----
    {
        Scene s("ov");
        GameObject* a = s.CreateGameObject("A");
        a->AddComponent<SphereCollider3D>()->radius = 1.0f;
        GameObject* b = s.CreateGameObject("B");
        b->transform->localPosition = {1.5f, 0, 0};
        b->AddComponent<SphereCollider3D>()->radius = 1.0f;
        s.Start(); s.Update(0.0f);
        auto hits = s.physics3D().OverlapSphere(s, {0, 0, 0}, 0.1f);
        CHECK(hits.size() == 1); // only sphere A contains the origin region
    }

    // ---- Capsule collider serialization round-trips ----
    {
        Scene s("ser");
        GameObject* o = s.CreateGameObject("Cap");
        auto* cap = o->AddComponent<CapsuleCollider3D>();
        cap->radius = 0.4f; cap->height = 2.5f; cap->axis = 2; cap->isTrigger = true;
        std::string txt = SceneSerializer::Serialize(s);

        Scene s2("x");
        SceneSerializer::Deserialize(s2, txt);
        GameObject* o2 = s2.Find("Cap");
        CHECK(o2 != nullptr);
        auto* c2 = o2 ? o2->GetComponent<CapsuleCollider3D>() : nullptr;
        CHECK(c2 != nullptr);
        if (c2) {
            CHECK_NEAR(c2->radius, 0.4f, 1e-5f);
            CHECK_NEAR(c2->height, 2.5f, 1e-5f);
            CHECK(c2->axis == 2);
            CHECK(c2->isTrigger);
        }
    }

    // ---- Mesh + Cylinder colliders: overlap, resolve, round-trip ----
    {
        Scene s("col3d_new");
        // A mesh collider (box-like) sized 4x1x4 at the origin.
        GameObject* floor = s.CreateGameObject("MeshFloor");
        auto* mc = floor->AddComponent<MeshCollider3D>();
        mc->size = {4.0f, 1.0f, 4.0f};
        // A cylinder collider standing on it.
        GameObject* pillar = s.CreateGameObject("Pillar");
        pillar->transform->localPosition = {0, 2, 0};
        auto* cy = pillar->AddComponent<CylinderCollider3D>();
        cy->radius = 0.5f; cy->height = 2.0f; cy->isTrigger = true;
        s.Start(); s.Update(0.0f);

        // OverlapSphere at the mesh floor centre finds the mesh collider — this used to
        // invalid-cast a Mesh (box-like) to a capsule.
        auto hits = s.physics3D().OverlapSphere(s, {0, 0, 0}, 0.5f);
        bool foundMesh = false;
        for (auto* c : hits) if (c == mc) foundMesh = true;
        CHECK(foundMesh);

        // Depenetrate a sphere sitting inside the mesh floor — it gets pushed out (up).
        Vec3 fixed = s.physics3D().ResolveSphere(s, Vec3{0, 0.2f, 0}, 0.5f, nullptr, 4);
        CHECK(fixed.y > 0.2f);

        // Round-trip both new collider types.
        Scene loaded("L");
        CHECK(SceneSerializer::Deserialize(loaded, SceneSerializer::Serialize(s)));
        auto* lm = loaded.Find("MeshFloor") ? loaded.Find("MeshFloor")->GetComponent<MeshCollider3D>() : nullptr;
        auto* lc = loaded.Find("Pillar") ? loaded.Find("Pillar")->GetComponent<CylinderCollider3D>() : nullptr;
        CHECK(lm != nullptr);
        CHECK(lc != nullptr);
        if (lm) CHECK_NEAR(lm->size.x, 4.0f, 1e-4f);
        if (lc) { CHECK_NEAR(lc->radius, 0.5f, 1e-4f); CHECK(lc->isTrigger); }
    }

    // ---- 2D capsule collider: overlaps a circle, round-trips ----
    {
        Scene s("cap2d");
        GameObject* cap = s.CreateGameObject("Cap2D");
        auto* cc = cap->AddComponent<CapsuleCollider2D>();
        cc->size = {1.0f, 3.0f};
        GameObject* hits = nullptr; (void)hits;
        s.Start(); s.Update(0.0f);
        // A point along the capsule's tall axis is inside it.
        Collider2D* at = s.physics().OverlapPoint(s, {0.0f, 1.0f});
        CHECK(at == cc);

        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("y");
        SceneSerializer::Deserialize(s2, txt);
        auto* c2 = s2.Find("Cap2D") ? s2.Find("Cap2D")->GetComponent<CapsuleCollider2D>() : nullptr;
        CHECK(c2 != nullptr);
        if (c2) CHECK_NEAR(c2->size.y, 3.0f, 1e-5f);
    }

    TEST_MAIN_RESULT();
}
