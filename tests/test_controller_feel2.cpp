// Character movement checks that had no test (claude/controller-1): diagonal speed, coyote time, jump buffering,
// slopes, steps, and the first-person look turn at 30 / 60 / 144 fps. The real controller, fake keys, headless.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "player_test_rig.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace okay;
using Mode = PlayerController::ViewMode;

namespace {
// The shared player rig, with this test's scene name, floor name and an optional frame rate.
struct Rig : okay_test::PlayerTestRig {
    explicit Rig(bool floor = true, float fps = 60.0f) : PlayerTestRig("feel2", floor, fps, "Box") {}
};
using okay_test::HSpeed;
}  // namespace

// Animator-type components on a player: names and layers.
static int CountAnim(GameObject* p, const char* what) {
    int n = 0;
    for (const auto& c : p->Components()) {
        if (!c) continue;
        const std::string name = typeid(*c).name();
        const bool anim = name.find("Anim") != std::string::npos;
        if (!anim) continue;
        ++n;
        int layer = -1;
        if (auto* sm = dynamic_cast<AnimStateMachine*>(c.get())) layer = sm->layer;
        std::printf("    %s: animator-type component %s layer %d\n", what, name.c_str(), layer);
    }
    return n;
}

int main() {
    RUN_SUITE("controller feel 2");

    // ---- one animator on a player, however it is built ----
    {
        Scene a("a"); GameObject* p1 = Templates::AddFirstPersonPlayer(a); a.Start();
        for (int i = 0; i < 30; ++i) a.Update(1.0f / 60.0f);
        Scene b("b"); GameObject* p2 = Templates::AddThirdPersonPlayer(b); b.Start();
        for (int i = 0; i < 30; ++i) b.Update(1.0f / 60.0f);
        Scene c("c"); GameObject* p3 = Templates::BuildPlayerBody(c, {0, 1, 0}, "Player");
        PlayerController::Add(p3, Mode::Both); c.Start();
        for (int i = 0; i < 30; ++i) c.Update(1.0f / 60.0f);
        const int n1 = CountAnim(p1, "first-person template"), n2 = CountAnim(p2, "third-person template"), n3 = CountAnim(p3, "Player Controller");
        // Two machines exist on purpose, and they are different things: the walk graph (layer 0, exactly one) and an
        // upper-body "hit reaction" layer (Torso mask, weight 0 until a hit). Not two walk animators.
        for (GameObject* p : {p1, p2, p3}) {
            int base = 0, hit = 0, other = 0;
            for (AnimStateMachine* sm : p->GetComponents<AnimStateMachine>()) {
                if (sm->layer == 0) ++base; else if (sm->maskBone == "Torso") ++hit; else ++other;
            }
            CHECK(base == 1); CHECK(hit <= 1); CHECK(other == 0);
            CHECK(p->GetComponents<ModelAnimator>().empty());
        }
        CHECK(n1 <= 2); CHECK(n2 <= 2); CHECK(n3 <= 2);
        // An old scene saved with both machines, and one saved with two base machines by mistake, loads without a crash
        // and keeps what it had.
        const std::string txt = SceneSerializer::Serialize(c);
        Scene back("back"); CHECK(SceneSerializer::Deserialize(back, txt));
        int loaded = 0; for (const auto& g : back.Objects()) if (g) loaded += (int)g->GetComponents<AnimStateMachine>().size();
        CHECK(loaded == (int)p3->GetComponents<AnimStateMachine>().size());
        p3->AddComponent<AnimStateMachine>();   // a second base machine
        Scene back2("back2"); CHECK(SceneSerializer::Deserialize(back2, SceneSerializer::Serialize(c)));
        int loaded2 = 0; for (const auto& g : back2.Objects()) if (g) loaded2 += (int)g->GetComponents<AnimStateMachine>().size();
        CHECK(loaded2 >= 2);
        for (int i = 0; i < 5; ++i) back2.Update(1.0f / 60.0f);
    }

    // ---- diagonal: W+D is no faster than W (both controllers, walk and sprint) ----
    for (Mode m : {Mode::First, Mode::Third})
        for (int sprint = 0; sprint < 2; ++sprint) {
            float s[2];
            for (int diag = 0; diag < 2; ++diag) {
                Rig r; r.Spawn(m);
                r.Frames(0.5, {});
                std::vector<char> k = {'w'};
                if (diag) k.push_back('d');
                if (sprint) k.push_back(Input::KeyShift);
                r.Frames(2.0, k);
                s[diag] = HSpeed(r);
            }
            std::printf("  diagonal %s %s: straight %.3f m/s, diagonal %.3f m/s\n", m == Mode::First ? "first" : "third",
                        sprint ? "sprint" : "walk", s[0], s[1]);
            CHECK(s[0] > 1.0f);
            CHECK(s[1] < s[0] * 1.03f);
        }

    // ---- coyote time: a jump just after walking off a ledge works; a jump long after does not ----
    for (int late = 0; late < 2; ++late) {
        Rig r(false); r.Box({0, -0.5f, 0}, {6, 1, 6});   // a 6 m square platform, nothing below it
        r.Spawn(Mode::First, {0, 0.02f, 2.0f});
        r.Frames(0.5, {});
        const float y0 = r.Pos().y;
        int i = 0;
        for (; i < 400 && r.Pos().y > y0 - (late ? 1.5f : 0.015f); ++i) r.Frame({'w'});   // walk off, until just left / well below
        CHECK(i < 400);
        float vyBefore = r.rb->velocity.y;
        r.Frame({' '}); r.Frame({' '});
        const bool jumped = r.rb->velocity.y > 1.0f;
        std::printf("  coyote %s: vy before %.2f, after %.2f, jumped %d\n", late ? "late (1.5 m down)" : "just left", vyBefore, r.rb->velocity.y, (int)jumped);
        CHECK(jumped == (late == 0));
    }

    // ---- jump buffering: Space pressed just before landing jumps again on landing; with a 0 buffer it does not ----
    for (int buf = 0; buf < 2; ++buf) {
        Rig r; r.Spawn(Mode::First);
        auto* fp = r.p->GetComponent<FirstPersonController>();
        fp->jumpBufferTime = buf ? 0.12f : 0.0f;
        r.Frames(0.5, {});
        r.Frame({' '});
        int n = 0;
        while (n < 200 && !(r.rb->velocity.y < 0 && r.Pos().y < 0.45f && n > 5)) { r.Frame({}); ++n; }
        r.Frame({' '});   // one tap, about a tenth of a second before landing
        float topAfter = 0; bool landed = false;
        for (int k = 0; k < 90; ++k) {
            r.Frame({});
            if (r.Pos().y < 0.08f) landed = true;
            if (landed) topAfter = std::max(topAfter, r.Pos().y);
        }
        std::printf("  jump buffer %.2f s: height after landing %.2f m\n", fp->jumpBufferTime, topAfter);
        CHECK(landed);
        if (buf) CHECK(topAfter > 0.5f); else CHECK(topAfter < 0.3f);
    }

    // ---- slope: a 20 degree ramp is climbed, a 75 degree wall of ramp is not (slope limit 50) ----
    for (int steep = 0; steep < 2; ++steep) {
        Rig r;
        const float deg = steep ? 75.0f : 20.0f, rad = deg * 3.14159265f / 180.0f, L = 20.0f;
        // plane through (0,0,-3): centre = start + dir*L/2 - normal*0.2
        const Vec3 start{0, 0, -3}, dir{0, std::sin(rad), -std::cos(rad)}, nrm{0, std::cos(rad), std::sin(rad)};
        r.Box(start + dir * (L * 0.5f) - nrm * 0.2f, {6, 0.4f, L}, {deg, 0, 0});
        r.Spawn(Mode::First);
        float top = 0;
        for (int i = 0; i < 360; ++i) { r.Frame({'w'}); top = std::max(top, r.Pos().y); }
        std::printf("  slope %2.0f deg: highest point reached %.2f m\n", deg, top);
        if (steep) CHECK(top < 1.0f); else CHECK(top > 1.5f);
    }

    // ---- steps: 0.25 m (under the 0.35 step offset) is climbed, 0.8 m blocks ----
    for (int tall = 0; tall < 2; ++tall) {
        Rig r;
        const float h = tall ? 0.8f : 0.25f;
        r.Box({0, h * 0.5f, -3.5f}, {6, h, 1});
        r.Spawn(Mode::First);
        r.Frames(4.0, {'w'});
        std::printf("  step %.2f m: ended at z %.2f, y %.2f\n", h, r.Pos().z, r.Pos().y);
        if (tall) CHECK(r.Pos().z > -3.4f); else CHECK(r.Pos().z < -4.5f);
    }

    // ---- first-person look: a stick held for a second turns the same angle at 30, 60 and 144 fps ----
    float turn[3]; const float fps[3] = {30, 60, 144};
    for (int i = 0; i < 3; ++i) {
        Rig r(true, fps[i]); r.Spawn(Mode::First);
        auto* fp = r.p->GetComponent<FirstPersonController>();
        r.Frames(0.3, {});
        const float y0 = fp->yaw;
        for (int k = 0, n = (int)std::lround(1.0f * fps[i]); k < n; ++k) {
            Input::FeedKeys({}); Input::FeedGamepadExtra({1.0f, 0.0f}, 0.0f, 0.0f, true); r.sc->Update(r.dt);
        }
        Input::FeedGamepadExtra({0.0f, 0.0f}, 0.0f, 0.0f, false);
        turn[i] = std::fabs(fp->yaw - y0);
        std::printf("  stick look 1 s at %3.0f fps: %.2f deg\n", fps[i], turn[i]);
    }
    CHECK(turn[0] > 10.0f);
    CHECK(std::fabs(turn[1] - turn[0]) < turn[0] * 0.05f);
    CHECK(std::fabs(turn[2] - turn[0]) < turn[0] * 0.05f);
    TEST_MAIN_RESULT();
}
