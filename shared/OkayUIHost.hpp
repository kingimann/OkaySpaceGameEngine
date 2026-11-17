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

// SDL.h on Windows renames every `main` to SDL_main (which breaks members such as Camera::main in the engine headers):
// the apps run their own entry point, so keep that rename off wherever this header is the first to include SDL.
// A build with -DOKAY_WITH_SDL=OFF has no SDL: the SDL feed (Feed, KeyIdOf) is left out and hosts use the plain Add* calls
// (shared/OkayUIHostPlatform.hpp).
#ifdef OKAY_PLATFORM_HAVE_SDL2
#ifndef SDL_MAIN_HANDLED
#  define SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#endif

#include <cstring>

namespace okayuihost {

/// Collects what OkayUI needs from SDL events between two frames.
class InputCollector {
public:
#ifdef OKAY_PLATFORM_HAVE_SDL2
    /// Feed every SDL event (the host may also pass it to ImGui; the collector never consumes events).
    void Feed(const SDL_Event& e) {
        switch (e.type) {
        case SDL_TEXTINPUT: AddText(e.text.text); break;
        case SDL_MOUSEBUTTONDOWN:   // a press and release that both arrive before one frame must still show as one pressed frame
            if (e.button.button == SDL_BUTTON_LEFT) AddButtonDown(0);
            else if (e.button.button == SDL_BUTTON_RIGHT) AddButtonDown(1);
            break;
        case SDL_MOUSEWHEEL: {
            float y = (float)e.wheel.y;
            if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) y = -y;
            AddWheel(y);
            break;
        }
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            AddKey(KeyIdOf(e.key.keysym.sym), e.type == SDL_KEYDOWN, (e.key.keysym.mod & KMOD_SHIFT) != 0,
                   (e.key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0);
            break;
        default: break;
        }
    }
#endif // OKAY_PLATFORM_HAVE_SDL2

