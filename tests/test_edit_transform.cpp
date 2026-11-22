// Modeling milestone 1: the G / R / S modal transforms used by mesh edit mode,
// and Mirror no longer overlapping a mesh that crosses the mirror plane.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Modeling/EditTransform.hpp"
#include <algorithm>
#include <cmath>

using namespace okay;
using namespace okay::modeling;

static bool Near(const Vec3& a, const Vec3& b, float eps = 1e-4f) {
    return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
}
// The vertex of a mesh at this position (or -1).
static int VertAt(const Mesh& m, const Vec3& p) {
    for (int i = 0; i < (int)m.vertices.size(); ++i) if (Near(m.vertices[i], p)) return i;
    return -1;
}
static std::vector<int> TopVerts(const Mesh& m) {
    std::vector<int> v;
    for (int i = 0; i < (int)m.vertices.size(); ++i) if (m.vertices[i].y > 0.4f) v.push_back(i);
    return v;
}

int main() {
    RUN_SUITE("edit_transform");

    // ---- Axis lock: X locks X, Shift+X the YZ plane, the same key again frees ----
    {
        AxisLock l;
        CHECK(l.Free() && l.Allows(0) && l.Allows(1) && l.Allows(2) && l.Label().empty());
        l.Press(0, false);
        CHECK(l.Allows(0) && !l.Allows(1) && !l.Allows(2) && l.Label() == "X");
        l.Press(0, true);
        CHECK(!l.Allows(0) && l.Allows(1) && l.Allows(2) && l.Label() == "YZ plane");
        l.Press(0, true);
        CHECK(l.Free());
        l.Press(2, false); l.Press(1, false);
        CHECK(l.Label() == "Y");
    }
    // ---- Typed values: digits, one point, minus toggles, backspace ----
    {
        NumericInput n;
        CHECK(!n.Active() && n.Value(7.0f) == 7.0f);
        n.Type('1'); n.Type('.'); n.Type('5'); n.Type('.');   // a second point is ignored
        CHECK(n.Text() == "1.5" && std::fabs(n.Value(0) - 1.5f) < 1e-6f);
        n.Type('-');
        CHECK(n.Text() == "-1.5" && std::fabs(n.Value(0) + 1.5f) < 1e-6f);
        CHECK(!n.Type('x'));
        n.Backspace(); n.Backspace(); n.Backspace();
        CHECK(n.Text() == "-");
        CHECK(n.Value(3.0f) == 3.0f);   // just "-" still means "use the mouse"
        n.Backspace();
        CHECK(!n.Active());
        n.Type('.');
        CHECK(n.Text() == "0." && n.Value(9.0f) == 0.0f);
    }
    // ---- Snapping ----
    CHECK(std::fabs(SnapTo(0.37f, 0.25f) - 0.25f) < 1e-6f);
    CHECK(std::fabs(SnapTo(-0.9f, 0.5f) + 1.0f) < 1e-6f);
    CHECK(std::fabs(SnapTo(12.0f, 5.0f) - 10.0f) < 1e-6f);
    CHECK(SnapTo(0.37f, 0.0f) == 0.37f);

    // ---- Matrices ----
    {
        AxisLock y; y.Press(1, false);
        CHECK(Near(MoveMatrix({1, 2, 3}, y).MultiplyPoint({0, 0, 0}), {0, 2, 0}));
        AxisLock notZ; notZ.Press(2, true);
        CHECK(Near(MoveMatrix({1, 2, 3}, notZ).MultiplyPoint({1, 1, 1}), {2, 3, 1}));
        // 90 degrees about +Y through (1,0,0): (2,0,0) -> (1,0,-1).
        CHECK(Near(RotateMatrix({1, 0, 0}, {0, 1, 0}, 90.0f).MultiplyPoint({2, 0, 0}), {1, 0, -1}));
        // Uniform scale about a pivot, and scale along X only.
        CHECK(Near(ScaleMatrix({1, 1, 1}, AxisLock{}, 2.0f).MultiplyPoint({2, 2, 2}), {3, 3, 3}));
        AxisLock x; x.Press(0, false);
        CHECK(Near(ScaleMatrix({0, 0, 0}, x, 3.0f).MultiplyPoint({1, 1, 1}), {3, 1, 1}));
    }
    // ---- Mouse rays -> movement ----
    {
        // Camera at z = 10 looking down -Z at the origin; rays through x = 0 then x = 2.
        Ray r0{{0, 0, 10}, {0, 0, -1}}, r1{{2, 1, 10}, {0, 0, -1}};
        Vec3 free = MoveFromRays(AxisLock{}, {0, 0, 0}, {0, 0, -1}, r0, r1);
        CHECK(Near(free, {2, 1, 0}));
        AxisLock x; x.Press(0, false);
        CHECK(Near(MoveFromRays(x, {0, 0, 0}, {0, 0, -1}, r0, r1), {2, 0, 0}));
        // Locked to Z while looking straight down Z: no movement rather than a jump.
        AxisLock z; z.Press(2, false);
        CHECK(Near(MoveFromRays(z, {0, 0, 0}, {0, 0, -1}, r0, r1), {0, 0, 0}));
        // XZ plane seen from above: the ray hits the ground plane.
        AxisLock xz; xz.Press(1, true);
        Ray d0{{0, 10, 0}, {0, -1, 0}}, d1{{3, 10, -2}, {0, -1, 0}};
        CHECK(Near(MoveFromRays(xz, {0, 0, 0}, {0, -1, 0}, d0, d1), {3, 0, -2}));
    }
    // ---- Screen angle: a quarter turn counter-clockwise on screen is +90 ----
    CHECK(std::fabs(ScreenAngleDelta(0, 0, 10, 0, 0, -10) - 90.0f) < 1e-3f);
    CHECK(std::fabs(ScreenAngleDelta(0, 0, -10, 1, -10, -1) + 11.42f) < 0.1f);   // across the ±180 seam

    // ---- A session moves only the selection, from the start positions ----
    {
        Mesh m = Mesh::Cube(1.0f);
        const Mesh orig = m;
        const std::vector<int> top = TopVerts(m);
        CHECK(top.size() == 4);
        EditTransformSession s;
        s.Begin(m, EditXform::Move, top, Mat4::Identity());
        CHECK(s.Active() && s.MovedCount() == 4);
        CHECK(Near(s.PivotWorld(), {0, 0.5f, 0}));
        AxisLock y; y.Press(1, false);
        s.Apply(m, MoveMatrix({5, 1, 5}, y));
        s.Apply(m, MoveMatrix({5, 2, 5}, y));   // applied from the start, not on top of the last frame
        for (int v : top) CHECK(std::fabs(m.vertices[v].y - 2.5f) < 1e-5f);
        for (int i = 0; i < (int)m.vertices.size(); ++i)
            if (orig.vertices[i].y < 0) CHECK(Near(m.vertices[i], orig.vertices[i]));
        CHECK(m.name.empty());
        s.Cancel(m);
        CHECK(!s.Active());
        for (int i = 0; i < (int)m.vertices.size(); ++i) CHECK(Near(m.vertices[i], orig.vertices[i]));
        CHECK(m.name == orig.name);   // cancelling gives the primitive its name back
    }
    // ---- Rotate and scale about the selection's centre, in world space ----
    {
        Mesh m = Mesh::Cube(1.0f);
        const std::vector<int> top = TopVerts(m);
        EditTransformSession s;
        s.Begin(m, EditXform::Rotate, top, Mat4::Identity());
        s.Apply(m, RotateMatrix(s.PivotWorld(), {0, 1, 0}, 90.0f));
        int a = VertAt(m, {0.5f, 0.5f, -0.5f});
        CHECK(a >= 0);   // the top square turned onto itself
        CHECK(VertAt(m, {0.5f, -0.5f, 0.5f}) >= 0);   // the bottom didn't move
        s.Confirm();
        CHECK(!s.Active());

        EditTransformSession sc;
        sc.Begin(m, EditXform::Scale, TopVerts(m), Mat4::Identity());
        sc.Apply(m, ScaleMatrix(sc.PivotWorld(), AxisLock{}, 0.0f));   // collapse the top to its centre
        for (int v : TopVerts(m)) CHECK(Near(m.vertices[v], {0, 0.5f, 0}));
    }
    // ---- An object that is moved and scaled: the transform happens in world space ----
    {
        Mesh m = Mesh::Cube(1.0f);
        const Mat4 toWorld = Mat4::TRS({10, 0, 0}, Quat(), {2, 2, 2});
        EditTransformSession s;
        s.Begin(m, EditXform::Move, TopVerts(m), toWorld);
        CHECK(Near(s.PivotWorld(), {10, 1, 0}));
        s.Apply(m, MoveMatrix({0, 2, 0}, AxisLock{}));   // 2 world units up = 1 local unit
        for (int v : TopVerts(m)) CHECK(std::fabs(m.vertices[v].y - 1.5f) < 1e-4f);
    }
    // ---- Soft selection: the falloff matches Move Soft ----
    {
        Mesh a = Mesh::Grid(4.0f, 4.0f, 8, 8);
        Mesh b = a;
        int centre = VertAt(a, {0, 0, 0});
        CHECK(centre >= 0);
        a.MoveVerticesSoft({centre}, {0, 1, 0}, 1.5f);
        EditTransformSession s;
        EditTransformSession::Options o; o.softRadius = 1.5f;
        s.Begin(b, EditXform::Move, {centre}, Mat4::Identity(), o);
        CHECK(s.MovedCount() > 1);
        s.Apply(b, MoveMatrix({0, 1, 0}, AxisLock{}));
        bool same = true;
        for (int i = 0; i < (int)a.vertices.size(); ++i) if (!Near(a.vertices[i], b.vertices[i])) same = false;
        CHECK(same);
    }
    // ---- X symmetry: the partner across x = 0 moves mirrored ----
    {
        Mesh m = Mesh::Cube(1.0f);
        int v = VertAt(m, {0.5f, 0.5f, 0.5f}), partner = VertAt(m, {-0.5f, 0.5f, 0.5f});
        CHECK(v >= 0 && partner >= 0);
        EditTransformSession s;
        EditTransformSession::Options o; o.mirrorX = true;
        s.Begin(m, EditXform::Move, {v}, Mat4::Identity(), o);
        CHECK(s.MovedCount() == 2);
        s.Apply(m, MoveMatrix({0.25f, 0.5f, 0}, AxisLock{}));
        CHECK(Near(m.vertices[v], {0.75f, 1.0f, 0.5f}));
        CHECK(Near(m.vertices[partner], {-0.75f, 1.0f, 0.5f}));
    }
    // ---- Empty selection: nothing to do ----
    {
        Mesh m = Mesh::Cube(1.0f);
        EditTransformSession s;
        s.Begin(m, EditXform::Move, {}, Mat4::Identity());
        CHECK(!s.Active());
    }

    // ---- Topology report ----
    {
        Mesh c = Mesh::Cube(1.0f);
        auto t = c.Topology();
        CHECK(t.Closed() && t.edges == 18 && t.degenerateTris == 0);
        Mesh p = Mesh::Plane(1.0f);
        CHECK(p.Topology().boundaryEdges == 4);
    }
    // ---- Mirror on a centred cube: still one closed cube, no overlap ----
    {
        Mesh m = Mesh::Cube(1.0f);
        m.Mirror(0, true);
        auto t = m.Topology();
        CHECK(t.Closed());
        CHECK(t.nonManifoldEdges == 0);
        Vec3 lo, hi; m.Bounds(lo, hi);
        CHECK(Near(lo, {-0.5f, -0.5f, -0.5f}) && Near(hi, {0.5f, 0.5f, 0.5f}));
    }
    // ---- Mirror of a shifted cube that crosses the plane: the - side is cut first ----
    {
        Mesh m = Mesh::Cube(1.0f);
        for (Vec3& v : m.vertices) v.x += 0.25f;   // x from -0.25 to 0.75
        m.Mirror(0, true);
        auto t = m.Topology();
        CHECK(t.Closed());
        Vec3 lo, hi; m.Bounds(lo, hi);
        CHECK(std::fabs(lo.x + 0.75f) < 1e-4f && std::fabs(hi.x - 0.75f) < 1e-4f);
    }
    // ---- Mirror of a half model (all on +X) still appends the other half ----
    {
        Mesh m = Mesh::Cube(1.0f);
        for (Vec3& v : m.vertices) v.x += 0.5f;   // x from 0 to 1, touching the plane
        m.Mirror(0, true);
        CHECK(m.Topology().Closed());
        Vec3 lo, hi; m.Bounds(lo, hi);
        CHECK(std::fabs(lo.x + 1.0f) < 1e-4f && std::fabs(hi.x - 1.0f) < 1e-4f);
    }

    // ---- Pivot: Individual Origins and Active Vertex ----
    {
        // Two separate unit quads on y = 0, centred at x = -3 and x = +3.
        auto twoQuads = [] {
            Mesh m;
            const float cs[2] = {-3.0f, 3.0f};
            for (float cx : cs) {
                const int b = (int)m.vertices.size();
                m.vertices.push_back({cx - 0.5f, 0, -0.5f}); m.vertices.push_back({cx - 0.5f, 0, 0.5f});
                m.vertices.push_back({cx + 0.5f, 0, 0.5f}); m.vertices.push_back({cx + 0.5f, 0, -0.5f});
                m.triangles.insert(m.triangles.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
            }
            return m;
        };
        const std::vector<int> all = {0, 1, 2, 3, 4, 5, 6, 7};
        // Median: scaling both by 2 about the common centre pushes the quads apart (centres to -6 / +6).
        {
            Mesh m = twoQuads();
            EditTransformSession s; s.Begin(m, EditXform::Scale, all, Mat4::Identity());
            CHECK(Near(s.PivotWorld(), Vec3{0, 0, 0}));
            s.Apply(m, ScaleMatrix(s.PivotWorld(), AxisLock{}, 2.0f));
            CHECK(Near(m.vertices[0], Vec3{-7, 0, -1}));
        }
        // Individual Origins: each quad grows about its own centre and stays where it was.
        {
            Mesh m = twoQuads();
            EditTransformOptions o; o.pivot = EditPivot::Individual;
            EditTransformSession s; s.Begin(m, EditXform::Scale, all, Mat4::Identity(), o);
            s.Apply(m, ScaleMatrix(s.PivotWorld(), AxisLock{}, 2.0f));
            CHECK(Near(m.vertices[0], Vec3{-4, 0, -1}));
            CHECK(Near(m.vertices[2], Vec3{-2, 0, 1}));
            CHECK(Near(m.vertices[4], Vec3{2, 0, -1}));
            CHECK(Near(m.vertices[6], Vec3{4, 0, 1}));
            // Rotate 90 degrees about Y: each quad turns in place (its corners stay on its own circle).
            Mesh r = twoQuads();
            EditTransformSession t; t.Begin(r, EditXform::Rotate, all, Mat4::Identity(), o);
            t.Apply(r, RotateMatrix(t.PivotWorld(), Vec3{0, 1, 0}, 90.0f));
            Vec3 c0{0, 0, 0}, c1{0, 0, 0};
            for (int i = 0; i < 4; ++i) { c0 += r.vertices[i]; c1 += r.vertices[4 + i]; }
            CHECK(Near(c0 * 0.25f, Vec3{-3, 0, 0}) && Near(c1 * 0.25f, Vec3{3, 0, 0}));
            CHECK(std::fabs((r.vertices[0] - Vec3{-3, 0, 0}).Magnitude() - std::sqrt(0.5f)) < 1e-4f);
            // Move is the same for every group; Cancel puts everything back exactly.
            Mesh mv = twoQuads();
            EditTransformSession u; u.Begin(mv, EditXform::Move, all, Mat4::Identity(), o);
            u.Apply(mv, MoveMatrix(Vec3{0, 1, 0}, AxisLock{}));
            CHECK(Near(mv.vertices[0], Vec3{-3.5f, 1, -0.5f}) && Near(mv.vertices[7], Vec3{3.5f, 1, -0.5f}));
            u.Cancel(mv);
            CHECK(Near(mv.vertices[0], Vec3{-3.5f, 0, -0.5f}));
        }
        // Individual Origins on an unwelded mesh: corners at the same place count as joined, so a
        // primitive cube (24 vertices, one face each) scales as one part, not six.
        {
            Mesh c = Mesh::Cube(1.0f);
            std::vector<int> every; for (int i = 0; i < (int)c.vertices.size(); ++i) every.push_back(i);
            EditTransformOptions o; o.pivot = EditPivot::Individual;
            EditTransformSession s; s.Begin(c, EditXform::Scale, every, Mat4::Identity(), o);
            s.Apply(c, ScaleMatrix(s.PivotWorld(), AxisLock{}, 2.0f));
            Vec3 lo, hi; c.Bounds(lo, hi);
            CHECK(Near(lo, Vec3{-1, -1, -1}) && Near(hi, Vec3{1, 1, 1}));
        }
        // Active Vertex: everything turns about that vertex, which stays put.
        {
            Mesh m = twoQuads();
            EditTransformOptions o; o.pivot = EditPivot::Active; o.activeVertex = 2;
            EditTransformSession s; s.Begin(m, EditXform::Scale, all, Mat4::Identity(), o);
            CHECK(Near(s.PivotWorld(), Vec3{-2.5f, 0, 0.5f}));
            s.Apply(m, ScaleMatrix(s.PivotWorld(), AxisLock{}, 3.0f));
            CHECK(Near(m.vertices[2], Vec3{-2.5f, 0, 0.5f}));
            CHECK(Near(m.vertices[0], Vec3{-2.5f - 3.0f, 0, 0.5f - 3.0f}));
            // An active vertex that is not selected, or does not exist, falls back to the median.
            Mesh n = twoQuads();
            EditTransformOptions bad; bad.pivot = EditPivot::Active; bad.activeVertex = 99;
            EditTransformSession t; t.Begin(n, EditXform::Scale, {0, 1, 2, 3}, Mat4::Identity(), bad);
            CHECK(Near(t.PivotWorld(), Vec3{-3, 0, 0}));
            bad.activeVertex = 5;                                // exists, but not in the selection
            t.Begin(n, EditXform::Scale, {0, 1, 2, 3}, Mat4::Identity(), bad);
            CHECK(Near(t.PivotWorld(), Vec3{-3, 0, 0}));
        }
        // Individual Origins with X symmetry: the mirrored partner turns about its own mirrored centre.
        {
            Mesh m = twoQuads();                                 // quad 1 is quad 0 mirrored across x = 0
            EditTransformOptions o; o.pivot = EditPivot::Individual; o.mirrorX = true;
            EditTransformSession s; s.Begin(m, EditXform::Scale, {0, 1, 2, 3}, Mat4::Identity(), o);
            CHECK(s.MovedCount() == 8);
            s.Apply(m, ScaleMatrix(s.PivotWorld(), AxisLock{}, 2.0f));
            CHECK(Near(m.vertices[0], Vec3{-4, 0, -1}));
            CHECK(Near(m.vertices[7], Vec3{4, 0, -1}));       // partner of vertex 0 (x mirrored)
        }
        // Hostile: empty selection does nothing; a world transform with Individual Origins keeps groups in local space.
        {
            Mesh m = twoQuads();
            EditTransformOptions o; o.pivot = EditPivot::Individual;
            EditTransformSession s; s.Begin(m, EditXform::Scale, {}, Mat4::Identity(), o);
            CHECK(!s.Active());
            const Mat4 toWorld = Mat4::Translate(Vec3{10, 0, 0}) * Mat4::Scale(Vec3{2, 2, 2});
            EditTransformSession w; w.Begin(m, EditXform::Scale, all, toWorld, o);
            w.Apply(m, ScaleMatrix(w.PivotWorld(), AxisLock{}, 2.0f));
            CHECK(Near(m.vertices[0], Vec3{-4, 0, -1}) && Near(m.vertices[6], Vec3{4, 0, 1}));
        }
    }

    // ---- Individual Origins in Face / Edge mode: parts are joined only through the selected faces / edges ----
    // Bug: parts were joined by any edge between two selected vertices, so two faces with one unselected face
    // between them (whose side edges have both ends selected) scaled as one part about their common centre.
    {
        Mesh strip;                                              // three unit quads in a row along x, welded
        for (int x = 0; x <= 3; ++x) { strip.vertices.push_back({(float)x, 0, 0}); strip.vertices.push_back({(float)x, 0, 1}); }
        for (int q = 0; q < 3; ++q) { const int a = q * 2; strip.triangles.insert(strip.triangles.end(), {a, a + 1, a + 3, a, a + 3, a + 2}); }
        const std::vector<int> tris = {0, 1, 4, 5};              // quads 0 and 2; quad 1 between them is not selected
        std::vector<int> verts;
        for (int t : tris) for (int k = 0; k < 3; ++k) {
            const int v = strip.triangles[(std::size_t)t * 3 + k];
            if (std::find(verts.begin(), verts.end(), v) == verts.end()) verts.push_back(v);
        }
        EditTransformOptions o; o.pivot = EditPivot::Individual; o.faces = &tris;
        EditTransformSession s; s.Begin(strip, EditXform::Scale, verts, Mat4::Identity(), o);
        s.Apply(strip, ScaleMatrix(s.PivotWorld(), AxisLock{}, 0.5f));
        CHECK(Near(strip.vertices[0], Vec3{0.25f, 0, 0.25f}));  // quad 0 about (0.5, 0, 0.5)
        CHECK(Near(strip.vertices[7], Vec3{2.75f, 0, 0.75f}));  // quad 2 about (2.5, 0, 0.5)
        // Edge mode: two separate selected edges (x = 0 and x = 3) scale about their own middles.
        Mesh e2 = strip; s.Cancel(e2);
        Mesh e3;
        for (int x = 0; x <= 3; ++x) { e3.vertices.push_back({(float)x, 0, 0}); e3.vertices.push_back({(float)x, 0, 1}); }
        for (int q = 0; q < 3; ++q) { const int a = q * 2; e3.triangles.insert(e3.triangles.end(), {a, a + 1, a + 3, a, a + 3, a + 2}); }
        const std::vector<std::pair<int, int>> edges = {{0, 1}, {6, 7}};
        EditTransformOptions oe; oe.pivot = EditPivot::Individual; oe.edges = &edges;
        EditTransformSession se; se.Begin(e3, EditXform::Scale, {0, 1, 6, 7}, Mat4::Identity(), oe);
        se.Apply(e3, ScaleMatrix(se.PivotWorld(), AxisLock{}, 0.5f));
        CHECK(Near(e3.vertices[0], Vec3{0, 0, 0.25f}) && Near(e3.vertices[7], Vec3{3, 0, 0.75f}));
        // Hostile: face and edge lists naming triangles / vertices that do not exist are ignored.
        const std::vector<int> badTris = {-1, 99};
        const std::vector<std::pair<int, int>> badEdges = {{-1, 0}, {0, 999}};
        EditTransformOptions ob; ob.pivot = EditPivot::Individual; ob.faces = &badTris; ob.edges = &badEdges;
        Mesh e4 = e3; EditTransformSession sb; sb.Begin(e4, EditXform::Scale, {0, 1}, Mat4::Identity(), ob);
        sb.Apply(e4, ScaleMatrix(sb.PivotWorld(), AxisLock{}, 2.0f));
        bool finite = true; for (const Vec3& v : e4.vertices) if (!std::isfinite(v.x + v.y + v.z)) finite = false;
        CHECK(finite);
    }

    // ---- Round 3: transform orientation (Global / Local / Normal) ----
    {
        auto ortho = [](const OrientAxes& a) {
            for (int i = 0; i < 3; ++i) {
                if (std::fabs(a[i].Magnitude() - 1.0f) > 1e-4f) return false;
                for (int j = i + 1; j < 3; ++j) if (std::fabs(Vec3::Dot(a[i], a[j])) > 1e-4f) return false;
            }
            return Near(Vec3::Cross(a[0], a[1]), a[2]);   // right-handed
        };
        Mesh cube = Mesh::Cube(1.0f);
        const OrientAxes g = OrientationAxes(cube, {0}, Mat4::Identity(), EditOrient::Global);
        CHECK(Near(g[0], Vec3{1, 0, 0}) && Near(g[1], Vec3{0, 1, 0}) && Near(g[2], Vec3{0, 0, 1}));
        CHECK(std::string(EditOrientName(EditOrient::Normal)) == "Normal");
        // Local: the object's rotation (90 degrees about Y turns X to -Z).
        const Mat4 rotY = Mat4::Rotate(Quat::AngleAxis(90.0f, Vec3{0, 1, 0}));
        const OrientAxes l = OrientationAxes(cube, {0}, rotY, EditOrient::Local);
        CHECK(ortho(l) && Near(l[0], rotY.MultiplyVector(Vec3{1, 0, 0})) && Near(l[1], Vec3{0, 1, 0}));
        // A scaled, rotated object: still unit axes at right angles.
        const OrientAxes ls = OrientationAxes(cube, {0}, Mat4::TRS(Vec3{5, 0, 0}, Quat::AngleAxis(30.0f, Vec3{0, 0, 1}), Vec3{3, 1, 0.5f}), EditOrient::Local);
        CHECK(ortho(ls));
        // Normal, Face mode: the top face's two triangles (6 and 7) give Z = up.
        const std::vector<int> top = {6, 7};
        const OrientAxes n = OrientationAxes(cube, {}, Mat4::Identity(), EditOrient::Normal, &top);
        CHECK(ortho(n) && Near(n[2], Vec3{0, 1, 0}));
        // Normal from the four top vertices (the side faces' normals cancel out).
        Mesh welded = Mesh::Cube(1.0f); welded.WeldVertices();
        const OrientAxes nv = OrientationAxes(welded, TopVerts(welded), Mat4::Identity(), EditOrient::Normal);
        CHECK(ortho(nv) && Near(nv[2], Vec3{0, 1, 0}));
        // On a rotated object the normal turns with it (90 degrees about Z: up becomes -X).
        const Mat4 rotZ = Mat4::Rotate(Quat::AngleAxis(90.0f, Vec3{0, 0, 1}));
        const OrientAxes nr = OrientationAxes(cube, {}, rotZ, EditOrient::Normal, &top);
        CHECK(Near(nr[2], Vec3{-1, 0, 0}));
        // Hostile: no selection, bad indices, a zero-area face, a broken matrix: the world axes.
        CHECK(Near(OrientationAxes(cube, {}, Mat4::Identity(), EditOrient::Normal)[2], Vec3{0, 0, 1}));
        const std::vector<int> badTris = {-3, 999};
        CHECK(Near(OrientationAxes(cube, {-1, 500}, Mat4::Identity(), EditOrient::Normal, &badTris)[0], Vec3{1, 0, 0}));
        Mesh flat; flat.vertices = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}; flat.triangles = {0, 1, 2};
        CHECK(Near(OrientationAxes(flat, {0, 1, 2}, Mat4::Identity(), EditOrient::Normal)[2], Vec3{0, 0, 1}));
        Mat4 nanM = Mat4::Identity(); nanM.at(0, 0) = NAN;
        CHECK(ortho(OrientationAxes(cube, {}, nanM, EditOrient::Local)) && ortho(OrientationAxes(cube, {}, nanM, EditOrient::Normal, &top)));

        // Move locked to X goes along the orientation's X.
        OrientAxes d45;
        d45.axis[0] = Vec3{1, 1, 0}.Normalized(); d45.axis[1] = Vec3{-1, 1, 0}.Normalized(); d45.axis[2] = Vec3{0, 0, 1};
        AxisLock lx; lx.Press(0, false);
        CHECK(Near(MoveMatrix(Vec3{1, 0, 0}, lx, d45).MultiplyPoint(Vec3{0, 0, 0}), Vec3{0.5f, 0.5f, 0}));
        AxisLock pz; pz.Press(2, true);   // the XY plane of the orientation
        CHECK(Near(MoveMatrix(Vec3{1, 2, 3}, pz, d45).MultiplyPoint(Vec3{0, 0, 0}), Vec3{1, 2, 0}));
        // The world-axis versions are unchanged.
        CHECK(Near(MoveMatrix(Vec3{1, 2, 3}, lx).MultiplyPoint(Vec3{0, 0, 0}), Vec3{1, 0, 0}));
        // Scale X by 2 along the orientation's X: a point on that axis doubles, a point on its Y stays.
        const Mat4 S = ScaleMatrix(Vec3{0, 0, 0}, lx, 2.0f, d45);
        CHECK(Near(S.MultiplyPoint(d45[0]), d45[0] * 2.0f) && Near(S.MultiplyPoint(d45[1]), d45[1]));
        CHECK(Near(ScaleMatrix(Vec3{1, 1, 1}, AxisLock{}, 3.0f, d45).MultiplyPoint(Vec3{2, 1, 1}), Vec3{4, 1, 1}));   // free: uniform
        // Mouse rays: locked to the orientation's X, the move stays on that axis; locked to a plane, it has no part along the normal.
        const Ray r0{Vec3{0, 0, 10}, Vec3{0, 0, -1}}, r1{Vec3{1, 0.2f, 10}, Vec3{0, 0, -1}};
        const Vec3 mx = MoveFromRays(lx, Vec3{0, 0, 0}, Vec3{0, 0, -1}, r0, r1, d45);
        CHECK(std::fabs(Vec3::Dot(mx, d45[1])) < 1e-4f && std::fabs(Vec3::Dot(mx, d45[2])) < 1e-4f && mx.Magnitude() > 0.1f);
        AxisLock px; px.Press(0, true);   // plane across the orientation's X
        const Ray s0{Vec3{10, 0, 0}, Vec3{-1, 0, 0}}, s1{Vec3{10, 1, 1}, Vec3{-1, 0, 0}};
        const Vec3 mp = MoveFromRays(px, Vec3{0, 0, 0}, Vec3{-1, 0, 0}, s0, s1, d45);
        CHECK(std::fabs(Vec3::Dot(mp, d45[0])) < 1e-4f && mp.Magnitude() > 0.1f);
        CHECK(Near(MoveFromRays(lx, Vec3{0, 0, 0}, Vec3{0, 0, -1}, r0, r1, OrientAxes{}), MoveFromRays(lx, Vec3{0, 0, 0}, Vec3{0, 0, -1}, r0, r1)));
    }

    // ---- Round 3: the 3D cursor as the pivot ----
    {
        Mesh m = Mesh::Cube(1.0f); m.WeldVertices();
        std::vector<int> all; for (int i = 0; i < (int)m.vertices.size(); ++i) all.push_back(i);
        const Mesh before = m;
        EditTransformOptions o; o.pivot = EditPivot::Cursor; o.cursorWorld = Vec3{10, 0, 0};
        EditTransformSession s; s.Begin(m, EditXform::Scale, all, Mat4::Identity(), o);
        CHECK(Near(s.PivotWorld(), Vec3{10, 0, 0}));
        s.Apply(m, ScaleMatrix(s.PivotWorld(), AxisLock{}, 2.0f));
        bool ok = true;
        for (int i = 0; i < (int)m.vertices.size(); ++i) if (!Near(m.vertices[i], (before.vertices[i] - Vec3{10, 0, 0}) * 2.0f + Vec3{10, 0, 0})) ok = false;
        CHECK(ok);
        s.Cancel(m);
        // On a moved object the cursor stays where it is in the world.
        EditTransformSession s2; s2.Begin(m, EditXform::Rotate, all, Mat4::Translate(Vec3{0, 5, 0}), o);
        CHECK(Near(s2.PivotWorld(), Vec3{10, 0, 0}));
        s2.Cancel(m);
        CHECK(std::string(EditPivotName(EditPivot::Cursor)) == "3D Cursor");
        // Hostile: a cursor that is not a number falls back to the selection's centre.
        EditTransformOptions bad; bad.pivot = EditPivot::Cursor; bad.cursorWorld = Vec3{NAN, 0, 0};
        EditTransformSession s3; s3.Begin(m, EditXform::Scale, all, Mat4::Identity(), bad);
        CHECK(Near(s3.PivotWorld(), Vec3{0, 0, 0}));
        s3.Cancel(m);
    }

    TEST_MAIN_RESULT();
}
