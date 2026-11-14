#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>

using namespace okay;

namespace {

// One frame of input: held keys, a gamepad, then the actions sampled.
void Frame(std::vector<char> keys, Vec2 left = {}, unsigned pad = 0, Vec2 right = {}, float lt = 0.0f, float rt = 0.0f,
           unsigned mouse = 0) {
    Input::FeedKeys(keys);
    Input::FeedMouse({0.0f, 0.0f}, mouse);
    Input::FeedGamepad(left, pad);
    Input::FeedGamepadExtra(right, lt, rt, true);
    InputActions::Tick();
}
unsigned Bit(int b) { return 1u << b; }

} // namespace

int main() {
    RUN_SUITE("input_actions");
    Prefs::Clear();
    InputActions::Restore();

    // ---- Bindings read and write as text ----
    {
        for (const char* t : {"key:space", "key:e", "key:shift", "mouse:right", "pad:rt", "pad:ls", "keys:w,s,a,d",
                              "stick:left", "stick:right", "dpad", "mouse:delta"}) {
            InputBinding b = InputBinding::Parse(t);
            CHECK(b.kind != InputBinding::Kind::None);
            CHECK(b.ToString() == t);
        }
        CHECK(InputBinding::Parse("Key: Space").ToString() == "key:space");
        CHECK(InputBinding::Parse("key:f13").kind == InputBinding::Kind::None);   // not a key name
        CHECK(InputBinding::Parse("pad:z").kind == InputBinding::Kind::None);
        CHECK(InputBinding::Parse("keys:w,s,a").kind == InputBinding::Kind::None);
        CHECK(InputBinding::Parse("pad:rt").Label() == "RT");
        CHECK(InputBinding::Parse("mouse:left").Label() == "Left Mouse");
        CHECK(InputBinding::Parse("keys:w,s,a,d").Label() == "W A S D");
        CHECK(InputBinding::Parse("pad:a").IsGamepad());
        CHECK(!InputBinding::Parse("key:a").IsGamepad());
    }

    // ---- Buttons: keyboard or controller, with press / release edges ----
    {
        Frame({});
        CHECK(!InputActions::Get("Jump"));
        Frame({' '});
        CHECK(InputActions::Get("Jump"));
        CHECK(InputActions::Down("jump"));          // names are case-insensitive
        Frame({' '});
        CHECK(InputActions::Get("Jump"));
        CHECK(!InputActions::Down("Jump"));
        Frame({});
        CHECK(InputActions::Up("Jump"));
        Frame({}, {}, Bit(Input::PadA));
        CHECK(InputActions::Down("Jump"));
        Frame({}, {}, Bit(Input::PadStart));
        CHECK(InputActions::Get("Pause"));
        Frame({}, {}, 0, {}, 0.0f, 0.0f, 1u);      // left mouse
        CHECK(InputActions::Get("Fire"));
        CHECK(!InputActions::Get("NoSuchAction"));
    }

    // ---- Triggers are analog ----
    {
        Frame({}, {}, 0, {}, 0.0f, 0.3f);
        CHECK_NEAR(InputActions::Value("Fire"), 0.3f, 1e-4);
        CHECK(!InputActions::Get("Fire"));
        Frame({}, {}, Bit(Input::PadRT), {}, 0.0f, 0.8f);
        CHECK_NEAR(InputActions::Value("Fire"), 0.8f, 1e-4);
        CHECK(InputActions::Down("Fire"));
        Frame({}, {}, 0, {}, 0.6f, 0.0f);
        CHECK(InputActions::Get("Aim"));
        CHECK_NEAR(Input::GamepadTrigger(0), 0.6f, 1e-4);
    }

    // ---- Directions: keys, sticks, d-pad; never longer than 1 ----
    {
        Frame({'w'});
        CHECK_NEAR(InputActions::Vector("Move").y, 1.0f, 1e-4);
        CHECK_NEAR(InputActions::Vector("Move").x, 0.0f, 1e-4);
        Frame({Input::KeyLeft});
        CHECK_NEAR(InputActions::Vector("Move").x, -1.0f, 1e-4);
        Frame({}, {0.3f, 0.4f});
        CHECK_NEAR(InputActions::Vector("Move").x, 0.3f, 1e-4);   // analog tilt kept
        CHECK_NEAR(InputActions::Vector("Move").y, 0.4f, 1e-4);
        CHECK_NEAR(InputActions::Value("Move"), 0.5f, 1e-4);
        Frame({'w', 'd'});
        const Vec2 v = InputActions::Vector("Move");
        CHECK_NEAR(std::sqrt(v.x * v.x + v.y * v.y), 1.0f, 1e-4);
        CHECK_NEAR(v.x, v.y, 1e-4);
        Frame({}, {}, Bit(Input::PadDown));
        CHECK_NEAR(InputActions::Vector("Move").y, -1.0f, 1e-4);
        Frame({}, {}, 0, {-1.0f, 0.0f});
        CHECK_NEAR(InputActions::Vector("Look").x, -1.0f, 1e-4);
        CHECK(InputActions::Get("Move") == false);
    }

    // ---- Rebinding, saved per player ----
    {
        Prefs::Clear();
        CHECK(InputActions::Rebind("Jump", "key:f"));
        Frame({' '});
        CHECK(!InputActions::Get("Jump"));        // Space no longer jumps
        Frame({'f'});
        CHECK(InputActions::Get("Jump"));
        Frame({}, {}, Bit(Input::PadA));
        CHECK(InputActions::Get("Jump"));          // the controller binding is untouched
        CHECK(Prefs::GetString("input.jump") == "key:f;pad:a");

        CHECK(InputActions::Rebind("Jump", "pad:rb"));
        CHECK(InputActions::Current("Jump", true)->ToString() == "pad:rb");
        CHECK(InputActions::Current("Jump", false)->ToString() == "key:f");
        CHECK(!InputActions::Rebind("Jump", "pad:nope"));
        CHECK(!InputActions::Rebind("Nope", "key:f"));

        // One key of a four-key direction.
        CHECK(InputActions::Rebind("Move", "key:i", 0));
        Frame({'i'});
        CHECK_NEAR(InputActions::Vector("Move").y, 1.0f, 1e-4);
        Frame({'w'});
        CHECK_NEAR(InputActions::Vector("Move").y, 0.0f, 1e-4);

        // A new session: defaults, then the player's prefs on top.
        const std::string path = "test_input_actions.okayprefs";
        CHECK(Prefs::Save(path));
        Prefs::Clear();
        InputActions::Restore();
        CHECK(InputActions::Current("Jump", false)->ToString() == "key:space");
        CHECK(Prefs::Load(path));
        InputActions::Load();
        CHECK(InputActions::Current("Jump", false)->ToString() == "key:f");
        CHECK(InputActions::Current("Jump", true)->ToString() == "pad:rb");
        CHECK(InputActions::Current("Move", false)->keys[0] == 'i');
        CHECK(!Prefs::Has("input.fire"));          // only what the player changed is stored

        InputActions::Reset("Jump");
        CHECK(InputActions::Current("Jump", false)->ToString() == "key:space");
        CHECK(!Prefs::Has("input.jump"));
        InputActions::Reset();
        CHECK(InputActions::Current("Move", false)->keys[0] == 'w');
        std::remove(path.c_str());
        Prefs::Clear();
    }

    // ---- Games add their own actions ----
    {
        InputActions::Define("Dash", {"key:q", "pad:rs"});
        Frame({'q'});
        CHECK(InputActions::Down("Dash"));
        InputActions::Define("Steer", {"keys:i,k,j,l", "stick:right"}, true);
        Frame({'l'});
        CHECK_NEAR(InputActions::Vector("Steer").x, 1.0f, 1e-4);
        CHECK(InputActions::Has("Steer"));
        // Redefining keeps what the player rebound.
        CHECK(InputActions::Rebind("Dash", "key:z"));
        InputActions::Define("Dash", {"key:q", "pad:rs"});
        CHECK(InputActions::Current("Dash", false)->ToString() == "key:z");
        InputActions::Reset("Dash");
        CHECK(InputActions::Current("Dash", false)->ToString() == "key:q");
        InputActions::Restore();
        CHECK(!InputActions::Has("Dash"));
    }

    // ---- Gamepad: names, rumble requests ----
    {
        CHECK(Input::GamepadButtonFromName("RT") == Input::PadRT);
        CHECK(Input::GamepadButtonFromName("ls") == Input::PadLeftStick);
        CHECK(Input::GamepadButtonFromName("q") == -1);
        CHECK(Input::GamepadButtonName(Input::PadLB) == "LB");
        CHECK(Input::GamepadButtonKey(Input::PadLB) == "lb");
        Input::Rumble(0.5f, 2.0f, 0.25f);
        float lo = 0, hi = 0, sec = 0;
        CHECK(Input::TakeRumble(lo, hi, sec));
        CHECK_NEAR(lo, 0.5f, 1e-4);
        CHECK_NEAR(hi, 1.0f, 1e-4);                // clamped
        CHECK_NEAR(sec, 0.25f, 1e-4);
        CHECK(!Input::TakeRumble(lo, hi, sec));    // taken once
        Frame({}, {}, Bit(Input::PadRightStick));
        CHECK(Input::FirstGamepadButtonDown() == Input::PadRightStick);
    }

    // ---- The keybind field rebinds an action on a keyboard or a controller ----
    {
        Prefs::Clear();
        InputActions::Restore();
        Scene s;
        auto* kb = s.CreateGameObject("Bind")->AddComponent<UIKeybindField>();
        kb->inputAction = "Interact";
        CHECK(kb->KeyLabel() == "E");
        kb->gamepad = true;
        CHECK(kb->KeyLabel() == "X");
        kb->Listen();
        CHECK(std::string(kb->ListenPrompt()) == "Press a button...");
        Frame({});
        kb->Update(0.016f);                         // the frame that started listening
        Frame({}, {}, Bit(Input::PadY));
        kb->Update(0.016f);
        CHECK(!kb->IsListening());
        CHECK(kb->KeyLabel() == "Y");
        CHECK(InputActions::Current("Interact", true)->ToString() == "pad:y");

        kb->gamepad = false;
        kb->Listen();
        Frame({});
        kb->Update(0.016f);
        Frame({'g'});
        kb->Update(0.016f);
        CHECK(kb->KeyLabel() == "G");
        Frame({'g'});
        CHECK(InputActions::Get("Interact"));

        // One direction key of Move.
        auto* mv = s.CreateGameObject("Up")->AddComponent<UIKeybindField>();
        mv->inputAction = "Move"; mv->part = 0;
        CHECK(mv->KeyLabel() == "W");
        mv->Listen();
        Frame({});
        mv->Update(0.016f);
        Frame({Input::KeyUp});
        mv->Update(0.016f);
        CHECK(mv->KeyLabel() == "Up");

        // Backspace while listening puts the action's defaults back.
        kb->Listen();
        Frame({});
        kb->Update(0.016f);
        Frame({'\b'});
        kb->Update(0.016f);
        CHECK(kb->KeyLabel() == "E");
        Prefs::Clear();
        InputActions::Restore();
    }

    // ---- Keybind fields keep their action through save / load ----
    {
        Scene s;
        auto* kb = s.CreateGameObject("Bind")->AddComponent<UIKeybindField>();
        kb->inputAction = "Fire"; kb->gamepad = true; kb->part = 2;
        const std::string text = SceneSerializer::Serialize(s);
        Scene back;
        CHECK(SceneSerializer::Deserialize(back, text));
        GameObject* g = back.Find("Bind");
        auto* b = g ? g->GetComponent<UIKeybindField>() : nullptr;
        CHECK(b != nullptr);
        if (b) {
            CHECK(b->inputAction == "Fire");
            CHECK(b->gamepad);
            CHECK(b->part == 2);
        }
    }

    TEST_MAIN_RESULT();
}
