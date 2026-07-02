#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

static bool VApprox(const Vec3& a, const Vec3& b, float eps = 1e-3f) {
    return std::fabs(a.x-b.x) < eps && std::fabs(a.y-b.y) < eps && std::fabs(a.z-b.z) < eps;
}

int main() {
    RUN_SUITE("math_extra");

    // ---- Vec3 ----
    CHECK(VApprox(Vec3::Reflect({1, -1, 0}, {0, 1, 0}), {1, 1, 0}));        // bounce off the floor
    CHECK(VApprox(Vec3::Project({3, 4, 0}, {1, 0, 0}), {3, 0, 0}));
    CHECK(VApprox(Vec3::ProjectOnPlane({3, 4, 0}, {0, 1, 0}), {3, 0, 0}));  // slide along the ground
    CHECK(VApprox(Vec3::ClampMagnitude({3, 4, 0}, 2.5f), {1.5f, 2.0f, 0})); // len 5 -> 2.5
    CHECK(std::fabs(Vec3::Angle({1, 0, 0}, {0, 1, 0}) - 90.0f) < 1e-2f);
    CHECK(std::fabs(Vec3::SignedAngle({1, 0, 0}, {0, 1, 0}, {0, 0, 1}) - 90.0f) < 1e-2f);
    CHECK(std::fabs(Vec3::SignedAngle({1, 0, 0}, {0, 1, 0}, {0, 0, -1}) + 90.0f) < 1e-2f);
    CHECK(VApprox(Vec3::Scale({2, 3, 4}, {5, 6, 7}), {10, 18, 28}));
    CHECK(VApprox(Vec3::Min({1, 5, 3}, {4, 2, 6}), {1, 2, 3}));
    CHECK(VApprox(Vec3::Max({1, 5, 3}, {4, 2, 6}), {4, 5, 6}));
    CHECK(VApprox(Vec3::Abs({-1, 2, -3}), {1, 2, 3}));
    { Vec3 m = Vec3::Slerp({1, 0, 0}, {0, 1, 0}, 0.5f);
      CHECK(std::fabs(m.Magnitude() - 1.0f) < 1e-2f);
      CHECK(std::fabs(m.x - m.y) < 1e-2f && m.x > 0.0f); }              // halfway on the arc

    // ---- Quat: ToEuler inverts Euler for single axes ----
    CHECK(VApprox(Quat::Euler(30, 0, 0).ToEuler(), {30, 0, 0}, 0.05f));
    CHECK(VApprox(Quat::Euler(0, 40, 0).ToEuler(), {0, 40, 0}, 0.05f));
    CHECK(VApprox(Quat::Euler(0, 0, 50).ToEuler(), {0, 0, 50}, 0.05f));

    // Combined: ToEuler -> rebuild -> rotates a vector the same way (order-agnostic check).
    {
        Quat q = Quat::Euler(20, 35, 10);
        Quat q2 = Quat::Euler(q.ToEuler());
        Vec3 v{0.3f, 0.5f, 0.8f};
        CHECK(VApprox(q * v, q2 * v, 1e-2f));
    }

    // FromToRotation turns one direction into another.
    {
        Quat q = Quat::FromToRotation({1, 0, 0}, {0, 1, 0});
        CHECK(VApprox(q * Vec3{1, 0, 0}, {0, 1, 0}, 1e-2f));
    }

    // Angle between rotations, and RotateTowards caps the step.
    {
        Quat a = Quat::Identity;
        Quat b = Quat::AngleAxis(90.0f, Vec3::Up);
        CHECK(std::fabs(Quat::Angle(a, b) - 90.0f) < 0.1f);
        Quat step = Quat::RotateTowards(a, b, 30.0f);
        CHECK(std::fabs(Quat::Angle(a, step) - 30.0f) < 0.1f);
    }

    // Inverse: q * q^-1 == identity.
    {
        Quat q = Quat::Euler(15, 60, -25);
        Quat r = q * q.Inverse();
        Vec3 v{1, 2, 3};
        CHECK(VApprox(r * v, v, 1e-2f));
    }

    TEST_MAIN_RESULT();
}
