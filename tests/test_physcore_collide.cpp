// okay::phys shapes and narrow phase (our own physics core).
//  - Convex hulls: a cube's corners plus inner points give 8 vertices, 12 triangles, volume 8; flat and tiny inputs
//    still give a solid.
//  - Mass properties: each closed-form solid against the same solid built as a fine hull (box exactly; sphere,
//    cylinder and cone within a few percent), parallel-axis numbers for the capsule.
//  - GJK distance against known answers; EPA depth checked by moving the shapes apart: by depth + a hair they are
//    separate, by depth - a little they still touch; for random pairs of every shape type in random poses.
//  - Manifolds: box on box (4 points, normal up), sphere on a face (1), capsule lying on a box (2), cylinder standing
//    on a plane (4), cone on its side (2), two capsules side by side (2), speculative points for separated shapes.
#include "test_framework.hpp"
#include "okay/Physics/Core/Collide.hpp"

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <vector>

using namespace okay::phys;

namespace {
struct Lcg {
    std::uint64_t s;
    explicit Lcg(std::uint64_t seed) : s(seed) {}
    std::uint32_t Next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return (std::uint32_t)(s >> 33); }
    float Unit() { return (float)(Next() & 0xFFFFFF) / 16777216.0f; }
    float Range(float a, float b) { return a + (b - a) * Unit(); }
};
constexpr float kPi = 3.14159265358979f;

Q RandomRot(Lcg& r) {
    return NormalizeQ(Q{r.Range(-1, 1), r.Range(-1, 1), r.Range(-1, 1), r.Range(-1, 1)});
}

Shape RandomShape(Lcg& r, int kind) {
    switch (kind % 6) {
        case 0: return Shape::Sphere(r.Range(0.2f, 1.0f));
        case 1: return Shape::Box(V3{r.Range(0.2f, 1.0f), r.Range(0.2f, 1.0f), r.Range(0.2f, 1.0f)});
        case 2: return Shape::Capsule(r.Range(0.2f, 0.6f), r.Range(0.2f, 0.8f));
        case 3: return Shape::Cylinder(r.Range(0.2f, 0.8f), r.Range(0.2f, 0.8f));
        case 4: return Shape::Cone(r.Range(0.3f, 0.8f), r.Range(0.3f, 0.8f));
        default: {
            std::vector<V3> p;
            for (int i = 0; i < 20; ++i) p.push_back(V3{r.Range(-0.7f, 0.7f), r.Range(-0.7f, 0.7f), r.Range(-0.7f, 0.7f)});
            return Shape::Hull(p);
        }
    }
}

std::vector<V3> SpherePoints(float radius, int rings, int segs) {
    std::vector<V3> p;
    for (int i = 0; i <= rings; ++i) {
        const float th = kPi * (float)i / (float)rings;
        for (int j = 0; j < segs; ++j) {
            const float ph = 2.0f * kPi * (float)j / (float)segs;
            p.push_back(V3{radius * std::sin(th) * std::cos(ph), radius * std::cos(th), radius * std::sin(th) * std::sin(ph)});
        }
    }
    return p;
}
} // namespace

