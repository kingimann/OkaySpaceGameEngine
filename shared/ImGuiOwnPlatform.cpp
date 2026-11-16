// The shared Dear ImGui platform glue over okay::platform. See ImGuiOwnPlatform.hpp.
//
// Written from imgui.h's documented contract only (moved here unchanged from launcher/ImGuiOwnBackend.cpp, where it was written):
//  * ImGuiIO input events: AddMousePosEvent, AddMouseButtonEvent, AddMouseWheelEvent (y > 0 up, x > 0 left),
//    AddKeyEvent, AddInputCharactersUTF8, AddFocusEvent; DisplaySize, DisplayFramebufferScale, DeltaTime.
//  * ImGuiPlatformIO::Platform_SetImeDataFn: called at the end of a frame with the caret position (InputPos, in ImGui's
//    coordinates, minus the viewport's Pos) and the line height while a text field wants the input method (WantVisible).
#include "ImGuiOwnPlatform.hpp"

#include "imgui.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace okay::imguiown {

namespace plat = okay::platform;

namespace {
// The cursor shape the backend was last told (one pointer, shared by every window and every glue): -2 = none yet.
int g_appliedCursor = -2;
} // namespace

// ---- platform glue ------------------------------------------------------------------------------------

int ImGuiKeyFor(plat::Key key) {
    const int k = (int)key;
    if (k >= 4 && k <= 29) return ImGuiKey_A + (k - 4);              // letters
    if (k >= 30 && k <= 38) return ImGuiKey_1 + (k - 30);            // 1..9
    if (k == 39) return ImGuiKey_0;
    if (k >= 58 && k <= 69) return ImGuiKey_F1 + (k - 58);           // F1..F12
    if (k >= 104 && k <= 115) return ImGuiKey_F13 + (k - 104);       // F13..F24
    if (k >= 89 && k <= 97) return ImGuiKey_Keypad1 + (k - 89);      // keypad 1..9
    switch (k) {
    case 98: return ImGuiKey_Keypad0;
    case 99: return ImGuiKey_KeypadDecimal;
    case 84: return ImGuiKey_KeypadDivide;
    case 85: return ImGuiKey_KeypadMultiply;
    case 86: return ImGuiKey_KeypadSubtract;
    case 87: return ImGuiKey_KeypadAdd;
    case 88: return ImGuiKey_KeypadEnter;
    case 103: return ImGuiKey_KeypadEqual;
    case 40: return ImGuiKey_Enter;
    case 41: return ImGuiKey_Escape;
    case 42: return ImGuiKey_Backspace;
    case 43: return ImGuiKey_Tab;
    case 44: return ImGuiKey_Space;
    case 45: return ImGuiKey_Minus;
    case 46: return ImGuiKey_Equal;
    case 47: return ImGuiKey_LeftBracket;
    case 48: return ImGuiKey_RightBracket;
    case 49: return ImGuiKey_Backslash;
    case 51: return ImGuiKey_Semicolon;
    case 52: return ImGuiKey_Apostrophe;
    case 53: return ImGuiKey_GraveAccent;
    case 54: return ImGuiKey_Comma;
    case 55: return ImGuiKey_Period;
    case 56: return ImGuiKey_Slash;
    case 57: return ImGuiKey_CapsLock;
    case 70: return ImGuiKey_PrintScreen;
    case 71: return ImGuiKey_ScrollLock;
    case 72: return ImGuiKey_Pause;
    case 73: return ImGuiKey_Insert;
    case 74: return ImGuiKey_Home;
    case 75: return ImGuiKey_PageUp;
    case 76: return ImGuiKey_Delete;
    case 77: return ImGuiKey_End;
    case 78: return ImGuiKey_PageDown;
    case 79: return ImGuiKey_RightArrow;
    case 80: return ImGuiKey_LeftArrow;
    case 81: return ImGuiKey_DownArrow;
    case 82: return ImGuiKey_UpArrow;
    case 83: return ImGuiKey_NumLock;
    case 100: return ImGuiKey_Oem102;
    case 101: return ImGuiKey_Menu;
    case 224: return ImGuiKey_LeftCtrl;
    case 225: return ImGuiKey_LeftShift;
    case 226: return ImGuiKey_LeftAlt;
    case 227: return ImGuiKey_LeftSuper;
    case 228: return ImGuiKey_RightCtrl;
    case 229: return ImGuiKey_RightShift;
    case 230: return ImGuiKey_RightAlt;
    case 231: return ImGuiKey_RightSuper;
    default: return ImGuiKey_None;
    }
}

