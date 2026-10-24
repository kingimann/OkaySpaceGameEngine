// Modeling milestone 1: the G / R / S modal transforms used by mesh edit mode,
// and Mirror no longer overlapping a mesh that crosses the mirror plane.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Modeling/EditTransform.hpp"
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

    TEST_MAIN_RESULT();
}
