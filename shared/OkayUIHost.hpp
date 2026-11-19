#pragma once
// Hosting OkayUI inside an SDL + Dear ImGui application (the launcher, the editor). This is the part that is the same for
// every host: turning SDL events into the OkayUI::Input of one frame, and deciding who owns the mouse and the keyboard
// when an OkayUI region sits inside an ImGui window. It has no ImGui in it, so the tests can drive it with plain SDL_Event
// structs (tests/test_okayui_host.cpp).
//
// The frame, as the apps run it:
//     events:   collector.Feed(e)                       for every SDL event, next to ImGui_ImplSDL2_ProcessEvent
//     start:    OkayUI::BeginFrame(collector.Build(...))   before the ImGui panels are submitted
//     panels:   a hosted screen asks ImGui for a rectangle (an InvisibleButton over it, so ImGui treats it as an item and
//               not as free window space), tells the host with Region::Offer(rect, hovered), then draws its OkayUI screen
//               into the rectangle
//     end:      OkayUI::EndFrame(renderer)              after ImGui's draw data, so OkayUI is on top
// OkayUI draws on top of every ImGui window, so a hosted region must not be one that other ImGui windows or menus are
// meant to cover (documented in docs/own_ui_toolkit.md, section 5).
#include "okay/UI/OkayUI.hpp"
#include "okay/Platform/Platform.hpp"

// SDL.h on Windows renames every `main` to SDL_main (which breaks members such as Camera::main in the engine headers):
// the apps run their own entry point, so keep that rename off wherever this header is the first to include SDL.
#ifndef SDL_MAIN_HANDLED
#  define SDL_MAIN_HANDLED
#endif
#include <SDL.h>

#include <cstring>

namespace okayuihost {

/// Collects what OkayUI needs from SDL events between two frames.
class InputCollector {
public:
    /// Feed every SDL event (the host may also pass it to ImGui; the collector never consumes events).
    void Feed(const SDL_Event& e) {
        switch (e.type) {
        case SDL_TEXTINPUT: {
            const std::size_t have = std::strlen(m_text);
            const std::size_t add = std::strlen(e.text.text);
            if (have + add < sizeof(m_text)) std::memcpy(m_text + have, e.text.text, add + 1);   // a full buffer drops the rest
            break;
        }
        case SDL_MOUSEBUTTONDOWN:   // a press and release that both arrive before one frame must still show as one pressed frame
            if (e.button.button == SDL_BUTTON_LEFT) m_leftTap = true;
            else if (e.button.button == SDL_BUTTON_RIGHT) m_rightTap = true;
            break;
        case SDL_MOUSEWHEEL: {
            float y = (float)e.wheel.y;
            if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) y = -y;
            m_wheel += y;
            break;
        }
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            m_shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
            const bool ctrl = (e.key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0;
            m_ctrl = ctrl;
            {   // held state of the named keys (Input::keyDown); a press and release between two frames still shows for one frame
                const int id = KeyIdOf(e.key.keysym.sym);
                if (id != OkayUI::KeyId_None) {
                    m_keyHeld[id] = (e.type == SDL_KEYDOWN);
                    if (e.type == SDL_KEYDOWN) m_keyTap[id] = true;
                }
            }
            if (e.type != SDL_KEYDOWN) break;                 // key repeat arrives as more KEYDOWN events: each one is an edge
            switch (e.key.keysym.sym) {
            case SDLK_BACKSPACE: m_back = true; break;
            case SDLK_DELETE:    m_del = true; break;
            case SDLK_LEFT:      m_left = true; break;
            case SDLK_RIGHT:     m_right = true; break;
            case SDLK_HOME:      m_home = true; break;
            case SDLK_END:       m_end = true; break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:  m_enter = true; break;
            case SDLK_ESCAPE:    m_escape = true; break;
            case SDLK_UP:        m_up = true; break;
            case SDLK_DOWN:      m_down = true; break;
            case SDLK_a:         if (ctrl) m_selectAll = true; break;
            case SDLK_c:         if (ctrl) m_copy = true; break;
            case SDLK_x:         if (ctrl) m_cut = true; break;
            case SDLK_v:         if (ctrl) m_paste = true; break;
            default: break;
            }
            break;
        }
        default: break;
        }
    }

