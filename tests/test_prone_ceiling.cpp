// Prone under a low ceiling, for the first-person, third-person and third-person-shooter controllers: going prone
// shrinks the body, a stand-up (or crouch) asked for under a slab lower than that stance's height is held back, and in
// toggle mode it happens on its own once the player is out from under the slab. Prone itself (keys, speed, eye height,
// saving) is covered by `stance`; this file covers the ceiling rule the controllers' comments promise, the buttons a
// server runs for a networked player (Simulate with the Prone button and the pad's stance button), and the sample scene.
// Written for this engine.
//
// The last part walks the sample scene the owner opens in the editor (examples/stance/stance_course.okayscene, see
// docs/try_it.md): it is loaded from disk and driven with key presses only, in third person (where it starts) and again
// in first person after V. It proves the course can be played as the help text on screen says.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace okay;

namespace {
const float kDt = 1.0f / 60.0f;

void Static(Scene& s, const char* name, Vec3 pos, Vec3 size) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = pos;
    g->AddComponent<BoxCollider3D>()->size = size;
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
}

// A little glue so the same ceiling checks run on every controller. First Person and Third Person hold the stance on
// themselves; the Third Person Shooter keeps it in a StanceControl (the same SettleStance rule underneath).
inline void Setup(FirstPersonController* c) { c->toggleStance = true; c->lockCursor = false; }
inline void Setup(ThirdPersonController* c) { c->toggleStance = true; c->lockCursor = false; }
inline void Setup(ThirdPersonShooterController* c) { c->stanceControl.toggle = true; c->lockCursor = false; }
inline int  StanceOf(const FirstPersonController* c) { return (int)c->stance(); }    // 0 stand, 1 crouch, 2 prone
inline int  StanceOf(const ThirdPersonController* c) { return (int)c->stance(); }
inline int  StanceOf(const ThirdPersonShooterController* c) { return c->stanceControl.Stance(); }
inline char ProneKeyOf(const FirstPersonController* c) { return c->proneKey; }
inline char ProneKeyOf(const ThirdPersonController* c) { return c->proneKey; }
inline char ProneKeyOf(const ThirdPersonShooterController* c) { return c->stanceControl.proneKey; }
inline char CrouchKeyOf(const FirstPersonController* c) { return c->crouchKey; }
inline char CrouchKeyOf(const ThirdPersonController* c) { return c->crouchKey; }
inline char CrouchKeyOf(const ThirdPersonShooterController* c) { return c->stanceControl.crouchKey; }

template <typename Ctrl>
struct Rig {
    Scene scene{"prone_ceiling"};
    GameObject* player = nullptr;
    Ctrl* ctrl = nullptr;
    BoxCollider3D* box = nullptr;
    Rig() {
        Static(scene, "Floor", {0, -0.5f, 0}, {60, 1, 60});
        // A slab whose underside is 0.9 above the floor, over x in [-3, 3]: room to crawl, not to crouch or stand.
        Static(scene, "Slab", {0, 1.0f, 0}, {6, 0.2f, 6});
        player = scene.CreateGameObject("Player");
        player->transform->localPosition = {8, 0.05f, 0};
        player->AddComponent<Rigidbody3D>();
        box = player->AddComponent<BoxCollider3D>();
        box->size = {0.6f, 1.8f, 0.6f};
        box->offset = {0, 0.9f, 0};
        ctrl = player->AddComponent<Ctrl>();
        Setup(ctrl);
        GameObject* eye = scene.CreateGameObject("Eye");
        eye->AddComponent<Camera>();
        eye->transform->SetParent(player->transform, false);
        eye->transform->localPosition = {0, 1.6f, 0};
        scene.Start();
        Tick({}, 20);
    }
    void Tick(const std::vector<char>& keys, int frames) {
        for (int i = 0; i < frames; ++i) { Input::FeedKeys(keys); scene.Update(kDt); }
    }
    void Tap(char k) { Tick({k}, 1); Tick({}, 1); }
    void PlaceAt(float x) {
        player->transform->SetPosition({x, player->transform->Position().y, 0});
        if (auto* rb = player->GetComponent<Rigidbody3D>()) rb->velocity = Vec3{0, 0, 0};
        Tick({}, 30);
    }
    int StanceNow() const { return StanceOf(ctrl); }
};

