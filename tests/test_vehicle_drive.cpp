#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
#include <sstream>
using namespace okay;

// The open-world driving model: gears, brakes, drifts, presets, frame-rate independence. Deterministic (input is set
// with SetInput, not read from the keyboard). Numbers are printed so the owner can see them.
struct Rig {
    Scene s{"drive"};
    GameObject* car = nullptr; VehicleController* v = nullptr; Rigidbody3D* rb = nullptr;
    explicit Rig(const char* preset = "Sedan") {
        car = s.CreateGameObject("Car");
        rb = car->AddComponent<Rigidbody3D>(); rb->gravityScale = 0.0f;
        v = car->AddComponent<VehicleController>();
        v->ApplyPreset(preset);
        v->SetSurfaceGrip(1.0f);
        s.Start();
    }
    void Run(float seconds, float fps) { int n = (int)std::lround(seconds * fps); for (int i = 0; i < n; ++i) s.Update(1.0f / fps); }
    float Z() const { return car->transform->Position().z; }
};
static VehicleController::DriveInput Gas(float t = 1) { VehicleController::DriveInput i; i.throttle = t; return i; }
static VehicleController::DriveInput Brake() { VehicleController::DriveInput i; i.brake = 1; return i; }

int main() {
    RUN_SUITE("vehicle_drive");

    // --- Presets: every built-in applies, is sane, and survives the .okayvehicle text ---------------------------------
    for (const auto& p : VehicleTuning::Presets()) {
        VehicleTuning t; CHECK(t.ApplyPreset(p.name)); CHECK(t.openWorld);
        std::string text = t.ToFile(p.name);
        VehicleTuning u; std::string nm; CHECK(u.FromFile(text, &nm));
        CHECK(nm == p.name);
        for (const auto& d : VehicleTuning::Table()) { double a = 0, b = 0; t.GetSetting(d.name, a); u.GetSetting(d.name, b); CHECK_NEAR(a, b, 1e-4 * (1 + std::fabs(a))); }
    }
    { VehicleTuning t; CHECK(!t.ApplyPreset("Nope")); CHECK(!t.FromFile("hello 1\n")); CHECK(t.FromFile("okayvehicle 1\nmaxSpeed 99\nfutureThing 5\n")); CHECK_NEAR(t.maxSpeed, 99, 1e-4); }
    { VehicleTuning t; CHECK(t.SetSetting("maxSpeed", 99999)); CHECK_NEAR(t.maxSpeed, 10000, 1e-4); CHECK(!t.SetSetting("bogus", 1)); CHECK(!t.SetSetting("maxSpeed", std::nan(""))); }

    // --- Scene save / load keeps the tuning; an old scene (no tuning line) stays the older model --------------------
    {
        Scene s("ser"); GameObject* c = s.CreateGameObject("Car");
        auto* v = c->AddComponent<VehicleController>(); v->ApplyPreset("Drift Car"); v->gearbox = 1; v->keyHorn = 'k';
        std::string txt = SceneSerializer::SerializeObject(*c);
        CHECK(txt.find("vehicletune ") != std::string::npos);
        Scene s2("ser2"); GameObject* c2 = SceneSerializer::InstantiateFromText(s2, txt);
        auto* v2 = c2->GetComponent<VehicleController>();
        CHECK(v2->openWorld); CHECK(v2->gearbox == 1); CHECK(v2->keyHorn == 'k'); CHECK_NEAR(v2->handbrakeGrip, 0.25f, 1e-4);
        Scene s3("old"); GameObject* c3 = s3.CreateGameObject("Old"); c3->AddComponent<VehicleController>();
        std::string old = SceneSerializer::SerializeObject(*c3);
        CHECK(old.find("vehicletune") == std::string::npos);
        Scene s4("old2"); auto* v4 = SceneSerializer::InstantiateFromText(s4, old)->GetComponent<VehicleController>();
        CHECK(!v4->openWorld);
    }

    // --- 0 to 100 km/h, top speed, no NaN -----------------------------------------------------------------------
    for (const char* name : {"Sports Car", "Sedan", "Muscle Car", "Pickup", "Bus"}) {
        Rig r(name); r.v->SetInput(Gas());
        float t100 = -1; const float target = 100.0f / 3.6f;
        for (int i = 0; i < 60 * 40; ++i) { r.s.Update(1.0f / 60.0f); if (t100 < 0 && r.v->Speed() >= target) t100 = i / 60.0f; }
        std::printf("  %-11s 0-100 km/h: %s s, top speed after 40 s: %.1f m/s (setting %.1f), gear %d\n", name,
                    t100 < 0 ? "not reached" : std::to_string(t100).substr(0, 4).c_str(), r.v->Speed(), r.v->maxSpeed, r.v->Gear());
        CHECK(std::isfinite(r.v->Speed()));
        CHECK(r.v->Speed() > r.v->maxSpeed * 0.8f); CHECK(r.v->Speed() < r.v->maxSpeed * 1.1f);
        if (std::string(name) == "Sports Car") { CHECK(t100 > 0 && t100 < 8.0f); }
        if (std::string(name) == "Bus") { CHECK(t100 < 0 || t100 > 8.0f); }   // a bus is slower than a sports car
    }

    // --- Frame-rate independence --------------------------------------------------------------------------------
    {
        float zs[3], sp[3], yaws[3]; const float fps[3] = {30, 60, 144};
        for (int k = 0; k < 3; ++k) {
            Rig r("Sports Car");
            VehicleController::DriveInput in = Gas(); r.v->SetInput(in);
            r.Run(4.0f, fps[k]);
            in.steer = 0.5f; r.v->SetInput(in);
            r.Run(3.0f, fps[k]);
            zs[k] = r.Z(); sp[k] = r.v->Speed(); yaws[k] = r.car->transform->Forward().x;
        }
        std::printf("  frame rate: z %.1f / %.1f / %.1f   speed %.2f / %.2f / %.2f (30 / 60 / 144 fps)\n", zs[0], zs[1], zs[2], sp[0], sp[1], sp[2]);
        for (int k : {0, 2}) { CHECK_NEAR(sp[k], sp[1], sp[1] * 0.03f); CHECK_NEAR(zs[k], zs[1], std::fabs(zs[1]) * 0.05f + 1.0f); CHECK_NEAR(yaws[k], yaws[1], 0.08f); }
    }

    // --- Braking distance (numbers printed) ----------------------------------------------------------------------
    auto stopDist = [](const char* preset, bool abs, float grip, float from) {
        Rig r(preset); r.v->antiLock = abs; r.v->SetSurfaceGrip(grip);
        r.rb->velocity = Vec3{0, 0, from}; r.v->SetInput(Brake());
        const float z0 = r.Z(); r.Run(0.01f, 60);     // first frame picks the velocity up
        for (int i = 0; i < 60 * 30 && std::fabs(r.v->Speed()) > 0.05f; ++i) r.s.Update(1.0f / 60.0f);
        return r.Z() - z0;
    };
    {
        const float d = stopDist("Sedan", true, 1.0f, 27.8f);
        const float dIceAbs = stopDist("Sedan", true, 0.25f, 20.0f), dIceNo = stopDist("Sedan", false, 0.25f, 20.0f);
        std::printf("  braking from 100 km/h on tarmac: %.1f m. From 72 km/h on ice: ABS %.1f m, no ABS %.1f m\n", d, dIceAbs, dIceNo);
        CHECK(d > 20.0f && d < 60.0f);
        CHECK(dIceAbs < dIceNo * 0.9f);      // ABS shortens the stop on ice
        CHECK(dIceAbs > d * 0.5f);           // ice stops longer than the same speed on tarmac would
    }

    // --- All-wheel gets going better than rear-wheel on ice ------------------------------------------------------
    {
        float dist[2];
        for (int k = 0; k < 2; ++k) {
            Rig r("Sports Car"); r.v->drivetrain = k == 0 ? 0 : 2; r.v->SetSurfaceGrip(0.25f); r.v->SetInput(Gas()); r.Run(5.0f, 60);
            dist[k] = r.Z();
        }
        std::printf("  ice, 5 s flat out: rear-wheel %.1f m, all-wheel %.1f m\n", dist[0], dist[1]);
        CHECK(dist[1] > dist[0] * 1.15f);
    }

    // --- Gearbox: manual stuck in first is slower than automatic; gear changes happen ----------------------------------
    {
        Rig a("Sedan"); a.v->SetInput(Gas()); a.Run(8, 60);
        Rig m("Sedan"); m.v->gearbox = 1; m.v->SetInput(Gas()); m.Run(8, 60);
        std::printf("  8 s flat out: automatic %.1f m/s (gear %d), manual left in gear 1 %.1f m/s\n", a.v->Speed(), a.v->Gear(), m.v->Speed());
        CHECK(a.v->Gear() >= 3); CHECK(m.v->Gear() == 1); CHECK(a.v->Speed() > m.v->Speed() * 1.5f);
        CHECK(m.v->Rpm() > 0.9f * m.v->maxRpm);      // it sits on the limiter
    }

    // --- Brake to reverse, release to roll ----------------------------------------------------------------------------
    {
        Rig r("Sedan"); r.v->SetInput(Brake()); r.Run(2.0f, 60);
        CHECK(r.v->Speed() < -2.0f); CHECK(r.v->Gear() == -1); CHECK(r.Z() < -1.0f);
        r.v->SetInput(VehicleController::DriveInput{}); r.Run(6.0f, 60);
        CHECK_NEAR(r.v->Speed(), 0.0f, 0.2f);
        // moving forward, the brake brakes (it does not slam into reverse)
        Rig f("Sedan"); f.rb->velocity = Vec3{0, 0, 15}; f.v->SetInput(Brake()); f.Run(0.2f, 60);
        CHECK(f.v->Speed() > 5.0f);
    }

    // --- Steering lock shrinks with speed; steering never exceeds the grip limit; D turns right ---------------------
    {
        float rate[2];
        for (int k = 0; k < 2; ++k) {
            Rig r("Sedan"); r.rb->velocity = Vec3{0, 0, k == 0 ? 6.0f : 30.0f};
            auto in = Gas(0.5f); in.steer = 1; r.v->SetInput(in); r.Run(0.1f, 60);
            Vec3 f0 = r.car->transform->Forward(); r.Run(0.5f, 60); Vec3 f1 = r.car->transform->Forward();
            rate[k] = std::acos(std::min(1.0f, f0.x * f1.x + f0.z * f1.z)) * 57.2958f / 0.5f;
            CHECK(r.car->transform->Position().x < -0.01f);       // full right = towards -X when facing +Z
        }
        std::printf("  full steering: turns %.0f deg/s at 6 m/s, %.0f deg/s at 30 m/s\n", rate[0], rate[1]);
        CHECK(rate[1] < rate[0] * 1.0f);
        CHECK(rate[1] < 90.0f);
    }

    // --- Drift entry and exit ---------------------------------------------------------------------------------------
    {
        Rig r("Drift Car"); r.v->SetInput(Gas()); r.Run(5, 60);
        auto in = Gas(); in.steer = 0.8f; in.handbrake = true; r.v->SetInput(in);
        float maxSlip = 0; bool drifted = false;
        for (int i = 0; i < 90; ++i) { r.s.Update(1.0f / 60.0f); maxSlip = std::max(maxSlip, std::fabs(r.v->SlipAngle())); drifted = drifted || r.v->IsDrifting(); }
        in.handbrake = false; in.steer = 0; r.v->SetInput(in);
        r.Run(3.0f, 60);
        std::printf("  drift: biggest slip angle %.0f deg; after letting go: %.1f deg, speed %.1f m/s\n", maxSlip, std::fabs(r.v->SlipAngle()), r.v->Speed());
        CHECK(drifted); CHECK(maxSlip > 15.0f); CHECK(std::fabs(r.v->SlipAngle()) < 6.0f); CHECK(r.v->Speed() > 8.0f);
        // the same move in a Bus barely slides
        Rig b("Bus"); b.v->SetInput(Gas()); b.Run(5, 60); auto in2 = Gas(); in2.steer = 0.8f; in2.handbrake = true; b.v->SetInput(in2);
        float busSlip = 0; for (int i = 0; i < 90; ++i) { b.s.Update(1.0f / 60.0f); busSlip = std::max(busSlip, std::fabs(b.v->SlipAngle())); }
        CHECK(busSlip < maxSlip);
    }

    // --- Stable at speed: wiggling the wheel for a minute stays finite and bounded ------------------------------------
    {
        Rig r("Sports Car"); VehicleController::DriveInput in = Gas(); bool bad = false;
        for (int i = 0; i < 60 * 60; ++i) {
            in.steer = std::sin(i * 0.05f) * (i % 600 < 300 ? 1.0f : 0.3f); in.handbrake = (i % 500) > 470; in.boost = (i % 700) < 100;
            r.v->SetInput(in); r.s.Update(1.0f / 60.0f);
            const Vec3 p = r.car->transform->Position();
            if (!std::isfinite(p.x) || !std::isfinite(p.z) || !std::isfinite(r.v->Speed()) || std::fabs(r.v->Speed()) > 80.0f) bad = true;
        }
        CHECK(!bad);
        Rig e("Sports Car"); e.v->SetInput(Gas()); e.Run(0.0f, 60);
        for (float dt : {0.0001f, 0.5f, 2.0f}) { e.s.Update(dt); CHECK(std::isfinite(e.v->Speed())); }   // tiny and huge frames
    }

    // --- Boost raises the top speed and runs out ---------------------------------------------------------------------
    {
        Rig a("Sedan"); a.v->SetInput(Gas()); a.Run(15, 60);
        Rig b("Sedan"); auto in = Gas(); in.boost = true; b.v->SetInput(in); b.Run(15, 60);
        std::printf("  boost: top speed %.1f m/s without, %.1f with a full tank held 15 s (tank left %.2f)\n", a.v->Speed(), b.v->Speed(), b.v->BoostLeft());
        CHECK(b.v->BoostLeft() < 0.5f);
        Rig c("Sedan"); in.boost = true; c.v->SetInput(in); c.Run(2.5f, 60);
        CHECK(c.v->Speed() > a.v->Speed() * 0.0f + 10.0f);
        CHECK(c.v->BoostLeft() < 1.0f);
    }

    // --- Flip recovery ---------------------------------------------------------------------------------------------------
    {
        Rig r("Sedan"); r.Run(0.1f, 60);
        r.car->transform->localRotation = Quat::Euler(0, 0, 180);     // upside down
        r.Run(0.5f, 60); CHECK(r.v->IsFlipped());
        r.Run(2.0f, 60); CHECK(!r.v->IsFlipped()); CHECK(r.car->transform->Up().y > 0.9f);
        Rig off("Sedan"); off.v->autoFlip = false; off.Run(0.1f, 60); off.car->transform->localRotation = Quat::Euler(0, 0, 180);
        off.Run(4.0f, 60); CHECK(off.v->IsFlipped());                 // stays until the player presses recover
        off.v->Recover(); off.Run(0.1f, 60); CHECK(!off.v->IsFlipped());
    }

    // --- Lights and the older model ---------------------------------------------------------------------------------
    {
        Rig r("Sedan"); GameObject* hl = r.s.CreateGameObject("Headlight L"); hl->transform->SetParent(r.car->transform);
        r.v->SetLights(false); CHECK(!hl->active); r.v->SetLights(true); CHECK(hl->active && r.v->LightsOn());
        Scene s("classic"); GameObject* c = s.CreateGameObject("C"); c->AddComponent<Rigidbody3D>()->gravityScale = 0; auto* v = c->AddComponent<VehicleController>();
        s.Start(); CHECK(!v->openWorld);   // old scenes keep the older model
    }


    // --- Wheel spacing survives save and load for every vehicle type (the model steers by it) ----------------------
    for (const auto& sh : VehicleBuilder::Shapes()) {
        Scene a("a"); GameObject* c = VehicleBuilder::Make(a, sh.name, {0, 0, 0}, 0, Color::FromBytes(1, 2, 3), false);
        auto* v = c->GetComponent<VehicleController>();
        const float wb = v->wheelBase, tw = v->trackWidth;
        Scene b("b"); GameObject* c2 = SceneSerializer::InstantiateFromText(b, SceneSerializer::SerializeObject(*c));
        auto* v2 = c2->GetComponent<VehicleController>();
        CHECK_NEAR(v2->wheelBase, wb, 1e-3f); CHECK_NEAR(v2->trackWidth, tw, 1e-3f);
    }
    // --- Scripted pedals let go by themselves; the keyboard works again --------------------------------------------
    {
        Rig r("Sedan"); r.v->SetThrottle(0.0f); r.v->Boost(true); r.Run(1.0f, 60);
        Input::FeedKeys({'w'}); r.Run(2.0f, 60); Input::FeedKeys({});
        CHECK(r.v->Speed() > 3.0f);
        Rig k("Sedan"); k.v->SetInput(VehicleController::DriveInput{}); k.Run(1.0f, 60);   // whole-input setter holds until cleared
        Input::FeedKeys({'w'}); k.Run(1.0f, 60); Input::FeedKeys({}); CHECK(k.v->Speed() < 0.5f);
        k.v->ClearInput(); Input::FeedKeys({'w'}); k.Run(1.0f, 60); Input::FeedKeys({}); CHECK(k.v->Speed() > 2.0f);
    }
    // --- The player's driving options never change the saved settings -----------------------------------------------
    {
        Prefs::SetBool("driving.assists", false); Prefs::SetFloat("driving.steer", 1.5f); Prefs::SetBool("driving.invert", true);
        Rig r("Sports Car");
        const std::string before = SceneSerializer::SerializeObject(*r.car);
        r.v->SetInput(Gas()); r.Run(2.0f, 60);
        CHECK(SceneSerializer::SerializeObject(*r.car).substr(0, 0).empty());
        std::string after = SceneSerializer::SerializeObject(*r.car);
        auto vehLines = [](const std::string& t) { std::string o; std::istringstream in(t); std::string l; while (std::getline(in, l)) if (l.find("  vehicle") == 0) o += l + "\n"; return o; };
        CHECK(vehLines(before) == vehLines(after));
        CHECK(r.v->antiLock && r.v->steerAssist && !r.v->invertSteer);
        Prefs::Delete("driving.assists"); Prefs::Delete("driving.steer"); Prefs::Delete("driving.invert");
    }
    // --- Tilting in the air is not a flip; Recover before the first update keeps the heading --------------------------
    {
        Rig r("Buggy"); auto in = Gas(); in.steer = 1; r.v->SetInput(in); r.Run(10.0f, 60);
        CHECK(r.car->transform->Up().y > 0.5f); CHECK(!r.v->IsFlipped());
        Scene s("rec"); GameObject* c = s.CreateGameObject("C"); c->transform->localRotation = Quat::Euler(0, 90, 0);
        auto* v = c->AddComponent<VehicleController>(); v->Recover();
        CHECK_NEAR(c->transform->Forward().x, 1.0f, 1e-3f);
    }
    // --- Settings keep their old wide ranges --------------------------------------------------------------------------
    { VehicleTuning t; CHECK(t.SetSetting("maxSpeed", 250)); CHECK_NEAR(t.maxSpeed, 250, 1e-3); CHECK(t.SetSetting("turnSpeed", 20000)); CHECK_NEAR(t.turnSpeed, 20000, 1e-2); CHECK(t.SetSetting("grip", 500)); CHECK_NEAR(t.grip, 500, 1e-3); }
    // --- The camera glides the same at 30 and 144 fps ----------------------------------------------------------------
    {
        float cx[2]; const float fps[2] = {30, 144};
        for (int k = 0; k < 2; ++k) {
            Scene s("cam"); GameObject* co = s.CreateGameObject("Cam"); auto* cam = co->AddComponent<Camera>(); cam->main = true;
            co->transform->SetPosition({20, 5, 20});
            GameObject* c = s.CreateGameObject("Car"); c->AddComponent<Rigidbody3D>()->gravityScale = 0;
            auto* v = c->AddComponent<VehicleController>(); v->ApplyPreset("Sedan"); v->followCamera = true; v->SetSurfaceGrip(1);
            s.Start(); int n = (int)(1.0f * fps[k]); for (int i = 0; i < n; ++i) s.Update(1.0f / fps[k]);
            cx[k] = co->transform->Position().x;
        }
        std::printf("  camera x after 1 s: %.2f (30 fps) %.2f (144 fps)\n", cx[0], cx[1]);
        CHECK_NEAR(cx[0], cx[1], 0.5f);
    }
    TEST_MAIN_RESULT();
}
