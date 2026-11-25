// Input pieces that existed but were not connected (claim claude/input-wiring-1). One section per piece:
//   1 a built game loads the project's Assets/input.okayinput (the player's start-up, and the export ships the file)
//   2 a bound gesture drives its action with no manual Gestures::Apply call; several actions per gesture; "gesture:" bindings
//   3 mouse side buttons (X1, X2) and the sideways wheel reach Input and can be bound
//   4 the controller's Guide (Xbox / PS / Home) button reaches Input and can be bound
//   5 punctuation, numpad, CapsLock, PrintScreen, Pause, NumLock and Menu have names and codes; old codes stay
//   6 eight controllers, so players 5 to 8 get a pad
//   7 a controller connecting or leaving shows a toast through the UI the host installed
//   8 the arrows can be bound apart from W A S D (raw reads still see an arrow as its letter)
// player/main.cpp and editor/BuildGame.cpp cannot be linked into a test, so their one-line calls are guarded by source
// (as test_editor_play_input does); everything behind those calls is run here for real.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/BuildPipeline.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Input/Gestures.hpp"
#include "okay/Input/InputAsset.hpp"
#include "okay/Input/InputRuntime.hpp"
#include "okay/Input/PlatformGamepad.hpp"
#include "okay/Input/PlatformKeys.hpp"
#include "okay/Scripting/ScriptUI.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace okay;
namespace fs = std::filesystem;
namespace pf = okay::platform;

namespace {

std::string Slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str();
}
bool Has(const std::string& text, const char* what) { return text.find(what) != std::string::npos; }

struct FakePad final : pf::Gamepad {
    int id = 0;
    bool btn[32] = {};
    int InstanceId() const override { return id; }
    const char* Name() const override { return "Xbox Wireless Controller"; }
    int16_t Axis(pf::GamepadAxis) const override { return 0; }
    bool Button(pf::GamepadButton b) const override { return btn[(int)b]; }
    bool Rumble(uint16_t, uint16_t, uint32_t) override { return true; }
    bool HasBuiltinMapping() const override { return true; }
};

struct ToastUI final : ScriptUIBridge {
    std::vector<std::string> toasts;
    void Toast(const char* s, int, float) override { toasts.push_back(s ? s : ""); }
};

void QuietInput() {
    Input::ClearTypedText();
    Input::FeedKeys({}); Input::FeedKeys({});
    Input::FeedMouse({0, 0}, 0); Input::FeedMouse({0, 0}, 0);
    for (int p = 0; p < Input::kMaxGamepads; ++p) { Input::FeedGamepadRaw(p, {}, {}, 0, 0, 0, false); Input::FeedGamepadRaw(p, {}, {}, 0, 0, 0, false); }
}

// An upward swipe of one finger, ticked the way the player and the editor's Play do (Gestures::Tick, then InputActions::Tick).
void SwipeUp(double t0, bool tickActions) {
    Input::FeedTouch(3, 0, {80, 400}); Gestures::Tick(t0);        if (tickActions) InputActions::Tick(0.016f); Input::ClearTypedText();
    Input::FeedTouch(3, 1, {80, 120}); Gestures::Tick(t0 + 0.10); if (tickActions) InputActions::Tick(0.016f); Input::ClearTypedText();
    Input::FeedTouch(3, 2, {80, 90});  Gestures::Tick(t0 + 0.12);
}

