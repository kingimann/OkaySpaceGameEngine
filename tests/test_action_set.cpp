// ActionSet (Input/ActionSet.hpp): value types, composites, processors, control schemes and auto switch, with the real Input
// fed by hand (no SDL, no window). With OKAY_SHOT_DIR set it also writes software-rendered pictures (not a real window).
#include "test_framework.hpp"
#include "shot_support.hpp"
#include "okay/Input/ActionSet.hpp"
#include <cmath>

using namespace okay;

namespace {
bool Near(float a, float b, float t = 0.001f) { return std::fabs(a - b) <= t; }
void Reset() {
    Input::FeedKeys({}); Input::FeedMouse({400, 300}, 0);
    for (int p = 0; p < Input::kMaxGamepads; ++p) { Input::GamepadSettings(p) = Input::PadSettings{}; Input::FeedGamepadRaw(p, {}, {}, 0, 0, 0, false); Input::FeedGamepadRaw(p, {}, {}, 0, 0, 0, false); }
}
void Keys(std::vector<char> k) { Input::FeedKeys(k); }

ActionSet MakeSet() {
    ActionSet s;
    s.AddScheme({"Keyboard+Mouse", true, true, false, false});
    s.AddScheme({"Gamepad", false, false, true, false});
    ActionDef& mv = s.Add("Move", ValueType::Vector2);
    mv.AddVector2("key:w", "key:s", "key:a", "key:d", "Keyboard+Mouse");
    mv.AddBinding("stick:left", "Gamepad", "deadzone(0.2,0.9)");
    mv.AddVector2("pad:up", "pad:down", "pad:left", "pad:right", "Gamepad");
    ActionDef& jump = s.Add("Jump", ValueType::Button);
    jump.AddBinding("key:space", "Keyboard+Mouse"); jump.AddBinding("pad:a", "Gamepad");
    ActionDef& th = s.Add("Throttle", ValueType::Axis1D);
    th.AddAxis("key:q", "key:e", "Keyboard+Mouse"); th.AddAxis("pad:lt", "pad:rt", "Gamepad");
    ActionDef& dash = s.Add("Dash", ValueType::Button);
    dash.AddChord({"key:ctrl", "key:f"});   // every scheme
    s.Add("Fly", ValueType::Vector3).AddVector3("key:w", "key:s", "key:a", "key:d", "key:e", "key:q");
    return s;
}
} // namespace

