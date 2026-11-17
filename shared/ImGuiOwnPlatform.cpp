// The shared Dear ImGui platform glue over okay::platform. See ImGuiOwnPlatform.hpp.
//
// Written from imgui.h's documented contract only (moved here unchanged from launcher/ImGuiOwnBackend.cpp, where it was written):
//  * ImGuiIO input events: AddMousePosEvent, AddMouseButtonEvent, AddMouseWheelEvent (y > 0 up, x > 0 left),
//    AddKeyEvent, AddInputCharactersUTF8, AddFocusEvent; DisplaySize, DisplayFramebufferScale, DeltaTime.
//  * ImGuiPlatformIO::Platform_SetImeDataFn: called at the end of a frame with the caret position (InputPos, in ImGui's
//    coordinates, minus the viewport's Pos) and the line height while a text field wants the input method (WantVisible).
//  * Multi-window panels (EnableViewports), from imgui.h's "Multi-Viewport Support" section and the comments on ImGuiPlatformIO's
//    Platform_* functions, ImGuiViewport (its flags, PlatformUserData / PlatformHandle, PlatformRequestMove / Resize / Close) and
//    ImGuiPlatformMonitor: with viewports on, every ImGui coordinate is a desktop coordinate, so the mouse goes in as a desktop
//    position; the main viewport must carry the main window; the monitor list must be filled before the first frame; ImGui calls
//    the platform functions from NewFrame (read the windows' state) and UpdatePlatformWindows (create, move, size, show, destroy).
#include "ImGuiOwnPlatform.hpp"

#include "imgui.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace okay::imguiown {

namespace plat = okay::platform;

namespace {
// The cursor shape the backend was last told (one pointer, shared by every window and every glue): -2 = none yet.
int g_appliedCursor = -2;

// What a viewport's PlatformUserData points at: the okay::platform window that shows it. The main viewport's window belongs to the
// app (`own` is empty); a panel's window is made and destroyed here.
struct VpWin {
    std::unique_ptr<plat::Window> own;
    plat::Window* win = nullptr;
    int lastX = 0, lastY = 0;   // the position we set or last saw (a decorated panel window moved by the window manager)
};
VpWin* DataOf(ImGuiViewport* vp) { return vp ? (VpWin*)vp->PlatformUserData : nullptr; }
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
    if (m_viewports) {
        // Every panel window goes (ImGui calls VpDestroy for each viewport, the main one included), then the hooks.
        ImGui::DestroyPlatformWindows();
        ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        pio.Platform_CreateWindow = nullptr; pio.Platform_DestroyWindow = nullptr; pio.Platform_ShowWindow = nullptr;
        pio.Platform_SetWindowPos = nullptr; pio.Platform_GetWindowPos = nullptr; pio.Platform_SetWindowSize = nullptr;
        pio.Platform_GetWindowSize = nullptr; pio.Platform_GetWindowFramebufferScale = nullptr; pio.Platform_SetWindowFocus = nullptr;
        pio.Platform_GetWindowFocus = nullptr; pio.Platform_GetWindowMinimized = nullptr; pio.Platform_SetWindowTitle = nullptr;
        pio.Platform_SetWindowAlpha = nullptr;
        pio.Monitors.resize(0);
        ImGuiIO& io = ImGui::GetIO();
        io.BackendFlags &= ~ImGuiBackendFlags_PlatformHasViewports;
        io.BackendPlatformUserData = nullptr;
        m_viewports = false;
    }
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
    plat::Window* w = WindowOf(vp);   // a panel window's caret goes to that window
    g->m_imeTaken = (w ? *w : g->m_w).SetTextInputArea(x, y, 1, h);
    if (g->m_log) std::fprintf(stderr, "%s: ime caret %d,%d height %d (%s)\n", g->m_logPrefix, x, y, h, g->m_imeTaken ? "taken" : "not taken");
}