// ---- 1 -------------------------------------------------------------------------------------------------------------
void TestProjectAssetInBuiltGame() {
    RUN_SUITE("1 a built game loads the project's input asset");
    Prefs::Clear(); InputRuntime::Reset(); InputActions::Restore(); QuietInput();
    const fs::path root = fs::temp_directory_path() / "okay_input_wiring_game";
    fs::remove_all(root);
    // The project: its input asset has an action the default asset lacks.
    const fs::path proj = root / "proj";
    fs::create_directories(proj / "Assets");
    {
        ActionSet a = DefaultInputAsset();
        a.Add("Zoom", ValueType::Button, "Gameplay").AddBinding("key:z", "Keyboard+Mouse");
        CHECK(SaveInputAsset((proj / "Assets" / "input.okayinput").string(), a));
    }
    // The export ships it where the game looks: <data>/Assets/input.okayinput.
    namespace bp = okay::buildpipe;
    bp::WalkSettings ws; ws.projectDir = proj.generic_string();
    const bp::WalkResult walk = bp::WalkAssets({{"Assets/input.okayinput", "the project's input asset"}}, ws);
    CHECK(walk.assets.size() == 1 && walk.assets[0].dest == "Assets/input.okayinput");
    const fs::path data = root / "out" / "Game_Data";
    fs::create_directories(data / "Assets");
    { std::ofstream(data / "game.okayconfig") << "title=Game\n"; }
    if (!walk.assets.empty()) fs::copy_file(walk.assets[0].src, data / fs::path(walk.assets[0].dest));
    // The player's start-up: find the data folder, then the project's asset and the player's saved bindings.
    const std::string baseDir = bp::FindDataDir((root / "out").generic_string() + "/", "Game");
    CHECK(baseDir == data.generic_string() + "/");
    CHECK(InputRuntime::UseProjectAsset(baseDir));
    InputActions::Load();
    CHECK(InputActions::Has("Zoom") && InputActions::Has("Move"));
    Input::FeedKeys({'z'}); InputActions::Tick(0.016f);
    CHECK(InputActions::Get("Zoom"));
    QuietInput();
    // A game with no asset of its own: the default one, and no error.
    fs::remove(data / "Assets" / "input.okayinput");
    CHECK(!InputRuntime::UseProjectAsset(baseDir));
    InputActions::Load();
    CHECK(!InputActions::Has("Zoom") && InputActions::Has("Jump"));
    // The two host lines this section stands for.
    const std::string player = Slurp(std::string(OKAY_SOURCE_DIR) + "/player/main.cpp");
    const std::string build = Slurp(std::string(OKAY_SOURCE_DIR) + "/editor/BuildGame.cpp");
    CHECK(Has(player, "InputRuntime::UseProjectAsset(baseDir)"));      // the player loads it at start-up
    CHECK(Has(build, "InputRuntime::kProjectAssetRef"));                // the export ships it even when no scene names it
    fs::remove_all(root);
    InputRuntime::Reset(); InputActions::Restore(); Prefs::Clear(); QuietInput();
}

// ---- 2 -------------------------------------------------------------------------------------------------------------
void TestGestureBindings() {
    RUN_SUITE("2 a bound gesture drives its action");
    Prefs::Clear(); InputRuntime::Reset(); InputActions::Restore(); QuietInput();
    Gestures::Reset(); Gestures::ClearBinds();
    InputActions::Define("Dash", {"key:q"});
    InputActions::Tick(0.016f);
    // Two actions on one gesture: the second Bind used to replace the first.
    Gestures::Bind("swipe up", "Jump");
    Gestures::Bind("Swipe_Up", "Dash");
    Gestures::Bind("swipe up", "Jump");   // the same pair again adds nothing
    SwipeUp(1.0, true);
    CHECK(Gestures::Happened("swipe up"));
    InputActions::Tick(0.016f);           // no Gestures::Apply here: the tick that reads the gesture also drives the actions
    CHECK(InputActions::Down("Jump"));
    CHECK(InputActions::Down("Dash"));
    Input::ClearTypedText(); Gestures::Tick(1.20); InputActions::Tick(0.016f);
    CHECK(!InputActions::Get("Jump") && !InputActions::Get("Dash"));   // a gesture is one frame
    // A gesture as a binding of the asset, so it is saved and loaded with it.
    Gestures::ClearBinds(); Gestures::Reset();
    const InputBinding g = InputBinding::Parse("gesture:swipe up");
    CHECK(g.kind != InputBinding::Kind::None);
    CHECK(g.ToString() == "gesture:swipeup");
    CHECK(g.Label() == "Swipe Up");
    CHECK(InputBinding::Parse("gesture:double tap").ToString() == "gesture:doubletap");
    CHECK(InputBinding::Parse("gesture:wiggle").kind == InputBinding::Kind::None);
    {
        ActionSet a = DefaultInputAsset();
        a.Add("Vault", ValueType::Button, "Gameplay").AddBinding("gesture:swipeup", "Touch");
        ActionSet back;
        CHECK(ParseInputAsset(SerializeInputAsset(a), back));
        const ActionDef* v = back.Find("Vault");
        CHECK(v && v->bindings.size() == 1 && v->bindings[0].parts.size() == 1 && v->bindings[0].parts[0] == "gesture:swipeup");
        back.SetAllSchemes(true);
        Input::ClearTypedText();
        Input::FeedTouch(3, 0, {80, 400}); Gestures::Tick(5.0);  back.Update(0.016f); Input::ClearTypedText();
        Input::FeedTouch(3, 1, {80, 120}); Gestures::Tick(5.10); back.Update(0.016f); Input::ClearTypedText();
        Input::FeedTouch(3, 2, {80, 90});  Gestures::Tick(5.12); back.Update(0.016f);
        CHECK(back.Pressed("Vault"));
    }
    // Unbind takes one action, or every action of the gesture; an unknown gesture is refused.
    CHECK(!Gestures::Bind("wiggle", "Jump") && !Gestures::Bind("tap", ""));
    CHECK(Gestures::Bind("swipe up", "Jump") && Gestures::Bind("swipe up", "Dash"));
    Gestures::Unbind("swipe up", "Jump");
    Input::ClearTypedText(); Gestures::Reset();
    SwipeUp(9.0, true);
    InputActions::Tick(0.016f);
    CHECK(!InputActions::Down("Jump") && InputActions::Down("Dash"));
    InputActions::Tick(0.016f);           // a second action tick with no new gesture tick does not press it again
    CHECK(!InputActions::Get("Dash"));
    Input::ClearTypedText(); Gestures::Reset(); Gestures::ClearBinds();
    InputRuntime::Reset(); InputActions::Restore(); Prefs::Clear(); QuietInput();
}

