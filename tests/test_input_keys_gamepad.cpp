// Extended keys (F1-F12, nav keys, left / right modifiers), input contexts that
// swallow inputs, and gamepad dead zones / curves / thresholds / several controllers.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

namespace {
void Keys(std::vector<char> k) { Input::FeedKeys(k); InputActions::Tick(); }
unsigned Bit(int b) { return 1u << b; }
float Len(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }
void ClearPads() {
    for (int p = 0; p < Input::kMaxGamepads; ++p) {
        Input::GamepadSettings(p) = Input::PadSettings{};
        Input::FeedGamepadRaw(p, {}, {}, 0, 0, 0, false);
        Input::FeedGamepadRaw(p, {}, {}, 0, 0, 0, false);
    }
}
}

int main() {
    RUN_SUITE("input_keys_gamepad");
    Prefs::Clear();
    InputActions::Restore();

    // ---- Extended key names ----
    {
        CHECK(Input::KeyFromName("F1") == Input::KeyF(1));      // used to fall back to 'f'
        CHECK(Input::KeyFromName("f12") == Input::KeyF12);
        CHECK(Input::KeyFromName("F5") != 'f');
        CHECK(Input::KeyFromName("f13") == 'f');                // unknown: old fallback
        CHECK(!Input::IsKnownKeyName("f13"));
        CHECK(Input::IsKnownKeyName("F9") && Input::IsKnownKeyName("PageUp") && Input::IsKnownKeyName("Left Shift"));
        CHECK(Input::KeyFromName("lshift") == Input::KeyLShift);
        CHECK(Input::KeyFromName("Right Ctrl") == Input::KeyRCtrl);
        CHECK(Input::KeyFromName("page_down") == Input::KeyPageDown);
        CHECK(Input::KeyFromName("alt") == Input::KeyAlt);
        CHECK(Input::KeyFromName("left") == Input::KeyLeft);    // arrows unchanged
        CHECK(Input::KeyFromName("shift") == Input::KeyShift);
        CHECK(Input::KeyFromName("a") == 'a' && Input::KeyFromName("A") == 'a');
        for (int n = 1; n <= 12; ++n) {
            CHECK(Input::KeyName(Input::KeyF(n)) == "F" + std::to_string(n));
            CHECK(Input::KeyFromName(Input::KeyName(Input::KeyF(n))) == Input::KeyF(n));
        }
        for (char k : {Input::KeyInsert, Input::KeyDelete, Input::KeyHome, Input::KeyEnd, Input::KeyPageUp, Input::KeyPageDown,
                       Input::KeyLShift, Input::KeyRShift, Input::KeyLCtrl, Input::KeyRCtrl, Input::KeyAlt, Input::KeyLAlt,
                       Input::KeyRAlt}) {
            CHECK(Input::KeyFromName(Input::KeyName(k)) == k);
            CHECK(Input::IsKnownKeyName(Input::KeyName(k)));
        }
    }

    // ---- Held state and the derived generic modifiers ----
    {
        Keys({});
        Keys({Input::KeyF(5)});
        CHECK(Input::GetKey(Input::KeyF(5)) && Input::GetKeyDown(Input::KeyF(5)));
        CHECK(!Input::GetKey(Input::KeyF(6)));
        CHECK(!Input::GetKey('f'));
        Keys({Input::KeyRShift});
        CHECK(Input::GetKey(Input::KeyShift));                  // either side
        CHECK(Input::GetKey(Input::KeyRShift) && !Input::GetKey(Input::KeyLShift));
        Keys({Input::KeyLCtrl, Input::KeyLAlt});
        CHECK(Input::GetKey(Input::KeyCtrl) && Input::GetKey(Input::KeyAlt) && !Input::GetKey(Input::KeyShift));
        Keys({Input::KeyShift});                                // an older runtime feeding the generic code
        CHECK(Input::GetKey(Input::KeyShift) && !Input::GetKey(Input::KeyLShift));
        Keys({Input::KeyF(1)});
        CHECK(Input::FirstKeyDown() == Input::KeyF(1));
        CHECK(Input::AnyKeyDown());
        Keys({});
    }

    // ---- Bindings: save / load round trip and old saved bindings ----
    {
        for (const char* t : {"key:f1", "key:f12", "key:left shift", "key:right ctrl", "key:page up", "key:home", "key:alt"}) {
            InputBinding b = InputBinding::Parse(t);
            CHECK(b.kind == InputBinding::Kind::Key);
            CHECK(b.ToString() == t);
            CHECK(InputBinding::Parse(b.ToString()).code == b.code);
        }
        CHECK(InputBinding::Parse("key:F1").code == (unsigned char)Input::KeyF(1));
        CHECK(InputBinding::Parse("key:f13").kind == InputBinding::Kind::None);
        CHECK(InputBinding::Parse("key:f1").Label() == "F1");
        CHECK(InputBinding::Parse("key:lshift").Label() == "Left Shift");
        // an action bound to F5 reads the key, and Rebind persists it
        InputActions::Define("QuickSave", {"key:f5"});
        Keys({});
        CHECK(!InputActions::Get("QuickSave"));
        Keys({Input::KeyF(5)});
        CHECK(InputActions::Down("QuickSave"));
        Keys({});
        CHECK(InputActions::Rebind("QuickSave", "key:f9"));
        CHECK(Prefs::GetString("input.quicksave") == "key:f9");
        Keys({Input::KeyF(5)});
        CHECK(!InputActions::Get("QuickSave"));
        Keys({Input::KeyF(9)});
        CHECK(InputActions::Get("QuickSave"));
        // old saved bindings (plain chars, keys:) still load
        Prefs::SetString("input.quicksave", "key:space;pad:a");
        InputActions::Load();
        Keys({' '});
        CHECK(InputActions::Get("QuickSave"));
        Keys({});
        // a Keys4 binding can use extended keys
        InputBinding k4 = InputBinding::Parse("keys:home,end,delete,pagedown");
        CHECK(k4.kind == InputBinding::Kind::Keys4);
        CHECK(k4.ToString() == "keys:home,end,delete,page down");
        Prefs::Clear();
        InputActions::Restore();
        // generic Shift still triggers the default Sprint binding from either side
        Keys({Input::KeyLShift});
        CHECK(InputActions::Get("Sprint"));
        Keys({});
    }

    // ---- Input contexts ----
    {
        Prefs::Clear();
        InputActions::Restore();
        InputActions::DefineIn("UI", "Submit", {"key:space", "pad:a"});
        InputActions::DefineIn("UI", "Cancel", {"key:escape", "pad:b"});
        Keys({});
        // a UI action is dead until its context is pushed
        Keys({' '});
        CHECK(InputActions::Get("Jump"));
        CHECK(!InputActions::Get("Submit"));
        CHECK(!InputActions::ContextActive("UI"));
        Keys({});

        InputActions::PushContext("UI");
        CHECK(InputActions::ContextActive("UI") && InputActions::TopContext() == "UI");
        Keys({' '});
        CHECK(InputActions::Get("Submit"));
        CHECK(!InputActions::Get("Jump"));                      // swallowed
        CHECK(Input::GetKey(' '));                              // raw input is untouched
        CHECK(InputActions::KeyConsumed(' ') && !InputActions::KeyConsumed('e'));
        Keys({'e'});                                            // keys the UI does not bind pass through
        CHECK(InputActions::Get("Interact"));
        Keys({});

        // Move's WASD does not use space, but a UI that binds W swallows just that key
        InputActions::DefineIn("UI", "Up", {"key:w"});
        Keys({'w', 'd'});
        CHECK(InputActions::Get("Up"));
        CHECK(InputActions::Vector("Move").x > 0.99f);
        CHECK(std::fabs(InputActions::Vector("Move").y) < 0.01f);   // W swallowed, D not
        Keys({});

        // controller buttons are swallowed the same way
        Input::FeedGamepadAt(0, {}, Bit(Input::PadA));
        Input::FeedKeys({}); InputActions::Tick();
        CHECK(InputActions::Get("Submit") && !InputActions::Get("Jump"));
        CHECK(InputActions::PadButtonConsumed(Input::PadA) && !InputActions::PadButtonConsumed(Input::PadA, 1));
        Input::FeedGamepadAt(0, {}, 0);

        // popping gives the keys back
        InputActions::PopContext("UI");
        Keys({' '});
        CHECK(InputActions::Get("Jump") && !InputActions::Get("Submit"));
        Keys({});

        // Passthrough does not swallow; Block swallows everything beneath
        InputActions::PushContext("UI", InputActions::ContextMode::Passthrough);
        Keys({' '});
        CHECK(InputActions::Get("Jump") && InputActions::Get("Submit"));
        Keys({});
        InputActions::PushContext("UI", InputActions::ContextMode::Block);
        Keys({'e', ' '});
        CHECK(!InputActions::Get("Interact") && !InputActions::Get("Jump") && InputActions::Get("Submit"));
        CHECK(InputActions::KeyConsumed('e'));
        Keys({});

        // release edges: Up fires when a context takes over a held key
        InputActions::ClearContexts();
        Keys({' '});
        CHECK(InputActions::Get("Jump"));
        InputActions::PushContext("UI");
        Keys({' '});
        CHECK(InputActions::Up("Jump"));
        CHECK(InputActions::Down("Submit"));
        InputActions::ClearContexts();
        Keys({});

        // nested contexts: top one wins, lower named context keeps what the top left over
        InputActions::DefineIn("Dialog", "Confirm", {"key:space"});
        InputActions::PushContext("UI");
        InputActions::PushContext("Dialog");
        CHECK(InputActions::TopContext() == "Dialog");
        Keys({' '});
        CHECK(InputActions::Get("Confirm") && !InputActions::Get("Submit") && !InputActions::Get("Jump"));
        InputActions::PopContext("dialog");                      // not case sensitive
        Keys({' '});
        CHECK(InputActions::Get("Submit") && !InputActions::Get("Confirm"));
        InputActions::ClearContexts();
        CHECK(InputActions::SetContext("Submit", ""));
        Keys({' '});
        CHECK(InputActions::Get("Submit"));                     // moved to the base context
        Keys({});
        InputActions::Restore();
        CHECK(InputActions::TopContext().empty());
    }

    // ---- Action press threshold ----
    {
        InputActions::Restore();
        ClearPads();
        InputActions::SetThreshold("Fire", 0.2f);
        Input::FeedGamepadExtraAt(0, {}, 0, 0.3f, true);
        Input::FeedKeys({}); InputActions::Tick();
        CHECK(InputActions::Get("Fire"));
        InputActions::SetThreshold("Fire", 0.8f);
        Input::FeedKeys({}); InputActions::Tick();
        CHECK(!InputActions::Get("Fire"));
        InputActions::Restore();
        ClearPads();
    }

    // ---- Dead zones, outer zone, curves ----
    {
        Input::StickSettings s;                                 // radial 0.18
        CHECK(Len(Input::ProcessStick({0.1f, 0.1f}, s)) == 0.0f);   // |0.14| < 0.18
        CHECK(Len(Input::ProcessStick({0.17f, 0.0f}, s)) == 0.0f);
        const Vec2 full = Input::ProcessStick({0.0f, 1.0f}, s);
        CHECK_NEAR(full.y, 1.0f, 1e-5);
        const Vec2 mid = Input::ProcessStick({0.59f, 0.0f}, s);     // (0.59-0.18)/0.82 = 0.5
        CHECK_NEAR(mid.x, 0.5f, 1e-4);
        // radial keeps the direction
        const Vec2 dia = Input::ProcessStick({0.5f, 0.5f}, s);
        CHECK_NEAR(dia.x, dia.y, 1e-6);
        CHECK(dia.x > 0.0f);

        // axial: each axis on its own, so a small off-axis drift is removed
        Input::StickSettings ax; ax.shape = Input::StickSettings::Shape::Axial; ax.deadZone = 0.2f;
        const Vec2 drift = Input::ProcessStick({0.9f, 0.15f}, ax);
        CHECK(drift.y == 0.0f && drift.x > 0.8f);
        const Vec2 radialDrift = Input::ProcessStick({0.9f, 0.15f}, Input::StickSettings{Input::StickSettings::Shape::Radial, 0.2f, 1.0f, 1.0f});
        CHECK(radialDrift.y > 0.0f);                            // radial keeps the small component
        CHECK_NEAR(Input::ProcessStick({-0.6f, 0.0f}, ax).x, -0.5f, 1e-4);

        // outer zone: a stick that only reaches 0.9 can still read 1
        Input::StickSettings worn; worn.deadZone = 0.1f; worn.outerZone = 0.9f;
        CHECK_NEAR(Input::ProcessStick({0.9f, 0.0f}, worn).x, 1.0f, 1e-5);
        CHECK_NEAR(Input::ProcessStick({1.0f, 0.0f}, worn).x, 1.0f, 1e-5);   // never above 1

        // response curve: quadratic gives finer control near the centre
        Input::StickSettings q; q.deadZone = 0.0f; q.curve = 2.0f;
        CHECK_NEAR(Input::ProcessStick({0.5f, 0.0f}, q).x, 0.25f, 1e-5);
        CHECK_NEAR(Input::ProcessStick({0.0f, -1.0f}, q).y, -1.0f, 1e-5);

        // nonsense settings do not blow up
        Input::StickSettings bad; bad.deadZone = 5.0f; bad.outerZone = -1.0f; bad.curve = -3.0f;
        const Vec2 b = Input::ProcessStick({1.0f, 0.0f}, bad);
        CHECK(std::isfinite(b.x) && b.x >= 0.0f && b.x <= 1.0f);

        Input::TriggerSettings t; t.deadZone = 0.2f;
        CHECK(Input::ProcessTrigger(0.1f, t) == 0.0f);
        CHECK_NEAR(Input::ProcessTrigger(0.6f, t), 0.5f, 1e-5);
        CHECK_NEAR(Input::ProcessTrigger(1.5f, t), 1.0f, 1e-5);
    }

    // ---- Raw feeding goes through the per-controller settings ----
    {
        ClearPads();
        Input::FeedGamepadRaw(0, {0.1f, 0.0f}, {0.0f, 0.1f}, 0.0f, 0.0f, 0, true);
        CHECK(Input::GamepadAxis().x == 0.0f && Input::GamepadRightAxis().y == 0.0f);   // inside the default dead zone
        Input::GamepadSettings(0).left.deadZone = 0.0f;
        Input::FeedGamepadRaw(0, {0.1f, 0.0f}, {0.0f, 0.1f}, 0.0f, 0.0f, 0, true);
        CHECK_NEAR(Input::GamepadAxis().x, 0.1f, 1e-5);
        CHECK(Input::GamepadRightAxis().y == 0.0f);             // right stick has its own settings

        // trigger threshold -> the LT / RT button bits
        Input::GamepadSettings(0).triggers.threshold = 0.3f;
        Input::FeedGamepadRaw(0, {}, {}, 0.35f, 0.25f, 0, true);
        CHECK(Input::GetGamepadButton(Input::PadLT) && !Input::GetGamepadButton(Input::PadRT));
        CHECK_NEAR(Input::GamepadTrigger(0), 0.35f, 1e-5);
        Input::GamepadSettings(0).triggers.threshold = 0.9f;
        Input::FeedGamepadRaw(0, {}, {}, 0.35f, 0.95f, 0, true);
        CHECK(!Input::GetGamepadButton(Input::PadLT) && Input::GetGamepadButton(Input::PadRT));
        // trigger dead zone
        Input::GamepadSettings(0).triggers.deadZone = 0.4f;
        Input::FeedGamepadRaw(0, {}, {}, 0.35f, 0.0f, 0, true);
        CHECK(Input::GamepadTrigger(0) == 0.0f);
        // edges
        Input::GamepadSettings(0) = Input::PadSettings{};
        Input::FeedGamepadRaw(0, {}, {}, 0.0f, 0.0f, 0, true);
        Input::FeedGamepadRaw(0, {}, {}, 0.0f, 0.8f, 0, true);
        CHECK(Input::GetGamepadButtonDown(Input::PadRT));
        Input::FeedGamepadRaw(0, {}, {}, 0.0f, 0.0f, 0, true);
        CHECK(Input::GetGamepadButtonUp(Input::PadRT));
        ClearPads();
    }

    // ---- Several controllers ----
    {
        ClearPads();
        CHECK(Input::GamepadCount() == 0 && !Input::GamepadConnected(0));
        Input::FeedGamepadRaw(0, {0.0f, 1.0f}, {}, 0, 0, Bit(Input::PadA), true);
        Input::FeedGamepadRaw(1, {1.0f, 0.0f}, {-1.0f, 0.0f}, 0, 1.0f, Bit(Input::PadB), true);
        CHECK(Input::GamepadCount() == 2);
        CHECK(Input::GamepadConnected(0) && Input::GamepadConnected(1) && !Input::GamepadConnected(2));
        CHECK(Input::GamepadAxis(0).y > 0.99f && Input::GamepadAxis(0).x == 0.0f);
        CHECK(Input::GamepadAxis(1).x > 0.99f && Input::GamepadAxis(1).y == 0.0f);
        CHECK(Input::GamepadRightAxis(1).x < -0.99f && Input::GamepadRightAxis(0).x == 0.0f);
        CHECK(Input::GetGamepadButton(Input::PadA, 0) && !Input::GetGamepadButton(Input::PadA, 1));
        CHECK(Input::GetGamepadButton(Input::PadB, 1) && !Input::GetGamepadButton(Input::PadB, 0));
        CHECK(Input::GamepadTrigger(1, 1) > 0.99f && Input::GamepadTrigger(1, 0) == 0.0f);
        CHECK(Input::GetGamepadButtonDown(Input::PadB, 1) && !Input::GetGamepadButtonDown(Input::PadB, 0));
        CHECK(Input::FirstGamepadButtonDown(1) == Input::PadB);
        // out-of-range players read as empty and ignore feeds
        CHECK(!Input::GamepadConnected(-1) && !Input::GamepadConnected(99));
        CHECK(Len(Input::GamepadAxis(7)) == 0.0f && !Input::GetGamepadButton(Input::PadA, 7));
        Input::FeedGamepadRaw(9, {1, 1}, {}, 0, 0, 0xFFFFu, true);
        CHECK(Input::GamepadCount() == 2);
        // existing single-pad calls are player 0
        CHECK(Input::GamepadAxis().y > 0.99f && Input::GamepadConnected());
        // per-controller settings are independent
        Input::GamepadSettings(1).left.deadZone = 0.5f;
        CHECK(Input::GamepadSettings(0).left.deadZone == 0.18f);
        Input::GamepadSettings(1) = Input::PadSettings{};

        // rumble goes to the controller it was asked for
        Input::Rumble(0.5f, 0.5f, 1.0f, 1);
        float lo, hi, sec;
        CHECK(!Input::TakeRumble(lo, hi, sec, 0));
        CHECK(Input::TakeRumble(lo, hi, sec, 1) && lo == 0.5f && sec == 1.0f);
        CHECK(!Input::TakeRumble(lo, hi, sec, 1));
        Input::Rumble(1.0f, 0.0f, 0.1f);                        // default: first controller
        CHECK(Input::TakeRumble(lo, hi, sec));
        Input::Rumble(1.0f, 0.0f, 0.1f, 8);                     // bad slot: ignored
        CHECK(!Input::TakeRumble(lo, hi, sec, 8));

        // bindings name their controller
        InputBinding b1 = InputBinding::Parse("pad:a@1");
        CHECK(b1.kind == InputBinding::Kind::PadButton && b1.player == 1 && b1.ToString() == "pad:a@1");
        CHECK(b1.Label() == "A (Pad 2)");
        CHECK(InputBinding::Parse("pad:a").player == 0 && InputBinding::Parse("pad:a").ToString() == "pad:a");
        CHECK(InputBinding::Parse("pad:a@0").ToString() == "pad:a");
        CHECK(InputBinding::Parse("stick:left@2").ToString() == "stick:left@2");
        CHECK(InputBinding::Parse("dpad@1").ToString() == "dpad@1");
        CHECK(InputBinding::Parse("pad:rt@3").player == 3);
        CHECK(InputBinding::Parse("pad:a@4").kind == InputBinding::Kind::None);
        CHECK(InputBinding::Parse("pad:a@x").kind == InputBinding::Kind::None);
        CHECK(InputBinding::Parse("key:@").kind == InputBinding::Kind::Key);   // '@' as a key is still a key
        CHECK(InputBinding::Parse("key:space@1").kind == InputBinding::Kind::None);

        // two players, two action sets
        InputActions::Define("P1Jump", {"key:space", "pad:a"});
        InputActions::Define("P2Jump", {"pad:a@1"});
        InputActions::Define("P2Move", {"stick:left@1", "dpad@1"}, true);
        Input::FeedKeys({});
        InputActions::Tick();
        CHECK(InputActions::Get("P1Jump") && !InputActions::Get("P2Jump"));
        CHECK(InputActions::Vector("P2Move").x > 0.99f);
        CHECK(InputActions::Vector("Move").x == 0.0f);          // the default Move reads controller 0
        Input::FeedGamepadRaw(1, {}, {}, 0, 0, Bit(Input::PadA) | Bit(Input::PadLeft), true);
        InputActions::Tick();
        CHECK(InputActions::Get("P2Jump"));
        CHECK(InputActions::Vector("P2Move").x < -0.99f);
        // a rebind to player 1 persists with its @1
        Prefs::Clear();
        CHECK(InputActions::Rebind("Interact", "pad:y@1"));
        CHECK(Prefs::GetString("input.interact") == "key:e;pad:y@1");
        InputActions::Restore();
        Prefs::Clear();
        ClearPads();
    }

    Prefs::Clear();
    InputActions::Restore();
    TEST_MAIN_RESULT();
}
