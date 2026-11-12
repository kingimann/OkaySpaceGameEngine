#pragma once
// Physical keys the char-based keyboard feed does not cover: function keys, the
// navigation block, Tab and the left / right modifiers. They are read by physical
// position through okay::platform, so they are the same on every keyboard layout
// and on either backend. (Replaces the SDL-scancode version of this header.)
#include "okay/Input/Input.hpp"
#include "okay/Platform/Platform.hpp"
#include <vector>

namespace okay {

/// Add every held extended key to `down` (the vector given to Input::FeedKeys).
/// The generic Shift / Ctrl / Alt codes are derived by Input::FeedKeys from the
/// left / right ones. `held` is any callable bool(platform::Key), so the mapping
/// can be tested without a window or a keyboard.
template <class Held>
inline void AppendExtendedKeysWith(Held&& held, std::vector<char>& down) {
    using platform::Key;
    for (int n = 1; n <= 12; ++n)
        if (held((Key)((int)Key::F1 + (n - 1)))) down.push_back(Input::KeyF(n));
    struct Map { Key key; char out; };
    static const Map kMap[] = {
        {Key::Tab, '\t'},
        {Key::Insert, Input::KeyInsert}, {Key::Delete, Input::KeyDelete},
        {Key::Home, Input::KeyHome}, {Key::End, Input::KeyEnd},
        {Key::PageUp, Input::KeyPageUp}, {Key::PageDown, Input::KeyPageDown},
        {Key::LShift, Input::KeyLShift}, {Key::RShift, Input::KeyRShift},
        {Key::LCtrl, Input::KeyLCtrl}, {Key::RCtrl, Input::KeyRCtrl},
        {Key::LAlt, Input::KeyLAlt}, {Key::RAlt, Input::KeyRAlt},
    };
    for (const Map& m : kMap) if (held(m.key)) down.push_back(m.out);
}

inline void AppendExtendedKeys(platform::Backend& pb, std::vector<char>& down) {
    AppendExtendedKeysWith([&pb](platform::Key k) { return pb.IsKeyDown(k); }, down);
}

/// The whole keyboard feed a running game sees: letters and digits by the layout's
/// character (`charKey`: AZERTY WASD works), Space, generic Shift / Ctrl, editing
/// keys, arrows (as themselves and as WASD), Escape when `includeEscape`, and the
/// extended keys above. Shared by the built player and the editor's Play mode so a
/// game reads the same keys in both.
template <class Held, class CharKey>
inline void AppendPlayKeysWith(Held&& held, CharKey&& charKey, std::vector<char>& down, bool includeEscape) {
    using platform::Key;
    for (char c = 'a'; c <= 'z'; ++c) if (held(charKey(c))) down.push_back(c);
    for (char c = '0'; c <= '9'; ++c) if (held(charKey(c))) down.push_back(c);
    if (held(Key::Space)) down.push_back(' ');
    if (held(Key::LShift) || held(Key::RShift)) down.push_back(Input::KeyShift);   // sprint
    if (held(Key::LCtrl) || held(Key::RCtrl)) down.push_back(Input::KeyCtrl);      // crouch
    if (held(Key::Backspace)) down.push_back((char)8);
    if (held(Key::Return) || held(Key::KpEnter)) down.push_back('\r');
    if (includeEscape) down.push_back((char)27);
    if (held(Key::Up))    { down.push_back('w'); down.push_back(Input::KeyUp); }
    if (held(Key::Left))  { down.push_back('a'); down.push_back(Input::KeyLeft); }
    if (held(Key::Down))  { down.push_back('s'); down.push_back(Input::KeyDown); }
    if (held(Key::Right)) { down.push_back('d'); down.push_back(Input::KeyRight); }
    AppendExtendedKeysWith(held, down);
}

/// AppendPlayKeysWith over a live backend. `includeEscape` says whether the game
/// should see Escape this frame (the player: while held; the editor: only when
/// Play decided Escape belongs to the game).
inline void AppendPlayKeys(platform::Backend& pb, std::vector<char>& down, bool includeEscape) {
    AppendPlayKeysWith([&pb](platform::Key k) { return pb.IsKeyDown(k); },
                       [&pb](char c) { return pb.KeyForChar(c); }, down, includeEscape);
}

} // namespace okay