// ---- 3 -------------------------------------------------------------------------------------------------------------
void TestMouseSideButtonsAndWheel() {
    RUN_SUITE("3 mouse side buttons and the sideways wheel");
    QuietInput();
    Input::FeedMouse({10, 10}, 1u << 3);
    CHECK(Input::GetMouseButton(3) && Input::GetMouseButtonDown(3) && !Input::GetMouseButton(4));
    Input::FeedMouse({10, 10}, 1u << 4);
    CHECK(Input::GetMouseButtonUp(3) && Input::GetMouseButtonDown(4));
    Input::FeedMouse({10, 10}, 0);
    Input::FeedMouseTap(4);                       // a side click shorter than a frame still lands
    Input::FeedMouse({10, 10}, 0);
    CHECK(Input::GetMouseButtonDown(4));
    Input::FeedMouse({10, 10}, 0);
    CHECK(!Input::GetMouseButton(5) && !Input::GetMouseButton(-1));   // out of range stays false
    const InputBinding x1 = InputBinding::Parse("mouse:x1"), x2 = InputBinding::Parse("mouse:x2");
    CHECK(x1.kind == InputBinding::Kind::Mouse && x1.code == 3 && x1.ToString() == "mouse:x1" && x1.Label() == "Mouse 4");
    CHECK(x2.kind == InputBinding::Kind::Mouse && x2.code == 4 && x2.ToString() == "mouse:x2" && x2.Label() == "Mouse 5");
    CHECK(InputBinding::Parse("mouse:left").code == 0 && InputBinding::Parse("mouse:middle").code == 2);   // the old three are unchanged
    Input::FeedMouse({10, 10}, 1u << 3);
    CHECK(x1.Value() == 1.0f && x2.Value() == 0.0f);
    Input::FeedMouse({10, 10}, 0);
    const InputBinding wl = InputBinding::Parse("wheel:left"), wr = InputBinding::Parse("wheel:right");
    CHECK(wl.kind == InputBinding::Kind::Wheel && wr.kind == InputBinding::Kind::Wheel);
    CHECK(wl.ToString() == "wheel:left" && wr.ToString() == "wheel:right" && wl.Label() == "Wheel Left" && wr.Label() == "Wheel Right");
    CHECK(InputBinding::Parse("wheel:up").ToString() == "wheel:up" && InputBinding::Parse("wheel:down").ToString() == "wheel:down");
    // One notch sideways is one frame of the binding, and a new frame clears it.
    Input::ClearTypedText();
    Input::FeedMouseWheelX(1.0f);
    CHECK(Input::MouseWheelX() == 1.0f && wr.Value() == 1.0f && wl.Value() == 0.0f && Input::MouseWheel() == 0.0f);
    Input::ClearTypedText();
    Input::FeedMouseWheelX(-2.0f);
    CHECK(wl.Value() == 1.0f && wr.Value() == 0.0f);
    Input::ClearTypedText();
    CHECK(Input::MouseWheelX() == 0.0f && wl.Value() == 0.0f);
    // The platform layer's buttons as Input's: the mask the player feeds every frame and the button of a click event.
    CHECK(InputMouseMask(pf::MouseMask(pf::MouseLeft) | pf::MouseMask(pf::MouseX1)) == ((1u << 0) | (1u << 3)));
    CHECK(InputMouseMask(pf::MouseMask(pf::MouseRight) | pf::MouseMask(pf::MouseMiddle) | pf::MouseMask(pf::MouseX2)) == ((1u << 1) | (1u << 2) | (1u << 4)));
    CHECK(InputMouseButton(pf::MouseLeft) == 0 && InputMouseButton(pf::MouseRight) == 1 && InputMouseButton(pf::MouseMiddle) == 2);
    CHECK(InputMouseButton(pf::MouseX1) == 3 && InputMouseButton(pf::MouseX2) == 4 && InputMouseButton(9) == -1);
    // The player feeds them from the platform layer.
    const std::string player = Slurp(std::string(OKAY_SOURCE_DIR) + "/player/main.cpp");
    CHECK(Has(player, "Input::FeedMouse(s_virtualMouse, okay::InputMouseMask(mb))"));
    CHECK(Has(player, "Input::FeedMouseTap(okay::InputMouseButton(e.button))"));
    CHECK(Has(player, "Input::FeedMouseWheelX("));
    QuietInput();
}

