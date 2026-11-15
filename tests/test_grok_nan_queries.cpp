// Grok findings 20 and 21 (issue #888): a scene query whose origin, size or direction is
// not a number must say "no hit" (or return nothing), never "hit right here".
//
// Before the fix, in 3D, SphereCast / BoxCast / CapsuleCast from (0, NaN, 0) reported a
// hit at distance 0 with a NaN point (Raycast already said no hit). In 2D, Raycast from a
// NaN origin hit a box (even one 100 units away), the casts hit at distance 0, and
// OverlapBox with NaN half-extents returned colliders.
//
// Each case also runs a finite twin of the same query so the test proves the scene is set
// up to be hit, and that finite inputs (including an infinite reach) behave as before.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <limits>

using namespace okay;

namespace {
const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

// ---- 3D: one collider at the origin of the given kind.
GameObject* Collider3DAt(Scene& s, int kind) {
    GameObject* g = s.CreateGameObject("Target");
    if (kind == 0) g->AddComponent<BoxCollider3D>()->size = {10, 1, 10};
    else if (kind == 1) g->AddComponent<SphereCollider3D>()->radius = 1.0f;
    else if (kind == 2) { auto* c = g->AddComponent<CapsuleCollider3D>(); c->radius = 1.0f; c->height = 2.0f; }
    else { auto* c = g->AddComponent<CylinderCollider3D>(); c->radius = 1.0f; c->height = 2.0f; }
    return g;
}

// ---- 2D: one collider at the origin of the given kind.
GameObject* Collider2DAt(Scene& s, int kind) {
    GameObject* g = s.CreateGameObject("Target");
    if (kind == 0) g->AddComponent<BoxCollider2D>()->size = {10, 1};
    else if (kind == 1) g->AddComponent<CircleCollider2D>()->radius = 1.0f;
    else g->AddComponent<CapsuleCollider2D>()->size = {1, 2};
    return g;
}
} // namespace

