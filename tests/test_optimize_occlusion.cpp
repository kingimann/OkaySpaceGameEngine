// Optimization tools 27.2: the CPU occlusion buffer and portal / cell visibility.
#include "test_framework.hpp"
#include "okay/Optimize/OcclusionBuffer.hpp"
#include "okay/Optimize/PortalCells.hpp"
#include <cmath>
#include <cstring>
#include <string>

using namespace okay;
using namespace okay::optimize;

namespace {

Bounds Box(float cx, float cy, float cz, float sx, float sy, float sz) { return Bounds(Vec3{cx, cy, cz}, Vec3{sx, sy, sz}); }

OcclusionBuffer MakeBuffer(const Vec3& eye, const Vec3& target) {
    OcclusionBuffer b(256, 128);
    b.SetCamera(eye, target, Vec3{0, 1, 0}, 60.0f, 2.0f, 0.1f, 200.0f);
    return b;
}

// Tiny deterministic generator for the sweep tests.
struct Lcg {
    unsigned s;
    explicit Lcg(unsigned seed) : s(seed) {}
    float Next() { s = s * 1664525u + 1013904223u; return static_cast<float>((s >> 8) & 0xffff) / 65535.0f; }
    float Range(float lo, float hi) { return lo + (hi - lo) * Next(); }
};

// Reference: does the segment from `from` to `to` pass through the box?
bool SegmentHitsBox(const Vec3& from, const Vec3& to, const Bounds& box) {
    const Vec3 d = to - from;
    float t0 = 0.0f, t1 = 1.0f;
    const Vec3 mn = box.Min(), mx = box.Max();
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(d[a]) < 1e-9f) { if (from[a] < mn[a] || from[a] > mx[a]) return false; continue; }
        float ta = (mn[a] - from[a]) / d[a], tb = (mx[a] - from[a]) / d[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    return true;
}

unsigned Hash(const OcclusionBuffer& b) {
    unsigned h = 2166136261u;
    for (int y = 0; y < b.Height(); ++y)
        for (int x = 0; x < b.Width(); ++x) {
            const float f = b.InverseDepthAt(x, y);
            unsigned bits;
            std::memcpy(&bits, &f, sizeof(bits));
            h = (h ^ bits) * 16777619u;
        }
    return h;
}

}  // namespace