    // ---- The same input without SDL (an okay::platform host feeds these: OkayUIHostPlatform.hpp) ----
    /// Typed text (UTF-8). A full buffer drops the rest.
    void AddText(const char* utf8) {
        if (!utf8) return;
        const std::size_t have = std::strlen(m_text);
        const std::size_t add = std::strlen(utf8);
        if (have + add < sizeof(m_text)) std::memcpy(m_text + have, utf8, add + 1);
    }
    /// A mouse button went down: 0 = left, 1 = right (others are not OkayUI's).
    void AddButtonDown(int button) { if (button == 0) m_leftTap = true; else if (button == 1) m_rightTap = true; }
    /// Wheel notches, up = +y.
    void AddWheel(float y) { m_wheel += y; }
    /// A key went down (each key repeat is another down) or up. `id` is an OkayUI KeyId (KeyId_None for keys OkayUI does not
    /// name: only the modifiers are taken then); `ctrl` is Ctrl or the Command / Windows key.
    void AddKey(int id, bool down, bool shift, bool ctrl) {
        m_shift = shift;
        m_ctrl = ctrl;
        if (id != OkayUI::KeyId_None && id >= 0 && id < OkayUI::KeyId_COUNT) {   // held state; a press and release between two frames still shows for one frame
            m_keyHeld[id] = down;
            if (down) m_keyTap[id] = true;
        }
        if (!down) return;
        switch (id) {
        case OkayUI::KeyId_Backspace: m_back = true; break;
        case OkayUI::KeyId_Delete:    m_del = true; break;
        case OkayUI::KeyId_Left:      m_left = true; break;
        case OkayUI::KeyId_Right:     m_right = true; break;
        case OkayUI::KeyId_Home:      m_home = true; break;
        case OkayUI::KeyId_End:       m_end = true; break;
        case OkayUI::KeyId_Enter:     m_enter = true; break;
        case OkayUI::KeyId_Escape:    m_escape = true; break;
        case OkayUI::KeyId_Up:        m_up = true; break;
        case OkayUI::KeyId_Down:      m_down = true; break;
        case OkayUI::KeyId_A:         if (ctrl) m_selectAll = true; break;
        case OkayUI::KeyId_C:         if (ctrl) m_copy = true; break;
        case OkayUI::KeyId_X:         if (ctrl) m_cut = true; break;
        case OkayUI::KeyId_V:         if (ctrl) m_paste = true; break;
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

#ifdef OKAY_PLATFORM_HAVE_SDL2
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
        case SDLK_SLASH: return KeyId_Slash;
        default: return KeyId_None;
        }
    }
#endif // OKAY_PLATFORM_HAVE_SDL2

private:
    bool  m_up = false, m_down = false, m_copy = false, m_cut = false, m_paste = false, m_ctrl = false;
    bool  m_keyHeld[OkayUI::KeyId_COUNT] = {}, m_keyTap[OkayUI::KeyId_COUNT] = {};
    char  m_text[64] = {0};
    char  m_out[64] = {0};
    float m_wheel = 0.0f;
    bool  m_leftTap = false, m_rightTap = false;   // a button went down since the last frame (even if it is already up again)
    bool  m_back = false, m_del = false, m_left = false, m_right = false, m_home = false, m_end = false;
    bool  m_enter = false, m_escape = false, m_selectAll = false, m_shift = false;
};

/// The rectangles hosted screens occupy inside ImGui windows, remembered from one frame to the next (the OkayUI frame starts
/// before the panels are submitted, so the decision at the frame's start uses last frame's rectangles; OwnsThisFrame lets the
/// host correct it as each screen is offered, so there is no frame of latency). Several screens
/// can be hosted in one frame (a page and the notifications panel floating over it): each Offer adds one rectangle, up to
/// kMax; the set offered in a frame replaces the set of the frame before.
class Region {
public:
    static constexpr int kMax = 8;
    /// Called by a hosted panel each frame it draws: `rect` is its screen rectangle, `hovered` ImGui's answer to "is the
    /// mouse over my placeholder item and not covered by another window or popup" (ImGui::IsItemHovered).
    /// `overlay`: a popup, menu list or dropdown OkayUI draws over everything (the host keeps a press there from ImGui too).
    void Offer(float x, float y, float w, float h, bool hovered, bool overlay = false) {
        int pick = -1;
        for (int i = 0; i < kMax && pick < 0; ++i)   // the same rectangle offered again this frame: the newest answer wins
            if (m_r[i].valid && m_r[i].age == 0 && m_r[i].x == x && m_r[i].y == y && m_r[i].w == w && m_r[i].h == h) pick = i;
        for (int i = 0; i < kMax && pick < 0; ++i) if (!m_r[i].valid || m_r[i].age > 0) pick = i;   // a slot not offered this frame yet
        if (pick < 0) return;                                                                      // more than kMax in one frame: ignored
        m_r[pick] = Rect{x, y, w, h, hovered, true, 0, overlay};
    }
    /// Start of the next frame: a rectangle that was not offered last frame (panel closed, another page showing) is dropped.
    void NextFrame() { for (Rect& r : m_r) if (r.valid && ++r.age > 1) r.valid = false; }
    /// True when the mouse (x, y) is over a rectangle whose panel was visible and not covered last frame.
    bool Owns(float x, float y) const {
        for (const Rect& r : m_r)
            if (r.valid && r.hovered && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h) return true;
        return false;
    }
    /// The same, using only the rectangles offered so far THIS frame (with ImGui's answer of this frame). A host calls it right
    /// after Offer and, when it is true, unblocks OkayUI's mouse for the rest of the frame (OkayUI::SetInputBlocked(false)):
    /// then a click that arrives in the same frame as the move onto the screen goes to the screen, not to ImGui.
    bool OwnsThisFrame(float x, float y) const {
        for (const Rect& r : m_r)
            if (r.valid && r.age == 0 && r.hovered && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h) return true;
        return false;
    }
    /// True when the mouse is over an overlay offered last frame: a press or the wheel there is OkayUI's alone.
    bool OwnsOverlay(float x, float y) const {
        for (const Rect& r : m_r)
            if (r.valid && r.overlay && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h) return true;
        return false;
    }
    bool Valid() const { return Count() > 0; }
    /// How many rectangles are remembered.
    int Count() const { int n = 0; for (const Rect& r : m_r) n += r.valid ? 1 : 0; return n; }
private:
    struct Rect { float x = 0, y = 0, w = 0, h = 0; bool hovered = false, valid = false; int age = 0; bool overlay = false; };
    Rect m_r[kMax];
};

/// Whether ImGui should be told to leave the keyboard to OkayUI (call after the hosted screens ran this frame).
inline bool WantsKeyboard() { return OkayUI::WantsTextInput(); }

} // namespace okayuihost
