#pragma once
// The platform feed: turns okay::platform events into the UI's input (OkayUI's IO, which takes Dear ImGui's io.Add*Event
// calls), and does the per-frame work (display size, delta time, mouse cursor, clipboard, the input method's caret).
// The editor and the launcher use it; it is their only input path (docs/platform/platform_layer.md, "ImGui platform feed").
//
// Written from Dear ImGui's public header (the io.Add*Event functions, ImGuiKey, ImGuiPlatformIO) and from the
// okay::platform seam, in our own words. It does not use or follow imgui_impl_sdl2 or any SDL code.
//
// Two layers so most of it is testable with no window:
//   * Translate(): a platform Event -> a list of Actions (pure; needs only the ImGuiKey enum values).
//   * Feed: applies Actions to an ImGuiIO and runs NewFrame() against a small FeedHost, which hides the window and
//     the backend (FeedHost::FromPlatform() adapts a real okay::platform window; tests pass a fake).
//
// What it does not do (on purpose): gamepads (nothing reads pads here), more than one OS window (OkayUI draws into one),
// touch (the seam fires it for sdl2 only). Header rules: no <mutex>/<thread> (MinGW win32 threads).
#include "imgui.h"
#include "okay/Platform/Platform.hpp"

#include <cfloat>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace okay::imguifeed {

/// A seam key to an ImGuiKey (ImGuiKey_None for a key Dear ImGui has no name for). The seam numbers keys with USB HID
/// usage ids; the numbers not in the seam's enum (F13..F24 = 104..115, keypad = = 103) are matched by value.
inline ImGuiKey KeyToImGui(okay::platform::Key k) {
    using K = okay::platform::Key;
    const int v = (int)k;
    if (v >= (int)K::A && v <= (int)K::Z) return (ImGuiKey)(ImGuiKey_A + (v - (int)K::A));
    if (v >= (int)K::N1 && v <= (int)K::N9) return (ImGuiKey)(ImGuiKey_1 + (v - (int)K::N1));
    if (v >= (int)K::F1 && v <= (int)K::F12) return (ImGuiKey)(ImGuiKey_F1 + (v - (int)K::F1));
    if (v >= 104 && v <= 115) return (ImGuiKey)(ImGuiKey_F13 + (v - 104));
    if (v >= (int)K::Kp1 && v <= (int)K::Kp9) return (ImGuiKey)(ImGuiKey_Keypad1 + (v - (int)K::Kp1));
    switch (k) {
    case K::N0: return ImGuiKey_0;
    case K::Return: return ImGuiKey_Enter;
    case K::Escape: return ImGuiKey_Escape;
    case K::Backspace: return ImGuiKey_Backspace;
    case K::Tab: return ImGuiKey_Tab;
    case K::Space: return ImGuiKey_Space;
    case K::Minus: return ImGuiKey_Minus;
    case K::Equals: return ImGuiKey_Equal;
    case K::LeftBracket: return ImGuiKey_LeftBracket;
    case K::RightBracket: return ImGuiKey_RightBracket;
    case K::Backslash: return ImGuiKey_Backslash;
    case K::Semicolon: return ImGuiKey_Semicolon;
    case K::Apostrophe: return ImGuiKey_Apostrophe;
    case K::Grave: return ImGuiKey_GraveAccent;
    case K::Comma: return ImGuiKey_Comma;
    case K::Period: return ImGuiKey_Period;
    case K::Slash: return ImGuiKey_Slash;
    case K::CapsLock: return ImGuiKey_CapsLock;
    case K::PrintScreen: return ImGuiKey_PrintScreen;
    case K::ScrollLock: return ImGuiKey_ScrollLock;
    case K::Pause: return ImGuiKey_Pause;
    case K::Insert: return ImGuiKey_Insert;
    case K::Home: return ImGuiKey_Home;
    case K::PageUp: return ImGuiKey_PageUp;
    case K::Delete: return ImGuiKey_Delete;
    case K::End: return ImGuiKey_End;
    case K::PageDown: return ImGuiKey_PageDown;
    case K::Right: return ImGuiKey_RightArrow;
    case K::Left: return ImGuiKey_LeftArrow;
    case K::Down: return ImGuiKey_DownArrow;
    case K::Up: return ImGuiKey_UpArrow;
    case K::NumLock: return ImGuiKey_NumLock;
    case K::KpDivide: return ImGuiKey_KeypadDivide;
    case K::KpMultiply: return ImGuiKey_KeypadMultiply;
    case K::KpMinus: return ImGuiKey_KeypadSubtract;
    case K::KpPlus: return ImGuiKey_KeypadAdd;
    case K::KpEnter: return ImGuiKey_KeypadEnter;
    case K::Kp0: return ImGuiKey_Keypad0;
    case K::KpPeriod: return ImGuiKey_KeypadDecimal;
    case K::NonUsBackslash: return ImGuiKey_Oem102;
    case K::Application: return ImGuiKey_Menu;
    case K::LCtrl: return ImGuiKey_LeftCtrl;
    case K::LShift: return ImGuiKey_LeftShift;
    case K::LAlt: return ImGuiKey_LeftAlt;
    case K::LGui: return ImGuiKey_LeftSuper;
    case K::RCtrl: return ImGuiKey_RightCtrl;
    case K::RShift: return ImGuiKey_RightShift;
    case K::RAlt: return ImGuiKey_RightAlt;
    case K::RGui: return ImGuiKey_RightSuper;
    default: break;
    }
    if (v == 103) return ImGuiKey_KeypadEqual;
    return ImGuiKey_None;
}

