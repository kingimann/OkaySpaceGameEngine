// Core audit: regression tests for bugs found auditing the math types,
// Camera, Layers, DataAsset and input action bindings.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <filesystem>

using namespace okay;

namespace {
bool Fin(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool Fin(const Mat4& m) { for (float f : m.m) if (!std::isfinite(f)) return false; return true; }
bool SameRot(const Quat& a, const Quat& b) { return std::fabs(Quat::Dot(a, b)) > 0.9999f; }
} // namespace

int main() {
    RUN_SUITE("core_audit");

    // ---- Quat::LookRotation when forward is parallel to up ----
    {
        // Looking straight up / down with the default up vector used to collapse
        // the basis (right = 0) and return a 180 degree roll instead of a look.
        Quat up = Quat::LookRotation(Vec3::Up);
        CHECK(Fin(up * Vec3::Forward));
        CHECK((up * Vec3::Forward) == Vec3::Up);
        Quat down = Quat::LookRotation(Vec3::Down);
        CHECK((down * Vec3::Forward) == Vec3::Down);
        // A zero forward must not produce a bogus rotation.
        CHECK(SameRot(Quat::LookRotation(Vec3::Zero), Quat::Identity));
        // Normal case still right.
        Quat r = Quat::LookRotation(Vec3{1, 0, 0});
        CHECK((r * Vec3::Forward) == Vec3::Right);
    }

    // ---- Quat Euler round trip, including looking straight up / down ----
    {
        const float angles[][3] = {{30, 40, 50}, {90, 0, 0}, {-90, 0, 0}, {90, 30, 0}, {89.99f, 20, 10},
                                   {0, 180, 0}, {45, -170, 20}};
        for (const auto& a : angles) {
            Quat q = Quat::Euler(a[0], a[1], a[2]);
            Vec3 e = q.ToEuler();
            CHECK(Fin(e));
            CHECK(SameRot(Quat::Euler(e), q));
        }
    }

    // ---- Quat::Slerp keeps unit length ----
    {
        Quat a = Quat::Euler(0, 10, 0), b = Quat::Euler(0, 170, 0);
        for (float t : {0.1f, 0.5f, 0.9f}) CHECK_NEAR(Quat::Slerp(a, b, t).Magnitude(), 1.0, 1e-4);
        CHECK(SameRot(Quat::Slerp(a, b, 0.0f), a));
        CHECK(SameRot(Quat::Slerp(a, b, 1.0f), b));
    }

    // ---- Mathf edge cases that must not produce NaN ----
    {
        float v = 0.0f;
        float out = Mathf::SmoothDamp(5.0f, 5.0f, v, 0.3f, 0.0f);   // paused frame: dt = 0
        CHECK(std::isfinite(out) && std::isfinite(v));
        float v2 = 0.0f;
        out = Mathf::SmoothDamp(1.0f, 5.0f, v2, 0.3f, 0.0f);
        CHECK(std::isfinite(out) && std::isfinite(v2));
        CHECK(std::isfinite(Mathf::Repeat(3.0f, 0.0f)));
        CHECK(std::isfinite(Mathf::PingPong(3.0f, 0.0f)));
        CHECK(std::isfinite(Mathf::LerpAngle(10.0f, 350.0f, 0.5f)));
        Vec3 vel{};
        Vec3 sd = Vec3::SmoothDamp({1, 1, 1}, {1, 1, 1}, vel, 0.3f, 0.0f);
        CHECK(Fin(sd) && Fin(vel));
        Vec2 vel2{};
        Vec2 sd2 = Vec2::SmoothDamp({1, 1}, {1, 1}, vel2, 0.3f, 0.0f);
        CHECK(std::isfinite(sd2.x) && std::isfinite(vel2.x));
    }

    // ---- Vec3::Slerp of opposite vectors ----
    {
        Vec3 m = Vec3::Slerp({1, 0, 0}, {-1, 0, 0}, 0.5f);
        CHECK(Fin(m));
        CHECK_NEAR(m.Magnitude(), 1.0, 1e-3);
    }

    // ---- Easing endpoints ----
    {
        for (int i = 0; i < (int)Ease::Count; ++i) {
            CHECK_NEAR(Easing::Evaluate((Ease)i, 0.0f), 0.0, 1e-5);
            CHECK_NEAR(Easing::Evaluate((Ease)i, 1.0f), 1.0, 1e-5);
        }
    }

    // ---- Camera: degenerate viewport sizes and clip planes ----
    {
        Scene scene("cam");
        GameObject* go = scene.CreateGameObject("Cam");
        go->transform->localPosition = {0, 0, -10};
        Camera* cam = go->AddComponent<Camera>();
        for (auto proj : {Camera::Projection::Perspective, Camera::Projection::Orthographic}) {
            cam->projection = proj;
            CHECK(Fin(cam->ProjectionMatrix(0.0f)));
            CHECK(Fin(cam->ProjectionMatrix(-1.0f)));
            CHECK(Fin(cam->ProjectionMatrix(std::nanf(""))));
            Vec2 px;
            // A minimised window: width or height 0.
            bool ok = cam->WorldToScreen({0, 0, 0}, 0.0f, 600.0f, px);
            CHECK(!ok || (std::isfinite(px.x) && std::isfinite(px.y)));
            Vec3 o, d;
            cam->ScreenPointToRay(0.0f, 0.0f, 0.0f, 600.0f, o, d);
            CHECK(Fin(o) && Fin(d));
            cam->ScreenPointToRay(10.0f, 10.0f, 800.0f, 0.0f, o, d);
            CHECK(Fin(o) && Fin(d));
        }
        cam->projection = Camera::Projection::Perspective;
        cam->nearClip = 1.0f; cam->farClip = 1.0f;     // inspector typo: far == near
        CHECK(Fin(cam->ProjectionMatrix(1.5f)));
        cam->nearClip = -2.0f; cam->farClip = 100.0f;
        CHECK(Fin(cam->ProjectionMatrix(1.5f)));
        Mat4 p = cam->ProjectionMatrix(1.5f);
        CHECK(p.at(2, 3) == -1.0f && p.at(3, 2) < 0.0f);   // still a sane perspective (near > 0)
    }

    // ---- Layers::MaskFromList names are not case sensitive ----
    {
        CHECK(Layers::MaskFromList("default") == 1u);
        CHECK(Layers::MaskFromList("Default") == 1u);
        CHECK(Layers::MaskFromList("water, ui") == ((1u << 4) | (1u << 5)));
        CHECK(Layers::MaskFromList("!default") == ~1u);
    }

    // ---- DataAsset: a failed Load keeps what was there; base dir joins with '/' ----
    {
        namespace fs = std::filesystem;
        fs::path dir = fs::temp_directory_path() / "okay_core_audit_da";
        fs::create_directories(dir);
        {
            std::ofstream f(dir / "item.okaydata");
            f << "name = sword\ndamage = 7\n";
        }
        DataAsset a;
        a.Set("keep", "me");
        CHECK(!a.Load((dir / "missing.okaydata").string()));
        CHECK(a.GetString("keep") == "me");

        DataAsset::SetBaseDir(dir.string());               // no trailing separator
        DataAsset b;
        CHECK(b.Load("item.okaydata"));
        CHECK(b.GetString("name") == "sword");
        CHECK_NEAR(b.GetNumber("damage"), 7.0, 1e-9);
        DataAsset::SetBaseDir("");
        fs::remove_all(dir);
    }

    // ---- Input bindings: keys ';' ',' '%' survive saving (the list is ';' / ',' separated) ----
    {
        Prefs::Clear();
        InputActions::Restore();
        for (const char* k : {"key:;", "key:,", "key:%"}) {
            Prefs::Clear();
            InputActions::Restore();
            CHECK(InputActions::Rebind("Jump", k));
            InputBinding before = *InputActions::Current("Jump", false);
            InputActions::Restore();           // forget memory, keep Prefs
            InputActions::Load();
            const InputBinding* after = InputActions::Current("Jump", false);
            CHECK(after != nullptr);
            if (after) {
                CHECK(after->kind == InputBinding::Kind::Key);
                CHECK(after->code == before.code);
            }
            // pad binding must still be there too
            const InputBinding* pad = InputActions::Current("Jump", true);
            CHECK(pad && pad->code == Input::PadA);
        }
        // A four-key direction with a comma key.
        Prefs::Clear();
        InputActions::Restore();
        CHECK(InputActions::Rebind("Move", "key:,", 0));
        InputActions::Restore();
        InputActions::Load();
        const InputBinding* mv = InputActions::Current("Move", false);
        CHECK(mv && mv->kind == InputBinding::Kind::Keys4 && mv->keys[0] == ',' && mv->keys[1] == 's');
        Prefs::Clear();
        InputActions::Restore();
    }

    TEST_MAIN_RESULT();
}
