// MeshCollider3D: a real triangle-mesh collider (BVH) for static world geometry.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>

using namespace okay;

namespace {
const float kPi = 3.14159265358979f;

// ---- Mesh builders (analytic shapes) ---------------------------------------------
void Quad(Mesh& m, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {   // a,b,c,d counter-clockwise
    int i = (int)m.vertices.size();
    m.vertices.insert(m.vertices.end(), {a, b, c, d});
    m.triangles.insert(m.triangles.end(), {i, i + 1, i + 2, i, i + 2, i + 3});
}
Mesh Box(Vec3 lo, Vec3 hi) {
    Mesh m;
    Vec3 p[8];
    for (int i = 0; i < 8; ++i) p[i] = {i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z};
    Quad(m, p[0], p[1], p[3], p[2]); Quad(m, p[4], p[6], p[7], p[5]);
    Quad(m, p[0], p[2], p[6], p[4]); Quad(m, p[1], p[5], p[7], p[3]);
    Quad(m, p[0], p[4], p[5], p[1]); Quad(m, p[2], p[3], p[7], p[6]);
    return m;
}
// Concave L: two arms in the XZ plane, extruded over y in [0,1].
//   arm A: x in [0,4], z in [0,1];  arm B: x in [0,1], z in [1,4]
Mesh LShape() {
    Mesh m;
    const float y0 = 0, y1 = 1;
    // Caps (top faces up, bottom faces down).
    Quad(m, {0, y1, 0}, {0, y1, 1}, {4, y1, 1}, {4, y1, 0});
    Quad(m, {0, y1, 1}, {0, y1, 4}, {1, y1, 4}, {1, y1, 1});
    Quad(m, {0, y0, 0}, {4, y0, 0}, {4, y0, 1}, {0, y0, 1});
    Quad(m, {0, y0, 1}, {1, y0, 1}, {1, y0, 4}, {0, y0, 4});
    // Outline walls: (0,0)-(4,0)-(4,1)-(1,1)-(1,4)-(0,4)
    const float xs[6] = {0, 4, 4, 1, 1, 0}, zs[6] = {0, 0, 1, 1, 4, 4};
    for (int i = 0; i < 6; ++i) {
        int j = (i + 1) % 6;
        Quad(m, {xs[i], y0, zs[i]}, {xs[j], y0, zs[j]}, {xs[j], y1, zs[j]}, {xs[i], y1, zs[i]});
    }
    return m;
}
// 4x4 plate of unit cells over [-2,2]^2 at y=0, with the cell [0,1]x[0,1] missing.
Mesh PlateWithHole() {
    Mesh m;
    for (int cx = -2; cx < 2; ++cx)
        for (int cz = -2; cz < 2; ++cz) {
            if (cx == 0 && cz == 0) continue;
            Quad(m, {(float)cx, 0, (float)cz}, {(float)cx, 0, (float)cz + 1},
                    {(float)cx + 1, 0, (float)cz + 1}, {(float)cx + 1, 0, (float)cz});
        }
    return m;
}
// Open bowl (inside of a lower hemisphere): rim at y=0, bottom at y=-R.
Mesh Bowl(float R, int rings, int sectors) {
    Mesh m;
    for (int r = 0; r <= rings; ++r) {
        float th = kPi * 0.5f + (kPi * 0.5f) * (float)r / rings;
        for (int s = 0; s < sectors; ++s) {
            float ph = 2 * kPi * (float)s / sectors;
            m.vertices.push_back({R * std::sin(th) * std::cos(ph), R * std::cos(th), R * std::sin(th) * std::sin(ph)});
        }
    }
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < sectors; ++s) {
            int a = r * sectors + s, b = r * sectors + (s + 1) % sectors, c = a + sectors, d = b + sectors;
            m.triangles.insert(m.triangles.end(), {a, c, b, b, c, d});
        }
    return m;
}

GameObject* AddMesh(Scene& s, const char* name, const Mesh& m, Vec3 pos = {0, 0, 0}) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = pos;
    g->AddComponent<MeshRenderer>()->mesh = m;
    g->AddComponent<MeshCollider3D>();
    return g;
}

// Brute-force reference: nearest two-sided ray hit over every triangle of a mesh.
bool BruteRay(const Mesh& m, Vec3 o, Vec3 d, float& tBest) {
    bool any = false; tBest = 1e30f;
    for (size_t i = 0; i + 2 < m.triangles.size(); i += 3) {
        Vec3 a = m.vertices[m.triangles[i]], b = m.vertices[m.triangles[i + 1]], c = m.vertices[m.triangles[i + 2]];
        Vec3 e1 = b - a, e2 = c - a, p = Vec3::Cross(d, e2);
        float det = Vec3::Dot(e1, p);
        if (std::fabs(det) < 1e-12f) continue;
        float inv = 1.0f / det; Vec3 sv = o - a;
        float u = Vec3::Dot(sv, p) * inv; if (u < 0 || u > 1) continue;
        Vec3 q = Vec3::Cross(sv, e1);
        float v = Vec3::Dot(d, q) * inv; if (v < 0 || u + v > 1) continue;
        float t = Vec3::Dot(e2, q) * inv;
        if (t >= 0 && t < tBest) { tBest = t; any = true; }
    }
    return any;
}

class Counter : public Behaviour {
public:
    int trigEnter = 0;
    void OnTriggerEnter3D(Collider3D*) override { ++trigEnter; }
};

class CollisionCounter : public Behaviour {
public:
    int enter = 0, stay = 0, exit = 0;
    GameObject* other = nullptr;
    float enterPen = 0.0f;
    void OnCollisionEnter3D(const Collision3D& c) override { ++enter; other = c.gameObject; enterPen = c.penetration; }
    void OnCollisionStay3D(const Collision3D&) override { ++stay; }
    void OnCollisionExit3D(const Collision3D&) override { ++exit; }
};