template <typename Ctrl>
void Check(const char* who) {
    Rig<Ctrl> r;
    const int Stand = 0, Prone = 2;                      // StanceOf: 0 stand, 1 crouch, 2 prone
    CHECK(r.StanceNow() == Stand);
    const float standH = r.box->size.y;
    r.Tap(ProneKeyOf(r.ctrl));
    r.Tick({}, 90);
    CHECK(r.StanceNow() == Prone);
    const float proneH = r.box->size.y;
    std::printf("  %s: body %.2f standing, %.2f prone\n", who, standH, proneH);
    CHECK(proneH < 0.9f && proneH < standH * 0.5f);     // low enough to fit under the slab
    r.PlaceAt(0.0f);                                     // crawl in under the slab
    CHECK(r.StanceNow() == Prone);
    r.Tap(ProneKeyOf(r.ctrl));                           // ask to stand up: no room
    r.Tick({}, 60);
    std::printf("  %s: under the slab after asking to stand: stance %d, body %.2f\n", who, r.StanceNow(), r.box->size.y);
    CHECK(r.StanceNow() == Prone);
    CHECK(r.box->size.y < 0.9f);
    r.Tap(CrouchKeyOf(r.ctrl));                          // crouch does not fit either
    r.Tick({}, 60);
    CHECK(r.StanceNow() == Prone);
    r.Tap(CrouchKeyOf(r.ctrl));                          // back to asking for Stand (toggle)
    r.Tick({}, 10);
    r.PlaceAt(8.0f);                                     // out from under it: the stand-up happens on its own
    r.Tick({}, 90);
    std::printf("  %s: in the open: stance %d, body %.2f\n", who, r.StanceNow(), r.box->size.y);
    CHECK(r.StanceNow() == Stand);
    CHECK(r.box->size.y > standH - 0.05f);
}

// What the server runs for a networked player (server-authoritative movement): the same Simulate() with the buttons a
// client sent. Prone has to work from the Prone button alone, in hold mode from the held button, from the pad's stance
// button held long enough (a short tap only crouches), and "Allow Prone" off has to refuse it.
template <typename Ctrl>
void CheckSimulatedButtons(const char* who) {
    using St = typename Ctrl::Stance;
    Scene t("sim_stance");
    t.physicsEnabled = false;
    auto* c = t.CreateGameObject("P")->AddComponent<Ctrl>();
    c->lockCursor = false; c->driveAnimation = false;
    t.Start();
    CHECK(c->stance() == St::Stand);
    const PlayerInput none;
    PlayerInput pr; pr.held = pr.pressed = PlayerInput::Prone;
    c->Simulate(pr, 0.1f);
    CHECK(c->stance() == St::Prone);                    // a press of the Prone button
    pr.pressed = 0; c->Simulate(pr, 0.1f);
    CHECK(c->stance() == St::Prone);                    // held without a new press: still prone
    c->Simulate(none, 0.1f);
    CHECK(c->stance() == St::Prone);                    // toggle mode remembers it
    pr.pressed = PlayerInput::Prone; c->Simulate(pr, 0.1f);
    CHECK(c->stance() == St::Stand);                    // the next press gets up
    // The pad: tap = crouch, hold (proneHoldTime, 0.4 s) = prone.
    PlayerInput pad; pad.held = PlayerInput::StanceButton;
    for (int i = 0; i < 3; ++i) c->Simulate(pad, 0.1f);   // 0.3 s: a tap
    c->Simulate(none, 0.1f);
    CHECK(c->stance() == St::Crouch);
    for (int i = 0; i < 6; ++i) c->Simulate(pad, 0.1f);   // 0.6 s: a hold
    CHECK(c->stance() == St::Prone);
    c->Simulate(none, 0.1f);
    CHECK(c->stance() == St::Prone);                    // letting go of the button does not stand you up
    // Hold mode (a fresh body): prone only while the button is held. Allow Prone off: the button does nothing.
    Scene h("sim_stance_hold");
    h.physicsEnabled = false;
    auto* c2 = h.CreateGameObject("P")->AddComponent<Ctrl>();
    c2->lockCursor = false; c2->driveAnimation = false; c2->toggleStance = false;
    h.Start();
    PlayerInput held; held.held = PlayerInput::Prone;
    c2->Simulate(held, 0.1f);
    CHECK(c2->stance() == St::Prone);
    c2->Simulate(none, 0.1f);
    CHECK(c2->stance() == St::Stand);                   // released: up again
    c2->allowProne = false;
    c2->Simulate(held, 0.1f);
    CHECK(c2->stance() == St::Stand);
    std::printf("  %s: Simulate with the Prone button and the pad stance button: ok\n", who);
}

