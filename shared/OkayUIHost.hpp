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
            if (e.type != SDL_KEYDOWN) break;                 // key repeat arrives as more KEYDOWN events: each one is an edge
            const bool ctrl = (e.key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0;
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
            case SDLK_a:         if (ctrl) m_selectAll = true; break;
            default: break;
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
            in.shift = m_shift;
        }
        m_text[0] = 0;
        m_wheel = 0.0f;
        m_leftTap = m_rightTap = false;
        m_back = m_del = m_left = m_right = m_home = m_end = m_enter = m_escape = m_selectAll = false;
        return in;
    }

private:
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