    /// The same as Feed(SDL_Event) for an okay::platform event, so an app that polls the seam needs no SDL event here.
    /// Only the kinds OkayUI wants are read (text, left and right button down, wheel, key down and up); any other is ignored.
    /// `layoutChar` is what the key types on the current layout ('a'..'z', '0'..'9', ' '; 0 = none): Backend::CharForKey(e.key).
    /// It decides the Ctrl+A/C/X/V shortcuts and the letter keys the way the SDL version does with the key symbol (an AZERTY
    /// keyboard's Ctrl+Q is select-all there, because its "a" key is where "q" is). Pass 0 to assume a US layout.
    void Feed(const okay::platform::Event& e, char layoutChar = 0) {
        using okay::platform::EventType;
        using okay::platform::Key;
        switch (e.type) {
        case EventType::TextInput: {
            const std::size_t have = std::strlen(m_text);
            const std::size_t add = e.text.size();
            if (have + add < sizeof(m_text)) { std::memcpy(m_text + have, e.text.data(), add); m_text[have + add] = 0; }   // a full buffer drops the rest
            break;
        }
        case EventType::MouseButtonDown:
            if (e.button == okay::platform::MouseLeft) m_leftTap = true;
            else if (e.button == okay::platform::MouseRight) m_rightTap = true;
            break;
        case EventType::MouseWheel:
            m_wheel += e.wheelY;   // already "up = +y" whatever the OS scrolling setting is (the SDL path undoes SDL's flip to get the same)
            break;
        case EventType::KeyDown:
        case EventType::KeyUp: {
            const bool down = e.type == EventType::KeyDown;
            m_shift = (e.mods & okay::platform::ModShift) != 0;
            const bool ctrl = (e.mods & (okay::platform::ModCtrl | okay::platform::ModGui)) != 0;
            m_ctrl = ctrl;
            if (!layoutChar) layoutChar = UsCharOf(e.key);
            {
                const int id = KeyIdOf(e.key, layoutChar);
                if (id != OkayUI::KeyId_None) { m_keyHeld[id] = down; if (down) m_keyTap[id] = true; }
            }
            if (!down) break;
            switch (e.key) {
            case Key::Backspace: m_back = true; break;
            case Key::Delete:    m_del = true; break;
            case Key::Left:      m_left = true; break;
            case Key::Right:     m_right = true; break;
            case Key::Home:      m_home = true; break;
            case Key::End:       m_end = true; break;
            case Key::Return: case Key::KpEnter: m_enter = true; break;
            case Key::Escape:    m_escape = true; break;
            case Key::Up:        m_up = true; break;
            case Key::Down:      m_down = true; break;
            default:
                if (ctrl) {
                    if (layoutChar == 'a') m_selectAll = true;
                    else if (layoutChar == 'c') m_copy = true;
                    else if (layoutChar == 'x') m_cut = true;
                    else if (layoutChar == 'v') m_paste = true;
                }
                break;
            }
            break;
        }
        default: break;
        }
    }