// ---- The sample scene: examples/stance/stance_course.okayscene -----------------------------------------------------
// The course runs along -Z (W walks that way at yaw 0). Metres, ground top at y = 0:
//   orange beam   z -11.5 .. -6.5, underside 1.40 m   (crouch 1.17 m fits, standing 1.80 m does not)
//   red tunnel    z -23.0 .. -17.0, roof underside 0.80 m between two walls (prone 0.59 m fits, crouch does not)
//   green pad     z about -28 (looks only)
struct CourseRun {
    Scene scene{"stance_course"};
    GameObject* player = nullptr;
    PlayerController* pc = nullptr;
    BoxCollider3D* box = nullptr;
    Rigidbody3D* rb = nullptr;
    bool ok = false;

    explicit CourseRun(bool firstPerson) {
        std::string err;
        const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/stance/stance_course.okayscene";
        ok = SceneSerializer::LoadFromFile(scene, path, &err, nullptr);
        if (!ok) std::fprintf(stderr, "    could not load %s: %s\n", path.c_str(), err.c_str());
        CHECK(ok);
        if (!ok) return;
        player = scene.Find("Player");
        pc = PlayerController::Of(player);
        box = player ? player->GetComponent<BoxCollider3D>() : nullptr;
        rb = player ? player->GetComponent<Rigidbody3D>() : nullptr;
        ok = player && pc && box && rb && pc->first() && pc->third() && pc->toggle();
        CHECK(ok);
        if (!ok) return;
        scene.Start();
        Tick({}, 90);                                   // drop to the ground and settle
        CHECK(!FirstPersonOwns(player));                // the sample starts in third person
        if (firstPerson) {
            Tap('v');                                   // the view toggle
            Tick({}, 60);
            CHECK(FirstPersonOwns(player));
        }
    }
    void Tick(const std::vector<char>& keys, int frames) {
        for (int i = 0; i < frames; ++i) { Input::FeedKeys(keys); scene.Update(kDt); }
        Input::FeedKeys({});
    }
    void Tap(char k) { Tick({k}, 1); Tick({}, 1); }
    float Z() const { return player->transform->Position().z; }
    float X() const { return player->transform->Position().x; }
    int StanceNow() const { return FirstPersonOwns(player) ? (int)pc->first()->stance() : (int)pc->third()->stance(); }
    char CrouchKey() const { return FirstPersonOwns(player) ? pc->first()->crouchKey : pc->third()->crouchKey; }
    char ProneKey() const { return FirstPersonOwns(player) ? pc->first()->proneKey : pc->third()->proneKey; }
    void PlaceAt(float z) {                             // back to the middle of the course at `z`, stance kept
        player->transform->SetPosition({0, player->transform->Position().y, z});
        rb->velocity = Vec3{0, 0, 0};
        Tick({}, 30);
    }
    // Hold W until `done()` or `maxFrames` frames. Returns the frames used.
    template <typename F> int Walk(int maxFrames, F done) {
        int n = 0;
        while (n < maxFrames && !done()) { Tick({'w'}, 1); ++n; }
        return n;
    }
};

