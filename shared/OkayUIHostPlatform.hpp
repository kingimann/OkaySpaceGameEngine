#pragma once
// Hosting OkayUI with okay::platform events instead of SDL ones (the launcher's own renderer, OKAY_LAUNCHER_RENDERER=own):
// the same InputCollector (OkayUIHost.hpp), fed from okay::platform::Event. Written from the two headers' contracts
// (okay::platform::Event in engine/include/okay/Platform/Platform.hpp, OkayUI::Input and KeyId in OkayUI.hpp).
//
// One difference to the SDL feed: our Key is the physical key (USB HID numbering, like SDL's scancodes), SDL's feed uses the
// layout's key code. On a layout where the letter keys move (AZERTY, Dvorak) Ctrl+A / C / X / V and the held-key state follow
// the key's position here, not its label. Typed text is the same on both (it comes from the text events).
#include "OkayUIHost.hpp"
#include "okay/Platform/Platform.hpp"

namespace okayuihost {

/// The OkayUI key for one of our physical keys (KeyId_None for the keys OkayUI does not name).
inline int KeyIdOfPlatform(okay::platform::Key key) {
    using namespace OkayUI;
    using K = okay::platform::Key;
    const int k = (int)key;
    if (k >= (int)K::A && k <= (int)K::Z) return KeyId_A + (k - (int)K::A);
    if (k >= (int)K::N1 && k <= (int)K::N9) return KeyId_1 + (k - (int)K::N1);
    if (key == K::N0) return KeyId_0;
    if (k >= (int)K::F1 && k <= (int)K::F12) return KeyId_F1 + (k - (int)K::F1);
    switch (key) {
    case K::Tab: return KeyId_Tab;          case K::Left: return KeyId_Left;         case K::Right: return KeyId_Right;
    case K::Up: return KeyId_Up;            case K::Down: return KeyId_Down;         case K::PageUp: return KeyId_PageUp;
    case K::PageDown: return KeyId_PageDown; case K::Home: return KeyId_Home;        case K::End: return KeyId_End;
    case K::Insert: return KeyId_Insert;    case K::Delete: return KeyId_Delete;     case K::Backspace: return KeyId_Backspace;
    case K::Space: return KeyId_Space;      case K::Return: case K::KpEnter: return KeyId_Enter;
    case K::Escape: return KeyId_Escape;
    case K::LCtrl: case K::RCtrl: return KeyId_Ctrl;
    case K::LShift: case K::RShift: return KeyId_Shift;
    case K::LAlt: case K::RAlt: return KeyId_Alt;
    default: return KeyId_None;
    }
}

/// Feed one okay::platform event to the collector (the host may also give it to ImGui; nothing is consumed).
inline void FeedPlatform(InputCollector& c, const okay::platform::Event& e) {
    using ET = okay::platform::EventType;
    switch (e.type) {
    case ET::TextInput: c.AddText(e.text.c_str()); break;
    case ET::MouseButtonDown:
        if (e.button == okay::platform::MouseLeft) c.AddButtonDown(0);
        else if (e.button == okay::platform::MouseRight) c.AddButtonDown(1);
        break;
    case ET::MouseWheel: c.AddWheel(e.wheelY); break;   // already up = +y whatever the OS scrolling direction is
    case ET::KeyDown:
    case ET::KeyUp:
        c.AddKey(KeyIdOfPlatform(e.key), e.type == ET::KeyDown, (e.mods & okay::platform::ModShift) != 0,
                 (e.mods & (okay::platform::ModCtrl | okay::platform::ModGui)) != 0);
        break;
    default: break;
    }
}

} // namespace okayuihost