// ---- 4 -------------------------------------------------------------------------------------------------------------
void TestGuideButton() {
    RUN_SUITE("4 the Guide button");
    QuietInput();
    const int guide = Input::GamepadButtonFromName("guide");
    CHECK(guide >= 0);
    CHECK(Input::GamepadButtonFromName("home") == guide && guide >= 0);
    CHECK(Input::PadA == 0 && Input::PadRT == 15);   // the old ids did not move
    CHECK(Input::GamepadButtonLabel(guide, Input::PadFamily::Xbox) == "Guide");
    CHECK(Input::GamepadButtonLabel(guide, Input::PadFamily::PlayStation) == "PS");
    CHECK(Input::GamepadButtonLabel(guide, Input::PadFamily::Switch) == "Home");
    CHECK(PadButtonName(guide, ControllerFamily::DualSense) == "PS" && PadButtonName(guide, ControllerFamily::XboxSeries) == "Guide");
    const InputBinding b = InputBinding::Parse("pad:guide");
    CHECK(b.kind == InputBinding::Kind::PadButton && b.code == guide && b.ToString() == "pad:guide");
    FakePad pad;
    pad.btn[(int)pf::GamepadButton::Guide] = true;
    FeedPadAt(0, &pad, nullptr);
    CHECK(guide >= 0 && Input::GetGamepadButton(guide, 0) && Input::GetGamepadButtonDown(guide, 0));
    CHECK(Input::FirstGamepadButtonDown(0) == guide && guide >= 0);
    CHECK(b.Value() == 1.0f);
    pad.btn[(int)pf::GamepadButton::Guide] = false;
    FeedPadAt(0, &pad, nullptr);
    CHECK(guide >= 0 && Input::GetGamepadButtonUp(guide, 0));
    QuietInput();
}