void WalkCourse(bool firstPerson) {
    const char* who = firstPerson ? "course, first person" : "course, third person";
    CourseRun r(firstPerson);
    if (!r.ok) return;
    const float standH = r.box->size.y;
    CHECK_NEAR(standH, 1.8f, 1e-3f);
    // The keys the on-screen help names: Ctrl crouches, Z goes prone.
    CHECK(r.CrouchKey() == Input::KeyCtrl);
    CHECK(r.ProneKey() == 'z');
    CHECK(r.StanceNow() == 0);

    // 1. Standing, the orange beam (1.4 m clear) stops you at its edge.
    r.PlaceAt(-3.0f);
    r.Tick({'w'}, 300);
    std::printf("  %s: standing, walked at the beam: z %.2f (its edge is -6.5)\n", who, r.Z());
    CHECK(r.Z() < -4.0f);                               // it did walk toward the beam
    CHECK(r.Z() > -6.5f);                               // and stopped in front of it
    CHECK(r.StanceNow() == 0);

    // 2. Crouched (1.17 m), you walk under it.
    r.Tap(r.CrouchKey());
    r.Tick({}, 45);
    CHECK(r.StanceNow() == 1);
    bool lowEnough = true;
    r.Walk(600, [&] {
        if (r.Z() < -7.4f && r.Z() > -10.6f) lowEnough = lowEnough && r.box->size.y < 1.4f && r.StanceNow() == 1;
        return r.Z() < -12.5f;
    });
    std::printf("  %s: crouched under the beam: z %.2f, body %.2f\n", who, r.Z(), r.box->size.y);
    CHECK(r.Z() < -12.5f);
    CHECK(lowEnough);
    CHECK(std::fabs(r.X()) < 1.5f);

    // 3. Under the beam, a stand-up waits; outside it happens by itself.
    r.PlaceAt(-9.0f);
    CHECK(r.StanceNow() == 1);
    r.Tap(r.CrouchKey());                               // ask to stand up: no room under 1.4 m
    r.Tick({}, 60);
    CHECK(r.StanceNow() == 1);
    CHECK(r.box->size.y < 1.4f);
    r.Walk(600, [&] { return r.Z() < -12.5f; });        // crawl out the far side, still asking to stand
    r.Tick({}, 100);
    std::printf("  %s: out from under the beam: stance %d, body %.2f\n", who, r.StanceNow(), r.box->size.y);
    CHECK(r.StanceNow() == 0);
    CHECK(r.box->size.y > standH - 0.05f);

    // 4. The red tunnel (0.8 m clear): standing and crouching are both stopped at the mouth ...
    r.PlaceAt(-14.0f);
    r.Tick({'w'}, 240);
    CHECK(r.Z() < -14.5f);
    CHECK(r.Z() > -17.0f);
    r.PlaceAt(-14.0f);
    r.Tap(r.CrouchKey());
    r.Tick({}, 45);
    CHECK(r.StanceNow() == 1);
    r.Tick({'w'}, 240);
    std::printf("  %s: crouched at the tunnel: z %.2f (the mouth is -17)\n", who, r.Z());
    CHECK(r.Z() > -17.0f);
    r.PlaceAt(-14.0f);
    r.Tap(r.CrouchKey());                               // stand up again in the open
    r.Tick({}, 60);
    CHECK(r.StanceNow() == 0);

    // ... but lying prone (0.59 m) you crawl in.
    r.Tap(r.ProneKey());
    r.Tick({}, 90);
    CHECK(r.StanceNow() == 2);
    CHECK(r.box->size.y < 0.8f);
    r.Walk(1500, [&] { return r.Z() < -19.0f; });
    std::printf("  %s: crawled into the tunnel: z %.2f, body %.2f\n", who, r.Z(), r.box->size.y);
    CHECK(r.Z() < -19.0f);
    CHECK(r.StanceNow() == 2);
    CHECK(std::fabs(r.X()) < 1.5f);

    // 5. Inside, asking to stand does nothing; past the far end the stand-up happens.
    r.Tap(r.ProneKey());
    r.Tick({}, 90);
    CHECK(r.StanceNow() == 2);
    CHECK(r.box->size.y < 0.8f);
    r.Walk(1500, [&] { return r.Z() < -24.5f; });
    r.Tick({}, 120);
    std::printf("  %s: out of the tunnel: z %.2f, stance %d, body %.2f\n", who, r.Z(), r.StanceNow(), r.box->size.y);
    CHECK(r.Z() < -24.5f);
    CHECK(r.StanceNow() == 0);
    CHECK(r.box->size.y > standH - 0.05f);
}

