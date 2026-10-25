#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Modeling/PolyMesh.hpp"
#include "okay/Modeling/PolyOps.hpp"
#include "okay/Modeling/UVUnwrap.hpp"
#include <cmath>
#include <string>

#ifndef CHECK_EQ
#define CHECK_EQ(a, b) CHECK((a) == (b))
#endif

using namespace okay;
using namespace okay::modeling;

// Modeling M5: per-corner UVs that survive welding / edit mode, UV seams,
// LSCM unwrapping, closed-part cutting, angle seams and island packing.
namespace {

// An n x n quad grid on XZ; `bend` curls it into a half pipe (still developable).
PolyMesh Grid(int n, bool bend) {
    PolyMesh pm;
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            const float u = (float)i / n, v = (float)j / n;
            if (bend) {
                const float a = u * 3.14159265f;
                pm.verts.push_back({std::cos(a), std::sin(a), v * 2.0f});
            } else {
                pm.verts.push_back({u * 2.0f, 0.0f, v * 2.0f});
            }
        }
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            PolyFace f;
            const int a = j * (n + 1) + i;
            f.verts = {a, a + n + 1, a + n + 2, a + 1};
            pm.faces.push_back(f);
        }
    return pm;
}

// An open tube (no caps) around Y, `seg` quads round and `rings` high.
PolyMesh Tube(int seg, int rings) {
    PolyMesh pm;
    for (int j = 0; j <= rings; ++j)
        for (int i = 0; i < seg; ++i) {
            const float a = (float)i / seg * 6.2831853f;
            pm.verts.push_back({std::cos(a), (float)j / rings * 2.0f, std::sin(a)});
        }
    for (int j = 0; j < rings; ++j)
        for (int i = 0; i < seg; ++i) {
            PolyFace f;
            const int a = j * seg + i, b = j * seg + (i + 1) % seg;
            f.verts = {a, a + seg, b + seg, b};
            pm.faces.push_back(f);
        }
    return pm;
}

// Every face has UVs, they're finite, inside 0..1, and no triangle is flipped or flat.
// (v = 0 is the image's top, so a face seen from outside winds clockwise in (u, v).)
bool UVsSound(const PolyMesh& pm, int* flipped = nullptr) {
    int bad = 0;
    for (int f = 0; f < (int)pm.faces.size(); ++f) {
        const PolyFace& F = pm.faces[f];
        if (F.uvs.size() != F.verts.size()) return false;
        for (const Vec2& t : F.uvs)
            if (!std::isfinite(t.x) || !std::isfinite(t.y) || t.x < -1e-4f || t.y < -1e-4f || t.x > 1.0001f || t.y > 1.0001f)
                return false;
        const std::vector<int> tri = pm.TriangulateFace(f);
        for (std::size_t k = 0; k + 2 < tri.size(); k += 3) {
            const Vec2 a = F.uvs[tri[k]], b = F.uvs[tri[k + 1]], c = F.uvs[tri[k + 2]];
            const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (area >= 0.0f) ++bad;
        }
    }
    if (flipped) *flipped = bad;
    return bad == 0;
}

// The UV bounding boxes of separate islands don't overlap.
bool IslandsApart(const PolyMesh& pm) {
    const auto islands = UVIslands(pm);
    std::vector<std::array<float, 4>> box;
    for (const auto& is : islands) {
        std::array<float, 4> b{1e9f, 1e9f, -1e9f, -1e9f};
        for (int f : is) for (const Vec2& t : pm.faces[f].uvs) {
            b[0] = std::min(b[0], t.x); b[1] = std::min(b[1], t.y);
            b[2] = std::max(b[2], t.x); b[3] = std::max(b[3], t.y);
        }
        box.push_back(b);
    }
    for (std::size_t i = 0; i < box.size(); ++i)
        for (std::size_t j = i + 1; j < box.size(); ++j)
            if (box[i][0] < box[j][2] - 1e-5f && box[j][0] < box[i][2] - 1e-5f &&
                box[i][1] < box[j][3] - 1e-5f && box[j][1] < box[i][3] - 1e-5f)
                return false;
    return true;
}

} // namespace