// ---- 5 -------------------------------------------------------------------------------------------------------------
void TestMoreKeys() {
    RUN_SUITE("5 punctuation, numpad and lock keys");
    // Codes that are saved in bindings and scenes: they must never move.
    CHECK((unsigned char)Input::KeyF1 == 128 && (unsigned char)Input::KeyF12 == 139 && (unsigned char)Input::KeyInsert == 140);
    CHECK((unsigned char)Input::KeyPageDown == 145 && (unsigned char)Input::KeyRAlt == 152 && (unsigned char)Input::KeyCmd == 153);
    CHECK(Input::KeyUp == 1 && Input::KeyRight == 4 && Input::KeyShift == 16 && Input::KeyCtrl == 17);
    CHECK(Input::KeyFromName("enter") == '\r' && Input::KeyFromName("f5") == Input::KeyF(5));
    // Punctuation by name (a binding cannot write ',' or ';' as they are).
    struct P { const char* name; char code; };
    for (const P& p : {P{"minus", '-'}, P{"equals", '='}, P{"leftbracket", '['}, P{"rightbracket", ']'}, P{"backslash", '\\'},
                       P{"semicolon", ';'}, P{"apostrophe", '\''}, P{"grave", '`'}, P{"comma", ','}, P{"period", '.'}, P{"slash", '/'}}) {
        CHECK(Input::IsKnownKeyName(p.name));
        CHECK(Input::KeyFromName(p.name) == p.code);
    }
    // New keys: known names, codes of their own in the free 154..255 range, and names that read back.
    std::string seen;
    for (const char* n : {"kp0", "kp1", "kp2", "kp3", "kp4", "kp5", "kp6", "kp7", "kp8", "kp9", "kpdivide", "kpmultiply", "kpminus", "kpplus",
                          "kpenter", "kpperiod", "capslock", "printscreen", "scrolllock", "pause", "numlock", "menu"}) {
        CHECK(Input::IsKnownKeyName(n));
        const char c = Input::KeyFromName(n);
        CHECK((unsigned char)c >= 154);
        CHECK(seen.find(c) == std::string::npos);
        seen += c;
        const std::string shown = Input::KeyName(c);
        CHECK(Input::IsKnownKeyName(shown) && Input::KeyFromName(shown) == c);   // the label is a name too, so a saved binding loads
        const InputBinding b = InputBinding::Parse(std::string("key:") + n);
        CHECK(b.kind == InputBinding::Kind::Key && InputBinding::Parse(b.ToString()).code == b.code);
    }
    CHECK(Input::IsKnownKeyName("kpenter") && Input::KeyFromName("kpenter") != '\r');   // not the main Enter
    CHECK(Input::KeyName(Input::KeyFromName("kpenter")) == "Numpad Enter");
    CHECK(Input::KeyName(Input::KeyFromName("kp7")) == "Numpad 7");
    CHECK(InputBinding::Parse("key:comma").ToString() == "key:%2c");   // the saved form of ',' is unchanged
    CHECK(InputBinding::Parse("keys:kp8,kp2,kp4,kp6").kind == InputBinding::Kind::Keys4);
    // The host feed (the one the player and the editor's Play share), with faked physical keys.
    {
        using pf::Key;
        std::vector<Key> heldKeys = {Key::Comma, Key::LeftBracket, Key::Kp7, Key::Kp0, Key::KpEnter, Key::KpPlus, Key::CapsLock, Key::Pause,
                                     Key::PrintScreen, Key::NumLock, Key::Application, Key::F5};
        auto held = [&](Key k) { for (Key h : heldKeys) if (h == k) return true; return false; };
        std::vector<char> down;
        AppendHeldMainKeys(held, [](char) { return Key::Unknown; }, false, down);
        AppendExtendedKeys(held, down);
        Input::FeedKeys(down);
        CHECK(Input::GetKey(',') && Input::GetKey('[') && !Input::GetKey('.') && !Input::GetKey(']'));
        CHECK(Input::GetKey(Input::KeyKp(7)) && Input::GetKey(Input::KeyKp(0)) && !Input::GetKey(Input::KeyKp(1)) && !Input::GetKey('7') && !Input::GetKey('0'));
        CHECK(Input::GetKey(Input::KeyKpEnter) && Input::GetKey('\r'));          // numpad Enter is its own key and still Enter, as before
        CHECK(Input::FirstKeyDown() == Input::KeyKpEnter);                         // "press a key" gets the numpad key
        CHECK(Input::GetKey(Input::KeyKpPlus) && Input::GetKey(Input::KeyCapsLock) && Input::GetKey(Input::KeyPause));
        CHECK(Input::GetKey(Input::KeyPrintScreen) && Input::GetKey(Input::KeyNumLock) && Input::GetKey(Input::KeyMenu) && Input::GetKey(Input::KeyF(5)));
        CHECK(InputBinding::Parse("key:kpenter").Value() == 1.0f && InputBinding::Parse("key:comma").Value() == 1.0f);
        // The main Enter alone is not the numpad one.
        heldKeys = {Key::Return};
        down.clear();
        AppendHeldMainKeys(held, [](char) { return Key::Unknown; }, false, down);
        AppendExtendedKeys(held, down);
        Input::FeedKeys(down);
        CHECK(Input::GetKey('\r') && !Input::GetKey(Input::KeyKpEnter));
        CHECK(InputBinding::Parse("key:kpenter").Value() == 0.0f && InputBinding::Parse("key:enter").Value() == 1.0f);
        // Typing in a text field: the numpad's digits and signs are typing keys, numpad Enter is not.
        Input::FeedKeys({Input::KeyKp(4), Input::KeyKpEnter, ','});
        Input::SetTextFocused(true);
        CHECK(!Input::GetKeyGame(Input::KeyKp(4)) && !Input::GetKeyGame(',') && Input::GetKeyGame(Input::KeyKpEnter));
        Input::SetTextFocused(false);
        CHECK(Input::GetKeyGame(Input::KeyKp(4)));
    }
    QuietInput();
}

