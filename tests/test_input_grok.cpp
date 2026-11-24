// ActionSet: per-axis dead zone, press buffer on a local player's own set, Controls rows for composites, smooth per controller.
#include "test_framework.hpp"
#include "okay/Input/ActionSet.hpp"
#include "okay/Input/ActionEditor.hpp"
#include "okay/Input/Gestures.hpp"
#include <cmath>
#include <string>

using namespace okay;

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

    TEST_MAIN_RESULT();
}
