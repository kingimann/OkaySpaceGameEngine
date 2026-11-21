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
bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool BoundsOk(Collider3D* c) { Vec3 a, b; c->WorldAABB(a, b); return Finite(a) && Finite(b) && a.x <= b.x && a.y <= b.y && a.z <= b.z; }
void Settle(Scene& s, int n) { for (int i = 0; i < n; ++i) s.Update(1.0f / 60.0f); }
const float NaN = std::numeric_limits<float>::quiet_NaN();
}

int main() {
    RUN_SUITE("collider_hardening");
    Log::console = false;

    {   // Convex hull: NaN, infinite and absurdly huge points are ignored; the rest still makes the right solid.
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
            CHECK(!m || (v >= 0.0f && std::isfinite(v) && Finite(in) && Finite(c)));
            (void)s.physics3D().Raycast(s, {0.3f, 5, 0.3f}, {0, -1, 0}, 10);
            Settle(s, 30);
            CHECK(Finite(f->transform->Position()));
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
    {   // A hull never keeps an unbounded number of points (memory).
        std::ostringstream o; for (int i = 0; i < 50000; ++i) o << (i ? "; " : "") << std::cos(i * 0.01f) << ' ' << std::sin(i * 0.013f) << ' ' << std::cos(i * 0.007f);
        Scene s("hull_big");
        auto* h = Obj(s, "H", {0, 0, 0})->AddComponent<ConvexHullCollider3D>(); h->points = o.str();
        CHECK(h->PointCount() <= 4096);
        CHECK(h->PointCount() >= 4);
        s.Start(); CHECK(BoundsOk(h));
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

    {   // Compound with zero children, junk children, or a child that names another compound: collides with nothing.
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
            CHECK(Finite(f->transform->Position()));
        }
        Scene s("cmp_empty"); auto* c = Obj(s, "C", {0, 0, 0})->AddComponent<CompoundCollider3D>();
        s.Start();
        CHECK(c->PartCount() == 0);
        CHECK(!s.physics3D().Raycast(s, {0, 5, 0}, {0, -1, 0}, 10).hit);
    }
    {   // A compound cannot hold a compound: that part is dropped. The part count is capped (memory).
        Scene s("cmp_deep");
        auto* c = Obj(s, "C", {0, 0, 0})->AddComponent<CompoundCollider3D>();
        c->parts = "compound 0 0 0 0 0 0 1 1 1 1; box 0 0 0 0 0 0 1 1 1 1";
        CHECK(c->PartCount() == 1);
        std::ostringstream o; for (int i = 0; i < 100000; ++i) o << (i ? "; " : "") << "sphere 0 0 0 0 0 0 0.1 0 0 1";
        c->parts = o.str();
        CHECK(c->PartCount() <= 1024);
        CHECK(c->PartCount() >= 1);
    }

    {   // Heightfield with bad settings: zero / negative / NaN cells, NaN heights, silly grid sizes. Finite, no crash.
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
            CHECK(Finite(b->transform->Position()));
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

    {   // Older scene files: only the older shapes, no skin record. They load and the new fields keep their defaults.
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

    TEST_MAIN_RESULT();
}
