#pragma once
// Physical keys the char-based keyboard feed does not cover: function keys, the
// navigation block, Tab, the left / right modifiers, punctuation, the numpad and the
// lock keys. They are read by physical position through okay::platform, so they are
// the same on every keyboard layout and on either backend. (Replaces the
// SDL-scancode version of this header.)
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
/// or digit on that key, Space, Enter, Escape, Backspace, and an arrow key as itself (Input::FeedKeys
/// adds its W A S D letter for the raw reads, as for a held arrow). `charKey(c)` gives the key for a character
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
        case Key::Up:        out.push_back(Input::KeyUp); break;
        case Key::Left:      out.push_back(Input::KeyLeft); break;
        case Key::Down:      out.push_back(Input::KeyDown); break;
        case Key::Right:     out.push_back(Input::KeyRight); break;
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
    // Arrows are fed as themselves; Input::FeedKeys adds W A S D for the raw reads (and not for bindings).
    if (held(Key::Up))    down.push_back(Input::KeyUp);
    if (held(Key::Left))  down.push_back(Input::KeyLeft);
    if (held(Key::Down))  down.push_back(Input::KeyDown);
    if (held(Key::Right)) down.push_back(Input::KeyRight);
}
/// The same on a backend: held keys from Backend::IsKeyDown, letters by KeyForLetter, Escape when it is held.
inline void AppendHeldMainKeys(platform::Backend& pb, std::vector<char>& down) {
    AppendHeldMainKeys([&pb](platform::Key k) { return pb.IsKeyDown(k); }, [&pb](char c) { return KeyForLetter(pb, c); },
                       pb.IsKeyDown(platform::Key::Escape), down);
}

/// Add every held extended key to `down` (the vector given to Input::FeedKeys): F1-F12, Tab, the navigation block,
/// left / right modifiers, punctuation (as its US character: the key right of P is '[' on every layout), the numpad
/// (digits and signs with codes of their own; numpad Enter as itself, and as Enter through AppendHeldMainKeys), Caps
/// Lock, Print Screen, Scroll Lock, Pause, Num Lock and Menu. `held(key)` says whether a physical key is down.
/// The generic Shift / Ctrl / Alt codes are derived by Input::FeedKeys from the left / right ones.
template <class Held>
inline void AppendExtendedKeys(Held held, std::vector<char>& down) {
    using platform::Key;
    for (int n = 1; n <= 12; ++n)
        if (held((Key)((int)Key::F1 + (n - 1)))) down.push_back(Input::KeyF(n));
    for (int n = 1; n <= 9; ++n)
        if (held((Key)((int)Key::Kp1 + (n - 1)))) down.push_back(Input::KeyKp(n));
    struct Map { Key key; char out; };
    static const Map kMap[] = {
        {Key::Tab, '\t'},
        {Key::Insert, Input::KeyInsert}, {Key::Delete, Input::KeyDelete},
        {Key::Home, Input::KeyHome}, {Key::End, Input::KeyEnd},
        {Key::PageUp, Input::KeyPageUp}, {Key::PageDown, Input::KeyPageDown},
        {Key::LShift, Input::KeyLShift}, {Key::RShift, Input::KeyRShift},
        {Key::LCtrl, Input::KeyLCtrl}, {Key::RCtrl, Input::KeyRCtrl},
        {Key::LAlt, Input::KeyLAlt}, {Key::RAlt, Input::KeyRAlt},
        {Key::Minus, '-'}, {Key::Equals, '='}, {Key::LeftBracket, '['}, {Key::RightBracket, ']'}, {Key::Backslash, '\\'},
        {Key::Semicolon, ';'}, {Key::Apostrophe, '\''}, {Key::Grave, '`'}, {Key::Comma, ','}, {Key::Period, '.'}, {Key::Slash, '/'},
        {Key::Kp0, Input::KeyKp(0)}, {Key::KpDivide, Input::KeyKpDivide}, {Key::KpMultiply, Input::KeyKpMultiply},
        {Key::KpMinus, Input::KeyKpMinus}, {Key::KpPlus, Input::KeyKpPlus}, {Key::KpEnter, Input::KeyKpEnter},
        {Key::KpPeriod, Input::KeyKpPeriod},
        {Key::CapsLock, Input::KeyCapsLock}, {Key::PrintScreen, Input::KeyPrintScreen}, {Key::ScrollLock, Input::KeyScrollLock},
        {Key::Pause, Input::KeyPause}, {Key::NumLock, Input::KeyNumLock}, {Key::Application, Input::KeyMenu},
    };
    for (const Map& m : kMap) if (held(m.key)) down.push_back(m.out);
}
/// The same on a backend (Backend::IsKeyDown).
inline void AppendExtendedKeys(platform::Backend& pb, std::vector<char>& down) {
    AppendExtendedKeys([&pb](platform::Key k) { return pb.IsKeyDown(k); }, down);
}

/// The Input::FeedMouse button mask (bit 0 left, 1 right, 2 middle, 3 and 4 the side buttons) for a platform mouse
/// mask (Backend::MouseState / RelativeMouseState: MouseMask bits).
inline unsigned InputMouseMask(uint32_t platformMask) {
    unsigned mask = 0;
    if (platformMask & platform::MouseMask(platform::MouseLeft))   mask |= 1u << 0;
    if (platformMask & platform::MouseMask(platform::MouseRight))  mask |= 1u << 1;
    if (platformMask & platform::MouseMask(platform::MouseMiddle)) mask |= 1u << 2;
    if (platformMask & platform::MouseMask(platform::MouseX1))     mask |= 1u << 3;
    if (platformMask & platform::MouseMask(platform::MouseX2))     mask |= 1u << 4;
    return mask;
}
/// The Input mouse button (0..4) of a platform MouseButton (an event's `button`), or -1.
inline int InputMouseButton(int platformButton) {
    switch (platformButton) {
        case platform::MouseLeft:   return 0;
        case platform::MouseRight:  return 1;
        case platform::MouseMiddle: return 2;
        case platform::MouseX1:     return 3;
        case platform::MouseX2:     return 4;
        default:                    return -1;
    }
}

} // namespace okay
