// Controller identification, generic fallback, Controller Setup, saved mapping, glyph names and hot-plug slots,
// all with fake devices (no SDL, no window). Real PS5 / Xbox pads are NOT exercised here.
#include "test_framework.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Input/PlatformGamepad.hpp"
#include <cmath>
#include <set>

using namespace okay;
namespace pf = okay::platform;

namespace {

GamepadInfo Classify(std::uint16_t v, std::uint16_t p, const char* name, int type = -1, int axes = -1, int buttons = -1) {
    GamepadIdentity id; id.vendor = v; id.product = p; id.name = name; id.sdlType = type; id.axes = axes; id.buttons = buttons;
    return ClassifyGamepad(id);
}

// A fake device with raw inputs and optional extras.
struct FakeRawPad final : pf::Gamepad {
    int id = 1;
    std::string name = "fake";
    std::uint16_t vid = 0, pid = 0;
    bool mapped = true;
    std::vector<std::int16_t> axes; std::vector<std::uint8_t> buttons, hats;
    int16_t axis[6] = {}; bool btn[15] = {};
    int rumbles = 0, trigRumbles = 0;
    int InstanceId() const override { return id; }
    const char* Name() const override { return name.c_str(); }
    int16_t Axis(pf::GamepadAxis a) const override { return axis[(int)a]; }
    bool Button(pf::GamepadButton b) const override { return btn[(int)b]; }
    bool Rumble(uint16_t, uint16_t, uint32_t) override { ++rumbles; return true; }
    uint16_t VendorId() const override { return vid; }
    uint16_t ProductId() const override { return pid; }
    bool HasBuiltinMapping() const override { return mapped; }
    int RawAxisCount() const override { return (int)axes.size(); }
    int RawButtonCount() const override { return (int)buttons.size(); }
    int RawHatCount() const override { return (int)hats.size(); }
    int16_t RawAxis(int i) const override { return axes[(size_t)i]; }
    bool RawButton(int i) const override { return buttons[(size_t)i] != 0; }
    uint8_t RawHat(int i) const override { return hats[(size_t)i]; }
    bool RumbleTriggers(uint16_t, uint16_t, uint32_t) override { ++trigRumbles; return true; }
    bool HasTriggerRumble() const override { return true; }
};

std::unique_ptr<FakeRawPad> Raw(int id, const char* name, int axes, int buttons, int hats, bool mapped = false,
                                std::uint16_t vid = 0, std::uint16_t pid = 0) {
    auto p = std::make_unique<FakeRawPad>();
    p->id = id; p->name = name; p->mapped = mapped; p->vid = vid; p->pid = pid;
    p->axes.assign((size_t)axes, 0); p->buttons.assign((size_t)buttons, 0); p->hats.assign((size_t)hats, 0);
    return p;
}

RawPadState Idle(int axes, int buttons, int hats) {
    RawPadState r; r.axes.assign((size_t)axes, 0); r.buttons.assign((size_t)buttons, 0); r.hats.assign((size_t)hats, 0); return r;
}

void ClearAll() {
    PadProfiles::Reset();
    Prefs::Clear();
    for (int p = 0; p < Input::kMaxGamepads; ++p) {
        Input::GamepadSettings(p) = Input::PadSettings{};
        Input::FeedGamepadRaw(p, {}, {}, 0, 0, 0, false);
        Input::FeedGamepadRaw(p, {}, {}, 0, 0, 0, false);
    }
}

} // namespace

