#pragma once
// Physical keys the char-based keyboard feed does not cover: function keys, the
// navigation block, Tab and the left / right modifiers. They are read by physical
// position through okay::platform, so they are the same on every keyboard layout
// and on either backend. (Replaces the SDL-scancode version of this header.)
#include "okay/Input/Input.hpp"
#include "okay/Platform/Platform.hpp"
#include <vector>

namespace okay {

/// The key a letter or digit is on. `found` is what the layout reported for that character
/// (Backend::KeyForChar). Layouts with no Latin letters (Russian, Greek, Arabic, Hebrew...)
/// report "unknown" for 'w', which used to leave W A S D dead; then the key at that letter's
/// place on a US keyboard is used instead, so the game stays playable. Latin layouts (AZERTY,
/// QWERTZ, Dvorak) are unchanged: the key labelled W is still W, unless the player turned on
/// Input::SetKeysByPosition (then every letter and digit is read at its US key position).
inline platform::Key KeyForLetter(platform::Key found, char c) {
    using platform::Key;
    if (found != Key::Unknown && !Input::KeysByPosition()) return found;   // "by position" (Input::SetKeysByPosition): always the US place
    if (c >= 'a' && c <= 'z') return (Key)((int)Key::A + (c - 'a'));
    if (c >= '1' && c <= '9') return (Key)((int)Key::N1 + (c - '1'));
    if (c == '0') return Key::N0;
    return Key::Unknown;
}
inline platform::Key KeyForLetter(platform::Backend& pb, char c) { return KeyForLetter(pb.KeyForChar(c), c); }

/// The characters Input.FeedKeys uses for platform key `k`, for Input::FeedKeyTap: the letter
/// or digit on that key, Space, Enter, Escape, Backspace, and an arrow key as itself plus W A
/// S D (the same pairing the held-key feed makes). `charKey(c)` gives the key for a character
/// (KeyForLetter on the backend). Empty when the key means nothing to the game.
template <class CharKey>
inline void TapCharsForKey(platform::Key k, CharKey charKey, std::vector<char>& out) {
    using platform::Key;
    if (k == Key::Unknown) return;
    for (char c = 'a'; c <= 'z'; ++c) if (charKey(c) == k) out.push_back(c);
    for (char c = '0'; c <= '9'; ++c) if (charKey(c) == k) out.push_back(c);
    switch (k) {
        case Key::Space:     out.push_back(' '); break;
        case Key::Return: case Key::KpEnter: out.push_back('\r'); break;
        case Key::Escape:    out.push_back((char)27); break;
        case Key::Backspace: out.push_back((char)8); break;
        case Key::Tab:       out.push_back('\t'); break;
        case Key::Up:        out.push_back('w'); out.push_back(Input::KeyUp); break;
        case Key::Left:      out.push_back('a'); out.push_back(Input::KeyLeft); break;
        case Key::Down:      out.push_back('s'); out.push_back(Input::KeyDown); break;
        case Key::Right:     out.push_back('d'); out.push_back(Input::KeyRight); break;
        default: break;
    }
}

/// Add the characters Input::FeedKeys takes for the main keys held right now: letters and digits by the layout
/// (`charKey(c)`, normally KeyForLetter on the backend), Space, Shift, Ctrl, Command (Gui), Backspace, Enter, the
/// arrows as themselves and as W A S D, and Escape when `escape` is true (the caller decides: the player feeds the
/// held key, the editor its own release rule). `held(key)` says whether a physical key is down. The one copy used
/// by the player and the editor's Play (and their tests, with faked keys). Extended keys: AppendExtendedKeys.
template <class Held, class CharKey>
inline void AppendHeldMainKeys(Held held, CharKey charKey, bool escape, std::vector<char>& down) {
    using platform::Key;
    for (char c = 'a'; c <= 'z'; ++c) if (held(charKey(c))) down.push_back(c);
    for (char c = '0'; c <= '9'; ++c) if (held(charKey(c))) down.push_back(c);
    if (held(Key::Space)) down.push_back(' ');
    if (held(Key::LShift) || held(Key::RShift)) down.push_back(Input::KeyShift);   // sprint
    if (held(Key::LCtrl) || held(Key::RCtrl)) down.push_back(Input::KeyCtrl);      // crouch
    if (held(Key::LGui) || held(Key::RGui)) down.push_back(Input::KeyCmd);         // Command, for copy / paste
    if (held(Key::Backspace)) down.push_back((char)8);                             // text editing
    if (held(Key::Return) || held(Key::KpEnter)) down.push_back('\r');
    if (escape) down.push_back((char)27);
    // Arrows count as themselves and as W A S D, so axis_x / axis_y and key("w") agree.
    if (held(Key::Up))    { down.push_back('w'); down.push_back(Input::KeyUp); }
    if (held(Key::Left))  { down.push_back('a'); down.push_back(Input::KeyLeft); }
    if (held(Key::Down))  { down.push_back('s'); down.push_back(Input::KeyDown); }
    if (held(Key::Right)) { down.push_back('d'); down.push_back(Input::KeyRight); }
}
/// The same on a backend: held keys from Backend::IsKeyDown, letters by KeyForLetter, Escape when it is held.
inline void AppendHeldMainKeys(platform::Backend& pb, std::vector<char>& down) {
    AppendHeldMainKeys([&pb](platform::Key k) { return pb.IsKeyDown(k); }, [&pb](char c) { return KeyForLetter(pb, c); },
                       pb.IsKeyDown(platform::Key::Escape), down);
}

/// Add every held extended key to `down` (the vector given to Input::FeedKeys).
/// The generic Shift / Ctrl / Alt codes are derived by Input::FeedKeys from the
/// left / right ones.
inline void AppendExtendedKeys(platform::Backend& pb, std::vector<char>& down) {
    using platform::Key;
    for (int n = 1; n <= 12; ++n)
        if (pb.IsKeyDown((Key)((int)Key::F1 + (n - 1)))) down.push_back(Input::KeyF(n));
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
    for (const Map& m : kMap) if (pb.IsKeyDown(m.key)) down.push_back(m.out);
}

} // namespace okay