ImGuiPlatformGlue::ImGuiPlatformGlue(plat::Backend& b, plat::Window& w) : m_b(b), m_w(w) {
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "okay_platform";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_GetClipboardTextFn = GetClipboard;
    pio.Platform_SetClipboardTextFn = SetClipboard;
    pio.Platform_ClipboardUserData = this;
    pio.Platform_SetImeDataFn = SetImeData;
    pio.Platform_ImeUserData = this;
    m_lastTicks = m_b.PerfCounter();
    m_b.StopTextInput();   // on only while an ImGui text field wants it (NewFrame)
}

ImGuiPlatformGlue::~ImGuiPlatformGlue() {
    if (m_textOn) m_b.StopTextInput();
    m_b.SetSystemCursor(plat::SystemCursor::Arrow);
    m_b.ShowCursor(true);
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_GetClipboardTextFn = nullptr;
    pio.Platform_SetClipboardTextFn = nullptr;
    pio.Platform_ClipboardUserData = nullptr;
    pio.Platform_SetImeDataFn = nullptr;
    pio.Platform_ImeUserData = nullptr;
    g_appliedCursor = -2;
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = nullptr;
    io.BackendFlags &= ~ImGuiBackendFlags_HasMouseCursors;
}

void ImGuiPlatformGlue::SetTextInputAlwaysOn(bool on) {
    m_textAlways = on;
    if (on && !m_textOn) { m_b.StartTextInput(); m_textOn = true; }
}

const char* ImGuiPlatformGlue::GetClipboard(ImGuiContext*) {
    ImGuiPlatformGlue* g = (ImGuiPlatformGlue*)ImGui::GetPlatformIO().Platform_ClipboardUserData;
    if (!g) return nullptr;
    g->m_clip = g->m_b.GetClipboardText();
    return g->m_clip.c_str();
}

void ImGuiPlatformGlue::SetClipboard(ImGuiContext*, const char* text) {
    ImGuiPlatformGlue* g = (ImGuiPlatformGlue*)ImGui::GetPlatformIO().Platform_ClipboardUserData;
    if (g) g->m_b.SetClipboardText(text ? text : "");
}

void ImGuiPlatformGlue::SetImeData(ImGuiContext*, ImGuiViewport* vp, ImGuiPlatformImeData* d) {
    ImGuiPlatformGlue* g = (ImGuiPlatformGlue*)ImGui::GetPlatformIO().Platform_ImeUserData;
    if (!g || !d || !d->WantVisible) return;   // no text field has the keyboard: the input method keeps where it was
    const float ox = vp ? vp->Pos.x : 0.0f, oy = vp ? vp->Pos.y : 0.0f;
    const int x = (int)std::floor(d->InputPos.x - ox), y = (int)std::floor(d->InputPos.y - oy);
    const int h = (int)std::ceil(d->InputLineHeight);
    if (g->m_imeSet && x == g->m_imeX && y == g->m_imeY && h == g->m_imeH) return;
    g->m_imeSet = true; g->m_imeX = x; g->m_imeY = y; g->m_imeH = h;
    g->m_imeTaken = g->m_w.SetTextInputArea(x, y, 1, h);
    if (g->m_log) std::fprintf(stderr, "%s: ime caret %d,%d height %d (%s)\n", g->m_logPrefix, x, y, h, g->m_imeTaken ? "taken" : "not taken");
}