int main() {
    RUN_SUITE("uv");

    // --- Corner UVs survive a weld, and split back exactly ----------------------
    {
        // A cube whose six sides each have their own UVs (24 vertices, split at every edge).
        PolyMesh sides = PolyMesh::FromMesh(Mesh::Cube(1.0f));
        for (std::size_t f = 0; f < sides.faces.size(); ++f) {
            const float o = (float)f * 2.0f;
            sides.faces[f].uvs = {{o, 0}, {o + 1, 0}, {o + 1, 1}, {o, 1}};
        }
        Mesh m = sides.ToMesh();
        CHECK_EQ((int)m.vertices.size(), 24);
        const Mesh orig = m;
        CHECK(m.HasUVs());
        m.CaptureCornerUVs();
        CHECK(m.HasCornerUVs());
        m.WeldVertices();
        CHECK_EQ((int)m.vertices.size(), 8);                 // shared corners, like edit mode
        CHECK(m.HasCornerUVs());
        CHECK_EQ((int)m.cornerUVs.size(), orig.TriangleCount() * 3);
        // Every corner kept its own UV, even where 3 faces with different UVs meet.
        int same = 0;
        for (std::size_t i = 0; i < orig.triangles.size(); ++i) {
            const Vec2 a = orig.uvs[orig.triangles[i]], b = m.cornerUVs[i];
            if (std::fabs(a.x - b.x) < 1e-6f && std::fabs(a.y - b.y) < 1e-6f) ++same;
        }
        CHECK_EQ(same, (int)orig.triangles.size());
        const Mesh s = m.SplitCornerUVs();
        CHECK(!s.HasCornerUVs());
        CHECK(s.HasUVs());
        CHECK(s.vertices.size() > 8);                        // split at the seams
        for (int v = 0; v < 8; ++v) CHECK((s.vertices[v] - m.vertices[v]).SqrMagnitude() < 1e-12f);   // numbering kept
        bool exact = true;
        for (std::size_t i = 0; i < s.triangles.size(); ++i) {
            const Vec2 a = s.uvs[s.triangles[i]], b = m.cornerUVs[i];
            if (std::fabs(a.x - b.x) > 1e-6f || std::fabs(a.y - b.y) > 1e-6f) exact = false;
            if ((s.vertices[s.triangles[i]] - m.vertices[m.triangles[i]]).SqrMagnitude() > 1e-12f) exact = false;
        }
        CHECK(exact);
        // Welding the split mesh again folds the copies back onto vertices 0..7.
        Mesh w = s;
        w.CaptureCornerUVs();
        w.WeldVertices();
        CHECK_EQ((int)w.vertices.size(), 8);
        CHECK(w.triangles == m.triangles);
    }

    // --- PolyMesh reads corner UVs, and writes them back without splitting ------
    {
        PolyMesh sides = PolyMesh::FromMesh(Mesh::Cube(1.0f));
        for (PolyFace& f : sides.faces) f.uvs = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        Mesh m = sides.ToMesh();
        m.CaptureCornerUVs();
        m.WeldVertices();
        m.seams = {{0, 1}, {1, 2}};
        PolyMesh pm = PolyMesh::FromMesh(m, {false, true, 1.0f});
        CHECK_EQ((int)pm.verts.size(), 8);
        CHECK_EQ((int)pm.faces.size(), 6);
        for (const PolyFace& f : pm.faces) CHECK_EQ(f.uvs.size(), f.verts.size());
        CHECK(pm.IsSeam(0, 1));
        CHECK(pm.IsSeam(2, 1));
        CHECK(!pm.IsSeam(0, 2));
        const Mesh back = pm.ToMesh(nullptr, false);
        CHECK_EQ((int)back.vertices.size(), 8);
        CHECK(back.HasCornerUVs());
        CHECK_EQ((int)back.seams.size(), 2);
        // The same UV at the same 3D corner of the same face.
        PolyMesh again = PolyMesh::FromMesh(back, {false, true, 1.0f});
        // (a polygon may come back starting at another corner: match corners by vertex)
        bool same = again.faces.size() == pm.faces.size();
        for (std::size_t f = 0; same && f < pm.faces.size(); ++f)
            for (std::size_t k = 0; k < pm.faces[f].verts.size(); ++k) {
                int c = -1;
                for (std::size_t j = 0; j < again.faces[f].verts.size(); ++j)
                    if (again.faces[f].verts[j] == pm.faces[f].verts[k]) c = (int)j;
                if (c < 0) same = false;
                else if (std::fabs(again.faces[f].uvs[c].x - pm.faces[f].uvs[k].x) > 1e-6f ||
                         std::fabs(again.faces[f].uvs[c].y - pm.faces[f].uvs[k].y) > 1e-6f) same = false;
            }
        CHECK(same);
        // Splitting (the default) gives exact per-vertex UVs instead.
        const Mesh split = pm.ToMesh();
        CHECK(!split.HasCornerUVs());
        CHECK(split.vertices.size() > 8);
        // A seam that no longer is an edge is dropped; a deleted vertex's seam too.
        PolyMesh cut = pm;
        cut.SetSeam(0, 6, true);                        // a diagonal: not an edge of any face
        CHECK_EQ((int)cut.ToMesh().seams.size(), 2);
        cut.faces.erase(cut.faces.begin());             // drop a face, compact
        cut.SetSeam(0, 6, false);
        const std::vector<int> remap = CompactVerts(cut);
        for (const auto& e : cut.seams) CHECK(e.first >= 0 && e.second < (int)cut.verts.size());
        (void)remap;
    }

    // --- Projections fill exact corner UVs, oriented like the unwrapper's -------
    {
        Mesh m = Mesh::Cube(1.0f);                       // 8 shared vertices
        m.ProjectUVBox(1.0f);
        CHECK(m.HasCornerUVs());
        int wrong = 0;
        for (std::size_t t = 0; t + 2 < m.triangles.size(); t += 3) {
            const Vec2 a = m.cornerUVs[t], b = m.cornerUVs[t + 1], c = m.cornerUVs[t + 2];
            const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (!(area < -1e-6f)) ++wrong;               // every side reads the right way round
        }
        CHECK_EQ(wrong, 0);
        // The top of each side gets v = 0 (the image's top).
        for (std::size_t i = 0; i < m.triangles.size(); ++i) {
            const Vec3& p = m.vertices[m.triangles[i]];
            const Vec3 n = Vec3::Cross(m.vertices[m.triangles[i / 3 * 3 + 1]] - m.vertices[m.triangles[i / 3 * 3]],
                                       m.vertices[m.triangles[i / 3 * 3 + 2]] - m.vertices[m.triangles[i / 3 * 3]]);
            if (std::fabs(n.y) < 0.5f && p.y > 0.0f) CHECK(m.cornerUVs[i].y < 1e-5f);
        }
        // Split for drawing: corners where the sides' UVs differ get their own vertex.
        CHECK(m.SplitCornerUVs().vertices.size() > 8);

        Mesh cyl = Mesh::Cylinder(0.5f, 1.0f, 16);
        // (The built-in cylinder winds its faces inward; turn it outward so
        // "seen from outside" matches the winding this check reads.)
        cyl.FlipWinding();
        cyl.WeldVertices();
        cyl.ProjectUVCylinder(1.0f);
        int sideWrong = 0, wrapped = 0;
        for (std::size_t t = 0; t + 2 < cyl.triangles.size(); t += 3) {
            const Vec3 n = Vec3::Cross(cyl.vertices[cyl.triangles[t + 1]] - cyl.vertices[cyl.triangles[t]],
                                       cyl.vertices[cyl.triangles[t + 2]] - cyl.vertices[cyl.triangles[t]]);
            if (std::fabs(n.y) > 0.1f * n.Magnitude()) continue;   // caps
            const Vec2 a = cyl.cornerUVs[t], b = cyl.cornerUVs[t + 1], c = cyl.cornerUVs[t + 2];
            const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (!(area < 0.0f)) ++sideWrong;
            const float span = std::max(a.x, std::max(b.x, c.x)) - std::min(a.x, std::min(b.x, c.x));
            if (span > 0.5f) ++wrapped;                  // a face smeared across the whole texture
        }
        CHECK_EQ(sideWrong, 0);
        CHECK_EQ(wrapped, 0);
    }

    // --- Splitting a seam edge keeps both halves as seams ------------------------
    {
        PolyMesh pm = Grid(3, false);
        pm.SetSeam(1, 2, true);                         // a border edge along the first row
        const int m = SplitEdge(pm, 1, 2, 0.5f);
        CHECK(m >= 0);
        CHECK(!pm.IsSeam(1, 2));
        CHECK(pm.IsSeam(1, m) && pm.IsSeam(m, 2));
        PolyMesh tube = Tube(8, 2);
        tube.SetSeam(0, 8, true);                       // a vertical edge of the first column
        const EdgeList ring = LoopCut(tube, 0, 8, 1, 0.5f);
        CHECK(!ring.empty());
        CHECK(!tube.IsSeam(0, 8));
        int halves = 0;
        for (const auto& e : tube.seams) if (e.first == 0 || e.second == 0 || e.first == 8 || e.second == 8) ++halves;
        CHECK_EQ(halves, 2);
        // Unwrapping still opens along the (now split) seam line.
        UnwrapReport rep;
        CHECK(Unwrap(tube, {}, UnwrapOptions(), &rep));
        CHECK(UVsSound(tube));
    }

    // --- .okaymesh keeps seams --------------------------------------------------
    {
        PolyMesh pm = Grid(2, false);
        pm.SetSeam(1, 4, true);
        pm.SetSeam(4, 1, true);                         // same edge: stored once
        CHECK_EQ((int)pm.seams.size(), 1);
        PolyMesh back;
        std::string err;
        CHECK(PolyMesh::Parse(pm.Serialize(), back, &err));
        CHECK(back.IsSeam(4, 1));
        CHECK_EQ((int)back.seams.size(), 1);
        CHECK(!PolyMesh::Parse("okaymesh 1\nverts 1\nv 0 0 0\nfaces 0\nseam 0 7\n", back, &err));
        pm.SetSeam(1, 4, false);
        CHECK(pm.seams.empty());
    }

    // --- Unwrapping a flat grid: one island, no distortion -----------------------
    {
        PolyMesh pm = Grid(6, false);
        UnwrapReport rep;
        CHECK(Unwrap(pm, {}, UnwrapOptions(), &rep));
        CHECK_EQ(rep.islands, 1);
        CHECK_EQ(rep.openedClosed, 0);
        CHECK_EQ(rep.projected, 0);
        CHECK(UVsSound(pm));
        CHECK(UVAngleDistortion(pm) < 0.05f);
        // Packed to fill the square: a square grid spans nearly all of 0..1.
        float hi = 0;
        for (const PolyFace& f : pm.faces) for (const Vec2& t : f.uvs) hi = std::max(hi, std::max(t.x, t.y));
        CHECK(hi > 0.9f);
    }

    // --- A curled sheet (developable) flattens without distortion ---------------
    {
        PolyMesh pm = Grid(8, true);
        UnwrapReport rep;
        CHECK(Unwrap(pm, {}, UnwrapOptions(), &rep));
        CHECK_EQ(rep.islands, 1);
        CHECK(UVsSound(pm));
        CHECK(UVAngleDistortion(pm) < 0.5f);
        // Unrolled: the aspect follows the arc length (pi) against the height (2).
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        for (const PolyFace& f : pm.faces) for (const Vec2& t : f.uvs) {
            x0 = std::min(x0, t.x); x1 = std::max(x1, t.x); y0 = std::min(y0, t.y); y1 = std::max(y1, t.y);
        }
        const float aspect = (x1 - x0) / (y1 - y0);
        CHECK_NEAR(aspect, 3.0614675f / 2.0f, 0.06f);   // chord sum of 8 segments over pi
    }

    // --- A tube with one seam line opens into a strip ----------------------------
    {
        PolyMesh pm = Tube(12, 3);
        for (int j = 0; j < 3; ++j) pm.SetSeam(j * 12, (j + 1) * 12, true);
        CHECK_EQ((int)SeamIslands(pm).size(), 1);      // one seam doesn't split the faces apart
        UnwrapReport rep;
        CHECK(Unwrap(pm, {}, UnwrapOptions(), &rep));
        CHECK_EQ(rep.islands, 1);
        CHECK_EQ(rep.openedClosed, 0);
        CHECK(UVsSound(pm));
        CHECK(UVAngleDistortion(pm) < 0.5f);
        // The seam's vertices got two different UVs (one per side).
        int splitCorners = 0;
        for (const PolyFace& f : pm.faces) {
            for (std::size_t k = 0; k < f.verts.size(); ++k)
                if (f.verts[k] == 12) {
                    static Vec2 first{-1, -1};
                    if (first.x < 0) first = f.uvs[k];
                    else if (std::fabs(first.x - f.uvs[k].x) > 1e-3f || std::fabs(first.y - f.uvs[k].y) > 1e-3f) ++splitCorners;
                }
        }
        CHECK(splitCorners > 0);
        // Without the seam the tube is a ring (two rims): it's cut from rim to rim first.
        PolyMesh plain = Tube(12, 3);
        CHECK(Unwrap(plain, {}, UnwrapOptions(), &rep));
        CHECK_EQ(rep.openedClosed, 1);
        CHECK_EQ(rep.seamsAdded, 3);                    // straight up one column
        CHECK(UVsSound(plain));
        CHECK(UVAngleDistortion(plain) < 0.5f);
    }

    // --- A closed cube is cut open along a shortest path -------------------------
    {
        PolyMesh pm = PolyMesh::FromMesh(Mesh::Cube(1.0f));
        CHECK_EQ((int)pm.faces.size(), 6);
        UnwrapReport rep;
        CHECK(Unwrap(pm, {}, UnwrapOptions(), &rep));
        CHECK_EQ(rep.islands, 1);
        CHECK_EQ(rep.openedClosed, 1);
        CHECK(rep.seamsAdded >= 1);
        CHECK_EQ((int)pm.seams.size(), rep.seamsAdded);
        int flipped = -1;
        UVsSound(pm, &flipped);
        for (const PolyFace& f : pm.faces) for (const Vec2& t : f.uvs) CHECK(std::isfinite(t.x) && std::isfinite(t.y));
        CHECK_EQ(rep.projected, 0);
    }

    // --- Smart unwrap: seams at sharp edges, one island per cube side -------------
    {
        PolyMesh pm = PolyMesh::FromMesh(Mesh::Cube(1.0f));
        CHECK_EQ(MarkSeamsByAngle(pm, 60.0f), 12);
        CHECK_EQ(MarkSeamsByAngle(pm, 60.0f), 0);      // already marked
        CHECK_EQ((int)SeamIslands(pm).size(), 6);
        UnwrapReport rep;
        CHECK(Unwrap(pm, {}, UnwrapOptions(), &rep));
        CHECK_EQ(rep.islands, 6);
        CHECK_EQ(rep.openedClosed, 0);
        CHECK(UVsSound(pm));
        CHECK(UVAngleDistortion(pm) < 0.05f);
        CHECK_EQ((int)UVIslands(pm).size(), 6);
        CHECK(IslandsApart(pm));
        // Same texel density: every side gets the same UV area.
        float area[6];
        for (int f = 0; f < 6; ++f) {
            const auto& t = pm.faces[f].uvs;
            area[f] = 0.5f * std::fabs((t[1].x - t[0].x) * (t[2].y - t[0].y) - (t[1].y - t[0].y) * (t[2].x - t[0].x)) +
                      0.5f * std::fabs((t[2].x - t[0].x) * (t[3].y - t[0].y) - (t[2].y - t[0].y) * (t[3].x - t[0].x));
        }
        for (int f = 1; f < 6; ++f) CHECK_NEAR(area[f], area[0], area[0] * 0.01f);
    }

    // --- A sphere (closed, curved) still unwraps cleanly -------------------------
    {
        PolyMesh pm = PolyMesh::FromMesh(Mesh::Sphere(0.5f, 10, 16));
        UnwrapReport rep;
        CHECK(Unwrap(pm, {}, UnwrapOptions(), &rep));
        CHECK_EQ(rep.openedClosed, 1);
        CHECK_EQ(rep.projected, 0);
        int flipped = -1;
        bool inside = true;
        for (const PolyFace& f : pm.faces) for (const Vec2& t : f.uvs)
            if (!std::isfinite(t.x) || t.x < -1e-4f || t.x > 1.0001f || t.y < -1e-4f || t.y > 1.0001f) inside = false;
        CHECK(inside);
        UVsSound(pm, &flipped);
        CHECK(flipped == 0);
        CHECK(UVAngleDistortion(pm) < 12.0f);           // conformal: angles mostly kept
    }

    // --- A torus (a handle) is cut into a disc by two loops ---------------------
    {
        PolyMesh pm = PolyMesh::FromMesh(Mesh::FromName("Torus"));
        const int faces = (int)pm.faces.size();
        CHECK(faces > 0);
        UnwrapReport rep;
        CHECK(Unwrap(pm, {}, UnwrapOptions(), &rep));
        CHECK_EQ(rep.openedClosed, 1);
        CHECK_EQ(rep.projected, 0);
        int flipped = -1;
        UVsSound(pm, &flipped);
        CHECK_EQ(flipped, 0);
        CHECK(UVAngleDistortion(pm) < 8.0f);
    }

    // --- Only the chosen faces are unwrapped -------------------------------------
    {
        PolyMesh pm = Grid(4, false);
        for (PolyFace& f : pm.faces) f.uvs.assign(f.verts.size(), Vec2{0.25f, 0.75f});
        CHECK(Unwrap(pm, {0, 1, 2}, UnwrapOptions()));
        CHECK(std::fabs(pm.faces[5].uvs[0].x - 0.25f) < 1e-6f);   // untouched
        CHECK(!(std::fabs(pm.faces[0].uvs[0].x - 0.25f) < 1e-6f && std::fabs(pm.faces[0].uvs[1].x - 0.25f) < 1e-6f));
        CHECK(!Unwrap(pm, {99}, UnwrapOptions()));      // no valid faces
    }

    // --- Pack: islands are kept in shape, moved apart -----------------------------
    {
        PolyMesh pm = PolyMesh::FromMesh(Mesh::Cube(1.0f));
        MarkSeamsByAngle(pm, 60.0f);
        UnwrapOptions o; o.pack = false;
        CHECK(Unwrap(pm, {}, o));
        const float before = UVAngleDistortion(pm);
        PackUVIslands(pm, {}, 0.05f, false);
        CHECK(UVsSound(pm));
        CHECK(IslandsApart(pm));
        CHECK_NEAR(UVAngleDistortion(pm), before, 0.01f);
        // A bigger margin leaves smaller islands.
        PolyMesh tight = pm, loose = pm;
        PackUVIslands(tight, {}, 0.0f, false);
        PackUVIslands(loose, {}, 0.1f, false);
        auto side = [](const PolyMesh& p) {
            float x0 = 1e9f, x1 = -1e9f;
            for (const Vec2& t : p.faces[0].uvs) { x0 = std::min(x0, t.x); x1 = std::max(x1, t.x); }
            return x1 - x0;
        };
        CHECK(side(loose) < side(tight));
    }

    // --- Mesh -> unwrap -> split mesh -> scene save + reload keeps UVs and seams ---
    {
        Scene s("uv");
        GameObject* go = s.CreateGameObject("Crate");
        auto* mr = go->AddComponent<MeshRenderer>();
        Mesh m = Mesh::Cube(1.0f);
        m.name = "";
        m.WeldVertices();
        PolyMesh pm = PolyMesh::FromMesh(m, {false, true, 1.0f});
        MarkSeamsByAngle(pm, 60.0f);
        CHECK(Unwrap(pm));
        mr->mesh = pm.ToMesh(nullptr, false);          // the edit-mode form: welded + corner UVs
        CHECK(mr->mesh.HasCornerUVs());
        CHECK_EQ((int)mr->mesh.seams.size(), 12);
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("meshseams 12") != std::string::npos);
        Scene r("reload");
        std::string err;
        CHECK(SceneSerializer::Deserialize(r, text, &err));
        auto* mr2 = r.Find("Crate")->GetComponent<MeshRenderer>();
        CHECK(!mr2->mesh.HasCornerUVs());               // saved split, ready to draw
        CHECK(mr2->mesh.HasUVs());
        CHECK_EQ((int)mr2->mesh.seams.size(), 12);
        // Same UV at every triangle corner as before saving.
        bool same = mr2->mesh.triangles.size() == mr->mesh.triangles.size();
        for (std::size_t i = 0; same && i < mr2->mesh.triangles.size(); ++i) {
            const Vec2 a = mr2->mesh.uvs[mr2->mesh.triangles[i]], b = mr->mesh.cornerUVs[i];
            if (std::fabs(a.x - b.x) > 1e-4f || std::fabs(a.y - b.y) > 1e-4f) same = false;
        }
        CHECK(same);
        // Back into edit mode: weld + corner UVs, seams still on the same edges.
        Mesh e = mr2->mesh;
        e.CaptureCornerUVs();
        e.WeldVertices();
        CHECK_EQ((int)e.vertices.size(), 8);
        PolyMesh again = PolyMesh::FromMesh(e, {false, true, 1.0f});
        CHECK_EQ((int)again.seams.size(), 12);
        CHECK_EQ((int)UVIslands(again).size(), 6);
    }

    TEST_MAIN_RESULT();
}