/// Dear ImGui wants the modifier state as its own key events (ImGuiMod_Ctrl ...) besides the left / right key events.
/// Returns the bit (0..7) of a physical modifier key in the feed's held mask, or -1 for any other key.
inline int ModBitOfKey(ImGuiKey k) {
    switch (k) {
    case ImGuiKey_LeftCtrl: return 0;  case ImGuiKey_RightCtrl: return 1;
    case ImGuiKey_LeftShift: return 2; case ImGuiKey_RightShift: return 3;
    case ImGuiKey_LeftAlt: return 4;   case ImGuiKey_RightAlt: return 5;
    case ImGuiKey_LeftSuper: return 6; case ImGuiKey_RightSuper: return 7;
    default: return -1;
    }
}
/// The ImGuiMod_ key for a pair of bits (bit / 2).
inline ImGuiKey ModKeyOfPair(int pair) {
    static const ImGuiKey k[4] = { OkayUI::KeyId_ModCtrl, OkayUI::KeyId_ModShift, OkayUI::KeyId_ModAlt, OkayUI::KeyId_ModSuper };
    return k[pair & 3];
}

/// A seam mouse button (MouseLeft = 1 ...) to Dear ImGui's button number (0 left, 1 right, 2 middle, 3 and 4 the side
/// buttons); -1 for a number that is not a button.
inline int MouseButtonToImGui(int b) {
    switch (b) {
    case okay::platform::MouseLeft: return 0;
    case okay::platform::MouseRight: return 1;
    case okay::platform::MouseMiddle: return 2;
    case okay::platform::MouseX1: return 3;
    case okay::platform::MouseX2: return 4;
    default: return -1;
    }
}

/// A seam cursor shape for an ImGuiMouseCursor (ImGuiMouseCursor_None has none: the caller hides the pointer).
inline bool CursorToSeam(ImGuiMouseCursor c, okay::platform::CursorShape& out) {
    using S = okay::platform::CursorShape;
    switch (c) {
    case ImGuiMouseCursor_Arrow: out = S::Arrow; return true;
    case ImGuiMouseCursor_TextInput: out = S::TextInput; return true;
    case ImGuiMouseCursor_ResizeAll: out = S::ResizeAll; return true;
    case ImGuiMouseCursor_ResizeNS: out = S::ResizeNS; return true;
    case ImGuiMouseCursor_ResizeEW: out = S::ResizeEW; return true;
    case ImGuiMouseCursor_ResizeNESW: out = S::ResizeNESW; return true;
    case ImGuiMouseCursor_ResizeNWSE: out = S::ResizeNWSE; return true;
    case ImGuiMouseCursor_Hand: out = S::Hand; return true;
    case ImGuiMouseCursor_Wait: out = S::Wait; return true;
    case ImGuiMouseCursor_Progress: out = S::Progress; return true;
    case ImGuiMouseCursor_NotAllowed: out = S::NotAllowed; return true;
    default: return false;
    }
}

/// One thing to tell Dear ImGui's IO.
struct Action {
    enum Kind { Key, MousePos, MouseButton, Wheel, Text, Focus } kind = Key;
    int key = 0;          // Key: an ImGuiKey
    int button = 0;       // MouseButton: Dear ImGui's number
    bool down = false;    // Key, MouseButton, Focus (true = focused)
    float x = 0, y = 0;   // MousePos (window pixels), Wheel (x, y; up = +y)
    std::string text;     // Text: UTF-8
};

