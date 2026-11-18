// ActionEditor (the logic behind the Input Actions window) and the live debugger lines. Real Input fed by hand. With OKAY_SHOT_DIR set
// it writes software renders of what the window shows (the text the window draws; NOT a screenshot of the ImGui window).
#include "test_framework.hpp"
#include "shot_support.hpp"
#include "okay/Input/ActionEditor.hpp"
#include "okay/Input/PlatformGamepad.hpp"
#include <cstdio>
#include <filesystem>

using namespace okay;

namespace {
void Reset() {
    Input::FeedKeys({}); Input::FeedMouse({400, 300}, 0);
    for (int p = 0; p < Input::kMaxGamepads; ++p) { Input::FeedGamepadRaw(p, {}, {}, 0, 0, 0, false); Input::FeedGamepadRaw(p, {}, {}, 0, 0, 0, false); }
}
struct Dev final : platform::Gamepad {
    int InstanceId() const override { return 5; }
    const char* Name() const override { return "DualSense Wireless Controller"; }
    std::int16_t Axis(platform::GamepadAxis) const override { return 0; }
    bool Button(platform::GamepadButton) const override { return false; }
    bool Rumble(std::uint16_t, std::uint16_t, std::uint32_t) override { return true; }
    std::uint16_t VendorId() const override { return 0x054C; }
    std::uint16_t ProductId() const override { return 0x0CE6; }
    int ControllerType() const override { return 7; }
    int PowerLevel() const override { return 2; }
};
bool Any(const std::vector<std::string>& v, const char* needle) { for (auto& s : v) if (s.find(needle) != std::string::npos) return true; return false; }
} // namespace

