// MeshSimplify quality for LOD generation (feature map 2.8): material slots, per-triangle colours and vertex colours
// survive simplification, and the line between two materials (or two colours) stays where it was, like a UV seam.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Optimize/MeshSimplify.hpp"
#include <cmath>

using namespace okay;

// An n x n grid of quads on the XZ plane from -1 to 1 (shared vertices), split down x = 0.
static Mesh Grid(int n) {
    Mesh m;
    for (int z = 0; z <= n; ++z)
        for (int x = 0; x <= n; ++x) {
            const float fx = -1.0f + 2.0f * x / n, fz = -1.0f + 2.0f * z / n;
            // A gentle bump so the quadrics have something to keep.
            m.vertices.push_back({fx, 0.15f * std::sin(fx * 2.0f) * std::cos(fz * 2.0f), fz});
            m.uvs.push_back({(float)x / n, (float)z / n});
        }
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            const int a = z * (n + 1) + x, b = a + 1, c = a + n + 1, d = c + 1;
            m.triangles.insert(m.triangles.end(), {a, c, b, b, c, d});
        }
    return m;
}

static float CentroidX(const Mesh& m, int t) {
    return (m.vertices[m.triangles[t * 3]].x + m.vertices[m.triangles[t * 3 + 1]].x + m.vertices[m.triangles[t * 3 + 2]].x) / 3.0f;
}

// No triangle reaches across x = 0 (every vertex on its own side, within eps).
static int CrossingTriangles(const Mesh& m, const std::vector<int>& side) {
    int bad = 0;
    for (int t = 0; t < m.TriangleCount(); ++t)
        for (int k = 0; k < 3; ++k) {
            const float x = m.vertices[m.triangles[t * 3 + k]].x;
            if (side[(std::size_t)t] == 0 && x > 1e-4f) { ++bad; break; }
            if (side[(std::size_t)t] == 1 && x < -1e-4f) { ++bad; break; }
        }
    return bad;
}

int main() {
    RUN_SUITE("material slots are kept and their border stays");
    {
        Mesh m = Grid(24);
        m.triSlot.resize((std::size_t)m.TriangleCount());
        for (int t = 0; t < m.TriangleCount(); ++t) m.triSlot[(std::size_t)t] = CentroidX(m, t) < 0.0f ? 0 : 1;
        CHECK(m.HasSlots());
        optimize::SimplifyOptions o;
        o.targetRatio = 0.25f;
        const optimize::SimplifyResult r = optimize::SimplifyMesh(m, o);
        std::printf("  %d -> %d triangles\n", r.trianglesBefore, r.trianglesAfter);
        CHECK(r.trianglesAfter < r.trianglesBefore / 2);
        CHECK(r.mesh.HasSlots());
        if (r.mesh.HasSlots()) {
            int zero = 0, one = 0;
            for (int s : r.mesh.triSlot) (s == 0 ? zero : one)++;
            CHECK(zero > 0 && one > 0);
            CHECK(CrossingTriangles(r.mesh, r.mesh.triSlot) == 0);
        }
    }

    RUN_SUITE("per-triangle colours keep their border");
    {
        Mesh m = Grid(24);
        std::vector<int> side((std::size_t)m.TriangleCount());
        for (int t = 0; t < m.TriangleCount(); ++t) {
            side[(std::size_t)t] = CentroidX(m, t) < 0.0f ? 0 : 1;
            m.triColors.push_back(side[(std::size_t)t] == 0 ? Color(1, 0.2f, 0.2f, 1) : Color(0.2f, 0.3f, 1, 1));
        }
        optimize::SimplifyOptions o;
        o.targetRatio = 0.25f;
        const optimize::SimplifyResult r = optimize::SimplifyMesh(m, o);
        CHECK(r.mesh.HasFaceColors());
        if (r.mesh.HasFaceColors()) {
            std::vector<int> outSide;
            for (const Color& c : r.mesh.triColors) outSide.push_back(c.r > 0.5f ? 0 : 1);
            CHECK(CrossingTriangles(r.mesh, outSide) == 0);
        }
    }

    RUN_SUITE("vertex colours ride along");
    {
        Mesh m = Grid(24);
        for (const Vec3& v : m.vertices) m.vertexColors.push_back(Color(0.5f + 0.5f * v.x, 0.5f - 0.5f * v.x, 0.25f, 1));
        CHECK(m.HasVertexColors());
        optimize::SimplifyOptions o;
        o.targetRatio = 0.3f;
        const optimize::SimplifyResult r = optimize::SimplifyMesh(m, o);
        CHECK(r.mesh.HasVertexColors());
        if (r.mesh.HasVertexColors()) {
            float worst = 0.0f;
            for (std::size_t i = 0; i < r.mesh.vertices.size(); ++i) {
                const float want = 0.5f + 0.5f * r.mesh.vertices[i].x;
                worst = std::max(worst, std::fabs(r.mesh.vertexColors[i].r - want));
            }
            std::printf("  largest vertex colour error %.3f\n", worst);
            CHECK(worst < 0.1f);
        }
    }

    RUN_SUITE("a mesh without any of these is unchanged in layout");
    {
        const Mesh m = Grid(12);
        optimize::SimplifyOptions o;
        o.targetRatio = 0.5f;
        const optimize::SimplifyResult r = optimize::SimplifyMesh(m, o);
        CHECK(!r.mesh.HasSlots());
        CHECK(!r.mesh.HasVertexColors());
        CHECK(r.mesh.uvs.size() == r.mesh.vertices.size());
    }
    TEST_MAIN_RESULT();
}
