#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

int main() {
    RUN_SUITE("phys_math");

    // ---- Vec3/Vec2 SmoothDamp: converges to the target and slows as it arrives ----
    {
        Vec3 cur{0, 0, 0}, vel{0, 0, 0};
        for (int i = 0; i < 600; ++i) cur = Vec3::SmoothDamp(cur, {10, 0, 0}, vel, 0.3f, 1.0f / 60.0f);
        CHECK(std::fabs(cur.x - 10.0f) < 0.05f);
        CHECK(vel.Magnitude() < 0.5f);                       // damped to rest at the target

        Vec2 c2{0, 0}, v2{0, 0};
        Vec2 prev = c2; bool overshot = false;
        for (int i = 0; i < 300; ++i) {
            c2 = Vec2::SmoothDamp(c2, {5, 5}, v2, 0.25f, 1.0f / 60.0f);
            if (c2.x > 5.01f || c2.y > 5.01f) overshot = true;  // critically damped: no overshoot
            prev = c2;
        }
        CHECK(!overshot);
        CHECK((c2 - Vec2{5, 5}).Magnitude() < 0.05f);
        (void)prev;
    }

    // ---- Quat::Lerp (nlerp): stays unit length, lands on the endpoints ----
    {
        Quat a = Quat::Identity, b = Quat::AngleAxis(90.0f, Vec3::Up);
        Quat m = Quat::Lerp(a, b, 0.5f);
        CHECK(std::fabs(m.Magnitude() - 1.0f) < 1e-3f);
        CHECK(Quat::Angle(Quat::Lerp(a, b, 0.0f), a) < 0.1f);
        CHECK(Quat::Angle(Quat::Lerp(a, b, 1.0f), b) < 0.1f);
        CHECK(Quat::Angle(m, Quat::AngleAxis(45.0f, Vec3::Up)) < 2.0f);   // ~halfway
    }

    // ---- Mat4 Transpose & Determinant ----
    {
        Mat4 t = Mat4::Translate({3, 4, 5});
        CHECK(std::fabs(t.Determinant() - 1.0f) < 1e-4f);   // rigid transform, det 1
        Mat4 s = Mat4::Scale({2, 3, 4});
        CHECK(std::fabs(s.Determinant() - 24.0f) < 1e-3f);  // product of the scales
        // (Aᵀ)ᵀ == A
        Mat4 tt = t.Transpose().Transpose();
        bool same = true;
        for (int i = 0; i < 16; ++i) if (std::fabs(tt.m[i] - t.m[i]) > 1e-5f) same = false;
        CHECK(same);
        // Transpose swaps off-diagonal: translate's row vs column.
        CHECK(std::fabs(t.Transpose().at(0, 3) - t.at(3, 0)) < 1e-5f);
    }

    // ---- Mathf angle helpers ----
    {
        CHECK(std::fabs(Mathf::MoveTowardsAngle(350, 10, 5) - 355.0f) < 1e-3f);  // shortest way up
        CHECK(std::fabs(Mathf::MoveTowardsAngle(10, 350, 5) - 5.0f) < 1e-3f);    // shortest way down
        float v = 0;
        float ang = 170;
        for (int i = 0; i < 600; ++i) ang = Mathf::SmoothDampAngle(ang, -170, v, 0.2f, 1.0f / 60.0f);
        // -170 and 190 are the same angle; converged within a wrap.
        CHECK(std::fabs(Mathf::DeltaAngle(ang, -170)) < 1.0f);
    }

    // ---- ForceMode: Acceleration & VelocityChange ignore mass; Force & Impulse don't ----
    {
        // Two bodies, different masses, same Acceleration → same velocity gain.
        Scene s("fm_accel");
        auto* a = s.CreateGameObject("A"); auto* ra = a->AddComponent<Rigidbody2D>();
        ra->gravityScale = 0; ra->mass = 1.0f;
        auto* b = s.CreateGameObject("B"); auto* rb = b->AddComponent<Rigidbody2D>();
        rb->gravityScale = 0; rb->mass = 10.0f;
        ra->AddForce({10, 0}, ForceMode::Acceleration);
        rb->AddForce({10, 0}, ForceMode::Acceleration);
        s.Start();
        s.Update(1.0f / 60.0f);
        CHECK(std::fabs(ra->velocity.x - rb->velocity.x) < 1e-4f);   // mass-independent

        // VelocityChange is instantaneous and mass-independent.
        Scene s2("fm_vc");
        auto* c = s2.CreateGameObject("C"); auto* rc = c->AddComponent<Rigidbody3D>();
        rc->gravityScale = 0; rc->mass = 5.0f;
        rc->AddForce({0, 4, 0}, ForceMode::VelocityChange);
        CHECK(std::fabs(rc->velocity.y - 4.0f) < 1e-5f);             // velocity set directly

        // Impulse is mass-dependent: same impulse, heavier body gains less.
        Scene s3("fm_imp");
        auto* d = s3.CreateGameObject("D"); auto* rd = d->AddComponent<Rigidbody3D>();
        rd->gravityScale = 0; rd->mass = 2.0f;
        rd->AddForce({8, 0, 0}, ForceMode::Impulse);
        CHECK(std::fabs(rd->velocity.x - 4.0f) < 1e-5f);             // 8 / mass(2)
    }

    // ---- Linecast (2D & 3D): hits a collider between two points ----
    {
        Scene s("linecast2d");
        auto* wall = s.CreateGameObject("Wall");
        wall->transform->localPosition = {5, 0, 0};
        auto* c = wall->AddComponent<BoxCollider2D>(); c->size = {1, 10};
        s.Start();
        Physics2D phys;
        RaycastHit2D hit = phys.Linecast(s, {0, 0}, {10, 0});
        CHECK(hit.hit && hit.gameObject == wall);
        CHECK(hit.distance > 4.0f && hit.distance < 5.0f);          // hit the near face
        RaycastHit2D miss = phys.Linecast(s, {0, 0}, {0, 20});      // away from the wall
        CHECK(!miss.hit);
    }
    {
        Scene s("linecast3d");
        auto* box = s.CreateGameObject("Box");
        box->transform->localPosition = {0, 0, 6};
        auto* c = box->AddComponent<BoxCollider3D>(); c->size = {2, 2, 2};
        s.Start();
        Physics3D phys;
        RaycastHit3D hit = phys.Linecast(s, {0, 0, 0}, {0, 0, 12});
        CHECK(hit.hit && hit.gameObject == box);
        CHECK(hit.distance > 4.0f && hit.distance < 5.5f);
    }

    TEST_MAIN_RESULT();
}
