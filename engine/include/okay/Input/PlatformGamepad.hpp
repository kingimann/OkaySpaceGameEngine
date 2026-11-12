#pragma once
// Reads a game controller (through okay::platform, so SDL2 or our own evdev
// backend) into Input: both sticks, both triggers and every button, and plays
// Input::Rumble requests on it. Shared by the player and the editor's Play mode.
// Header-only; no SDL types appear here.
#include "okay/Input/Input.hpp"
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

/// Feed one controller's state as player `player` (pad may be null: empty slot).
/// Raw sticks and triggers go through that player's Input::GamepadSettings. When
/// `keys` is non-null (player 0 only, normally) the left stick, d-pad and A also
/// push W/A/S/D and Space, so games written against the keyboard play on a pad.
inline void FeedPadAt(int player, platform::Gamepad* pad, std::vector<char>* keys = nullptr) {
    using platform::GamepadAxis;
    using platform::GamepadButton;
    Vec2 left, right; unsigned mask = 0; float lt = 0.0f, rt = 0.0f;
    if (pad) {
        left = PadRawStick(*pad, GamepadAxis::LeftX, GamepadAxis::LeftY);
        right = PadRawStick(*pad, GamepadAxis::RightX, GamepadAxis::RightY);
        lt = pad->Axis(GamepadAxis::TriggerLeft) / 32767.0f;
        rt = pad->Axis(GamepadAxis::TriggerRight) / 32767.0f;
        static const GamepadButton kMap[] = {
            GamepadButton::A, GamepadButton::B, GamepadButton::X, GamepadButton::Y,
            GamepadButton::Back, GamepadButton::Start,
            GamepadButton::LeftShoulder, GamepadButton::RightShoulder,
            GamepadButton::DpadUp, GamepadButton::DpadDown,
            GamepadButton::DpadLeft, GamepadButton::DpadRight,
            GamepadButton::LeftStick, GamepadButton::RightStick};
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
    if (Input::TakeRumble(lo, hi, sec, player) && pad)   // nothing to shake: the request is dropped
        pad->Rumble((uint16_t)(lo * 65535.0f), (uint16_t)(hi * 65535.0f), (uint32_t)(sec * 1000.0f));
}

/// Feed this frame's first controller (pad may be null: nothing connected).
/// The left stick, d-pad and A also go into `keys` as W/A/S/D and Space.
inline void FeedPad(platform::Gamepad* pad, std::vector<char>& keys) {
    FeedPadAt(0, pad, &keys);
}

/// Up to Input::kMaxGamepads controllers, one per player slot, filled in the order
/// they are plugged in and freed when they are pulled out. Use it from the event
/// loop: Add(deviceIndex) on a controller-added event, Remove(instanceId) on a
/// controller-removed event (SDL_CONTROLLERDEVICE* or platform::EventType::Gamepad*),
/// Feed once per frame.
class PadSet {
public:
    /// Open every controller that is already plugged in.
    void OpenAll() {
        platform::Backend* b = platform::Default();
        if (!b) return;
        for (int i = 0, n = b->GamepadDeviceCount(); i < n; ++i) if (b->IsGamepad(i)) Add(i);
    }
    /// Open device `index` into the first free slot; returns the player index or -1.
    int Add(int deviceIndex) {
        platform::Backend* b = platform::Default();
        if (!b) return -1;
        for (int p = 0; p < Input::kMaxGamepads; ++p)
            if (!m_pads[p]) {
                std::unique_ptr<platform::Gamepad> c = b->OpenGamepad(deviceIndex);
                if (!c) return -1;
                for (int q = 0; q < Input::kMaxGamepads; ++q)   // already open (a rescan)
                    if (m_pads[q] && m_pads[q]->InstanceId() == c->InstanceId()) return q;
                m_pads[p] = std::move(c);
                return p;
            }
        return -1;
    }
    /// A controller was unplugged; frees its slot. Returns the player index or -1.
    int Remove(int instanceId) {
        for (int p = 0; p < Input::kMaxGamepads; ++p)
            if (m_pads[p] && m_pads[p]->InstanceId() == instanceId) { m_pads[p].reset(); return p; }
        return -1;
    }
    platform::Gamepad* Pad(int player) const { return player >= 0 && player < Input::kMaxGamepads ? m_pads[player].get() : nullptr; }
    /// Feed every slot (empty ones as disconnected). Player 0 also drives `keys`.
    void Feed(std::vector<char>& keys) {
        for (int p = 0; p < Input::kMaxGamepads; ++p) FeedPadAt(p, m_pads[p].get(), p == 0 ? &keys : nullptr);
    }
    void CloseAll() { for (auto& c : m_pads) c.reset(); }
private:
    std::unique_ptr<platform::Gamepad> m_pads[Input::kMaxGamepads];
};

} // namespace okay
