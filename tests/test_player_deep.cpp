// Player deep audit: one check per bug the audit proved (docs/player_audit.md). Every `player_deep_*` check passes
// with the fix and FAILS with the old behaviour: run this program with OKAY_OLD_PLAYER=1 in the environment to see
// the old path fail them (the same switch brings the old per-frame work and body behaviour back in the Windows
// player and the editor). The step/slope/anything-else tests of the player stay in their own files.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace okay;
using Mode = PlayerController::ViewMode;

namespace {

struct Rig {
    std::unique_ptr<Scene> sc;
    GameObject* p = nullptr;
    Rigidbody3D* rb = nullptr;
    float dt = 1.0f / 60.0f;
    explicit Rig(bool fixed = true) {
        sc = std::make_unique<Scene>("deep");
        sc->physicsEnabled = true;
        if (fixed) sc->UseFixedStepPhysics(true);
    }
    GameObject* Box(Vec3 c, Vec3 size, Quat q = Quat::Identity) {
        GameObject* g = sc->CreateGameObject("Box");
        g->transform->localPosition = c; g->transform->localRotation = q;
        g->AddComponent<BoxCollider3D>()->size = size;
        return g;
    }
    void Spawn(Vec3 feet, Mode m = Mode::First) {
        p = sc->CreateGameObject("Player");
        p->transform->localPosition = feet;
        p->AddComponent<Character>()->Apply();
        HumanoidLibrary::EnsureAnimator(p);
        rb = p->AddComponent<Rigidbody3D>();
        auto* c = p->AddComponent<BoxCollider3D>(); c->size = {0.6f, 1.8f, 0.6f}; c->offset = {0, 0.9f, 0};
        PlayerController::Add(p, m, PlayerView::View::First);
        Templates::MountEyeCamera(*sc, p, "Main Camera");
        sc->Start();
    }
    void Frame(const std::vector<char>& k) { Input::FeedKeys(k); sc->Update(dt); }
    void Frames(double s, const std::vector<char>& k) { for (int i = 0, n = (int)std::lround(s / dt); i < n; ++i) Frame(k); }
    ~Rig() { Input::FeedKeys({}); }
};

bool Grounded(GameObject* p) { return p->GetComponent<FirstPersonController>()->IsGrounded(); }

} // namespace

