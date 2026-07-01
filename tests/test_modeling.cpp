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

    TEST_MAIN_RESULT();
}
