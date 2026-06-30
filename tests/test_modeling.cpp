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

    TEST_MAIN_RESULT();
}