    /// The input of the frame about to start. A mouse button that went down and up again between two frames (a fast click, or a
/// slow frame) is reported down for this frame and up for the next, so the click is not lost. `deliverKeys` is whether OkayUI may have the keyboard this frame (a text
    /// field has focus and no ImGui field does); when it is false the typed text and key edges are dropped, not kept, so
    /// they do not arrive late. The one-shot state is cleared by this call.
    OkayUI::Input Build(float mouseX, float mouseY, bool leftDown, bool rightDown, bool blocked, bool deliverKeys) {
        OkayUI::Input in;
        in.mouseX = mouseX; in.mouseY = mouseY;
        in.mouseDown = leftDown || m_leftTap; in.rightDown = rightDown || m_rightTap; in.blocked = blocked;
        in.wheel = m_wheel;
        if (deliverKeys) {
            std::memcpy(m_out, m_text, sizeof(m_out));
            in.text = m_out[0] ? m_out : nullptr;
            in.backspace = m_back; in.del = m_del; in.left = m_left; in.right = m_right;
            in.home = m_home; in.end = m_end; in.enter = m_enter; in.escape = m_escape; in.selectAll = m_selectAll;
            in.shift = m_shift; in.ctrl = m_ctrl;
            in.up = m_up; in.down = m_down; in.copy = m_copy; in.cut = m_cut; in.paste = m_paste;
            for (int k = 0; k < OkayUI::KeyId_COUNT; ++k) in.keyDown[k] = m_keyHeld[k] || m_keyTap[k];
        }
        for (int k = 0; k < OkayUI::KeyId_COUNT; ++k) m_keyTap[k] = false;
        m_up = m_down = m_copy = m_cut = m_paste = false;
        m_text[0] = 0;
        m_wheel = 0.0f;
        m_leftTap = m_rightTap = false;
        m_back = m_del = m_left = m_right = m_home = m_end = m_enter = m_escape = m_selectAll = false;
        return in;
    }

    /// The OkayUI key a SDL keycode stands for (KeyId_None for the keys OkayUI does not name).
    static int KeyIdOf(SDL_Keycode k) {
        using namespace OkayUI;
        if (k >= SDLK_a && k <= SDLK_z) return KeyId_A + (int)(k - SDLK_a);
        if (k >= SDLK_0 && k <= SDLK_9) return KeyId_0 + (int)(k - SDLK_0);
        if (k >= SDLK_F1 && k <= SDLK_F12) return KeyId_F1 + (int)(k - SDLK_F1);
        switch (k) {
        case SDLK_TAB: return KeyId_Tab;       case SDLK_LEFT: return KeyId_Left;     case SDLK_RIGHT: return KeyId_Right;
        case SDLK_UP: return KeyId_Up;         case SDLK_DOWN: return KeyId_Down;     case SDLK_PAGEUP: return KeyId_PageUp;
        case SDLK_PAGEDOWN: return KeyId_PageDown; case SDLK_HOME: return KeyId_Home; case SDLK_END: return KeyId_End;
        case SDLK_INSERT: return KeyId_Insert; case SDLK_DELETE: return KeyId_Delete; case SDLK_BACKSPACE: return KeyId_Backspace;
        case SDLK_SPACE: return KeyId_Space;   case SDLK_RETURN: case SDLK_KP_ENTER: return KeyId_Enter;
        case SDLK_ESCAPE: return KeyId_Escape;
        case SDLK_LCTRL: case SDLK_RCTRL: return KeyId_Ctrl;
        case SDLK_LSHIFT: case SDLK_RSHIFT: return KeyId_Shift;
        case SDLK_LALT: case SDLK_RALT: return KeyId_Alt;
        default: return KeyId_None;
        }
    }

