// Player feel upgrade (claude/player-feel): controller presets, motion numbers per preset, the preset hotkeys,
// camera stick acceleration, camera steadiness at any frame rate, foot sliding of the real controller, and the
// per-system frame-cost lines. Each block prints the numbers it measured so a reader can compare the presets.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/ControllerPresets.hpp"
#include "okay/Core/SystemCost.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
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
    Rig() {
        sc = std::make_unique<Scene>("feel");
        sc->physicsEnabled = true;
        sc->UseFixedStepPhysics(true);
        GameObject* f = sc->CreateGameObject("Floor");
        f->transform->localPosition = {0, -0.5f, 0};
        f->AddComponent<BoxCollider3D>()->size = {400, 1, 400};
    }
    void Spawn(Mode m, int preset, float y = 0.02f) {
        p = Templates::BuildPlayerBody(*sc, {0, y, 0}, "Player");
        rb = p->GetComponent<Rigidbody3D>();
        PlayerController::Add(p, m, m == Mode::Third ? PlayerView::View::Third : PlayerView::View::First);
        if (preset >= 0) ControllerPresets::ApplyTo(p, preset);
        Templates::MountEyeCamera(*sc, p, "Main Camera");
        sc->Start();
    }
    void Frame(const std::vector<char>& k, float d = 0.0f) { Input::FeedKeys(k); sc->Update(d > 0 ? d : dt); }
    void Frames(double s, const std::vector<char>& k) { for (int i = 0, n = (int)std::lround(s / dt); i < n; ++i) Frame(k); }
    Vec3 Pos() const { return p->transform->Position(); }
    ~Rig() { Input::FeedKeys({}); }
};

float HSpeed(const Rig& r) { return std::hypot(r.rb->velocity.x, r.rb->velocity.z); }

struct Motion { float timeToRun = 0, stopDist = 0, stopTime = 0, airGain = 0, runSpeed = 0; };

// Hold forward + sprint on flat ground: time to reach 90% of the speed it settles at; then release and measure how far
// and how long it takes to stop; then jump while strafing and read the sideways speed gained in the air.
Motion Measure(Mode m, int preset) {
    Rig r; r.Spawn(m, preset);
    r.Frames(0.6, {});
    Motion o;
    float top = 0; int n = 0;
    for (int i = 0; i < 240; ++i) { r.Frame({'w', Input::KeyShift}); top = std::max(top, HSpeed(r)); }
    o.runSpeed = top;
    Rig q; q.Spawn(m, preset);
    q.Frames(0.6, {});
    for (; n < 240; ++n) { q.Frame({'w', Input::KeyShift}); if (HSpeed(q) >= 0.9f * top) break; }
    o.timeToRun = n * q.dt;
    q.Frames(1.5, {'w', Input::KeyShift});
    const Vec3 a = q.Pos(); int s = 0;
    for (; s < 300; ++s) { q.Frame({}); if (HSpeed(q) < 0.05f) break; }
    o.stopTime = s * q.dt; o.stopDist = std::hypot(q.Pos().x - a.x, q.Pos().z - a.z);
    Rig j; j.Spawn(m, preset);
    j.Frames(0.6, {});
    j.Frame({' '}); j.Frame({' '});
    float side = 0;
    for (int i = 0; i < 20 && j.rb->velocity.y > 0.5f; ++i) {
        j.Frame({'d'});
        side = std::max(side, std::fabs(j.rb->velocity.x));   // started from rest: all of it was gained in the air
    }
    o.airGain = side;
    return o;
}

}  // namespace