// ---- 6 -------------------------------------------------------------------------------------------------------------
void TestEightPads() {
    RUN_SUITE("6 eight controllers");
    Prefs::Clear(); InputRuntime::Reset(); InputActions::Restore(); QuietInput();
    CHECK(Input::kMaxGamepads == PlayerInputManager::kMaxPlayers);
    PlayerInputManager& pm = InputRuntime::Players();
    int joined = 0;
    for (int p = 0; p < PlayerInputManager::kMaxPlayers; ++p) if (pm.Join(JoinDevice::Pad, p) == p) ++joined;
    CHECK(joined == 8);
    CHECK(pm.Join(JoinDevice::Pad, 8) == -1);
    // Player 8 presses A on controller 8: only that player's Jump.
    Input::FeedGamepadRaw(7, {}, {}, 0, 0, 1u << Input::PadA, true);
    InputActions::Tick(0.016f);
    CHECK(Input::GamepadConnected(7) && Input::GetGamepadButton(Input::PadA, 7));
    CHECK(pm.Player(7) && pm.Player(7)->Held("Jump"));
    CHECK(pm.Player(6) && !pm.Player(6)->Held("Jump"));
    CHECK(Input::GamepadCount() == 1);
    const InputBinding b = InputBinding::Parse("pad:a@7");
    CHECK(b.kind == InputBinding::Kind::PadButton && b.player == 7 && b.ToString() == "pad:a@7" && b.Value() == 1.0f);
    CHECK(InputBinding::Parse("pad:a@8").kind == InputBinding::Kind::None);
    InputRuntime::Reset();
    CHECK(InputRuntime::JoinNamed("pad7") == 0);
    CHECK(InputRuntime::JoinNamed("pad8") == -1);
    // Eight pads plugged in: one slot each; the ninth waits.
    {
        PadSet set;
        int slots = 0;
        for (int i = 0; i < 8; ++i) { auto p = std::make_unique<FakePad>(); p->id = 100 + i; if (set.Attach(std::move(p)) == i) ++slots; }
        CHECK(slots == 8);
        auto ninth = std::make_unique<FakePad>(); ninth->id = 200;
        CHECK(set.Attach(std::move(ninth)) == -1);
        std::vector<char> keys; set.Feed(keys);
        CHECK(Input::GamepadCount() == 8);
        set.CloseAll();
    }
    InputRuntime::Reset(); InputActions::Restore(); Prefs::Clear(); QuietInput();
}

// ---- 7 -------------------------------------------------------------------------------------------------------------
void TestPadToasts() {
    RUN_SUITE("7 controller connect and disconnect toasts");
    ToastUI ui;
    ScriptUIBridge* before = GetScriptUI();
    SetScriptUI(&ui);                       // what the player and the editor install (OkayUI's toasts)
    PadProfiles::SetToastHandler(nullptr);  // nobody set a handler: the notices go to the installed UI
    GamepadIdentity id; id.name = "Xbox Wireless Controller"; id.vendor = 0x045E; id.product = 0x0B13;
    PadProfiles::Connect(1, 42, id, RawPadState{}, true);
    CHECK(ui.toasts.size() == 1);
    CHECK(!ui.toasts.empty() && ui.toasts[0].find("Controller 2 connected") == 0);
    PadProfiles::Disconnect(1);
    CHECK(ui.toasts.size() == 2);
    CHECK(ui.toasts.size() == 2 && Has(ui.toasts[1], "disconnected"));
    // A game's own handler replaces it.
    std::vector<std::string> own;
    PadProfiles::SetToastHandler([&](const std::string& t) { own.push_back(t); });
    PadProfiles::Connect(1, 42, id, RawPadState{}, true);
    CHECK(own.size() == 1 && ui.toasts.size() == 2);
    PadProfiles::Disconnect(1, false);      // a silent one (a swap) shows nothing
    CHECK(own.size() == 1);
    PadProfiles::SetToastHandler(nullptr);
    SetScriptUI(nullptr);                   // no UI installed: nothing to show, nothing crashes
    PadProfiles::Connect(1, 42, id, RawPadState{}, true);
    PadProfiles::Disconnect(1);
    SetScriptUI(before);
    PadEvent e; while (PadProfiles::PopEvent(e)) {}
}

