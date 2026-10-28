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

/// A stick with a radial dead zone, rescaled so it still reaches 1 at full tilt.
inline Vec2 SdlStick(SDL_GameController* pad, SDL_GameControllerAxis ax, SDL_GameControllerAxis ay) {
    constexpr float kDead = 0.18f;
    const float x = SDL_GameControllerGetAxis(pad, ax) / 32767.0f;
    const float y = -SDL_GameControllerGetAxis(pad, ay) / 32767.0f;   // up is +y
    const float len = std::sqrt(x * x + y * y);
    if (len < kDead) return {0.0f, 0.0f};
    const float scale = std::fmin(1.0f, (len - kDead) / (1.0f - kDead)) / len;
    return {x * scale, y * scale};
}

/// Feed this frame's controller state (pad may be null: nothing connected).
/// The left stick, d-pad and A also go into `keys` as W/A/S/D and Space, so
/// games written against the keyboard play on a controller too.
inline void FeedSdlGamepad(SDL_GameController* pad, std::vector<char>& keys) {
    Vec2 left, right; unsigned mask = 0; float lt = 0.0f, rt = 0.0f;
    if (pad) {
        left = SdlStick(pad, SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY);
        right = SdlStick(pad, SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY);
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
        if (lt > 0.5f) mask |= 1u << Input::PadLT;
        if (rt > 0.5f) mask |= 1u << Input::PadRT;
        if (left.x > 0.4f  || (mask & (1u << Input::PadRight))) keys.push_back('d');
        if (left.x < -0.4f || (mask & (1u << Input::PadLeft)))  keys.push_back('a');
        if (left.y > 0.4f  || (mask & (1u << Input::PadUp)))    keys.push_back('w');
        if (left.y < -0.4f || (mask & (1u << Input::PadDown)))  keys.push_back('s');
        if (mask & (1u << Input::PadA)) keys.push_back(' ');

        float lo, hi, sec;
        if (Input::TakeRumble(lo, hi, sec))
            SDL_GameControllerRumble(pad, (Uint16)(lo * 65535.0f), (Uint16)(hi * 65535.0f), (Uint32)(sec * 1000.0f));
    } else {
        float lo, hi, sec;
        Input::TakeRumble(lo, hi, sec);   // nothing to shake: drop the request
    }
    Input::FeedGamepad(left, mask);
    Input::FeedGamepadExtra(right, lt, rt, pad != nullptr);
}

} // namespace okay
