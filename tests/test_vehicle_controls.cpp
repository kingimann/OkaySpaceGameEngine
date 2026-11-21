// Vehicle control schemes (engine/include/okay/Components/VehicleControlScheme.hpp): presets, shaping, speed-sensitive
// steering, camera recentre, enter hold, rebinding, save/load round trip and bad-input refusal. Hand-computed numbers.
#include "test_framework.hpp"
#include "okay/Components/VehicleControlScheme.hpp"
#include <limits>

using namespace okay::drivectl;

int main() {
    RUN_SUITE("vehicle_controls");

    ControlScheme cl, st, tmp;
    CHECK(Preset("Classic", cl)); CHECK(Preset("Street", st)); CHECK(!Preset("Nope", tmp));
    CHECK(PresetNames().size() == 4); CHECK(std::string(DefaultPresetName()) == "Street");
    for (const std::string& n : PresetNames()) { ControlScheme p; CHECK(Preset(n, p)); CHECK(p.name == n); ControlScheme b; CHECK(FromText(ToText(p), b)); CHECK(ToText(b) == ToText(p)); }
    ControlScheme ar, re; CHECK(Preset("Arcade", ar)); CHECK(Preset("Realistic", re));
    CHECK_NEAR(ar.steerAssist, 1.0, 1e-6); CHECK_NEAR(re.steerAssist, 0.2, 1e-6); CHECK(!re.autoGear); CHECK(ar.autoGear);
    CHECK(cl.act[Throttle].primary == Binding::K('w'));
    CHECK(cl.act[EnterExit].primary == Binding::K('e'));
    CHECK(!cl.mouseLook);
    // Street: separate analog gas and brake on the triggers, steering on the left stick, enter on F, mouse look on.
    CHECK(st.act[Throttle].pad == Binding::T(1)); CHECK(st.act[Brake].pad == Binding::T(0));
    CHECK(st.act[SteerLeft].pad == Binding::A(0, true)); CHECK(st.act[SteerRight].pad == Binding::A(0, false));
    CHECK(st.act[EnterExit].primary == Binding::K('f')); CHECK(st.mouseLook);
    CHECK(st.act[LookBack].primary == Binding::K('c'));
    for (int a = 0; a < ActionCount; ++a) { CHECK(Conflicts(cl, a, 0).empty()); CHECK(Conflicts(st, a, 0).empty()); CHECK(Conflicts(st, a, 1).empty()); CHECK(Conflicts(cl, a, 1).empty()); }
    for (int a = 0; a < ActionCount; ++a) CHECK(ActionFromName(ActionName(a)) == a);
    CHECK(ActionFromName("zzz") == -1);

    // Shape: dead zone 0.15, straight curve: 0.575 -> (0.575-0.15)/0.85 = 0.5
    ActionConfig c;
    CHECK_NEAR(Shape(0.575f, c), 0.5, 1e-5); CHECK_NEAR(Shape(0.1f, c), 0.0, 1e-6); CHECK_NEAR(Shape(1.0f, c), 1.0, 1e-6);
    c.curve = 2.0f; CHECK_NEAR(Shape(0.575f, c), 0.25, 1e-5);          // 0.5^2
    c.curve = 1.0f; c.sensitivity = 0.5f; CHECK_NEAR(Shape(1.0f, c), 0.5, 1e-6);
    c.sensitivity = 3.0f; CHECK_NEAR(Shape(0.575f, c), 1.0, 1e-6);   // 0.5*3 clamps to 1
    CHECK_NEAR(Shape(std::numeric_limits<float>::quiet_NaN(), c), 0.0, 1e-9);
    CHECK_NEAR(Shape(-4.0f, c), 0.0, 1e-9); CHECK_NEAR(Shape(9.0f, c), 1.0, 1e-9);
    // Steering: right 0.575 and no left = +0.5; invert flips; both = cancel.
    ActionConfig l, r; CHECK_NEAR(SteerValue(0, 0.575f, l, r), 0.5, 1e-5);
    CHECK_NEAR(SteerValue(0.575f, 0, l, r), -0.5, 1e-5); CHECK_NEAR(SteerValue(1, 1, l, r), 0.0, 1e-6);
    r.invert = true; CHECK_NEAR(SteerValue(0, 0.575f, l, r), -0.5, 1e-5);

    // Speed-sensitive steering: half of top speed, assist 1: smoothstep(0.5)=0.5, 1-0.7*0.5 = 0.65; top = 0.3; still = 1.
    CHECK_NEAR(SteerLimit(0, 40, 1), 1.0, 1e-6); CHECK_NEAR(SteerLimit(20, 40, 1), 0.65, 1e-5);
    CHECK_NEAR(SteerLimit(40, 40, 1), 0.3, 1e-5); CHECK_NEAR(SteerLimit(-40, 40, 1), 0.3, 1e-5);
    CHECK_NEAR(SteerLimit(40, 40, 0), 1.0, 1e-6); CHECK_NEAR(SteerLimit(400, 40, 1), 0.3, 1e-5);

    // Camera cycling.
    CHECK(NextCamera(0) == 1); CHECK(NextCamera(3) == 0); CHECK(NextCamera(-1) == 0);
    int v = 0; for (int i = 0; i < 4; ++i) v = NextCamera(v); CHECK(v == 0);
    CHECK(std::string(CameraViewName(2)) == "Hood");

    // Recentre: look 90 deg, no change until 1.5 s idle, then it eases back; look back = 180.
    CameraRecentre cr; cr.Look(90.0f);
    CHECK_NEAR(cr.Update(1.0f, false, 1.5f, 2.5f), 90.0, 1e-4);
    CHECK_NEAR(cr.Update(0.4f, false, 1.5f, 2.5f), 90.0, 1e-4);        // idle 1.4 < 1.5
    const float after = cr.Update(0.2f, false, 1.5f, 2.5f);            // idle 1.6: one step of 0.2 s: 90*exp(-0.5)
    CHECK_NEAR(after, 90.0 * std::exp(-0.5), 1e-3);
    for (int i = 0; i < 400; ++i) cr.Update(0.05f, false, 1.5f, 2.5f);
    CHECK(cr.offset == 0.0f);
    CHECK_NEAR(cr.Update(0.016f, true, 1.5f, 2.5f), 180.0, 1e-6);
    cr.Look(200.0f); CHECK_NEAR(cr.offset, 20.0, 1e-4);              // 180 + 200 = 380 wraps to 20

    // Enter key: tap = driver on release; hold 0.45 s = passenger once, nothing more on release.
    EnterHold e;
    CHECK(e.Update(true, 0.1f, 0.45f) == EnterHold::Nothing); CHECK(e.Update(false, 0.1f, 0.45f) == EnterHold::Driver);
    CHECK(e.Update(false, 0.1f, 0.45f) == EnterHold::Nothing);
    int pass = 0, drv = 0;
    for (int i = 0; i < 12; ++i) { auto r2 = e.Update(true, 0.05f, 0.45f); pass += r2 == EnterHold::Passenger; drv += r2 == EnterHold::Driver; }
    CHECK(pass == 1); CHECK(e.Update(false, 0.05f, 0.45f) == EnterHold::Nothing); CHECK(drv == 0);
    CHECK(e.Update(true, 0.1f, 0.45f) == EnterHold::Nothing);          // a new press starts fresh
    CHECK(e.Update(false, 0.1f, 0.45f) == EnterHold::Driver);

    // Rebinding: legal changes apply, wrong device / bad code is refused and changes nothing.
    ControlScheme s = st;
    CHECK(Rebind(s, Horn, 0, Binding::M(1))); CHECK(s.act[Horn].primary == Binding::M(1));
    CHECK(Rebind(s, Horn, 1, Binding::A(3, false)));
    CHECK(!Rebind(s, Horn, 0, Binding::B(2))); CHECK(!Rebind(s, Horn, 1, Binding::K('k')));
    CHECK(!Rebind(s, Horn, 0, Binding::K((char)0))); CHECK(!Rebind(s, Horn, 1, Binding::B(16)));
    CHECK(!Rebind(s, 99, 0, Binding::K('k'))); CHECK(!Rebind(s, Horn, 2, Binding::K('k')));
    CHECK(s.act[Horn].primary == Binding::M(1));
    CHECK(Rebind(s, Horn, 0, Binding()));                              // unbind is allowed
    CHECK(Rebind(s, Headlights, 0, Binding::K('h')));
    CHECK(Rebind(s, Horn, 0, Binding::K('h')));
    CHECK(Conflicts(s, Horn, 0).size() == 1 && Conflicts(s, Horn, 0)[0] == Headlights);

    // Save / load round trip for both presets and an edited scheme.
    for (const ControlScheme* p : {&cl, &st, &s}) {
        ControlScheme back; CHECK(FromText(ToText(*p), back));
        CHECK(ToText(back) == ToText(*p)); CHECK(back.name == p->name);
    }
    ControlScheme ed = st; ed.name = "My drive"; ed.act[Throttle].sensitivity = 1.7f; ed.act[SteerRight].invert = true;
    ed.act[SteerLeft].invert = true; ed.steerAssist = 0.25f; ed.autoGear = false; ed.camDistance[1] = 14.5f; ed.camFov[2] = 85.0f;
    ed.mouseLookSensitivity = 0.4f; ed.act[Brake].deadZone = 0.3f; ed.act[Brake].curve = 2.0f;
    ControlScheme back; CHECK(FromText(ToText(ed), back));
    CHECK(back.name == "My drive"); CHECK_NEAR(back.act[Throttle].sensitivity, 1.7, 1e-5); CHECK(back.act[SteerRight].invert);
    CHECK_NEAR(back.steerAssist, 0.25, 1e-6); CHECK(!back.autoGear); CHECK_NEAR(back.camDistance[1], 14.5, 1e-5);
    CHECK_NEAR(back.camFov[2], 85.0, 1e-5); CHECK_NEAR(back.act[Brake].curve, 2.0, 1e-6); CHECK_NEAR(back.mouseLookSensitivity, 0.4, 1e-6);
    // A partial file loads over the Classic defaults; a newer file with unknown words still loads.
    ControlScheme part; CHECK(FromText("okaydrivecontrols 1\nname Mini\nfutureThing 7\nsteerAssist 0.9\n", part));
    CHECK(part.name == "Mini"); CHECK_NEAR(part.steerAssist, 0.9, 1e-6); CHECK(part.act[Throttle].primary == Binding::K('w'));

    // Bad input is refused and `out` is left alone.
    const std::string good = ToText(st);
    auto bad = [&](const std::string& t) { ControlScheme o = cl; const std::string before = ToText(o); const bool ok = FromText(t, o); return !ok && ToText(o) == before; };
    CHECK(bad("")); CHECK(bad("garbage")); CHECK(bad("okaydrivecontrols 0\n")); CHECK(bad("okayvehicle 1\n"));
    CHECK(bad("okaydrivecontrols 1\nsteerAssist 2\n")); CHECK(bad("okaydrivecontrols 1\nsteerAssist nan\n"));
    CHECK(bad("okaydrivecontrols 1\nautoGear 5\n")); CHECK(bad("okaydrivecontrols 1\nrecentreRate -1\n"));
    CHECK(bad("okaydrivecontrols 1\ncamera 9 1 1 60\n")); CHECK(bad("okaydrivecontrols 1\ncamera 0 1 1 5\n"));
    CHECK(bad("okaydrivecontrols 1\naction bogus 1 119 0 0 0 0 1 0.1 1 0\n"));
    CHECK(bad("okaydrivecontrols 1\naction horn 1 119 0 0 0 0 9 0.1 1 0\n"));      // sensitivity out of range
    CHECK(bad("okaydrivecontrols 1\naction horn 1 119 0 0 0 0 1 0.1 1 3\n"));      // invert flag
    CHECK(bad("okaydrivecontrols 1\naction horn 3 4 0 0 0 0 1 0.1 1 0\n"));        // pad button in the keyboard slot
    CHECK(bad("okaydrivecontrols 1\naction horn 1 119 0 1 120 0 1 0.1 1 0\n"));    // key in the pad slot
    CHECK(bad("okaydrivecontrols 1\naction horn 1 400 0 0 0 0 1 0.1 1 0\n"));      // key code out of range
    CHECK(bad("okaydrivecontrols 1\naction horn 1 119 0\n"));                      // truncated
    CHECK(bad(good + "action horn x\n"));
    // Plain-words messages and press-a-key remapping.
    { ControlScheme o = cl; std::string why; CHECK(!FromText("nope", o, &why)); CHECK(why == "This is not a driving controls file.");
      why.clear(); CHECK(!FromText("okaydrivecontrols 1\nsteerAssist 7\n", o, &why)); CHECK(why.find("steerAssist") != std::string::npos); }
    { ControlScheme o = st; CHECK(CaptureRebind(o, Horn, 0, Binding::K('k')).empty());
      CHECK(CaptureRebind(o, Horn, 0, Binding::K('l')) == "Warning: this input is also used for headlights.");
      CHECK(o.act[Horn].primary == Binding::K('l'));
      CHECK(CaptureRebind(o, Horn, 0, Binding::B(3)) == "That input cannot be used for this slot. Nothing was changed.");
      CHECK(o.act[Horn].primary == Binding::K('l')); }
    TEST_MAIN_RESULT();
}