int main() {
    RUN_SUITE("action_editor");
    ActionEditor e;
    e.NewFromDefault(); e.dirty = false;
    CHECK(e.Validate().empty());
    CHECK(Any(e.Outline(), "MAP Gameplay  priority 0") && Any(e.Outline(), "Move  [vector2]  4 bindings") && Any(e.Outline(), "MAP UI  priority 10  consumes  (off)"));
    CHECK(Any(e.Describe("Move"), "2D vector  up=key:w  down=key:s  left=key:a  right=key:d   group Keyboard+Mouse"));
    CHECK(e.Describe("Nope")[0] == "(no action selected)");

    // edits
    CHECK(!e.AddAction("Dash Now", ValueType::Button, "Gameplay") && !e.AddAction("Jump", ValueType::Button, "Gameplay") && !e.AddAction("X", ValueType::Button, "NoMap") && !e.dirty);
    CHECK(e.AddAction("Dash", ValueType::Button, "Gameplay") && e.dirty);
    CHECK(e.AddBinding("Dash", BindingSpec::Kind::Chord, "Keyboard+Mouse") && e.set.Find("Dash")->bindings[0].parts.size() == 2);
    CHECK(e.SetPart("Dash", 0, 0, "key:ctrl") && e.SetPart("Dash", 0, 1, "key:q"));
    CHECK(!e.SetPart("Dash", 0, 1, "key:no_such_key") && !e.SetPart("Dash", 0, 5, "key:q") && !e.SetPart("Dash", 4, 0, "key:q") && !e.SetPart("Nope", 0, 0, "key:q"));
    CHECK(e.AddBinding("Dash", BindingSpec::Kind::Single, "Gamepad") && e.SetPart("Dash", 1, 0, "pad:x"));
    CHECK(e.SetProcessors("Dash", 1, "deadzone(0.2) scale(2)") && !e.SetProcessors("Dash", 1, "bogus") && e.SetProcessors("Dash", -1, "clamp(0,1)") && !e.SetProcessors("Dash", 9, "scale(2)"));
    CHECK(e.AddInteraction("Dash", "hold(0.3)") && !e.AddInteraction("Dash", "hold(0)") && e.set.Find("Dash")->interactions.size() == 1);
    CHECK(e.Validate().empty());
    CHECK(e.AddScheme("Pad2", false, false, true, false) && !e.AddScheme("Pad2", false, false, true, false) && !e.AddScheme("bad name", true, false, false, false));
    CHECK(e.AddMap("Vehicle", 5, true) && !e.AddMap("Vehicle") && !e.AddMap("x y"));
    CHECK(e.SetGroup("Dash", 1, "Pad2"));
    // validation finds mistakes
    e.set.Find("Dash")->bindings[0].group = "Nope";
    e.set.Find("Dash")->bindings[0].parts[1] = "key:???";
    e.set.Find("Dash")->AddInteraction("chorded(Ghost)");
    e.set.Find("Dash")->map = "Missing";
    const auto probs = e.Validate();
    CHECK(Any(probs, "group Nope is not a control scheme") && Any(probs, "cannot read input \"key:???\"") && Any(probs, "chorded(Ghost) names a missing action") && Any(probs, "map Missing does not exist"));
    e.set.Find("Dash")->bindings[0].group = "Keyboard+Mouse"; e.set.Find("Dash")->bindings[0].parts[1] = "key:q";
    e.set.Find("Dash")->interactions.pop_back(); e.set.Find("Dash")->map = "Gameplay";
    CHECK(e.Validate().empty());
    // remove / rename keep references consistent
    CHECK(e.RemoveBinding("Dash", 1) && !e.RemoveBinding("Dash", 1) && e.RemoveInteraction("Dash", 0) && !e.RemoveInteraction("Dash", 0));
    CHECK(e.set.RenameMap("Vehicle", "Cars") && !e.set.RenameMap("Cars", "UI") && e.set.Maps().back().name == "Cars");
    CHECK(e.Rename("Dash", "Dodge") && !e.Rename("Dodge", "Jump") && !e.Rename("Nope", "A") && e.set.Find("Dodge"));
    e.set.Find("Jump")->AddInteraction("chorded(Dodge)");
    CHECK(e.Rename("Dodge", "Roll") && e.set.Find("Jump")->interactions[0].arg == "Roll");
    CHECK(e.set.RemoveMap("Gameplay") && e.set.Find("Jump")->map.empty());
    CHECK(e.set.RemoveScheme("Gamepad") && e.set.Find("Jump")->bindings[1].group.empty() && e.Remove("Roll") && !e.Remove("Roll"));
    // save / load
    const std::string file = (std::filesystem::temp_directory_path() / "okay_action_editor_test.okayinput").string();
    CHECK(!e.Save());   // no path yet
    CHECK(e.SaveAs(file) && !e.dirty);
    ActionEditor f; std::string err;
    CHECK(f.Load(file, &err) && SerializeInputAsset(f.set) == SerializeInputAsset(e.set) && !f.dirty);
    std::remove(file.c_str());
    CHECK(!f.Load("/nonexistent/x.okayinput", &err) && !f.SaveAs("/nonexistent/dir/x.okayinput"));
    ActionEditor m; Reset(); InputActions::Restore(); m.NewFromMigration();
    CHECK(m.set.Actions().size() == InputActions::All().size() && m.dirty);

    // live debugger lines
    {
        Reset();
        PadProfiles::Reset();
        PadSet pads;
        CHECK(ControllerDebugLines().back() == "no controller connected");
        CHECK(pads.Attach(std::make_unique<Dev>()) == 0);
        std::vector<char> k; pads.Feed(k);
        const auto cl = ControllerDebugLines();
        CHECK(Any(cl, "controller 1: DualSense Wireless Controller") && Any(cl, "0x054c:0x0ce6") && Any(cl, "mapping built-in") && Any(cl, "battery medium"));
        ActionSet s = DefaultInputAsset(); s.SetAutoSwitch(true);
        s.Find("Jump")->AddInteraction("hold(0.5)");
        Input::FeedGamepadRaw(0, {0.0f, 1.0f}, {}, 0, 0, 1u << Input::PadA, true);
        s.Update(0.1f); s.Update(0.1f);
        const auto al = ActionSetDebugLines(s);
        CHECK(Any(al, "scheme Gamepad (auto)") && Any(al, "maps: Gameplay(on) UI(off)") && Any(al, "Move = 0.00,1.00") && Any(al, "Jump = DOWN") && Any(al, "hold 0.20") && Any(al, "Move = 0.00,1.00   active") && Any(al, "via pad:a"));
        CHECK(ActionSetDebugLines(s, 3).back() == "...");

        // pictures
        ActionEditor shotEd; shotEd.NewFromDefault(); shotEd.path = "Assets/input.okayinput"; shotEd.dirty = false;
        {
            Scene sc("shot"); sc.physicsEnabled = false;
            std::string left, right;
            for (auto& l : shotEd.Outline()) left += l + "\n";
            for (auto& l : shotEd.Describe("Look")) right += l + "\n";
            right += "\nBindings of Move:\n";
            for (auto& l : shotEd.Describe("Move")) right += l + "\n";
            right += "\nCheck: ";
            right += shotEd.Validate().empty() ? "no problems found" : "problems";
            shot::AddText(sc, "Title", 16, 8, "INPUT ACTIONS WINDOW (what it lists; a text render of the editor logic, not the ImGui window)", 1.0f, Color{1.0f, 0.85f, 0.4f, 1.0f});
            shot::AddText(sc, "L", 16, 40, left, 0.95f);
            shot::AddText(sc, "R", 560, 40, right, 0.95f);
            CHECK(shot::ShotTo(sc, "input-actions-window.png"));
        }
        {
            Scene sc("shot"); sc.physicsEnabled = false;
            std::string t = "LIVE INPUT DEBUGGER (a text render of the lines the Input panel and the Live tab draw)\n\n";
            for (auto& l : ControllerDebugLines()) t += l + "\n";
            t += "\nPlayer 1\n";
            for (auto& l : al) t += l + "\n";
            shot::AddText(sc, "T", 16, 16, t, 1.0f);
            CHECK(shot::ShotTo(sc, "input-debugger.png"));
        }
        pads.CloseAll(); PadProfiles::Reset(); Reset();
    }
    TEST_MAIN_RESULT();
}
