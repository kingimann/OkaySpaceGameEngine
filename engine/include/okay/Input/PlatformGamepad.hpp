#pragma once
// Reads a game controller (through okay::platform, so SDL2 or our own evdev
// backend) into Input: both sticks, both triggers and every button, and plays
// Input::Rumble requests on it. Shared by the player and the editor's Play mode.
// Header-only; no SDL types appear here.
#include "okay/Input/Input.hpp"
#include "okay/Input/PadProfiles.hpp"
#include "okay/Input/PadSlots.hpp"
#include "okay/Platform/Platform.hpp"
#include <cmath>
#include <memory>
#include <vector>

namespace okay {

/// A raw stick, -1..1 with up as +y. Dead zone, outer zone and curve are applied
/// by Input::FeedGamepadRaw from Input::GamepadSettings(player).
inline Vec2 PadRawStick(const platform::Gamepad& pad, platform::GamepadAxis ax, platform::GamepadAxis ay) {
    return {pad.Axis(ax) / 32767.0f, -pad.Axis(ay) / 32767.0f};
}

/// Who a platform pad says it is (ids, name, host type, raw counts) for the classifier.
inline GamepadIdentity ReadPadIdentity(const platform::Gamepad& pad) {
    GamepadIdentity id;
    id.vendor = pad.VendorId(); id.product = pad.ProductId();
    const char* n = pad.Name(); id.name = n ? n : "";
    id.sdlType = pad.ControllerType();
    id.axes = pad.RawAxisCount(); id.buttons = pad.RawButtonCount(); id.hats = pad.RawHatCount();
    return id;
}
/// The pad's numbered axes, buttons and hats as they are now (empty for a backend that has no raw access).
inline RawPadState ReadPadRaw(const platform::Gamepad& pad) {
    RawPadState r;
    const int na = pad.RawAxisCount(), nb = pad.RawButtonCount(), nh = pad.RawHatCount();
    r.axes.resize((size_t)na); r.buttons.resize((size_t)nb); r.hats.resize((size_t)nh);
    for (int i = 0; i < na; ++i) r.axes[(size_t)i] = pad.RawAxis(i);
    for (int i = 0; i < nb; ++i) r.buttons[(size_t)i] = pad.RawButton(i) ? 1 : 0;
    for (int i = 0; i < nh; ++i) r.hats[(size_t)i] = pad.RawHat(i);
    return r;
}
/// A raw joystick worth offering as a controller: sticks and some buttons, or plenty of buttons.
inline bool LooksLikeGamepad(int axes, int buttons, int hats) {
    return (axes >= 2 && buttons >= 4) || buttons >= 8 || (hats >= 1 && buttons >= 4);
}

/// Feed one controller's state as player `player` (pad may be null: empty slot).
/// Raw sticks and triggers go through that player's Input::GamepadSettings. When
/// `keys` is non-null (player 0 only, normally) the left stick, d-pad and A also
/// push W/A/S/D and Space, so games written against the keyboard play on a pad.
inline void FeedPadAt(int player, platform::Gamepad* pad, std::vector<char>* keys = nullptr) {
    using platform::GamepadAxis;
    using platform::GamepadButton;
    Vec2 left, right; unsigned mask = 0; float lt = 0.0f, rt = 0.0f;
    // Index in the old Input::PadButton order -> logical (platform) button, for both paths.
    static const GamepadButton kMap[] = {
        GamepadButton::A, GamepadButton::B, GamepadButton::X, GamepadButton::Y,
        GamepadButton::Back, GamepadButton::Start,
        GamepadButton::LeftShoulder, GamepadButton::RightShoulder,
        GamepadButton::DpadUp, GamepadButton::DpadDown,
        GamepadButton::DpadLeft, GamepadButton::DpadRight,
        GamepadButton::LeftStick, GamepadButton::RightStick};
    const ControllerMapping* map = pad ? PadProfiles::Mapping(player) : nullptr;
    const bool settingUp = pad && PadProfiles::Setup(player) && PadProfiles::Setup(player)->Active();
    if (pad && settingUp) {
        // Controller Setup is listening: the game sees a neutral pad so the presses do not click through.
    } else if (pad && map) {
        // A saved or generic mapping drives this pad from its raw inputs.
        const LogicalPadState st = ApplyMapping(*map, ReadPadRaw(*pad));
        left = {st.axis[LAxLeftX] / 32767.0f, -st.axis[LAxLeftY] / 32767.0f};
        right = {st.axis[LAxRightX] / 32767.0f, -st.axis[LAxRightY] / 32767.0f};
        lt = st.axis[LAxTriggerLeft] / 32767.0f; rt = st.axis[LAxTriggerRight] / 32767.0f;
        for (int i = 0; i < (int)(sizeof(kMap) / sizeof(kMap[0])); ++i)
            if (st.button[(int)kMap[i]]) mask |= 1u << i;
    } else if (pad) {
        left = PadRawStick(*pad, GamepadAxis::LeftX, GamepadAxis::LeftY);
        right = PadRawStick(*pad, GamepadAxis::RightX, GamepadAxis::RightY);
        lt = pad->Axis(GamepadAxis::TriggerLeft) / 32767.0f;
        rt = pad->Axis(GamepadAxis::TriggerRight) / 32767.0f;
        for (int i = 0; i < (int)(sizeof(kMap) / sizeof(kMap[0])); ++i)
            if (pad->Button(kMap[i])) mask |= 1u << i;
    }
    // Processed first so the keyboard stand-in keys follow the dead zone too.
    Input::FeedGamepadRaw(player, left, right, lt, rt, mask, pad != nullptr);
    if (pad && keys) {
        const Vec2 l = Input::GamepadAxis(player);
        if (l.x > 0.4f  || (mask & (1u << Input::PadRight))) keys->push_back('d');
        if (l.x < -0.4f || (mask & (1u << Input::PadLeft)))  keys->push_back('a');
        if (l.y > 0.4f  || (mask & (1u << Input::PadUp)))    keys->push_back('w');
        if (l.y < -0.4f || (mask & (1u << Input::PadDown)))  keys->push_back('s');
        if (mask & (1u << Input::PadA)) keys->push_back(' ');
    }
    float lo, hi, sec;
    if (Input::TakeRumble(lo, hi, sec, player) && pad) {   // nothing to shake: the request is dropped
        pad->Rumble((uint16_t)(lo * 65535.0f), (uint16_t)(hi * 65535.0f), (uint32_t)(sec * 1000.0f));
        // Off by default: with PadProfiles::SetMirrorTriggers(true), Xbox One / Series triggers shake with the motors too.
        if (PadProfiles::MirrorTriggers() && pad->HasTriggerRumble())
            pad->RumbleTriggers((uint16_t)(lo * 65535.0f), (uint16_t)(hi * 65535.0f), (uint32_t)(sec * 1000.0f));
    }
}

/// Feed this frame's first controller (pad may be null: nothing connected).
/// The left stick, d-pad and A also go into `keys` as W/A/S/D and Space.
inline void FeedPad(platform::Gamepad* pad, std::vector<char>& keys) {
    FeedPadAt(0, pad, &keys);
}

/// Up to Input::kMaxGamepads controllers, one per player slot. A pad takes the slot it had
/// last time when that is free (else the first free one) and frees it when pulled out. Use it
/// from the event loop: Add(deviceIndex) on a controller-added event, Remove(instanceId) on a
/// controller-removed event (SDL_CONTROLLERDEVICE* or platform::EventType::Gamepad*),
/// Feed once per frame. Every connect and disconnect is reported to PadProfiles (events and
/// the toast hook); Feed keeps PadProfiles current (raw state, battery, touchpad, sensors).
class PadSet {
public:
    /// Open every controller that is already plugged in. Pads the host has a layout for come first, so
    /// a plain joystick (a flight stick, a wheel) never takes player 1 from a real gamepad.
    void OpenAll() {
        platform::Backend* b = platform::Default();
        if (!b) return;
        std::vector<std::unique_ptr<platform::Gamepad>> later;
        for (int i = 0, n = b->GamepadDeviceCount(); i < n; ++i) {
            if (!b->IsGamepad(i)) continue;
            std::unique_ptr<platform::Gamepad> c = b->OpenGamepad(i);
            if (!c) continue;
            if (c->HasBuiltinMapping()) Attach(std::move(c)); else later.push_back(std::move(c));
        }
        for (auto& c : later) Attach(std::move(c));
    }
    /// Put an already-open controller in a slot. A null pad returns -1.
    /// The same instance id already in the set returns that slot and drops `pad`.
    /// Returns the player index, or -1 when every slot is full (and drops `pad`) or the
    /// device is a plain joystick that does not look like a gamepad.
    int Attach(std::unique_ptr<platform::Gamepad> pad) {
        if (!pad) return -1;
        for (int q = 0; q < Input::kMaxGamepads; ++q)
            if (m_pads[q] && m_pads[q]->InstanceId() == pad->InstanceId()) return q;
        if (!pad->HasBuiltinMapping() &&
            !LooksLikeGamepad(pad->RawAxisCount(), pad->RawButtonCount(), pad->RawHatCount())) return -1;
        const platform::Gamepad& ref = *pad;
        const std::string key = DeviceKeyOf(ref);
        const int p = m_slots.Acquire(key);
        if (p < 0) { m_full = true; return -1; }
        m_pads[p] = std::move(pad);
        Announce(p);
        return p;
    }
    /// Open device `index` into a slot; returns the player index or -1.
    int Add(int deviceIndex) {
        platform::Backend* b = platform::Default();
        if (!b) return -1;
        std::unique_ptr<platform::Gamepad> c = b->OpenGamepad(deviceIndex);
        return Attach(std::move(c));
    }
    /// A controller was unplugged; frees its slot. Returns the player index or -1.
    int Remove(int instanceId) {
        for (int p = 0; p < Input::kMaxGamepads; ++p)
            if (m_pads[p] && m_pads[p]->InstanceId() == instanceId) {
                m_pads[p].reset();
                m_slots.Release(p);
                PadProfiles::Disconnect(p);
                Input::SetGamepadFamily(p, Input::PadFamily::Xbox);
                if (m_full) { m_full = false; Rescan(); }   // a pad that did not fit may fit now
                return p;
            }
        return -1;
    }
    /// Two players swap controllers.
    void Swap(int a, int b) {
        if (a < 0 || b < 0 || a >= Input::kMaxGamepads || b >= Input::kMaxGamepads || a == b) return;
        std::swap(m_pads[a], m_pads[b]);
        m_slots.Swap(a, b);
        for (int p : {a, b}) { if (m_pads[p]) Announce(p, false); else PadProfiles::Disconnect(p, false); }
    }
    platform::Gamepad* Pad(int player) const { return player >= 0 && player < Input::kMaxGamepads ? m_pads[player].get() : nullptr; }
    /// Feed every slot (empty ones as disconnected). Player 0 also drives `keys`.
    void Feed(std::vector<char>& keys) {
        int sa = 0, sb = 0;
        if (PadProfiles::TakeSwap(sa, sb)) Swap(sa, sb);
        for (int p = 0; p < Input::kMaxGamepads; ++p) {
            platform::Gamepad* pad = m_pads[p].get();
            if (pad) Observe(p, *pad);
            FeedPadAt(p, pad, p == 0 ? &keys : nullptr);
        }
    }
    void CloseAll() {
        for (int p = 0; p < Input::kMaxGamepads; ++p) if (m_pads[p]) { m_pads[p].reset(); m_slots.Release(p); PadProfiles::Disconnect(p); }
    }
private:
    static std::string DeviceKeyOf(const platform::Gamepad& g) {
        return DeviceKey(g.VendorId(), g.ProductId(), g.RawAxisCount(), g.RawButtonCount(), g.RawHatCount()) + "|" + (g.Name() ? g.Name() : "");
    }
    void Rescan() {
        platform::Backend* b = platform::Default();
        if (!b) return;
        for (int i = 0, n = b->GamepadDeviceCount(); i < n; ++i) if (b->IsGamepad(i)) Add(i);
    }
    /// Tell PadProfiles (and the pictures / labels) who is in slot p.
    void Announce(int p, bool event = true) {
        platform::Gamepad& g = *m_pads[p];
        const GamepadIdentity id = ReadPadIdentity(g);
        PadProfiles::Connect(p, g.InstanceId(), id, ReadPadRaw(g), g.HasBuiltinMapping(), event);   // a swap is silent
        Input::SetGamepadFamily(p, CoarseFamily(PadProfiles::Get(p).info.family));
        PadProfiles::Edit(p).touchpads = g.TouchpadCount();
        PadProfiles::Edit(p).hostGyro = g.HasSensor(0);
        PadProfiles::Edit(p).hostAccel = g.HasSensor(1);
        PadProfiles::Edit(p).hostTriggerRumble = g.HasTriggerRumble();
    }
    void Observe(int p, platform::Gamepad& g) {
        PadProfiles::Update(p, ReadPadRaw(g), g.PowerLevel());
        PadProfile& prof = PadProfiles::Edit(p);
        for (int f = 0; f < 2 && prof.touchpads > 0; ++f) {
            bool down; float x, y, pr;
            g.TouchpadFinger(0, f, down, x, y, pr);
            prof.finger[f].down = down; prof.finger[f].x = x; prof.finger[f].y = y;
        }
        if (prof.sensorsWanted) {   // only while something reads them: sensors cost battery on a wireless pad
            prof.sensorsWanted = false;
            if (prof.hostGyro) g.SensorData(0, prof.gyro);
            if (prof.hostAccel) g.SensorData(1, prof.accel);
        }
        int r, gr, b;
        if (PadProfiles::TakeLightbar(p, r, gr, b)) g.SetLightbar((uint8_t)r, (uint8_t)gr, (uint8_t)b);
        float l, rr, sec;
        if (PadProfiles::TakeTriggerRumble(p, l, rr, sec))
            g.RumbleTriggers((uint16_t)(l * 65535.0f), (uint16_t)(rr * 65535.0f), (uint32_t)(sec * 1000.0f));
        if (!prof.playerLedSet) { prof.playerLedSet = true; g.SetPlayerIndex(p); }   // the pad shows which player it is
    }
    std::unique_ptr<platform::Gamepad> m_pads[Input::kMaxGamepads];
    PadSlotAllocator m_slots{Input::kMaxGamepads};
    bool m_full = false;
};

} // namespace okay
