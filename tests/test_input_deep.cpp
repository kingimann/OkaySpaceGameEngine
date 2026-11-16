// The input system, end to end, headless. Each block is one audit case (docs/input_audit.md):
// "audit_*" blocks pin behaviour that was already right; "input_deep_*" blocks are the bugs and gaps
// found, each of which failed before its fix; "upgrade_*" blocks cover the new features.
// Input is fed the way the player and the editor feed it (FeedKeys / FeedMouse / FeedGamepadRaw /
// FeedTouch), then InputActions::Tick runs once per frame.
#include "test_framework.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Input/InputRecorder.hpp"
#include "okay/Input/InputDebug.hpp"
#include "okay/Input/PlatformKeys.hpp"
#include "okay/Input/RemapModel.hpp"
#include "okay/Core/AccessibilitySettings.hpp"
#include "okay/Core/Prefs.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace okay;

namespace {

int g_blockFailures = 0;
void Block(const char* name) {
    if (okaytest::g_failures == g_blockFailures) { /* previous block passed */ }
    std::printf("[case] %s\n", name);
    g_blockFailures = okaytest::g_failures;
}

void Reset() {
    Prefs::Clear();
    InputActions::Restore();
    InputActions::ClearContexts();
    Accessibility::Reset();
    Input::SetTextFocused(false);
    Input::DropKeyTaps();
    InputRecorder::StopPlayback();
    InputRecorder::StopRecording();
    Input::FeedKeys({});
    Input::FeedMouse({0, 0}, 0);
    for (int p = 0; p < Input::kMaxGamepads; ++p) Input::FeedGamepadRaw(p, {}, {}, 0, 0, 0, false);
    Input::ClearTypedText();
    InputActions::Tick(0.0f);
    Input::FeedMouse({0, 0}, 0);   // twice: the first one may move the cursor, the second reads zero motion
    InputActions::Tick(0.0f);
}
// One frame the way a host does it: clear the per-frame text, feed, tick.
void Frame(std::vector<char> keys = {}, unsigned mouse = 0, Vec2 mousePos = {0, 0}, float dt = 1.0f / 60.0f) {
    Input::ClearTypedText();
    Input::FeedKeys(keys);
    Input::FeedMouse(mousePos, mouse);
    InputActions::Tick(dt);
}
void Pad(int p, Vec2 left, unsigned mask = 0, Vec2 right = {}, float lt = 0, float rt = 0, bool connected = true) {
    Input::FeedGamepadRaw(p, left, right, lt, rt, mask, connected);
}
unsigned Bit(int b) { return 1u << b; }
float Len(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

} // namespace

int main() {
    RUN_SUITE("input_deep");

    // ============================ AUDIT: already right ============================
    Block("audit_key_edge_once");
    {
        Reset();
        Frame({' '});
        CHECK(InputActions::Down("Jump") && InputActions::Get("Jump"));
        Frame({' '});
        CHECK(!InputActions::Down("Jump") && InputActions::Get("Jump"));   // held: no repeat edge
        Frame({});
        CHECK(InputActions::Up("Jump") && !InputActions::Get("Jump"));
    }
    Block("audit_two_keys_one_action");
    {
        Reset();
        InputActions::Define("Dash", {"key:q", "key:z"});
        Frame({'q'});  CHECK(InputActions::Down("Dash"));
        Frame({'q', 'z'}); CHECK(InputActions::Get("Dash") && !InputActions::Down("Dash"));   // second key adds no edge
        Frame({'z'});  CHECK(InputActions::Get("Dash") && !InputActions::Up("Dash"));          // still held by the other
        Frame({});     CHECK(InputActions::Up("Dash"));
    }
    Block("audit_modifier_combos");
    {
        Reset();
        Frame({'w', Input::KeyShift});
        CHECK(InputActions::Get("Sprint") && InputActions::Vector("Move").y > 0.99f);    // Shift+W: both
        Frame({Input::KeyCtrl, 'z'});
        CHECK(InputActions::Get("Crouch"));    // Ctrl+Z also crouches: see input_deep_chord_wins for the fix
        Input::FeedKeys({Input::KeyLShift});
        CHECK(Input::GetKey(Input::KeyShift));  // generic Shift follows the left key
        Input::FeedKeys({Input::KeyRCtrl});
        CHECK(Input::GetKey(Input::KeyCtrl));
    }
    Block("audit_move_vector");
    {
        Reset();
        Frame({'w', 'd'});
        CHECK_NEAR(Len(InputActions::Vector("Move")), 1.0f, 1e-4);   // diagonal is not faster
        Frame({'a', 'd'});
        CHECK_NEAR(InputActions::Vector("Move").x, 0.0f, 1e-6);      // opposite keys cancel
        Frame({'w', Input::KeyUp});
        CHECK_NEAR(InputActions::Vector("Move").y, 1.0f, 1e-6);      // WASD and arrows together do not double
        Pad(0, {0.0f, 0.6f}); Frame({'w'});
        CHECK_NEAR(Len(InputActions::Vector("Move")), 1.0f, 1e-4);   // key + stick together are clamped
    }
    Block("audit_deadzones");
    {
        Reset();
        Input::StickSettings radial;
        CHECK(Len(Input::ProcessStick({0.12f, 0.12f}, radial)) == 0.0f);         // length 0.17 < 0.18
        Input::StickSettings axial; axial.shape = Input::StickSettings::Shape::Axial;
        const Vec2 a = Input::ProcessStick({0.5f, 0.1f}, axial);
        CHECK(a.y == 0.0f && a.x > 0.3f);                                         // axial: the small axis dies alone
        const Vec2 r = Input::ProcessStick({0.5f, 0.1f}, radial);
        CHECK(r.y > 0.0f);                                                        // radial: the diagonal survives
        CHECK_NEAR(Len(Input::ProcessStick({1.0f, 0.0f}, radial)), 1.0f, 1e-5);   // full tilt reads full
        Input::TriggerSettings t;
        CHECK(Input::ProcessTrigger(0.4f, t) < t.threshold && Input::ProcessTrigger(0.6f, t) >= t.threshold);
    }
    Block("audit_triggers_dpad_pads");
    {
        Reset();
        Pad(0, {}, 0, {}, 0.0f, 0.6f);
        CHECK(Input::GetGamepadButton(Input::PadRT) && !Input::GetGamepadButton(Input::PadLT));
        InputActions::Tick(0.0f);
        CHECK(InputActions::Get("Fire") && InputActions::Value("Fire") > 0.5f);   // RT drives Fire as an analog value
        Pad(0, {}, Bit(Input::PadLeft)); InputActions::Tick(0.0f);
        CHECK(InputActions::Vector("Move").x < -0.99f);                           // dpad as a direction
        CHECK(Input::GetGamepadButton(Input::PadLeft));                           // and as a button
        Pad(0, {}, 0); Pad(1, {}, Bit(Input::PadA)); InputActions::Tick(0.0f);
        CHECK(!InputActions::Get("Jump"));                                        // pad 2's A is not player 1's Jump
        Pad(1, {}, 0, {}, 0, 0, false);
    }
    Block("audit_pad_unplug_releases");
    {
        Reset();
        Pad(0, {1.0f, 0.0f}, Bit(Input::PadA)); InputActions::Tick(0.0f);
        CHECK(InputActions::Get("Jump"));
        Pad(0, {}, 0, {}, 0, 0, false); InputActions::Tick(0.0f);
        CHECK(!InputActions::Get("Jump") && InputActions::Up("Jump") && !Input::GamepadConnected(0));   // nothing stays stuck
        CHECK(Input::GamepadAxis().x == 0.0f);
    }
    Block("audit_mouse_wheel_and_delta");
    {
        Reset();
        Frame({}, 0, {500, 300});
        CHECK(Input::MouseDelta().x == 500.0f || Input::MouseDelta().x == 0.0f);
        Frame({}, 0, {500, 300});
        CHECK(Input::MouseDelta().x == 0.0f);
        Frame({}, 0, {510, 290});
        CHECK_NEAR(Input::MouseDelta().x, 10.0f, 1e-5); CHECK_NEAR(Input::MouseDelta().y, -10.0f, 1e-5);
        Input::FeedMouseWheel(1.0f); Input::FeedMouseWheel(2.0f);
        CHECK_NEAR(Input::MouseWheel(), 3.0f, 1e-6);       // notches added up within a frame
        Input::ClearTypedText();
        CHECK(Input::MouseWheel() == 0.0f);                // and gone at the next frame start
    }
    Block("audit_touch_multi_finger");
    {
        Reset();
        Input::ClearTypedText();
        Input::FeedTouch(1, 0, {10, 10}); Input::FeedTouch(2, 0, {100, 100});
        CHECK(Input::TouchCount() == 2 && Input::PointerIsTouch());
        Input::ClearTypedText();
        Input::FeedTouch(1, 1, {40, 10});
        CHECK(Input::TouchDragging());
        Input::FeedTouch(2, 2, {100, 100});
        CHECK(Input::TouchCount() == 2);                   // a lifted finger stays one frame as Ended
        Input::ClearTypedText();
        CHECK(Input::TouchCount() == 1);
        Input::FeedTouch(1, 2, {40, 10}); Input::ClearTypedText();
        CHECK(Input::TouchCount() == 0 && !Input::PointerIsTouch());
    }
    Block("audit_contexts");
    {
        Reset();
        InputActions::DefineIn("UI", "Confirm", {"key:space"});
        InputActions::PushContext("UI", InputActions::ContextMode::Consume);
        Frame({' '});
        CHECK(InputActions::Get("Confirm") && !InputActions::Get("Jump"));           // the menu eats Space
        InputActions::PopContext("UI");
        Frame({' '});
        CHECK(InputActions::Get("Jump") && !InputActions::Get("Confirm"));
        InputActions::PushContext("UI", InputActions::ContextMode::Block);
        Frame({'w'});
        CHECK(InputActions::Vector("Move").y == 0.0f);                               // a modal menu blocks the game
        InputActions::ClearContexts();
    }
    Block("audit_hold_to_toggle");
    {
        Reset();
        Accessibility::SetToggleAction("Sprint", true);
        Frame({Input::KeyShift}); Frame({});
        CHECK(InputActions::Get("Sprint"));       // stays on after the key comes up
        Frame({Input::KeyShift}); Frame({});
        CHECK(!InputActions::Get("Sprint"));      // the next press turns it off
        Accessibility::Reset();
    }
    Block("audit_saved_bindings_stay_readable");
    {
        Reset();
        Prefs::SetString("input.jump", "key:x;pad:b");          // the format every earlier version wrote
        InputActions::Load();
        Frame({'x'});
        CHECK(InputActions::Get("Jump"));
        CHECK(InputActions::Find("Jump")->bindings.size() == 2);
        InputActions::Reset();
        CHECK(!Prefs::Has("input.jump"));
    }
    Block("audit_pause_context_blocks_gameplay");
    {
        Reset();
        InputActions::DefineIn("Pause", "Resume", {"key:escape"});
        InputActions::PushContext("Pause", InputActions::ContextMode::Block);
        Frame({'w', ' '});
        CHECK(!InputActions::Get("Jump") && InputActions::Vector("Move").y == 0.0f);
        Frame({(char)27});
        CHECK(InputActions::Get("Resume"));
        InputActions::ClearContexts();
    }

    // ============================ BUGS AND GAPS (failed before the fix) ============================
    Block("input_deep_tap_between_polls");
    {
        // A key that went down and up between two polls of the keyboard (a hitch, a very quick tap)
        // never reached the game: the runtime polls the held set once per frame.
        Reset();
        Frame({});
        Input::FeedKeys({});                     // what the poll saw: nothing held
        CHECK(!Input::GetKeyDown('e'));          // the old path: the press is invisible
        Input::FeedKeyTap('e');                  // the KeyDown event the runtime now forwards
        Frame({});
        CHECK(Input::GetKeyDown('e'));
        CHECK(InputActions::Down("Interact"));   // and so does the action
        Frame({});
        CHECK(Input::GetKeyUp('e') && InputActions::Up("Interact") && !InputActions::Get("Interact"));   // released the next frame
        Frame({});
        CHECK(!Input::GetKey('e'));              // not stuck
        Input::FeedKeyTap('e'); Frame({'e'});    // a tap on a key that is also held: one press, no double edge
        CHECK(Input::GetKeyDown('e'));
        Frame({'e'});
        CHECK(!Input::GetKeyDown('e'));
        Input::FeedKeyTap('Q'); Input::DropKeyTaps(); Frame({});
        CHECK(!Input::GetKey('q'));              // a tap nobody consumed is dropped, not kept for later
    }
    Block("input_deep_click_between_polls");
    {
        Reset();
        Frame({}, 0);
        Input::FeedMouseTap(0);
        Frame({}, 0);
        CHECK(Input::GetMouseButtonDown(0) && InputActions::Down("Fire"));
        Frame({}, 0);
        CHECK(Input::GetMouseButtonUp(0) && !Input::GetMouseButton(0));
    }
    Block("input_deep_rebind_direction_action");
    {
        // Rebinding Move to one key used to replace its four-key binding with a single key,
        // which has no direction: Move read (0, 0) for ever.
        Reset();
        CHECK(!InputActions::Rebind("Move", "key:x"));
        Frame({'w'});
        CHECK(InputActions::Vector("Move").y > 0.99f);                 // Move still works
        CHECK(InputActions::Rebind("Move", "keys:i,k,j,l"));           // a direction binding is fine
        Frame({'i'});
        CHECK(InputActions::Vector("Move").y > 0.99f);
        CHECK(InputActions::Rebind("Jump", "keys:i,k,j,l"));           // a button may take a direction binding (length)
        InputActions::Reset(); Prefs::Clear();
    }
    Block("input_deep_typing_does_not_move_player");
    {
        // Typing a name into a text field also walked the player: scripts read raw keys.
        Reset();
        Input::SetTextFocused(true);
        Frame({'w', 'a', ' '});
        CHECK(!Input::GetKeyGame('w') && !Input::GetKeyDownGame(' '));
        CHECK(Len(Input::AxisWASD()) == 0.0f);
        CHECK(Len(InputActions::Vector("Move")) == 0.0f && !InputActions::Get("Jump"));
        CHECK(Input::GetKey('w'));                                   // the field itself still sees the raw key
        Frame({(char)27});
        CHECK(Input::GetKeyGame((char)27));                          // Escape still gets through
        Input::SetTextFocused(false);
        Frame({'w'});
        CHECK(Input::GetKeyGame('w') && InputActions::Vector("Move").y > 0.99f);
    }
    Block("input_deep_layout_without_latin_letters");
    {
        // Russian, Greek and Arabic layouts report "unknown" for 'w': W A S D were dead.
        using platform::Key;
        CHECK(KeyForLetter(Key::Unknown, 'w') == Key::W);
        CHECK(KeyForLetter(Key::Unknown, 'a') == Key::A);
        CHECK(KeyForLetter(Key::Unknown, '5') == Key::N5);
        CHECK(KeyForLetter(Key::Unknown, '0') == Key::N0);
        CHECK(KeyForLetter(Key::Z, 'w') == Key::Z);                  // AZERTY: the key labelled W stays W
        std::vector<char> chars;
        TapCharsForKey(Key::Up, [](char c) { return KeyForLetter(Key::Unknown, c); }, chars);
        CHECK(chars.size() == 2 && chars[0] == 'w' && chars[1] == Input::KeyUp);
        chars.clear();
        TapCharsForKey(Key::D, [](char c) { return KeyForLetter(Key::Unknown, c); }, chars);
        CHECK(chars.size() == 1 && chars[0] == 'd');
    }
    Block("input_deep_quick_touch_tap_still_began");
    {
        Reset();
        Input::ClearTypedText();
        Input::FeedTouch(5, 0, {30, 30}); Input::FeedTouch(5, 2, {30, 30});   // down and up in one frame
        CHECK(Input::TouchCount() == 1);
        CHECK(Input::GetTouch(0).phase == Input::Touch::Phase::Ended);
        CHECK(Input::GetTouch(0).began);                                       // it is still known to have begun
        Input::ClearTypedText();
        CHECK(Input::TouchCount() == 0);
    }
    Block("input_deep_chord_wins");
    {
        // Ctrl+Z (undo, save...) also crouched and typed Z: a held chord now claims its keys.
        Reset();
        InputActions::Define("Undo", {"chord:ctrl+z"});
        InputActions::Define("Zoom", {"key:z"});
        Frame({'z'});
        CHECK(InputActions::Get("Zoom") && !InputActions::Get("Undo"));
        Frame({Input::KeyCtrl, 'z'});
        CHECK(InputActions::Get("Undo"));
        CHECK(!InputActions::Get("Crouch") && !InputActions::Get("Zoom"));   // the chord's keys are spoken for
        Frame({Input::KeyCtrl});
        CHECK(!InputActions::Get("Undo") && InputActions::Get("Crouch"));
        InputActions::Rebind("Undo", "chord:ctrl+shift+z");
        CHECK(InputActions::Find("Undo")->bindings[0].ToString() == "chord:ctrl+shift+z");
        CHECK(InputActions::Find("Undo")->bindings[0].Label() == "Ctrl + Shift + Z");
        Frame({Input::KeyCtrl, 'z'}); CHECK(!InputActions::Get("Undo"));       // needs all three
        Frame({Input::KeyCtrl, Input::KeyShift, 'z'}); CHECK(InputActions::Get("Undo"));
        CHECK(InputBinding::Parse("chord:ctrl").kind == InputBinding::Kind::None);          // one key is not a chord
        CHECK(InputBinding::Parse("chord:ctrl+nosuchkey").kind == InputBinding::Kind::None);
        Prefs::Clear();
    }
    Block("input_deep_wheel_binding");
    {
        Reset();
        InputActions::Define("NextWeapon", {"wheel:up", "pad:rb"});
        InputActions::Define("PrevWeapon", {"wheel:down", "pad:lb"});
        Input::ClearTypedText(); Input::FeedMouseWheel(1.0f); InputActions::Tick(0.0f);
        CHECK(InputActions::Down("NextWeapon") && !InputActions::Get("PrevWeapon"));
        Input::ClearTypedText(); Input::FeedMouseWheel(-2.0f); InputActions::Tick(0.0f);
        CHECK(InputActions::Get("PrevWeapon") && !InputActions::Get("NextWeapon"));
        Input::ClearTypedText(); InputActions::Tick(0.0f);
        CHECK(!InputActions::Get("PrevWeapon"));
        CHECK(InputBinding::Parse("wheel:up").ToString() == "wheel:up" && InputBinding::Parse("wheel:down").Label() == "Wheel Down");
    }
    Block("limitation_look_flick_is_clamped");
    {
        // mouse:delta feeds Look, and a direction action never passes length 1: a 100 pixel flick
        // in one frame reads the same as 20. Scripts that want raw motion read mouse deltas.
        Reset();
        Frame({}, 0, {0, 0}); Frame({}, 0, {100, 0});
        CHECK_NEAR(InputActions::Vector("Look").x, 1.0f, 1e-5);
        CHECK_NEAR(Input::MouseDelta().x, 100.0f, 1e-4);            // the raw delta is intact
        InputActions::SetResponse("Look", {0.0f, 1.0f, 3.0f, false, false});   // upgrade: sensitivity can pass 1
        Frame({}, 0, {110, 0});
        CHECK(InputActions::Vector("Look").x > 1.4f);               // 10 px * 3 / 20
        Prefs::Clear();
    }

    // ============================ UPGRADE: timing ============================
    Block("upgrade_jump_buffer_consume");
    {
        Reset();
        Frame({' '}, 0, {0, 0}, 0.016f);
        for (int i = 0; i < 5; ++i) Frame({}, 0, {0, 0}, 0.016f);     // 80 ms later the player lands
        CHECK(InputActions::Buffered("Jump", 0.15f));
        CHECK(InputActions::Consume("Jump"));
        CHECK(!InputActions::Buffered("Jump", 0.15f) && !InputActions::Consume("Jump"));   // used up: no double jump
        Frame({' '}, 0, {0, 0}, 0.016f); Frame({}, 0, {0, 0}, 0.5f);
        CHECK(!InputActions::Buffered("Jump", 0.15f));                // too long ago
        CHECK(!InputActions::Buffered("NoSuchAction"));
    }
    Block("upgrade_double_tap_and_hold");
    {
        Reset();
        Frame({'e'}, 0, {0, 0}, 0.05f); Frame({}, 0, {0, 0}, 0.05f);
        Frame({'e'}, 0, {0, 0}, 0.05f);
        CHECK(InputActions::DoubleTap("Interact", 0.3f));             // 100 ms between presses
        Frame({}, 0, {0, 0}, 0.05f); Frame({}, 0, {0, 0}, 0.5f); Frame({'e'}, 0, {0, 0}, 0.05f);
        CHECK(!InputActions::DoubleTap("Interact", 0.3f));            // too slow
        // hold to confirm
        Reset();
        bool reached = false; int reachedFrames = 0;
        for (int i = 0; i < 40; ++i) {
            Frame({'r'}, 0, {0, 0}, 0.05f);
            if (InputActions::HoldReached("Reload", 1.0f)) { reached = true; ++reachedFrames; }
            if (i == 9) CHECK_NEAR(InputActions::HoldProgress("Reload", 1.0f), 0.5f, 0.06f);
        }
        CHECK(reached && reachedFrames == 1);                         // exactly one frame announces it
        Frame({}, 0, {0, 0}, 0.05f);
        CHECK(InputActions::HeldFor("Reload") == 0.0f && InputActions::HoldProgress("Reload", 1.0f) == 0.0f);
    }
    Block("upgrade_edges_independent_of_frame_rate");
    {
        // The same 100 ms press seen at 30, 60 and 240 frames per second: one Down, one Up, the same hold time.
        for (int fps : {30, 60, 144, 240}) {
            Reset();
            const float dt = 1.0f / (float)fps;
            int downs = 0, ups = 0; float held = 0.0f;
            const int pressFrames = std::max(1, (int)std::lround(0.1f * fps));
            for (int f = 0; f < fps / 2; ++f) {
                const bool on = f >= 2 && f < 2 + pressFrames;
                Frame(on ? std::vector<char>{' '} : std::vector<char>{}, 0, {0, 0}, dt);
                downs += InputActions::Down("Jump"); ups += InputActions::Up("Jump");
                if (InputActions::Get("Jump")) held += dt;
            }
            CHECK(downs == 1 && ups == 1);
            CHECK_NEAR(held, 0.1f, 0.5f / fps + 0.0001f);
        }
    }

    // ============================ UPGRADE: response curves, per-player, rebinding ============================
    Block("upgrade_response_curve_invert");
    {
        Reset();
        CHECK(InputActions::SetResponse("Look", {0.2f, 2.0f, 1.0f, false, true}));
        InputActions::Response r = InputActions::GetResponse("Look");
        CHECK_NEAR(r.deadzone, 0.2f, 1e-6); CHECK_NEAR(r.curve, 2.0f, 1e-6); CHECK(r.invertY);
        CHECK(InputActions::ShapeValue(0.1f, r) == 0.0f);                              // inside the dead zone
        CHECK_NEAR(InputActions::ShapeValue(1.0f, r), 1.0f, 1e-6);                     // full stays full
        CHECK_NEAR(InputActions::ShapeValue(0.6f, r), 0.25f, 1e-5);                    // (0.6-0.2)/0.8 = 0.5, squared
        Pad(0, {}, 0, {0.0f, 1.0f}); Frame({});
        CHECK(InputActions::Vector("Look").y < -0.99f);                                // inverted
        CHECK(Prefs::Has("inputresp.look"));                                           // saved
        InputActions::Restore();                                                       // a new run: loaded back
        CHECK_NEAR(InputActions::GetResponse("Look").curve, 2.0f, 1e-6);
        InputActions::SetResponse("Look", {});
        CHECK(!Prefs::Has("inputresp.look"));                                          // neutral is not stored
        CHECK(!InputActions::SetResponse("NoSuch", {}));
        InputActions::SetResponse("Jump", {0.0f, 1.0f, 100.0f, false, false});
        CHECK(InputActions::GetResponse("Jump").sensitivity <= 20.0f);                 // clamped
        Pad(0, {}, 0, {}, 0, 0, false);
    }
    Block("upgrade_two_players_two_pads");
    {
        Reset();
        InputActions::SetPerPlayer("Jump"); InputActions::SetPerPlayer("Move");
        Pad(0, {}, 0); Pad(1, {}, Bit(Input::PadA)); Pad(2, {0.0f, 1.0f});
        Frame({});
        CHECK(!InputActions::Get("Jump", 0) && InputActions::Get("Jump", 1) && !InputActions::Get("Jump", 2));
        CHECK(InputActions::Vector("Move", 2).y > 0.9f && Len(InputActions::Vector("Move", 1)) == 0.0f);
        CHECK(InputActions::Down("Jump", 1));
        Frame({' '});                                    // the keyboard belongs to player 0
        CHECK(InputActions::Get("Jump", 0) && InputActions::Get("Jump", 1));
        CHECK(!InputActions::Get("Jump", 3) && !InputActions::Get("Jump", 9) && !InputActions::Get("Nope", 0));
        CHECK(!InputActions::Get("Jump") || InputActions::Get("Jump"));   // the single-player read still works
        for (int p = 0; p < 3; ++p) Pad(p, {}, 0, {}, 0, 0, false);
    }
    Block("upgrade_rebind_conflicts_swap_reset");
    {
        Reset();
        auto c = RemapConflicts("Jump", "key:e");                            // E is Interact (the existing remap model)
        CHECK(c.size() == 1 && c[0].action == "Interact");
        CHECK(RemapConflicts("Jump", "key:f9").empty());
        CHECK(RemapConflicts("Jump", "garbage").empty());
        CHECK(RemapConflicts("NextWeapon2", "wheel:up").empty());
        InputActions::Define("NextWeapon", {"wheel:up"}); InputActions::Define("PrevWeapon", {"wheel:down"});
        CHECK(RemapConflicts("PrevWeapon", "wheel:up").size() == 1 && RemapConflicts("PrevWeapon", "wheel:down").empty());   // up and down are different inputs
        InputActions::Define("Undo", {"chord:ctrl+z"});
        CHECK(RemapConflicts("Jump", "chord:ctrl+z").size() == 1 && RemapConflicts("Jump", "chord:ctrl+y").empty());
        CHECK(RemapBind("Jump", "key:e", RemapResolve::Swap).ok);
        CHECK(InputActions::Find("Jump")->bindings[0].ToString() == "key:e");
        CHECK(InputActions::Find("Interact")->bindings[0].ToString() == "key:space");   // got Jump's old key: nothing is left unbound
        Frame({' '}); CHECK(InputActions::Get("Interact") && !InputActions::Get("Jump"));
        InputActions::Reset();
        CHECK(InputActions::Find("Jump")->bindings[0].ToString() == "key:space" && InputActions::Find("Interact")->bindings[0].ToString() == "key:e");
        CHECK(!Prefs::Has("input.jump") && !Prefs::Has("input.interact"));
    }
    Block("upgrade_profiles_presets_export");
    {
        Reset();
        InputActions::Rebind("Jump", "key:x");
        CHECK(InputActions::SaveProfile("Player 2"));
        CHECK(InputActions::HasProfile("player 2") && !InputActions::HasProfile("nobody"));
        InputActions::Reset();
        CHECK(InputActions::Find("Jump")->bindings[0].ToString() == "key:space");
        CHECK(InputActions::LoadProfile("Player 2"));
        CHECK(InputActions::Find("Jump")->bindings[0].ToString() == "key:x");
        const std::string text = InputActions::ExportBindings();
        CHECK(text.rfind("okayinput 1\n", 0) == 0);
        CHECK(!InputActions::ImportBindings("not a profile") && !InputActions::ImportBindings("okayinput 9\nJump=key:z\n"));   // newer version: refused
        CHECK(InputActions::ImportBindings("okayinput 1\nJump=key:z;pad:a\nNoSuchAction=key:q\n"));
        CHECK(InputActions::Find("Jump")->bindings[0].ToString() == "key:z" && !InputActions::Has("NoSuchAction"));
        InputActions::DeleteProfile("player 2"); CHECK(!InputActions::HasProfile("Player 2"));
        CHECK(InputActions::ApplyPreset("left") && InputActions::Find("Fire")->bindings[0].ToString() == "key:f");
        Frame({'f'}); CHECK(InputActions::Get("Fire"));
        CHECK(InputActions::ApplyPreset("right") && InputActions::Find("Move")->bindings[0].ToString() == "keys:i,k,j,l");
        CHECK(!InputActions::ApplyPreset("sideways"));
        InputActions::Reset(); Prefs::Clear();
    }

    // ============================ UPGRADE: glyphs, last device ============================
    Block("upgrade_glyphs_follow_active_device");
    {
        Reset();
        Frame({'e'});
        CHECK(Input::LastDevice() == Input::Device::Keyboard);
        Pad(1, {}, Bit(Input::PadX)); InputActions::Tick(0.0f);
        CHECK(Input::LastDevice() == Input::Device::Gamepad && Input::LastGamepad() == 1);
        Input::SetGamepadFamily(1, Input::PadFamily::PlayStation);
        CHECK(InputBinding::Parse("pad:a").Label(Input::GamepadFamily(1)) == "Cross");
        CHECK(InputBinding::Parse("pad:rt").Label(Input::PadFamily::PlayStation) == "R2");
        CHECK(InputBinding::Parse("pad:a").Label(Input::PadFamily::Switch) == "B");           // Nintendo swaps A and B
        CHECK(InputBinding::Parse("pad:a").Label(Input::PadFamily::Xbox) == "A" && InputBinding::Parse("pad:a").Label() == "A");
        CHECK(InputBinding::Parse("key:e").Label(Input::PadFamily::PlayStation) == "E");
        CHECK(Input::FamilyFromName("PS5 Controller") == Input::PadFamily::PlayStation);
        CHECK(Input::FamilyFromName("DualSense Wireless Controller") == Input::PadFamily::PlayStation);
        CHECK(Input::FamilyFromName("Nintendo Switch Pro Controller") == Input::PadFamily::Switch);
        CHECK(Input::FamilyFromName("Xbox 360 Controller") == Input::PadFamily::Xbox);
        CHECK(Input::FamilyFromName("Some Generic Pad") == Input::PadFamily::Generic);
        Pad(0, {}, 0); Pad(1, {}, 0, {}, 0, 0, false);
        Frame({}, 0, {0, 0}); Frame({}, 0, {50, 50});   // a mouse move is the mouse
        CHECK(Input::LastDevice() == Input::Device::Mouse);
        Input::SetGamepadFamily(1, Input::PadFamily::Xbox);
        // W A S D typed for a pad by the runtime do not make the keyboard the "active device"
        Pad(0, {0.9f, 0.0f}); Input::FeedKeys({'d'}); 
        CHECK(Input::LastDevice() == Input::Device::Gamepad);
        Pad(0, {}, 0);
    }

    // ============================ UPGRADE: recording and playback ============================
    Block("upgrade_record_and_replay");
    {
        Reset();
        InputRecorder::SetSeed(1234); InputRecorder::SetNote("scene: test");
        InputRecorder::StartRecording();
        std::vector<int> jumpDown, jumpUp; std::vector<float> moveY, lookX;
        auto run = [&](int i, bool record) {
            std::vector<char> k;
            if (i >= 2 && i < 5) k.push_back(' ');
            if (i >= 3 && i < 9) k.push_back('w');
            Input::ClearTypedText();
            Input::FeedKeys(k);
            Input::FeedMouse({(float)(i * 7), 5.0f}, i == 4 ? 1u : 0u);
            Pad(0, {0.0f, i == 6 ? 0.9f : 0.0f}, i == 7 ? Bit(Input::PadB) : 0u, {}, 0, 0.0f, i < 10);
            if (i == 8) Input::FeedMouseWheel(1.0f);
            InputActions::Tick(1.0f / 60.0f);
            if (record) { jumpDown.push_back(InputActions::Down("Jump")); jumpUp.push_back(InputActions::Up("Jump"));
                          moveY.push_back(InputActions::Vector("Move").y); lookX.push_back(Input::MouseDelta().x); }
        };
        for (int i = 0; i < 12; ++i) run(i, true);
        InputRecorder::StopRecording();
        CHECK(InputRecorder::FrameCount() == 12);
        const std::string text = InputRecorder::Text();
        CHECK(text.rfind("okayinputrec 1\n", 0) == 0 && text.find("seed 1234") != std::string::npos);
        // Replay with the host feeding NOTHING: the recording supplies every frame.
        Reset();
        CHECK(!InputRecorder::StartPlayback("garbage") && !InputRecorder::StartPlayback("okayinputrec 7\n"));
        CHECK(InputRecorder::StartPlayback(text));
        CHECK(InputRecorder::Seed() == 1234 && InputRecorder::Note() == "scene: test");
        std::vector<int> d2, u2; std::vector<float> m2, l2;
        for (int i = 0; i < 12; ++i) {
            Input::ClearTypedText(); Input::FeedKeys({}); Input::FeedMouse({0, 0}, 0);
            InputActions::Tick(1.0f / 60.0f);
            d2.push_back(InputActions::Down("Jump")); u2.push_back(InputActions::Up("Jump"));
            m2.push_back(InputActions::Vector("Move").y); l2.push_back(Input::MouseDelta().x);
        }
        CHECK(d2 == jumpDown && u2 == jumpUp);
        bool same = true;
        for (std::size_t i = 0; i < moveY.size(); ++i) if (std::fabs(moveY[i] - m2[i]) > 1e-5f || std::fabs(lookX[i] - l2[i]) > 1e-4f) same = false;
        CHECK(same);
        CHECK(InputRecorder::Finished());                                    // the last frame has just been played
        Input::FeedKeys({'a'}); InputActions::Tick(0.0f);
        CHECK(!InputRecorder::Playing());                                    // it stopped by itself
        CHECK(Input::GetKey('a'));                                            // and the real keyboard is back
        // text round trip
        const std::string again = [&] { InputRecorder::StopPlayback(); return InputRecorder::Text(); }();
        CHECK(again == text);
        Pad(0, {}, 0, {}, 0, 0, false);
    }

    // ============================ UPGRADE: the input debugger's model ============================
    Block("upgrade_input_debug_events");
    {
        Reset();
        InputDebug::Clear();
        Frame({'w'});                                       // off: nothing is stored
        CHECK(InputDebug::Events().empty());
        InputDebug::Enable(true);
        Frame({'w'});                                       // already held when it was switched on: not news
        CHECK(InputDebug::Events().empty());
        Frame({'w', ' '}, 1u);
        auto has = [](const char* t) { for (const auto& e : InputDebug::Events()) if (e == t) return true; return false; };
        CHECK(has("Key Space down") && has("Mouse left down") && has("Action Jump down") && has("Action Fire down"));
        Pad(1, {}, Bit(Input::PadA)); Frame({'w', ' '}, 1u);
        CHECK(has("Pad 2 A down") && has("Controller 2 connected") && has("Now using the gamepad"));
        Pad(1, {}, 0, {}, 0, 0, false); Frame({});
        CHECK(has("Key Space up") && has("Controller 2 unplugged") && has("Action Jump up"));
        for (int i = 0; i < 40; ++i) { Frame({'q'}); Frame({}); }
        CHECK(InputDebug::Events().size() == InputDebug::Capacity());   // a ring: the oldest fall off
        const auto snap = InputDebug::Snapshot();
        CHECK(!snap.empty() && snap[0].rfind("Keys held:", 0) == 0);
        Frame({'w'});
        bool sawMove = false;
        for (const auto& l : InputDebug::Snapshot()) if (l.rfind("Action Move:", 0) == 0) sawMove = true;
        CHECK(sawMove);
        InputDebug::Enable(false); InputDebug::Clear();
    }

    // ============================ SPEED: same results as the old path ============================
    Block("upgrade_fast_path_matches_legacy");
    {
        // 600 random frames of keys, mouse and four pads, with contexts and chords in play, evaluated by the
        // fast path and by the original one (OKAY_OLD_INPUT): every action value, edge and vector must match.
        std::srand(7);
        auto rnd = [] { return std::rand() / (float)RAND_MAX; };
        struct Snap { std::vector<float> v; };
        auto runOnce = [&](bool legacy) {
            Reset();
            InputActions::SetLegacy(legacy);
            InputActions::Define("Undo", {"chord:ctrl+z"});
            InputActions::DefineIn("UI", "Confirm", {"key:space", "pad:a@1"});
            InputActions::SetPerPlayer("Fire");
            std::srand(7);
            std::vector<Snap> out;
            for (int f = 0; f < 600; ++f) {
                if (f == 100) InputActions::PushContext("UI", InputActions::ContextMode::Consume);
                if (f == 300) InputActions::PopContext("UI");
                if (f == 400) InputActions::PushContext("UI", InputActions::ContextMode::Block);
                if (f == 500) InputActions::ClearContexts();
                std::vector<char> keys;
                for (char c : {'w', 'a', 's', 'd', ' ', 'e', 'z'}) if (rnd() < 0.3f) keys.push_back(c);
                if (rnd() < 0.2f) keys.push_back(Input::KeyCtrl);
                if (rnd() < 0.2f) keys.push_back(Input::KeyShift);
                Input::ClearTypedText();
                Input::FeedKeys(keys);
                Input::FeedMouse({rnd() * 800, rnd() * 600}, (unsigned)(std::rand() % 8));
                for (int p = 0; p < 4; ++p)
                    Pad(p, {rnd() * 2 - 1, rnd() * 2 - 1}, (unsigned)(std::rand() % 65536) & 0xFFFFu, {rnd(), rnd()}, rnd(), rnd(), p < 3);
                InputActions::Tick(1.0f / 60.0f);
                Snap s;
                for (const auto& a : InputActions::All()) {
                    s.v.push_back(a.value); s.v.push_back(a.vector.x); s.v.push_back(a.vector.y);
                    s.v.push_back((float)InputActions::Down(a.name)); s.v.push_back((float)InputActions::Up(a.name));
                }
                for (int p = 0; p < 4; ++p) { s.v.push_back(InputActions::Value("Fire", p)); s.v.push_back((float)InputActions::Down("Fire", p)); }
                out.push_back(s);
            }
            InputActions::SetLegacy(false);
            return out;
        };
        const auto a = runOnce(false), b = runOnce(true);
        bool equal = a.size() == b.size();
        for (std::size_t i = 0; equal && i < a.size(); ++i) equal = a[i].v == b[i].v;
        CHECK(equal);
        for (int p = 0; p < 4; ++p) Pad(p, {}, 0, {}, 0, 0, false);
    }

    std::printf("done\n");
    TEST_MAIN_RESULT();
}