int main() {
    RUN_SUITE("action_set");

    // ---- processors: text round trip and arithmetic
    {
        std::vector<Processor> l;
        CHECK(ParseProcessors("deadzone(0.2,0.9) invert(y);scale(2) normalize swizzle(yxz) smooth(0.1) curve(2) clamp(-1,1)", l) && l.size() == 8);
        CHECK(ProcessorsToString(l) == "deadzone(0.2,0.9) invert(y) scale(2) normalize swizzle(yxz) smooth(0.1) curve(2) clamp(-1,1)");
        std::vector<Processor> l2;
        CHECK(ParseProcessors(ProcessorsToString(l), l2) && ProcessorsToString(l2) == ProcessorsToString(l));
        std::vector<Processor> keep = l;
        CHECK(!ParseProcessors("scale(2) bogus(1)", keep) && keep.size() == 8);   // unchanged on error
        CHECK(!ParseProcessors("clamp(2,1)", keep) && !ParseProcessors("deadzone", keep) && !ParseProcessors("swizzle(xx)", keep) && !ParseProcessors("invert(q)", keep));
        Processor p;
        CHECK(Processor::Parse("deadzone(0.2,0.9)", p));
        CHECK(p.Apply({0.1f, 0, 0}, 0).x == 0 && Near(p.Apply({0.9f, 0, 0}, 0).x, 1.0f) && Near(p.Apply({0.55f, 0, 0}, 0).x, 0.5f));
        const InputValue dz = p.Apply({0.15f, 0.15f, 0}, 0);   // length 0.212: just past the zone, the direction is kept
        CHECK(dz.x > 0 && Near(dz.x, dz.y) && dz.Length() < 0.05f);
        Processor::Parse("invert(y)", p);  CHECK(p.Apply({1, 2, 3}, 0) == (InputValue{1, -2, 3}));
        Processor::Parse("invert", p);     CHECK(p.Apply({1, 2, 3}, 0) == (InputValue{-1, -2, -3}));
        Processor::Parse("scale(2)", p);   CHECK(p.Apply({1, 2, 3}, 0) == (InputValue{2, 4, 6}));
        Processor::Parse("scale(2,0.5)", p); CHECK(p.Apply({1, 2, 3}, 0) == (InputValue{2, 1, 3}));
        Processor::Parse("normalize", p);  CHECK(Near(p.Apply({3, 4, 0}, 0).Length(), 1.0f) && p.Apply({0, 0, 0}, 0).Length() == 0);
        Processor::Parse("swizzle(yxz)", p); CHECK(p.Apply({1, 2, 3}, 0) == (InputValue{2, 1, 3}));
        Processor::Parse("swizzle(zzx)", p); CHECK(p.Apply({1, 2, 3}, 0) == (InputValue{3, 3, 1}));
        Processor::Parse("curve(2)", p);   CHECK(Near(p.Apply({0.5f, 0, 0}, 0).x, 0.25f) && Near(p.Apply({-0.5f, 0, 0}, 0).x, -0.25f));
        Processor::Parse("clamp(-1,1)", p); CHECK(p.Apply({5, -5, 0.5f}, 0) == (InputValue{1, -1, 0.5f}));
        Processor::Parse("smooth(0.1)", p);
        CHECK(p.Apply({0, 0, 0}, 0.016f).x == 0);                       // the first sample is taken as it is
        const float s1 = p.Apply({1, 0, 0}, 0.016f).x, s2 = p.Apply({1, 0, 0}, 0.016f).x;
        CHECK(s1 > 0.0f && s1 < 0.5f && s2 > s1 && s2 < 1.0f);         // eases toward 1
    }

    // ---- types, composites, chord, schemes
    {
        Reset();
        ActionSet s = MakeSet();
        s.SetScheme("Keyboard+Mouse");
        Keys({'w', 'd'}); s.Update(0.016f);
        CHECK(Near(s.Vector("Move").x, 0.7071f) && Near(s.Vector("Move").y, 0.7071f));          // a diagonal is not faster
        CHECK(s.Value("Move").z == 0 && s.Find("Move")->activeBinding == 0);
        Keys({'a'}); s.Update(0.016f);
        CHECK(Near(s.Vector("Move").x, -1.0f) && Near(s.Vector("Move").y, 0.0f));
        Keys({'a', 'd'}); s.Update(0.016f);
        CHECK(s.Vector("Move").x == 0.0f);                                                        // opposite keys cancel
        Keys({' '}); s.Update(0.016f);
        CHECK(s.Pressed("Jump") && s.Held("Jump") && !s.Released("Jump"));
        s.Update(0.016f);
        CHECK(!s.Pressed("Jump") && s.Held("Jump"));
        Keys({}); s.Update(0.016f);
        CHECK(s.Released("Jump") && !s.Held("Jump"));
        // a 1D axis from two keys, and the button type reading a vector's length
        Keys({'e'}); s.Update(0.016f); CHECK(s.Axis("Throttle") == 1.0f);
        Keys({'q'}); s.Update(0.016f); CHECK(s.Axis("Throttle") == -1.0f);
        // a chord: the modifier first, then the key; the key alone does nothing, the modifier alone does nothing
        Keys({'f'}); s.Update(0.016f); CHECK(!s.Held("Dash"));
        Keys({Input::KeyCtrl}); s.Update(0.016f); CHECK(!s.Held("Dash"));
        Keys({Input::KeyCtrl, 'f'}); s.Update(0.016f); CHECK(s.Pressed("Dash"));
        // 3D: up, left and forward
        Keys({'w', 'a', 'e'}); s.Update(0.016f);
        const InputValue f = s.Value("Fly");
        CHECK(Near(f.Length(), 1.0f) && f.x < 0 && f.y > 0 && f.z > 0);
        // keyboard bindings are off under the Gamepad scheme, pad bindings on
        s.SetScheme("Gamepad");
        Keys({'w'}); s.Update(0.016f);
        CHECK(s.Vector("Move").y == 0.0f);
        Input::FeedGamepadRaw(0, {0.0f, 1.0f}, {}, 0, 0, 0, true); s.Update(0.016f);
        CHECK(s.Vector("Move").y > 0.9f && s.Find("Move")->activeBinding == 1);
        Input::FeedGamepadRaw(0, {}, {}, 0, 0, 1u << Input::PadRight, true); s.Update(0.016f);
        CHECK(Near(s.Vector("Move").x, 1.0f) && s.Find("Move")->activeBinding == 2);              // the d-pad composite
        Input::FeedGamepadRaw(0, {}, {}, 1.0f, 0.0f, 0, true); s.Update(0.016f);
        CHECK(Near(s.Axis("Throttle"), -1.0f));                                                    // pad:lt is the negative side
        CHECK(!s.SetScheme("Nope") && s.Scheme() == "Gamepad");
        // a processor on the action itself comes after the binding's
        ActionDef& boost = s.Add("Boost", ValueType::Axis1D);
        boost.AddBinding("key:b"); ParseProcessors("scale(3) clamp(0,2)", boost.processors);
        Keys({'b'}); s.Update(0.016f);
        CHECK(s.Axis("Boost") == 2.0f);
        // unknown actions read as nothing
        CHECK(s.Value("Nope").Length() == 0 && !s.Pressed("Nope"));
        Reset();
    }

    // ---- auto switch to the last used device, and a paired local player
    {
        Reset();
        ActionSet s = MakeSet();
        s.SetAutoSwitch(true);
        Keys({'w'}); s.Update(0.016f);
        CHECK(s.Scheme() == "Keyboard+Mouse" && s.Vector("Move").y > 0.9f);
        Keys({});
        Input::FeedGamepadRaw(0, {0.0f, 0.0f}, {}, 0, 0, 1u << Input::PadA, true); s.Update(0.016f);
        CHECK(s.Scheme() == "Gamepad" && s.Pressed("Jump"));
        Input::FeedGamepadRaw(0, {}, {}, 0, 0, 0, true);   // (a W A S D press while a pad button is held is read as the pad's own stand-in key)
        Keys({'d'}); s.Update(0.016f);
        CHECK(s.Scheme() == "Keyboard+Mouse");
        // pair with controller 2 (index 1): the first pad's activity does not switch it, and its stick drives Move
        ActionSet p2 = MakeSet();
        p2.SetAutoSwitch(true); p2.SetPad(1);
        Keys({'w'}); p2.Update(0.016f);
        CHECK(p2.Scheme() == "Keyboard+Mouse");
        Keys({});
        Input::FeedGamepadRaw(0, {0.0f, 0.0f}, {}, 0, 0, 1u << Input::PadB, true); p2.Update(0.016f);
        CHECK(p2.Scheme() == "Keyboard+Mouse");
        Input::FeedGamepadRaw(0, {}, {}, 0, 0, 0, true);
        Input::FeedGamepadRaw(1, {0.0f, -1.0f}, {}, 0, 0, 1u << Input::PadA, true); p2.Update(0.016f);
        CHECK(p2.Scheme() == "Gamepad" && p2.Vector("Move").y < -0.9f && p2.Pressed("Jump"));
        Reset();
    }

    // ---- pictures (software renders; only when OKAY_SHOT_DIR is set)
    {
        auto render = [&](ActionSet& s, const char* title, const char* file) {
            Scene sc("shot"); sc.physicsEnabled = false;
            std::string t = std::string("ACTION SET   ") + title + "\n";
            t += "active control scheme: " + s.Scheme() + "   auto switch: " + (s.AutoSwitch() ? "on" : "off") + "\n\n";
            t += shot::Pad("action", 12) + shot::Pad("type", 10) + shot::Pad("value", 30) + "driven by\n";
            for (const ActionDef& a : s.Actions()) {
                char v[64];
                if (a.type == ValueType::Button) std::snprintf(v, sizeof v, "%s (%.2f)", a.down ? "DOWN" : "up", a.value.x);
                else if (a.type == ValueType::Axis1D) std::snprintf(v, sizeof v, "%.2f", a.value.x);
                else if (a.type == ValueType::Vector2) std::snprintf(v, sizeof v, "%.2f, %.2f", a.value.x, a.value.y);
                else std::snprintf(v, sizeof v, "%.2f, %.2f, %.2f", a.value.x, a.value.y, a.value.z);
                std::string by = "-";
                if (a.activeBinding >= 0) { const BindingSpec& b = a.bindings[(std::size_t)a.activeBinding]; by = b.parts.empty() ? "" : b.parts[0]; if (b.kind != BindingSpec::Kind::Single) by += " ... (composite)"; if (!b.processors.empty()) by += "  " + ProcessorsToString(b.processors); by += "  [" + (b.group.empty() ? std::string("all") : b.group) + "]"; }
                t += shot::Pad(a.name, 12) + shot::Pad(ValueTypeName(a.type), 10) + shot::Pad(v, 30) + by + "\n";
            }
            shot::AddText(sc, "T", 16, 16, t, 1.2f);
            CHECK(shot::ShotTo(sc, file));
        };
        Reset();
        ActionSet s = MakeSet();
        s.SetAutoSwitch(true);
        Keys({'w', 'd', Input::KeyCtrl, 'f'}); s.Update(0.016f);
        render(s, "keyboard in use: W + D, Ctrl + F", "action-set-keyboard.png");
        Keys({});
        Input::FeedGamepadRaw(0, {0.6f, 0.8f}, {}, 0, 0.7f, 1u << Input::PadA, true); s.Update(0.016f);
        render(s, "controller in use: left stick, A, right trigger", "action-set-gamepad.png");
        Reset();
    }

    TEST_MAIN_RESULT();
}
