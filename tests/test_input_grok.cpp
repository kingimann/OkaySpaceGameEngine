// ActionSet: per-axis dead zone, press buffer on a local player's own set, Controls rows for composites, smooth per controller.
#include "test_framework.hpp"
#include <limits>
#include "okay/Input/ActionSet.hpp"
#include "okay/Input/ActionEditor.hpp"
#include "okay/Input/InputRuntime.hpp"
#include "okay/Input/Gestures.hpp"
#include <cmath>
#include <string>
#include <type_traits>
#include <utility>

using namespace okay;

namespace {
template <class T, class = void> struct HasPadQueue : std::false_type {};
template <class T> struct HasPadQueue<T, std::void_t<decltype(T::TakePadEdge(std::declval<int&>(), std::declval<bool&>()))>> : std::true_type {};
template <class T, class = void> struct HasControlRows : std::false_type {};
template <class T> struct HasControlRows<T, std::void_t<decltype(std::declval<const T&>().ControlRows())>> : std::true_type {};
}

int main() {
    RUN_SUITE("axial dead zone");
    {
        Processor axial, radial;
        CHECK(Processor::Parse("axial(0.2)", axial));
        CHECK(Processor::Parse("deadzone(0.2)", radial));
        const InputValue a = axial.Apply({0.1f, 1.0f, 0.0f}, 0.0f);
        CHECK(a.x == 0.0f);
        CHECK_NEAR(a.y, 1.0f, 0.001f);
        const InputValue r = radial.Apply({0.1f, 1.0f, 0.0f}, 0.0f);
        CHECK(r.x > 0.02f);   // the radial zone keeps the sideways bias; axial removes it
        std::vector<Processor> one{axial};
        CHECK(ProcessorsToString(one) == "axial(0.2)");
        std::vector<Processor> back;
        CHECK(ParseProcessors("axial(0.15,0.9)", back) && back.size() == 1);
        CHECK(ProcessorsToString(back) == "axial(0.15,0.9)");
        const InputValue mid = back[0].Apply({0.525f, 0.0f, 0.0f}, 0.0f);   // halfway from 0.15 to 0.9
        CHECK_NEAR(mid.x, 0.5f, 0.02f);
        const InputValue bad = axial.Apply({std::numeric_limits<float>::quiet_NaN(), 0.4f, 0.1f}, 0.0f);
        CHECK(bad.x == 0.0f);                 // NaN must not become a full deflection
        CHECK(bad.y > 0.2f && bad.y < 0.3f);  // the other axis still uses the axial ramp
        CHECK(bad.z == 0.0f);
    }

    RUN_SUITE("action set buffer");
    {
        Input::FeedKeys({});
        ActionSet s;
        s.Add("Jump", ValueType::Button).AddBinding("key:space");
        Input::FeedKeys({' '});
        s.Update(0.016f);
        Input::FeedKeys({});
        s.Update(0.05f);
        CHECK(s.Buffered("Jump", 0.2f));
        CHECK(s.Consume("Jump", 0.2f));
        CHECK(!s.Buffered("Jump", 0.2f));
        CHECK(!s.Consume("Jump", 0.2f));
        Input::FeedKeys({' '});
        s.Update(0.016f);
        Input::FeedKeys({});
        s.Update(0.5f);
        CHECK(!s.Buffered("Jump", 0.2f));
        CHECK(!s.Consume("Jump", 0.2f));   // the press is older than the window

        ActionSet d;
        d.Add("Jump", ValueType::Button).AddBinding("key:space");
        Input::FeedKeys({' '}); d.Update(0.016f);
        Input::FeedKeys({}); d.Update(0.05f);
        Input::FeedKeys({' '}); d.Update(0.016f);
        CHECK(d.DoubleTap("Jump", 0.3f));
        Input::FeedKeys({}); d.Update(0.016f);
        CHECK(!d.DoubleTap("Jump", 0.3f));

        ActionSet h;
        h.Add("Confirm", ValueType::Button).AddBinding("key:e");
        Input::FeedKeys({'e'});
        h.Update(0.1f);   // the press frame starts the hold clock, so the time held is still ~0
        CHECK(h.HeldFor("Confirm") < 0.02f);
        CHECK(!h.HoldReached("Confirm", 0.25f));
        h.Update(0.3f);   // 0.3 seconds later the hold crosses 0.25
        CHECK(h.HeldFor("Confirm") > 0.25f && h.HeldFor("Confirm") < 0.35f);
        CHECK(h.HoldReached("Confirm", 0.25f));
        h.Update(0.1f);
        CHECK(!h.HoldReached("Confirm", 0.25f));
        Input::FeedKeys({});

        InputRuntime::Reset();
        Input::FeedKeys({});
        InputActions::Tick(0.016f);
        Input::FeedKeys({' '});
        InputActions::Tick(0.016f);
        Input::FeedKeys({});
        InputActions::Tick(0.05f);
        CHECK(InputActions::Buffered("Jump", 0.2f));
        CHECK(InputRuntime::Actions().Buffered("Jump", 0.2f));
        CHECK(InputActions::Consume("Jump"));
        CHECK(!InputRuntime::Actions().Buffered("Jump", 0.2f));
        CHECK(!InputActions::Buffered("Jump", 0.2f));
        InputRuntime::Reset();
        Input::FeedKeys({});
    }

    RUN_SUITE("control rows for composites");
    {
        ActionEditor ed;
        ed.set.Add("Move", ValueType::Vector2).AddVector2("pad:up", "pad:down", "pad:left", "pad:right");
        ed.set.Add("Throttle", ValueType::Axis1D).AddAxis("pad:lt", "pad:rt");
        ed.set.Add("Fly", ValueType::Vector3).AddVector3("key:w", "key:s", "key:a", "key:d", "key:e", "key:q");
        const std::vector<ActionEditor::ControlRow> rows = ed.ControlRows();
        CHECK(rows.size() == 3);
        CHECK(rows[0].gamepad.find("None") == std::string::npos);
        CHECK(rows[0].gamepad.find("D-pad Up") != std::string::npos);
        CHECK(rows[0].gamepad.find("D-pad Right") != std::string::npos);
        CHECK(rows[0].keyboard.empty());
        CHECK(rows[1].type == "axis");
        CHECK(rows[1].gamepad.find("LT") != std::string::npos);
        CHECK(rows[1].gamepad.find("RT") != std::string::npos);
        CHECK(rows[1].gamepad.find("None") == std::string::npos);
        CHECK(rows[2].keyboard.find("W") != std::string::npos);
        CHECK(rows[2].keyboard.find("E") != std::string::npos);
        CHECK(rows[2].keyboard.find("None") == std::string::npos);

        ActionEditor schemes;
        schemes.set.AddScheme(ControlScheme{"Keys", true, true, false, false});
        schemes.set.AddScheme(ControlScheme{"Pad", false, false, true, false});
        schemes.set.Add("Jump", ValueType::Button).AddBinding("key:space", "Keys");
        schemes.set.Find("Jump")->AddBinding("pad:a", "Pad");
        schemes.set.Add("Trick", ValueType::Button).AddBinding("pad:b", "Keys");
        const std::vector<ActionEditor::ControlRow> byScheme = schemes.ControlRows();
        CHECK(byScheme.size() == 2);
        CHECK(byScheme[0].keyboard.find("Space") != std::string::npos);
        CHECK(byScheme[0].gamepad.find("A") != std::string::npos);
        CHECK(byScheme[1].keyboard.find("B") != std::string::npos);
        CHECK(byScheme[1].gamepad.empty());
    }

    RUN_SUITE("smooth per controller");
    {
        for (int p = 0; p < Input::kMaxGamepads; ++p) Input::FeedGamepadAt(p, {0, 0}, 0);
        ActionSet s;
        ActionDef& mv = s.Add("Move", ValueType::Vector2);
        mv.perPlayer = true;
        mv.AddBinding("stick:left", "", "smooth(1)");
        Input::FeedGamepadAt(0, {1, 0}, 0);
        Input::FeedGamepadAt(1, {1, 0}, 0);
        s.Update(0.1f);                 // each controller's smooth memory takes its own first sample
        Input::FeedGamepadAt(0, {0, 0}, 0);   // pad 1 stays fully right
        s.Update(0.1f);
        const float x = s.Find("Move")->pads[0].vector.x;
        const float other = s.Find("Move")->pads[1].vector.x;
        CHECK(x > 0.87f);              // one step from 1 toward 0 (about 0.90). A shared memory steps twice (~0.82).
        CHECK(x < 0.95f);
        CHECK(other > 0.98f);          // pad 2 keeps its own 1, it is not dragged down by pad 1
        Input::FeedGamepadAt(0, {0, 0}, 0);
        Input::FeedGamepadAt(1, {0, 0}, 0);
    }

    RUN_SUITE("response survives save and load");
    {
        ActionSet set;
        ActionDef& look = set.Add("Look", ValueType::Vector2);
        look.AddBinding("stick:right");
        set.Rebind("Look", 0, 0, "stick:left");
        look.response.deadzone = 0.25f;
        look.response.curve = 2.0f;
        look.response.sensitivity = 1.5f;
        look.response.invertY = true;
        const std::string saved = set.ExportOverrides();
        look.response = {};
        set.Rebind("Look", 0, 0, "stick:right");
        CHECK(set.ImportOverrides(saved) >= 1);
        const ActionDef* back = set.Find("Look");
        CHECK(back && back->bindings[0].parts[0] == "stick:left");
        CHECK(std::fabs(back->response.deadzone - 0.25f) < 1e-4f);
        CHECK(std::fabs(back->response.curve - 2.0f) < 1e-4f);
        CHECK(std::fabs(back->response.sensitivity - 1.5f) < 1e-4f);
        CHECK(back->response.invertY);
        CHECK(!back->response.invertX);
    }

    RUN_SUITE("controller hot-plug edges");
    {
        int who = -1; bool on = false;
        while (Input::TakePadEdge(who, on)) {}
        Input::FeedGamepadExtraAt(1, {0, 0}, 0, 0, false);
        CHECK(!Input::TakePadEdge(who, on));
        Input::FeedGamepadExtraAt(1, {0, 0}, 0, 0, true);
        CHECK(Input::TakePadEdge(who, on) && who == 1 && on);
        CHECK(Input::GamepadConnected(1));
        CHECK(!Input::TakePadEdge(who, on));
        Input::FeedGamepadExtraAt(1, {0, 0}, 0, 0, false);
        CHECK(Input::TakePadEdge(who, on) && who == 1 && !on);
        CHECK(!Input::GamepadConnected(1));
    }

    RUN_SUITE("gesture drives an action");
    {
        Gestures::Reset();
        Gestures::ClearBinds();
        Gestures::Bind("swipe up", "Jump");
        ActionSet set;
        set.Add("Jump", ValueType::Button);
        Input::FeedTouch(3, 0, {80, 400});
        Gestures::Tick(1.0);
        Input::FeedTouch(3, 1, {80, 120});
        Gestures::Tick(1.10);
        Input::FeedTouch(3, 2, {80, 90});
        Gestures::Tick(1.12);
        Gestures::Apply(set);
        set.Update(0.016f);
        CHECK(set.Pressed("Jump"));
        Input::ClearTypedText();
        Gestures::Reset();
        Gestures::ClearBinds();
    }

    RUN_SUITE("swipe without clearing the touch list");
    {
        Gestures::Reset();
        Input::FeedTouch(1, 0, {100, 300});
        Gestures::Tick(0.0);
        Input::FeedTouch(1, 1, {100, 80});
        Gestures::Tick(0.10);
        Input::FeedTouch(1, 2, {100, 60});
        Gestures::Tick(0.12);
        CHECK(Gestures::Happened("swipe up"));
        CHECK(Gestures::SwipeDirection() == "up");
        Input::ClearTypedText();
        Gestures::Reset();
    }

    RUN_SUITE("raw feed records a controller edge");
    {
        int who = -1; bool on = false;
        while (Input::TakePadEdge(who, on)) {}
        Input::FeedGamepadRaw(3, {}, {}, 0, 0, 0, false);
        Input::DropPadEdges();
        Input::FeedGamepadRaw(3, {}, {}, 0, 0, 0, true);
        CHECK(Input::TakePadEdge(who, on) && who == 3 && on);
        Input::FeedGamepadRaw(2, {}, {}, 0, 0, 0, true);
        Input::DropPadEdges();
        CHECK(!Input::TakePadEdge(who, on));
    }

    RUN_SUITE("press and move in one frame still began");
    {
        Input::ClearTypedText();
        Input::FeedTouch(9, 0, {10, 10});
        Input::FeedTouch(9, 1, {40, 80});
        const Input::Touch* found = nullptr;
        for (int i = 0; i < Input::TouchCount(); ++i)
            if (Input::GetTouch(i).id == 9) found = &Input::GetTouch(i);
        CHECK(found && found->phase == Input::Touch::Phase::Began && found->began);
        CHECK(found && found->position.x == 40.0f && found->delta.y == 70.0f);
        Input::ClearTypedText();
    }

    RUN_SUITE("touch began survives Gestures::Tick");
    {
        Input::ClearTypedText();
        Gestures::Reset();
        Input::FeedTouch(5, 0, {20, 20});
        Gestures::Tick(0.0);
        const Input::Touch* t = nullptr;
        for (int i = 0; i < Input::TouchCount(); ++i) if (Input::GetTouch(i).id == 5) t = &Input::GetTouch(i);
        CHECK(t && t->began && t->phase == Input::Touch::Phase::Began);
        Input::ClearTypedText();   // the frame ends: now it is a held finger, and a later sample is a move
        Input::FeedTouch(5, 1, {30, 30});
        for (int i = 0; i < Input::TouchCount(); ++i) if (Input::GetTouch(i).id == 5) t = &Input::GetTouch(i);
        CHECK(t && !t->began && t->phase == Input::Touch::Phase::Moved);
        Input::ClearTypedText();
        Gestures::Reset();
    }

    RUN_SUITE("response overrides keep action names with spaces");
    {
        ActionSet a;
        a.Add("Fire Gun", ValueType::Axis1D);
        a.Add("Look", ValueType::Vector2);
        InputActions::Response r; r.deadzone = 0.3f; r.curve = 2.0f; r.sensitivity = 1.5f; r.invertY = true;
        a.Find("Fire Gun")->response = r;
        a.Find("Look")->response = r;
        const std::string text = a.ExportOverrides();
        ActionSet b;
        b.Add("Fire Gun", ValueType::Axis1D);
        b.Add("Look", ValueType::Vector2);
        CHECK(b.ImportOverrides(text) == 2);
        CHECK_NEAR(b.Find("Fire Gun")->response.deadzone, 0.3f, 0.001f);
        CHECK_NEAR(b.Find("Fire Gun")->response.curve, 2.0f, 0.001f);
        CHECK(b.Find("Fire Gun")->response.invertY);
        CHECK_NEAR(b.Find("Look")->response.sensitivity, 1.5f, 0.001f);
        ActionSet c;   // a set without the action keeps the line for later
        c.Add("Look", ValueType::Vector2);
        CHECK(c.ImportOverrides(text) == 1);
        CHECK(c.ExportOverrides().find("Fire Gun") != std::string::npos);
    }

    RUN_SUITE("TEMP repro: ten edges in, ten out");
    {
        int who = -1; bool on = false;
        while (Input::TakePadEdge(who, on)) {}
        int out = 0;
        for (int k = 0; k < 10; ++k) Input::FeedGamepadRaw(0, {}, {}, 0, 0, 0, (k % 2) == 0);
        while (Input::TakePadEdge(who, on)) ++out;
        CHECK(out == 10);
    }

    RUN_SUITE("one row model and one connect detector, no second copies");
    {
        CHECK(!HasPadQueue<Input>::value);       // the platform's GamepadAdded/Removed events are the only connect source
        CHECK(!HasControlRows<ActionEditor>::value);   // the Controls screen builds its rows from the set alone
    }

    TEST_MAIN_RESULT();
}
