// Sculpt and paint brushes (feature map 3.8): Mesh::SculptBrush (Grab, Inflate, Smooth,
// Flatten, Pinch), its falloff and X symmetry, and the face paint tools (PaintFaces,
// FillFacesColor, EnsureFaceColors, MottleFaceColors). Expected numbers are worked out
// by hand from the falloff w = s(1 - d / r), s(x) = x * x * (3 - 2x).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Modeling/PolyMesh.hpp"
#include <cmath>
#include <map>
#include <tuple>
#include <vector>

using namespace okay;

static int FindVert(const Mesh& m, Vec3 p) {
    for (int i = 0; i < (int)m.vertices.size(); ++i) if ((m.vertices[i] - p).SqrMagnitude() < 1e-8f) return i;
    return -1;
}

// A flat 4 x 4 grid of 8 x 8 cells (spacing 0.5) on y = 0, centred on the origin.
static Mesh Flat() { return Mesh::Grid(4.0f, 4.0f, 8, 8); }

// The same grid, but its triangles are mirror images across x = 0 (the left half's
// diagonals lean one way, the right half's the other), so normals and neighbours are
// mirror-symmetric and a symmetric stroke must give an exactly symmetric result.
static Mesh SymGrid() {
    Mesh m = Flat();
    m.triangles.clear();
    const int n = 8, stride = n + 1;
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            const int a = z * stride + x, b = a + 1, c = a + stride, d = c + 1;
            if (x < n / 2) m.triangles.insert(m.triangles.end(), {a, c, b, b, c, d});
            else           m.triangles.insert(m.triangles.end(), {a, c, d, a, d, b});
        }
    return m;
}

// Every vertex at (x, y, z) has a partner at (-x, y, z) after the stroke.
static float MirrorError(const Mesh& before, const Mesh& after) {
    float worst = 0;
    for (int i = 0; i < (int)before.vertices.size(); ++i) {
        const Vec3 p = before.vertices[i];
        const int j = FindVert(before, Vec3{-p.x, p.y, p.z});
        if (j < 0) return 1e9f;
        const Vec3 a = after.vertices[i], b = after.vertices[j];
        worst = std::fmax(worst, (a - Vec3{-b.x, b.y, b.z}).Magnitude());
    }
    return worst;
}