    /// The OkayUI key a platform key stands for. `layoutChar` is the character the key types on the current layout (the letters and
    /// digits follow the layout, as SDL's key symbols do); everything else goes by the physical key.
    static int KeyIdOf(okay::platform::Key k, char layoutChar) {
        using namespace OkayUI;
        using K = okay::platform::Key;
        if (layoutChar >= 'a' && layoutChar <= 'z') return KeyId_A + (layoutChar - 'a');
        if (layoutChar >= '0' && layoutChar <= '9') return KeyId_0 + (layoutChar - '0');
        if (layoutChar == ' ') return KeyId_Space;
        const int v = (int)k;
        if (v >= (int)K::F1 && v <= (int)K::F12) return KeyId_F1 + (v - (int)K::F1);
        switch (k) {
        case K::Tab: return KeyId_Tab;       case K::Left: return KeyId_Left;     case K::Right: return KeyId_Right;
        case K::Up: return KeyId_Up;         case K::Down: return KeyId_Down;     case K::PageUp: return KeyId_PageUp;
        case K::PageDown: return KeyId_PageDown; case K::Home: return KeyId_Home; case K::End: return KeyId_End;
        case K::Insert: return KeyId_Insert; case K::Delete: return KeyId_Delete; case K::Backspace: return KeyId_Backspace;
        case K::Space: return KeyId_Space;   case K::Return: case K::KpEnter: return KeyId_Enter;
        case K::Escape: return KeyId_Escape;
        case K::LCtrl: case K::RCtrl: return KeyId_Ctrl;
        case K::LShift: case K::RShift: return KeyId_Shift;
        case K::LAlt: case K::RAlt: return KeyId_Alt;
        default: return KeyId_None;
        }
    }

private:
    /// The character a key types on a US layout (the fallback when the caller gives none).
    static char UsCharOf(okay::platform::Key k) {
        const int v = (int)k;
        if (v >= (int)okay::platform::Key::A && v <= (int)okay::platform::Key::Z) return (char)('a' + (v - (int)okay::platform::Key::A));
        if (v >= (int)okay::platform::Key::N1 && v <= (int)okay::platform::Key::N9) return (char)('1' + (v - (int)okay::platform::Key::N1));
        if (k == okay::platform::Key::N0) return '0';
        return 0;
    }
    bool  m_up = false, m_down = false, m_copy = false, m_cut = false, m_paste = false, m_ctrl = false;
    bool  m_keyHeld[OkayUI::KeyId_COUNT] = {}, m_keyTap[OkayUI::KeyId_COUNT] = {};
    char  m_text[64] = {0};
    char  m_out[64] = {0};
    float m_wheel = 0.0f;
    bool  m_leftTap = false, m_rightTap = false;   // a button went down since the last frame (even if it is already up again)
    bool  m_back = false, m_del = false, m_left = false, m_right = false, m_home = false, m_end = false;
    bool  m_enter = false, m_escape = false, m_selectAll = false, m_shift = false;
};

/// The rectangle a hosted screen occupies inside an ImGui window, remembered from one frame to the next (the OkayUI frame
/// starts before the panels are submitted, so the mouse decision uses last frame's rectangle: one frame of latency).
class Region {
public:
    /// Called by the hosted panel each frame it draws: `rect` is its screen rectangle, `hovered` ImGui's answer to "is the
    /// mouse over my placeholder item and not covered by another window or popup" (ImGui::IsItemHovered).
    void Offer(float x, float y, float w, float h, bool hovered) {
        m_x = x; m_y = y; m_w = w; m_h = h; m_hovered = hovered; m_age = 0; m_valid = true;
    }
    /// Start of the next frame: a region that was not offered last frame (panel closed, another page showing) is dropped.
    void NextFrame() { if (m_valid && ++m_age > 1) m_valid = false; }
    /// True when the mouse (x, y) is over a region whose panel was visible and not covered last frame.
    bool Owns(float x, float y) const {
        return m_valid && m_hovered && x >= m_x && y >= m_y && x < m_x + m_w && y < m_y + m_h;
    }
    bool Valid() const { return m_valid; }
private:
    float m_x = 0, m_y = 0, m_w = 0, m_h = 0;
    bool  m_hovered = false, m_valid = false;
    int   m_age = 0;
};

/// Whether ImGui should be told to leave the keyboard to OkayUI (call after the hosted screens ran this frame).
inline bool WantsKeyboard() { return OkayUI::WantsTextInput(); }

} // namespace okayuihost

