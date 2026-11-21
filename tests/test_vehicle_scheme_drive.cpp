// VehicleController reading a control scheme: the real Input (fed keys, triggers, mouse) drives the car for N frames at
// 30, 60 and 144 fps. Analog gas, speed-sensitive steering, handbrake, camera cycle, look behind, mouse look, recentre,
// legacy scenes and scheme save/load. Headless, deterministic.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
using namespace okay;
using namespace okay::drivectl;

struct Rig {
    Scene s{"scheme"};
    GameObject* car = nullptr; VehicleController* v = nullptr; Rigidbody3D* rb = nullptr; Camera* cam = nullptr;
    explicit Rig(const char* schemeName = "Street", bool withCamera = false) {
        car = s.CreateGameObject("Car");
        rb = car->AddComponent<Rigidbody3D>(); rb->gravityScale = 0.0f;
        v = car->AddComponent<VehicleController>();
        v->ApplyPreset("Sedan"); v->SetSurfaceGrip(1.0f);
        if (schemeName) { Preset(schemeName, v->scheme); v->useScheme = true; }
        if (withCamera) {
            GameObject* c = s.CreateGameObject("Cam"); cam = c->AddComponent<Camera>(); s.mainCamera = cam;
            v->followCamera = true;
        }
        s.Start();
        Input::FeedKeys({}); Input::FeedGamepadExtra({0, 0}, 0, 0, false); Input::FeedGamepad({0, 0}, 0);
    }
    void Run(float seconds, float fps, const std::vector<char>& keys = {}) {
        const int n = (int)std::lround(seconds * fps);
        for (int i = 0; i < n; ++i) { Input::FeedKeys(keys); s.Update(1.0f / fps); }
    }
    void RunPad(float seconds, float fps, float lt, float rt, float stickX = 0.0f) {
        const int n = (int)std::lround(seconds * fps);
        for (int i = 0; i < n; ++i) { Input::FeedKeys({}); Input::FeedGamepad({stickX, 0}, 0); Input::FeedGamepadExtra({0, 0}, lt, rt, true); s.Update(1.0f / fps); }
        Input::FeedGamepadExtra({0, 0}, 0, 0, false); Input::FeedGamepad({0, 0}, 0);
    }
    void Tap(char k, float fps) { Input::FeedKeys({k}); s.Update(1.0f / fps); Input::FeedKeys({}); s.Update(1.0f / fps); }
};

