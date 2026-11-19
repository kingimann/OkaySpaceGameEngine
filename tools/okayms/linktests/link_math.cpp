// Link-and-run test for the engine's header-only math (Mathf, Vec3, Quat, Mat4, Easing): the checks of tests/test_math.cpp
// and tests/test_easing.cpp (the parts that do not need Tween or the whole engine header), plus a few of
// tests/test_core_math_extra.cpp, with the small harness of mini.hpp. Built by okay-cl (/O0 /O1 /O2, with okay-ms's own
// <cmath>) and by MinGW g++ (its own <cmath>), run under Wine; the exit code and the printed numbers must be the same.
#include "mini.hpp"
#include "okay/Math/Easing.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"

using namespace okay;

static void ShowF(const char* label, float v) { std::printf("%s %lld\n", label, mini::Micro(v)); }

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
    ShowF("normalized.x", n.x);

    // Lerp clamps
    CHECK(Vec3::Lerp(Vec3::Zero, Vec3::One, 2.0f) == Vec3::One);

    // Quaternion rotates a vector
    Quat q = Quat::AngleAxis(90.0f, Vec3::Up);
    Vec3 r = q * Vec3::Right;  // +X rotated 90 degrees about +Y -> -Z
    CHECK_NEAR(r.x, 0.0f, 1e-4);
    CHECK_NEAR(r.y, 0.0f, 1e-4);
    CHECK_NEAR(r.z, -1.0f, 1e-4);
    ShowF("rotated.z", r.z);

    // A matrix point transform
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
    CHECK_NEAR(Mathf::Repeat(3.0f, 0.0f), 0.0f, 1e-6);
    CHECK_NEAR(Mathf::DeltaAngle(350.0f, 10.0f), 20.0f, 1e-4);
    CHECK_NEAR(Mathf::DeltaAngle(10.0f, 350.0f), -20.0f, 1e-4);
    {
        float v = 0.0f;
        float out = Mathf::SmoothDamp(5.0f, 5.0f, v, 0.3f, 0.0f);
        CHECK(std::isfinite(v));
        CHECK_NEAR(out, 5.0f, 1e-6);
    }

    // A rotation made from a matrix and its inverse give the point back.
    {
        Quat rq = Quat::AngleAxis(33.0f, Vec3{0, 1, 0});
        Mat4 m = Mat4::TRS(Vec3{1, 2, 3}, rq, Vec3{2, 2, 2});
        Vec3 world = m.MultiplyPoint(Vec3{1, 0, 0});
        Mat4 inv = m.Inverse();
        Vec3 back = inv.MultiplyPoint(world);
        CHECK_NEAR(back.x, 1.0f, 1e-4);
        CHECK_NEAR(back.y, 0.0f, 1e-4);
        CHECK_NEAR(back.z, 0.0f, 1e-4);
        ShowF("world.x", world.x);
        ShowF("world.z", world.z);
    }

    // Easing: every curve pins its endpoints (tests/test_easing.cpp), and a few shapes.
    Ease all[] = {Ease::Linear,  Ease::QuadIn,  Ease::QuadOut,   Ease::QuadInOut, Ease::CubicIn,   Ease::CubicOut,
                  Ease::CubicInOut, Ease::SineIn, Ease::SineOut, Ease::SineInOut, Ease::ExpoIn,    Ease::ExpoOut,
                  Ease::ExpoInOut,  Ease::BackIn, Ease::BackOut, Ease::BackInOut, Ease::ElasticOut, Ease::BounceOut};
    for (Ease e : all) {
        CHECK_NEAR(Easing::Evaluate(e, 0.0f), 0.0f, 0.01f);
        CHECK_NEAR(Easing::Evaluate(e, 1.0f), 1.0f, 0.01f);
    }
    CHECK_NEAR(Easing::Linear(0.5f), 0.5f, 0.0001f);
    CHECK(Easing::QuadIn(0.5f) < 0.5f);
    CHECK(Easing::QuadOut(0.5f) > 0.5f);
    CHECK_NEAR(Easing::QuadInOut(0.5f), 0.5f, 0.0001f);
    CHECK(Easing::BackIn(0.3f) < 0.0f);
    ShowF("bounce.0.3", Easing::Evaluate(Ease::BounceOut, 0.3f));
    ShowF("sine.0.5", Easing::Evaluate(Ease::SineInOut, 0.3f));
    TEST_MAIN_RESULT();
}