// Every source vertex is on the inner side of every hull face (the shape is the convex hull,
// not the original concave triangles and not the fallback box).
bool SourceInsideHull(const Mesh& hull, const Mesh& src) {
    if (hull.vertices.size() < 4 || hull.triangles.size() < 4) return false;
    Vec3 c{0, 0, 0};
    for (const Vec3& v : hull.vertices) c = c + v;
    c = c * (1.0f / (float)hull.vertices.size());
    const int nv = (int)hull.vertices.size();
    for (size_t t = 0; t + 2 < hull.triangles.size(); t += 3) {
        int i0 = hull.triangles[t], i1 = hull.triangles[t + 1], i2 = hull.triangles[t + 2];
        if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= nv || i1 >= nv || i2 >= nv) return false;
        Vec3 a = hull.vertices[i0], b = hull.vertices[i1], d = hull.vertices[i2];
        Vec3 n = Vec3::Cross(b - a, d - a);
        float sideC = Vec3::Dot(n, c - a);
        if (std::fabs(sideC) < 1e-8f) continue;
        for (const Vec3& p : src.vertices) {
            float side = Vec3::Dot(n, p - a);
            if (side * sideC < -1e-3f * (std::fabs(sideC) + 1.0f)) return false;
        }
    }
    return true;
}
} // namespace