int main() {
    RUN_SUITE("physcore hull: cube corners and inner points");
    {
        std::vector<V3> p;
        for (int i = 0; i < 8; ++i) p.push_back(V3{(i & 1) ? 1.f : -1.f, (i & 2) ? 1.f : -1.f, (i & 4) ? 1.f : -1.f});
        Lcg r(3);
        for (int i = 0; i < 50; ++i) p.push_back(V3{r.Range(-0.9f, 0.9f), r.Range(-0.9f, 0.9f), r.Range(-0.9f, 0.9f)});
        const auto h = HullData::Build(p);
        CHECK(h->verts.size() == 8);
        CHECK(h->tris.size() == 12);
        CHECK_NEAR(h->volume, 8.0, 1e-4);
        CHECK_NEAR(Len(h->centroid), 0.0, 1e-5);
        // flat input (a square) and a single point still give a thin solid
        const auto flat = HullData::Build({V3{0, 0, 0}, V3{1, 0, 0}, V3{1, 0, 1}, V3{0, 0, 1}});
        CHECK(flat->volume > 0.0f && flat->tris.size() >= 4);
        const auto one = HullData::Build({V3{2, 2, 2}});
        CHECK(one->volume > 0.0f && one->verts.size() == 8);
    }

    RUN_SUITE("physcore mass: closed forms against fine hulls");
    {
        const Shape box = Shape::Box(V3{0.5f, 1.0f, 1.5f});
        std::vector<V3> corners;
        for (int i = 0; i < 8; ++i) corners.push_back(V3{(i & 1) ? .5f : -.5f, (i & 2) ? 1.f : -1.f, (i & 4) ? 1.5f : -1.5f});
        const MassProps mb = box.Mass(2.0f), mh = Shape::Hull(corners).Mass(2.0f);
        CHECK_NEAR(mb.mass, 12.0, 1e-4);
        CHECK_NEAR(mh.mass, mb.mass, 1e-3);
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) CHECK_NEAR(mh.inertia.m[i][j], mb.inertia.m[i][j], 1e-3);

        const MassProps ms = Shape::Sphere(1.0f).Mass(1.0f), msh = Shape::Hull(SpherePoints(1.0f, 32, 64)).Mass(1.0f);
        CHECK_NEAR(msh.mass / ms.mass, 1.0, 0.02);
        CHECK_NEAR(msh.inertia.m[1][1] / ms.inertia.m[1][1], 1.0, 0.03);

        std::vector<V3> cyl, cone;
        for (int j = 0; j < 128; ++j) {
            const float a = 2.0f * kPi * (float)j / 128.0f;
            cyl.push_back(V3{0.5f * std::cos(a), -0.8f, 0.5f * std::sin(a)});
            cyl.push_back(V3{0.5f * std::cos(a), 0.8f, 0.5f * std::sin(a)});
            cone.push_back(V3{0.5f * std::cos(a), -0.8f, 0.5f * std::sin(a)});
        }
        cone.push_back(V3{0, 0.8f, 0});
        const MassProps mc = Shape::Cylinder(0.5f, 0.8f).Mass(1.0f), mch = Shape::Hull(cyl).Mass(1.0f);
        CHECK_NEAR(mch.mass / mc.mass, 1.0, 0.01);
        CHECK_NEAR(mch.inertia.m[0][0] / mc.inertia.m[0][0], 1.0, 0.01);
        CHECK_NEAR(mch.inertia.m[1][1] / mc.inertia.m[1][1], 1.0, 0.01);
        const MassProps mo = Shape::Cone(0.5f, 0.8f).Mass(1.0f), moh = Shape::Hull(cone).Mass(1.0f);
        CHECK_NEAR(moh.mass / mo.mass, 1.0, 0.01);
        CHECK_NEAR(moh.com.y, mo.com.y, 1e-3);
        CHECK_NEAR(moh.inertia.m[0][0] / mo.inertia.m[0][0], 1.0, 0.01);
        CHECK_NEAR(moh.inertia.m[1][1] / mo.inertia.m[1][1], 1.0, 0.01);

        // capsule: a fine hull of a capsule's points (cylinder plus two hemispheres)
        std::vector<V3> cap;
        for (const V3& v : SpherePoints(0.4f, 32, 64)) cap.push_back(v + V3{0, v.y >= 0 ? 0.6f : -0.6f, 0});
        const MassProps mk = Shape::Capsule(0.4f, 0.6f).Mass(1.0f), mkh = Shape::Hull(cap).Mass(1.0f);
        CHECK_NEAR(mkh.mass / mk.mass, 1.0, 0.02);
        CHECK_NEAR(mkh.inertia.m[0][0] / mk.inertia.m[0][0], 1.0, 0.03);
        CHECK_NEAR(mkh.inertia.m[1][1] / mk.inertia.m[1][1], 1.0, 0.03);
    }

    RUN_SUITE("physcore GJK: known distances");
    {
        const Shape s1 = Shape::Sphere(1.0f), b1 = Shape::Box(V3{1, 1, 1});
        DistanceResult d = GjkDistance(s1, Xf{V3{0, 0, 0}, Q{}}, s1, Xf{V3{5, 0, 0}, Q{}});
        CHECK(!d.overlap);
        CHECK_NEAR(d.distance, 5.0, 1e-5);   // cores are points: radius not included
        d = GjkDistance(b1, Xf{V3{0, 0, 0}, Q{}}, b1, Xf{V3{3, 0.5f, 0}, Q{}});
        CHECK(!d.overlap);
        CHECK_NEAR(d.distance, 1.0, 1e-5);
        CHECK_NEAR(d.normal.x, 1.0, 1e-5);
        // a box rotated 45 degrees about z: its corner is sqrt(2) from its centre
        d = GjkDistance(b1, Xf{V3{0, 0, 0}, Q{}}, b1, Xf{V3{4, 0, 0}, Q::AxisAngle(V3{0, 0, 1}, kPi / 4)});
        CHECK_NEAR(d.distance, 3.0 - std::sqrt(2.0), 1e-4);
        d = GjkDistance(b1, Xf{V3{0, 0, 0}, Q{}}, b1, Xf{V3{1.5f, 0, 0}, Q{}});
        CHECK(d.overlap);
        // far from the world origin the answer is the same
        d = GjkDistance(b1, Xf{V3{10000, 0, 0}, Q{}}, b1, Xf{V3{10003, 0.5f, 0}, Q{}});
        CHECK_NEAR(d.distance, 1.0, 2e-3);
    }

    RUN_SUITE("physcore EPA: depth separates exactly, random pairs of every shape");
    {
        Lcg r(99);
        int tested = 0, depthOk = 0, normalOk = 0;
        for (int it = 0; it < 600; ++it) {
            const Shape a = RandomShape(r, it), b = RandomShape(r, it / 6);
            if (a.CoreRadius() > 0 && b.CoreRadius() > 0) continue;
            const Xf xa{V3{r.Range(-0.3f, 0.3f), r.Range(-0.3f, 0.3f), r.Range(-0.3f, 0.3f)}, RandomRot(r)};
            const Xf xb{V3{r.Range(-0.6f, 0.6f), r.Range(-0.6f, 0.6f), r.Range(-0.6f, 0.6f)}, RandomRot(r)};
            const DistanceResult g = GjkDistance(a, xa, b, xb);
            if (!g.overlap) continue;
            const PenetrationResult p = EpaPenetration(a, xa, b, xb);
            if (!p.ok) continue;
            ++tested;
            const float tol = 0.01f;
            const Xf apart{xb.p + p.normal * (p.depth + tol), xb.q};
            const Xf less{xb.p + p.normal * std::max(p.depth - tol, 0.0f), xb.q};
            const bool sep = !GjkDistance(a, xa, b, apart).overlap;
            const bool still = p.depth <= tol || GjkDistance(a, xa, b, less).overlap;
            depthOk += (sep && still);
            if (!(sep && still) && std::getenv("PHYS_VERBOSE"))
                std::cout << "    kinds " << (int)a.type << "," << (int)b.type << " depth " << p.depth << " sep " << sep << " still " << still << "\n";
            normalOk += std::fabs(Len(p.normal) - 1.0f) < 1e-4f;
        }
        std::cout << "  " << tested << " overlapping pairs\n";
        CHECK(tested > 100);
        CHECK(depthOk >= tested * 97 / 100);   // EPA on curved shapes is an approximation (a polytope), a few may be off
        CHECK(normalOk == tested);
    }

    RUN_SUITE("physcore manifolds");
    {
        Manifold m;
        const Shape b1 = Shape::Box(V3{1, 1, 1});
        // box resting on a box, overlapping by 0.01
        CHECK(Collide(b1, Xf{V3{0, 0, 0}, Q{}}, b1, Xf{V3{0.3f, 1.99f, 0.2f}, Q{}}, 0.02f, m));
        CHECK(m.count == 4);
        CHECK_NEAR(m.normal.y, 1.0, 1e-4);
        bool depths = true;
        for (int i = 0; i < m.count; ++i) depths = depths && std::fabs(m.pts[i].separation + 0.01f) < 1e-4f;
        CHECK(depths);
        // the same box slightly tilted still gets 4 points, with different separations
        CHECK(Collide(b1, Xf{V3{0, 0, 0}, Q{}}, b1, Xf{V3{0, 2.0f, 0}, Q::AxisAngle(V3{1, 0, 0}, 0.02f)}, 0.05f, m));
        CHECK(m.count == 4);
        // sphere on a face
        CHECK(Collide(b1, Xf{V3{0, 0, 0}, Q{}}, Shape::Sphere(0.5f), Xf{V3{0.2f, 1.45f, 0}, Q{}}, 0.0f, m));
        CHECK(m.count == 1);
        CHECK_NEAR(m.pts[0].separation, -0.05, 1e-4);
        CHECK_NEAR(m.normal.y, 1.0, 1e-4);
        // capsule lying on a box (core along x)
        const Shape cap = Shape::Capsule(0.25f, 0.5f);
        CHECK(Collide(b1, Xf{V3{0, 0, 0}, Q{}}, cap, Xf{V3{0, 1.24f, 0}, Q::AxisAngle(V3{0, 0, 1}, kPi / 2)}, 0.0f, m));
        CHECK(m.count == 2);
        CHECK_NEAR(m.pts[0].separation, -0.01, 1e-4);
        // cylinder standing on a plane
        const Shape ground = Shape::Plane(V3{0, 1, 0}, 0.0f);
        CHECK(Collide(ground, Xf{}, Shape::Cylinder(0.5f, 0.5f), Xf{V3{0, 0.49f, 0}, Q{}}, 0.0f, m));
        CHECK(m.count == 4);
        CHECK_NEAR(m.normal.y, 1.0, 1e-5);
        // the same with the plane second: the normal is flipped
        CHECK(Collide(Shape::Cylinder(0.5f, 0.5f), Xf{V3{0, 0.49f, 0}, Q{}}, ground, Xf{}, 0.0f, m));
        CHECK(m.count == 4);
        CHECK_NEAR(m.normal.y, -1.0, 1e-5);
        // a cone lying on its side on a box: its slanted side line touches (2 points)
        const Shape cone = Shape::Cone(0.5f, 0.5f);
        const float tilt = std::atan2(0.5f, 1.0f);                // the side makes this angle with the axis
        const Q lie = Q::AxisAngle(V3{0, 0, 1}, kPi / 2 + tilt);  // lay it down so one side line is flat
        // find the height that makes it touch the box top, then sink 0.01
        const Xf probe{V3{0, 3, 0}, lie};
        const float lowest = probe.Apply(cone.Support(InvRotate(lie, V3{0, -1, 0}))).y;
        CHECK(Collide(b1, Xf{}, cone, Xf{V3{0, 3 - (lowest - 1.0f) - 0.01f, 0}, lie}, 0.0f, m));
        CHECK(m.count == 2);
        // two capsules side by side
        CHECK(Collide(cap, Xf{V3{0, 0, 0}, Q{}}, cap, Xf{V3{0.45f, 0.2f, 0}, Q{}}, 0.0f, m));
        CHECK(m.count == 2);
        CHECK_NEAR(m.normal.x, 1.0, 1e-4);
        // separated by 0.05 with a speculative distance of 0.1: points with separation 0.05
        CHECK(Collide(b1, Xf{}, b1, Xf{V3{0, 2.05f, 0}, Q{}}, 0.1f, m));
        CHECK(m.count == 4);
        CHECK_NEAR(m.pts[0].separation, 0.05, 1e-4);
        CHECK(!Collide(b1, Xf{}, b1, Xf{V3{0, 2.2f, 0}, Q{}}, 0.1f, m));
        // hull tetrahedron on its face on a plane: 3 points
        const Shape tet = Shape::Hull({V3{-1, 0, -1}, V3{1, 0, -1}, V3{0, 0, 1}, V3{0, 1, 0}});
        CHECK(Collide(ground, Xf{}, tet, Xf{V3{0, -0.01f, 0}, Q{}}, 0.0f, m));
        CHECK(m.count == 3);
        // a box on a triangle (a mesh piece)
        const Shape tri = Shape::Triangle(V3{-5, 0, -5}, V3{0, 0, 5}, V3{5, 0, -5});
        CHECK(Collide(tri, Xf{}, b1, Xf{V3{0, 0.99f, 0}, Q{}}, 0.0f, m));
        CHECK(m.count == 4);
        CHECK_NEAR(m.normal.y, 1.0, 1e-4);
    }

    RUN_SUITE("physcore manifolds: random pairs agree with GJK and EPA");
    {
        Lcg r(5);
        int bad = 0, n = 0;
        for (int it = 0; it < 2000; ++it) {
            const Shape a = RandomShape(r, it), b = RandomShape(r, it / 6 + 1);
            const Xf xa{V3{}, RandomRot(r)};
            const Xf xb{V3{r.Range(-1.6f, 1.6f), r.Range(-1.6f, 1.6f), r.Range(-1.6f, 1.6f)}, RandomRot(r)};
            Manifold m;
            if (!Collide(a, xa, b, xb, 0.1f, m)) continue;
            ++n;
            float minSep = 1e30f;
            for (int i = 0; i < m.count; ++i) minSep = std::min(minSep, m.pts[i].separation);
            const DistanceResult g = GjkDistance(a, xa, b, xb);
            float want;
            if (!g.overlap) want = g.distance - a.CoreRadius() - b.CoreRadius();
            else { const PenetrationResult p = EpaPenetration(a, xa, b, xb); want = -p.depth - a.CoreRadius() - b.CoreRadius(); }
            // the deepest manifold point is about as deep as the true depth (clipping uses the face normal, so allow some)
            if (std::fabs(minSep - want) > 0.05f + 0.1f * std::fabs(want)) ++bad;
            if (!(std::fabs(Len(m.normal) - 1.0f) < 1e-3f) || m.count < 1 || m.count > 4) ++bad;
        }
        std::cout << "  " << n << " touching pairs, " << bad << " off\n";
        CHECK(n > 300);
        CHECK(bad <= n / 50);
    }

    TEST_MAIN_RESULT();
}