int main() {
    RUN_SUITE("optimize_occlusion");

    const Vec3 eye{0, 0, 10};
    const Bounds wall = Box(0, 0, 0, 10, 6, 0.2f);

    // ---- a wall hides a box behind it -------------------------------------------------------------
    {
        OcclusionBuffer b = MakeBuffer(eye, Vec3{0, 0, 0});
        CHECK(b.Width() == 256 && b.Height() == 128);
        CHECK(b.Coverage() == 0.0f);
        // Nothing rasterized yet: nothing is occluded.
        CHECK(b.Test(Box(0, 0, -5, 1, 1, 1)) == OcclusionResult::Visible);
        b.RasterizeBox(wall);
        CHECK(b.Coverage() > 0.1f);
        CHECK(b.Stats().trianglesIn == 12 && b.Stats().trianglesRasterized > 0);
        CHECK(b.Covered(128, 64));                                                   // the middle of the screen
        CHECK(!b.Covered(2, 2));                                                     // the corner sees past the wall
        CHECK(b.Test(Box(0, 0, -5, 1, 1, 1)) == OcclusionResult::Occluded);          // hidden
        CHECK(b.IsOccluded(Box(2, 1, -20, 2, 2, 2)));                                // farther away, still hidden
        CHECK(b.IsOccludedSphere(Vec3{-2, -1, -8}, 0.7f));
        // In front of the wall: visible.
        CHECK(b.Test(Box(0, 0, 4, 1, 1, 1)) == OcclusionResult::Visible);
        // Touching the wall's front, or poking through it: visible.
        CHECK(!b.IsOccluded(Box(0, 0, 0.5f, 1, 1, 1)));
        CHECK(!b.IsOccluded(Box(0, 0, 0, 1, 1, 3)));
        // Beside the wall (outside its silhouette): visible.
        CHECK(!b.IsOccluded(Box(9, 0, -5, 1, 1, 1)));
        CHECK(!b.IsOccluded(Box(0, 5, -5, 1, 1, 1)));                                // above it
        // Partially visible: straddling the wall's edge stays visible.
        CHECK(b.Test(Box(7.5f, 0, -5, 3, 1, 1)) == OcclusionResult::Visible);
        CHECK(b.Test(Box(0, 3, -5, 1, 3, 1)) == OcclusionResult::Visible);
        // Behind the camera / far off to the side: not on screen at all.
        CHECK(b.Test(Box(0, 0, 20, 1, 1, 1)) == OcclusionResult::OffScreen);
        CHECK(b.Test(Box(500, 0, -5, 1, 1, 1)) == OcclusionResult::OffScreen);
        CHECK(!b.IsOccluded(Box(0, 0, 20, 1, 1, 1)));                                // off screen is not "occluded"
        CHECK(b.Stats().queries > 5 && b.Stats().occluded >= 3);
        // Clear forgets the occluders.
        b.Clear();
        CHECK(b.Coverage() == 0.0f);
        CHECK(!b.IsOccluded(Box(0, 0, -5, 1, 1, 1)));
    }

    // ---- two triangles of a quad leave no gap along the diagonal -----------------------------------
    {
        OcclusionBuffer b = MakeBuffer(eye, Vec3{0, 0, 0});
        // A wall quad as two triangles whose diagonal runs across the middle of the screen.
        b.RasterizeQuad(Vec3{-6, -3.4f, 0}, Vec3{6, -3.4f, 0}, Vec3{6, 3.4f, 0}, Vec3{-6, 3.4f, 0});
        // Every pixel in the interior of the quad's projection is covered: no seam.
        int holes = 0;
        for (int y = 40; y < 90; ++y) for (int x = 64; x < 192; ++x) if (!b.Covered(x, y)) ++holes;
        CHECK(holes == 0);
        // Boxes placed along the diagonal are hidden, wherever they are.
        for (float t = -0.8f; t <= 0.8f; t += 0.2f) CHECK(b.IsOccluded(Box(t * 4.0f, t * 2.0f, -6, 0.8f, 0.8f, 0.8f)));
    }

    // ---- the buffer never hides something that can be seen (random sweep against ray casts) ---------
    {
        const Bounds walls[3] = {Box(0, 0, 0, 10, 6, 0.4f), Box(-9, 0, -3, 0.4f, 6, 8), Box(6, -2, 3, 6, 2, 0.4f)};
        OcclusionBuffer b = MakeBuffer(eye, Vec3{0, 0, -2});
        for (const Bounds& w : walls) b.RasterizeBox(w);
        const Mat4 vp = b.ViewProjection();
        Lcg rng(1234);
        int occluded = 0, tested = 0, violations = 0;
        for (int i = 0; i < 3000; ++i) {
            const Bounds box(Vec3{rng.Range(-12, 12), rng.Range(-4, 4), rng.Range(-30, 6)},
                             Vec3{rng.Range(0.3f, 3.0f), rng.Range(0.3f, 3.0f), rng.Range(0.3f, 3.0f)});
            if (b.Test(box) != OcclusionResult::Occluded) { ++tested; continue; }
            ++occluded;
            // Every on-screen sample point of an occluded box must have a blocked line of sight.
            for (int sx = 0; sx < 3; ++sx) for (int sy = 0; sy < 3; ++sy) for (int sz = 0; sz < 3; ++sz) {
                const Vec3 p = box.Min() + Vec3{box.Size().x * sx * 0.5f, box.Size().y * sy * 0.5f, box.Size().z * sz * 0.5f};
                const Vec4 c = vp * Vec4{p, 1.0f};
                if (c.w <= 0.1f || std::fabs(c.x / c.w) > 1.0f || std::fabs(c.y / c.w) > 1.0f) continue;
                // Gaps narrower than about a pixel between two occluders are not resolved, so the reference
                // walls are grown by 1.5 pixels' worth (0.15 units at this distance) before a point counts as seen.
                bool blocked = false;
                for (const Bounds& w : walls) blocked = blocked || SegmentHitsBox(eye, p, Bounds(w.center, w.Size() + Vec3(0.3f)));
                if (!blocked) ++violations;
            }
        }
        CHECK(violations == 0);
        CHECK(occluded > 150);                                                       // the test is not vacuous
        CHECK(tested > 150);
    }

    // ---- the camera is inside --------------------------------------------------------------------------
    {
        const Vec3 inside{0, 0, 0};
        OcclusionBuffer b = MakeBuffer(inside, Vec3{0, 0, -1});
        // A room around the camera: six large walls, double sided.
        const float h = 10.0f;
        b.RasterizeQuad(Vec3{-h, -h, -h}, Vec3{h, -h, -h}, Vec3{h, h, -h}, Vec3{-h, h, -h});   // front
        b.RasterizeQuad(Vec3{-h, -h, h}, Vec3{h, -h, h}, Vec3{h, h, h}, Vec3{-h, h, h});       // back
        b.RasterizeQuad(Vec3{-h, -h, -h}, Vec3{-h, -h, h}, Vec3{-h, h, h}, Vec3{-h, h, -h});   // left
        b.RasterizeQuad(Vec3{h, -h, -h}, Vec3{h, -h, h}, Vec3{h, h, h}, Vec3{h, h, -h});       // right
        b.RasterizeQuad(Vec3{-h, -h, -h}, Vec3{h, -h, -h}, Vec3{h, -h, h}, Vec3{-h, -h, h});   // floor
        b.RasterizeQuad(Vec3{-h, h, -h}, Vec3{h, h, -h}, Vec3{h, h, h}, Vec3{-h, h, h});       // ceiling
        // Walls (partly behind the near plane at their closest) cover the whole view.
        CHECK(b.Coverage() > 0.99f);
        CHECK(b.IsOccluded(Box(0, 0, -30, 4, 4, 4)));                                // outside the room: hidden
        CHECK(b.IsOccluded(Box(3, 1, -14, 4, 4, 4)));
        CHECK(!b.IsOccluded(Box(0, 0, -5, 2, 2, 2)));                                // inside the room: visible
        CHECK(!b.IsOccluded(Box(3, -2, -8, 1, 1, 1)));
        // A box that contains the camera is never hidden, and neither is one crossing the near plane.
        CHECK(b.Test(Box(0, 0, 0, 4, 4, 4)) == OcclusionResult::NearClipped);
        CHECK(b.Test(Box(0, 0, -0.1f, 1, 1, 1)) == OcclusionResult::NearClipped);
        CHECK(!b.IsOccluded(Box(0, 0, 0, 4, 4, 4)));
        // A solid box around the camera works the same as the quads.
        OcclusionBuffer c = MakeBuffer(inside, Vec3{1, 0, 0});
        c.RasterizeBox(Box(0, 0, 0, 20, 20, 20));
        CHECK(c.Coverage() > 0.99f);
        CHECK(c.IsOccluded(Box(40, 0, 0, 3, 3, 3)));
        CHECK(!c.IsOccluded(Box(6, 0, 0, 2, 2, 2)));
    }

    // ---- a floor that runs behind the camera is clipped, not mangled ---------------------------------------
    {
        OcclusionBuffer b = MakeBuffer(Vec3{0, 3, 10}, Vec3{0, 0, 0});
        b.RasterizeQuad(Vec3{-100, 0, -100}, Vec3{100, 0, -100}, Vec3{100, 0, 100}, Vec3{-100, 0, 100});
        CHECK(b.Coverage() > 0.2f && b.Coverage() < 0.8f);
        CHECK(b.Covered(128, 5));                                                    // bottom of the screen is the floor
        CHECK(!b.Covered(128, 125));                                                 // the sky is empty
        CHECK(b.IsOccluded(Box(0, -3, -20, 2, 2, 2)));                               // under the floor
        CHECK(!b.IsOccluded(Box(0, 2, -20, 2, 2, 2)));                               // standing on it
    }

    // ---- mesh occluders, model matrices, backface culling -------------------------------------------------------
    {
        const Mesh cube = Mesh::Cube(1.0f);
        const Mat4 model = Mat4::TRS(Vec3{0, 0, 0}, Quat::Identity, Vec3{8, 5, 1});
        OcclusionBuffer a = MakeBuffer(eye, Vec3{0, 0, 0});
        a.RasterizeMesh(cube, model);
        OcclusionBuffer c = MakeBuffer(eye, Vec3{0, 0, 0});
        c.RasterizeMesh(cube, model, true);                                           // a closed mesh needs only its front faces
        CHECK(Hash(a) == Hash(c));
        CHECK(a.Stats().trianglesIn == 12);
        CHECK(a.IsOccluded(Box(0, 0, -6, 1, 1, 1)));
        CHECK(!a.IsOccluded(Box(0, 0, 6, 1, 1, 1)));
        // The same wall as an oriented box under the same matrix.
        OcclusionBuffer d = MakeBuffer(eye, Vec3{0, 0, 0});
        d.RasterizeBox(Box(0, 0, 0, 1, 1, 1), model);
        CHECK(Hash(a) == Hash(d));
        // Rotated wall (turned 90 degrees, seen edge on) hides little.
        OcclusionBuffer e = MakeBuffer(eye, Vec3{0, 0, 0});
        e.RasterizeBox(Box(0, 0, 0, 1, 1, 1), Mat4::TRS(Vec3{0, 0, 0}, Quat::Euler(0, 90, 0), Vec3{8, 5, 1}));
        CHECK(e.Coverage() < a.Coverage());
        // A sphere occluder hides a small box right behind its middle but not one beside it.
        OcclusionBuffer s = MakeBuffer(eye, Vec3{0, 0, 0});
        s.RasterizeMesh(Mesh::Sphere(0.5f, 16, 24), Mat4::Scale(Vec3{8, 8, 8}), true);
        CHECK(s.IsOccluded(Box(0, 0, -6, 1, 1, 1)));
        CHECK(!s.IsOccluded(Box(7, 0, -6, 1, 1, 1)));
    }

    // ---- determinism, resizing, depth bias ------------------------------------------------------------------------
    {
        OcclusionBuffer a = MakeBuffer(eye, Vec3{0, 0, 0});
        OcclusionBuffer c = MakeBuffer(eye, Vec3{0, 0, 0});
        for (OcclusionBuffer* b : {&a, &c}) { b->RasterizeBox(wall); b->RasterizeBox(Box(6, 1, 3, 2, 2, 2)); }
        CHECK(Hash(a) == Hash(c));
        a.Resize(64, 32);
        CHECK(a.Width() == 64 && a.Height() == 32 && a.Coverage() == 0.0f);
        a.RasterizeBox(wall);
        CHECK(a.IsOccluded(Box(0, 0, -5, 1, 1, 1)));                                 // works at a coarse resolution too
        // A large bias keeps a box that is only slightly behind the wall visible.
        OcclusionBuffer b = MakeBuffer(eye, Vec3{0, 0, 0});
        b.RasterizeBox(wall);
        const Bounds nearBox = Box(0, 0, -0.5f, 1, 1, 0.6f);                             // its front face is 0.2 behind the wall's back face
        CHECK(b.IsOccluded(nearBox));
        b.SetDepthBias(0.5f);
        CHECK(!b.IsOccluded(nearBox));
        CHECK(b.DepthBias() == 0.5f);
        // The text picture has the right shape.
        b.SetDepthBias(0.002f);
        const std::string pic = b.ToAscii(32, 8);
        CHECK(pic.size() == static_cast<std::size_t>(33 * 8));
        CHECK(pic.find('#') != std::string::npos && pic.find('.') != std::string::npos);
        // Degenerate input is ignored.
        OcclusionBuffer z = MakeBuffer(eye, Vec3{0, 0, 0});
        z.RasterizeTriangle(Vec3{0, 0, 0}, Vec3{0, 0, 0}, Vec3{0, 0, 0});
        z.RasterizeTriangle(Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{2, 0, 0});
        z.RasterizeTriangle(Vec3{0, 0, 20}, Vec3{1, 0, 20}, Vec3{0, 1, 20});          // behind the camera
        CHECK(z.Coverage() == 0.0f);
    }

    // ================= portal cells ====================================================================================
    // Three rooms in a row along x: A [0,10], B [10,20], C [20,30]; y 0..4, z 0..10.
    auto Rooms = [](float bcDoorZ) {
        PortalCells p;
        p.AddCell(Bounds(Vec3{5, 2, 5}, Vec3{10, 4, 10}), "A");
        p.AddCell(Bounds(Vec3{15, 2, 5}, Vec3{10, 4, 10}), "B");
        p.AddCell(Bounds(Vec3{25, 2, 5}, Vec3{10, 4, 10}), "C");
        p.AddPortalRect(0, 1, Vec3{10, 2, 5}, Vec3{0, 0, 1}, Vec3{0, 1.5f, 0}, true, "AB");
        p.AddPortalRect(1, 2, Vec3{20, 2, bcDoorZ}, Vec3{0, 0, 1}, Vec3{0, 1.5f, 0}, true, "BC");
        return p;
    };
    auto View = [](const Vec3& e, const Vec3& t) {
        return Mat4::Perspective(60.0f, 1.0f, 0.1f, 100.0f) * Mat4::LookAt(e, t, Vec3{0, 1, 0});
    };
    {
        PortalCells p = Rooms(8.5f);
        CHECK(p.CellCount() == 3 && p.PortalCount() == 2);
        CHECK(p.FindCell(Vec3{2, 2, 5}) == 0 && p.FindCell(Vec3{12, 2, 5}) == 1 && p.FindCell(Vec3{25, 2, 5}) == 2);
        CHECK(p.FindCell(Vec3{50, 2, 5}) == -1);
        const Vec3 cam{2, 2, 5};
        PortalVisibility v = p.ComputeVisibility(cam, View(cam, Vec3{10, 2, 5}));
        CHECK(v.cameraCell == 0 && !v.cameraOutside);
        CHECK(v.CellVisible(0) && v.CellVisible(1));                                  // through the doorway
        CHECK(!v.CellVisible(2));                                                      // the far door is off to the side of that view
        CHECK(v.portalsPassed >= 1 && v.portalsTested >= v.portalsPassed);
        // Narrowing: an object in B inside the doorway's cone is visible, one in the frustum but behind the wall is not.
        CHECK(v.BoundsVisible(1, Bounds(Vec3{15, 2, 5.5f}, Vec3{1, 1, 1})));
        CHECK(!v.BoundsVisible(1, Bounds(Vec3{15, 2, 9}, Vec3{1, 1, 1})));
        CHECK(v.BoundsVisible(0, Bounds(Vec3{6, 2, 5}, Vec3{1, 1, 1})));              // the camera's own cell uses the full frustum
        CHECK(!v.BoundsVisible(2, Bounds(Vec3{25, 2, 5}, Vec3{1, 1, 1})));
        // Looking away from the door: only the camera's room.
        PortalVisibility away = p.ComputeVisibility(cam, View(cam, Vec3{-10, 2, 5}));
        CHECK(away.CellVisible(0) && !away.CellVisible(1) && !away.CellVisible(2));
        CHECK(away.visibleCells.size() == 1);
        // A closed door blocks the view.
        p.SetPortalOpen(0, false);
        PortalVisibility closed = p.ComputeVisibility(cam, View(cam, Vec3{10, 2, 5}));
        CHECK(closed.CellVisible(0) && !closed.CellVisible(1));
        p.SetPortalOpen(0, true);
        // Looking at the door from the side so the line of sight is too steep to pass: still sees B? (door is wide in y)
        PortalVisibility steep = p.ComputeVisibility(Vec3{9, 2, 0.5f}, View(Vec3{9, 2, 0.5f}, Vec3{12, 2, 9}));
        CHECK(steep.CellVisible(1));
        // Outside every cell: the caller must treat everything as visible.
        PortalVisibility out = p.ComputeVisibility(Vec3{100, 2, 5}, View(Vec3{100, 2, 5}, Vec3{0, 2, 5}));
        CHECK(out.cameraOutside && out.cameraCell == -1 && out.visibleCells.empty());
        CHECK(out.BoundsVisible(1, Bounds(Vec3{15, 2, 5}, Vec3{1, 1, 1})));
    }
    {
        // Aligned doors: a straight view down the whole suite sees every room, farthest last.
        PortalCells p = Rooms(5.0f);
        const Vec3 cam{1, 2, 5};
        PortalVisibility v = p.ComputeVisibility(cam, View(cam, Vec3{29, 2, 5}));
        CHECK(v.CellVisible(0) && v.CellVisible(1) && v.CellVisible(2));
        CHECK(v.BoundsVisible(2, Bounds(Vec3{25, 2, 5}, Vec3{1, 1, 1})));
        CHECK(!v.BoundsVisible(2, Bounds(Vec3{25, 2, 9.5f}, Vec3{1, 1, 1})));          // beyond the doorway's cone
        // maxDepth limits how many portals the walk crosses.
        PortalVisibility shallow = p.ComputeVisibility(cam, View(cam, Vec3{29, 2, 5}), 1);
        CHECK(shallow.CellVisible(1) && !shallow.CellVisible(2));
        // A tiny visit budget stops early and says so.
        PortalVisibility cut = p.ComputeVisibility(cam, View(cam, Vec3{29, 2, 5}), 32, 1);
        CHECK(cut.truncated);
        // Standing in a doorway: the portal plane passes through the eye, so nothing is narrowed away.
        const Vec3 door{10, 2, 5};
        PortalVisibility in = p.ComputeVisibility(door, View(door, Vec3{29, 2, 5}));
        CHECK(in.CellVisible(1) && in.CellVisible(2));
    }
    {
        // A loop of rooms (A-B-C-A) terminates and sees each room once.
        PortalCells p;
        p.AddCell(Bounds(Vec3{5, 2, 5}, Vec3{10, 4, 10}), "A");
        p.AddCell(Bounds(Vec3{15, 2, 5}, Vec3{10, 4, 10}), "B");
        p.AddCell(Bounds(Vec3{10, 2, 15}, Vec3{20, 4, 10}), "C");
        p.AddPortalRect(0, 1, Vec3{10, 2, 5}, Vec3{0, 0, 2}, Vec3{0, 1.5f, 0});
        p.AddPortalRect(1, 2, Vec3{15, 2, 10}, Vec3{2, 0, 0}, Vec3{0, 1.5f, 0});
        p.AddPortalRect(2, 0, Vec3{5, 2, 10}, Vec3{2, 0, 0}, Vec3{0, 1.5f, 0});
        const Vec3 cam{3, 2, 3};
        PortalVisibility v = p.ComputeVisibility(cam, View(cam, Vec3{12, 2, 12}));
        CHECK(v.visibleCells.size() == 3);
        CHECK(!v.truncated);
        CHECK(p.AddPortal(0, 0, {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0}}) == -1);   // bad portals are refused
        CHECK(p.AddPortal(0, 9, {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0}}) == -1);
        CHECK(p.AddPortal(0, 1, {Vec3{0, 0, 0}, Vec3{1, 0, 0}}) == -1);
        // Overlapping cells: the smaller one wins.
        p.AddCell(Bounds(Vec3{3, 2, 3}, Vec3{2, 2, 2}), "closet");
        CHECK(p.FindCell(Vec3{3, 2, 3}) == 3);
        CHECK(p.FindCell(Vec3{8, 2, 8}) == 0);
    }
    {
        // Polygon clipping against a region, and the view-projection planes.
        const ConvexRegion r = ConvexRegion::FromViewProjection(View(Vec3{0, 0, 0}, Vec3{0, 0, -1}));
        CHECK(r.planes.size() == 6);
        CHECK(r.ContainsPoint(Vec3{0, 0, -5}));
        CHECK(!r.ContainsPoint(Vec3{0, 0, 5}));
        CHECK(!r.ContainsPoint(Vec3{50, 0, -5}));
        CHECK(r.IntersectsBounds(Bounds(Vec3{0, 0, -5}, Vec3{1, 1, 1})));
        CHECK(!r.IntersectsBounds(Bounds(Vec3{0, 0, 5}, Vec3{1, 1, 1})));
        CHECK(r.IntersectsSphere(Vec3{4, 0, -5}, 1.5f));
        CHECK(!r.IntersectsSphere(Vec3{40, 0, -5}, 1.5f));
        const std::vector<Vec3> inside = ClipPolygon({Vec3{-1, -1, -5}, Vec3{1, -1, -5}, Vec3{1, 1, -5}, Vec3{-1, 1, -5}}, r);
        CHECK(inside.size() == 4);
        const std::vector<Vec3> none = ClipPolygon({Vec3{40, -1, -5}, Vec3{42, -1, -5}, Vec3{42, 1, -5}}, r);
        CHECK(none.empty());
        const std::vector<Vec3> cut = ClipPolygon({Vec3{-1, -1, -5}, Vec3{40, -1, -5}, Vec3{40, 1, -5}, Vec3{-1, 1, -5}}, r);
        CHECK(cut.size() >= 3);
        for (const Vec3& v : cut) CHECK(v.x <= 5.0f * 0.5774f + 1e-3f);                // clipped to the right plane (tan 30 at z = -5)
    }

    TEST_MAIN_RESULT();
}
