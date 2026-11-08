// Core audit: edge cases in Mathf, Quat and Color that gave NaN or wrong results.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <iostream>

using namespace okay;

int main() {
    RUN_SUITE("core_audit");

    // SmoothDamp with no time passing (a paused game) must not poison the velocity.
    {
        float v = 0.0f;
        float out = Mathf::SmoothDamp(5.0f, 5.0f, v, 0.3f, 0.0f);
        CHECK(std::isfinite(v));
        CHECK_NEAR(out, 5.0f, 1e-6);
        v = 2.0f;
        out = Mathf::SmoothDamp(1.0f, 9.0f, v, 0.3f, 0.0f);
        CHECK(std::isfinite(v) && std::isfinite(out));
        CHECK_NEAR(out, 1.0f, 1e-6);   // no time, no movement
        CHECK_NEAR(v, 2.0f, 1e-6);
        float va = 0.0f;
        const float oa = Mathf::SmoothDampAngle(10.0f, 10.0f, va, 0.3f, 0.0f);
        CHECK(std::isfinite(oa) && std::isfinite(va));
    }
    // Repeat / PingPong with a zero length.
    {
        CHECK_NEAR(Mathf::Repeat(3.0f, 0.0f), 0.0f, 1e-6);
        CHECK(std::isfinite(Mathf::PingPong(3.0f, 0.0f)));
        CHECK_NEAR(Mathf::DeltaAngle(350.0f, 10.0f), 20.0f, 1e-4);
        CHECK_NEAR(Mathf::DeltaAngle(10.0f, 350.0f), -20.0f, 1e-4);
    }
    // Quat edge cases.
    {
        // Looking along the up axis (forward parallel to up).
        Quat q = Quat::LookRotation(Vec3::Up, Vec3::Up);
        CHECK(std::isfinite(q.x + q.y + q.z + q.w));
        CHECK_NEAR(q.Magnitude(), 1.0f, 1e-4);
        Vec3 f = q * Vec3::Forward;
        CHECK_NEAR(f.y, 1.0f, 1e-3);
        Quat q2 = Quat::LookRotation(Vec3::Zero);
        CHECK_NEAR(q2.Magnitude(), 1.0f, 1e-4);
        // Slerp between opposite signs of one rotation stays that rotation.
        Quat a = Quat::AngleAxis(40.0f, Vec3::Up);
        Quat b{-a.x, -a.y, -a.z, -a.w};
        Quat s = Quat::Slerp(a, b, 0.5f);
        CHECK_NEAR(Quat::Angle(s, a), 0.0f, 1e-2);
        CHECK_NEAR(s.Magnitude(), 1.0f, 1e-4);
        // Slerp result stays unit length across the range.
        Quat c = Quat::AngleAxis(170.0f, Vec3::Right);
        for (float t = 0.0f; t <= 1.0f; t += 0.125f)
            CHECK_NEAR(Quat::Slerp(a, c, t).Magnitude(), 1.0f, 1e-4);
        // NaN input does not escape as a non-unit "rotation" from Normalized.
        Quat n = Quat(NAN, 0, 0, 1).Normalized();
        CHECK(std::isfinite(n.x + n.y + n.z + n.w));
        CHECK_NEAR(Quat::RotateTowards(a, a, 5.0f).w, a.w, 1e-5);
    }
    // Color parsing.
    {
        bool ok = false;
        Color c = Color::Parse("#FF8000", &ok);
        CHECK(ok);
        CHECK_NEAR(c.r, 1.0f, 1e-5); CHECK_NEAR(c.g, 128.0f / 255.0f, 1e-5); CHECK_NEAR(c.b, 0.0f, 1e-5); CHECK_NEAR(c.a, 1.0f, 1e-5);
        c = Color::Parse("f80", &ok);                     // short form, no '#'
        CHECK(ok); CHECK_NEAR(c.g, 136.0f / 255.0f, 1e-5);
        c = Color::Parse("#11223344", &ok);
        CHECK(ok); CHECK_NEAR(c.a, 68.0f / 255.0f, 1e-5);
        c = Color::Parse("#fa8", &ok);
        CHECK(ok); CHECK_NEAR(c.r, 1.0f, 1e-5);
        c = Color::Parse("300, -5, 128", &ok);            // bytes clamp, they do not wrap
        CHECK(ok); CHECK_NEAR(c.r, 1.0f, 1e-5); CHECK_NEAR(c.g, 0.0f, 1e-5); CHECK_NEAR(c.a, 1.0f, 1e-5);
        Color keep(0.1f, 0.2f, 0.3f, 0.4f);
        c = Color::Parse("#12G456", &ok, keep);           // bad digit: the fallback
        CHECK(!ok); CHECK_NEAR(c.g, 0.2f, 1e-6);
        c = Color::Parse("", &ok, keep);
        CHECK(!ok);
        c = Color::Parse("#12345", &ok, keep);            // wrong length
        CHECK(!ok);
        c = Color::Parse("  #FFF  ", &ok);
        CHECK(ok);
        CHECK_NEAR(c.b, 1.0f, 1e-6);
    }
    TEST_MAIN_RESULT();
}
