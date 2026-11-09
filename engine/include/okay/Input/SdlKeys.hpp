#pragma once
// Physical keys the char-based keyboard feed does not cover: function keys, the
// navigation block, Tab and the left / right modifiers. They come from SDL
// scancodes, so they are the same on every keyboard layout. Include only from code
// that already links SDL.
#include "okay/Input/Input.hpp"
#include <SDL_scancode.h>
#include <SDL_stdinc.h>
#include <vector>

namespace okay {

/// Add every held extended key to `down` (the vector given to Input::FeedKeys).
/// `ks` is SDL_GetKeyboardState(nullptr). The generic Shift / Ctrl / Alt codes are
/// derived by Input::FeedKeys from the left / right ones.
inline void AppendSdlExtendedKeys(const Uint8* ks, std::vector<char>& down) {
    for (int n = 1; n <= 12; ++n)
        if (ks[SDL_SCANCODE_F1 + (n - 1)]) down.push_back(Input::KeyF(n));
    struct Map { int scancode; char key; };
    static const Map kMap[] = {
        {SDL_SCANCODE_TAB, '\t'},
        {SDL_SCANCODE_INSERT, Input::KeyInsert}, {SDL_SCANCODE_DELETE, Input::KeyDelete},
        {SDL_SCANCODE_HOME, Input::KeyHome}, {SDL_SCANCODE_END, Input::KeyEnd},
        {SDL_SCANCODE_PAGEUP, Input::KeyPageUp}, {SDL_SCANCODE_PAGEDOWN, Input::KeyPageDown},
        {SDL_SCANCODE_LSHIFT, Input::KeyLShift}, {SDL_SCANCODE_RSHIFT, Input::KeyRShift},
        {SDL_SCANCODE_LCTRL, Input::KeyLCtrl}, {SDL_SCANCODE_RCTRL, Input::KeyRCtrl},
        {SDL_SCANCODE_LALT, Input::KeyLAlt}, {SDL_SCANCODE_RALT, Input::KeyRAlt},
    };
    for (const Map& m : kMap) if (ks[m.scancode]) down.push_back(m.key);
}

} // namespace okay
