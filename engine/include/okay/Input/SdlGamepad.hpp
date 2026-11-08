#pragma once
// Reads an SDL game controller into Input: both sticks, both triggers and every
// button, and plays Input::Rumble requests on it. Shared by the player and the
// editor's Play mode. Header-only so the engine library stays SDL-free; include
// it only from code that already links SDL.
#include "okay/Input/Input.hpp"
#include <SDL_gamecontroller.h>   // not SDL.h: on Windows it renames every `main` (Camera::main) to SDL_main
#include <cmath>
#include <vector>

namespace okay {

/// A raw stick, -1..1 with up as +y. Dead zone, outer zone and curve are applied
/// by Input::FeedGamepadRaw from Input::GamepadSettings(player).
inline Vec2 SdlRawStick(SDL_GameController* pad, SDL_GameControllerAxis ax, SDL_GameControllerAxis ay) {
    return {SDL_GameControllerGetAxis(pad, ax) / 32767.0f, -SDL_GameControllerGetAxis(pad, ay) / 32767.0f};
}

/// Feed one controller's state as player `player` (pad may be null: empty slot).
/// Raw sticks and triggers go through that player's Input::GamepadSettings. When
/// `keys` is non-null (player 0 only, normally) the left stick, d-pad and A also
/// push W/A/S/D and Space, so games written against the keyboard play on a pad.
inline void FeedSdlGamepadAt(int player, SDL_GameController* pad, std::vector<char>* keys = nullptr) {
    Vec2 left, right; unsigned mask = 0; float lt = 0.0f, rt = 0.0f;
    if (pad) {
        left = SdlRawStick(pad, SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY);
        right = SdlRawStick(pad, SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY);
        lt = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0f;
        rt = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0f;
        static const SDL_GameControllerButton kMap[] = {
            SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
            SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START,
            SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
            SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
            SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
            SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK};
        for (int i = 0; i < (int)(sizeof(kMap) / sizeof(kMap[0])); ++i)
            if (SDL_GameControllerGetButton(pad, kMap[i])) mask |= 1u << i;
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
        SDL_GameControllerRumble(pad, (Uint16)(lo * 65535.0f), (Uint16)(hi * 65535.0f), (Uint32)(sec * 1000.0f));
}

/// Feed this frame's first controller (pad may be null: nothing connected).
/// The left stick, d-pad and A also go into `keys` as W/A/S/D and Space.
inline void FeedSdlGamepad(SDL_GameController* pad, std::vector<char>& keys) {
    FeedSdlGamepadAt(0, pad, &keys);
}

/// Up to Input::kMaxGamepads controllers, one per player slot, filled in the order
/// they are plugged in and freed when they are pulled out. Use it from the SDL
/// event loop: Add(e.cdevice.which) on SDL_CONTROLLERDEVICEADDED, Remove(e.cdevice.which)
/// on SDL_CONTROLLERDEVICEREMOVED (both are device / instance ids), Feed once per frame.
class SdlGamepadSet {
public:
    /// Open every controller that is already plugged in.
    void OpenAll() {
        for (int i = 0, n = SDL_NumJoysticks(); i < n; ++i) if (SDL_IsGameController(i)) Add(i);
    }
    /// Open device `index` into the first free slot; returns the player index or -1.
    int Add(int deviceIndex) {
        for (int p = 0; p < Input::kMaxGamepads; ++p)
            if (!m_pads[p]) {
                SDL_GameController* c = SDL_GameControllerOpen(deviceIndex);
                if (!c) return -1;
                const SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c));
                for (int q = 0; q < Input::kMaxGamepads; ++q)   // already open (a rescan)
                    if (m_pads[q] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(m_pads[q])) == id) {
                        SDL_GameControllerClose(c);
                        return q;
                    }
                m_pads[p] = c;
                return p;
            }
        return -1;
    }
    /// A controller was unplugged; frees its slot. Returns the player index or -1.
    int Remove(SDL_JoystickID instanceId) {
        for (int p = 0; p < Input::kMaxGamepads; ++p)
            if (m_pads[p] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(m_pads[p])) == instanceId) {
                SDL_GameControllerClose(m_pads[p]);
                m_pads[p] = nullptr;
                return p;
            }
        return -1;
    }
    SDL_GameController* Pad(int player) const { return player >= 0 && player < Input::kMaxGamepads ? m_pads[player] : nullptr; }
    /// Feed every slot (empty ones as disconnected). Player 0 also drives `keys`.
    void Feed(std::vector<char>& keys) {
        for (int p = 0; p < Input::kMaxGamepads; ++p) FeedSdlGamepadAt(p, m_pads[p], p == 0 ? &keys : nullptr);
    }
    void CloseAll() {
        for (auto& c : m_pads) if (c) { SDL_GameControllerClose(c); c = nullptr; }
    }
private:
    SDL_GameController* m_pads[Input::kMaxGamepads] = {};
};

} // namespace okay
