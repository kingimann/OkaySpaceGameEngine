// The controller -> Input bridge (Input/PlatformGamepad.hpp) driven by a fake
// okay::platform::Gamepad, so the mapping is tested with no device and on any backend.
#include "test_framework.hpp"
#include "okay/Input/PlatformGamepad.hpp"
#include <cmath>

using namespace okay;
namespace pf = okay::platform;

namespace {

struct FakePad final : pf::Gamepad {
    int id = 7;
    int16_t axis[6] = {};
    bool btn[15] = {};
    int rumbles = 0; uint16_t lastLo = 0, lastHi = 0; uint32_t lastMs = 0;
    int InstanceId() const override { return id; }
    const char* Name() const override { return "fake"; }
    int16_t Axis(pf::GamepadAxis a) const override { return axis[(int)a]; }
    bool Button(pf::GamepadButton b) const override { return btn[(int)b]; }
    bool Rumble(uint16_t lo, uint16_t hi, uint32_t ms) override { ++rumbles; lastLo = lo; lastHi = hi; lastMs = ms; return true; }
};

void ClearPads() {
    for (int p = 0; p < Input::kMaxGamepads; ++p) {
        Input::GamepadSettings(p) = Input::PadSettings{};
        Input::FeedGamepadRaw(p, {}, {}, 0, 0, 0, false);
        Input::FeedGamepadRaw(p, {}, {}, 0, 0, 0, false);
    }
}
bool Has(const std::vector<char>& v, char c) { for (char x : v) if (x == c) return true; return false; }

} // namespace

int main() {
    RUN_SUITE("platform_gamepad");
    ClearPads();

    FakePad pad;
    std::vector<char> keys;
    // Centred, nothing pressed: connected, no keys.
    FeedPadAt(0, &pad, &keys);
    CHECK(Input::GamepadConnected(0));
    CHECK(keys.empty());
    CHECK(!Input::GetGamepadButton(Input::PadA, 0));

    // Left stick full right and up (+y is down in the platform layer, so up is negative), A held, right shoulder.
    pad.axis[(int)pf::GamepadAxis::LeftX] = 32767;
    pad.axis[(int)pf::GamepadAxis::LeftY] = -32767;
    pad.btn[(int)pf::GamepadButton::A] = true;
    pad.btn[(int)pf::GamepadButton::RightShoulder] = true;
    pad.btn[(int)pf::GamepadButton::DpadLeft] = true;
    FeedPadAt(0, &pad, &keys);
    CHECK(Input::GamepadAxis(0).x > 0.6f);            // the diagonal is clamped to the unit circle: about 0.707
    CHECK(Input::GamepadAxis(0).y > 0.6f);            // up is +y in Input
    CHECK(Input::GetGamepadButton(Input::PadA, 0));
    CHECK(Input::GetGamepadButton(Input::PadRB, 0));
    CHECK(Input::GetGamepadButton(Input::PadLeft, 0));
    CHECK(!Input::GetGamepadButton(Input::PadB, 0));
    CHECK(Has(keys, 'd') && Has(keys, 'w') && Has(keys, ' '));
    CHECK(Has(keys, 'a'));                            // the d-pad left also types A
    CHECK(!Has(keys, 's'));

    // Triggers: 0..32767 -> 0..1.
    pad.axis[(int)pf::GamepadAxis::TriggerLeft] = 32767;
    pad.axis[(int)pf::GamepadAxis::TriggerRight] = 0;
    FeedPadAt(0, &pad, nullptr);
    CHECK(Input::GamepadTrigger(0, 0) > 0.9f);
    CHECK(Input::GamepadTrigger(1, 0) < 0.05f);

    // A second slot is independent; a null pad empties its slot.
    FakePad pad2; pad2.btn[(int)pf::GamepadButton::Y] = true;
    FeedPadAt(1, &pad2);
    CHECK(Input::GamepadConnected(1) && Input::GetGamepadButton(Input::PadY, 1));
    FeedPadAt(1, nullptr);
    CHECK(!Input::GamepadConnected(1));

    // Rumble requests reach the pad as motor strengths in 0..65535 and milliseconds.
    Input::Rumble(1.0f, 0.5f, 0.25f, 0);
    FeedPadAt(0, &pad, nullptr);
    CHECK(pad.rumbles == 1 && pad.lastLo == 65535 && std::abs((int)pad.lastHi - 32767) <= 1 && pad.lastMs == 250);
    FeedPadAt(0, &pad, nullptr);
    CHECK(pad.rumbles == 1);                          // consumed once
    Input::Rumble(1.0f, 1.0f, 1.0f, 2);               // no pad in slot 2: the request is dropped, not kept
    FeedPadAt(2, nullptr);
    FeedPadAt(2, &pad2);
    CHECK(pad2.rumbles == 0);

    // PadSet with no devices (and the default backend, whatever it is): nothing opens, nothing breaks.
    PadSet set;
    set.OpenAll();                                    // may open real pads when a machine has them; must not crash
    set.CloseAll();
    CHECK(set.Add(9999) == -1);
    CHECK(set.Remove(12345) == -1);
    CHECK(set.Pad(0) == nullptr && set.Pad(-1) == nullptr && set.Pad(Input::kMaxGamepads) == nullptr);
    std::vector<char> k2;
    set.Feed(k2);
    CHECK(!Input::GamepadConnected(0) && k2.empty());

    ClearPads();
    TEST_MAIN_RESULT();
}