/// Pure: what one platform event means for Dear ImGui. `windowId` filters: an event for another window gives nothing
/// (0 = accept events of every window). Events that carry no input for ImGui (resize, expose, close, drop, gamepad,
/// touch, IME composition, key repeats) give no Actions; Dear ImGui repeats keys itself.
inline void Translate(const okay::platform::Event& e, uint32_t windowId, std::vector<Action>& out) {
    using T = okay::platform::EventType;
    const bool windowed = e.type == T::KeyDown || e.type == T::KeyUp || e.type == T::TextInput || e.type == T::MouseMove ||
        e.type == T::MouseButtonDown || e.type == T::MouseButtonUp || e.type == T::MouseWheel ||
        e.type == T::WindowFocusGained || e.type == T::WindowFocusLost;
    if (windowed && windowId != 0 && e.windowId != windowId) return;
    Action a;
    switch (e.type) {
    case T::KeyDown: case T::KeyUp: {
        if (e.repeat) return;
        const ImGuiKey k = KeyToImGui(e.key);
        if (k == ImGuiKey_None) return;
        a.kind = Action::Key; a.key = (int)k; a.down = e.type == T::KeyDown; out.push_back(a); return;
    }
    case T::TextInput:
        if (e.text.empty()) return;
        a.kind = Action::Text; a.text = e.text; out.push_back(a); return;
    case T::MouseMove:
        a.kind = Action::MousePos; a.x = (float)e.x; a.y = (float)e.y; out.push_back(a); return;
    case T::MouseButtonDown: case T::MouseButtonUp: {
        const int b = MouseButtonToImGui(e.button);
        if (b < 0) return;
        // The position goes first so a click lands where the pointer is even when no motion event came before it.
        a.kind = Action::MousePos; a.x = (float)e.x; a.y = (float)e.y; out.push_back(a);
        Action c; c.kind = Action::MouseButton; c.button = b; c.down = e.type == T::MouseButtonDown; out.push_back(c); return;
    }
    case T::MouseWheel:
        a.kind = Action::Wheel; a.x = e.wheelX; a.y = e.wheelY; out.push_back(a); return;
    case T::WindowFocusGained: case T::WindowFocusLost:
        a.kind = Action::Focus; a.down = e.type == T::WindowFocusGained; out.push_back(a); return;
    default: return;
    }
}

/// Apply Actions to an ImGuiIO.
inline void Apply(const std::vector<Action>& actions, ImGuiIO& io) {
    for (const Action& a : actions) {
        switch (a.kind) {
        case Action::Key: io.AddKeyEvent((ImGuiKey)a.key, a.down); break;
        case Action::MousePos: io.AddMousePosEvent(a.x, a.y); break;
        case Action::MouseButton: io.AddMouseButtonEvent(a.button, a.down); break;
        case Action::Wheel: io.AddMouseWheelEvent(a.x, a.y); break;
        case Action::Text: io.AddInputCharactersUTF8(a.text.c_str()); break;
        case Action::Focus: io.AddFocusEvent(a.down); break;
        }
    }
}

/// What the feed needs from a window and the backend. Empty members are skipped (a missing one is "not supported").
struct FeedHost {
    std::function<void(int&, int&)> windowSize;          // client size in window units
    std::function<void(int&, int&)> drawableSize;        // size in pixels (differs from the window size on high-DPI)
    std::function<bool()> hasMouseFocus;                 // the pointer is over the window
    std::function<bool()> isMinimized;
    std::function<double()> nowSeconds;                  // a monotonic clock
    std::function<bool(okay::platform::CursorShape)> setCursorShape;
    std::function<void(bool)> showCursor;
    std::function<std::string()> getClipboard;
    std::function<bool(const std::string&)> setClipboard;
    std::function<void(int, int)> warpMouse;             // window position
    std::function<void(int, int, int, int)> setTextInputRect;

