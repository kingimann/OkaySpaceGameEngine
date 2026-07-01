#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

int main() {
    RUN_SUITE("modeling");

    // ---- Mirror: a shape entirely on +X becomes symmetric about x=0 ----
    {
        Mesh m = Mesh::Cube(1.0f);
        for (Vec3& v : m.vertices) v.x += 2.0f;     // shift fully into +X
        int before = (int)m.vertices.size();
        m.Mirror(0, /*weld=*/true);
        Vec3 lo, hi; m.Bounds(lo, hi);
        CHECK(std::fabs(lo.x + hi.x) < 1e-3f);       // symmetric across x=0
        CHECK((int)m.vertices.size() > before);      // a mirrored copy was added
        CHECK(m.name.empty());                       // now custom geometry
    }

    // ---- Spherify: varied vertices all end ~equidistant from the centre ----
    {
        Mesh m = Mesh::Cube(1.0f);
        m.Subdivide(); m.Subdivide();                // more vertices at varied radii
        m.Spherify(1.0f);
        Vec3 lo, hi; m.Bounds(lo, hi);
        Vec3 c{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
        float dmin = 1e9f, dmax = 0.0f;
        for (const Vec3& v : m.vertices) {
            Vec3 d{v.x - c.x, v.y - c.y, v.z - c.z};
            float r = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            dmin = std::min(dmin, r); dmax = std::max(dmax, r);
        }
        CHECK(dmax - dmin < 0.05f * dmax);           // nearly a perfect sphere
    }

    // ---- Twist: the two ends rotate in opposite directions about Y ----
    {
        Mesh m;
        m.vertices = {{1, -1, 0}, {1, 1, 0}, {1, 0, 0.5f}};
        m.triangles = {0, 1, 2};
        m.Twist(90.0f, /*axis=*/1);
        // y=-1 end: (1,-1,0) -> (0,-1,1); y=+1 end: (1,1,0) -> (0,1,-1).
        CHECK(std::fabs(m.vertices[0].x - 0.0f) < 1e-3f);
        CHECK(std::fabs(m.vertices[0].z - 1.0f) < 1e-3f);
        CHECK(std::fabs(m.vertices[1].x - 0.0f) < 1e-3f);
        CHECK(std::fabs(m.vertices[1].z + 1.0f) < 1e-3f);
    }

    // ---- Solidify: a flat surface gains a back face (counts double) ----
    {
        Mesh m = Mesh::Quad(1.0f);
        int v0 = (int)m.vertices.size(), t0 = m.TriangleCount();
        m.Solidify(0.1f);
        CHECK((int)m.vertices.size() == v0 * 2);
        CHECK(m.TriangleCount() == t0 * 2);
    }

    // ---- Taper: top collapses toward the axis, bottom keeps its width ----
    {
        Mesh m = Mesh::Cube(1.0f);                       // verts in [-0.5,0.5]
        m.Taper(1, 0.0f);                                // pinch the top to a point
        float topW = 0.0f, botW = 0.0f;
        for (const Vec3& v : m.vertices) {
            float w = std::sqrt(v.x * v.x + v.z * v.z);  // distance from the Y axis
            if (v.y > 0.0f) topW = std::max(topW, w);
            else            botW = std::max(botW, w);
        }
        CHECK(topW < 0.01f);                             // top pinched to the axis
        CHECK(botW > 0.4f);                              // bottom unchanged
    }

    // ---- Bend: a straight bar curves (its ends rise out of line) ----
    {
        Mesh m = Mesh::Cube(1.0f);
        for (Vec3& v : m.vertices) v.x *= 6.0f;          // a long bar along X
        Vec3 lo0, hi0; m.Bounds(lo0, hi0);
        float flatUp = hi0.y;
        m.Bend(120.0f, 0);                               // bend strongly around X→Y
        Vec3 lo1, hi1; m.Bounds(lo1, hi1);
        CHECK(hi1.y > flatUp + 0.5f);                    // the bar curled upward
        CHECK(hi1.x < hi0.x);                            // and drew its ends inward
    }

    // ---- Array modifier: N copies marching along an offset ----
    {
        Mesh m = Mesh::Cube(1.0f);                        // spans [-0.5, 0.5]
        int t0 = m.TriangleCount();
        m.Array(4, {2.0f, 0.0f, 0.0f});
        CHECK(m.TriangleCount() == t0 * 4);              // four copies of the geometry
        Vec3 lo, hi; m.Bounds(lo, hi);
        CHECK(std::fabs(lo.x + 0.5f) < 1e-3f);           // first copy still at the origin
        CHECK(std::fabs(hi.x - 6.5f) < 1e-3f);           // last copy 3*2 further along
        CHECK(m.name.empty());                           // now custom geometry
    }

    // ---- PointInside: parity test classifies in/out of a closed mesh ----
    {
        Mesh cube = Mesh::Cube(2.0f);                    // spans [-1, 1]
        CHECK(cube.PointInside({0.0f, 0.0f, 0.0f}));     // centre is inside
        CHECK(cube.PointInside({0.5f, -0.5f, 0.5f}));    // off-centre but inside
        CHECK(!cube.PointInside({3.0f, 0.0f, 0.0f}));    // well outside
        CHECK(!cube.PointInside({0.0f, 5.0f, 0.0f}));    // above
    }

    // ---- Voxel remesh: a sphere stays a closed, roughly-spherical solid ----
    {
        Mesh sphere = Mesh::Sphere(1.0f, 12, 16);
        Mesh blocks = Mesh::VoxelRemesh(sphere, 0.25f);
        CHECK(blocks.TriangleCount() > 0);
        Vec3 lo, hi; blocks.Bounds(lo, hi);
        CHECK(std::fabs((hi.x - lo.x) - 2.0f) < 0.5f);   // ~diameter 2 (within a voxel)
        CHECK(std::fabs((hi.y - lo.y) - 2.0f) < 0.5f);
        // The remesh is solid: its centre is inside, a far corner is outside.
        CHECK(blocks.PointInside({0.0f, 0.0f, 0.0f}));
        CHECK(!blocks.PointInside({5.0f, 5.0f, 5.0f}));
    }

    // ---- Boolean difference: punch a bar through a cube → the centre hollows ----
    {
        Mesh box = Mesh::Cube(2.0f);                     // [-1,1]^3
        Mesh bar = Mesh::Cube(1.0f);
        for (Vec3& v : bar.vertices) { v.x *= 0.5f; v.z *= 0.5f; v.y *= 4.0f; }  // tall thin rod through Y
        Mesh diff = Mesh::Boolean(box, bar, Mesh::BoolOp::Difference, 0.2f);
        CHECK(diff.TriangleCount() > 0);
        CHECK(!diff.PointInside({0.0f, 0.0f, 0.0f}));    // the rod carved out the core
        CHECK(diff.PointInside({0.9f, 0.0f, 0.9f}));     // a corner of the box remains solid
    }

    // ---- Boolean union vs intersect of two overlapping cubes ----
    {
        Mesh a = Mesh::Cube(2.0f);                       // [-1,1]^3
        Mesh b = Mesh::Cube(2.0f);
        for (Vec3& v : b.vertices) v.x += 1.0f;          // shifted → overlaps in [0,1] on X
        Mesh uni = Mesh::Boolean(a, b, Mesh::BoolOp::Union, 0.2f);
        Mesh inter = Mesh::Boolean(a, b, Mesh::BoolOp::Intersect, 0.2f);
        // Union spans both; intersect only the shared slab.
        Vec3 ulo, uhi, ilo, ihi; uni.Bounds(ulo, uhi); inter.Bounds(ilo, ihi);
        CHECK(uhi.x - ulo.x > 2.5f);                     // union is wider than one cube
        CHECK(ihi.x - ilo.x < 1.5f);                     // intersection is the thin overlap
        CHECK(uni.PointInside({1.5f, 0.0f, 0.0f}));      // only-B region is in the union
        CHECK(!inter.PointInside({1.5f, 0.0f, 0.0f}));   // but NOT in the intersection
        CHECK(inter.PointInside({0.5f, 0.0f, 0.0f}));    // the shared region is
    }

    // ---- Decimate: vertex clustering cuts triangle count, keeps the silhouette ----
    {
        Mesh m = Mesh::Sphere(1.0f, 16, 24);
        int t0 = m.TriangleCount();
        Vec3 lo0, hi0; m.Bounds(lo0, hi0);
        int t1 = m.Decimate(0.5f);
        Vec3 lo1, hi1; m.Bounds(lo1, hi1);
        CHECK(t1 < t0);                                  // fewer triangles
        CHECK(t1 > 0);
        CHECK(std::fabs((hi1.x - lo1.x) - (hi0.x - lo0.x)) < 0.4f);   // roughly same size
        CHECK(m.name.empty());
    }

    // ---- Convex hull: shell around a point cloud contains every point ----
    {
        std::vector<Vec3> pts = {
            {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
            {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1},
            {0, 0, 0}, {0.3f, 0.2f, -0.1f}          // interior points → ignored by the hull
        };
        Mesh hull = Mesh::ConvexHull(pts);
        CHECK(hull.TriangleCount() >= 4);           // at least a tetra; a cube hull = 12
        Vec3 lo, hi; hull.Bounds(lo, hi);
        CHECK(std::fabs(lo.x + 1.0f) < 1e-3f && std::fabs(hi.x - 1.0f) < 1e-3f);
        // Every original point lies inside (or on) the closed hull.
        CHECK(hull.PointInside({0.0f, 0.0f, 0.0f}));         // interior point is inside
        CHECK(!hull.PointInside({2.0f, 2.0f, 2.0f}));        // a far point is outside
        // The interior points must NOT have become hull vertices (cube → 8 corners).
        CHECK((int)hull.vertices.size() == 8);
    }

    // ---- Convex hull of a sphere's vertices ≈ the sphere's bounding shell ----
    {
        Mesh s = Mesh::Sphere(1.0f, 10, 14);
        Mesh hull = Mesh::ConvexHull(s.vertices);
        CHECK(hull.TriangleCount() > 0);
        CHECK(hull.PointInside({0.0f, 0.0f, 0.0f}));
        Vec3 lo, hi; hull.Bounds(lo, hi);
        CHECK(std::fabs((hi.y - lo.y) - 2.0f) < 0.2f);       // ~diameter 2
    }

    // ---- Degenerate hulls (coplanar / too few points) return empty ----
    {
        std::vector<Vec3> flat = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};  // all z=0
        CHECK(Mesh::ConvexHull(flat).TriangleCount() == 0);
        std::vector<Vec3> three = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        CHECK(Mesh::ConvexHull(three).TriangleCount() == 0);
    }

    // ---- Bisect: lop off the top half of a cube, capped → still closed ----
    {
        Mesh m = Mesh::Cube(2.0f);                          // [-1,1]^3
        m.Bisect({0, 0, 0}, {0, -1, 0}, /*cap=*/true, /*keepPositive=*/true);  // keep y<0 half
        Vec3 lo, hi; m.Bounds(lo, hi);
        CHECK(hi.y < 0.01f);                                // top removed
        CHECK(std::fabs(lo.y + 1.0f) < 1e-3f);              // bottom intact
        // Capped, so it's a closed solid: a point in the remaining half is inside.
        CHECK(m.PointInside({0.0f, -0.5f, 0.0f}));
        CHECK(!m.PointInside({0.0f, 0.5f, 0.0f}));          // the removed half is gone
    }

    // ---- Bisect without a cap leaves the cut face open (not a closed solid) ----
    {
        Mesh open = Mesh::Cube(2.0f);
        open.Bisect({0, 0, 0}, {0, -1, 0}, /*cap=*/false, true);
        Mesh capped = Mesh::Cube(2.0f);
        capped.Bisect({0, 0, 0}, {0, -1, 0}, /*cap=*/true, true);
        CHECK(capped.TriangleCount() > open.TriangleCount());  // the cap added faces
    }

    // ---- Shrink/Fatten: inflate grows the shell, deflate shrinks it ----
    {
        Mesh m = Mesh::Sphere(1.0f, 10, 14);
        Vec3 lo0, hi0; m.Bounds(lo0, hi0);
        m.ShrinkFatten(0.5f);                            // inflate outward
        Vec3 lo1, hi1; m.Bounds(lo1, hi1);
        CHECK((hi1.x - lo1.x) > (hi0.x - lo0.x) + 0.5f); // grew ~2*0.5 across
        m.ShrinkFatten(-0.5f);                           // back roughly to start
        Vec3 lo2, hi2; m.Bounds(lo2, hi2);
        CHECK(std::fabs((hi2.x - lo2.x) - (hi0.x - lo0.x)) < 0.15f);
    }

    // ---- Wireframe modifier: edges become beams; shape stays a hollow lattice ----
    {
        Mesh m = Mesh::Cube(2.0f);                        // [-1,1], 12 tris
        m.Wireframe(0.1f);
        CHECK(m.TriangleCount() > 100);                  // one 12-tri beam per edge
        CHECK(m.TriangleCount() % 12 == 0);
        Vec3 lo, hi; m.Bounds(lo, hi);
        CHECK(hi.x > 1.0f && hi.x < 1.2f);               // beams sit just outside the corners
        CHECK(lo.x < -1.0f && lo.x > -1.2f);
    }

    // ---- Screw: closed revolve makes a ring; a helix climbs by pitch*turns ----
    {
        std::vector<Vec2> prof = {{1.0f, -0.2f}, {1.2f, -0.2f}, {1.2f, 0.2f}, {1.0f, 0.2f}};
        Mesh ring = Mesh::Screw(prof, 1.0f, 0.0f, 24);   // closed square torus
        CHECK(ring.TriangleCount() > 0);
        Vec3 lo, hi; ring.Bounds(lo, hi);
        CHECK(std::fabs(hi.x - 1.2f) < 0.05f);           // outer radius 1.2
        CHECK(std::fabs((hi.y - lo.y) - 0.4f) < 0.05f);  // profile height only (flat ring)

        Mesh helix = Mesh::Screw(prof, 3.0f, 1.0f, 24);  // 3 turns, rise 1 per turn
        Vec3 hlo, hhi; helix.Bounds(hlo, hhi);
        CHECK((hhi.y - hlo.y) > 3.0f);                   // climbed ~3 units over 3 turns
    }

    // ---- Displace: deterministic noise roughens the surface (repeatable by seed) ----
    {
        Mesh a = Mesh::Sphere(1.0f, 12, 16); a.Subdivide();
        Mesh b = a;                                       // identical copy
        a.Displace(0.3f, 2.0f, /*seed=*/7);
        b.Displace(0.3f, 2.0f, /*seed=*/7);
        // Same seed → identical result; and the surface actually moved.
        bool identical = a.vertices.size() == b.vertices.size();
        float maxMove = 0.0f;
        Mesh ref = Mesh::Sphere(1.0f, 12, 16); ref.Subdivide();
        for (std::size_t i = 0; i < a.vertices.size() && identical; ++i) {
            if ((a.vertices[i] - b.vertices[i]).Magnitude() > 1e-6f) identical = false;
            maxMove = std::max(maxMove, (a.vertices[i] - ref.vertices[i]).Magnitude());
        }
        CHECK(identical);                                 // reproducible
        CHECK(maxMove > 0.05f);                           // it displaced the shell
        // A different seed gives a different pattern.
        Mesh c = ref; c.Displace(0.3f, 2.0f, /*seed=*/99);
        bool differs = false;
        for (std::size_t i = 0; i < a.vertices.size(); ++i)
            if ((a.vertices[i] - c.vertices[i]).Magnitude() > 1e-4f) { differs = true; break; }
        CHECK(differs);
    }

    // ---- Cast to cylinder: a cube's cross-section rounds toward a fixed radius ----
    {
        Mesh m = Mesh::Cube(2.0f);                        // corners at radius sqrt(2) in XZ
        m.Subdivide();                                    // midpoints too
        m.CastToCylinder(1.0f, /*axis=*/1, 1.0f);         // full cast around Y
        for (const Vec3& v : m.vertices) {
            float r = std::sqrt(v.x * v.x + v.z * v.z);
            if (r > 1e-4f) CHECK(std::fabs(r - 1.0f) < 1e-3f);  // every point sits on radius 1
        }
        Vec3 lo, hi; m.Bounds(lo, hi);
        CHECK(std::fabs(hi.y - 1.0f) < 1e-3f);            // height (Y) untouched
    }

    // ---- VisibleEdges: flat triangulation diagonals are hidden (tri-to-quad) ----
    {
        // A cube shows exactly its 12 box edges — the 6 face diagonals are hidden.
        Mesh cube = Mesh::Cube(1.0f);
        CHECK((int)cube.VisibleEdges().size() == 12);

        // A flat quad is one face: its single diagonal is hidden → 4 border edges.
        Mesh quad = Mesh::Quad(1.0f);
        CHECK((int)quad.VisibleEdges().size() == 4);

        // Subdivided once → a clean 2x2 quad grid (12 edges), no diagonals shown.
        Mesh grid = Mesh::Quad(1.0f); grid.Subdivide();
        CHECK((int)grid.VisibleEdges().size() == 12);

        // A subdivided cube reads as a grid of quads, far fewer edges than raw
        // triangle edges (which would include every diagonal).
        Mesh sc = Mesh::Cube(1.0f); sc.Subdivide();
        int rawEdges = sc.TriangleCount() * 3;              // with duplicates/diagonals
        CHECK((int)sc.VisibleEdges().size() < rawEdges);
        // 24 shared border segments (12 cube edges × 2) + 24 internal grid edges
        // (4 per face × 6); every cell diagonal hidden.
        CHECK((int)sc.VisibleEdges().size() == 48);

        // A sphere is curved: every edge is a crease, so none are hidden.
        Mesh sph = Mesh::Sphere(0.5f, 6, 8);
        CHECK((int)sph.VisibleEdges().size() > 20);         // full wire, nothing collapsed
    }

    // ---- Stretch: scaling along Y about the centre keeps X/Z, grows Y ----
    {
        Mesh m = Mesh::Cube(2.0f);                        // [-1,1]^3
        m.Stretch(1, 2.0f);
        Vec3 lo, hi; m.Bounds(lo, hi);
        CHECK(std::fabs((hi.y - lo.y) - 4.0f) < 1e-3f);  // Y doubled
        CHECK(std::fabs((hi.x - lo.x) - 2.0f) < 1e-3f);  // X unchanged
    }

    TEST_MAIN_RESULT();
}
