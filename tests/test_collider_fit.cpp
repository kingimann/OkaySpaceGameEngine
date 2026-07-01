#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("collider_fit");

    // 2D: a box collider fits the sprite's size.
    {
        Scene s("Fit2D");
        GameObject* o = s.CreateGameObject("Spr");
        o->AddComponent<SpriteRenderer>()->size = {3.0f, 2.0f};
        auto* bc = o->AddComponent<BoxCollider2D>();   // default 1x1
        FitColliders(o);
        CHECK_NEAR(bc->size.x, 3.0f, 1e-4f);
        CHECK_NEAR(bc->size.y, 2.0f, 1e-4f);
    }

    // 2D circle fits to the larger half-extent.
    {
        Scene s("FitCircle");
        GameObject* o = s.CreateGameObject("Spr");
        o->AddComponent<SpriteRenderer>()->size = {4.0f, 2.0f};
        auto* cc = o->AddComponent<CircleCollider2D>();
        FitColliders(o);
        CHECK_NEAR(cc->radius, 2.0f, 1e-4f);           // max(4,2)/2
    }

    // 3D: a box collider fits the cube mesh bounds (1x1x1).
    {
        Scene s("Fit3D");
        GameObject* o = s.CreateGameObject("Cube");
        o->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
        auto* bc = o->AddComponent<BoxCollider3D>();
        FitColliders(o);
        CHECK_NEAR(bc->size.x, 1.0f, 1e-3f);
        CHECK_NEAR(bc->size.y, 1.0f, 1e-3f);
        CHECK_NEAR(bc->size.z, 1.0f, 1e-3f);
    }

    // 3D: after an edit makes the mesh OFF-CENTER (like an extrude), the collider's
    // WORLD center must land on the mesh's world center — not be shoved aside. This
    // is the "extrude breaks the collider" regression.
    {
        Scene s("OffCenter");
        GameObject* o = s.CreateGameObject("Box");
        Mesh m = Mesh::Cube();                          // centered [-0.5,0.5]
        for (Vec3& v : m.vertices) if (v.y > 0.0f) v.y += 1.0f;   // extrude the top up by 1
        o->AddComponent<MeshRenderer>()->mesh = m;      // bounds now y in [-0.5, 1.5], center y=0.5
        auto* bc = o->AddComponent<BoxCollider3D>();
        FitColliders(o);
        Vec3 mn, mx; bc->WorldAABB(mn, mx);
        CHECK_NEAR((mn.y + mx.y) * 0.5f, 0.5f, 1e-3f);  // collider centered on the mesh, not at 0
        CHECK_NEAR(mx.y, 1.5f, 1e-3f);                  // covers the extruded top
        CHECK_NEAR(mn.y, -0.5f, 1e-3f);                 // and the original base
    }

    // 3D: a SCALED, off-center object — the collider offset is world-space, so the
    // box must still wrap the scaled mesh (the case that broke on the scaled Ground).
    {
        Scene s("Scaled");
        GameObject* o = s.CreateGameObject("Ground");
        Mesh m = Mesh::Cube();
        for (Vec3& v : m.vertices) if (v.y > 0.0f) v.y += 1.0f;   // off-center in Y
        o->AddComponent<MeshRenderer>()->mesh = m;
        o->transform->localScale = {10.0f, 1.0f, 10.0f};
        o->transform->localPosition = {0, 0, 0};
        auto* bc = o->AddComponent<BoxCollider3D>();
        FitColliders(o);
        Vec3 mn, mx; bc->WorldAABB(mn, mx);
        CHECK_NEAR((mn.y + mx.y) * 0.5f, 0.5f, 1e-3f);  // Y unscaled → center still 0.5
        CHECK_NEAR((mx.x - mn.x), 10.0f, 1e-2f);        // X width scaled by 10
    }

    // 3D: a perfectly flat mesh gets a thin (non-zero) collider, not a zero slab.
    {
        Scene s("Flat");
        GameObject* o = s.CreateGameObject("Plane");
        o->AddComponent<MeshRenderer>()->mesh = Mesh::Quad(2.0f);   // zero thickness in Y
        auto* bc = o->AddComponent<BoxCollider3D>();
        FitColliders(o);
        CHECK(bc->size.y > 0.0f);                       // never a zero-thickness collider
    }

    // autoFit re-fits during Scene::Update when the sprite changes size.
    {
        Scene s("AutoFit");
        GameObject* o = s.CreateGameObject("Spr");
        auto* sr = o->AddComponent<SpriteRenderer>(); sr->size = {1.0f, 1.0f};
        auto* bc = o->AddComponent<BoxCollider2D>();
        bc->autoFit = true;
        s.Start();
        sr->size = {5.0f, 3.0f};
        s.Update(0.016f);                               // auto-fit pass runs
        CHECK_NEAR(bc->size.x, 5.0f, 1e-4f);
        CHECK_NEAR(bc->size.y, 3.0f, 1e-4f);
    }

    // autoFit survives a serialize round-trip.
    {
        Scene s("Persist");
        GameObject* o = s.CreateGameObject("Spr");
        o->AddComponent<SpriteRenderer>()->size = {2.0f, 2.0f};
        o->AddComponent<BoxCollider2D>()->autoFit = true;
        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        auto* bc = s2.Find("Spr") ? s2.Find("Spr")->GetComponent<BoxCollider2D>() : nullptr;
        CHECK(bc != nullptr);
        if (bc) CHECK(bc->autoFit == true);
    }

    TEST_MAIN_RESULT();
}