// ---- The host half without SDL and without Dear ImGui's backends: okay::platform events in, okay::platform::Painter out ------------------
// Define OKAYUI_HOST_PLATFORM before including this header to get it (it needs okay/Platform and okayui/soft on the include path, the
// okay_ui_soft library). This is what replaces, for a window that runs OkayUI alone, the Dear ImGui backend calls of the editor and launcher
// (listed in docs/own_ui_toolkit.md 6.18):
//   ImGui_ImplSDL2_ProcessEvent(&e)      ->  host.Feed(event)                    events from okay::platform::Backend::PollEvent
//   ImGui_ImplSDL2_NewFrame()            ->  host.BeginFrame()                   window size from the painter, input of the frame, OkayUI::BeginFrame
//   ImGui_ImplSDLRenderer2_NewFrame()    ->  (nothing: the glyph atlas is uploaded by the FrameDrawer when it changes)
//   ImGui::Render() + RenderDrawData()   ->  host.EndFrame()                     OkayUI::EndFrameData, clear, FrameDrawer::Draw, Present
//   io.AddKeyEvent / AddInputCharacter   ->  Feed(KeyDown / TextInput events)
//   io.WantCaptureKeyboard               ->  host.WantsTextInput()               start / stop text input on the window from it
#ifdef OKAYUI_HOST_PLATFORM
#include "okay/Platform/Platform.hpp"
#include "okay/Platform/Painter.hpp"
#include "OkayUI_SoftPainter.hpp"
#include <string>