// What the scene file itself holds: the objects the help names and a help text that tells the truth about the keys.
void CheckCourseFile() {
    Scene s("stance_course_file");
    std::string err;
    const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/stance/stance_course.okayscene";
    CHECK(SceneSerializer::LoadFromFile(s, path, &err, nullptr));
    GameObject* beam = s.Find("Crouch Beam (1.4 m clear)");
    GameObject* roof = s.Find("Crawl Tunnel Roof (0.8 m clear)");
    GameObject* help = s.Find("Instructions");
    CHECK(beam != nullptr && roof != nullptr && help != nullptr);
    if (beam) {   // underside = centre - half the scaled height
        const Vec3 p = beam->transform->Position(), sc = beam->transform->LossyScale();
        CHECK_NEAR(p.y - 0.5f * sc.y, 1.4f, 1e-3f);
    }
    if (roof) {
        const Vec3 p = roof->transform->Position(), sc = roof->transform->LossyScale();
        CHECK_NEAR(p.y - 0.5f * sc.y, 0.8f, 1e-3f);
    }
    auto* text = help ? help->GetComponent<TextRenderer>() : nullptr;
    CHECK(text != nullptr);
    if (text) {
        CHECK(text->screenSpace);
        CHECK(text->text.find("Ctrl crouches") != std::string::npos);
        CHECK(text->text.find("Z goes prone") != std::string::npos);
        CHECK(text->text.find("1.4 m") != std::string::npos && text->text.find("0.8 m") != std::string::npos);
    }
    // The body the course was sized for: 1.8 m standing, crouch 0.65 and prone 0.33 of that.
    GameObject* player = s.Find("Player");
    auto* pc = PlayerController::Of(player);
    CHECK(pc != nullptr && pc->viewMode() == PlayerController::ViewMode::Both);
    if (pc && pc->first() && pc->third()) {
        CHECK(pc->first()->allowProne && pc->third()->allowProne && pc->first()->resizeBody && pc->third()->resizeBody);
        CHECK_NEAR(pc->first()->crouchHeight, 0.65f, 1e-4f);
        CHECK_NEAR(pc->first()->proneHeight, 0.33f, 1e-4f);
        CHECK_NEAR(pc->third()->crouchHeight, 0.65f, 1e-4f);
        CHECK_NEAR(pc->third()->proneHeight, 0.33f, 1e-4f);
    }
}
} // namespace

int main() {
    RUN_SUITE("prone_ceiling");
    Check<FirstPersonController>("first person");
    Check<ThirdPersonController>("third person");
    Check<ThirdPersonShooterController>("third person shooter");
    CheckSimulatedButtons<FirstPersonController>("first person");
    CheckSimulatedButtons<ThirdPersonController>("third person");
    CheckCourseFile();
    WalkCourse(false);   // third person (the sample starts there)
    WalkCourse(true);    // first person (after V)
    TEST_MAIN_RESULT();
}
