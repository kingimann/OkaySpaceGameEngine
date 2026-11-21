// Collider shapes under bad input: zero sizes, NaN / huge numbers, tiny or flat point clouds, empty or over-large
// compounds and grids, odd grid sizes, scaled shapes, older scene files, and the order things are created in.
// Each expected number is the geometric answer for the shape.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Physics/ColliderShapes3D.hpp"
#include <cmath>
#include <limits>
#include <sstream>

using namespace okay;

namespace {
GameObject* Obj(Scene& s, const char* name, Vec3 p) { GameObject* g = s.CreateGameObject(name); g->transform->localPosition = p; return g; }
bool AllFinite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool BoundsOk(Collider3D* c) { Vec3 a, b; c->WorldAABB(a, b); return AllFinite(a) && AllFinite(b) && a.x <= b.x && a.y <= b.y && a.z <= b.z; }
void Settle(Scene& s, int n) { for (int i = 0; i < n; ++i) s.Update(1.0f / 60.0f); }
const float NaN = std::numeric_limits<float>::quiet_NaN();
}

int main() {
    RUN_SUITE("collider_hardening");
    Log::console = false;

    {   // Convex hull from text: words that are not numbers ("nan", "inf" do not parse) and absurdly huge points are
        // ignored; the rest still makes the right solid. (Real NaN / infinity values are fed through SetPoints below.)
        Scene s("hull_bad");
        GameObject* g = Obj(s, "H", {0, 0, 0});
        auto* h = g->AddComponent<ConvexHullCollider3D>();
        h->points = "1 0 0; -1 0 0; 0 1 0; 0 -1 0; 0 0 1; 0 0 -1; nan 0 0; inf 1 1; 1e30 0 0; 0 -1e30 0";
        CHECK(h->PointCount() == 6);
        s.Start();
        CHECK(BoundsOk(h));
        Vec3 a, b; h->WorldAABB(a, b); CHECK_NEAR(b.x - a.x, 2.0f, 0.01f);
        auto r = s.physics3D().Raycast(s, {0, 5, 0}, {0, -1, 0}, 10);
        CHECK(r.hit); CHECK_NEAR(r.point.y, 1.0f, 0.01f);
    }
    {   // Convex hull with fewer than 4 points, on a line, or on a plane: no crash, finite bounds.
        const char* sets[] = {"", "1 2 3", "0 0 0; 1 0 0", "0 0 0; 1 0 0; 0 1 0", "0 0 0; 1 0 0; 2 0 0; 3 0 0", "0 0 0; 1 0 0; 0 0 1; 1 0 1; 0.5 0 0.5"};
        for (const char* t : sets) {
            Scene s("hull_few");
            GameObject* g = Obj(s, "H", {0, 0, 0});
            auto* h = g->AddComponent<ConvexHullCollider3D>(); h->points = t;
            GameObject* f = Obj(s, "Ball", {0.3f, 4, 0.3f});
            f->AddComponent<SphereCollider3D>()->radius = 0.2f; f->AddComponent<Rigidbody3D>();
            s.Start();
            CHECK(BoundsOk(h));
            Vec3 c, in; float v = 0;
            bool m = h->MassShape(c, v, in);
            CHECK(!m || (v >= 0.0f && std::isfinite(v) && AllFinite(in) && AllFinite(c)));
            (void)s.physics3D().Raycast(s, {0.3f, 5, 0.3f}, {0, -1, 0}, 10);
            Settle(s, 30);
            CHECK(AllFinite(f->transform->Position()));
        }
    }
    {   // A coplanar cloud keeps its true (flat) extent.
        Scene s("hull_flat");
        GameObject* g = Obj(s, "H", {0, 0, 0});
        auto* h = g->AddComponent<ConvexHullCollider3D>(); h->points = "0 0 0; 2 0 0; 0 0 2; 2 0 2; 1 0 1";
        s.Start();
        Vec3 a, b; h->WorldAABB(a, b);
        CHECK_NEAR(b.x - a.x, 2.0f, 0.01f); CHECK_NEAR(b.y - a.y, 0.0f, 0.01f);
    }
    {   // A hull never keeps an unbounded number of points (memory), and the kept points still span the cloud: the
        // bounds stay within a small distance of the cloud's own extent (thinning keeps the far-out points).
        std::ostringstream o;
        Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (int i = 0; i < 50000; ++i) {
            Vec3 v{std::cos(i * 0.01f), std::sin(i * 0.013f), std::cos(i * 0.007f)};
            o << (i ? "; " : "") << v.x << ' ' << v.y << ' ' << v.z;
            lo = Vec3::Min(lo, v); hi = Vec3::Max(hi, v);
        }
        Scene s("hull_big");
        auto* h = Obj(s, "H", {0, 0, 0})->AddComponent<ConvexHullCollider3D>(); h->points = o.str();
        CHECK(h->PointCount() <= 4096);
        CHECK(h->PointCount() >= 4);
        s.Start(); CHECK(BoundsOk(h));
        Vec3 a, b; h->WorldAABB(a, b);
        CHECK_NEAR(a.x, lo.x, 0.02f); CHECK_NEAR(a.y, lo.y, 0.02f); CHECK_NEAR(a.z, lo.z, 0.02f);
        CHECK_NEAR(b.x, hi.x, 0.02f); CHECK_NEAR(b.y, hi.y, 0.02f); CHECK_NEAR(b.z, hi.z, 0.02f);
    }
    {   // Non-convex input (a flat star with a dent): the hull is the convex wrap, so the dent is filled.
        Scene s("hull_nonconvex");
        auto* h = Obj(s, "H", {0, 0, 0})->AddComponent<ConvexHullCollider3D>();
        h->points = "2 0 0; -2 0 0; 0 0 2; 0 0 -2; 0.2 0 0.2; 0 1 0; 0 -1 0";
        s.Start();
        // The wrap passes through (2,0,0) and (0,0,2): at x = 0.4 its edge in z is 1.6 (at y = 0).
        auto r = s.physics3D().Raycast(s, {0.4f, 0.0f, 3}, {0, 0, -1}, 10);
        CHECK(r.hit); CHECK_NEAR(r.point.z, 1.6f, 0.02f);
    }

    {   // Compound with zero children, junk text children ("nan" / "inf" do not parse as numbers), or a child that names
        // another compound: collides with nothing. (Regression guard: these already held before the hardening.)
        const char* sets[] = {"", ";;;", "compound 0 0 0 0 0 0 1 1 1 1", "box 0 0", "sphere nan 0 0 0 0 0 1", "sphere 0 0 0 0 0 0 inf"};
        for (const char* t : sets) {
            Scene s("cmp0");
            auto* c = Obj(s, "C", {0, 0, 0})->AddComponent<CompoundCollider3D>(); c->parts = t;
            GameObject* f = Obj(s, "Ball", {0, 3, 0});
            f->AddComponent<SphereCollider3D>()->radius = 0.2f; f->AddComponent<Rigidbody3D>();
            s.Start();
            CHECK(BoundsOk(c));
            Vec3 cm, in; float v;
            CHECK(!c->MassShape(cm, v, in) || (v > 0 && std::isfinite(v)));
            Settle(s, 20);
            CHECK(AllFinite(f->transform->Position()));
        }
        Scene s("cmp_empty"); auto* c = Obj(s, "C", {0, 0, 0})->AddComponent<CompoundCollider3D>();
        s.Start();
        CHECK(c->PartCount() == 0);
        CHECK(!s.physics3D().Raycast(s, {0, 5, 0}, {0, -1, 0}, 10).hit);
    }
    {   // A compound cannot hold a compound: that part is dropped (this already held before the hardening; kept as a
        // guard). The part count is capped (memory).
        Scene s("cmp_deep");
        auto* c = Obj(s, "C", {0, 0, 0})->AddComponent<CompoundCollider3D>();
        c->parts = "compound 0 0 0 0 0 0 1 1 1 1; box 0 0 0 0 0 0 1 1 1 1";
        CHECK(c->PartCount() == 1);
        std::ostringstream o; for (int i = 0; i < 100000; ++i) o << (i ? "; " : "") << "sphere 0 0 0 0 0 0 0.1 0 0 1";
        c->parts = o.str();
        CHECK(c->PartCount() <= 1024);
        CHECK(c->PartCount() >= 1);
    }

    {   // Heightfield with bad settings: zero / negative / NaN cells, junk height text, silly grid sizes. Finite, no crash.
        struct Case { int c, r; float cx, cz; const char* h; };
        Case cases[] = {{2, 2, 0.0f, 0.0f, "0 0 0 0"}, {2, 2, -1.0f, 1.0f, "0 0 0 0"}, {2, 2, NaN, 1.0f, "0 0 0 0"},
                        {2, 2, 1.0f, 1.0f, "nan 1 inf 2"}, {0, -5, 1.0f, 1.0f, ""}, {100000, 100000, 1.0f, 1.0f, "1 2 3"}, {3, 5, 1.0f, 1.0f, "1 2 3"}};
        for (const Case& k : cases) {
            Scene s("hf_bad");
            auto* hf = Obj(s, "G", {0, 0, 0})->AddComponent<HeightfieldCollider3D>();
            hf->columns = k.c; hf->rows = k.r; hf->cellX = k.cx; hf->cellZ = k.cz; hf->heights = k.h;
            GameObject* b = Obj(s, "Ball", {0, 3, 0});
            b->AddComponent<SphereCollider3D>()->radius = 0.4f; b->AddComponent<Rigidbody3D>();
            s.Start();
            CHECK(BoundsOk(hf));
            (void)s.physics3D().Raycast(s, {0.1f, 9, 0.1f}, {0, -1, 0}, 30);
            Settle(s, 30);
            CHECK(AllFinite(b->transform->Position()));
            float y; (void)hf->LocalHeight(0, 0, y);
            std::vector<std::array<Vec3, 3>> tr; hf->CollectTriangles({-5, -5, -5}, {5, 5, 5}, tr);
            CHECK(tr.size() < 1000000);
        }
    }
    {   // Heightfield sampling at the very edge and with odd sizes (3 x 5).
        Scene s("hf_edge");
        auto* hf = Obj(s, "G", {0, 0, 0})->AddComponent<HeightfieldCollider3D>();
        std::vector<float> h; for (int r = 0; r < 5; ++r) for (int c = 0; c < 3; ++c) h.push_back((float)(c + 10 * r));
        hf->SetHeights(3, 5, h); hf->cellX = 2.0f; hf->cellZ = 0.5f;
        s.Start();
        float y;
        CHECK(hf->LocalHeight(-2.0f, -1.0f, y)); CHECK_NEAR(y, 0.0f, 1e-4f);       // first corner
        CHECK(hf->LocalHeight(2.0f, 1.0f, y));   CHECK_NEAR(y, 42.0f, 1e-4f);      // last corner (col 2, row 4)
        CHECK(hf->LocalHeight(2.0f, -1.0f, y));  CHECK_NEAR(y, 2.0f, 1e-4f);
        CHECK(hf->LocalHeight(0.0f, 0.0f, y));   CHECK_NEAR(y, 21.0f, 1e-4f);      // centre sample (col 1, row 2)
        CHECK(!hf->LocalHeight(2.01f, 0.0f, y)); CHECK(!hf->LocalHeight(0.0f, -1.01f, y));
        auto r = s.physics3D().Raycast(s, {1.9f, 80, 0.9f}, {0, -1, 0}, 200);      // just inside the far corner
        CHECK(r.hit); CHECK(r.point.y > 38.0f && r.point.y < 43.0f);
        CHECK(!s.physics3D().Raycast(s, {2.5f, 80, 0.0f}, {0, -1, 0}, 200).hit);  // outside the grid
    }
    {   // Missing heights read as 0; an enormous height text is cut (memory).
        Scene s("hf_short");
        auto* hf = Obj(s, "G", {0, 0, 0})->AddComponent<HeightfieldCollider3D>();
        hf->columns = 3; hf->rows = 3; hf->heights = "5 5 5";
        CHECK_NEAR(hf->HeightAt(0, 0), 5.0f, 1e-6f); CHECK_NEAR(hf->HeightAt(2, 2), 0.0f, 1e-6f);
        std::ostringstream o; for (int i = 0; i < 3000000; ++i) o << "1 ";
        hf->heights = o.str(); hf->columns = 4; hf->rows = 4;
        CHECK_NEAR(hf->HeightAt(3, 3), 1.0f, 1e-6f);
        s.Start(); CHECK(BoundsOk(hf));
    }

    {   // Cone: down the axis hits the tip; beside the tip, the slope; from below, the base cap; outside, nothing.
        Scene s("cone_rays");
        auto* co = Obj(s, "K", {0, 0, 0})->AddComponent<ConeCollider3D>(); co->radius = 0.5f; co->height = 1.0f;
        s.Start();
        auto tip = s.physics3D().Raycast(s, {0, 5, 0}, {0, -1, 0}, 10);
        CHECK(tip.hit); CHECK_NEAR(tip.point.y, 0.5f, 0.01f);
        auto side = s.physics3D().Raycast(s, {0.2f, 5, 0}, {0, -1, 0}, 10);   // radius at height y is 0.5 * (0.5 - y), so x = 0.2 is at y = 0.1
        CHECK(side.hit); CHECK_NEAR(side.point.y, 0.1f, 0.01f);
        auto base = s.physics3D().Raycast(s, {0.3f, -5, 0}, {0, 1, 0}, 10);
        CHECK(base.hit); CHECK_NEAR(base.point.y, -0.5f, 0.01f); CHECK_NEAR(base.normal.y, -1.0f, 0.01f);
        CHECK(!s.physics3D().Raycast(s, {0.6f, 5, 0}, {0, -1, 0}, 10).hit);
    }
    {   // Cylinder caps: top and bottom from the centre line and near the rim; just outside the rim misses; short rays stop short.
        Scene s("cyl_rays");
        auto* cy = Obj(s, "C", {0, 0, 0})->AddComponent<CylinderCollider3D>(); cy->radius = 0.5f; cy->height = 2.0f;
        s.Start();
        auto t = s.physics3D().Raycast(s, {0, 5, 0}, {0, -1, 0}, 10); CHECK(t.hit); CHECK_NEAR(t.point.y, 1.0f, 0.01f);
        auto b = s.physics3D().Raycast(s, {0, -5, 0}, {0, 1, 0}, 10); CHECK(b.hit); CHECK_NEAR(b.point.y, -1.0f, 0.01f); CHECK_NEAR(b.normal.y, -1.0f, 0.01f);
        auto rim = s.physics3D().Raycast(s, {0.499f, 5, 0}, {0, -1, 0}, 10); CHECK(rim.hit); CHECK_NEAR(rim.point.y, 1.0f, 0.01f);
        CHECK(!s.physics3D().Raycast(s, {0.51f, 5, 0}, {0, -1, 0}, 10).hit);
        CHECK(!s.physics3D().Raycast(s, {0, 5, 0}, {0, -1, 0}, 3.5f).hit);
    }

    {   // Non-uniform scale: every shape stretches along the object's own axes.
        Scene s("scale");
        auto mk = [&](const char* n, Vec3 pos) { GameObject* g = Obj(s, n, pos); g->transform->localScale = {2, 1, 3}; return g; };
        auto* cy = mk("Cyl", {0, 0, 0})->AddComponent<CylinderCollider3D>(); cy->radius = 0.5f; cy->height = 2.0f;
        auto* co = mk("Cone", {20, 0, 0})->AddComponent<ConeCollider3D>(); co->radius = 0.5f; co->height = 1.0f;
        auto* h = mk("Hull", {40, 0, 0})->AddComponent<ConvexHullCollider3D>(); h->points = "1 0 0; -1 0 0; 0 1 0; 0 -1 0; 0 0 1; 0 0 -1";
        auto* cp = mk("Cmp", {60, 0, 0})->AddComponent<CompoundCollider3D>(); cp->AddBox({0, 0, 0}, {1, 1, 1});
        auto* hf = mk("Hf", {80, 0, 0})->AddComponent<HeightfieldCollider3D>(); hf->SetHeights(2, 2, {1, 1, 1, 1}); hf->cellX = 4; hf->cellZ = 4;
        s.Start();
        Vec3 a, b;
        cy->WorldAABB(a, b); CHECK_NEAR(b.x - a.x, 2.0f, 0.02f); CHECK_NEAR(b.y - a.y, 2.0f, 0.02f); CHECK_NEAR(b.z - a.z, 3.0f, 0.02f);
        co->WorldAABB(a, b); CHECK_NEAR(b.x - a.x, 2.0f, 0.02f); CHECK_NEAR(b.y - a.y, 1.0f, 0.02f); CHECK_NEAR(b.z - a.z, 3.0f, 0.02f);
        h->WorldAABB(a, b);  CHECK_NEAR(b.x - a.x, 4.0f, 0.02f); CHECK_NEAR(b.y - a.y, 2.0f, 0.02f); CHECK_NEAR(b.z - a.z, 6.0f, 0.02f);
        cp->WorldAABB(a, b); CHECK_NEAR(b.x - a.x, 2.0f, 0.02f); CHECK_NEAR(b.z - a.z, 3.0f, 0.02f);
        auto rc = s.physics3D().Raycast(s, {-5, 0, 0}, {1, 0, 0}, 20); CHECK(rc.hit); CHECK_NEAR(rc.point.x, -1.0f, 0.01f);   // cylinder radius 0.5 * 2
        auto rz = s.physics3D().Raycast(s, {0, 0, -9}, {0, 0, 1}, 20); CHECK(rz.hit); CHECK_NEAR(rz.point.z, -1.5f, 0.01f);   // radius 0.5 * 3
        auto rh = s.physics3D().Raycast(s, {40, 9, 0}, {0, -1, 0}, 20); CHECK(rh.hit); CHECK_NEAR(rh.point.y, 1.0f, 0.01f);
        auto rf = s.physics3D().Raycast(s, {80, 9, 0}, {0, -1, 0}, 20); CHECK(rf.hit); CHECK_NEAR(rf.point.y, 1.0f, 0.02f);
        // The stretch follows the object when it is turned.
        cy->transform->localRotation = Quat::Euler(0, 90, 0);
        auto rt = s.physics3D().Raycast(s, {0, 0, -9}, {0, 0, 1}, 20); CHECK(rt.hit); CHECK_NEAR(rt.point.z, -1.0f, 0.02f);
    }
    {   // Zero-sized shapes behave as thin shapes, not as NaN.
        Scene s("zero");
        auto* cy = Obj(s, "Cyl", {0, 0, 0})->AddComponent<CylinderCollider3D>(); cy->radius = 0; cy->height = 0;
        auto* co = Obj(s, "Cone", {5, 0, 0})->AddComponent<ConeCollider3D>(); co->radius = -1; co->height = 0;
        GameObject* sc = Obj(s, "Z", {10, 0, 0}); sc->transform->localScale = {0, 0, 0};
        auto* cz = sc->AddComponent<ConeCollider3D>();
        s.Start();
        CHECK(BoundsOk(cy)); CHECK(BoundsOk(co)); CHECK(BoundsOk(cz));
        Vec3 c, in; float v;
        CHECK(!cy->MassShape(c, v, in) || std::isfinite(v));
        CHECK(!cz->MassShape(c, v, in) || std::isfinite(v));
        (void)s.physics3D().Raycast(s, {0, 5, 0}, {0, -1, 0}, 10);
    }

    {   // A scene file in the record format written before the new collider shapes (#1083): fixed text, written out
        // field by field from that release's serializer. Box, Sphere, Capsule and Cylinder colliders, a physics material
        // by index (index 2 = the sphere, in that release's count) and a trigger filter. They load with these values.
        const char* oldText =
            "okayscene 3\n"
            "gameobject 0 \"Old\"\n"
            "  active 1\n"
            "  parent -1\n"
            "  transform 1 2 3 0 0 0 1 1 1 1\n"
            "  cylindercollider3d 0.7 1.5 2 0 0.25 0 0 3 0\n"
            "  boxcollider3d 2 1 4 0.5 0 0 1 2 0\n"
            "  spherecollider3d 0.35 0 1 0 0 0 1\n"
            "  capsulecollider3d 0.4 1.8 1 0 0.9 0 0 0 0\n"
            "  trigfilter3d \"Player\" 5\n"
            "  physmat3d 0.3 0.1 2 1 2\n"
            "end\n";
        Scene t("old_fixed"); std::string err;
        CHECK(SceneSerializer::Deserialize(t, oldText, &err, nullptr));
        GameObject* g = t.Find("Old");
        CHECK(g != nullptr);
        if (g) {
            CHECK_NEAR(g->transform->localPosition.y, 2.0f, 1e-6f);
            auto* cy = g->GetComponent<CylinderCollider3D>();
            CHECK(cy && cy->radius == 0.7f && cy->height == 1.5f && cy->axis == 2 && cy->offset.y == 0.25f && cy->layer == 3 && !cy->isTrigger);
            BoxCollider3D* bx = nullptr;
            for (auto* b : g->GetComponents<BoxCollider3D>()) if (b->shape() == Collider3D::Shape::Box) bx = b;
            CHECK(bx && bx->size.x == 2.0f && bx->size.z == 4.0f && bx->offset.x == 0.5f && bx->isTrigger && bx->layer == 2);
            auto* sp = g->GetComponent<SphereCollider3D>();
            CHECK(sp && sp->radius == 0.35f && sp->offset.x == 0.0f && sp->offset.y == 1.0f && sp->layer == 0 && sp->autoFit);
            CapsuleCollider3D* cp = nullptr;
            for (auto* c : g->GetComponents<CapsuleCollider3D>()) if (c->shape() == Collider3D::Shape::Capsule) cp = c;
            CHECK(cp && cp->radius == 0.4f && cp->height == 1.8f && cp->axis == 1 && cp->offset.y == 0.9f);
            CHECK(sp && sp->friction == 0.3f && sp->bounciness == 0.1f && (int)sp->frictionCombine == 2 && (int)sp->bounceCombine == 1);
            CHECK(cy && cy->friction < 0.0f && cp && cp->friction < 0.0f);         // the material went to the sphere only
            CHECK(cy && cy->triggerTag == "Player" && cy->triggerLayers == 5u && sp && sp->triggerTag == "Player");
            CHECK(cy && cy->contactOffset == 0.0f);                                 // a field newer than the file: default
            t.Start();
            CHECK(BoundsOk(cy)); CHECK(BoundsOk(sp));
        }
    }
    {   // Saving with today's serializer writes no skin record for colliders without one, and reloads.
        Scene s("old");
        GameObject* g = Obj(s, "A", {0, 0, 0});
        g->AddComponent<BoxCollider3D>(); g->AddComponent<CylinderCollider3D>()->radius = 0.7f;
        std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("colskin") == std::string::npos);
        Scene t("old2"); std::string err;
        CHECK(SceneSerializer::Deserialize(t, text, &err, nullptr));
        auto* cy = t.Find("A") ? t.Find("A")->GetComponent<CylinderCollider3D>() : nullptr;
        CHECK(cy && cy->radius == 0.7f && cy->contactOffset == 0.0f);
    }
    {   // New colliders with hostile saved values load without a crash and still give finite bounds.
        Scene s("hostile");
        GameObject* g = Obj(s, "A", {0, 0, 0});
        auto* hf = g->AddComponent<HeightfieldCollider3D>(); hf->columns = 6; hf->rows = 6; hf->cellX = 0.0f;
        auto* h = g->AddComponent<ConvexHullCollider3D>(); h->points = "nan 0 0; 1 1 1";
        std::string text = SceneSerializer::Serialize(s);
        Scene t("hostile2"); std::string err;
        (void)SceneSerializer::Deserialize(t, text, &err, nullptr);
        t.Start();
        Settle(t, 5);
        auto* g2 = t.Find("A");
        CHECK(g2 != nullptr);
        if (g2) { auto* h2 = g2->GetComponent<HeightfieldCollider3D>(); CHECK(!h2 || BoundsOk(h2)); }
    }

    {   // Order independence: the same objects made in the opposite order settle to the same places.
        auto run = [](bool reversed, float* out) {
            Scene s("order");
            GameObject* fl = Obj(s, "Floor", {0, -0.5f, 0});
            fl->AddComponent<BoxCollider3D>()->size = {40, 1, 40};
            fl->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
            GameObject* a = nullptr; GameObject* b = nullptr;
            auto makeA = [&]() { a = Obj(s, "Cyl", {-2, 2, 0}); auto* c = a->AddComponent<CylinderCollider3D>(); c->radius = 0.5f; c->height = 1.0f; a->AddComponent<Rigidbody3D>(); };
            auto makeB = [&]() { b = Obj(s, "Cone", {2, 2, 0}); auto* c = b->AddComponent<ConeCollider3D>(); c->radius = 0.5f; c->height = 1.0f; b->AddComponent<Rigidbody3D>(); };
            if (reversed) { makeB(); makeA(); } else { makeA(); makeB(); }
            s.Start();
            Settle(s, 240);
            out[0] = a->transform->Position().y; out[1] = b->transform->Position().y;
        };
        float p[2], q[2]; run(false, p); run(true, q);
        CHECK_NEAR(p[0], q[0], 0.02f); CHECK_NEAR(p[1], q[1], 0.02f);
        CHECK(p[0] > 0.3f && p[0] < 0.8f);
    }

    {   // Real NaN and infinity values (not text) given through the API: SetPoints drops them.
        Scene s("hull_nan_api");
        auto* h = Obj(s, "H", {0, 0, 0})->AddComponent<ConvexHullCollider3D>();
        const float Inf = std::numeric_limits<float>::infinity();
        h->SetPoints({{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {NaN, 0, 0}, {Inf, 1, 1}, {0, -Inf, 0}, {3e38f, 0, 0}});
        CHECK(h->PointCount() == 6);
        s.Start(); CHECK(BoundsOk(h));
        Vec3 a, b; h->WorldAABB(a, b); CHECK_NEAR(b.x - a.x, 2.0f, 0.01f);
    }
    {   // SetHeights with NaN / infinity: those samples read as 0 and the samples after them are kept.
        Scene s("hf_nan_api");
        auto* hf = Obj(s, "G", {0, 0, 0})->AddComponent<HeightfieldCollider3D>();
        const float Inf = std::numeric_limits<float>::infinity();
        hf->SetHeights(3, 2, {1, NaN, 3, -Inf, 5, 6});
        CHECK_NEAR(hf->HeightAt(0, 0), 1.0f, 1e-6f);
        CHECK_NEAR(hf->HeightAt(1, 0), 0.0f, 1e-6f);
        CHECK_NEAR(hf->HeightAt(2, 0), 3.0f, 1e-6f);
        CHECK_NEAR(hf->HeightAt(0, 1), 0.0f, 1e-6f);
        CHECK_NEAR(hf->HeightAt(2, 1), 6.0f, 1e-6f);
        s.Start(); CHECK(BoundsOk(hf));
    }
    {   // heightScale NaN / infinity / huge, and huge finite heights: bounds and depth stay finite everywhere.
        const float Inf = std::numeric_limits<float>::infinity();
        const float scales[] = {NaN, Inf, -Inf, 3e38f, -3e38f};
        for (float k : scales) {
            Scene s("hf_scale");
            auto* hf = Obj(s, "G", {0, 0, 0})->AddComponent<HeightfieldCollider3D>();
            hf->SetHeights(2, 2, {0, 1, 2, 3});
            hf->heightScale = k;
            CHECK(BoundsOk(hf));
            CHECK(std::isfinite(hf->Depth()));
            CHECK(std::isfinite(hf->HeightAt(1, 1)));
        }
        Scene s("hf_huge");
        auto* hf = Obj(s, "G", {0, 0, 0})->AddComponent<HeightfieldCollider3D>();
        hf->SetHeights(2, 2, {-1e38f, 1e38f, 0, 0});
        hf->heightScale = 10.0f;
        CHECK(BoundsOk(hf));
        CHECK(std::isfinite(hf->Depth()));
        CHECK(hf->HeightAt(1, 0) <= 1.0e6f);                                         // clamped to the coordinate limit
        Vec3 a, b; hf->WorldAABB(a, b); CHECK(b.y <= 1.0e6f && a.y >= -1.0e7f);
    }
    {   // A dynamic Mesh collider on a mesh with thousands of corners builds a hull of at most 4,096 points (the same
        // thinning as the Convex Hull collider, so a big mesh cannot stall the wrap), and the hull still spans the mesh.
        Scene s("mesh_hull_big");
        GameObject* g = Obj(s, "M", {0, 0, 0});
        g->AddComponent<MeshRenderer>()->mesh = Mesh::Sphere(1.0f, 72, 72);
        auto* mc = g->AddComponent<MeshCollider3D>();
        g->AddComponent<Rigidbody3D>();
        CHECK(g->GetComponent<MeshRenderer>()->mesh.vertices.size() > 4096u);
        CHECK(mc->UsesConvexHull());
        CHECK(mc->HullVertexCount() <= 4096);
        CHECK(mc->HullVertexCount() >= 4);
        Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (const Vec3& v : mc->Hull().vertices) { lo = Vec3::Min(lo, v); hi = Vec3::Max(hi, v); }
        CHECK_NEAR(lo.y, -1.0f, 0.02f); CHECK_NEAR(hi.y, 1.0f, 0.02f);
        CHECK_NEAR(hi.x - lo.x, 2.0f, 0.05f); CHECK_NEAR(hi.z - lo.z, 2.0f, 0.05f);
    }
    {   // A resolution-2048 Terrain (2049 x 2049 samples) copies into a Heightfield with every row in place.
        Terrain tr;
        tr.resolution = 2048; tr.size = 2048.0f;
        const int D = tr.Dim();
        tr.heights.assign((std::size_t)D * (std::size_t)D, 0.0f);
        tr.heights[(std::size_t)D - 1] = 3.0f;                                       // col 2048, row 0
        tr.heights[(std::size_t)(D - 1) * (std::size_t)D] = 5.0f;                    // col 0, row 2048
        tr.heights[(std::size_t)D * (std::size_t)D - 1] = 7.0f;                      // the far corner
        tr.heights[(std::size_t)1000 * (std::size_t)D + 1500] = 9.0f;                // somewhere in the middle
        HeightfieldCollider3D hf;
        CHECK(hf.SetFromTerrain(tr));
        CHECK(hf.Cols() == D); CHECK(hf.Rows() == D);
        CHECK_NEAR(hf.HeightAt(D - 1, 0), 3.0f, 1e-6f);
        CHECK_NEAR(hf.HeightAt(0, D - 1), 5.0f, 1e-6f);
        CHECK_NEAR(hf.HeightAt(D - 1, D - 1), 7.0f, 1e-6f);
        CHECK_NEAR(hf.HeightAt(1500, 1000), 9.0f, 1e-6f);
        float y = 0;
        CHECK(hf.LocalHeight(1024.0f, 1024.0f, y)); CHECK_NEAR(y, 7.0f, 1e-4f);    // the far corner in local metres
    }

    {   // (Last, so a hang here still shows every other result.) A ray that reaches a fine heightfield far from its own start returns (it used to loop forever: at a large
        // distance, adding one small step no longer changed the float position).
        Scene s("hf_far_ray");
        auto* hf = Obj(s, "G", {0, 0, 0})->AddComponent<HeightfieldCollider3D>();
        hf->SetHeights(5, 5, std::vector<float>(25, 0.0f)); hf->cellX = 0.01f; hf->cellZ = 0.01f;   // a 4 cm grid
        s.Start();
        Vec3 n; float tHit = 0;
        // 500 km away a float position moves in steps of 3 cm, more than the 5 mm march step: passes 1 m above, a miss.
        CHECK(!hf->RayShape({-500000.0f, 1.0f, 0.0f}, {1, 0, 0}, 1.0e6f, tHit, n));
    }
    TEST_MAIN_RESULT();
}