int main() {
    RUN_SUITE("player_feel");

    // ---- Presets: names, lookups, the numbers they set, Classic = a new controller. ----
    {
        CHECK(std::string(ControllerPresets::Name(0)) == "Classic");
        CHECK(std::string(ControllerPresets::Name(4)) == "Shooter");
        CHECK(std::string(ControllerPresets::Name(9)) == "Custom");
        CHECK(ControllerPresets::FromName("ARCADE") == 2 && ControllerPresets::FromName("modern") == 1);
        CHECK(ControllerPresets::FromName("nope") == -1 && ControllerPresets::FromName(nullptr) == -1);
        CHECK(ControllerPresets::Next(4) == 0);

        Scene s("p");
        GameObject* g = s.CreateGameObject("P");
        auto* pc = PlayerController::Add(g, Mode::Both);
        FirstPersonController* fp = pc->first(); ThirdPersonController* tp = pc->third();
        CHECK(fp && tp);
        const FirstPersonController fpDefault; const ThirdPersonController tpDefault;
        CHECK(!ControllerPresets::ApplyTo(nullptr, 1));
        CHECK(!ControllerPresets::Apply(*fp, 9) && !ControllerPresets::Apply(*fp, -1));
        CHECK(ControllerPresets::ApplyTo(g, ControllerPresets::Arcade));
        CHECK(fp->controllerPreset == 2 && tp->controllerPreset == 2);
        CHECK_NEAR(fp->airControl, 1.0f, 1e-6f);
        CHECK(fp->maxJumps == 2 && tp->maxJumps == 2 && fp->variableJump && tp->variableJump);
        CHECK_NEAR(tp->turnSpeed, 40.0f, 1e-6f);
        ControllerPresets::ApplyTo(g, ControllerPresets::Realistic);
        CHECK_NEAR(fp->airControl, 0.12f, 1e-6f);
        CHECK(!fp->variableJump && fp->maxJumps == 1);
        CHECK_NEAR(fp->bobSway, 0.9f, 1e-6f);
        // Classic restores every field the other presets touched, on each controller type's own defaults.
        ControllerPresets::ApplyTo(g, ControllerPresets::Classic);
        CHECK(fp->controllerPreset == 0);
        CHECK_NEAR(fp->walkSpeed, fpDefault.walkSpeed, 1e-6f); CHECK_NEAR(fp->acceleration, fpDefault.acceleration, 1e-6f);
        CHECK_NEAR(fp->deceleration, fpDefault.deceleration, 1e-6f); CHECK_NEAR(fp->airControl, fpDefault.airControl, 1e-6f);
        CHECK_NEAR(fp->sprintFovBoost, fpDefault.sprintFovBoost, 1e-6f); CHECK_NEAR(fp->bobAmount, fpDefault.bobAmount, 1e-6f);
        CHECK_NEAR(tp->sprintFovBoost, tpDefault.sprintFovBoost, 1e-6f); CHECK_NEAR(tp->turnSpeed, tpDefault.turnSpeed, 1e-6f);
        CHECK_NEAR(tp->orbitSmoothing, tpDefault.orbitSmoothing, 1e-6f);
        CHECK(fp->maxJumps == fpDefault.maxJumps && fp->variableJump == fpDefault.variableJump);
        // player_feel_presets
    }

    // ---- The pick and the stick ramp are saved with the scene. ----
    for (int view = 0; view < 3; ++view) {
        Scene a("save");
        GameObject* g = a.CreateGameObject("P");
        auto* pc = PlayerController::Add(g, view == 0 ? Mode::First : view == 1 ? Mode::Third : Mode::Both);
        ControllerPresets::ApplyTo(g, ControllerPresets::Shooter);
        if (pc->first()) pc->first()->stickLookRamp = 0.8f;
        if (pc->third()) pc->third()->stickLookRamp = 1.2f;
        if (auto* pv = g->GetComponent<PlayerView>()) pv->presetHotkeys = true;
        Scene b("load");
        CHECK(SceneSerializer::Deserialize(b, SceneSerializer::Serialize(a)));
        auto* q = PlayerController::Of(b.Find("P"));
        CHECK(q != nullptr);
        if (q && q->first()) { CHECK(q->first()->controllerPreset == 4); CHECK_NEAR(q->first()->stickLookRamp, 0.8f, 1e-5f); CHECK_NEAR(q->first()->airControl, 0.35f, 1e-5f); }
        if (q && q->third()) { CHECK(q->third()->controllerPreset == 4); CHECK_NEAR(q->third()->stickLookRamp, 1.2f, 1e-5f); CHECK_NEAR(q->third()->turnSpeed, 32.0f, 1e-5f); }
        auto* pv = b.Find("P") ? b.Find("P")->GetComponent<PlayerView>() : nullptr;
        if (view == 2) CHECK(pv && pv->presetHotkeys);   // a PlayerView exists in Both mode only
        // player_feel_saved
    }

    // ---- Measured: how each preset moves (numbers printed for the owner's comparison). ----
    {
        Motion m[ControllerPresets::Count];
        for (int i = 0; i < ControllerPresets::Count; ++i) {
            m[i] = Measure(Mode::First, i);
            std::printf("[player_feel] %-9s top %.2f m/s  to 90%% %.2f s  stop %.2f s over %.2f m  air sideways %.2f m/s\n",
                        ControllerPresets::Name(i), m[i].runSpeed, m[i].timeToRun, m[i].stopTime, m[i].stopDist, m[i].airGain);
        }
        const Motion &cl = m[0], &mo = m[1], &ar = m[2], &re = m[3], &sh = m[4];
        CHECK(re.timeToRun > cl.timeToRun && re.timeToRun > ar.timeToRun && re.timeToRun > sh.timeToRun);   // realistic is the slowest to start
        CHECK(re.stopDist > cl.stopDist && re.stopDist > ar.stopDist);                                       // ...and to stop
        CHECK(ar.stopDist <= cl.stopDist + 0.02f);                                                           // arcade stops as short as or shorter than classic
        CHECK(ar.airGain > re.airGain + 0.5f && ar.airGain > mo.airGain);                                    // arcade steers most in the air, realistic least
        CHECK(ar.runSpeed > cl.runSpeed && sh.runSpeed > re.runSpeed);
        for (int i = 0; i < ControllerPresets::Count; ++i) CHECK(m[i].runSpeed > 3.0f && m[i].runSpeed < 8.0f);
        // player_feel_motion
    }

    // ---- Preset hotkeys: 1-5 apply a preset while playing, and a text called PresetLabel shows it. Off by default. ----
    {
        Rig r;
        GameObject* lab = r.sc->CreateGameObject("PresetLabel");
        lab->AddComponent<TextRenderer>()->text = "?";
        r.Spawn(Mode::Both, -1);
        auto* fp = r.p->GetComponent<FirstPersonController>();
        auto* pv = r.p->GetComponent<PlayerView>();
        CHECK(pv && fp && !pv->presetHotkeys);
        if (!pv || !fp) return 1;
        r.Frame({'3'}); r.Frame({});
        CHECK(fp->controllerPreset == 0);                    // off: the key does nothing
        pv->presetHotkeys = true;
        r.Frame({'3'}); r.Frame({});
        CHECK(fp->controllerPreset == 2);
        CHECK(lab->GetComponent<TextRenderer>()->text.find("Arcade") != std::string::npos);
        r.Frame({'5'}); r.Frame({});
        CHECK(fp->controllerPreset == 4 && fp->maxJumps == 1);
        r.Frame({'1'}); r.Frame({});
        CHECK(fp->controllerPreset == 0);
        // player_feel_hotkeys
    }

    // ---- Camera stick acceleration: 45% at first, full after the ramp; a relaxed stick drops it; 0 = off. ----
    {
        StickLookRamp s;
        CHECK_NEAR(s.Gain(1.0f, 0.0f, 0.0f), 1.0f, 1e-6f);   // ramp 0 = constant speed
        CHECK_NEAR(s.Gain(1.0f, 0.0f, 1.0f), 0.45f, 1e-5f);
        float g = 0; for (int i = 0; i < 30; ++i) g = s.Gain(1.0f, 1.0f / 60.0f, 1.0f);
        CHECK(g > 0.65f && g < 0.75f);                        // half way after half the ramp
        for (int i = 0; i < 60; ++i) g = s.Gain(1.0f, 1.0f / 60.0f, 1.0f);
        CHECK_NEAR(g, 1.0f, 1e-5f);
        for (int i = 0; i < 10; ++i) g = s.Gain(0.3f, 1.0f / 60.0f, 1.0f);   // small throw: builds nothing and decays
        CHECK(g < 0.85f);
        for (int i = 0; i < 40; ++i) g = s.Gain(0.3f, 1.0f / 60.0f, 1.0f);
        CHECK_NEAR(g, 0.45f, 1e-5f);
        s.Gain(1.0f, 0.5f, 1.0f); s.Reset();
        CHECK_NEAR(s.Gain(0.9f, 0.0f, 1.0f), 0.45f, 1e-5f);
        // The controller applies it: the same stick held for 1 s turns further with the ramp off than on in the first 0.2 s.
        float turned[2];
        for (int on = 0; on < 2; ++on) {
            Rig r; r.Spawn(Mode::First, -1);
            auto* fp = r.p->GetComponent<FirstPersonController>();
            fp->stickLookRamp = on ? 1.0f : 0.0f;
            r.Frames(0.3, {});
            const float y0 = fp->yaw;
            Input::FeedGamepadExtra({1.0f, 0.0f}, 0.0f, 0.0f, true);
            for (int i = 0; i < 12; ++i) { Input::FeedKeys({}); Input::FeedGamepadExtra({1.0f, 0.0f}, 0.0f, 0.0f, true); r.sc->Update(r.dt); }
            turned[on] = std::fabs(fp->yaw - y0);
            Input::FeedGamepadExtra({0.0f, 0.0f}, 0.0f, 0.0f, false);
        }
        std::printf("[player_feel] stick look in 0.2 s at full throw: ramp off %.1f deg, ramp 1 s %.1f deg\n", turned[0], turned[1]);
        CHECK(turned[0] > 1.0f);
        CHECK(turned[1] < turned[0] * 0.85f);
        // player_feel_stick_ramp
    }

    // ---- Third-person camera steadiness at any frame rate: walking at a steady speed, the camera keeps the same
    // distance from the player and moves without jumps, at 30, 60, 144 and 240 frames per second. ----
    {
        float worstJump = 0;
        for (float fps : {30.0f, 60.0f, 144.0f, 240.0f}) {
            Rig r; r.Spawn(Mode::Third, ControllerPresets::Modern);
            const float d = 1.0f / fps;
            for (int i = 0; i < (int)(0.8f * fps); ++i) r.Frame({}, d);
            Camera* cam = r.sc->mainCamera;
            CHECK(cam != nullptr);
            if (!cam) continue;
            for (int i = 0; i < (int)(0.8f * fps); ++i) r.Frame({'w'}, d);       // reach a steady walk
            std::vector<float> off; Vec3 prev = cam->transform->Position(); float maxStep = 0, sumStep = 0; int n = 0;
            for (int i = 0; i < (int)(1.5f * fps); ++i) {
                r.Frame({'w'}, d);
                const Vec3 c = cam->transform->Position();
                off.push_back((c - r.Pos()).Magnitude());
                const float st = (c - prev).Magnitude() / d;                        // camera speed (m/s)
                maxStep = std::max(maxStep, st); sumStep += st; ++n; prev = c;
            }
            const float lo = *std::min_element(off.begin(), off.end()), hi = *std::max_element(off.begin(), off.end());
            const float meanSpeed = sumStep / std::max(1, n);
            const float jump = (maxStep - meanSpeed) / std::max(0.1f, meanSpeed);
            worstJump = std::max(worstJump, jump);
            std::printf("[player_feel] third person camera at %3.0f fps: distance range %.3f m, speed mean %.2f max %.2f m/s (jump %.0f%%)\n",
                        fps, hi - lo, meanSpeed, maxStep, jump * 100.0f);
            CHECK(hi - lo < 0.35f);
            CHECK(jump < 0.35f);
        }
        // player_feel_camera_fps
    }

    // ---- First-person camera: the eye height range while walking is the same at every frame rate (the bob is a
    // function of distance walked, not of frames), and Reduce Motion switches the bob and sway off. ----
    {
        float range[4]; int k = 0;
        for (float fps : {30.0f, 60.0f, 144.0f, 240.0f}) {
            Rig r; r.Spawn(Mode::First, ControllerPresets::Realistic);
            const float d = 1.0f / fps;
            for (int i = 0; i < (int)(0.6f * fps); ++i) r.Frame({}, d);
            Camera* cam = r.sc->mainCamera;
            for (int i = 0; i < (int)(1.0f * fps); ++i) r.Frame({'w'}, d);
            float lo = 9, hi = -9;
            for (int i = 0; i < (int)(2.0f * fps); ++i) { r.Frame({'w'}, d); const float y = cam->transform->Position().y - r.Pos().y; lo = std::min(lo, y); hi = std::max(hi, y); }
            range[k++] = hi - lo;
            std::printf("[player_feel] first person eye bob range at %3.0f fps: %.4f m\n", fps, hi - lo);
        }
        for (int i = 1; i < 4; ++i) CHECK_NEAR(range[i], range[0], 0.012f);
        CHECK(range[0] > 0.03f);   // the realistic bob is there
        // player_feel_bob_fps
    }

    // ---- Foot sliding: the planted foot of the real controller's character moves slower than a fraction of the
    // ground speed, walking and running, for every preset (measured on the animated body's lowest foot vertex). ----
    {
        for (int preset : {0, 1, 2, 3, 4}) {
            for (int run = 0; run < 2; ++run) {
                Rig r; r.Spawn(Mode::Third, preset);
                Character* ch = r.p->GetComponent<Character>();
                std::vector<int> bone; ch->BuildRig(bone);
                const int feet[2] = {Character::BoneIndex("l_foot"), Character::BoneIndex("r_foot")};
                auto* mr = r.p->GetComponent<MeshRenderer>();
                auto world = [&](std::size_t i) { return r.p->transform->TransformPoint(mr->mesh.vertices[i]); };
                auto lowest = [&](int b) { std::size_t best = 0; float lo = 1e9f; for (std::size_t i = 0; i < bone.size(); ++i) if (bone[i] == b) { const float y = world(i).y; if (y < lo) { lo = y; best = i; } } return best; };
                const float d = 1.0f / 120.0f;
                r.sc->Update(d);
                std::vector<char> keys = run ? std::vector<char>{'w', Input::KeyShift} : std::vector<char>{'w'};
                for (int i = 0; i < 240; ++i) r.Frame(keys, d);                // steady state
                std::size_t idx[2] = {0, 0}; Vec3 pos[2]; std::vector<float> slips; float groundSum = 0; int groundN = 0;
                Vec3 prevBody = r.Pos();
                for (int i = 0; i < 480; ++i) {
                    r.Frame(keys, d);
                    if (mr->mesh.vertices.size() != bone.size()) continue;
                    groundSum += std::hypot(r.Pos().x - prevBody.x, r.Pos().z - prevBody.z) / d; ++groundN; prevBody = r.Pos();
                    for (int f = 0; f < 2; ++f) {
                        if (i > 0 && pos[f].y < 0.01f) { const Vec3 w = world(idx[f]); slips.push_back(std::hypot(w.x - pos[f].x, w.z - pos[f].z) / d); }
                        idx[f] = lowest(feet[f]); pos[f] = world(idx[f]);
                    }
                }
                std::sort(slips.begin(), slips.end());
                const float ground = groundSum / std::max(1, groundN);
                const float median = slips.empty() ? 99.0f : slips[slips.size() / 2];
                std::printf("[player_feel] foot slip %-9s %s: ground %.2f m/s, planted-foot median %.3f m/s (%.0f%% of ground), %zu samples\n",
                            ControllerPresets::Name(preset), run ? "run " : "walk", ground, median, 100.0f * median / std::max(0.01f, ground), slips.size());
                CHECK(ground > 1.0f);
                CHECK(!slips.empty());
                CHECK(median < 0.25f * ground);
            }
        }
        // player_feel_foot_slip
    }

    // ---- Per-system frame cost: the profiler's scopes land in the right rows; the text is stable. ----
    {
        Profiler p; p.enabled = true;
        for (int f = 0; f < 5; ++f) {
            p.BeginFrame();
            { p.Begin("Physics.3D"); p.End(); }
            { p.Begin("Scripts.Update"); p.Begin("Anim.IK"); p.End(); p.End(); }
            { p.Begin("Audio.Mix"); p.End(); }
            { p.Begin("Render.Submit"); p.End(); }
            { p.Begin("UI.Draw"); p.End(); }
            { p.Begin("Voxel.Mesh"); p.End(); }
            p.EndFrame(20.0);
        }
        const auto rows = SystemCost::Rows(p);
        CHECK(rows.size() == 9);
        CHECK(rows[0].name == "Physics" && rows[2].name == "Animation" && rows[6].name == "Terrain" && rows[8].name == "Idle/wait");
        CHECK(rows[8].avgMs > 19.0);                         // 20 ms frame, almost all of it idle here
        const auto lines = SystemCost::Lines(p);
        CHECK(lines.size() == 11);
        CHECK(lines[0].find("last 5 frames") != std::string::npos);
        CHECK(lines.back() == "frame 20.00 ms");
        CHECK(SystemCost::Plot(p, 8).size() == 8 && SystemCost::Plot(p, 8)[0] == 0.0f && SystemCost::Plot(p, 8)[7] == 20.0f);
        Profiler off;
        CHECK(SystemCost::Lines(off).size() == 1);
        // A real scene fills the scripts / physics / animation rows through its own scopes.
        Rig r; r.Spawn(Mode::First, -1);
        Prof().enabled = true;
        for (int i = 0; i < 30; ++i) { Prof().BeginFrame(); r.Frame({'w'}); Prof().EndFrame(16.0); }
        const auto live = SystemCost::Rows(Prof());
        Prof().enabled = false;
        CHECK(live[0].avgMs > 0.0 || live[1].avgMs > 0.0);   // physics or scripts measured something
        std::printf("[player_feel] live system rows: physics %.3f scripts %.3f animation %.3f ms\n", live[0].avgMs, live[1].avgMs, live[2].avgMs);
        // player_feel_system_cost
    }

    // A body that is spawned in the air falls; it is not "snapped down to the ground it just left" (the ground-snap that keeps
    // a walker on stairs and curbs needs a real ground contact first). Before the fix the first air frame of a controller that
    // never touched ground counted as "just left the ground" and the body was moved the whole 0.2 m to the floor in one frame,
    // and the first-person eye then glided down that 0.2 m for a second: a camera bob nobody asked for.
    Accessibility::SetReduceMotion(true);   // so the walking head bob does not count as eye movement
    for (Mode m : {Mode::First, Mode::Third}) {
        Rig r; r.Spawn(m, -1, 0.2f);
        for (int i = 0; i < 6; ++i) r.Frame({});
        // 6 frames of free fall are about 5 cm; the snap-down had already put it on the floor by then.
        const float y4 = r.Pos().y;
        CHECK(y4 > 0.12f);
        r.Frames(0.5, {});
        CHECK(std::fabs(r.Pos().y) < 0.01f);
        // Once the landing is over the camera holds a fixed height over the body (no eye gliding down a snap).
        GameObject* cam = r.sc->Find("Main Camera");
        CHECK(cam != nullptr);
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < 40; ++i) { r.Frame({'w'}); const float h = cam->transform->Position().y - r.Pos().y; lo = std::min(lo, h); hi = std::max(hi, h); }
        std::printf("[player_feel] spawned 0.2 m in the air (%s): y after 6 frames %.3f, after landing %.4f, camera height spread while walking %.5f\n",
                    m == Mode::First ? "first" : "third", y4, r.Pos().y, hi - lo);
        if (m == Mode::First) CHECK(hi - lo < 0.001f);
    }
    Accessibility::SetReduceMotion(false);

    // Walking into a crate pushes it: a movable body is not a wall (no wall-slide), so the walker keeps its pace. The walker's
    // velocity is set by the controller each frame and the solver hands the crate its share; a person pushing a 30 kg crate
    // across a floor (about 150 N of friction) also keeps walking, a little slower, never at four fifths of the speed.
    {
        auto walk = [](float crateKg) {
            Scene sc("Push");
            GameObject* g = sc.CreateGameObject("Ground");
            g->transform->localPosition = {0, -0.5f, 0};
            g->transform->localScale = {100, 1, 100};
            g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
            g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
            GameObject* player = Templates::AddThirdPersonPlayer(sc, {0, 0, 0});
            if (crateKg > 0.0f) {   // 0.4 x 0.3 x 0.3 m, 0.8 m ahead of the walker
                GameObject* c = sc.CreateGameObject("Crate");
                c->transform->localPosition = {0, 0.3f, -0.8f};
                c->transform->localScale = {0.4f, 0.3f, 0.3f};
                c->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
                c->AddComponent<Rigidbody3D>()->mass = crateKg;
            }
            sc.Start();
            for (int i = 0; i < 30; ++i) { Input::FeedKeys({}); sc.Update(1.0f / 60.0f); }
            const Vec3 a = player->transform->Position();
            for (int i = 0; i < 90; ++i) { Input::FeedKeys({'w'}); sc.Update(1.0f / 60.0f); }
            Input::FeedKeys({});
            const Vec3 d = player->transform->Position() - a;
            return std::sqrt(d.x * d.x + d.z * d.z);
        };
        const float free = walk(0.0f), light = walk(1.0f), heavy = walk(30.0f);
        std::printf("[player_feel] pushing a crate head-on for 1.5 s: free %.3f m, 1 kg crate %.3f m, 30 kg crate %.3f m\n", free, light, heavy);
        CHECK(light > free * 0.95f);
        CHECK(heavy > free * 0.95f);
    }

    TEST_MAIN_RESULT();
}
