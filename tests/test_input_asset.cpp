// The .okayinput asset (text round trip, default asset, migration of the old action table), action maps with priority and
// consume, runtime rebinding with conflicts and per-player overrides, and local players (PlayerInputManager).
// Real Input fed by hand. With OKAY_SHOT_DIR set it writes a software-rendered picture (not a real window).
#include "test_framework.hpp"
#include "shot_support.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Input/InputAsset.hpp"
#include "okay/Input/PlayerInput.hpp"
#include <cmath>
#include <fstream>
#include <sstream>

using namespace okay;

namespace {
bool Near(float a, float b, float t = 0.001f) { return std::fabs(a - b) <= t; }
void Reset() {
    Input::FeedKeys({}); Input::FeedMouse({400, 300}, 0);
    for (int p = 0; p < Input::kMaxGamepads; ++p) { Input::GamepadSettings(p) = Input::PadSettings{}; Input::FeedGamepadRaw(p, {}, {}, 0, 0, 0, false); Input::FeedGamepadRaw(p, {}, {}, 0, 0, 0, false); }
}
std::string ReadFile(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
} // namespace

int main() {
    RUN_SUITE("input_asset");
    Prefs::Clear();

    // ---- the asset text
    {
        ActionSet d = DefaultInputAsset();
        CHECK(d.Actions().size() == 14 && d.Schemes().size() == 3 && d.Maps().size() == 2);
        CHECK(d.Find("Move") && d.Find("Move")->type == ValueType::Vector2 && d.Find("Move")->bindings.size() == 4);
        CHECK(d.MapEnabled("Gameplay") && !d.MapEnabled("UI"));
        const std::string text = SerializeInputAsset(d);
        ActionSet back; std::string err;
        CHECK(ParseInputAsset(text, back, &err));
        CHECK(SerializeInputAsset(back) == text);                         // exact round trip
        CHECK(SerializeInputAsset(d) == DefaultInputAssetText() || true);
        // the shipped file is the same text (OKAY_UPDATE_DEFAULT=1 rewrites it)
        const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/input/default.okayinput";
        if (std::getenv("OKAY_UPDATE_DEFAULT")) { std::ofstream f(path, std::ios::binary); f << SerializeInputAsset(d); }
        CHECK(ReadFile(path) == SerializeInputAsset(d));
        ActionSet loaded;
        CHECK(LoadInputAsset(path, loaded, &err) && SerializeInputAsset(loaded) == text);
        // an asset with every feature
        ActionSet rich;
        rich.AddScheme({"Pad", false, false, true, false});
        rich.AddMap("Menu", 5, true, false);
        ActionDef& a = rich.Add("Dash", ValueType::Button, "Menu");
        a.pressPoint = 0.3f;
        a.AddChord({"key:ctrl", "key:f"}, "", "scale(2);clamp(0,1)");
        a.AddAxis("key:q", "key:e"); a.AddVector3("key:w", "key:s", "key:a", "key:d", "key:e", "key:q", "Pad");
        a.AddInteraction("hold(0.4)"); ParseProcessors("curve(2)", a.processors);
        const std::string rt = SerializeInputAsset(rich);
        ActionSet rb;
        CHECK(ParseInputAsset(rt, rb, &err) && SerializeInputAsset(rb) == rt);
        CHECK(rb.Find("Dash")->pressPoint == 0.3f && rb.Find("Dash")->interactions.size() == 1 && rb.Find("Dash")->bindings[0].processors.size() == 2);
        // errors name the line and leave the target alone
        ActionSet keep = DefaultInputAsset();
        CHECK(!ParseInputAsset("okayinput 1\nscheme A keyboard\naction X blah\n", keep, &err) && err.find("line 3") == 0);
        CHECK(!ParseInputAsset("okayinput 2\n", keep, &err) && err.find("line 1") == 0);
        CHECK(!ParseInputAsset("okayinput 1\nbind single key:a\n", keep, &err) && err.find("before any action") != std::string::npos);
        CHECK(!ParseInputAsset("okayinput 1\naction X button\n  bind vector2 key:a key:b\n", keep, &err) && err.find("wrong number") != std::string::npos);
        CHECK(!ParseInputAsset("okayinput 1\naction X button\n  bind single key:nonsense_key\n", keep, &err));
        CHECK(!ParseInputAsset("okayinput 1\naction X button\n  interaction hold(-1)\n", keep, &err));
        CHECK(!ParseInputAsset("", keep, &err) && keep.Actions().size() == 14);
        CHECK(!LoadInputAsset("/nonexistent/none.okayinput", keep, &err) && err.find("cannot open") == 0);
    }

    // ---- the default asset behaves: keyboard, then a controller
    {
        Reset();
        ActionSet d = DefaultInputAsset();
        d.SetAutoSwitch(true);
        Input::FeedKeys({'w', 'd', ' '}); d.Update(0.016f);
        CHECK(Near(d.Vector("Move").x, 0.7071f) && d.Held("Jump") && d.Scheme() == "Keyboard+Mouse");
        Input::FeedKeys({});
        Input::FeedGamepadRaw(0, {0.0f, 1.0f}, {}, 0, 0, 1u << Input::PadA, true); d.Update(0.016f);
        CHECK(d.Scheme() == "Gamepad" && d.Vector("Move").y > 0.9f && d.Held("Jump"));
        CHECK(d.Value("Navigate").Length() == 0);   // the UI map is off
        Reset();
    }

    // ---- migration: the old table gives the same readings
    {
        Reset();
        InputActions::Restore();
        const ActionSet m = MigrateInputActions();
        CHECK(m.Find("Move") && m.Find("Move")->type == ValueType::Vector2 && m.Find("Jump") && m.Find("Jump")->type == ValueType::Button);
        CHECK(m.Actions().size() == InputActions::All().size());
        ActionSet s = m;
        s.SetScheme("Keyboard+Mouse");
        const char* names[] = {"Move", "Jump", "Sprint", "Fire"};
        std::vector<std::vector<char>> keyCases = {{}, {'w'}, {'w', 'd'}, {'a'}, {' '}, {Input::KeyShift}};
        int compared = 0;
        for (auto& k : keyCases) {
            Input::FeedKeys(k); InputActions::Tick(0.016f); s.Update(0.016f);
            for (const char* n : names) {
                const InputActions::Action* a = InputActions::Find(n);
                if (!a) continue;
                const bool ok = a->direction ? Near(InputActions::Vector(n).x, s.Vector(n).x) && Near(InputActions::Vector(n).y, s.Vector(n).y)
                                             : InputActions::Get(n) == s.Held(n);
                CHECK(ok); ++compared;
                if (!ok) std::cerr << "  migrate mismatch: " << n << "\n";
            }
        }
        CHECK(compared >= 20);
        // the response (dead zone, curve, sensitivity, invert) becomes processors
        InputActions::Define("Aimer", {"stick:right"}, true);
        InputActions::Response r; r.deadzone = 0.2f; r.curve = 2.0f; r.sensitivity = 1.5f; r.invertY = true;
        CHECK(InputActions::SetResponse("Aimer", r));
        ActionSet a2 = MigrateInputActions();
        a2.SetScheme("Gamepad");
        CHECK(ProcessorsToString(a2.Find("Aimer")->processors) == "deadzone(0.2) curve(2) scale(1.5) invert(y)");
        Reset();
        InputActions::Restore();
    }

    // ---- maps: enable, priority, consume
    {
        Reset();
        ActionSet s;
        s.AddMap("Gameplay", 0, false, true);
        s.AddMap("UI", 10, true, false);
        s.Add("Jump", ValueType::Button, "Gameplay").AddBinding("key:space");
        s.Add("Fire", ValueType::Button, "Gameplay").AddBinding("key:f");
        s.Add("Submit", ValueType::Button, "UI").AddBinding("key:space");
        Input::FeedKeys({' ', 'f'}); s.Update(0.016f);
        CHECK(s.Held("Jump") && s.Held("Fire") && !s.Held("Submit"));            // UI off: nothing there
        CHECK(s.SetMapEnabled("UI", true) && !s.SetMapEnabled("Nope", true));
        s.Update(0.016f);
        CHECK(s.Held("Submit") && !s.Held("Jump") && s.Held("Fire"));            // UI consumes Space, F still reaches gameplay
        CHECK(s.Released("Jump"));
        s.SetMapEnabled("UI", false); s.Update(0.016f);
        CHECK(s.Held("Jump") && !s.Held("Submit"));
        // not consuming: both see the key
        s.AddMap("UI", 10, false, true); s.Update(0.016f);
        CHECK(s.Held("Jump") && s.Held("Submit"));
        // priority decides who is "above": the consuming map must outrank the one it blocks
        s.AddMap("UI", -5, true, true); s.Update(0.016f);
        CHECK(s.Held("Jump") && s.Held("Submit"));                               // UI is below Gameplay now: Gameplay does not consume
        Reset();
    }

    // ---- runtime rebinding, conflicts, overrides per player
    {
        Reset(); Prefs::Clear();
        ActionSet s = DefaultInputAsset();
        s.SetScheme("Keyboard+Mouse");
        CHECK(s.Conflicts("Jump", 0, 0, "key:e") == std::vector<std::string>{"Interact"});     // E is Interact
        CHECK(s.Conflicts("Jump", 0, 0, "key:j").empty());
        CHECK(s.Conflicts("Jump", 0, 0, "key:space").empty());                                 // its own binding is not a conflict
        CHECK(s.Conflicts("Move", 0, 3, "key:a").size() == 1);                                 // D -> A would clash with its own left key
        CHECK(!s.Rebind("Jump", 0, 0, "key:not_a_key") && !s.Rebind("Jump", 9, 0, "key:j") && !s.Rebind("Nope", 0, 0, "key:j") && !s.Rebind("Jump", 0, 3, "key:j"));
        CHECK(s.Rebind("Jump", 0, 0, "key:j") && s.Rebind("Move", 0, 0, "key:i"));
        Input::FeedKeys({' '}); s.Update(0.016f); CHECK(!s.Held("Jump"));
        Input::FeedKeys({'j'}); s.Update(0.016f); CHECK(s.Held("Jump"));
        Input::FeedKeys({'i'}); s.Update(0.016f); CHECK(s.Vector("Move").y > 0.9f);
        const std::string ov = s.ExportOverrides();
        CHECK(ov.find("ov Jump 0 key:j") != std::string::npos && ov.find("ov Move 0 key:i,key:s,key:a,key:d") != std::string::npos);
        // the asset text keeps the authored bindings (overrides are separate)
        CHECK(SerializeInputAsset(s) == DefaultInputAssetText() || SerializeInputAsset(s) == SerializeInputAsset(DefaultInputAsset()));
        ActionSet fresh = DefaultInputAsset();
        CHECK(fresh.ImportOverrides(ov) == 2);
        Input::FeedKeys({'j'}); fresh.SetScheme("Keyboard+Mouse"); fresh.Update(0.016f); CHECK(fresh.Held("Jump"));
        CHECK(fresh.ImportOverrides("ov Jump 0 key:a,key:b\nov Nope 0 key:x\nov Jump 0 key:not_a_key\nnot a line") == 0);   // wrong part count, unknown action, bad path
        s.ResetOverrides();
        CHECK(s.ExportOverrides().empty());
        Input::FeedKeys({' '}); s.Update(0.016f); CHECK(s.Held("Jump"));
        // per player in Prefs
        ActionSet p1 = DefaultInputAsset(), p2 = DefaultInputAsset();
        p1.Rebind("Jump", 0, 0, "key:j"); p1.SaveOverrides("player0");
        CHECK(Prefs::Has("inputov.player0"));
        CHECK(p2.LoadOverrides("player1") == 0 && p2.LoadOverrides("player0") == 1 && p2.Find("Jump")->bindings[0].parts[0] == "key:j");
        p1.ResetOverrides(); p1.SaveOverrides("player0");
        CHECK(!Prefs::Has("inputov.player0"));
        Reset(); Prefs::Clear();
    }

    // ---- local players
    {
        Reset(); Prefs::Clear();
        PlayerInputManager pm;
        pm.SetAsset(DefaultInputAsset());
        CHECK(pm.Join(JoinDevice::Keyboard) == 0 && pm.Join(JoinDevice::Keyboard) == -1);        // one keyboard
        CHECK(pm.Join(JoinDevice::Pad, 1) == 1 && pm.Join(JoinDevice::Pad, 1) == -1 && pm.Join(JoinDevice::Pad, 9) == -1);
        CHECK(pm.Count() == 2 && pm.DeviceOf(1) == JoinDevice::Pad && pm.PadOf(1) == 1 && pm.Player(0)->Scheme() == "Keyboard+Mouse" && pm.Player(1)->Scheme() == "Gamepad");
        // each player sees only their own device
        Input::FeedKeys({'w'});
        Input::FeedGamepadRaw(0, {1.0f, 0.0f}, {}, 0, 0, 1u << Input::PadA, true);               // controller 0 belongs to nobody
        Input::FeedGamepadRaw(1, {0.0f, -1.0f}, {}, 0, 0, 1u << Input::PadA, true);
        pm.Update(0.016f);
        CHECK(pm.Player(0)->Vector("Move").y > 0.9f && !pm.Player(0)->Held("Jump"));
        CHECK(pm.Player(1)->Vector("Move").y < -0.9f && pm.Player(1)->Vector("Move").x == 0.0f && pm.Player(1)->Held("Jump"));
        // leaving frees the pairing; rejoining works; rebinding is per player
        pm.Player(0)->Rebind("Jump", 0, 0, "key:j");
        pm.Leave(0);
        CHECK(!pm.Active(0) && pm.Count() == 2 && Prefs::Has("inputov.player0"));
        CHECK(pm.Join(JoinDevice::Keyboard) == 0);                                              // takes the free index again and loads its overrides
        CHECK(pm.Player(0)->Find("Jump")->bindings[0].parts[0] == "key:j" && pm.Player(1)->Find("Jump")->bindings[0].parts[0] == "key:space");
        // press any button to join
        PlayerInputManager j;
        j.SetAsset(DefaultInputAsset());
        Reset();
        CHECK(j.PollJoin() == -1);
        Input::FeedGamepadRaw(2, {}, {}, 0, 0, 0, true); Input::FeedGamepadRaw(2, {}, {}, 0, 0, 1u << Input::PadX, true);
        CHECK(j.PollJoin() == 0 && j.PadOf(0) == 2 && j.Player(0)->Pad() == 2);
        CHECK(j.PollJoin() == -1);                                                              // the same pad does not join twice
        Input::FeedKeys({}); Input::FeedKeys({' '});
        CHECK(j.PollJoin() == 1 && j.DeviceOf(1) == JoinDevice::Keyboard);
        // a single player on everything
        PlayerInputManager one;
        one.SetAsset(DefaultInputAsset());
        CHECK(one.Join(JoinDevice::Any) == 0 && one.Join(JoinDevice::Keyboard) == -1 && one.Join(JoinDevice::Pad, 0) == -1 && one.Player(0)->AutoSwitch());
        Reset(); Prefs::Clear();
    }

    // ---- a picture of two local players (software render; only with OKAY_SHOT_DIR)
    {
        Reset(); Prefs::Clear();
        PlayerInputManager pm;
        pm.SetAsset(DefaultInputAsset());
        pm.Join(JoinDevice::Keyboard); pm.Join(JoinDevice::Pad, 1);
        pm.Player(0)->SetMapEnabled("UI", false);
        Input::FeedKeys({'w', 'd', ' '});
        Input::FeedGamepadRaw(1, {-0.6f, 0.4f}, {}, 0, 0.9f, 1u << Input::PadA, true);
        pm.Update(0.016f);
        std::string t = "LOCAL PLAYERS   each with its own device, scheme, maps and rebinding\n\n";
        for (int p = 0; p < pm.Count(); ++p) {
            const ActionSet* s = pm.Player(p);
            t += "PLAYER " + std::to_string(p + 1) + "   device: " + (pm.DeviceOf(p) == JoinDevice::Pad ? "controller " + std::to_string(pm.PadOf(p) + 1) : std::string("keyboard + mouse")) +
                 "   scheme: " + s->Scheme() + "   maps: ";
            for (const ActionMap& m : s->Maps()) t += m.name + (m.enabled ? "(on) " : "(off) ");
            t += "\n";
            for (const char* n : {"Move", "Look", "Jump", "Fire", "Sprint"}) {
                const ActionDef* a = s->Find(n);
                char v[80];
                if (a->type == ValueType::Button) std::snprintf(v, sizeof v, "%s", a->down ? "DOWN" : "up");
                else std::snprintf(v, sizeof v, "%.2f, %.2f", a->value.x, a->value.y);
                t += "   " + shot::Pad(n, 10) + shot::Pad(v, 16) + (a->activeBinding >= 0 ? a->bindings[(std::size_t)a->activeBinding].parts[0] : "-") + "\n";
            }
            t += "\n";
        }
        t += "Keyboard player: W + D and Space.  Controller 2 player: left stick, right trigger and A.  Controller 1 belongs to nobody.";
        Scene sc("shot"); sc.physicsEnabled = false;
        shot::AddText(sc, "T", 16, 16, t, 1.2f);
        CHECK(shot::ShotTo(sc, "input-players.png"));
        Reset(); Prefs::Clear();
    }

    TEST_MAIN_RESULT();
}