int main() {
    RUN_SUITE("vehicle_scheme_drive");
    const float fpsList[3] = {30.0f, 60.0f, 144.0f};

    // --- Street: W drives forward at every frame rate; the speed after 3 s agrees within 12 % -----------------------------
    float sp[3];
    for (int i = 0; i < 3; ++i) {
        Rig r; r.Run(3.0f, fpsList[i], {'w'});
        sp[i] = r.v->Speed();
        CHECK(std::isfinite(sp[i])); CHECK(sp[i] > 6.0f); CHECK(r.car->transform->Position().z > 3.0f);
    }
    CHECK(std::fabs(sp[0] - sp[1]) < 0.12f * sp[1]); CHECK(std::fabs(sp[2] - sp[1]) < 0.12f * sp[1]);
    std::printf("  speed after 3 s of W: 30fps %.2f  60fps %.2f  144fps %.2f m/s\n", sp[0], sp[1], sp[2]);

    // --- Analog trigger: half pull gives less speed than full pull; the left trigger brakes ----------------------------
    for (int i = 0; i < 3; ++i) {
        Rig half, full;
        half.RunPad(2.0f, fpsList[i], 0.0f, 0.55f); full.RunPad(2.0f, fpsList[i], 0.0f, 1.0f);
        CHECK(half.v->Speed() > 0.5f); CHECK(half.v->Speed() < full.v->Speed() - 0.5f);
        full.RunPad(1.5f, fpsList[i], 1.0f, 0.0f);   // brake only
        CHECK(full.v->Speed() < 2.0f);
    }
    // gas and brake are separate axes: both pulled together does not behave like gas alone
    { Rig a, b; a.RunPad(2.0f, 60, 0.0f, 1.0f); b.RunPad(2.0f, 60, 0.9f, 1.0f); CHECK(b.v->Speed() < a.v->Speed()); }

    // --- Speed-sensitive steering: at speed the wheel is turned less with more steering help ------------------------------
    for (int i = 0; i < 3; ++i) {
        Rig help, none;
        help.v->scheme.steerAssist = 1.0f; none.v->scheme.steerAssist = 0.0f;
        help.Run(3.0f, fpsList[i], {'w'}); none.Run(3.0f, fpsList[i], {'w'});
        help.Run(0.6f, fpsList[i], {'w', 'd'}); none.Run(0.6f, fpsList[i], {'w', 'd'});
        CHECK(std::fabs(none.v->SteerWheelAngle()) > 1.0f);
        CHECK(std::fabs(help.v->SteerWheelAngle()) < std::fabs(none.v->SteerWheelAngle()) - 0.3f);
        CHECK(none.v->SteerWheelAngle() * help.v->SteerWheelAngle() > 0.0f);   // same direction
    }
    { Rig l, rr; l.Run(2.0f, 60, {'w'}); rr.Run(2.0f, 60, {'w'}); l.Run(0.5f, 60, {'w', 'a'}); rr.Run(0.5f, 60, {'w', 'd'});
      CHECK(l.v->SteerWheelAngle() * rr.v->SteerWheelAngle() < 0.0f); }
    // left stick steers (analog): half deflection turns the wheel less than full deflection
    { Rig h, f; h.RunPad(2.0f, 60, 0, 1.0f, 0.0f); f.RunPad(2.0f, 60, 0, 1.0f, 0.0f);
      h.RunPad(0.4f, 60, 0, 1.0f, 0.5f); f.RunPad(0.4f, 60, 0, 1.0f, 1.0f);
      CHECK(std::fabs(h.v->SteerWheelAngle()) > 0.5f); CHECK(std::fabs(h.v->SteerWheelAngle()) < std::fabs(f.v->SteerWheelAngle())); }

    // --- Handbrake: Space slows the car faster than coasting -----------------------------------------------------------------
    for (int i = 0; i < 3; ++i) {
        Rig coast, hb; coast.Run(3.0f, fpsList[i], {'w'}); hb.Run(3.0f, fpsList[i], {'w'});
        coast.Run(1.0f, fpsList[i], {}); hb.Run(1.0f, fpsList[i], {' '});
        CHECK(hb.v->Speed() < coast.v->Speed() - 1.0f);
    }

    // --- Camera: V cycles chase, far, hood, cockpit and back (Street); C is the cycle key in Classic -------------------------
    for (int i = 0; i < 3; ++i) {
        Rig r; const float f = fpsList[i];
        CHECK(r.v->CameraView() == 0); r.Run(0.1f, f);
        r.Tap('v', f); CHECK(r.v->CameraView() == 1);
        r.Tap('v', f); CHECK(r.v->CameraView() == 2);
        r.Tap('v', f); CHECK(r.v->CameraView() == 3);
        r.Tap('v', f); CHECK(r.v->CameraView() == 0);
    }
    { Rig c("Classic"); c.Run(0.1f, 60); c.Tap('v', 60); CHECK(c.v->CameraView() == 0); c.Tap('c', 60); CHECK(c.v->CameraView() == 1); }
    // horn and headlights: L toggles, H is held
    { Rig r; r.Run(0.1f, 60); CHECK(!r.v->LightsOn()); r.Tap('l', 60); CHECK(r.v->LightsOn()); r.Tap('l', 60); CHECK(!r.v->LightsOn());
      Input::FeedKeys({'h'}); r.s.Update(1.0f / 60); CHECK(r.v->HornOn()); Input::FeedKeys({}); r.s.Update(1.0f / 60); CHECK(!r.v->HornOn()); }

    // --- Look behind (C), look around (Q / E), mouse look, and recentre ---------------------------------------------------------
    for (int i = 0; i < 3; ++i) {
        Rig r; const float f = fpsList[i];
        r.Run(0.2f, f); CHECK_NEAR(r.v->CameraYawOffset(), 0.0, 1e-4);
        r.Run(0.3f, f, {'c'}); CHECK_NEAR(r.v->CameraYawOffset(), 180.0, 1e-3); CHECK(r.v->LookingBack());
        r.Run(1.0f, f);                                         // released: stays for the recentre delay (1.5 s), minus nothing yet
        CHECK(r.v->CameraYawOffset() > 100.0f);
        r.Run(8.0f, f); CHECK_NEAR(r.v->CameraYawOffset(), 0.0, 0.05); CHECK(!r.v->LookingBack());
        r.Run(0.5f, f, {'e'}); const float off = r.v->CameraYawOffset();   // 120 deg/s for 0.5 s = 60 degrees
        CHECK(off > 40.0f && off < 75.0f);
        r.Run(0.5f, f, {'q'}); CHECK(r.v->CameraYawOffset() < off - 40.0f);
    }
    {   // mouse: a move of 100 counts at 0.15 degrees per count = 15 degrees
        Rig r; r.Run(0.1f, 60);
        Input::FeedMouse({0, 0}, 0); r.s.Update(1.0f / 60); Input::FeedMouse({100, 0}, 0); r.s.Update(1.0f / 60);
        CHECK_NEAR(std::fabs(r.v->CameraYawOffset()), 15.0, 1.0);
        Rig c("Classic"); c.Run(0.1f, 60); Input::FeedMouse({0, 0}, 0); c.s.Update(1.0f / 60); Input::FeedMouse({100, 0}, 0); c.s.Update(1.0f / 60);
        CHECK_NEAR(c.v->CameraYawOffset(), 0.0, 1e-4);          // Classic has no mouse look
        Input::FeedMouse({0, 0}, 0);
    }

    // --- The chase camera follows the offset: behind the car, then in front of it while looking back --------------------------
    {
        Rig r("Street", true); r.Run(1.5f, 60);
        const float cz = r.car->transform->Position().z, camZ = r.cam->gameObject->transform->Position().z;
        CHECK(camZ < cz - 3.0f);
        r.Run(2.5f, 60, {'c'});
        CHECK(r.cam->gameObject->transform->Position().z > r.car->transform->Position().z + 3.0f);
    }

    // --- Rebinding changes what drives the car -------------------------------------------------------------------------------
    { Rig r; CHECK(Rebind(r.v->scheme, Throttle, 0, Binding::K('i'))); r.Run(1.5f, 60, {'w'}); CHECK(r.v->Speed() < 0.5f);
      r.Run(1.5f, 60, {'i'}); CHECK(r.v->Speed() > 3.0f); }
    // sensitivity / dead zone / curve are read
    { Rig a, b; b.v->scheme.act[Throttle].sensitivity = 0.4f; a.Run(2.0f, 60, {'w'}); b.Run(2.0f, 60, {'w'}); CHECK(b.v->Speed() < a.v->Speed() - 1.0f); }

    // --- Old scenes keep the old key fields; new scenes keep their scheme ----------------------------------------------------------
    {
        Scene s1("a"); GameObject* c = s1.CreateGameObject("C"); auto* v = c->AddComponent<VehicleController>();
        v->ApplyPreset("Sedan"); CHECK(v->useScheme); CHECK(v->scheme.name == "Street");
        Preset("Arcade", v->scheme); v->scheme.act[Horn].primary = Binding::K('k'); v->scheme.camDistance[1] = 13.5f;
        const std::string txt = SceneSerializer::SerializeObject(*c);
        CHECK(txt.find("vehiclecontrols ") != std::string::npos);
        Scene s2("b"); auto* w = SceneSerializer::InstantiateFromText(s2, txt)->GetComponent<VehicleController>();
        CHECK(w->useScheme); CHECK(w->scheme.name == "Arcade"); CHECK(w->scheme.act[Horn].primary == Binding::K('k')); CHECK_NEAR(w->scheme.camDistance[1], 13.5, 1e-4);
        // an old scene: no vehiclecontrols line
        std::string old; { size_t pos = 0; while (pos < txt.size()) { size_t e = txt.find('\n', pos); if (e == std::string::npos) e = txt.size(); const std::string line = txt.substr(pos, e - pos);
                                if (line.find("vehiclecontrols") == std::string::npos) old += line + "\n"; pos = e + 1; } }
        Scene s3("c"); auto* o = SceneSerializer::InstantiateFromText(s3, old)->GetComponent<VehicleController>();
        CHECK(!o->useScheme);
        o->ApplyPreset("Sedan"); o->SetSurfaceGrip(1.0f); auto* rb = o->gameObject->GetComponent<Rigidbody3D>(); if (rb) rb->gravityScale = 0.0f; else o->gameObject->AddComponent<Rigidbody3D>()->gravityScale = 0.0f;
        s3.Start(); for (int i = 0; i < 180; ++i) { Input::FeedKeys({'w'}); s3.Update(1.0f / 60); }
        CHECK(o->Speed() > 5.0f);                                 // the old W key still drives
        Input::FeedKeys({});
    }
    TEST_MAIN_RESULT();
}
