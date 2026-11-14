// Core audit: regression tests for bugs found auditing the math types, ECS /
// NetWorld, Camera, Layers, DataAsset and input action bindings.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <filesystem>

using namespace okay;
using namespace okay::ecs;

namespace {
struct Pos { float x, y, z; };
struct Hp  { int hp; };

bool Fin(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool Fin(const Mat4& m) { for (float f : m.m) if (!std::isfinite(f)) return false; return true; }
bool SameRot(const Quat& a, const Quat& b) { return std::fabs(Quat::Dot(a, b)) > 0.9999f; }

void PutU32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back((std::uint8_t)((v >> (8 * i)) & 0xFF));
}
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

    // ---- ECS: a destroyed entity must not come back through add ----
    {
        World w;
        Entity a = w.create();
        w.add<Hp>(a, {5});
        w.destroy(a);
        CHECK(!w.alive(a));
        CHECK(!w.has<Hp>(a));
        w.add<Hp>(a, {9});                 // stale handle: must not leave a zombie
        int seen = 0; w.each<Hp>([&](Entity, Hp&) { ++seen; });
        CHECK(seen == 0);
        // The null entity never carries components or becomes alive.
        w.add<Hp>(Null, {1});
        CHECK(!w.has<Hp>(Null));
        CHECK(!w.alive(Null));
        CHECK(w.ensure(Null) == Null && !w.alive(Null));
        // A huge network-supplied id must not wrap the id counter back to Null.
        w.ensure(0xFFFFFFFFu);
        CHECK(w.create() != Null);
    }

    // ---- ECS: destroying the current entity while iterating visits everyone once ----
    {
        World w;
        std::vector<Entity> es;
        for (int i = 0; i < 6; ++i) { Entity e = w.create(); w.add<Hp>(e, {i}); es.push_back(e); }
        int visited = 0;
        w.each<Hp>([&](Entity e, Hp&) { ++visited; w.destroy(e); });
        CHECK(visited == 6);
        CHECK(w.count() == 0);
    }

    // ---- NetWorld::apply with a damaged / hostile snapshot ----
    {
        NetWorld nw;
        nw.replicate<Pos>(1);
        // Claims a million entities but carries two.
        std::vector<std::uint8_t> b;
        PutU32(b, 1000000u); PutU32(b, 1); PutU32(b, 2);
        b.push_back(0);
        nw.apply(b);
        CHECK(nw.world.count() <= 2);
        CHECK(!nw.world.alive(Null));

        // Component block claims 3 entries, holds one: no default-constructed ghosts.
        NetWorld n2;
        n2.replicate<Pos>(1);
        std::vector<std::uint8_t> c;
        PutU32(c, 1); PutU32(c, 7);        // one entity, id 7
        c.push_back(1); c.push_back(1);    // one type block, id 1
        PutU32(c, 3);                       // claims three components
        PutU32(c, 7);
        for (int i = 0; i < 3; ++i) PutU32(c, 0x3f800000u);   // 1.0f x3
        n2.apply(c);
        CHECK(!n2.world.alive(Null));
        CHECK(!n2.world.has<Pos>(Null));
        CHECK(n2.world.pool<Pos>().size() <= 1);
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