    /// A host over a real backend and window (both stay owned by the caller and must outlive the feed).
    static FeedHost FromPlatform(okay::platform::Backend* b, okay::platform::Window* w) {
        FeedHost h;
        h.windowSize = [w](int& x, int& y) { w->GetSize(x, y); };
        h.drawableSize = [w](int& x, int& y) { w->GetDrawableSize(x, y); };
        h.hasMouseFocus = [w]() { return w->HasMouseFocus(); };
        h.isMinimized = [w]() { return w->IsMinimized(); };
        h.nowSeconds = [b]() { const uint64_t f = b->PerfFrequency(); return f ? (double)b->PerfCounter() / (double)f : 0.0; };
        h.setCursorShape = [b](okay::platform::CursorShape s) { return b->SetCursorShape(s); };
        h.showCursor = [b](bool on) { b->ShowCursor(on); };
        h.getClipboard = [b]() { return b->GetClipboardText(); };
        h.setClipboard = [b](const std::string& t) { return b->SetClipboardText(t); };
        h.warpMouse = [b, w](int x, int y) { b->WarpMouse(w, x, y); };
        h.setTextInputRect = [b](int x, int y, int ww, int hh) { b->SetTextInputRect(x, y, ww, hh); };
        return h;
    }
};

/// One Dear ImGui context's feed (the launcher has two: the main window and the chat window). Use it with that context
/// current. Not thread safe, no locks.
class Feed {
public:
    /// `windowId` = okay::platform::Window::Id() of the window this context draws into; events for other windows are ignored.
    void Init(ImGuiIO& io, uint32_t windowId, FeedHost host) {
        m_host = std::move(host);
        m_windowId = windowId;
        io.BackendPlatformName = "okay::platform feed";
        if (m_host.setCursorShape) io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
        if (m_host.warpMouse) io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;
        ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        pio.Platform_ClipboardUserData = this;
        pio.Platform_GetClipboardTextFn = [](ImGuiContext* ctx) -> const char* {
            Feed* f = (Feed*)ImGui::GetPlatformIO().Platform_ClipboardUserData; (void)ctx;
            if (!f || !f->m_host.getClipboard) return nullptr;
            f->m_clip = f->m_host.getClipboard();
            return f->m_clip.empty() ? nullptr : f->m_clip.c_str();
        };
        pio.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
            Feed* f = (Feed*)ImGui::GetPlatformIO().Platform_ClipboardUserData;
            if (f && f->m_host.setClipboard && text) f->m_host.setClipboard(text);
        };
        pio.Platform_ImeUserData = this;
        pio.Platform_SetImeDataFn = [](ImGuiContext*, ImGuiViewport*, ImGuiPlatformImeData* d) {
            Feed* f = (Feed*)ImGui::GetPlatformIO().Platform_ImeUserData;
            if (f && d && d->WantVisible && f->m_host.setTextInputRect)
                f->m_host.setTextInputRect((int)d->InputPos.x, (int)d->InputPos.y, 1, (int)d->InputLineHeight);
        };
    }
    /// Detach: the callbacks stop pointing at this object.
    void Shutdown(ImGuiIO& io) {
        ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        pio.Platform_GetClipboardTextFn = nullptr; pio.Platform_SetClipboardTextFn = nullptr; pio.Platform_ClipboardUserData = nullptr;
        pio.Platform_SetImeDataFn = nullptr; pio.Platform_ImeUserData = nullptr;
        io.BackendPlatformName = nullptr;
        io.BackendFlags &= ~(ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_HasSetMousePos);
        m_host = FeedHost();
    }

    /// Feed one event. Returns true when it made Dear ImGui input (so a caller can tell it was for this window).
    bool ProcessEvent(const okay::platform::Event& e, ImGuiIO& io) {
        if (e.type == okay::platform::EventType::WindowDpiChanged && (m_windowId == 0 || e.windowId == m_windowId)) m_dpi = (float)e.x;
        std::vector<Action> acts;
        Translate(e, m_windowId, acts);
        if (acts.empty()) return false;
        for (const Action& a : acts) {
            if (a.kind == Action::MousePos) m_pointerOut = false;
            if (a.kind == Action::Key) {
                const int bit = ModBitOfKey((ImGuiKey)a.key);
                if (bit >= 0) {
                    const unsigned before = m_mods;
                    if (a.down) m_mods |= 1u << bit; else m_mods &= ~(1u << bit);
                    const unsigned pairMask = 3u << (bit & ~1);
                    if (((before & pairMask) != 0) != ((m_mods & pairMask) != 0))
                        io.AddKeyEvent(ModKeyOfPair(bit / 2), (m_mods & pairMask) != 0);
                }
            }
            if (a.kind == Action::Focus && !a.down) { m_mods = 0; m_buttonsDown = 0; }   // ImGui clears its keys and buttons on focus loss
            if (a.kind == Action::MouseButton) { if (a.down) ++m_buttonsDown; else if (m_buttonsDown > 0) --m_buttonsDown; }
        }
        Apply(acts, io);
        return true;
    }

    /// Call once per frame before ImGui::NewFrame(): display size and scale, delta time, pointer position when it left,
    /// the mouse cursor shape.
    void NewFrame(ImGuiIO& io) {
        int w = 0, h = 0, dw = 0, dh = 0;
        if (m_host.windowSize) m_host.windowSize(w, h);
        dw = w; dh = h;
        if (m_host.drawableSize) m_host.drawableSize(dw, dh);
        if (m_host.isMinimized && m_host.isMinimized()) w = h = 0;
        io.DisplaySize = ImVec2((float)w, (float)h);
        io.DisplayFramebufferScale = (w > 0 && h > 0) ? ImVec2((float)dw / (float)w, (float)dh / (float)h) : ImVec2(1, 1);

        if (m_host.nowSeconds) {   // never zero or negative: ImGui asserts on it
            const double now = m_host.nowSeconds();
            const double dt = m_haveTime ? now - m_lastTime : 1.0 / 60.0;
            io.DeltaTime = dt > 1e-6 ? (float)dt : (float)(1.0 / 1000000.0);
            m_lastTime = now; m_haveTime = true;
        } else if (io.DeltaTime <= 0.0f) io.DeltaTime = 1.0f / 60.0f;

        // The pointer left the window (and no button holds it): tell ImGui there is no mouse, once.
        if (m_host.hasMouseFocus && !m_host.hasMouseFocus() && m_buttonsDown == 0 && !m_pointerOut) {
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); m_pointerOut = true;
        }
        if (io.WantSetMousePos && m_host.warpMouse) m_host.warpMouse((int)io.MousePos.x, (int)io.MousePos.y);

        if (!(io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)) {
            const ImGuiMouseCursor c = io.MouseDrawCursor ? ImGuiMouseCursor_None : ImGui::GetMouseCursor();
            if (c != m_lastCursor) {
                okay::platform::CursorShape s;
                if (CursorToSeam(c, s)) {
                    if (m_host.showCursor && m_lastCursor == ImGuiMouseCursor_None) m_host.showCursor(true);
                    if (m_host.setCursorShape) m_host.setCursorShape(s);
                } else if (m_host.showCursor) m_host.showCursor(false);
                m_lastCursor = c;
            }
        }
        // The input method's window (IME candidates) goes where OkayUI's focused text field drew its caret last frame. OkayUI does not
        // call Platform_SetImeDataFn: it keeps the caret in GetImeRect(), which holds last frame's value until OkayUI's next NewFrame.
        // Sent when it changes, as Dear ImGui's documented backends do with io.PlatformImeData (its InputPos and InputLineHeight).
        const OkayUI::ImeRect ime = OkayUI::GetImeRect();
        if (ime.valid && m_host.setTextInputRect) {
            const int r[4] = {(int)ime.caret.x, (int)ime.caret.y, ime.caret.w < 1.0f ? 1 : (int)ime.caret.w, ime.caret.h < 1.0f ? 1 : (int)ime.caret.h};
            if (!m_imeSent || r[0] != m_ime[0] || r[1] != m_ime[1] || r[2] != m_ime[2] || r[3] != m_ime[3]) {
                m_host.setTextInputRect(r[0], r[1], r[2], r[3]);
                for (int i = 0; i < 4; ++i) m_ime[i] = r[i];
                m_imeSent = true;
            }
        } else {
            m_imeSent = false;   // the next field to take the keyboard sends its caret again
        }
    }

    float Dpi() const { return m_dpi; }   // last WindowDpiChanged for this window; 96 until one comes (not applied to ImGui's scale)
    uint32_t WindowId() const { return m_windowId; }

private:
    FeedHost m_host;
    uint32_t m_windowId = 0;
    std::string m_clip;
    double m_lastTime = 0; bool m_haveTime = false;
    bool m_pointerOut = false;
    int m_buttonsDown = 0;
    unsigned m_mods = 0;   // held physical modifier keys, one bit each (ModBitOfKey)
    ImGuiMouseCursor m_lastCursor = -2;   // not a real cursor, so the first frame always applies one
    float m_dpi = 96.0f;
    int m_ime[4] = {0, 0, 0, 0}; bool m_imeSent = false;   // the IME caret rectangle last sent to the window (OkayUI build)
};

} // namespace okay::imguifeed