namespace okayuihost {
namespace platform {

/// The OkayUI key a platform key stands for (KeyId_None for keys OkayUI does not name).
inline int KeyIdOf(okay::platform::Key k) {
    using okay::platform::Key; using namespace OkayUI;
    const int v = (int)k;
    if (v >= (int)Key::A && v <= (int)Key::Z) return KeyId_A + (v - (int)Key::A);
    if (v >= (int)Key::N1 && v <= (int)Key::N9) return KeyId_1 + (v - (int)Key::N1);
    if (k == Key::N0) return KeyId_0;
    if (v >= (int)Key::F1 && v <= (int)Key::F12) return KeyId_F1 + (v - (int)Key::F1);
    switch (k) {
    case Key::Tab: return KeyId_Tab;     case Key::Left: return KeyId_Left;   case Key::Right: return KeyId_Right;
    case Key::Up: return KeyId_Up;       case Key::Down: return KeyId_Down;   case Key::PageUp: return KeyId_PageUp;
    case Key::PageDown: return KeyId_PageDown; case Key::Home: return KeyId_Home; case Key::End: return KeyId_End;
    case Key::Insert: return KeyId_Insert; case Key::Delete: return KeyId_Delete; case Key::Backspace: return KeyId_Backspace;
    case Key::Space: return KeyId_Space; case Key::Return: case Key::KpEnter: return KeyId_Enter; case Key::Escape: return KeyId_Escape;
    case Key::LCtrl: case Key::RCtrl: return KeyId_Ctrl; case Key::LShift: case Key::RShift: return KeyId_Shift;
    case Key::LAlt: case Key::RAlt: return KeyId_Alt;
    case Key::Minus: return KeyId_Minus; case Key::Equals: return KeyId_Equal; case Key::Slash: return KeyId_Slash;
    case Key::Backslash: return KeyId_Backslash; case Key::Period: return KeyId_Period; case Key::Comma: return KeyId_Comma;
    case Key::LeftBracket: return KeyId_LeftBracket; case Key::RightBracket: return KeyId_RightBracket;
    case Key::Kp0: return KeyId_Keypad0; case Key::Kp1: return KeyId_Keypad1; case Key::Kp2: return KeyId_Keypad2; case Key::Kp3: return KeyId_Keypad3;
    case Key::Kp4: return KeyId_Keypad4; case Key::Kp5: return KeyId_Keypad5; case Key::Kp6: return KeyId_Keypad6; case Key::Kp7: return KeyId_Keypad7;
    case Key::Kp8: return KeyId_Keypad8; case Key::Kp9: return KeyId_Keypad9; case Key::KpPlus: return KeyId_KeypadAdd; case Key::KpMinus: return KeyId_KeypadSubtract;
    case Key::KpMultiply: return KeyId_KeypadMultiply; case Key::KpDivide: return KeyId_KeypadDivide; case Key::KpPeriod: return KeyId_KeypadDecimal;
    default: return KeyId_None;
    }
}

/// Collects what OkayUI needs from okay::platform events between two frames (the platform twin of InputCollector).
class EventCollector {
public:
    void Feed(const okay::platform::Event& e) {
        using okay::platform::EventType; using okay::platform::Key;
        switch (e.type) {
        case EventType::MouseMove: m_x = (float)e.x; m_y = (float)e.y; m_havePos = true; break;
        case EventType::MouseButtonDown:
            m_x = (float)e.x; m_y = (float)e.y; m_havePos = true;
            if (e.button == okay::platform::MouseLeft) { m_left = true; m_leftTap = true; } else if (e.button == okay::platform::MouseRight) { m_right = true; m_rightTap = true; }
            break;
        case EventType::MouseButtonUp:
            m_x = (float)e.x; m_y = (float)e.y; m_havePos = true;
            if (e.button == okay::platform::MouseLeft) m_left = false; else if (e.button == okay::platform::MouseRight) m_right = false;
            break;
        case EventType::MouseWheel: m_wheel += e.wheelFlipped ? -e.wheelY : e.wheelY; break;   // up = +y, as OkayUI wants
        case EventType::TextInput: {
            if (m_text.size() + e.text.size() < sizeof(m_textBuf)) m_text += e.text;
            break;
        }
        case EventType::TextEditing: m_composition = e.text; m_compCaret = e.x; m_composing = !e.text.empty(); break;
        case EventType::KeyDown: case EventType::KeyUp: {
            const bool down = e.type == EventType::KeyDown;
            m_shift = (e.mods & okay::platform::ModShift) != 0;
            m_ctrl = (e.mods & (okay::platform::ModCtrl | okay::platform::ModGui)) != 0;
            const int id = KeyIdOf(e.key);
            if (id != OkayUI::KeyId_None) { m_keyHeld[id] = down; if (down) m_keyTap[id] = true; }
            if (!down) break;
            switch (e.key) {
            case Key::Backspace: m_back = true; break;   case Key::Delete: m_del = true; break;
            case Key::Left: m_l = true; break;           case Key::Right: m_r = true; break;
            case Key::Home: m_home = true; break;        case Key::End: m_end = true; break;
            case Key::Return: case Key::KpEnter: m_enter = true; break;
            case Key::Escape: m_escape = true; break;    case Key::Up: m_up = true; break;   case Key::Down: m_down = true; break;
            case Key::A: if (m_ctrl) m_selectAll = true; break;
            case Key::C: if (m_ctrl) m_copy = true; break;
            case Key::X: if (m_ctrl) m_cut = true; break;
            case Key::V: if (m_ctrl) m_paste = true; break;
            default: break;
            }
            break;
        }
        case EventType::WindowResized: m_resized = true; break;
        case EventType::Quit: case EventType::WindowClose: m_quit = true; break;
        default: break;
        }
    }
    /// The window was asked to close (a Quit or WindowClose event came): the host loop leaves.
    bool QuitRequested() const { return m_quit; }
    /// A resize event came since the last frame (the painter already follows the window; this is for hosts that re-layout).
    bool TakeResized() { const bool r = m_resized; m_resized = false; return r; }
    /// The input of the frame about to start; the one-shot state is cleared. `displayW/H` are the painter's output size.
    OkayUI::Input Build(float displayW, float displayH, bool blocked = false, bool deliverKeys = true) {
        OkayUI::Input in;
        in.mouseX = m_x; in.mouseY = m_y; in.displayW = displayW; in.displayH = displayH;
        in.mouseDown = m_left || m_leftTap; in.rightDown = m_right || m_rightTap; in.blocked = blocked; in.wheel = m_wheel;
        if (deliverKeys) {
            std::memset(m_textBuf, 0, sizeof m_textBuf);
            std::memcpy(m_textBuf, m_text.c_str(), m_text.size() < sizeof(m_textBuf) - 1 ? m_text.size() : sizeof(m_textBuf) - 1);
            in.text = m_textBuf[0] ? m_textBuf : nullptr;
            in.backspace = m_back; in.del = m_del; in.left = m_l; in.right = m_r; in.home = m_home; in.end = m_end; in.enter = m_enter; in.escape = m_escape;
            in.selectAll = m_selectAll; in.shift = m_shift; in.ctrl = m_ctrl; in.up = m_up; in.down = m_down; in.copy = m_copy; in.cut = m_cut; in.paste = m_paste;
            for (int k = 0; k < OkayUI::KeyId_COUNT; ++k) in.keyDown[k] = m_keyHeld[k] || m_keyTap[k];
            if (m_composing) { in.composition = m_composition.c_str(); in.compositionCaret = m_compCaret; }
        }
        for (int k = 0; k < OkayUI::KeyId_COUNT; ++k) m_keyTap[k] = false;
        m_text.clear(); m_wheel = 0.0f; m_leftTap = m_rightTap = false;
        m_back = m_del = m_l = m_r = m_home = m_end = m_enter = m_escape = m_selectAll = m_up = m_down = m_copy = m_cut = m_paste = false;
        return in;
    }
private:
    float m_x = 0, m_y = 0, m_wheel = 0; bool m_havePos = false, m_left = false, m_right = false, m_leftTap = false, m_rightTap = false;
    bool m_shift = false, m_ctrl = false, m_back = false, m_del = false, m_l = false, m_r = false, m_home = false, m_end = false, m_enter = false, m_escape = false;
    bool m_selectAll = false, m_up = false, m_down = false, m_copy = false, m_cut = false, m_paste = false, m_resized = false, m_quit = false, m_composing = false;
    bool m_keyHeld[OkayUI::KeyId_COUNT] = {}, m_keyTap[OkayUI::KeyId_COUNT] = {};
    std::string m_text, m_composition; char m_textBuf[256] = {0}; int m_compCaret = 0;
};

/// One window driven by OkayUI alone. Per frame:   while (backend->PollEvent(e)) host.Feed(e);   host.BeginFrame();   ...OkayUI calls...   host.EndFrame();
class Host {
public:
    explicit Host(okay::platform::Painter& painter) : m_painter(painter), m_drawer(painter) {}
    void Feed(const okay::platform::Event& e) { m_events.Feed(e); }
    bool QuitRequested() const { return m_events.QuitRequested(); }
    /// Starts the frame: the display size is the painter's output size. `blocked` (pointer owned by something else) and `deliverKeys` as in Build.
    /// `time` is seconds since the start (Input::time; 0 = OkayUI derives it from the frame count).
    void BeginFrame(bool blocked = false, bool deliverKeys = true, double time = 0.0) {
        int w = 0, h = 0; m_painter.OutputSize(&w, &h);
        m_w = w; m_h = h;
        OkayUI::Input in = m_events.Build((float)w, (float)h, blocked, deliverKeys);
        in.time = time;
        OkayUI::BeginFrame(in);
    }
    /// A resize event came since the last frame (see EventCollector::TakeResized).
    bool TakeResized() { return m_events.TakeResized(); }
    /// Ends the frame: finishes the draw data, clears to `clear`, draws the frame through the painter and presents it.
    void EndFrame(unsigned char r = 24, unsigned char g = 26, unsigned char b = 32) {
        OkayUI::EndFrameData();
        m_painter.SetDrawColor(r, g, b, 255); m_painter.Clear();
        m_drawer.Draw(OkayUI::GetDrawData());
        m_painter.Present();
        ++m_frames;
    }
    /// True while a text field has the keyboard: the loop calls Backend::StartTextInput / StopTextInput from it (and SetTextInputRect from OkayUI::GetImeRect()).
    bool WantsTextInput() const { return OkayUI::WantsTextInput(); }
    int DisplayWidth() const { return m_w; }
    int DisplayHeight() const { return m_h; }
    int Frames() const { return m_frames; }
private:
    okay::platform::Painter& m_painter;
    OkayUI::soft::FrameDrawer m_drawer;
    EventCollector m_events;
    int m_w = 0, m_h = 0, m_frames = 0;
};

} // namespace platform
} // namespace okayuihost
#endif // OKAYUI_HOST_PLATFORM