int main() {
    RUN_SUITE("meshcollider3d");

    // ---- Ray vs an analytic cube: hits, normals, misses, max distance, from inside ----
    {
        Scene s("cube");
        auto* g = AddMesh(s, "Cube", Box({-1, -1, -1}, {1, 1, 1}));
        auto* mc = g->GetComponent<MeshCollider3D>();
        s.Start(); s.Update(0.0f);
        CHECK(mc->HasTriangles());
        CHECK(mc->Tris().TriangleCount() == 12);
        Physics3D& p = s.physics3D();
        RaycastHit3D h = p.Raycast(s, {0, 5, 0}, {0, -1, 0});
        CHECK(h.hit); CHECK(h.collider == mc);
        CHECK_NEAR(h.distance, 4.0f, 1e-4f); CHECK_NEAR(h.point.y, 1.0f, 1e-4f); CHECK_NEAR(h.normal.y, 1.0f, 1e-4f);
        h = p.Raycast(s, {-5, 0.3f, 0.2f}, {1, 0, 0});
        CHECK(h.hit); CHECK_NEAR(h.distance, 4.0f, 1e-4f); CHECK_NEAR(h.normal.x, -1.0f, 1e-4f);
        CHECK(!p.Raycast(s, {3, 5, 0}, {0, -1, 0}).hit);                 // beside it
        CHECK(!p.Raycast(s, {0, 5, 0}, {0, 1, 0}).hit);                  // pointing away
        CHECK(!p.Raycast(s, {0, 5, 0}, {0, -1, 0}, 3.9f).hit);           // short of it
        h = p.Raycast(s, {0, 0, 0}, {0, 1, 0});                          // from inside: hits the wall, faces the origin
        CHECK(h.hit); CHECK_NEAR(h.distance, 1.0f, 1e-4f); CHECK_NEAR(h.normal.y, -1.0f, 1e-4f);
        // Linecast and RaycastAll agree.
        CHECK(p.Linecast(s, {0, 5, 0}, {0, -5, 0}).hit);
        auto all = p.RaycastAll(s, {0, 5, 0}, {0, -1, 0});
        CHECK(all.size() == 1);                                          // one hit per collider: its nearest
        if (all.size() == 1) CHECK_NEAR(all[0].distance, 4.0f, 1e-4f);
    }

    // ---- Ray vs a tessellated sphere matches the analytic sphere ----
    {
        Scene s("sphere");
        AddMesh(s, "Ball", Mesh::Sphere(1.0f, 24, 32));
        s.Start(); s.Update(0.0f);
        int bad = 0; std::srand(7);
        for (int i = 0; i < 300; ++i) {
            Vec3 d{(float)std::rand() / RAND_MAX - 0.5f, (float)std::rand() / RAND_MAX - 0.5f, (float)std::rand() / RAND_MAX - 0.5f};
            if (d.SqrMagnitude() < 1e-3f) continue;
            d = d.Normalized();
            Vec3 o = d * 6.0f;                                           // aim back at the centre
            RaycastHit3D h = s.physics3D().Raycast(s, o, d * -1.0f);
            // Chord facets sit between cos(pi/24)=0.991 and 1.0 of the radius.
            if (!h.hit || h.distance < 5.0f - 1e-3f || h.distance > 5.0f + 0.012f) ++bad;
            else if (Vec3::Dot(h.normal, d) < 0.95f) ++bad;
        }
        CHECK(bad == 0);
        CHECK(!s.physics3D().Raycast(s, {0, 0, 5}, {0, 1, 0}).hit);      // grazes nothing
    }

    // ---- Concave L-shape: the notch is empty, the arms are solid ----
    {
        Scene s("L");
        auto* g = AddMesh(s, "L", LShape());
        auto* mc = g->GetComponent<MeshCollider3D>();
        s.Start(); s.Update(0.0f);
        Physics3D& p = s.physics3D();
        RaycastHit3D h = p.Raycast(s, {3, 5, 0.5f}, {0, -1, 0});          // arm A top
        CHECK(h.hit); CHECK_NEAR(h.distance, 4.0f, 1e-4f);
        CHECK(!p.Raycast(s, {3, 5, 3}, {0, -1, 0}).hit);                  // above the notch
        h = p.Raycast(s, {5, 0.5f, 3}, {-1, 0, 0});                       // across the notch: stops at arm B's inner face
        CHECK(h.hit); CHECK_NEAR(h.point.x, 1.0f, 1e-4f); CHECK_NEAR(h.normal.x, 1.0f, 1e-4f);
        h = p.Raycast(s, {5, 0.5f, 0.5f}, {-1, 0, 0});                    // through arm A: outer face first
        CHECK(h.hit); CHECK_NEAR(h.point.x, 4.0f, 1e-4f);
        // Overlaps respect the concavity: the notch is NOT inside the collider.
        CHECK(p.OverlapSphere(s, {3, 0.5f, 3}, 0.4f).empty());
        CHECK(!p.OverlapSphere(s, {3, 0.5f, 3}, 2.2f).empty());           // reaches the inner walls (2 away)
        CHECK(!p.OverlapSphere(s, {3, 0.5f, 0.5f}, 0.6f).empty());        // a shell: reaches arm A's top / bottom / sides
        // A sphere pushed from inside the notch's corner ends clear of both walls.
        Vec3 fixed = p.ResolveSphere(s, {1.3f, 0.5f, 1.3f}, 0.5f, nullptr, 6);
        CHECK(fixed.x >= 1.5f - 1e-3f); CHECK(fixed.z >= 1.5f - 1e-3f);
        (void)mc;
    }

    // ---- A mesh with a hole: rays and bodies pass through it ----
    {
        Scene s("hole");
        AddMesh(s, "Plate", PlateWithHole());
        GameObject* over = s.CreateGameObject("OverHole");
        over->transform->localPosition = {0.5f, 2, 0.5f};
        over->AddComponent<Rigidbody3D>();
        over->AddComponent<SphereCollider3D>()->radius = 0.2f;
        GameObject* onPlate = s.CreateGameObject("OnPlate");
        onPlate->transform->localPosition = {1.5f, 2, 0.5f};
        onPlate->AddComponent<Rigidbody3D>();
        onPlate->AddComponent<SphereCollider3D>()->radius = 0.2f;
        s.Start(); s.Update(0.0f);
        Physics3D& p = s.physics3D();
        CHECK(!p.Raycast(s, {0.5f, 1, 0.5f}, {0, -1, 0}).hit);
        CHECK(p.Raycast(s, {1.5f, 1, 0.5f}, {0, -1, 0}).hit);
        CHECK(!p.OverlapSphere(s, {0.5f, 0.05f, 0.5f}, 0.2f).empty() == false);
        CHECK(!p.OverlapSphere(s, {0.5f, 0.0f, 0.8f}, 0.3f).empty());     // reaches the hole's edge
        for (int i = 0; i < 180; ++i) s.Update(1.0f / 60.0f);
        CHECK(over->transform->Position().y < -3.0f);                     // fell through the hole
        CHECK(onPlate->transform->Position().y > 0.1f);                   // rests on the plate
        CHECK(onPlate->transform->Position().y < 0.4f);
    }

    // ---- Ramp (rotated mesh): a ball and a capsule slide down and are stopped by a wall mesh ----
    {
        Scene s("ramp");
        GameObject* ramp = s.CreateGameObject("Ramp");
        ramp->AddComponent<MeshRenderer>()->mesh = Mesh::Plane(30.0f);
        ramp->AddComponent<MeshCollider3D>();
        ramp->transform->localRotation = Quat::AngleAxis(30.0f, {0, 0, 1});     // rises toward +x
        GameObject* wall = s.CreateGameObject("Wall");
        wall->AddComponent<MeshRenderer>()->mesh = Mesh::Cube(1.0f);
        wall->AddComponent<MeshCollider3D>();
        wall->transform->localPosition = {-7.0f, -3.0f, 0};
        wall->transform->localScale = {1.0f, 8.0f, 10.0f};                    // face at x=-6.5 (non-uniform scale)
        GameObject* ball = s.CreateGameObject("Ball");
        ball->transform->localPosition = {0, 2, 0};
        auto* rbB = ball->AddComponent<Rigidbody3D>(); rbB->friction = 0.1f;
        ball->AddComponent<SphereCollider3D>()->radius = 0.5f;
        GameObject* cap = s.CreateGameObject("Capsule");
        cap->transform->localPosition = {0, 2, 3};
        auto* rbC = cap->AddComponent<Rigidbody3D>(); rbC->friction = 0.1f;
        auto* cc = cap->AddComponent<CapsuleCollider3D>(); cc->radius = 0.3f; cc->height = 1.4f;
        s.Start();
        float minDistBall = 1e9f, minDistCap = 1e9f;
        const Vec3 n{-0.5f, 0.8660254f, 0};
        for (int i = 0; i < 360; ++i) {
            s.Update(1.0f / 60.0f);
            if (i > 40) {
                minDistBall = std::min(minDistBall, Vec3::Dot(ball->transform->Position(), n));
                minDistCap = std::min(minDistCap, Vec3::Dot(cap->transform->Position(), n) - (0.4f * n.y));
            }
        }
        Vec3 pb = ball->transform->Position(), pc = cap->transform->Position();
        CHECK(pb.x < -3.0f);                              // slid down the slope
        CHECK(pc.x < -3.0f);
        CHECK(pb.x >= -6.5f + 0.5f - 0.06f);              // stopped by the wall (not through it)
        CHECK(pc.x >= -6.5f + 0.3f - 0.06f);
        CHECK(minDistBall > 0.5f - 0.06f);                // never sank into the ramp
        CHECK(minDistCap > 0.3f - 0.1f);
        CHECK(pb.y < 0.5f && pc.y < 0.5f);
        CHECK(rbB->velocity.Magnitude() < 1.0f);          // came to rest against the wall
    }

    // ---- A transform-driven character (ResolveSphere) slides along a wall and is stopped by it ----
    {
        Scene s("slide");
        AddMesh(s, "Wall", Box({2, -2, -10}, {3, 3, 10}));
        s.Start(); s.Update(0.0f);
        Vec3 pos{0, 0.5f, 0};
        for (int i = 0; i < 200; ++i) {
            pos = pos + Vec3{3.0f, 0, 1.0f} * (1.0f / 60.0f);               // pushing diagonally into the wall
            pos = s.physics3D().ResolveSphere(s, pos, 0.5f, nullptr, 4);
        }
        CHECK(pos.x <= 2.0f - 0.5f + 1e-3f);              // blocked by the face at x=2
        CHECK(pos.x > 1.4f);
        CHECK(pos.z > 2.0f);                              // ... but slid along it
        // Sphere cast onto a floor box, and box / capsule overlaps.
        Scene f("floor");
        AddMesh(f, "Floor", Box({-5, -1, -5}, {5, 0, 5}));
        f.Start(); f.Update(0.0f);
        Physics3D& p = f.physics3D();
        RaycastHit3D h = p.SphereCast(f, {0, 3, 0}, {0, -1, 0}, 0.5f, 10.0f);
        CHECK(h.hit); CHECK_NEAR(h.distance, 2.5f, 0.05f); CHECK_NEAR(h.normal.y, 1.0f, 0.05f);
        CHECK(!p.OverlapBox(f, {0, 0.25f, 0}, {0.5f, 0.5f, 0.5f}).empty());
        CHECK(p.OverlapBox(f, {0, 1.0f, 0}, {0.5f, 0.4f, 0.5f}).empty());
        CHECK(!p.OverlapCapsule(f, {0, 0.4f, 0}, {0, 1.6f, 0}, 0.5f).empty());
        CHECK(p.OverlapCapsule(f, {0, 0.8f, 0}, {0, 1.6f, 0}, 0.5f).empty());
        h = p.CapsuleCast(f, {0, 1.5f, 0}, {0, 2.5f, 0}, 0.3f, {0, -1, 0}, 10.0f);
        CHECK(h.hit); CHECK_NEAR(h.distance, 1.2f, 0.05f);
        h = p.BoxCast(f, {0, 2, 0}, {0.4f, 0.4f, 0.4f}, {0, -1, 0}, Quat::Identity, 10.0f);
        CHECK(h.hit); CHECK_NEAR(h.distance, 1.6f, 0.05f);
    }

    // ---- Rigidbody ball dropped into a concave bowl stays in it and settles at the bottom ----
    {
        Scene s("bowl");
        AddMesh(s, "Bowl", Bowl(3.0f, 12, 24));
        GameObject* ball = s.CreateGameObject("Ball");
        ball->transform->localPosition = {0.6f, 1.5f, 0.3f};
        auto* rb = ball->AddComponent<Rigidbody3D>();
        rb->drag = 0.6f; rb->friction = 0.5f;
        ball->AddComponent<SphereCollider3D>()->radius = 0.5f;
        s.Start();
        float maxR = 0;
        for (int i = 0; i < 900; ++i) {
            s.Update(1.0f / 60.0f);
            if (i > 60) maxR = std::max(maxR, ball->transform->Position().Magnitude());
        }
        Vec3 pb = ball->transform->Position();
        CHECK(maxR < 2.5f + 0.15f);                        // never left the bowl surface
        CHECK(pb.y < -2.2f);                               // near the bottom (centre at -2.5 when resting)
        CHECK(rb->velocity.Magnitude() < 0.6f);
        CHECK(rb->groundedOnTerrain || pb.y < -2.2f);
    }

    // ---- A box body rests flat on a mesh floor ----
    {
        Scene s("boxrest");
        AddMesh(s, "Floor", Mesh::Plane(20.0f));
        GameObject* b = s.CreateGameObject("Crate");
        b->transform->localPosition = {0, 3, 0};
        b->AddComponent<Rigidbody3D>();
        b->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        s.Start();
        for (int i = 0; i < 240; ++i) s.Update(1.0f / 60.0f);
        CHECK_NEAR(b->transform->Position().y, 0.5f, 0.06f);
    }

    // ---- Scale, rotation and non-uniform scale ----
    {
        Scene s("xform");
        GameObject* g = AddMesh(s, "Slab", Mesh::Cube(1.0f));
        g->transform->localScale = {4, 1, 1};
        g->transform->localRotation = Quat::AngleAxis(90.0f, {0, 1, 0});     // long axis now along world z
        s.Start(); s.Update(0.0f);
        Physics3D& p = s.physics3D();
        RaycastHit3D h = p.Raycast(s, {0, 0, 10}, {0, 0, -1});
        CHECK(h.hit); CHECK_NEAR(h.distance, 8.0f, 1e-3f); CHECK_NEAR(h.normal.z, 1.0f, 1e-3f);
        h = p.Raycast(s, {10, 0, 0}, {-1, 0, 0});
        CHECK(h.hit); CHECK_NEAR(h.distance, 9.5f, 1e-3f);
        CHECK(p.Raycast(s, {1.5f, 5, 0}, {0, -1, 0}).hit == false);
        CHECK(p.Raycast(s, {0, 5, 1.5f}, {0, -1, 0}).hit == true);
        Vec3 mn, mx; g->GetComponent<MeshCollider3D>()->WorldAABB(mn, mx);
        CHECK_NEAR(mn.z, -2.0f, 1e-3f); CHECK_NEAR(mx.x, 0.5f, 1e-3f);
        // Moving the object needs no rebuild and the collider follows.
        int builds = g->GetComponent<MeshCollider3D>()->BuildCount();
        g->transform->localPosition = {0, 10, 0};
        h = p.Raycast(s, {0, 20, 0}, {0, -1, 0});
        CHECK(h.hit); CHECK_NEAR(h.distance, 9.5f, 1e-3f);
        CHECK(g->GetComponent<MeshCollider3D>()->BuildCount() == builds);

        Scene e("ellipsoid");
        GameObject* el = AddMesh(e, "El", Mesh::Sphere(1.0f, 24, 32));
        el->transform->localScale = {2, 1, 1};
        e.Start(); e.Update(0.0f);
        h = e.physics3D().Raycast(e, {10, 0, 0}, {-1, 0, 0});
        CHECK(h.hit); CHECK_NEAR(h.point.x, 2.0f, 0.03f);
        CHECK(e.physics3D().OverlapSphere(e, {2.2f, 0, 0}, 0.3f).size() == 1);
        CHECK(e.physics3D().OverlapSphere(e, {0, 1.5f, 0}, 0.3f).empty());
        // Parent with rotation + scale: the child's triangles follow the whole chain.
        Scene pr("parent");
        GameObject* par = pr.CreateGameObject("Parent");
        par->transform->localScale = {2, 2, 2};
        par->transform->localRotation = Quat::AngleAxis(90.0f, {0, 0, 1});
        GameObject* ch = AddMesh(pr, "Child", Mesh::Cube(1.0f), {1, 0, 0});
        ch->transform->SetParent(par->transform);
        ch->transform->localScale = {1, 1, 1}; ch->transform->localRotation = Quat::Identity;
        ch->transform->localPosition = {1, 0, 0};                             // parent's +x -> world +y, x2 -> y=2
        pr.Start(); pr.Update(0.0f);
        h = pr.physics3D().Raycast(pr, {0, 10, 0}, {0, -1, 0});
        CHECK(h.hit); CHECK_NEAR(h.point.y, 3.0f, 1e-3f);                    // cube spans y in [1,3]
    }

    // ---- Offset only shapes the no-triangles fallback box; layers filter; triggers fire ----
    {
        Scene s("opts");
        GameObject* g = AddMesh(s, "Slab", Box({-1, -0.5f, -1}, {1, 0.5f, 1}));
        auto* mc = g->GetComponent<MeshCollider3D>();
        mc->offset = {0, 5, 0};                          // Fit writes the mesh-bounds centre here: must not move triangles
        GameObject* nm = s.CreateGameObject("NoMesh");
        nm->transform->localPosition = {20, 0, 0};
        auto* box = nm->AddComponent<MeshCollider3D>(); box->size = {2, 2, 2}; box->offset = {0, 5, 0};
        s.Start(); s.Update(0.0f);
        RaycastHit3D h = s.physics3D().Raycast(s, {0, 10, 0}, {0, -1, 0});
        CHECK(h.hit); CHECK_NEAR(h.point.y, 0.5f, 1e-3f);
        h = s.physics3D().Raycast(s, {20, 10, 0}, {0, -1, 0});     // fallback box honours the offset
        CHECK(h.hit); CHECK_NEAR(h.point.y, 6.0f, 1e-3f);
        // A layer the ball ignores: it falls straight through.
        Scene l("layers");
        GameObject* fl = AddMesh(l, "Floor", Mesh::Plane(20.0f));
        fl->GetComponent<MeshCollider3D>()->layer = 5;
        GameObject* ball = l.CreateGameObject("Ball");
        ball->transform->localPosition = {0, 2, 0};
        ball->AddComponent<Rigidbody3D>();
        ball->AddComponent<SphereCollider3D>()->layer = 3;
        l.physics3D().SetLayerCollision(3, 5, false);
        l.Start();
        for (int i = 0; i < 120; ++i) l.Update(1.0f / 60.0f);
        CHECK(ball->transform->Position().y < -2.0f);
        // Trigger mesh: fires when a body overlaps real triangles, not when it sits in the notch.
        Scene t("trig");
        GameObject* tg = AddMesh(t, "TriggerL", LShape());
        tg->GetComponent<MeshCollider3D>()->isTrigger = true;
        GameObject* inNotch = t.CreateGameObject("InNotch");
        inNotch->transform->localPosition = {3, 0.5f, 3};
        inNotch->AddComponent<SphereCollider3D>()->radius = 0.4f;
        auto* c1 = inNotch->AddComponent<Counter>();
        GameObject* onArm = t.CreateGameObject("OnArm");
        onArm->transform->localPosition = {3, 0.5f, 0.5f};
        onArm->AddComponent<SphereCollider3D>()->radius = 0.6f;
        auto* c2 = onArm->AddComponent<Counter>();
        t.Start(); t.Update(1.0f / 60.0f); t.Update(1.0f / 60.0f);
        CHECK(c1->trigEnter == 0);
        CHECK(c2->trigEnter >= 1);
        // A trigger mesh does not block.
        GameObject* fall = t.CreateGameObject("Fall");
        fall->transform->localPosition = {3.8f, 4, 0.5f};
        fall->AddComponent<Rigidbody3D>();
        fall->AddComponent<SphereCollider3D>()->radius = 0.2f;
        for (int i = 0; i < 90; ++i) t.Update(1.0f / 60.0f);
        CHECK(fall->transform->Position().y < -1.0f);
    }

    // ---- dynamic_mesh_convex_hull: a dynamic mesh body collides as the convex hull ----
    {
        std::cout << "[test] dynamic_mesh_convex_hull\n";
        Mesh cube = Box({-1, -1, -1}, {1, 1, 1});
        Scene s("dyn");
        GameObject* g = AddMesh(s, "Both", cube, {0, 0, 0});
        auto* mc = g->GetComponent<MeshCollider3D>();
        CHECK(mc->StatusMessage().empty());
        CHECK(!mc->UsesConvexHull());                         // no rigidbody yet: exact triangles
        auto* rb = g->AddComponent<Rigidbody3D>();
        CHECK(mc->IsDynamicBody());
        CHECK(mc->UsesConvexHull());
        CHECK(!mc->IsDynamicConflict());
        CHECK(mc->StatusMessage().find("ignored") == std::string::npos);
        const Mesh& hull = mc->Hull();
        CHECK(hull.vertices.size() >= 4);
        CHECK(hull.triangles.size() >= 12);                   // a cube hull is 12 triangles
        CHECK(SourceInsideHull(hull, cube));
        int unmatched = 0;
        for (const Vec3& hv : hull.vertices) {
            bool found = false;
            for (const Vec3& sv : cube.vertices)
                if ((hv - sv).SqrMagnitude() < 1e-8f) { found = true; break; }
            if (!found) ++unmatched;
        }
        CHECK(unmatched == 0);                                // volume mesh: hull vertices are mesh vertices
        // Kinematic keeps the exact triangles (a moving platform), not the hull.
        rb->bodyType = Rigidbody3D::BodyType::Kinematic;
        CHECK(!mc->UsesConvexHull());
        CHECK(!mc->IsDynamicConflict());
        rb->bodyType = Rigidbody3D::BodyType::Dynamic;
        CHECK(mc->UsesConvexHull());
        // A heavy dynamic cube (hull half-extent 1, not the size-1 fallback box) holds a ball.
        // Fallback box half-extent is 0.5, so a ball of radius 0.5 would rest near y = 1.
        // The hull's top is y = 1, so the ball rests near y = 1.5. No hull at all: it falls through.
        rb->gravityScale = 0.0f;
        rb->mass = 1.0e6f;
        GameObject* ball = s.CreateGameObject("Ball");
        ball->transform->localPosition = {0, 5, 0};
        ball->AddComponent<Rigidbody3D>();
        ball->AddComponent<SphereCollider3D>()->radius = 0.5f;
        auto* landed = ball->AddComponent<CollisionCounter>();
        s.Start();
        for (int i = 0; i < 180; ++i) s.Update(1.0f / 60.0f);
        CHECK(ball->transform->Position().y > 1.25f);
        CHECK(ball->transform->Position().y < 1.9f);
        CHECK(g->transform->Position().y > -0.2f);
        CHECK(landed->enter >= 1);                            // callback on the convex collider
        CHECK(landed->other == g);
        CHECK(s.physics3D().Raycast(s, {0, 9, 0}, {0, -1, 0}, 20.0f, ball).hit);

        // The hull fills a concave notch. Exact triangles do not.
        Scene notch("notch");
        GameObject* stat = AddMesh(notch, "StaticL", LShape());
        GameObject* dyn = AddMesh(notch, "DynamicL", LShape(), {10, 0, 0});
        auto* drb = dyn->AddComponent<Rigidbody3D>();
        drb->gravityScale = 0.0f; drb->mass = 1.0e6f;
        notch.Start(); notch.Update(0.0f);
        CHECK(!notch.physics3D().Raycast(notch, {2, 5, 2}, {0, -1, 0}).hit);          // notch is empty
        RaycastHit3D nh = notch.physics3D().Raycast(notch, {12, 5, 2}, {0, -1, 0});   // same point on the hull
        CHECK(nh.hit);
        CHECK(nh.collider == dyn->GetComponent<MeshCollider3D>());
        CHECK_NEAR(nh.point.y, 1.0f, 1e-2f);
        // Deep in the notch, away from every face: a shell query misses both. A sphere that
        // reaches the hull's diagonal face (and not the exact L's walls) hits only the hull.
        CHECK(notch.physics3D().OverlapSphere(notch, {2.2f, 0.5f, 2.2f}, 0.5f).empty());
        CHECK(!notch.physics3D().OverlapSphere(notch, {12.2f, 0.5f, 2.2f}, 0.5f).empty());
        CHECK(SourceInsideHull(dyn->GetComponent<MeshCollider3D>()->Hull(), LShape()));

        // A dynamic cube rests on a kinematic mesh cube by the hull's extent (top at y = 1),
        // not by the fallback box (half-extent 0.5 would stop the centre near y = 1.5).
        Scene rest("hullrest");
        GameObject* plat = AddMesh(rest, "Plat", cube);
        plat->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Kinematic;
        GameObject* crate = AddMesh(rest, "Crate", cube, {0, 4, 0});
        crate->AddComponent<Rigidbody3D>();
        rest.Start();
        for (int i = 0; i < 240; ++i) rest.Update(1.0f / 60.0f);
        CHECK(crate->transform->Position().y > 1.7f);
        CHECK(crate->transform->Position().y < 2.4f);

        // A kinematic platform carrying a mesh collider does support a body on it.
        Scene k("kin");
        GameObject* plat2 = AddMesh(k, "Plat", Mesh::Plane(10.0f));
        plat2->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Kinematic;
        GameObject* b2 = k.CreateGameObject("B2");
        b2->transform->localPosition = {0, 3, 0};
        b2->AddComponent<Rigidbody3D>();
        b2->AddComponent<SphereCollider3D>();
        k.Start();
        for (int i = 0; i < 180; ++i) k.Update(1.0f / 60.0f);
        CHECK(b2->transform->Position().y > 0.2f);
        (void)stat;
    }

    // ---- mesh_landing_collision_callbacks: a body landing on a solid mesh fires Enter/Stay/Exit ----
    {
        std::cout << "[test] mesh_landing_collision_callbacks\n";
        Scene s("landcb");
        GameObject* floor = AddMesh(s, "Floor", Mesh::Plane(20.0f));
        GameObject* ball = s.CreateGameObject("Ball");
        ball->transform->localPosition = {0, 3, 0};
        auto* rb = ball->AddComponent<Rigidbody3D>();
        rb->allowSleep = false;
        ball->AddComponent<SphereCollider3D>()->radius = 0.5f;
        auto* hits = ball->AddComponent<CollisionCounter>();
        auto* floorHits = floor->AddComponent<CollisionCounter>();
        s.Start();
        for (int i = 0; i < 180; ++i) s.Update(1.0f / 60.0f);
        CHECK(ball->transform->Position().y > 0.2f);
        CHECK(hits->enter >= 1);
        CHECK(hits->stay >= 1);
        CHECK(hits->exit == 0);
        CHECK(hits->other == floor);
        CHECK(hits->enterPen > 0.0f);
        CHECK(floorHits->enter >= 1);
        CHECK(floorHits->other == ball);
        int exits = hits->exit;
        ball->transform->localPosition = {0, 30, 0};
        rb->velocity = Vec3::Zero;
        rb->WakeUp();
        s.Update(1.0f / 60.0f);
        s.Update(1.0f / 60.0f);
        CHECK(hits->exit > exits);
        CHECK(floorHits->exit >= 1);
    }

    // ---- Degenerate / empty meshes ----
    {
        Scene s("degenerate");
        // No MeshRenderer: acts as its Size box, and says so.
        GameObject* a = s.CreateGameObject("NoMesh");
        auto* ma = a->AddComponent<MeshCollider3D>(); ma->size = {2, 2, 2};
        CHECK(!ma->HasTriangles());
        CHECK(!ma->StatusMessage().empty());
        // Empty mesh: same fallback.
        GameObject* b = s.CreateGameObject("Empty");
        b->transform->localPosition = {10, 0, 0};
        b->AddComponent<MeshRenderer>()->mesh = Mesh{};
        auto* mb = b->AddComponent<MeshCollider3D>(); mb->size = {2, 2, 2};
        CHECK(!mb->HasTriangles());
        // Only degenerate triangles: zero area, NaN, bad indices.
        Mesh bad;
        bad.vertices = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {0, 0, 0}, {NAN, 0, 0}, {0, 1, 0}};
        bad.triangles = {0, 1, 2, 0, 1, 99, -1, 0, 1, 3, 4, 5};
        GameObject* c = s.CreateGameObject("Bad");
        c->transform->localPosition = {20, 0, 0};
        c->AddComponent<MeshRenderer>()->mesh = bad;
        auto* mcb = c->AddComponent<MeshCollider3D>(); mcb->size = {2, 2, 2};
        // A mesh with both good and bad triangles keeps the good ones.
        Mesh mixed = Box({-1, -1, -1}, {1, 1, 1});
        mixed.triangles.insert(mixed.triangles.end(), {0, 0, 0, 0, 1, 99});
        GameObject* d = s.CreateGameObject("Mixed");
        d->transform->localPosition = {30, 0, 0};
        d->AddComponent<MeshRenderer>()->mesh = mixed;
        auto* mmx = d->AddComponent<MeshCollider3D>();
        // Singular scale: nothing to hit, no crash.
        GameObject* z = AddMesh(s, "Flat", Mesh::Cube(1.0f), {40, 0, 0});
        z->transform->localScale = {0, 1, 1};
        s.Start(); s.Update(0.0f);
        CHECK(!mcb->HasTriangles());
        CHECK(mcb->Tris().DroppedTriangles() == 4);                        // all four were rejected
        CHECK(s.physics3D().Raycast(s, {10, 5, 0}, {0, -1, 0}).hit);     // empty mesh -> its box
        CHECK(mmx->HasTriangles()); CHECK(mmx->Tris().TriangleCount() == 12);
        CHECK(mmx->Tris().DroppedTriangles() == 2);
        CHECK(s.physics3D().Raycast(s, {30, 5, 0}, {0, -1, 0}).hit);
        CHECK(!s.physics3D().Raycast(s, {40, 5, 0}, {0, -1, 0}).hit);    // zero scale: no volume
        for (int i = 0; i < 10; ++i) s.Update(1.0f / 60.0f);
        // Garbage queries never crash.
        s.physics3D().Raycast(s, {0, 0, 0}, {0, 0, 0});
        s.physics3D().OverlapSphere(s, {NAN, 0, 0}, 1.0f);
        s.physics3D().Raycast(s, {30, 5, 0}, {NAN, -1, 0});
    }

    // ---- The BVH follows mesh edits (vertex edit, replaced mesh, new triangles) ----
    {
        Scene s("rebuild");
        GameObject* g = AddMesh(s, "Slab", Box({-1, -1, -1}, {1, 1, 1}));
        auto* mc = g->GetComponent<MeshCollider3D>();
        auto* mr = g->GetComponent<MeshRenderer>();
        s.Start(); s.Update(0.0f);
        CHECK_NEAR(s.physics3D().Raycast(s, {0, 20, 0}, {0, -1, 0}).point.y, 1.0f, 1e-4f);
        int b0 = mc->BuildCount();
        for (int i = 0; i < 5; ++i) { s.Update(0.0f); s.physics3D().Raycast(s, {0, 20, 0}, {0, -1, 0}); }
        CHECK(mc->BuildCount() == b0);                                     // unchanged mesh: no rebuild
        for (Vec3& v : mr->mesh.vertices) v.y += 10.0f;                    // in-place vertex edit
        s.Update(0.0f);                                                    // contents are re-checked once per frame
        CHECK_NEAR(s.physics3D().Raycast(s, {0, 20, 0}, {0, -1, 0}).point.y, 11.0f, 1e-4f);
        CHECK(mc->BuildCount() == b0 + 1);
        mr->mesh = Mesh::Plane(4.0f);                                      // replaced mesh: noticed at once
        CHECK(mc->Tris().TriangleCount() == 2);
        CHECK_NEAR(s.physics3D().Raycast(s, {0, 20, 0}, {0, -1, 0}).point.y, 0.0f, 1e-4f);
        mr->mesh.vertices.push_back({0, 5, 0}); mr->mesh.vertices.push_back({1, 5, 0}); mr->mesh.vertices.push_back({0, 5, 1});
        int n = (int)mr->mesh.vertices.size();
        mr->mesh.triangles.insert(mr->mesh.triangles.end(), {n - 3, n - 2, n - 1});
        CHECK(mc->Tris().TriangleCount() == 3);                            // grown mesh: noticed at once
        mc->Invalidate(); CHECK(mc->HasTriangles());
    }

    // ---- 100k triangles: build and queries stay fast, and agree with brute force ----
    {
        Mesh big = Mesh::Grid(100.0f, 100.0f, 224, 224);                   // 100,352 triangles
        for (Vec3& v : big.vertices) v.y = 0.5f * std::sin(v.x * 0.4f) * std::cos(v.z * 0.3f);
        big.name = "";
        CHECK(big.TriangleCount() > 100000);
        Scene s("big");
        auto* mc = AddMesh(s, "Terrain", big)->GetComponent<MeshCollider3D>();
        auto t0 = std::chrono::steady_clock::now();
        s.Start(); s.Update(0.0f);
        CHECK(mc->HasTriangles());
        double buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(buildMs < 8000.0);
        CHECK(mc->Tris().TriangleCount() == big.TriangleCount());

        std::srand(11);
        auto rnd = [] { return (float)std::rand() / RAND_MAX; };
        int mismatches = 0;
        for (int i = 0; i < 100; ++i) {
            Vec3 o{-45 + 90 * rnd(), 5, -45 + 90 * rnd()};
            Vec3 d = Vec3{rnd() - 0.5f, -1.0f, rnd() - 0.5f}.Normalized();
            float tb; bool bh = BruteRay(big, o, d, tb);
            RaycastHit3D h = s.physics3D().Raycast(s, o, d);
            if (h.hit != bh || (bh && std::fabs(h.distance - tb) > 1e-3f)) ++mismatches;
        }
        CHECK(mismatches == 0);

        t0 = std::chrono::steady_clock::now();
        int hits = 0;
        for (int i = 0; i < 3000; ++i) {
            Vec3 o{-45 + 90 * rnd(), 5, -45 + 90 * rnd()};
            hits += s.physics3D().Raycast(s, o, {0.2f, -1, 0.1f}).hit ? 1 : 0;
            hits += s.physics3D().OverlapSphere(s, {o.x, 0, o.z}, 0.6f).empty() ? 0 : 1;
        }
        double queryMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(hits > 3000);
        CHECK(queryMs < 3000.0);                                           // 6000 queries (brute force would take ~minutes)
        std::cout << "  [perf] build " << buildMs << " ms, 6000 queries " << queryMs << " ms\n";
    }

    // ---- Serialization round trip: mesh + collider settings survive and still collide ----
    {
        Scene s("ser");
        GameObject* g = AddMesh(s, "LRoom", LShape(), {2, 0, 0});
        g->GetComponent<MeshRenderer>()->mesh.name = "";
        auto* mc = g->GetComponent<MeshCollider3D>();
        mc->offset = {0, 1, 0}; mc->layer = 7; mc->isTrigger = false; mc->size = {3, 3, 3};
        g->transform->localScale = {1, 2, 1};
        s.Start(); s.Update(0.0f);
        std::string txt = SceneSerializer::Serialize(s);
        Scene l("loaded");
        CHECK(SceneSerializer::Deserialize(l, txt));
        GameObject* lg = l.Find("LRoom");
        CHECK(lg != nullptr);
        if (lg) {
            auto* lm = lg->GetComponent<MeshCollider3D>();
            CHECK(lm != nullptr);
            if (lm) {
                CHECK(lm->layer == 7); CHECK_NEAR(lm->offset.y, 1.0f, 1e-5f); CHECK_NEAR(lm->size.x, 3.0f, 1e-5f);
                l.Start(); l.Update(0.0f);
                CHECK(lm->HasTriangles()); CHECK(lm->Tris().TriangleCount() == mc->Tris().TriangleCount());
                // Same answers before and after: arm top at y = 1 * 2 (scale) = 2; the notch is open.
                RaycastHit3D a = s.physics3D().Raycast(s, {5, 9, 0.5f}, {0, -1, 0});
                RaycastHit3D b = l.physics3D().Raycast(l, {5, 9, 0.5f}, {0, -1, 0});
                CHECK(a.hit && b.hit); CHECK_NEAR(a.distance, b.distance, 1e-4f); CHECK_NEAR(b.point.y, 2.0f, 1e-3f);
                CHECK(!l.physics3D().Raycast(l, {5, 9, 3}, {0, -1, 0}).hit);
                CHECK(l.physics3D().Raycast(l, {-5, 2, 3}, {1, 0, 0}).hit);
            }
        }
        // Serialising twice is stable.
        CHECK(SceneSerializer::Serialize(l) == SceneSerializer::Serialize(l));
    }

    TEST_MAIN_RESULT();
}
