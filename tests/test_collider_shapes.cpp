// Colliders must behave as the shapes they are, not as boxes. Every expected number below is the analytic answer for the
// shape (a flat cylinder top is at half the height, a cone's width shrinks linearly to its tip, ...).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Physics/ColliderShapes3D.hpp"
#include "okay/Physics/ColliderShapes2D.hpp"
#include "okay/Components/PhysicsSurface.hpp"
#include <cmath>

using namespace okay;

namespace {

GameObject* Obj(Scene& s, const char* name, Vec3 p) { GameObject* g = s.CreateGameObject(name); g->transform->localPosition = p; return g; }

GameObject* Floor(Scene& s) {
    GameObject* f = Obj(s, "Floor", {0, -0.5f, 0});
    f->AddComponent<BoxCollider3D>()->size = {60, 1, 60};
    f->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    return f;
}
void Settle(Scene& s, int steps) { for (int i = 0; i < steps; ++i) s.Update(1.0f / 60.0f); }
float MinY(Collider3D* c) { Vec3 a, b; c->WorldAABB(a, b); return a.y; }

class Enter3D : public Behaviour { public: int n = 0; void OnTriggerEnter3D(Collider3D*) override { ++n; } };

}  // namespace

int main() {
    RUN_SUITE("collider_shapes");
    Log::console = false;

    // ============================== 3D: the old complaints ==============================
    {   // A capsule turns with its object: lying on its side it is long in X, so a ray above its flank misses.
        Scene s("capsule");
        GameObject* g = Obj(s, "Cap", {0, 0, 0});
        auto* cap = g->AddComponent<CapsuleCollider3D>(); cap->radius = 0.5f; cap->height = 2.0f;
        g->transform->localRotation = Quat::Euler(0, 0, 90);
        s.Start();
        auto miss = s.physics3D().Raycast(s, {3, 0.8f, 0}, {-1, 0, 0}, 10);
        CHECK(!miss.hit);                                       // upright it would be hit here (its cap reaches y = 1)
        auto hit = s.physics3D().Raycast(s, {3, 0, 0}, {-1, 0, 0}, 10);
        CHECK(hit.hit); CHECK_NEAR(hit.point.x, 1.0f, 0.01f);   // the tip, half the height out
    }
    {   // A cylinder has flat ends and sharp rims: its top is exactly at half the height (it used to reach past it).
        Scene s("cyl");
        GameObject* g = Obj(s, "Cyl", {0, 0, 0});
        auto* cy = g->AddComponent<CylinderCollider3D>(); cy->radius = 0.5f; cy->height = 2.0f;
        s.Start();
        auto top = s.physics3D().Raycast(s, {0.45f, 5, 0}, {0, -1, 0}, 10);
        CHECK(top.hit); CHECK_NEAR(top.point.y, 1.0f, 0.01f); CHECK_NEAR(top.normal.y, 1.0f, 0.01f);
        Vec3 mn, mx; cy->WorldAABB(mn, mx);
        CHECK_NEAR(mx.y, 1.0f, 0.01f); CHECK_NEAR(mx.x, 0.5f, 0.01f);
        // Near the rim corner of its bounding box but not touching: no overlap (the old rounded ends said yes).
        CHECK(s.physics3D().OverlapSphere(s, {0.6f, 1.1f, 0}, 0.1f).empty());
        CHECK(!s.physics3D().OverlapSphere(s, {0.6f, 0.9f, 0}, 0.15f).empty());   // beside the flat side: touching
        Vec3 c, in; float vol;
        CHECK(cy->MassShape(c, vol, in));
        CHECK_NEAR(vol, 3.14159265f * 0.25f * 2.0f, 1e-3f);
        CHECK_NEAR(in.y, 0.5f * 0.25f, 1e-4f);                   // about its own axis: r^2 / 2
        CHECK_NEAR(in.x, (3.0f * 0.25f + 4.0f) / 12.0f, 1e-4f);
    }
    {   // A long capsule leaning on a big floor touches at its low end, not at the point nearest the floor's middle.
        Scene s("slant");
        Floor(s);
        GameObject* g = Obj(s, "Cap", {8, 1.5f, 0});
        auto* cap = g->AddComponent<CapsuleCollider3D>(); cap->radius = 0.3f; cap->height = 3.0f;
        g->transform->localRotation = Quat::Euler(0, 0, 50);          // the +Y end leans toward -X, the low end is far (+X) side
        auto* rb = g->AddComponent<Rigidbody3D>(); rb->freezeRotation = true;
        s.Start();
        Settle(s, 240);
        CHECK_NEAR(MinY(cap), 0.0f, 0.04f);                           // its lowest point sits on the floor, not sunk into it
    }
    {   // A ball does not collide at the corner of a cone's bounding box.
        Scene s("cone");
        GameObject* g = Obj(s, "Cone", {0, 0, 0});
        auto* co = g->AddComponent<ConeCollider3D>(); co->radius = 0.5f; co->height = 1.0f;
        s.Start();
        CHECK(s.physics3D().OverlapSphere(s, {0.45f, 0.45f, 0}, 0.1f).empty());     // by the tip corner of the box: empty air
        CHECK(!s.physics3D().OverlapSphere(s, {0.45f, -0.45f, 0}, 0.1f).empty());   // by the base rim: solid
        auto tip = s.physics3D().Raycast(s, {0, 5, 0}, {0, -1, 0}, 10);
        CHECK(tip.hit); CHECK_NEAR(tip.point.y, 0.5f, 0.01f);
        auto side = s.physics3D().Raycast(s, {0.4f, 5, 0}, {0, -1, 0}, 10);          // radius 0.4 is 0.8 of the way down
        CHECK(side.hit); CHECK_NEAR(side.point.y, -0.3f, 0.01f);
        CHECK(side.normal.x > 0.5f && side.normal.y > 0.2f);                          // leaning outward and up
        Vec3 c, in; float vol; CHECK(co->MassShape(c, vol, in));
        CHECK_NEAR(vol, 3.14159265f * 0.25f / 3.0f, 1e-3f);
        CHECK_NEAR(c.y, -0.25f, 1e-4f);                                               // centre of mass: a quarter up from the base
    }

    // ============================== 3D: the new shapes ==============================
    {   // Convex hull from points (an octahedron): rays and support are the real shape.
        Scene s("hull");
        GameObject* g = Obj(s, "Hull", {0, 0, 0});
        auto* h = g->AddComponent<ConvexHullCollider3D>();
        h->SetPoints({{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}});
        CHECK((h->PointCount()) == (6));
        s.Start();
        auto tip = s.physics3D().Raycast(s, {0, 5, 0}, {0, -1, 0}, 10);
        CHECK(tip.hit); CHECK_NEAR(tip.point.y, 1.0f, 0.01f);
        auto face = s.physics3D().Raycast(s, {0.25f, 5, 0.25f}, {0, -1, 0}, 10);     // the face plane x + y + z = 1
        CHECK(face.hit); CHECK_NEAR(face.point.y, 0.5f, 0.01f);
        CHECK(s.physics3D().OverlapSphere(s, {0.8f, 0.8f, 0.8f}, 0.2f).empty());   // the box corner of the octahedron is empty
        CHECK(!s.physics3D().OverlapSphere(s, {0.4f, 0.4f, 0.4f}, 0.2f).empty());
        Vec3 c, in; float vol; CHECK(h->MassShape(c, vol, in));
        CHECK_NEAR(vol, 4.0f / 3.0f, 1e-3f);                                          // octahedron: 4/3 a^3 with a = 1
        CHECK_NEAR(in.x, in.y, 1e-3f); CHECK_NEAR(in.y, in.z, 1e-3f);
    }
    {   // Wrap a hull around a mesh.
        Scene s("hullmesh");
        GameObject* g = Obj(s, "Hull", {0, 0, 0});
        auto* h = g->AddComponent<ConvexHullCollider3D>();
        Mesh m = Mesh::Cube();
        CHECK(h->SetFromMesh(m));
        CHECK((h->PointCount()) == (8));
        Vec3 mn, mx; h->WorldAABB(mn, mx);
        CHECK_NEAR(mx.x - mn.x, 1.0f, 0.01f);
    }
    {   // Compound: an L made of two boxes; a ball dropped into the notch reaches the notch floor (a box would stop it high up).
        Scene s("compound");
        Floor(s);
        GameObject* g = Obj(s, "L", {0, 1.0f, 0});
        auto* c = g->AddComponent<CompoundCollider3D>();
        c->AddBox({0, 0, 0}, {4, 2, 2});          // the long bar, x -2 .. 2, y 0 .. 2 (centred at y = 1)
        c->AddBox({-1.5f, 1.5f, 0}, {1, 1, 2});   // a tower on its left end, up to y = 3 in world
        g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        CHECK((c->PartCount()) == (2));
        GameObject* b = Obj(s, "Ball", {1.0f, 6, 0});
        auto* sp = b->AddComponent<SphereCollider3D>(); sp->radius = 0.5f;
        b->AddComponent<Rigidbody3D>();
        s.Start();
        Settle(s, 300);
        CHECK_NEAR(b->transform->Position().y, 2.5f, 0.1f);   // on the bar (top y = 2), beside the tower: not up at the box top (y = 3.5)
        auto r = s.physics3D().Raycast(s, {1.0f, 10, 0}, {0, -1, 0}, 20, b);
        CHECK(r.hit); CHECK_NEAR(r.point.y, 2.0f, 0.01f);
        auto r2 = s.physics3D().Raycast(s, {-1.5f, 10, 0}, {0, -1, 0}, 20, b);
        CHECK(r2.hit); CHECK_NEAR(r2.point.y, 3.0f, 0.01f);
    }
    {   // Plane: a ball rests at its radius; a tilted plane rolls it away.
        Scene s("plane");
        GameObject* p = Obj(s, "Ground", {0, 0, 0});
        p->AddComponent<PlaneCollider3D>();
        GameObject* b = Obj(s, "Ball", {0, 4, 0});
        b->AddComponent<SphereCollider3D>()->radius = 0.5f;
        b->AddComponent<Rigidbody3D>();
        s.Start();
        Settle(s, 240);
        CHECK_NEAR(b->transform->Position().y, 0.5f, 0.06f);
        auto r = s.physics3D().Raycast(s, {3, 9, -2}, {0, -1, 0}, 20, b);
        CHECK(r.hit); CHECK_NEAR(r.point.y, 0.0f, 0.01f);
        CHECK(!s.physics3D().Raycast(s, {3, -2, -2}, {0, -1, 0}, 20, b).hit);          // from below the ground: inside the solid
    }
    {   // Tilted plane: a ball rolls downhill.
        Scene s("tilt");
        GameObject* p = Obj(s, "Ground", {0, 0, 0});
        p->transform->localRotation = Quat::Euler(0, 0, 20);        // rises toward -X
        p->AddComponent<PlaneCollider3D>();
        GameObject* b = Obj(s, "Ball", {0, 2, 0});
        b->AddComponent<SphereCollider3D>()->radius = 0.5f;
        b->AddComponent<Rigidbody3D>();
        s.Start();
        Settle(s, 120);
        CHECK(b->transform->Position().x > 1.0f);
    }
    {   // Heightfield: a ramp. The ball rests on the surface; a ray lands at the right height.
        Scene s("hf");
        GameObject* g = Obj(s, "Ground", {0, 0, 0});
        auto* hf = g->AddComponent<HeightfieldCollider3D>();
        hf->SetHeights(3, 3, {0, 1, 2,  0, 1, 2,  0, 1, 2});         // rises 1 per 1 metre along +X (cells of 4 m: slope 0.25)
        hf->cellX = 4.0f; hf->cellZ = 4.0f;
        s.Start();
        float y; CHECK(hf->LocalHeight(0.0f, 0.0f, y)); CHECK_NEAR(y, 1.0f, 1e-4f);
        CHECK(hf->LocalHeight(2.0f, 0.0f, y)); CHECK_NEAR(y, 1.5f, 1e-4f);
        auto r = s.physics3D().Raycast(s, {2.0f, 9, 1.0f}, {0, -1, 0}, 20);
        CHECK(r.hit); CHECK_NEAR(r.point.y, 1.5f, 0.02f); CHECK(r.normal.x < -0.1f && r.normal.y > 0.9f);
        GameObject* b = Obj(s, "Ball", {0.0f, 6, 0});
        b->AddComponent<SphereCollider3D>()->radius = 0.5f;
        auto* rb = b->AddComponent<Rigidbody3D>(); rb->freezeRotation = true; rb->drag = 5.0f;
        Settle(s, 360);
        // Resting on (or sliding down) the slope: always above the surface by about its radius, never inside the ground.
        float surf; hf->LocalHeight(b->transform->Position().x, b->transform->Position().z, surf);
        CHECK(b->transform->Position().y > surf + 0.3f);
        CHECK(b->transform->Position().y < surf + 1.0f);
    }
    {   // A cylinder stands on its flat end; a cone stands on its base; both rest with the flat face on the floor.
        Scene s("stand");
        Floor(s);
        GameObject* c = Obj(s, "Cyl", {-3, 3, 0});
        auto* cy = c->AddComponent<CylinderCollider3D>(); cy->radius = 0.5f; cy->height = 2.0f;
        c->AddComponent<Rigidbody3D>()->freezeRotation = true;
        GameObject* k = Obj(s, "Cone", {3, 3, 0});
        auto* co = k->AddComponent<ConeCollider3D>(); co->radius = 0.6f; co->height = 1.5f;
        k->AddComponent<Rigidbody3D>()->freezeRotation = true;
        s.Start();
        Settle(s, 300);
        CHECK_NEAR(MinY(cy), 0.0f, 0.04f);
        CHECK_NEAR(MinY(co), 0.0f, 0.04f);
        CHECK_NEAR(c->transform->Position().y, 1.0f, 0.05f);
    }
    {   // Contact offset: the collider counts as thicker, so a box rests that far above the floor.
        Scene s("skin");
        Floor(s);
        GameObject* b = Obj(s, "Box", {0, 3, 0});
        auto* bx = b->AddComponent<BoxCollider3D>(); bx->contactOffset = 0.1f;
        b->AddComponent<Rigidbody3D>()->freezeRotation = true;
        s.Start();
        Settle(s, 240);
        CHECK(MinY(bx) > 0.06f); CHECK(MinY(bx) < 0.14f);
    }
    {   // A trigger cone reports an enter, with no push.
        Scene s("trig");
        GameObject* t = Obj(s, "Zone", {0, 0, 0});
        auto* co = t->AddComponent<ConeCollider3D>(); co->radius = 1.0f; co->height = 2.0f; co->isTrigger = true;
        auto* probe = t->AddComponent<Enter3D>();
        GameObject* b = Obj(s, "Ball", {0, 4, 0});
        b->AddComponent<SphereCollider3D>()->radius = 0.3f;
        auto* rb = b->AddComponent<Rigidbody3D>();
        s.Start();
        Settle(s, 120);
        CHECK(probe->n >= 1);
        CHECK(rb->velocity.y < -3.0f || b->transform->Position().y < -1.0f);   // fell straight through: a trigger never pushes
    }
    {   // The Physics Material hooks up to the new shapes: a box slides much further on an icy Plane.
        auto slide = [](const char* mat) {
            Scene s("ice"); s.physics3D().quality = PhysicsQuality::Improved;
            GameObject* p = Obj(s, "Ground", {0, 0, 0});
            p->AddComponent<PlaneCollider3D>();
            if (mat) AssignPhysicsMaterial(*p, mat);
            GameObject* b = Obj(s, "Box", {0, 0.5f, 0});
            b->AddComponent<BoxCollider3D>();
            auto* rb = b->AddComponent<Rigidbody3D>(); rb->freezeRotation = true;
            s.Start();
            Settle(s, 30);
            rb->velocity.x = 6.0f;
            float x0 = b->transform->Position().x;
            Settle(s, 90);
            return b->transform->Position().x - x0;
        };
        float plain = slide(nullptr), ice = slide("Ice");
        CHECK(plain < 4.5f); CHECK(ice > plain * 1.5f);
    }
    {   // A cylinder dropped on a static triangle mesh floor rests on its flat end (the mesh path handles the new shapes).
        Scene s("meshfloor");
        GameObject* f = Obj(s, "Floor", {0, 0, 0});
        auto* mr = f->AddComponent<MeshRenderer>(); mr->mesh = Mesh::Cube(); f->transform->localScale = {20, 1, 20};
        f->AddComponent<MeshCollider3D>();
        GameObject* c = Obj(s, "Cyl", {0, 3, 0});
        auto* cy = c->AddComponent<CylinderCollider3D>(); cy->radius = 0.4f; cy->height = 1.0f;
        c->AddComponent<Rigidbody3D>()->freezeRotation = true;
        s.Start();
        Settle(s, 300);
        CHECK_NEAR(MinY(cy), 0.5f, 0.06f);
    }
    {   // Auto fit: Cone / Convex Hull follow the mesh; the generic shapes read the mesh bounds like the others.
        Scene s("fit");
        GameObject* g = Obj(s, "Thing", {0, 0, 0});
        auto* mr = g->AddComponent<MeshRenderer>(); mr->mesh = Mesh::Cube();
        auto* co = g->AddComponent<ConeCollider3D>();
        auto* h = g->AddComponent<ConvexHullCollider3D>();
        FitColliders(g);
        CHECK_NEAR(co->radius, 0.5f, 0.01f); CHECK_NEAR(co->height, 1.0f, 0.01f);
        CHECK((h->PointCount()) == (8));
    }

    // ============================== saved scenes ==============================
    {   // An older scene (box, sphere, capsule, cylinder only) saves exactly as before: nothing new is written.
        Scene s("old");
        GameObject* g = Obj(s, "A", {0, 0, 0});
        g->AddComponent<BoxCollider3D>(); g->AddComponent<SphereCollider3D>();
        GameObject* c = Obj(s, "B", {1, 0, 0});
        c->AddComponent<CapsuleCollider3D>(); c->AddComponent<CylinderCollider3D>();
        GameObject* d = Obj(s, "C", {2, 0, 0});
        d->AddComponent<BoxCollider2D>(); d->AddComponent<CircleCollider2D>(); d->AddComponent<PolygonCollider2D>();
        std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("colskin") == std::string::npos);
        CHECK(text.find("compoundcollider") == std::string::npos);
        CHECK(text.find("conecollider") == std::string::npos);
        CHECK(text.find("convexhull") == std::string::npos);
    }
    {   // Every new collider round-trips with its settings, and the older by-index records still point at the right collider.
        Scene s("rt");
        GameObject* g = Obj(s, "Thing", {0, 0, 0});
        auto* bx = g->AddComponent<BoxCollider3D>(); bx->friction = 0.2f; bx->contactOffset = 0.03f;
        auto* co = g->AddComponent<ConeCollider3D>(); co->radius = 0.7f; co->height = 2.5f; co->axis = 2; co->isTrigger = true; co->friction = 0.9f;
        auto* h = g->AddComponent<ConvexHullCollider3D>(); h->SetPoints({{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}});
        auto* cp = g->AddComponent<CompoundCollider3D>(); cp->AddBox({0, 1, 0}, {1, 2, 3}); cp->AddCapsule({1, 0, 0}, 0.25f, 2.0f, 0, {0, 0, 30});
        g->AddComponent<PlaneCollider3D>()->bounciness = 0.5f;
        auto* hf = g->AddComponent<HeightfieldCollider3D>(); hf->SetHeights(2, 3, {0, 1, 2, 3, 4, 5}); hf->cellX = 2; hf->heightScale = 2;
        auto* cmp2 = g->AddComponent<CompositeCollider2D>(); cmp2->AddCircle({0, 1}, 0.5f); cmp2->AddBox({1, 0}, {1, 2}, 15); cmp2->contactOffset = 0.02f;
        auto* poly = g->AddComponent<PolygonCollider2D>(); poly->friction = 0.3f;
        std::string text = SceneSerializer::Serialize(s);
        Scene t("rt2"); std::string err;
        CHECK(SceneSerializer::Deserialize(t, text, &err, nullptr));
        GameObject* g2 = t.Find("Thing");
        CHECK(g2 != nullptr);
        if (g2) {
            auto* bx2 = g2->GetComponent<BoxCollider3D>();
            CHECK(bx2 && bx2->friction == 0.2f && std::abs(bx2->contactOffset - 0.03f) < 1e-6f);
            auto* co2 = g2->GetComponent<ConeCollider3D>();
            CHECK(co2 && co2->radius == 0.7f && co2->height == 2.5f && co2->axis == 2 && co2->isTrigger && co2->friction == 0.9f);
            auto* h2 = g2->GetComponent<ConvexHullCollider3D>(); CHECK(h2 && h2->PointCount() == 6);
            auto* cp2 = g2->GetComponent<CompoundCollider3D>(); CHECK(cp2 && cp2->PartCount() == 2);
            CHECK(g2->GetComponent<PlaneCollider3D>() && g2->GetComponent<PlaneCollider3D>()->bounciness == 0.5f);
            auto* hf2 = g2->GetComponent<HeightfieldCollider3D>(); CHECK(hf2 && hf2->columns == 2 && hf2->rows == 3 && hf2->heightScale == 2.0f && hf2->HeightAt(1, 2) == 10.0f);
            auto* cm = g2->GetComponent<CompositeCollider2D>(); CHECK(cm && cm->parts == cmp2->parts && std::abs(cm->contactOffset - 0.02f) < 1e-6f);
            auto* pg = g2->GetComponent<PolygonCollider2D>(); CHECK(pg && pg->friction == 0.3f);
        }
        CHECK(SceneSerializer::Serialize(t) == text);
    }

    // ============================== 2D ==============================
    {   // A capsule turns with its object: lying down it is long in X (the ray above its flank misses; the old one stayed upright).
        Scene s("cap2d");
        GameObject* g = Obj(s, "Cap", {0, 0, 0});
        auto* cap = g->AddComponent<CapsuleCollider2D>(); cap->size = {1, 2};
        g->transform->localRotation = Quat::Euler(0, 0, 90);
        s.Start();
        CHECK(!s.physics().Raycast(s, {3, 0.8f}, {-1, 0}, 10).hit);
        auto h = s.physics().Raycast(s, {3, 0}, {-1, 0}, 10);
        CHECK(h.hit); CHECK_NEAR(h.point.x, 1.0f, 0.01f);
    }
    {   // A polygon is the real triangle: rays and points use it, not its bounding box, and it is solid inside.
        Scene s("poly2d");
        GameObject* g = Obj(s, "Tri", {0, 0, 0});
        auto* pc = g->AddComponent<PolygonCollider2D>(); pc->points = {{-1, -1}, {1, -1}, {0, 1}};
        s.Start();
        auto h = s.physics().Raycast(s, {-3, 0.9f}, {1, 0}, 10);       // near the apex: the triangle there is only 0.1 wide
        CHECK(h.hit); CHECK_NEAR(h.point.x, -0.05f, 0.01f);
        CHECK(!s.physics().Raycast(s, {-3, 0.9f}, {1, 0}, 2.5f).hit);   // (the box corner is at x = -1, not reached)
        CHECK(s.physics().OverlapPoint(s, {0, -0.5f}) == pc);           // inside the solid
        CHECK(s.physics().OverlapPoint(s, {0.9f, 0.9f}) == nullptr);    // the empty corner of its bounding box
        auto below = s.physics().Raycast(s, {0, -5}, {0, 1}, 10);
        CHECK(below.hit); CHECK_NEAR(below.point.y, -1.0f, 0.01f);
    }
    {   // A polygon turns with its object.
        Scene s("poly2dr");
        GameObject* g = Obj(s, "Tri", {0, 0, 0});
        auto* pc = g->AddComponent<PolygonCollider2D>(); pc->points = {{-1, -1}, {1, -1}, {0, 1}};
        g->transform->localRotation = Quat::Euler(0, 0, 180);              // apex down
        s.Start();
        CHECK(s.physics().OverlapPoint(s, {0, 0.5f}) == pc);
        CHECK(s.physics().OverlapPoint(s, {0, -0.9f}) == nullptr);
    }
    {   // A ball dropped on a static solid polygon lands on it; one placed inside it is pushed out.
        Scene s("ball2d");
        GameObject* g = Obj(s, "Ground", {0, 0, 0});
        auto* pc = g->AddComponent<PolygonCollider2D>(); pc->points = {{-6, -2}, {6, -2}, {6, 0}, {-6, 0}};
        g->AddComponent<Rigidbody2D>()->bodyType = Rigidbody2D::BodyType::Static;
        GameObject* b = Obj(s, "Ball", {1, 3, 0});
        b->AddComponent<CircleCollider2D>()->radius = 0.5f; b->AddComponent<Rigidbody2D>();
        GameObject* in = Obj(s, "Inside", {-3, -1.2f, 0});
        in->AddComponent<CircleCollider2D>()->radius = 0.3f;
        auto* rbi = in->AddComponent<Rigidbody2D>(); rbi->gravityScale = 0.0f;
        s.Start();
        Settle(s, 240);
        CHECK_NEAR(b->transform->Position().y, 0.5f, 0.1f);
        CHECK(in->transform->Position().y > 0.0f);                          // out through the top, the nearest face
        (void)pc;
    }
    {   // Composite: a plus sign. A point in an empty corner is clear; points on the arms are solid; the ray hits the arm.
        Scene s("comp2d");
        GameObject* g = Obj(s, "Plus", {0, 0, 0});
        auto* c = g->AddComponent<CompositeCollider2D>();
        c->AddBox({0, 0}, {3, 1}); c->AddBox({0, 0}, {1, 3});
        s.Start();
        CHECK(s.physics().OverlapPoint(s, {1.2f, 1.2f}) == nullptr);
        CHECK(s.physics().OverlapPoint(s, {1.2f, 0}) == c);
        CHECK(s.physics().OverlapPoint(s, {0, -1.2f}) == c);
        auto h = s.physics().Raycast(s, {5, 1.0f}, {-1, 0}, 10);          // passes along the notch; hits the vertical arm's edge at x = 0.5
        CHECK(h.hit); CHECK_NEAR(h.point.x, 0.5f, 0.01f);
        auto h2 = s.physics().Raycast(s, {5, 0.2f}, {-1, 0}, 10);
        CHECK(h2.hit); CHECK_NEAR(h2.point.x, 1.5f, 0.01f);
    }
    {   // A capsule over a box corner: the overlap query uses the capsule's real outline, not its bounding box.
        Scene s("capbox");
        GameObject* g = Obj(s, "Cap", {0, 0, 0});
        auto* cap = g->AddComponent<CapsuleCollider2D>(); cap->size = {1, 3};
        g->transform->localRotation = Quat::Euler(0, 0, 45);
        s.Start();
        CHECK(s.physics().OverlapBox(s, {1.15f, 1.15f}, {0.1f, 0.1f}).empty());     // the empty corner of the rotated capsule's box
        CHECK(!s.physics().OverlapBox(s, {0.6f, 0.6f}, {0.1f, 0.1f}).empty());
    }
    {   // 2D contact offset and the capsule's turning inertia.
        Scene s("skin2d");
        GameObject* f = Obj(s, "Floor", {0, -0.5f, 0});
        f->AddComponent<BoxCollider2D>()->size = {40, 1}; f->AddComponent<Rigidbody2D>()->bodyType = Rigidbody2D::BodyType::Static;
        GameObject* b = Obj(s, "Box", {0, 2, 0});
        auto* bx = b->AddComponent<BoxCollider2D>(); bx->contactOffset = 0.1f;
        auto* rb = b->AddComponent<Rigidbody2D>(); rb->freezeRotation = true;
        s.Start();
        Settle(s, 200);
        Vec2 mn, mx; bx->WorldAABB(mn, mx);
        CHECK(mn.y > 0.06f); CHECK(mn.y < 0.14f);
    }

    TEST_MAIN_RESULT();
}
