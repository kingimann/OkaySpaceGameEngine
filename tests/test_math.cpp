#include "test_framework.hpp"
#include "okay/Math/Math.hpp"

using namespace okay;

int main() {
    RUN_SUITE("math");

    // Vec3 arithmetic
    Vec3 a{1, 2, 3}, b{4, 5, 6};
    CHECK((a + b) == Vec3(5, 7, 9));
    CHECK((b - a) == Vec3(3, 3, 3));
    CHECK((a * 2.0f) == Vec3(2, 4, 6));
    CHECK_NEAR(Vec3::Dot(a, b), 32.0f, 1e-4);
    CHECK(Vec3::Cross(Vec3::Right, Vec3::Up) == Vec3::Forward);

    // Normalization
    Vec3 n = Vec3{3, 4, 0}.Normalized();
    CHECK_NEAR(n.Magnitude(), 1.0f, 1e-5);

    // Lerp clamps
    CHECK(Vec3::Lerp(Vec3::Zero, Vec3::One, 2.0f) == Vec3::One);

    // Quaternion rotates a vector
    Quat q = Quat::AngleAxis(90.0f, Vec3::Up);
    Vec3 r = q * Vec3::Right;       // +X rotated 90° about +Y -> -Z
    CHECK_NEAR(r.x, 0.0f, 1e-4);
    CHECK_NEAR(r.y, 0.0f, 1e-4);
    CHECK_NEAR(r.z, -1.0f, 1e-4);

    // Euler round-trip through a matrix point transform
    Mat4 t = Mat4::TRS(Vec3{10, 0, 0}, Quat::Identity, Vec3::One);
    Vec3 p = t.MultiplyPoint(Vec3{1, 2, 3});
    CHECK(p == Vec3(11, 2, 3));

    // Scale then translate
    Mat4 s = Mat4::TRS(Vec3{0, 5, 0}, Quat::Identity, Vec3{2, 2, 2});
    CHECK(s.MultiplyPoint(Vec3{1, 1, 1}) == Vec3(2, 7, 2));

    // Mathf helpers
    CHECK_NEAR(Mathf::Clamp(15.0f, 0.0f, 10.0f), 10.0f, 1e-6);
    CHECK_NEAR(Mathf::Lerp(0.0f, 10.0f, 0.5f), 5.0f, 1e-6);
    CHECK(Mathf::Approximately(0.1f + 0.2f, 0.3f));

    TEST_MAIN_RESULT();
}
