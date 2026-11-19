#pragma once
// The keys Play feeds into the game. The editor and the editor Play test both
// use this, so a test that fills a keyboard state exercises the same mapping
// the window uses (SDL scancodes -> the letters scripts read).
#include "okay/Input/Input.hpp"
#include "PlainTypes.hpp"   // SDL.h in a normal build; plain types only with OKAY_NO_SDL
#include "okay/Platform/Platform.hpp"
#include <functional>
#include <vector>

namespace okay::editor {

#ifndef OKAY_NO_SDL   // the SDL scancode version (tests/test_templates_work.cpp fills an SDL keyboard-state array)
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

#endif   // !OKAY_NO_SDL

/// The same mapping over the platform layer: `held(key)` says whether a key is down (Backend::IsKeyDown, or a test's array). Letters and digits are taken by
/// US position here; the editor itself asks the backend for the layout's letter (Backend::KeyForChar) before it gets here.
inline void CollectEditorPlayKeysPlatform(std::vector<char>& down, const std::function<bool(okay::platform::Key)>& held) {
    using K = okay::platform::Key;
    for (char c = 'a'; c <= 'z'; ++c) if (held((K)((int)K::A + (c - 'a')))) down.push_back(c);
    for (char c = '1'; c <= '9'; ++c) if (held((K)((int)K::N1 + (c - '1')))) down.push_back(c);
    if (held(K::N0)) down.push_back('0');
    if (held(K::Space)) down.push_back(' ');
    if (held(K::LShift) || held(K::RShift)) down.push_back(okay::Input::KeyShift);
    if (held(K::LCtrl) || held(K::RCtrl)) down.push_back(okay::Input::KeyCtrl);
    if (held(K::LGui) || held(K::RGui)) down.push_back(okay::Input::KeyCmd);
    if (held(K::Backspace)) down.push_back((char)8);
    if (held(K::Return) || held(K::KpEnter)) down.push_back('\r');
    if (held(K::Escape)) down.push_back((char)27);
    if (held(K::Up))    { down.push_back('w'); down.push_back(okay::Input::KeyUp); }
    if (held(K::Left))  { down.push_back('a'); down.push_back(okay::Input::KeyLeft); }
    if (held(K::Down))  { down.push_back('s'); down.push_back(okay::Input::KeyDown); }
    if (held(K::Right)) { down.push_back('d'); down.push_back(okay::Input::KeyRight); }
}

} // namespace okay::editor