int main() {
    RUN_SUITE("grok_nan_queries");

    // ======================= 3D (Finding 20) =======================
    for (int kind = 0; kind < 4; ++kind) {
        Scene s("q3");
        s.physicsEnabled = false;
        Collider3DAt(s, kind);
        s.Start();
        Physics3D& p = s.physics3D();
        const Vec3 down{0, -1, 0};

        // Finite twin: everything hits from above.
        CHECK(p.Raycast(s, {0, 5, 0}, down, 10.0f).hit);
        CHECK(p.SphereCast(s, {0, 5, 0}, down, 0.3f, 10.0f).hit);
        CHECK(p.BoxCast(s, {0, 5, 0}, {0.3f, 0.3f, 0.3f}, down, Quat::Identity, 10.0f).hit);
        CHECK(p.CapsuleCast(s, {0, 5, 0}, {0, 5.5f, 0}, 0.3f, down, 10.0f).hit);
        // An infinite reach is still a normal query (it was before, and stays so).
        CHECK(p.Raycast(s, {0, 5, 0}, down, kInf).hit);
        CHECK(p.SphereCast(s, {0, 5, 0}, down, 0.3f, kInf).hit);

        // Grok's trigger: a NaN in the origin.
        const Vec3 bad{0, kNaN, 0};
        CHECK(!p.Raycast(s, bad, down, 10.0f).hit);
        CHECK(p.RaycastAll(s, bad, down, 10.0f).empty());
        RaycastHit3D h = p.SphereCast(s, bad, down, 0.3f, 10.0f);
        CHECK(!h.hit);
        h = p.BoxCast(s, bad, {0.3f, 0.3f, 0.3f}, down, Quat::Identity, 10.0f);
        CHECK(!h.hit);
        h = p.CapsuleCast(s, bad, {0, 0.5f, 0}, 0.3f, down, 10.0f);
        CHECK(!h.hit);
        h = p.CapsuleCast(s, {0, 5, 0}, bad, 0.3f, down, 10.0f);    // the other end NaN
        CHECK(!h.hit);
        // An infinite origin, a NaN size, a NaN rotation, a NaN reach.
        CHECK(!p.SphereCast(s, {0, kInf, 0}, down, 0.3f, 10.0f).hit);
        CHECK(!p.SphereCast(s, {0, 5, 0}, down, kNaN, 10.0f).hit);
        CHECK(!p.BoxCast(s, {0, 5, 0}, {kNaN, 0.3f, 0.3f}, down, Quat::Identity, 10.0f).hit);
        CHECK(!p.BoxCast(s, {0, 5, 0}, {0.3f, 0.3f, 0.3f}, down, Quat{kNaN, 0, 0, 1}, 10.0f).hit);
        CHECK(!p.CapsuleCast(s, {0, 5, 0}, {0, 5.5f, 0}, kInf, down, 10.0f).hit);
        CHECK(!p.SphereCast(s, {0, 5, 0}, down, 0.3f, kNaN).hit);
        CHECK(!p.Raycast(s, {0, 5, 0}, down, kNaN).hit);
        CHECK(!p.Raycast(s, {0, 5, 0}, {0, kNaN, 0}, 10.0f).hit);
        // Overlaps: a NaN centre or size finds nothing (a finite one finds the collider).
        CHECK(p.OverlapSphere(s, {0, 0, 0}, 0.5f).size() == 1);
        CHECK(p.OverlapSphere(s, bad, 0.5f).empty());
        CHECK(p.OverlapSphere(s, {0, 0, 0}, kNaN).empty());
        CHECK(p.OverlapBox(s, {0, 0, 0}, {0.5f, 0.5f, 0.5f}).size() == 1);
        CHECK(p.OverlapBox(s, {0, 0, 0}, {kNaN, kNaN, kNaN}).empty());
        CHECK(p.OverlapBox(s, bad, {0.5f, 0.5f, 0.5f}).empty());
        CHECK(p.OverlapCapsule(s, {0, -0.2f, 0}, {0, 0.2f, 0}, 0.5f).size() == 1);
        CHECK(p.OverlapCapsule(s, bad, {0, 0.2f, 0}, 0.5f).empty());
    }

    // ======================= 2D (Finding 21) =======================
    for (int kind = 0; kind < 3; ++kind) {
        Scene s("q2");
        s.physicsEnabled = false;
        Collider2DAt(s, kind);
        s.Start();
        Physics2D& p = s.physics();
        const Vec2 down{0, -1};

        // Finite twin.
        CHECK(p.Raycast(s, {0, 5}, down, 10.0f).hit);
        CHECK(p.CircleCast(s, {0, 5}, down, 0.3f, 10.0f).hit);
        CHECK(p.BoxCast(s, {0, 5}, {0.3f, 0.3f}, down, 0.0f, 10.0f).hit);
        CHECK(p.CapsuleCast(s, {0, 5}, {0, 5.5f}, 0.3f, down, 10.0f).hit);
        CHECK(p.Raycast(s, {0, 5}, down, kInf).hit);   // an infinite reach still works

        // Grok's table: (0, NaN), (NaN, 5), (NaN, 105) and (0, +inf).
        const Vec2 origins[4] = {{0, kNaN}, {kNaN, 5}, {kNaN, 105}, {0, kInf}};
        for (const Vec2& o : origins) {
            RaycastHit2D h = p.Raycast(s, o, down, 10.0f);
            CHECK(!h.hit);
            CHECK(p.RaycastAll(s, o, down, 10.0f).empty());
            h = p.CircleCast(s, o, down, 0.3f, 10.0f);
            CHECK(!h.hit);
            h = p.BoxCast(s, o, {0.3f, 0.3f}, down, 0.0f, 10.0f);
            CHECK(!h.hit);
            h = p.CapsuleCast(s, o, Vec2{0, 5.5f}, 0.3f, down, 10.0f);
            CHECK(!h.hit);
            CHECK(!p.Linecast(s, o, {0, -5}).hit);
            CHECK(p.OverlapPoint(s, o) == nullptr);
            CHECK(p.OverlapCircle(s, o, 0.5f).empty());
            CHECK(p.OverlapBox(s, o, {0.5f, 0.5f}).empty());
            CHECK(p.OverlapCapsule(s, o, {0, 0}, 0.5f).empty());
        }
        // A NaN size, angle, direction or reach.
        CHECK(!p.CircleCast(s, {0, 5}, down, kNaN, 10.0f).hit);
        CHECK(!p.BoxCast(s, {0, 5}, {kNaN, 0.3f}, down, 0.0f, 10.0f).hit);
        CHECK(!p.BoxCast(s, {0, 5}, {0.3f, 0.3f}, down, kNaN, 10.0f).hit);
        CHECK(!p.CapsuleCast(s, {0, 5}, {0, 5.5f}, kNaN, down, 10.0f).hit);
        CHECK(!p.Raycast(s, {0, 5}, {kNaN, -1}, 10.0f).hit);
        CHECK(!p.Raycast(s, {0, 5}, down, kNaN).hit);
        // Overlaps: Grok's OverlapBox with NaN half-extents, and its finite twin.
        CHECK(p.OverlapBox(s, {0, 0}, {0.5f, 0.5f}).size() == 1);
        CHECK(p.OverlapBox(s, {0, 0}, {kNaN, kNaN}).empty());
        CHECK(p.OverlapBox(s, {0, 0}, {kNaN, kNaN}, Physics2D::kAllLayers, true, nullptr, 0.5f).empty());
        CHECK(p.OverlapBox(s, {0, 0}, {0.5f, 0.5f}, Physics2D::kAllLayers, true, nullptr, kNaN).empty());
        CHECK(p.OverlapCircle(s, {0, 0}, 0.5f).size() == 1);
        CHECK(p.OverlapCircle(s, {0, 0}, kNaN).empty());
        CHECK(p.OverlapCapsule(s, {0, -0.2f}, {0, 0.2f}, 0.5f).size() == 1);
        CHECK(p.OverlapCapsule(s, {0, -0.2f}, {0, 0.2f}, kNaN).empty());
    }

    TEST_MAIN_RESULT();
}
