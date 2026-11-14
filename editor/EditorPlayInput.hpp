#pragma once
// The keys Play feeds into the game. The editor and the editor Play test both
// use this, so a test that fills a keyboard state exercises the same mapping
// the window uses (SDL scancodes -> the letters scripts read).
#include "okay/Input/Input.hpp"
#include <SDL.h>
#include <vector>

namespace okay::editor {

inline void CollectEditorPlayKeys(std::vector<char>& down, const Uint8* ks, int nkeys) {
    if (!ks || nkeys <= 0) return;
    auto on = [&](SDL_Scancode s) { return (int)s >= 0 && (int)s < nkeys && ks[s] != 0; };
    auto letter = [&](char c) {
        SDL_Scancode sc = SDL_GetScancodeFromKey((SDL_Keycode)c);
        // Before the keymap exists (a headless test) the letter's own scancode is the key.
        if (sc == SDL_SCANCODE_UNKNOWN && c >= 'a' && c <= 'z')
            sc = (SDL_Scancode)(SDL_SCANCODE_A + (c - 'a'));
        if (sc == SDL_SCANCODE_UNKNOWN && c >= '0' && c <= '9')
            sc = (SDL_Scancode)(SDL_SCANCODE_0 + (c - '0'));
        return on(sc);
    };
    for (char c = 'a'; c <= 'z'; ++c)
        if (letter(c)) down.push_back(c);
    for (char c = '0'; c <= '9'; ++c)
        if (letter(c)) down.push_back(c);
    if (on(SDL_SCANCODE_SPACE)) down.push_back(' ');
    if (on(SDL_SCANCODE_LSHIFT) || on(SDL_SCANCODE_RSHIFT)) down.push_back(okay::Input::KeyShift);
    if (on(SDL_SCANCODE_LCTRL) || on(SDL_SCANCODE_RCTRL)) down.push_back(okay::Input::KeyCtrl);
    if (on(SDL_SCANCODE_LGUI) || on(SDL_SCANCODE_RGUI)) down.push_back(okay::Input::KeyCmd);
    if (on(SDL_SCANCODE_BACKSPACE)) down.push_back((char)8);
    if (on(SDL_SCANCODE_RETURN) || on(SDL_SCANCODE_KP_ENTER)) down.push_back('\r');
    if (on(SDL_SCANCODE_ESCAPE)) down.push_back((char)27);
    if (on(SDL_SCANCODE_UP))    { down.push_back('w'); down.push_back(okay::Input::KeyUp); }
    if (on(SDL_SCANCODE_LEFT))  { down.push_back('a'); down.push_back(okay::Input::KeyLeft); }
    if (on(SDL_SCANCODE_DOWN))  { down.push_back('s'); down.push_back(okay::Input::KeyDown); }
    if (on(SDL_SCANCODE_RIGHT)) { down.push_back('d'); down.push_back(okay::Input::KeyRight); }
}

} // namespace okay::editor