bool ImGuiPlatformGlue::ProcessEvent(const plat::Event& e) {
    // Our window's events, and with multi-window panels also those of the panels' windows (any other window is not ImGui's).
    ImGuiViewport* panel = nullptr;
    if (e.windowId != 0 && e.windowId != m_w.Id()) {
        panel = m_viewports ? ViewportOfWindowId(e.windowId) : nullptr;
        if (!panel) return false;
    }
    ImGuiIO& io = ImGui::GetIO();
    // With viewports on, ImGui's coordinates are desktop coordinates: the OS's desktop position of the pointer, else the window's
    // corner plus the position in it. Without, the position in the main window.
    const bool desktop = ViewportsActive();
    auto mouseX = [&]() -> float {
        if (!desktop) return (float)e.x;
        if (e.hasScreenPos) return (float)e.screenX;
        int wx = 0, wy = 0; plat::Window* w = panel ? WindowOf(panel) : &m_w; if (w) w->GetPosition(wx, wy);
        return (float)(e.x + wx);
    };
    auto mouseY = [&]() -> float {
        if (!desktop) return (float)e.y;
        if (e.hasScreenPos) return (float)e.screenY;
        int wx = 0, wy = 0; plat::Window* w = panel ? WindowOf(panel) : &m_w; if (w) w->GetPosition(wx, wy);
        return (float)(e.y + wy);
    };
    switch (e.type) {
    case plat::EventType::MouseMove:
        io.AddMouseSourceEvent(e.fromTouch ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);   // a finger acting as the mouse
        io.AddMousePosEvent(mouseX(), mouseY());
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
        io.AddMouseSourceEvent(e.fromTouch ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
        const float mx = mouseX(), my = mouseY();
        io.AddMousePosEvent(mx, my);
        io.AddMouseButtonEvent(b, down);
        if (down) m_buttonsDown |= 1u << b; else m_buttonsDown &= ~(1u << b);
        if (m_log) {
            if (panel) std::fprintf(stderr, "%s: mouse %d %s at %d,%d (panel window %u, desktop %.0f,%.0f)\n", m_logPrefix, b,
                                    down ? "down" : "up", e.x, e.y, (unsigned)e.windowId, mx, my);
            else std::fprintf(stderr, "%s: mouse %d %s at %d,%d\n", m_logPrefix, b, down ? "down" : "up", e.x, e.y);
        }
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
    // A panel window closed or sized by the OS (a window manager's close button or border): ImGui reads the request next frame.
    case plat::EventType::WindowClose:
        if (!panel) return false;
        panel->PlatformRequestClose = true;
        return true;
    case plat::EventType::WindowResized:
        if (!panel) return false;
        panel->PlatformRequestResize = true;
        return true;
    default: return false;
    }
}

// ---- multi-window panels ----------------------------------------------------------------------------------

ImGuiPlatformGlue* ImGuiPlatformGlue::Current() {
    return ImGui::GetCurrentContext() ? (ImGuiPlatformGlue*)ImGui::GetIO().BackendPlatformUserData : nullptr;
}

plat::Window* ImGuiPlatformGlue::WindowOf(ImGuiViewport* vp) {
    VpWin* d = DataOf(vp);
    return d ? d->win : nullptr;
}

bool ImGuiPlatformGlue::ViewportsActive() const {
    const ImGuiIO& io = ImGui::GetIO();
    return m_viewports && (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) &&
           (io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports) && (io.BackendFlags & ImGuiBackendFlags_RendererHasViewports);
}

ImGuiViewport* ImGuiPlatformGlue::ViewportOfWindowId(std::uint32_t id) const {
    if (!id) return nullptr;
    for (ImGuiViewport* vp : ImGui::GetPlatformIO().Viewports) {
        plat::Window* w = WindowOf(vp);
        if (w && w->Id() == id) return vp;
    }
    return nullptr;
}

void ImGuiPlatformGlue::EnableViewports(bool opengl) {
    if (m_viewports) return;
    m_viewports = true;
    m_viewportGL = opengl;
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformUserData = this;
    io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_CreateWindow = VpCreate;
    pio.Platform_DestroyWindow = VpDestroy;
    pio.Platform_ShowWindow = VpShow;
    pio.Platform_SetWindowPos = VpSetPos;
    pio.Platform_GetWindowPos = VpGetPos;
    pio.Platform_SetWindowSize = VpSetSize;
    pio.Platform_GetWindowSize = VpGetSize;
    pio.Platform_GetWindowFramebufferScale = VpGetScale;
    pio.Platform_SetWindowFocus = VpSetFocus;
    pio.Platform_GetWindowFocus = VpGetFocus;
    pio.Platform_GetWindowMinimized = VpGetMinimized;
    pio.Platform_SetWindowTitle = VpSetTitle;
    pio.Platform_SetWindowAlpha = VpSetAlpha;
    // The main viewport is the main window (the app made it; never destroyed here). ImGui finds viewports by PlatformHandle.
    ImGuiViewport* main = ImGui::GetMainViewport();
    VpWin* d = new VpWin();
    d->win = &m_w;
    main->PlatformUserData = d;
    main->PlatformHandle = (void*)(std::uintptr_t)m_w.Id();
    UpdateMonitors(true);   // ImGui wants the list before its first frame
}

void ImGuiPlatformGlue::UpdateMonitors(bool force) {
    const std::uint64_t now = m_b.PerfCounter(), freq = m_b.PerfFrequency();
    if (!force && m_monitorsAt && freq && now - m_monitorsAt < freq) return;   // once a second: a monitor plugged in or moved
    m_monitorsAt = now ? now : 1;
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Monitors.resize(0);
    const std::vector<plat::DisplayInfo> ds = m_b.Displays();
    for (std::size_t i = 0; i < ds.size(); ++i) {
        const plat::DisplayInfo& di = ds[i];
        if (di.bounds.w <= 0 || di.bounds.h <= 0) continue;
        ImGuiPlatformMonitor m;
        m.MainPos = ImVec2((float)di.bounds.x, (float)di.bounds.y);
        m.MainSize = ImVec2((float)di.bounds.w, (float)di.bounds.h);
        const bool hasUsable = di.usable.w > 0 && di.usable.h > 0;
        m.WorkPos = hasUsable ? ImVec2((float)di.usable.x, (float)di.usable.y) : m.MainPos;
        m.WorkSize = hasUsable ? ImVec2((float)di.usable.w, (float)di.usable.h) : m.MainSize;
        m.DpiScale = di.scale > 0.0f ? di.scale : 1.0f;
        m.PlatformHandle = (void*)(std::uintptr_t)i;   // the index in Backend::Displays()
        pio.Monitors.push_back(m);
    }
    if (pio.Monitors.Size == 0) {   // the backend lists none: the main window's own rectangle, so ImGui has one
        int x = 0, y = 0, w = 0, h = 0;
        m_w.GetPosition(x, y);
        m_w.GetSize(w, h);
        ImGuiPlatformMonitor m;
        m.MainPos = m.WorkPos = ImVec2((float)x, (float)y);
        m.MainSize = m.WorkSize = ImVec2((float)(w > 0 ? w : 1), (float)(h > 0 ? h : 1));
        pio.Monitors.push_back(m);
    }
}

void ImGuiPlatformGlue::VpCreate(ImGuiViewport* vp) {
    ImGuiPlatformGlue* g = Current();
    if (!g || !vp) return;
    plat::WindowDesc d;
    d.title = "OkaySpace";
    d.x = (int)vp->Pos.x; d.y = (int)vp->Pos.y;
    d.width = vp->Size.x >= 1.0f ? (int)vp->Size.x : 1;
    d.height = vp->Size.y >= 1.0f ? (int)vp->Size.y : 1;
    d.hidden = true;   // ImGui sets the position, size and title, then asks for ShowWindow
    d.resizable = true;
    d.highDpi = true;
    d.opengl = g->m_viewportGL;
    d.borderless = (vp->Flags & ImGuiViewportFlags_NoDecoration) != 0;
    d.skipTaskbar = (vp->Flags & ImGuiViewportFlags_NoTaskBarIcon) != 0;
    d.topmost = (vp->Flags & ImGuiViewportFlags_TopMost) != 0;
    d.noActivate = (vp->Flags & ImGuiViewportFlags_NoFocusOnAppearing) != 0;
    VpWin* data = new VpWin();
    data->own = g->m_b.MakeWindow(d);
    data->win = data->own.get();
    data->lastX = d.x; data->lastY = d.y;
    vp->PlatformUserData = data;
    vp->PlatformHandle = (void*)(std::uintptr_t)(data->win ? data->win->Id() : 0);
    if (data->win) { ++g->m_panelWindows; ++g->m_panelWindowsMade; }
    if (g->m_log) std::fprintf(stderr, "%s: panel window %s (%dx%d at %d,%d)\n", g->m_logPrefix,
                               data->win ? "made" : "could not be made", d.width, d.height, d.x, d.y);
}

void ImGuiPlatformGlue::VpDestroy(ImGuiViewport* vp) {
    VpWin* d = DataOf(vp);
    if (!d) return;
    ImGuiPlatformGlue* g = Current();
    if (d->own && g) {
        --g->m_panelWindows;
        if (g->m_log) std::fprintf(stderr, "%s: panel window closed\n", g->m_logPrefix);
    }
    delete d;   // a panel's window goes with it; the main window is the app's
    vp->PlatformUserData = nullptr;
    vp->PlatformHandle = nullptr;
}

void ImGuiPlatformGlue::VpShow(ImGuiViewport* vp) { if (plat::Window* w = WindowOf(vp)) w->Show(); }

void ImGuiPlatformGlue::VpSetPos(ImGuiViewport* vp, ImVec2 pos) {
    VpWin* d = DataOf(vp);
    if (!d || !d->win) return;
    d->lastX = (int)pos.x; d->lastY = (int)pos.y;
    d->win->SetPosition(d->lastX, d->lastY);
}

ImVec2 ImGuiPlatformGlue::VpGetPos(ImGuiViewport* vp) {
    plat::Window* w = WindowOf(vp);
    int x = 0, y = 0;
    if (w) w->GetPosition(x, y);
    return ImVec2((float)x, (float)y);
}

void ImGuiPlatformGlue::VpSetSize(ImGuiViewport* vp, ImVec2 size) {
    if (plat::Window* w = WindowOf(vp)) w->SetSize(size.x >= 1.0f ? (int)size.x : 1, size.y >= 1.0f ? (int)size.y : 1);
}

ImVec2 ImGuiPlatformGlue::VpGetSize(ImGuiViewport* vp) {
    plat::Window* w = WindowOf(vp);
    int x = 0, y = 0;
    if (w) w->GetSize(x, y);
    return ImVec2((float)x, (float)y);
}

ImVec2 ImGuiPlatformGlue::VpGetScale(ImGuiViewport* vp) {
    plat::Window* w = WindowOf(vp);
    int x = 0, y = 0, dx = 0, dy = 0;
    if (w) { w->GetSize(x, y); w->GetDrawableSize(dx, dy); }
    if (x <= 0 || y <= 0 || dx <= 0 || dy <= 0) return ImVec2(1.0f, 1.0f);
    return ImVec2((float)dx / (float)x, (float)dy / (float)y);
}

void ImGuiPlatformGlue::VpSetFocus(ImGuiViewport* vp) {
    if (plat::Window* w = WindowOf(vp)) { w->Raise(); w->SetInputFocus(); }
}

bool ImGuiPlatformGlue::VpGetFocus(ImGuiViewport* vp) {
    plat::Window* w = WindowOf(vp);
    return w && w->IsFocused();
}

bool ImGuiPlatformGlue::VpGetMinimized(ImGuiViewport* vp) {
    plat::Window* w = WindowOf(vp);
    return w && w->IsMinimized();
}

void ImGuiPlatformGlue::VpSetTitle(ImGuiViewport* vp, const char* title) {
    if (plat::Window* w = WindowOf(vp)) w->SetTitle(title ? title : "");
}

void ImGuiPlatformGlue::VpSetAlpha(ImGuiViewport* vp, float alpha) {
    if (plat::Window* w = WindowOf(vp)) w->SetOpacity(alpha);
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

    // The pointer left the window (every window, with panels): ImGui must stop hovering what it last saw. Not while a button is
    // held: the OS still sends that drag's motion (dragging a panel out of the editor goes past its edge).
    bool inside = m_w.HasMouseFocus();
    if (m_viewports) {
        UpdateMonitors(false);
        ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        for (ImGuiViewport* vp : pio.Viewports) {
            VpWin* d = DataOf(vp);
            if (!d || !d->win || !d->own) continue;
            if (d->win->HasMouseFocus()) inside = true;
            // A panel window with a title bar can be moved by the window manager: tell ImGui to take the window's place.
            // (Borderless ones, ImGui's default, are only moved by ImGui itself.)
            if (!(vp->Flags & ImGuiViewportFlags_NoDecoration)) {
                int x = 0, y = 0;
                d->win->GetPosition(x, y);
                if (x != d->lastX || y != d->lastY) { d->lastX = x; d->lastY = y; vp->PlatformRequestMove = true; }
            }
        }
    }
    if (m_mouseInside && !inside && !m_buttonsDown) io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    m_mouseInside = inside || (m_mouseInside && m_buttonsDown);

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