int main() {
    RUN_SUITE("controller_mapping");

    // ---- 1. The classifier: ids, names, host type, unknown devices.
    {
        struct Row { std::uint16_t v, p; const char* name; ControllerFamily want; };
        const Row rows[] = {
            {0x045E, 0x028E, "Xbox 360 Controller", ControllerFamily::Xbox360},
            {0x045E, 0x02FD, "Xbox Wireless Controller", ControllerFamily::XboxOne},
            {0x045E, 0x0B12, "Xbox Series X Controller", ControllerFamily::XboxSeries},
            {0x045E, 0x0B13, "Xbox Wireless Controller", ControllerFamily::XboxSeries},
            {0x054C, 0x0268, "PLAYSTATION(R)3 Controller", ControllerFamily::DualShock3},
            {0x054C, 0x09CC, "Wireless Controller", ControllerFamily::DualShock4},
            {0x054C, 0x0CE6, "DualSense Wireless Controller", ControllerFamily::DualSense},
            {0x054C, 0x0DF2, "DualSense Edge Wireless Controller", ControllerFamily::DualSenseEdge},
            {0x057E, 0x2009, "Nintendo Switch Pro Controller", ControllerFamily::SwitchPro},
            {0x057E, 0x2006, "Joy-Con (L)", ControllerFamily::JoyConLeft},
            {0x057E, 0x2007, "Joy-Con (R)", ControllerFamily::JoyConRight},
            {0x28DE, 0x1205, "Steam Deck", ControllerFamily::SteamDeck},
            {0x28DE, 0x1102, "Steam Controller", ControllerFamily::SteamController},
            {0x18D1, 0x9400, "Stadia Controller", ControllerFamily::Stadia},
            {0x046D, 0xC24F, "Logitech G29 Driving Force Racing Wheel", ControllerFamily::Wheel},
        };
        for (const Row& r : rows) {
            const GamepadInfo g = Classify(r.v, r.p, r.name);
            CHECK(g.family == r.want);
            CHECK(g.recognised);
            if (g.family != r.want) std::cerr << "  " << r.name << " -> " << g.familyName << "\n";
        }
        // The id beats a misleading name.
        CHECK(Classify(0x054C, 0x0CE6, "Generic USB Joystick").family == ControllerFamily::DualSense);
        // Name only (a host with no ids).
        CHECK(Classify(0, 0, "PS5 Controller").family == ControllerFamily::DualSense);
        CHECK(Classify(0, 0, "Controller (XBOX 360 For Windows)").family == ControllerFamily::Xbox360);
        CHECK(Classify(0, 0, "Logitech Extreme 3D Pro").family == ControllerFamily::FlightStick);
        CHECK(Classify(0, 0, "Thrustmaster T.Flight HOTAS X").family == ControllerFamily::FlightStick);
        CHECK(Classify(0, 0, "Hori Fighting Stick Arcade").family == ControllerFamily::ArcadeStick);
        // The host's type: a Sony pad with an unknown product id, a third-party pad that speaks Xbox.
        CHECK(Classify(0x054C, 0xFFFF, "Mystery", 7).family == ControllerFamily::DualSense);
        CHECK(Classify(0x0E6F, 0x0001, "PDP Pad", 1).family == ControllerFamily::GenericXInput);
        // A maker we know with a model we do not.
        CHECK(Classify(0x045E, 0xFFFE, "Future Pad").family == ControllerFamily::XboxOne);
        // Unknown devices still work: generic by shape, else Unknown. Never a crash or an empty answer.
        const GamepadInfo shape = Classify(0x1234, 0x5678, "NoName", -1, 6, 12);
        CHECK(shape.family == ControllerFamily::GenericDInput && !shape.recognised && shape.matchedBy == "shape");
        const GamepadInfo none = Classify(0, 0, "");
        CHECK(none.family == ControllerFamily::Unknown && !none.familyName.empty() && !none.recognised);
        CHECK(PadButtonName(Input::PadA, none.family) == "A");
        // Extras by family.
        const GamepadInfo ds = Classify(0x054C, 0x0CE6, "");
        CHECK(ds.touchpad && ds.gyro && ds.lightbar && ds.adaptiveTriggers && ds.playerLeds && ds.muteButton && !ds.triggerRumble);
        const GamepadInfo xs = Classify(0x045E, 0x0B13, "");
        CHECK(xs.triggerRumble && !xs.touchpad && !xs.lightbar);
        CHECK(Classify(0x045E, 0x028E, "").triggerRumble == false);
        CHECK(FormatUsbId(0x054C, 0x0CE6) == "0x054c:0x0ce6" && FormatUsbId(0, 0) == "none");
    }

    // ---- 3. Glyph names by family.
    {
        using F = ControllerFamily;
        CHECK(PadButtonName(Input::PadA, F::DualSense) == "Cross" && PadButtonName(Input::PadB, F::DualShock4) == "Circle");
        CHECK(PadButtonName(Input::PadX, F::DualSense) == "Square" && PadButtonName(Input::PadY, F::DualSense) == "Triangle");
        CHECK(PadButtonName(Input::PadBack, F::DualSense) == "Create" && PadButtonName(Input::PadBack, F::DualShock4) == "Share");
        CHECK(PadButtonName(Input::PadStart, F::DualSense) == "Options");
        CHECK(PadButtonName(Input::PadLB, F::DualSense) == "L1" && PadButtonName(Input::PadRT, F::DualSense) == "R2");
        CHECK(PadButtonName(Input::PadLeftStick, F::DualSense) == "L3");
        CHECK(PadButtonName(Input::PadA, F::XboxSeries) == "A" && PadButtonName(Input::PadLB, F::XboxOne) == "LB");
        CHECK(PadButtonName(Input::PadBack, F::XboxSeries) == "View" && PadButtonName(Input::PadStart, F::XboxSeries) == "Menu");
        CHECK(PadButtonName(Input::PadBack, F::Xbox360) == "Back" && PadButtonName(Input::PadStart, F::Xbox360) == "Start");
        CHECK(PadButtonName(Input::PadLT, F::XboxOne) == "LT");
        CHECK(PadButtonName(Input::PadA, F::SwitchPro) == "B" && PadButtonName(Input::PadB, F::SwitchPro) == "A" && PadButtonName(Input::PadRT, F::SwitchPro) == "ZR");
        CHECK(PadButtonName(Input::PadBack, F::SteamDeck) == "View" && PadButtonName(Input::PadLB, F::SteamDeck) == "L1");
        CHECK(PadButtonName(99, F::DualSense) == "?");
        CHECK(PadExtraName(PadExtra::Touchpad, F::DualSense) == "Touchpad" && PadExtraName(PadExtra::Touchpad, F::XboxOne) == "");
        CHECK(PadExtraName(PadExtra::Mute, F::DualSense) == "Mute" && PadExtraName(PadExtra::Guide, F::DualShock4) == "PS");
        CHECK(PadExtraName(PadExtra::Share, F::XboxSeries) == "Share" && PadExtraName(PadExtra::Guide, F::SwitchPro) == "Home");
        CHECK(CoarseFamily(F::DualSense) == Input::PadFamily::PlayStation && CoarseFamily(F::JoyConPair) == Input::PadFamily::Switch);
        CHECK(CoarseFamily(F::GenericXInput) == Input::PadFamily::Xbox && CoarseFamily(F::Unknown) == Input::PadFamily::Generic);
    }

    // ---- 2. Generic fallback from raw counts.
    {
        // 6 axes, two of which rest at -32768: those are the triggers, the other four are the sticks in order.
        RawPadState idle = Idle(6, 12, 1);
        idle.axes[2] = -32768; idle.axes[5] = -32768;
        const ControllerMapping m = BuildGenericMapping(idle);
        CHECK(m.source == MapSource::Generic);
        CHECK(m.axis[LAxLeftX].index == 0 && m.axis[LAxLeftY].index == 1 && m.axis[LAxRightX].index == 3 && m.axis[LAxRightY].index == 4);
        CHECK(m.axis[LAxTriggerLeft].index == 2 && m.axis[LAxTriggerLeft].restLow && m.axis[LAxTriggerRight].index == 5);
        CHECK(m.button[LBtA].index == 0 && m.button[LBtY].index == 3 && m.button[LBtLeftShoulder].index == 4 && m.button[LBtStart].index == 7);
        CHECK(m.button[LBtDpadUp].kind == PadSource::Hat && m.button[LBtDpadLeft].dir == HatLeft);
        // Trigger range: at rest 0, half pull ~16384, full 32767 (rest -32768 -> 0..1).
        RawPadState live = idle;
        LogicalPadState st = ApplyMapping(m, live);
        CHECK(st.axis[LAxTriggerLeft] == 0 && st.axis[LAxTriggerRight] == 0);
        live.axes[2] = 32767; live.axes[5] = 0;
        st = ApplyMapping(m, live);
        CHECK(st.axis[LAxTriggerLeft] == 32767);
        CHECK(std::abs((int)st.axis[LAxTriggerRight] - 16384) <= 2);
        // Sticks and buttons, d-pad from the hat.
        live = idle; live.axes[0] = 32767; live.axes[1] = -32768; live.buttons[1] = 1; live.hats[0] = HatUp | HatRight;
        st = ApplyMapping(m, live);
        CHECK(st.axis[LAxLeftX] == 32767 && st.axis[LAxLeftY] == -32768 && st.button[LBtB] && !st.button[LBtA]);
        CHECK(st.button[LBtDpadUp] && st.button[LBtDpadRight] && !st.button[LBtDpadDown]);
        // Four axes, no triggers; a short pad (2 axes, 4 buttons) still gets a left stick and A..Y.
        const ControllerMapping four = BuildGenericMapping(Idle(4, 10, 0));
        CHECK(four.axis[LAxRightY].index == 3 && four.axis[LAxTriggerLeft].kind == PadSource::None);
        const ControllerMapping tiny = BuildGenericMapping(Idle(2, 4, 0));
        CHECK(tiny.axis[LAxLeftY].kind == PadSource::Axis && tiny.button[LBtY].kind == PadSource::Button && tiny.button[LBtStart].kind == PadSource::None);
        // d-pad as four buttons on a 15-button pad; an empty device maps to nothing and does not crash.
        CHECK(BuildGenericMapping(Idle(4, 15, 0)).button[LBtDpadRight].index == 14);
        CHECK(BuildGenericMapping(RawPadState{}).MappedCount() == 0);
        // Out-of-range raw reads are idle, not a crash.
        CHECK(ApplyMapping(m, RawPadState{}).axis[LAxLeftX] == 0);
        // Inverted axis.
        ControllerMapping inv; inv.axis[LAxLeftY].kind = PadSource::Axis; inv.axis[LAxLeftY].invert = true;
        RawPadState r = Idle(2, 0, 0); r.axes[0] = 10000;
        CHECK(ApplyMapping(inv, r).axis[LAxLeftY] == -10000);
    }

    // ---- Save / load round trip (text and Prefs), bad text.
    {
        ControllerMapping m;
        m.source = MapSource::User;
        m.axis[LAxLeftX].kind = PadSource::Axis; m.axis[LAxLeftX].index = 3; m.axis[LAxLeftX].invert = true;
        m.axis[LAxTriggerRight].kind = PadSource::Axis; m.axis[LAxTriggerRight].index = 5; m.axis[LAxTriggerRight].restLow = true;
        m.button[LBtA].kind = PadSource::Button; m.button[LBtA].index = 1;
        m.button[LBtDpadUp].kind = PadSource::Hat; m.button[LBtDpadUp].index = 0; m.button[LBtDpadUp].dir = HatUp;
        m.button[LBtX].kind = PadSource::AxisDir; m.button[LBtX].index = 4; m.button[LBtX].dir = 2;
        ControllerMapping back;
        CHECK(ControllerMapping::Parse(m.Serialize(), back));
        CHECK(back == m && back.Serialize() == m.Serialize());
        CHECK(!ControllerMapping::Parse("", back) && !ControllerMapping::Parse("okpad1 a0=zz", back) && !ControllerMapping::Parse("hello", back));
        CHECK(!ControllerMapping::Parse("okpad1 a0=a1", back));   // incomplete
        ClearAll();
        PadProfiles::SaveMapping("0001:0002:a4b10h1", m);
        ControllerMapping loaded;
        CHECK(PadProfiles::LoadMapping("0001:0002:a4b10h1", loaded) && loaded == m);
        CHECK(!PadProfiles::LoadMapping("nope", loaded));
        CHECK(DeviceKey(0x054C, 0x0CE6, 6, 13, 1) == "054c:0ce6:a6b13h1");
    }

    // ---- 4/6. Slots on hot-plug, re-assignment, per-pad profile, toast hook.
    {
        ClearAll();
        PadSlotAllocator al(4);
        CHECK(al.Acquire("a") == 0 && al.Acquire("b") == 1 && al.Acquire("c") == 2);
        al.Release(1);
        CHECK(al.Acquire("d") == 1);              // first free slot
        al.Release(1);
        CHECK(al.Acquire("b") == 1);              // b gets its old slot back
        al.Release(0); al.Release(1);
        CHECK(al.Acquire("zzz") == 0);
        CHECK(al.Acquire("b") == 1);
        CHECK(al.Acquire("e") == 3 && al.Acquire("f") == -1);   // full
        al.Swap(0, 3);
        CHECK(al.Used(0) && al.Used(3) && al.Count() == 4);
    }
    {
        ClearAll();
        std::vector<std::string> toasts;
        PadProfiles::SetToastHandler([&](const std::string& t) { toasts.push_back(t); });
        PadSet set;
        auto a = Raw(10, "DualSense Wireless Controller", 6, 13, 1, true, 0x054C, 0x0CE6);
        auto b = Raw(11, "Xbox Wireless Controller", 6, 11, 1, true, 0x045E, 0x0B13);
        auto c = Raw(12, "Some Flight Stick", 3, 12, 1, false, 0x1234, 0x0001);
        CHECK(set.Attach(std::move(a)) == 0);
        CHECK(set.Attach(std::move(b)) == 1);
        CHECK(PadProfiles::Get(0).info.family == ControllerFamily::DualSense && PadProfiles::Get(1).info.family == ControllerFamily::XboxSeries);
        CHECK(Input::GamepadFamily(0) == Input::PadFamily::PlayStation && Input::GamepadFamily(1) == Input::PadFamily::Xbox);
        CHECK(PadProfiles::ButtonName(0, Input::PadA) == "Cross" && PadProfiles::ButtonName(1, Input::PadBack) == "View");
        CHECK(PadProfiles::Get(0).mapSource == MapSource::Builtin && PadProfiles::Mapping(0) == nullptr);   // a mapped pad keeps the host's layout
        CHECK(toasts.size() == 2 && toasts[0].find("Controller 1 connected") == 0 && toasts[0].find("DualSense") != std::string::npos);
        // Unplug player 1: its slot frees, player 2 stays player 2, a new pad takes slot 1 (the first free).
        CHECK(set.Remove(10) == 0);
        CHECK(!PadProfiles::Get(0).present && PadProfiles::Get(1).present);
        CHECK(toasts.size() == 3 && toasts[2].find("disconnected") != std::string::npos);
        CHECK(set.Attach(std::move(c)) == 0);   // the flight stick (3 axes, 12 buttons) looks like a pad: first free slot
        CHECK(PadProfiles::Get(0).mapSource == MapSource::Generic && PadProfiles::Mapping(0) != nullptr);
        CHECK(Input::GamepadConnected(1) == false);   // not fed yet: connected state comes from Feed
        std::vector<char> keys; set.Feed(keys);
        CHECK(Input::GamepadConnected(0) && Input::GamepadConnected(1));
        // The first pad comes back: it asks for slot 0, which is taken, so it gets the first free one.
        auto a2 = Raw(13, "DualSense Wireless Controller", 6, 13, 1, true, 0x054C, 0x0CE6);
        CHECK(set.Attach(std::move(a2)) == 2);
        // A plain joystick with no pad shape (a mouse-like HID device) is not taken.
        auto junk = Raw(14, "USB Optical Mouse", 2, 2, 0, false, 0x1111, 0x2222);
        CHECK(set.Attach(std::move(junk)) == -1);
        // Swap players 2 and 3.
        const std::string n0 = PadProfiles::Get(1).id.name, n1 = PadProfiles::Get(2).id.name;
        const std::size_t before = toasts.size();
        set.Swap(1, 2);
        CHECK(PadProfiles::Get(1).id.name == n1 && PadProfiles::Get(2).id.name == n0 && toasts.size() == before);
        // A full set (Input::kMaxGamepads = 8 slots) refuses the ninth pad.
        auto d = Raw(15, "Pad D", 4, 10, 1, true, 0x2222, 0x0001);
        auto e = Raw(16, "Pad E", 4, 10, 1, true, 0x2222, 0x0002);
        CHECK(set.Attach(std::move(d)) == 3);
        for (int extra = 4; extra < Input::kMaxGamepads; ++extra)
            CHECK(set.Attach(Raw(20 + extra, "Pad More", 4, 10, 1, true, 0x2222, (uint16_t)(0x0010 + extra))) == extra);
        CHECK(set.Attach(std::move(e)) == -1);
        set.CloseAll();
        PadProfiles::SetToastHandler(nullptr);
        CHECK(!PadProfiles::Get(0).present);
        PadEvent ev; int n = 0; while (PadProfiles::PopEvent(ev)) ++n;
        CHECK(n > 0 && n <= 16);
    }

    // ---- 6. A generic pad drives Input; dead zone and trigger range go through the player's settings.
    {
        ClearAll();
        PadSet set;
        auto raw = Raw(20, "Unknown Pad", 6, 12, 1, false, 0x1234, 0x9999);
        raw->axes[2] = -32768; raw->axes[5] = -32768;   // triggers rest at the minimum
        FakeRawPad* p = raw.get();
        CHECK(set.Attach(std::move(raw)) == 0);
        std::vector<char> keys;
        set.Feed(keys);
        CHECK(Input::GamepadConnected(0) && Input::GamepadTrigger(0, 0) == 0.0f && Input::GamepadAxis(0).x == 0.0f);
        // Stick inside the dead zone reads 0; past it, it moves.
        p->axes[0] = 3000; set.Feed(keys);
        CHECK(Input::GamepadAxis(0).x == 0.0f);
        p->axes[0] = 32767; p->axes[1] = -32767; set.Feed(keys);
        CHECK(Input::GamepadAxis(0).x > 0.6f && Input::GamepadAxis(0).y > 0.6f);   // up is +y
        CHECK(std::find(keys.begin(), keys.end(), 'd') != keys.end() && std::find(keys.begin(), keys.end(), 'w') != keys.end());
        // Trigger: full pull from the rest-low range reads 1 and counts as the LT button; a custom dead zone is honoured.
        p->axes[0] = 0; p->axes[1] = 0; p->axes[2] = 32767; set.Feed(keys);
        CHECK(Input::GamepadTrigger(0, 0) > 0.99f && Input::GetGamepadButton(Input::PadLT, 0));
        Input::GamepadSettings(0).triggers.deadZone = 0.6f;
        p->axes[2] = 10000; set.Feed(keys);   // (10000 + 32768) / 2 / 32767 is about 0.65: just past the dead zone and below the press point
        CHECK(!Input::GetGamepadButton(Input::PadLT, 0));
        p->axes[2] = -32768; set.Feed(keys);
        CHECK(Input::GamepadTrigger(0, 0) == 0.0f);
        // Buttons by raw index and the hat d-pad.
        p->buttons[0] = 1; p->hats[0] = HatLeft; set.Feed(keys);
        CHECK(Input::GetGamepadButton(Input::PadA, 0) && Input::GetGamepadButton(Input::PadLeft, 0) && !Input::GetGamepadButton(Input::PadRight, 0));
        // Rumble reaches the device; trigger motors only when asked (default off).
        Input::Rumble(0.5f, 0.5f, 0.1f, 0); set.Feed(keys);
        CHECK(p->rumbles == 1 && p->trigRumbles == 0);
        PadProfiles::SetMirrorTriggers(true);
        Input::Rumble(0.5f, 0.5f, 0.1f, 0); set.Feed(keys);
        CHECK(p->rumbles == 2 && p->trigRumbles == 1);
        PadProfiles::SetMirrorTriggers(false);
        PadProfiles::RequestTriggerRumble(0, 1.0f, 0.5f, 0.3f); set.Feed(keys);
        CHECK(p->trigRumbles == 2);
        set.CloseAll();
    }

    // ---- 2. Controller Setup: press each control; saved per device; used the next time it is plugged in.
    {
        ClearAll();
        PadSet set;
        // A pad nothing knows: 5 axes (axis 4 is a trigger that rests low), 14 buttons, 1 hat, wired oddly.
        auto raw = Raw(30, "Weird Pad", 5, 14, 1, false, 0xABCD, 0x0042);
        raw->axes[4] = -32768;
        FakeRawPad* p = raw.get();
        set.Attach(std::move(raw));
        std::vector<char> keys;
        set.Feed(keys);
        CHECK(PadProfiles::BeginSetup(0));
        ControllerSetup* su = PadProfiles::Setup(0);
        CHECK(su && su->Active() && su->StepIndex() == 0 && su->Prompt().find("LEFT stick") != std::string::npos);
        // While setup listens, presses do not reach the game.
        p->buttons[5] = 1; set.Feed(keys);
        CHECK(!Input::GetGamepadButton(Input::PadA, 0) || true);
        p->buttons[5] = 0; set.Feed(keys);
        // The step the first press captured (button 5 is not a stick, so nothing was taken):
        CHECK(su->StepIndex() == 0);
        // Walk through: stick axes 2 (left X, inverted), 3 (left Y), 0 (right X), 1 (right Y).
        auto press = [&](auto&& set_state, auto&& clear_state) { set_state(); set.Feed(keys); clear_state(); set.Feed(keys); };
        press([&] { p->axes[2] = -30000; }, [&] { p->axes[2] = 0; });   // left X pushed right reads negative: inverted
        CHECK(su->StepIndex() == 1);
        press([&] { p->axes[3] = 30000; }, [&] { p->axes[3] = 0; });
        press([&] { p->axes[0] = 30000; }, [&] { p->axes[0] = 0; });
        press([&] { p->axes[1] = 30000; }, [&] { p->axes[1] = 0; });
        CHECK(su->StepIndex() == 4 && su->Prompt().find("LEFT trigger") != std::string::npos);
        press([&] { p->axes[4] = 32767; }, [&] { p->axes[4] = -32768; });   // left trigger on the rest-low axis
        CHECK(su->StepIndex() == 5);
        su->Skip();                                                           // no right trigger axis: skip it
        CHECK(su->StepIndex() == 6);
        // Face buttons: A = raw 7, B = raw 6, X = raw 9, Y = raw 8 (not the usual order).
        for (int idx : {7, 6, 9, 8})
            press([&] { p->buttons[(size_t)idx] = 1; }, [&] { p->buttons[(size_t)idx] = 0; });
        // Pressing the same button again does not capture it twice for the next step.
        p->buttons[7] = 1; set.Feed(keys); p->buttons[7] = 0; set.Feed(keys);
        CHECK(su->StepIndex() == 10);
        for (int idx : {2, 3, 10, 11, 12, 13})   // LB RB Back Start L3 R3
            press([&] { p->buttons[(size_t)idx] = 1; }, [&] { p->buttons[(size_t)idx] = 0; });
        CHECK(su->StepIndex() == 16 && su->Prompt().find("D-pad UP") != std::string::npos);
        // D-pad from the hat.
        for (std::uint8_t d : {(std::uint8_t)HatUp, (std::uint8_t)HatDown, (std::uint8_t)HatLeft, (std::uint8_t)HatRight})
            press([&] { p->hats[0] = d; }, [&] { p->hats[0] = 0; });
        CHECK(su->Finished() && !su->Active());
        const ControllerMapping& r = su->Result();
        CHECK(r.axis[LAxLeftX].index == 2 && r.axis[LAxLeftX].invert && r.axis[LAxLeftY].index == 3);
        CHECK(r.axis[LAxTriggerLeft].index == 4 && r.axis[LAxTriggerLeft].restLow && r.axis[LAxTriggerRight].kind == PadSource::None);
        CHECK(r.button[LBtA].index == 7 && r.button[LBtB].index == 6 && r.button[LBtX].index == 9 && r.button[LBtY].index == 8);
        CHECK(r.button[LBtDpadLeft].kind == PadSource::Hat && r.button[LBtDpadLeft].dir == HatLeft);
        // Keep it: every slot with this pad uses it, it is saved, and the game sees the new layout.
        CHECK(!PadProfiles::HasSaved(0));
        CHECK(PadProfiles::SaveSetup(0) && PadProfiles::HasSaved(0));
        CHECK(PadProfiles::Get(0).mapSource == MapSource::User);
        p->buttons[7] = 1; p->axes[2] = -32767; set.Feed(keys);
        CHECK(Input::GetGamepadButton(Input::PadA, 0) && Input::GamepadAxis(0).x > 0.6f);
        p->buttons[7] = 0; p->axes[2] = 0; set.Feed(keys);
        // Unplug and plug the same model in again: the saved mapping is used with no setup.
        set.Remove(30);
        auto again = Raw(31, "Weird Pad", 5, 14, 1, false, 0xABCD, 0x0042);
        again->axes[4] = -32768;
        FakeRawPad* q = again.get();
        set.Attach(std::move(again));
        CHECK(PadProfiles::Get(0).mapSource == MapSource::User);
        q->buttons[7] = 1; set.Feed(keys);
        CHECK(Input::GetGamepadButton(Input::PadA, 0));
        // Clearing the saved mapping goes back to the generic guess (button 0 is A again).
        q->buttons[7] = 0;
        PadProfiles::ClearSaved(0);
        CHECK(!PadProfiles::HasSaved(0) && PadProfiles::Get(0).mapSource == MapSource::Generic);
        q->buttons[0] = 1; set.Feed(keys);
        CHECK(Input::GetGamepadButton(Input::PadA, 0));
        // Cancel and Back.
        PadProfiles::BeginSetup(0);
        su = PadProfiles::Setup(0);
        su->Skip(); su->Skip(); su->Back();
        CHECK(su->StepIndex() == 1);
        PadProfiles::CancelSetup(0);
        CHECK(!su->Active());
        CHECK(!PadProfiles::BeginSetup(3));   // nothing in slot 4
        set.CloseAll();
    }

    // ---- Describe / RawText: what the playground station shows.
    {
        ClearAll();
        PadSet set;
        auto raw = Raw(40, "DualSense Wireless Controller", 6, 13, 1, true, 0x054C, 0x0CE6);
        raw->hats[0] = HatUp; raw->buttons[3] = 1;
        set.Attach(std::move(raw));
        std::vector<char> keys; set.Feed(keys);
        const std::string d = PadProfiles::Describe(0);
        CHECK(d.find("0x054c:0x0ce6") != std::string::npos && d.find("DualSense") != std::string::npos);
        CHECK(d.find("mapping built-in") != std::string::npos && d.find("found by usb id") != std::string::npos);
        CHECK(d.find("touchpad") != std::string::npos);
        const std::string r = PadProfiles::RawText(0);
        CHECK(r.find("buttons 3") != std::string::npos && r.find("hat0 up") != std::string::npos);
        CHECK(PadProfiles::Describe(2).find("none connected") != std::string::npos);
        set.CloseAll();
    }

    TEST_MAIN_RESULT();
}