void TestArrowsApartFromWasd() {
    RUN_SUITE("8 arrows bound apart from W A S D");
    Prefs::Clear(); InputRuntime::Reset(); InputActions::Restore(); QuietInput();
    // The held-key feed (player and editor's Play) and the tap feed give the arrow alone now.
    {
        using pf::Key;
        bool held[pf::kKeyCount] = {};
        held[(int)Key::Up] = true;
        std::vector<char> d;
        AppendHeldMainKeys([&](Key k) { return (int)k >= 0 && (int)k < pf::kKeyCount && held[(int)k]; },
                           [](char c) { return KeyForLetter(Key::Unknown, c); }, false, d);
        CHECK(d.size() == 1 && d[0] == Input::KeyUp);
        std::vector<char> t;
        TapCharsForKey(Key::Left, [](char c) { return KeyForLetter(Key::Unknown, c); }, t);
        CHECK(t.size() == 1 && t[0] == Input::KeyLeft);
    }
    InputActions::Define("Forward", {"key:w"});
    InputActions::Define("Menu Up", {"key:up"});
    InputActions::Define("Walk", {"keys:w,s,a,d"}, true);
    // Up alone: the arrow's action fires, W's does not; raw reads still see W (old games and scripts keep walking).
    Input::FeedKeys({Input::KeyUp}); InputActions::Tick(0.016f);
    CHECK(InputActions::Get("Menu Up"));
    CHECK(!InputActions::Get("Forward"));
    CHECK(InputActions::Vector("Walk").y == 0.0f);
    CHECK(Input::GetKey('w') && Input::GetKeyGame('w') && Input::AxisWASD().y == 1.0f);
    CHECK(Input::GetKeyGameExact(Input::KeyUp) && !Input::GetKeyGameExact('w'));
    CHECK(InputActions::Vector("Move").y == 1.0f);       // the default Move binds the arrows on their own
    // W alone: the other way round.
    Input::FeedKeys({'w'}); InputActions::Tick(0.016f);
    CHECK(InputActions::Get("Forward") && !InputActions::Get("Menu Up"));
    CHECK(InputActions::Vector("Walk").y == 1.0f);
    // Both held: both fire, and W is not "only an arrow".
    Input::FeedKeys({'w', Input::KeyUp}); InputActions::Tick(0.016f);
    CHECK(InputActions::Get("Forward") && InputActions::Get("Menu Up"));
    CHECK(Input::GetKeyGameExact('w'));
    // A tap of the arrow (down and up between two polls) is the arrow alone too.
    Input::FeedKeys({}); Input::FeedKeyTap(Input::KeyDown); Input::FeedKeys({}); InputActions::Tick(0.016f);
    CHECK(Input::GetKey('s') && !Input::GetKeyGameExact('s') && Input::GetKeyGameExact(Input::KeyDown));
    // Holding the arrow, W A S D's key-down fires once (the alias is not "pressed" every frame).
    Input::FeedKeys({}); Input::FeedKeys({Input::KeyRight});
    CHECK(Input::GetKeyDown('d'));
    Input::FeedKeys({Input::KeyRight});
    CHECK(!Input::GetKeyDown('d') && Input::GetKey('d'));
    InputActions::Restore(); InputRuntime::Reset(); Prefs::Clear(); QuietInput();
}

} // namespace

int main(int argc, char** argv) {
    const int only = argc > 1 ? std::atoi(argv[1]) : 0;   // "test_input_wiring 3" runs one section
    if (!only || only == 1) TestProjectAssetInBuiltGame();
    if (!only || only == 2) TestGestureBindings();
    if (!only || only == 3) TestMouseSideButtonsAndWheel();
    if (!only || only == 4) TestGuideButton();
    if (!only || only == 5) TestMoreKeys();
    if (!only || only == 6) TestEightPads();
    if (!only || only == 7) TestPadToasts();
    if (!only || only == 8) TestArrowsApartFromWasd();
    TEST_MAIN_RESULT();
}