bool ImGuiPlatformGlue::ProcessEvent(const plat::Event& e) {
    if (e.windowId != 0 && e.windowId != m_w.Id()) return false;
    ImGuiIO& io = ImGui::GetIO();
    switch (e.type) {
    case plat::EventType::MouseMove:
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMousePosEvent((float)e.x, (float)e.y);
        m_mouseInside = true;
        return true;
    case plat::EventType::MouseButtonDown:
    case plat::EventType::MouseButtonUp: {
        // ImGui numbers buttons 0 = left, 1 = right, 2 = middle, then the two extra ones.
        int b = -1;
        switch (e.button) {
        case plat::MouseLeft: b = 0; break;
        case plat::MouseRight: b = 1; break;
        case plat::MouseMiddle: b = 2; break;
        case plat::MouseX1: b = 3; break;
        case plat::MouseX2: b = 4; break;
        default: break;
        }
        if (b < 0) return false;
        const bool down = e.type == plat::EventType::MouseButtonDown;
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMousePosEvent((float)e.x, (float)e.y);
        io.AddMouseButtonEvent(b, down);
        if (m_log) std::fprintf(stderr, "%s: mouse %d %s at %d,%d\n", m_logPrefix, b, down ? "down" : "up", e.x, e.y);
        return true;
    }
    case plat::EventType::MouseWheel:
        // Ours: y > 0 = up, x > 0 = right. ImGui's: y > 0 = up, x > 0 = left.
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMouseWheelEvent(-e.wheelX, e.wheelY);
        return true;
    case plat::EventType::KeyDown:
    case plat::EventType::KeyUp: {
        const bool down = e.type == plat::EventType::KeyDown;
        io.AddKeyEvent(ImGuiMod_Ctrl, (e.mods & plat::ModCtrl) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (e.mods & plat::ModShift) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (e.mods & plat::ModAlt) != 0);
        io.AddKeyEvent(ImGuiMod_Super, (e.mods & plat::ModGui) != 0);
        const int k = ImGuiKeyFor(e.key);
        if (k != ImGuiKey_None) io.AddKeyEvent((ImGuiKey)k, down);
        if (m_log) std::fprintf(stderr, "%s: key %d %s\n", m_logPrefix, (int)e.key, down ? "down" : "up");
        return true;
    }
    case plat::EventType::TextInput:
        io.AddInputCharactersUTF8(e.text.c_str());
        if (m_log) std::fprintf(stderr, "%s: text \"%s\"\n", m_logPrefix, e.text.c_str());
        return true;
    case plat::EventType::WindowFocusGained: io.AddFocusEvent(true); return true;
    case plat::EventType::WindowFocusLost: io.AddFocusEvent(false); return true;
    default: return false;
    }
}

void ImGuiPlatformGlue::NewFrame() {
    ImGuiIO& io = ImGui::GetIO();
    int w = 0, h = 0, dw = 0, dh = 0;
    m_w.GetSize(w, h);
    m_w.GetDrawableSize(dw, dh);
    if (m_w.IsMinimized()) w = h = 0;
    io.DisplaySize = ImVec2((float)w, (float)h);
    if (w > 0 && h > 0 && dw > 0 && dh > 0) io.DisplayFramebufferScale = ImVec2((float)dw / w, (float)dh / h);

    const std::uint64_t now = m_b.PerfCounter(), freq = m_b.PerfFrequency();
    double dt = freq ? (double)(now - m_lastTicks) / (double)freq : 1.0 / 60.0;
    if (!(dt > 0)) dt = 1.0 / 1000.0;   // ImGui needs a step above zero
    io.DeltaTime = (float)dt;
    m_lastTicks = now;

    // The pointer left the window: ImGui must stop hovering what it last saw.
    const bool inside = m_w.HasMouseFocus();
    if (m_mouseInside && !inside) io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    m_mouseInside = inside;

    // Text input only while a text field has the keyboard (IME candidates and on-screen keyboards follow it).
    if (!m_textAlways && io.WantTextInput != m_textOn) {
        m_textOn = io.WantTextInput;
        if (m_textOn) m_b.StartTextInput(); else m_b.StopTextInput();
    }

    // The shape: one pointer for every window, so only the glue of the window under the pointer sets it, and only when it
    // differs from what any glue set last (the chat window's text cursor must not stay over the launcher).
    if (!(io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) && (inside || g_appliedCursor == -2)) {
        const int c = io.MouseDrawCursor ? (int)ImGuiMouseCursor_None : (int)ImGui::GetMouseCursor();
        if (c != g_appliedCursor) {
            g_appliedCursor = c;
            if (c == ImGuiMouseCursor_None) m_b.ShowCursor(false);
            else {
                plat::SystemCursor sc = plat::SystemCursor::Arrow;
                switch (c) {
                case ImGuiMouseCursor_TextInput: sc = plat::SystemCursor::TextInput; break;
                case ImGuiMouseCursor_ResizeAll: sc = plat::SystemCursor::ResizeAll; break;
                case ImGuiMouseCursor_ResizeNS: sc = plat::SystemCursor::ResizeNS; break;
                case ImGuiMouseCursor_ResizeEW: sc = plat::SystemCursor::ResizeEW; break;
                case ImGuiMouseCursor_ResizeNESW: sc = plat::SystemCursor::ResizeNESW; break;
                case ImGuiMouseCursor_ResizeNWSE: sc = plat::SystemCursor::ResizeNWSE; break;
                case ImGuiMouseCursor_Hand: sc = plat::SystemCursor::Hand; break;
                case ImGuiMouseCursor_Wait: case ImGuiMouseCursor_Progress: sc = plat::SystemCursor::Wait; break;
                case ImGuiMouseCursor_NotAllowed: sc = plat::SystemCursor::NotAllowed; break;
                default: break;
                }
                m_b.SetSystemCursor(sc);
                m_b.ShowCursor(true);
            }
        }
    }
}

} // namespace okay::imguiown