int main() {
    RUN_SUITE("player_deep");
    const bool old = OldPlayerPath();
    std::printf("[player_deep] old player path %s\n", old ? "ON (checks below are expected to fail)" : "off");

    // ---- A floor whose pivot is not below the player's: the second jump must still work. (Ground was judged by
    // comparing the other object's pivot height with the player's, so such a floor refilled no jump.) ----
    {
        Rig r;
        GameObject* f = r.sc->CreateGameObject("Floor");
        f->transform->localPosition = {0, 5.0f, 0};
        auto* c = f->AddComponent<BoxCollider3D>(); c->size = {40, 1, 40}; c->offset = {0, -5.5f, 0};   // top face at y = 0
        r.Spawn({0, 0.02f, 0});
        r.Frames(0.8, {});
        int jumps = 0;
        for (int k = 0; k < 4; ++k) {
            r.Frame({' '}); r.Frame({' '});
            if (r.rb->velocity.y > 4.0f) ++jumps;
            r.Frames(1.4, {});
        }
        CHECK(jumps == 4);   // player_deep_pivot_floor_jump
    }

    // ---- A floor that vanishes under an idle (sleeping) player: it falls. ----
    for (int how = 0; how < 3; ++how) {
        Rig r;
        GameObject* f = r.Box({0, -0.5f, 0}, {20, 1, 20});
        r.Spawn({0, 0, 0});
        r.Frames(2.0, {});
        if (how == 0) r.sc->Destroy(f);
        else if (how == 1) f->GetComponent<Collider3D>()->enabled = false;
        else f->active = false;
        r.Frames(2.0, {});
        CHECK(r.p->transform->Position().y < -1.0f);   // player_deep_floor_gone_falls
    }

    // ---- Someone else's avatar is placed by the network only: a wall overlapping it must not move it. ----
    {
        Rig r;
        r.sc->CreateGameObject("Net")->AddComponent<NetworkManager>();
        GameObject* rem = Templates::BuildPlayerBody(*r.sc, {20, 0, 0}, "Remote");
        PlayerController::Add(rem, Mode::First, PlayerView::View::First);
        auto* ns = rem->AddComponent<NetworkSync>(); ns->authority = NetworkSync::Authority::Manual; ns->owned = false;
        rem->GetComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Kinematic;   // only the controller could move it
        r.Box({0, -0.5f, 0}, {80, 1, 80});
        r.Box({20.2f, 2, 0}, {0.4f, 4, 4});
        r.Spawn({0, 0, 0});
        r.Frames(2.0, {});
        CHECK_NEAR(rem->transform->Position().x, 20.0f, 0.05f);   // player_deep_remote_not_eased
    }

    // ---- Walking down stairs: the body stays on the treads instead of hopping from one to the next. ----
    {
        Rig r;
        const int steps = 12; const float rise = 0.18f, tread = 0.30f;
        for (int i = 0; i < steps; ++i) r.Box({0, (i + 1) * rise * 0.5f, -2.0f - (i + 0.5f) * tread}, {3.0f, (i + 1) * rise, tread});
        r.Box({0, steps * rise - 0.5f, -2.0f - steps * tread - 4.0f}, {10, 1, 8});
        r.Box({0, -0.5f, 0}, {40, 1, 40});
        r.Spawn({0, steps * rise, -2.0f - steps * tread - 1.5f});
        r.p->GetComponent<FirstPersonController>()->yaw = 180.0f;
        r.Frames(0.6, {});
        int air = 0, n = 0;
        for (int i = 0; i < 180 && r.p->transform->Position().y > 0.08f; ++i) { r.Frame({'w'}); ++n; if (!Grounded(r.p)) ++air; }
        CHECK(n > 40);
        CHECK(air * 10 < n);   // player_deep_stairs_down_grounded: under 10% of frames in the air (was about 49%)
    }

    // ---- Jumping off a moving platform keeps its speed. ----
    {
        Rig r;
        GameObject* plat = r.Box({0, -0.25f, 0}, {40, 0.5f, 8});
        auto* b = plat->AddComponent<Rigidbody3D>(); b->bodyType = Rigidbody3D::BodyType::Kinematic; b->gravityScale = 0; b->velocity = {3, 0, 0};
        r.Spawn({-10, 0, 0});
        r.Frames(1.0, {});
        r.Frame({' '}); r.Frame({' '});
        CHECK(r.rb->velocity.x > 2.4f);   // player_deep_platform_momentum (was 0)
    }

    // ---- Sliding along a wall is not slowed to a crawl by contact friction. ----
    {
        Rig r;
        const Quat q = Quat::AngleAxis(30.0f, Vec3::Up);
        const Vec3 n = q * Vec3{0, 0, 1}, t = q * Vec3{1, 0, 0};
        r.Box(Vec3{0, 0, -3.0f} - n * 0.25f + Vec3{0, 2, 0}, {30, 4, 0.5f}, q);
        r.Box({0, -0.5f, 0}, {80, 1, 80});
        r.Spawn({0, 0, 0});
        r.Frames(1.2, {'w', Input::KeyShift});
        const Vec3 a = r.p->transform->Position();
        r.Frames(1.5, {'w', Input::KeyShift});
        const Vec3 d = r.p->transform->Position() - a;
        const double speed = (d.x * t.x + d.z * t.z) / 1.5;
        CHECK(speed > 0.7 * 5.5 * 0.5);   // player_deep_wall_slide: 70% of the frictionless 2.75 m/s while running (was 29%); walking is 2.0 m/s since the natural gait landed on main
    }

    // ---- Standing still on a slope the player may stand on: no creeping. ----
    for (int kind = 0; kind < 2; ++kind) {
        Rig r;
        const float deg = kind == 0 ? 25.0f : 40.0f;
        const float th = deg * 0.0174532925f;
        if (kind == 0) {
            GameObject* g = r.sc->CreateGameObject("Terrain");
            auto* t = g->AddComponent<Terrain>(); t->resolution = 100; t->Resize(100); t->size = 100.0f;
            for (int iz = 0; iz <= 100; ++iz) for (int ix = 0; ix <= 100; ++ix) t->SetHeight(ix, iz, -(iz * t->CellSize() - 50.0f) * std::tan(th));
            r.Spawn({0, 0, 0});
        } else {
            r.Box(Vec3{0, -0.25f * std::cos(th), -0.25f * std::sin(th)}, {40, 0.5f, 120}, Quat::AngleAxis(deg, Vec3::Right));
            r.Spawn({0, 0.3f * std::tan(th) + 0.05f, 0});
        }
        r.Frames(0.8, {});
        const Vec3 a = r.p->transform->Position();
        r.Frames(3.0, {});
        const Vec3 b = r.p->transform->Position();
        const float drift = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z));
        CHECK(drift < 0.1f);   // player_deep_slope_hold (was 0.5 to 0.8 m in 3 s)
    }

    // ---- The same answers with and without the early-outs: TryStepUp and EaseOutOfSolids over random scenes. ----
    {
        Rig r(false);
        unsigned s = 4242;
        auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (float)((s >> 8) & 0xFFFF) / 65535.0f; };
        for (int i = 0; i < 60; ++i) {
            const float h = 0.04f + rnd() * 0.5f;
            r.Box({-6 + rnd() * 12, h * 0.5f, -6 + rnd() * 12}, {0.5f + rnd() * 3, h, 0.5f + rnd() * 3}, Quat::AngleAxis(rnd() * 360, Vec3::Up));
        }
        r.Box({0, -0.5f, 0}, {40, 1, 40});
        r.Spawn({20, 0, 20});
        r.sc->Update(0.0f);
        int stepped = 0, eased = 0;
        for (int i = 0; i < 400; ++i) {
            const Vec3 at{-6 + rnd() * 12, 0, -6 + rnd() * 12};
            const float ang = rnd() * 6.2831853f;
            const Vec3 dir{std::sin(ang), 0, std::cos(ang)};
            Vec3 res[2], res2[2]; Vec3 vel[2];
            for (int pass = 0; pass < 2; ++pass) {
                SetOldPlayerPath(pass == 0);
                r.p->transform->SetPosition(at); r.rb->velocity = {0, -0.3f, 0};
                TryStepUp(*r.sc, r.p, r.rb, dir, true, true, 0.35f);
                res[pass] = r.p->transform->Position(); vel[pass] = r.rb->velocity;
                r.p->transform->SetPosition(at);
                SeparationMemory mem;
                EaseOutOfSolids(r.p, 1.0f / 60.0f, mem, dir);
                res2[pass] = r.p->transform->Position();
            }
            SetOldPlayerPath(old);
            CHECK(res[0].x == res[1].x && res[0].y == res[1].y && res[0].z == res[1].z && vel[0].y == vel[1].y);
            CHECK(res2[0].x == res2[1].x && res2[0].y == res2[1].y && res2[0].z == res2[1].z);
            if (res[0].y != 0.0f) ++stepped;
            if (res2[0].x != at.x || res2[0].z != at.z) ++eased;
        }
        CHECK(stepped > 5);   // the random scene really exercises both paths
        CHECK(eased > 5);
    }

    // ---- The player lines name the slope and the ground. ----
    {
        Rig r;
        const float th = 30.0f * 0.0174532925f;
        r.Box(Vec3{0, -0.25f * std::cos(th), -0.25f * std::sin(th)}, {40, 0.5f, 120}, Quat::AngleAxis(30.0f, Vec3::Right));
        r.Spawn({0, 0.3f * std::tan(th) + 0.05f, 0});
        r.Frames(1.0, {});
        const std::vector<std::string> lines = PlayerDebug::Lines(*r.sc);
        CHECK(!lines.empty());
        bool slope = false;
        for (const auto& l : lines) if (l.find("slope 30") != std::string::npos) slope = true;
        CHECK(slope);   // player_deep_debug_lines
    }

    // ---- Slope limit, step height and fall reset were never saved with the scene: now they are. ----
    for (int view = 0; view < 3; ++view) {
        Scene a("save");
        GameObject* p = a.CreateGameObject("P");
        auto* pc = PlayerController::Add(p, view == 0 ? Mode::First : view == 1 ? Mode::Third : Mode::Both);
        if (pc->first()) { pc->first()->slopeLimit = 33.0f; pc->first()->stepOffset = 0.2f; pc->first()->fallResetY = -50.0f; pc->first()->swimSpeed = 3.5f; }
        if (pc->third()) { pc->third()->slopeLimit = 41.0f; pc->third()->stepOffset = 0.15f; pc->third()->fallResetY = -60.0f; }
        const std::string text = SceneSerializer::Serialize(a);
        Scene b("load");
        CHECK(SceneSerializer::Deserialize(b, text));
        auto* q = PlayerController::Of(b.Find("P"));
        CHECK(q != nullptr);
        if (q && q->first()) { CHECK_NEAR(q->first()->slopeLimit, 33.0f, 1e-4f); CHECK_NEAR(q->first()->stepOffset, 0.2f, 1e-4f); CHECK_NEAR(q->first()->fallResetY, -50.0f, 1e-4f); CHECK_NEAR(q->first()->swimSpeed, 3.5f, 1e-4f); }
        if (q && q->third()) { CHECK_NEAR(q->third()->slopeLimit, 41.0f, 1e-4f); CHECK_NEAR(q->third()->stepOffset, 0.15f, 1e-4f); CHECK_NEAR(q->third()->fallResetY, -60.0f, 1e-4f); }
        // player_deep_settings_saved
    }

    // ---- Riding a kinematic platform UP: no wobble, no lost ground (it was lifted twice, so it hopped). ----
    for (bool up : {true, false}) {
        Rig r;
        GameObject* plat = r.Box({0, up ? -0.25f : 3.75f, 0}, {8, 0.5f, 8});
        auto* b = plat->AddComponent<Rigidbody3D>(); b->bodyType = Rigidbody3D::BodyType::Kinematic; b->gravityScale = 0; b->velocity = {0, up ? 2.0f : -2.0f, 0};
        r.Spawn({0, up ? 0.0f : 4.0f, 0});
        r.Frames(0.5, {});
        int air = 0, n = 0; float lo = 9, hi = -9;
        for (int i = 0; i < 120; ++i) {
            r.Frame({});
            const float rel = r.p->transform->Position().y - (plat->transform->Position().y + 0.25f);
            lo = std::min(lo, rel); hi = std::max(hi, rel); ++n; if (!Grounded(r.p)) ++air;
        }
        CHECK(air * 20 < n);            // player_deep_platform_up_grounded
        std::printf("[player_deep] platform %s: rider height range %.4f, air %d of %d\n", up ? "up" : "down", hi - lo, air, n);
        CHECK(hi - lo < 0.03f);
    }

    // ---- Swimming into a bank climbs out onto it. ----
    for (int bank = 0; bank < 2; ++bank) {
        Rig r;
        r.sc->physicsEnabled = true;
        GameObject* w = r.sc->CreateGameObject("Water");
        auto* wa = w->AddComponent<Water>(); wa->size = 20.0f; wa->depth = 4.0f; wa->waveHeight = 0.0f; wa->resolution = 8;
        r.Box({0, -4.5f, 0}, {20, 1, 20});
        r.Box({20.0f, -2.0f, 0}, {20, bank == 0 ? 4.6f : 3.8f, 20});   // top 0.3 m above the water, or 0.1 m under it
        r.Spawn({0, 1.5f, 0});
        r.p->GetComponent<FirstPersonController>()->yaw = -90.0f;
        r.Frames(2.5, {});
        r.Frames(6.0, {'w'});
        CHECK(r.p->transform->Position().x > 10.5f);     // player_deep_water_climb_out
        CHECK(r.p->transform->Position().y > -0.4f);
    }

    // ---- Variable jump height (off by default). ----
    for (int on = 0; on < 2; ++on) {
        float apex[2];
        for (int held = 0; held < 2; ++held) {
            Rig r;
            r.Box({0, -0.5f, 0}, {40, 1, 40});
            r.Spawn({0, 0, 0});
            r.p->GetComponent<FirstPersonController>()->variableJump = on != 0;
            r.Frames(0.6, {});
            float y = 0;
            for (int i = 0; i < 90; ++i) { r.Frame(held && i < 40 ? std::vector<char>{' '} : std::vector<char>{i == 0 ? ' ' : 0}); y = std::max(y, r.p->transform->Position().y); }
            apex[held] = y;
        }
        if (on) CHECK(apex[0] < 0.6f * apex[1]);          // player_deep_variable_jump: a tap is a short hop
        else CHECK(std::fabs(apex[0] - apex[1]) < 0.05f); // off: the same jump
    }

    // ---- Coyote time and jump buffer (already on by default): jump just after a ledge, press just before landing. ----
    {
        Rig r;
        r.Box({-10, -0.5f, 0}, {20, 1, 20});   // floor for x < 0
        r.Spawn({-1.0f, 0, 0});
        r.p->GetComponent<FirstPersonController>()->yaw = -90.0f;
        r.Frames(0.5, {});
        for (int i = 0; i < 200 && r.p->transform->Position().x < 0.45f; ++i) r.Frame({'w'});   // walk until the centre is past the edge
        for (int i = 0; i < 4; ++i) r.Frame({'w'});
        r.Frame({'w', ' '});
        CHECK(r.rb->velocity.y > 4.0f);                    // player_deep_coyote_jump
    }

    // ---- A ladder: a box trigger tagged Climbable. ----
    {
        Rig r;
        r.Box({0, -0.5f, 0}, {40, 1, 40});
        GameObject* ladder = r.sc->CreateGameObject("Ladder");
        ladder->tag = "Climbable";
        ladder->transform->localPosition = {0, 3.0f, -0.7f};
        auto* t = ladder->AddComponent<BoxCollider3D>(); t->size = {1, 6, 1}; t->isTrigger = true;
        r.Spawn({0, 0, 0});
        r.Frames(0.5, {});
        r.Frames(2.0, {'w'});
        const float up = r.p->transform->Position().y;
        CHECK(up > 2.5f);                                  // player_deep_ladder_climb
        r.Frames(1.0, {});
        CHECK(std::fabs(r.p->transform->Position().y - up) < 0.15f);   // holds on the ladder
        r.Frame({' '}); r.Frames(0.5, {});                  // jump off
        CHECK(r.p->transform->Position().y > up - 0.3f);
    }

    // ---- A hard landing is an event, not damage. ----
    for (float h : {2.0f, 15.0f}) {
        Rig r;
        r.Box({0, -0.5f, 0}, {40, 1, 40});
        r.Spawn({0, h, 0});
        int hits = 0; float impact = 0;
        auto* fp = r.p->GetComponent<FirstPersonController>();
        fp->onHardLand = [&](float v) { ++hits; impact = v; };
        int msgs = 0; (void)msgs;
        r.Frames(3.0, {});
        if (h > 10.0f) { CHECK(hits == 1); CHECK(impact > 12.0f); }   // player_deep_hard_land
        else CHECK(hits == 0);
    }

    // ---- Buried in the ground (spawned or teleported in): pushed out onto open ground, not out of the world. ----
    for (int kind = 0; kind < 3; ++kind) {
        Rig r;
        r.Box({0, -3.0f, 0}, {80, 6, 80});                 // a deep floor: the body is inside it, not under it
        if (kind == 2) r.Box({0, 2, 0}, {10, 4, 6.0f});
        r.Spawn({0, kind == 0 ? -1.5f : kind == 1 ? -3.0f : 0.0f, 0});
        r.Frames(2.5, {});
        CHECK(r.p->transform->Position().y > -0.5f);       // player_deep_buried_recovery
        CHECK(r.p->transform->Position().y < 1.0f);
    }

    // ---- The Scene-view gizmo knows the slope limit and the step height. ----
    {
        Rig r;
        r.Spawn({0, 0, 0});
        auto* fp = r.p->GetComponent<FirstPersonController>();
        fp->slopeLimit = 35.0f; fp->stepOffset = 0.3f;
        int spokes = 0, step = 0;
        for (const auto& sg : PlayerGizmo::Lines(r.p)) { if (sg.kind == 1) ++spokes; if (sg.kind == 2) ++step; }
        CHECK(spokes == 4); CHECK(step == 24);              // player_deep_gizmo
    }

    TEST_MAIN_RESULT();
}
