// Contact points between two boxes (Physics/BoxManifold.hpp): a box resting on a box touches along its whole face.
// Header-only maths, so no scene is needed.
#include "test_framework.hpp"
#include "okay/Physics/BoxManifold.hpp"
#include "okay/Math/Quat.hpp"

using namespace okay;
using namespace okay::boxmanifold;

static Box MakeBox(Vec3 c, Vec3 half, const Quat& q = Quat::Identity) {
    Box b; b.c = c; b.h = half;
    b.ax[0] = q * Vec3{1, 0, 0}; b.ax[1] = q * Vec3{0, 1, 0}; b.ax[2] = q * Vec3{0, 0, 1};
    return b;
}

int main() {
    RUN_SUITE("physics_deep_manifold");
    const Vec3 half{0.5f, 0.5f, 0.5f};

    // ---- Two lined-up cubes, one on the other, overlapping by 1 cm: four points at the corners, all 1 cm deep, normal up.
    {
        Box A = MakeBox({0, 0, 0}, half), B = MakeBox({0, 0.99f, 0}, half);
        Result r = Collide(A, B, 0.005f);
        CHECK(r.hit);
        CHECK(r.count == 4);
        CHECK(!r.edgeEdge);
        CHECK_NEAR(r.normal.y, 1.0f, 1e-4);
        for (int i = 0; i < r.count; ++i) {
            CHECK_NEAR(r.pts[i].depth, 0.01f, 1e-3);
            CHECK_NEAR(std::fabs(r.pts[i].pos.x), 0.5f, 1e-3);   // at the corners of the face
            CHECK_NEAR(std::fabs(r.pts[i].pos.z), 0.5f, 1e-3);
            CHECK_NEAR(r.pts[i].pos.y, 0.495f, 1e-3);             // halfway between the two surfaces
        }
    }
    // ---- Just touching (zero gap) and inside the skin (3 mm gap): still a contact with four points, depth <= 0.
    {
        Box A = MakeBox({0, 0, 0}, half), B = MakeBox({0, 1.0f, 0}, half);
        Result r = Collide(A, B, 0.005f);
        CHECK(r.hit && r.count == 4);
        CHECK_NEAR(r.pts[0].depth, 0.0f, 1e-4);
        Box C = MakeBox({0, 1.003f, 0}, half);
        Result g = Collide(A, C, 0.005f);
        CHECK(g.hit && g.count == 4);
        CHECK_NEAR(g.pts[0].depth, -0.003f, 1e-4);
        Box D = MakeBox({0, 1.008f, 0}, half);
        CHECK(!Collide(A, D, 0.005f).hit);                        // farther than the skin
        CHECK(!Collide(A, B, -0.001f).hit);                       // a negative skin means "overlap only"
    }
    // ---- Shifted half a box sideways: the contact patch is the overlap, four points inside it.
    {
        Box A = MakeBox({0, 0, 0}, half), B = MakeBox({0.5f, 0.99f, 0}, half);
        Result r = Collide(A, B, 0.005f);
        CHECK(r.hit && r.count == 4);
        for (int i = 0; i < r.count; ++i) {
            CHECK(r.pts[i].pos.x >= -0.001f && r.pts[i].pos.x <= 0.501f);   // inside the overlap in x (0 .. 0.5)
            CHECK_NEAR(r.pts[i].depth, 0.01f, 1e-3);
        }
    }
    // ---- A cube turned 45 degrees about the vertical on another: the diamond is clipped; at most four points, all inside A's face.
    {
        Box A = MakeBox({0, 0, 0}, half), B = MakeBox({0, 0.99f, 0}, half, Quat::AngleAxis(45.0f, {0, 1, 0}));
        Result r = Collide(A, B, 0.005f);
        CHECK(r.hit && r.count >= 3 && r.count <= 4);
        for (int i = 0; i < r.count; ++i) CHECK(std::fabs(r.pts[i].pos.x) <= 0.501f && std::fabs(r.pts[i].pos.z) <= 0.501f);
    }
    // ---- A cube tilted 10 degrees about x: it sits on one edge, so the contact is a line (two points), not four.
    {
        Box A = MakeBox({0, 0, 0}, half);
        Quat q = Quat::AngleAxis(10.0f, {1, 0, 0});
        Box B = MakeBox({0, 0.5f + 0.5f * (std::cos(10.0f * 0.0174533f) + std::sin(10.0f * 0.0174533f)) - 0.01f, 0}, half, q);
        Result r = Collide(A, B, 0.005f);
        CHECK(r.hit);
        CHECK(r.count == 2);
        CHECK_NEAR(r.normal.y, 1.0f, 1e-3);
    }
    // ---- The normal points from A toward B whichever box owns the face (swap the boxes).
    {
        Box A = MakeBox({0, 0, 0}, half), B = MakeBox({0, 0.99f, 0}, half);
        Result ab = Collide(A, B, 0.005f), ba = Collide(B, A, 0.005f);
        CHECK(ab.normal.y > 0.99f);
        CHECK(ba.normal.y < -0.99f);
        CHECK(ab.count == 4 && ba.count == 4);
    }
    // ---- A small box on a big floor (the floor owns the face): the small box's whole underside touches.
    {
        Box floorBox = MakeBox({0, -0.5f, 0}, {50, 0.5f, 50});
        Box crate = MakeBox({3.0f, 0.495f, -2.0f}, half);
        Result r = Collide(floorBox, crate, 0.005f);
        CHECK(r.hit && r.count == 4);
        for (int i = 0; i < r.count; ++i) CHECK_NEAR(r.pts[i].depth, 0.005f, 1e-3);
        CHECK_NEAR(r.normal.y, 1.0f, 1e-4);
    }
    // ---- Two skew edges (one box turned 45 degrees about x, the other 45 degrees about z): a single edge-on-edge point.
    {
        Box A = MakeBox({0, 0, 0}, half, Quat::AngleAxis(45.0f, {1, 0, 0}));
        float edgeTop = 0.5f * 1.41421356f;     // the turned cube's top edge is this high
        Box B = MakeBox({0, 2.0f * edgeTop - 0.02f, 0}, half, Quat::AngleAxis(45.0f, {0, 0, 1}));
        Result r = Collide(A, B, 0.005f);
        CHECK(r.hit);
        CHECK(r.edgeEdge);
        CHECK(r.count == 1);
        CHECK(r.normal.y > 0.95f);
        CHECK_NEAR(r.pts[0].depth, 0.02f, 4e-3);
    }
    // ---- Far apart and clearly crossing give no contact / a deep contact.
    {
        Box A = MakeBox({0, 0, 0}, half), far = MakeBox({5, 0, 0}, half), deep = MakeBox({0.3f, 0, 0}, half);
        CHECK(!Collide(A, far, 0.005f).hit);
        Result r = Collide(A, deep, 0.005f);
        CHECK(r.hit);
        CHECK_NEAR(r.pts[0].depth, 0.7f, 1e-3);                   // pushed out along x by 0.7
        CHECK_NEAR(r.normal.x, 1.0f, 1e-4);
    }
    TEST_MAIN_RESULT();
}