int main() {
    RUN_SUITE("sculpt_paint");

    // ---- Falloff (GRAB, mode 0): smoothstep of 1 - d / r ----
    {
        Mesh m = Flat();
        const Mesh before = m;
        m.SculptBrush({0, 0, 0}, {0, 1, 0}, 1.0f, 0.2f, 0);
        CHECK_NEAR(m.vertices[FindVert(before, {0, 0, 0})].y, 0.2f, 1e-5);          // d = 0: w = 1
        CHECK_NEAR(m.vertices[FindVert(before, {0.5f, 0, 0})].y, 0.1f, 1e-5);       // d = 0.5: s(0.5) = 0.5
        CHECK_NEAR(m.vertices[FindVert(before, {0.5f, 0, 0.5f})].y, 0.0414214f, 1e-5);   // d = 0.70711: s(0.29289) = 0.20711
        CHECK_NEAR(m.vertices[FindVert(before, {1.0f, 0, 0})].y, 0.0f, 1e-6);       // d = r: outside
        CHECK_NEAR(m.vertices[FindVert(before, {-2.0f, 0, -2.0f})].y, 0.0f, 1e-6);
        // Only y changes: the brush moves along dir.
        bool onlyY = true;
        for (std::size_t i = 0; i < m.vertices.size(); ++i)
            if (std::fabs(m.vertices[i].x - before.vertices[i].x) > 1e-6f || std::fabs(m.vertices[i].z - before.vertices[i].z) > 1e-6f) onlyY = false;
        CHECK(onlyY);
        CHECK(m.name.empty());
        // A NaN radius or strength changes nothing.
        Mesh n = Flat();
        n.SculptBrush({0, 0, 0}, {0, 1, 0}, NAN, 0.2f, 0);
        n.SculptBrush({0, 0, 0}, {0, 1, 0}, 1.0f, NAN, 1);
        bool same = true;
        for (std::size_t i = 0; i < n.vertices.size(); ++i) if ((n.vertices[i] - before.vertices[i]).Magnitude() > 0) same = false;
        CHECK(same);
    }

    // ---- INFLATE (mode 1): out along the normal; copies of a vertex stay together ----
    {
        Mesh s = Mesh::Sphere(1.0f, 12, 16);                  // has seam and pole copies
        const Mesh before = s;
        s.SculptBrush({0, 0, 0}, {0, 1, 0}, 10.0f, 0.1f, 1);
        // Every vertex is 1 from the centre: d = 1, w = s(0.9) = 0.972, so it moves out 0.0972.
        float worstR = 0;
        for (const Vec3& p : s.vertices) worstR = std::fmax(worstR, std::fabs(p.Magnitude() - 1.0972f));
        CHECK(worstR < 0.01f);
        // Vertices that started at the same place (the seam column, the poles) end at the same place.
        float worstSplit = 0;
        for (std::size_t i = 0; i < before.vertices.size(); ++i)
            for (std::size_t j = i + 1; j < before.vertices.size(); ++j)
                if ((before.vertices[i] - before.vertices[j]).SqrMagnitude() < 1e-10f)
                    worstSplit = std::fmax(worstSplit, (s.vertices[i] - s.vertices[j]).Magnitude());
        CHECK(worstSplit < 1e-5f);
        if (worstSplit >= 1e-5f) std::cerr << "  inflate tore copies apart by " << worstSplit << "\n";
    }

    // ---- SMOOTH (mode 2): a flat surface is already smooth; a spike comes down ----
    {
        Mesh m = Flat();
        const Mesh before = m;
        m.SculptBrush({0.25f, 0, 0.25f}, {0, 1, 0}, 1.2f, 1.0f, 2);
        float moved = 0;
        for (std::size_t i = 0; i < m.vertices.size(); ++i) moved = std::fmax(moved, (m.vertices[i] - before.vertices[i]).Magnitude());
        CHECK(moved < 1e-5f);
        if (moved >= 1e-5f) std::cerr << "  smooth moved a flat surface by " << moved << "\n";

        Mesh sp = Flat();
        const int c = FindVert(sp, {0, 0, 0});
        sp.vertices[c].y = 1.0f;
        sp.SculptBrush({0, 0, 0}, {0, 1, 0}, 0.6f, 0.5f, 2);
        // The centre's neighbours all sit at y = 0: y goes 1 -> 1 + (0 - 1) * 0.5 * s(1) = 0.5.
        CHECK_NEAR(sp.vertices[c].y, 0.5f, 1e-5);
        // Neighbours rise a little (one of their six neighbours was the spike), never past it.
        const int e = FindVert(sp, {0.5f, 0, 0});
        CHECK(sp.vertices[e].y > 0.0f && sp.vertices[e].y < 0.05f);
        // Strength above 1 never throws a vertex past the average (no overshoot).
        Mesh big = Flat();
        big.vertices[c].y = 1.0f;
        big.SculptBrush({0, 0, 0}, {0, 1, 0}, 0.6f, 5.0f, 2);
        CHECK(big.vertices[c].y >= -1e-5f && big.vertices[c].y <= 1.0f);
    }

    // ---- FLATTEN (mode 3): press onto the region's own plane ----
    {
        Mesh m = Flat();
        const int c = FindVert(m, {0, 0, 0});
        m.vertices[c].y = 0.3f;
        m.SculptBrush({0, 0, 0}, {0, 1, 0}, 0.6f, 1.0f, 3);
        // Five vertices lie inside r = 0.6 (the centre and its four axis neighbours), so the
        // plane is y = 0.3 / 5 = 0.06 and the centre (k = 1) lands on it.
        CHECK_NEAR(m.vertices[c].y, 0.06f, 2e-3);
        // An axis neighbour: d = 0.5, k = s(1/6) = 0.074074: y = 0.06 * 0.074074 = 0.00444.
        CHECK_NEAR(m.vertices[FindVert(m, {0.5f, 0, 0})].y, 0.00444f, 1e-3);
        CHECK_NEAR(m.vertices[FindVert(m, {1.0f, 0, 0})].y, 0.0f, 1e-6);
    }

    // ---- PINCH (mode 4): toward the brush centre ----
    {
        Mesh m = Flat();
        const int v = FindVert(m, {0.5f, 0, 0});
        m.SculptBrush({0, 0, 0}, {0, 1, 0}, 1.0f, 1.0f, 4);
        CHECK_NEAR(m.vertices[v].x, 0.25f, 1e-5);             // k = s(0.5) = 0.5 of the way in
        CHECK_NEAR(m.vertices[v].y, 0.0f, 1e-6);
        CHECK_NEAR(m.vertices[FindVert(Flat(), {0, 0, 0})].x, 0.0f, 1e-6);   // the centre stays
    }

    // ---- X symmetry: one stroke, mirrored, gives a mirror-image result for every brush ----
    {
        const char* names[] = {"grab", "inflate", "smooth", "flatten", "pinch"};
        for (int mode = 0; mode < 5; ++mode) {
            Mesh m = SymGrid();
            // Some relief so smooth, flatten and inflate have something to do (mirror-symmetric).
            for (Vec3& p : m.vertices) p.y = 0.2f * std::cos(p.x * 1.3f) * std::sin(p.z * 0.9f + 0.4f);
            const Mesh before = m;
            // The brush reaches across x = 0, so the two halves of the stroke overlap.
            m.SculptBrush({0.25f, 0.1f, 0}, Vec3{0.4f, 1.0f, 0.2f}, 1.0f, 0.3f, mode, true);
            const float err = MirrorError(before, m);
            CHECK(err < 1e-5f);
            if (err >= 1e-5f) std::cerr << "  symmetry " << names[mode] << ": mirror error " << err << "\n";
            // Vertices on the mirror plane stay on it.
            bool onPlane = true;
            for (std::size_t i = 0; i < m.vertices.size(); ++i)
                if (before.vertices[i].x == 0.0f && std::fabs(m.vertices[i].x) > 1e-6f) onPlane = false;
            CHECK(onPlane);
        }
        // Without symmetry the far side is left alone.
        Mesh m = Flat();
        m.SculptBrush({1.0f, 0, 0}, {0, 1, 0}, 0.6f, 0.3f, 0, false);
        CHECK_NEAR(m.vertices[FindVert(Flat(), {-1.0f, 0, 0})].y, 0.0f, 1e-6);
        CHECK_NEAR(m.vertices[FindVert(Flat(), {1.0f, 0, 0})].y, 0.3f, 1e-6);
    }

    // ---- Face paint: whole polygons, falloff, NaN safety ----
    {
        // One 2 x 2 quad (two triangles) from the polygon mesh, so the mesh knows they are one face.
        modeling::PolyMesh q;
        q.verts = {{-1, 0, -1}, {-1, 0, 1}, {1, 0, 1}, {1, 0, -1}};
        modeling::PolyFace f; f.verts = {0, 1, 2, 3}; q.faces.push_back(f);
        Mesh m = q.ToMesh();
        CHECK(m.TriangleCount() == 2 && m.polyFace.size() == 2);
        // The brush sits over one triangle's centre; the quad's centre (0,0,0) is d = 0.70711 away.
        m.PaintFaces({0.5f, 0, -0.5f}, 0.8f, Color{1, 0, 0, 1}, 1.0f);
        CHECK(m.HasFaceColors());
        // Both triangles of the quad take the same colour: w = s(1 - 0.70711 / 0.8) = 0.037320.
        CHECK_NEAR(m.triColors[0].g, m.triColors[1].g, 1e-6);
        CHECK_NEAR(m.triColors[0].g, 1.0f - 0.037320f, 1e-4);
        CHECK_NEAR(m.triColors[0].r, 1.0f, 1e-6);
        // Blend scales the weight and the result never passes the brush colour.
        Mesh full = q.ToMesh();
        full.PaintFaces({0, 0, 0}, 1.0f, Color{0, 0, 1, 1}, 3.0f);
        CHECK_NEAR(full.triColors[0].r, 0.0f, 1e-6);
        CHECK_NEAR(full.triColors[1].b, 1.0f, 1e-6);
        // A NaN radius or colour paints nothing (no NaN colours).
        Mesh n = q.ToMesh();
        n.PaintFaces({0, 0, 0}, NAN, Color{1, 0, 0, 1}, 1.0f);
        n.PaintFaces({0, 0, 0}, 1.0f, Color{NAN, 0, 0, 1}, 1.0f);
        n.PaintFaces({0, 0, 0}, 1.0f, Color{1, 0, 0, 1}, NAN);
        bool finite = true;
        for (const Color& c : n.triColors) if (!std::isfinite(c.r + c.g + c.b + c.a)) finite = false;
        CHECK(finite);
        // A plain triangle mesh (no polygon ids) still paints per triangle.
        Mesh t = Mesh::Plane(2.0f);
        t.PaintFaces({0.5f, 0, -0.5f}, 0.5f, Color{1, 0, 0, 1}, 1.0f);
        CHECK(t.HasFaceColors() && std::fabs(t.triColors[0].g - t.triColors[1].g) > 0.1f);
    }

    // ---- Mirrored face paint is mirror-symmetric (the editor paints c and then (-x, y, z)) ----
    {
        Mesh m = SymGrid();
        const Color red{1, 0, 0, 1};
        m.PaintFaces({0.3f, 0, 0.2f}, 0.9f, red, 0.7f);
        m.PaintFaces({-0.3f, 0, 0.2f}, 0.9f, red, 0.7f);
        float worst = 0;
        for (int fi = 0; fi < m.TriangleCount(); ++fi) {
            const Vec3 c = m.FaceCenter(fi);
            for (int fj = 0; fj < m.TriangleCount(); ++fj) {
                const Vec3 d = m.FaceCenter(fj);
                if ((Vec3{-c.x, c.y, c.z} - d).SqrMagnitude() < 1e-8f) worst = std::fmax(worst, std::fabs(m.triColors[fi].g - m.triColors[fj].g));
            }
        }
        CHECK(worst < 1e-5f);
    }

    // ---- Fill, Ensure and Mottle ----
    {
        Mesh m = Mesh::Cube();
        m.EnsureFaceColors({0.5f, 0.5f, 0.5f, 1});
        CHECK((int)m.triColors.size() == m.TriangleCount() && m.triColors[3].r == 0.5f);
        m.EnsureFaceColors({1, 1, 1, 1});                     // already there: kept
        CHECK(m.triColors[3].r == 0.5f);
        m.FillFacesColor({0, 1, 99, -1}, {0, 1, 0, 1});       // bad indices are skipped
        CHECK(m.triColors[0].g == 1.0f && m.triColors[1].r == 0.0f && m.triColors[2].r == 0.5f);
        Mesh a = m, b = m;
        a.MottleFaceColors(0.1f, 7);
        b.MottleFaceColors(0.1f, 7);
        bool sameSeed = true, inRange = true;
        for (int i = 0; i < m.TriangleCount(); ++i) {
            if (a.triColors[i].r != b.triColors[i].r) sameSeed = false;
            if (i >= 2 && (a.triColors[i].r < 0.45f - 1e-5f || a.triColors[i].r > 0.55f + 1e-5f)) inRange = false;
        }
        CHECK(sameSeed && inRange);
    }

    TEST_MAIN_RESULT();
}
