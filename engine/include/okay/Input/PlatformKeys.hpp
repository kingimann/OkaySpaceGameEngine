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
