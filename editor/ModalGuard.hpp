#pragma once
// Dear ImGui modals and clicks that go nowhere (owner, v2.903.8.118: "i just cant interact with anything or click anything" after a
// project opened; frames kept coming, so the hang watchdog stayed quiet). Header-only, Dear ImGui only, so tests run it headless
// (tests/test_editor_stale_modal.cpp).
//
//  - Sweep: called once a frame right after ImGui::NewFrame. A modal that is open but has not been drawn (submitted) for a few
//    frames is invisible yet would still block the mouse from every window behind it (Dear ImGui keeps a popup open until it is
//    closed, and GetTopMostPopupModal does not ask whether it was drawn). Dear ImGui closes such a modal by itself only when the
//    keyboard focus moves away from it, so the guard closes it and names it. A modal that is drawn but lies off the screen is moved
//    back to the middle of its viewport and named.
//  - ModalTrail: which modals opened and closed, for the crash breadcrumbs (okay_hang.txt and okay_crash.txt show the trail).
//  - InputProbe: counts the clicks the window system delivered and whether Dear ImGui saw them land on a window or an item. While
//    clicks arrive and none lands, Describe() writes what Dear ImGui thinks (the mouse, the hovered, focused and active window,
//    the top modal and its rectangle, the open popups) once every few seconds, for okay_editor_perf.log.
// Written from Dear ImGui's documented popup rules (popups stay open until closed; a modal blocks the windows behind it).
#include "imgui.h"
#include "imgui_internal.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace okayed::modalguard {

/// Frames a modal may go undrawn before the guard closes it (one or two frames happen when a modal is opened in one place and drawn
/// in another later in the frame).
constexpr int kUndrawnFrames = 3;

/// Close the named popup if it is open (and every popup opened over it). True when something was closed. For a gate that is no
/// longer needed (the sign-in confirmed, a project opened another way): it stops drawing its modal, so it must close it too.
inline bool CloseIfOpen(const char* name) {
    ImGuiContext& g = *GImGui;
    const ImGuiID id = ImGui::GetID(name);
    for (int i = 0; i < g.OpenPopupStack.Size; ++i)
        if (g.OpenPopupStack[i].PopupId == id) { ImGui::ClosePopupToLevel(i, true); return true; }
    return false;
}

/// Open the modal `name` when no other popup holds this level of the popup stack, and keep it open: call it every frame the question
/// is wanted, in place of `if (!IsPopupOpen(name)) OpenPopup(name)`. True when `name` is open (BeginPopupModal may follow).
/// Why: two questions that each re-open themselves every frame fight: Dear ImGui's OpenPopup at a level that holds another popup
/// closes that one, so "Recover Autosave" and "Restore recovered work?" replaced each other every frame after the editor was ended
/// in Task Manager; each was on its first frame every frame, which an auto-sized window spends hidden, so neither was ever shown
/// while a modal was always open and blocked every click (the owner's v2.903.8.118 report). Now the first to open stays until it is
/// answered and the next one waits its turn.
inline bool OpenModalWhenFree(const char* name) {
    ImGuiContext& g = *GImGui;
    const ImGuiID id = ImGui::GetID(name);
    const int level = g.BeginPopupStack.Size;
    if (g.OpenPopupStack.Size > level) return g.OpenPopupStack[level].PopupId == id;   // this level is taken: ours, or wait
    ImGui::OpenPopup(id);
    return true;
}

/// True when the window's rectangle shows nothing on its viewport (wholly outside it).
inline bool OffScreen(const ImGuiWindow* w) {
    const ImGuiViewport* vp = w->Viewport ? w->Viewport : ImGui::GetMainViewport();
    const ImRect r = w->Rect(), v(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y));
    if (v.GetWidth() <= 0.0f || v.GetHeight() <= 0.0f) return false;   // a minimised window: nothing to judge
    return r.Max.x <= v.Min.x || r.Max.y <= v.Min.y || r.Min.x >= v.Max.x || r.Min.y >= v.Max.y;
}

/// Frames a drawn modal may stay hidden (never shown) before the guard calls it a blocking popup.
constexpr int kHiddenFrames = 30;

/// Once a frame, right after ImGui::NewFrame. `closed(name)` is told about every modal closed because it was not drawn;
/// `moved(name)` about every modal moved back on screen; `blocking(name)`, once, about a modal that is drawn yet has stayed hidden
/// for kHiddenFrames frames (it is brought to the front and focused). Returns how many were closed.
inline int Sweep(const std::function<void(const char*)>& closed, const std::function<void(const char*)>& moved = {},
                 const std::function<void(const char*)>& blocking = {}) {
    ImGuiContext& g = *GImGui;
    static ImGuiStorage s_hidden;   // frames each modal (by window id) has been drawn hidden in a row
    for (int i = 0; i < g.OpenPopupStack.Size; ++i) {
        const ImGuiPopupData& p = g.OpenPopupStack[i];
        ImGuiWindow* w = p.Window;
        if (!w || !(w->Flags & ImGuiWindowFlags_Modal)) continue;
        int& hid = *s_hidden.GetIntRef(w->ID, 0);
        hid = (w->WasActive && w->Hidden) ? hid + 1 : 0;
        if (hid == kHiddenFrames) {
            ImGui::FocusWindow(w);
            ImGui::BringWindowToDisplayFront(w);
            if (blocking) blocking(w->Name);
        }
        if (g.FrameCount - p.OpenFrameCount > kUndrawnFrames && w->LastFrameActive < g.FrameCount - kUndrawnFrames) {
            const std::string name = w->Name;   // (closing does not free the window, but keep the name safe for the report)
            ImGui::ClosePopupToLevel(i, true);
            if (closed) closed(name.c_str());
            return 1;   // the stack changed: the rest is looked at next frame
        }
        if (w->WasActive && w->LastFrameActive >= g.FrameCount - 1 && OffScreen(w)) {
            const ImGuiViewport* vp = w->Viewport ? w->Viewport : ImGui::GetMainViewport();
            const ImVec2 c = vp->GetCenter();
            ImGui::SetWindowPos(w, ImVec2(c.x - w->Size.x * 0.5f, c.y - w->Size.y * 0.5f), ImGuiCond_Always);
            if (moved) moved(w->Name);
        }
    }
    return 0;
}

/// The modals that opened and closed since the last call, as breadcrumb lines ("Modal opened: <name>", "Modal closed: <name>").
class ModalTrail {
public:
    std::vector<std::string> Update() {
        std::vector<std::string> out, now;
        ImGuiContext& g = *GImGui;
        for (const ImGuiPopupData& p : g.OpenPopupStack)
            if (p.Window && (p.Window->Flags & ImGuiWindowFlags_Modal)) now.push_back(p.Window->Name);
        for (const std::string& n : now) if (!Has(m_open, n)) out.push_back("Modal opened: " + n);
        for (const std::string& n : m_open) if (!Has(now, n)) out.push_back("Modal closed: " + n);
        m_open.swap(now);
        return out;
    }
private:
    static bool Has(const std::vector<std::string>& v, const std::string& s) { for (const std::string& x : v) if (x == s) return true; return false; }
    std::vector<std::string> m_open;
};

/// What Dear ImGui thinks about the mouse right now, on one line.
inline std::string DescribeInputState() {
    ImGuiContext& g = *GImGui;
    const ImGuiIO& io = g.IO;
    auto name = [](const ImGuiWindow* w) { return w ? std::string(w->Name) : std::string("none"); };
    char buf[512];
    const ImGuiViewport* mv = ImGui::GetMainViewport();
    std::snprintf(buf, sizeof buf, "mouse %.0f,%.0f%s, display %.0fx%.0f, main window at %.0f,%.0f, WantCaptureMouse %d, hovered window %s, "
                  "focused window %s, active item %08X in %s, open popups %d",
                  io.MousePos.x, io.MousePos.y, ImGui::IsMousePosValid(&io.MousePos) ? "" : " (not valid)", io.DisplaySize.x, io.DisplaySize.y,
                  mv->Pos.x, mv->Pos.y, io.WantCaptureMouse ? 1 : 0, name(g.HoveredWindow).c_str(), name(g.NavWindow).c_str(),
                  (unsigned)g.ActiveId, name(g.ActiveIdWindow).c_str(), g.OpenPopupStack.Size);
    std::string s = buf;
    if (const ImGuiWindow* m = ImGui::GetTopMostPopupModal()) {
        std::snprintf(buf, sizeof buf, ", top modal \"%s\" at %.0f,%.0f size %.0fx%.0f, drawn %d frames ago%s", m->Name, m->Pos.x, m->Pos.y,
                      m->Size.x, m->Size.y, g.FrameCount - m->LastFrameActive, OffScreen(m) ? ", off screen" : "");
        s += buf;
    } else {
        s += ", no modal";
    }
    if (g.MouseViewport && g.MouseViewport != mv) { std::snprintf(buf, sizeof buf, ", mouse over another OS window (viewport %08X)", (unsigned)g.MouseViewport->ID); s += buf; }
    return s;
}

/// Clicks that arrive but land nowhere. The event loop calls NoteClick for every mouse press the window system delivers; the frame
/// calls EndFrame after the UI was drawn (before ImGui::Render). Every `periodSeconds` with clicks and none landed, EndFrame returns
/// a line for the log (empty otherwise).
class InputProbe {
public:
    explicit InputProbe(double periodSeconds = 5.0) : m_period(periodSeconds) {}
    void NoteClick(unsigned windowId, bool mainWindow) { ++m_clicks; m_lastWindow = windowId; if (!mainWindow) ++m_otherWindow; }
    std::string EndFrame(double now) {
        ImGuiContext& g = *GImGui;
        bool clicked = false;
        for (int b = 0; b < 3; ++b) clicked |= g.IO.MouseClicked[b];
        if (clicked) {
            ++m_seen;
            if (g.HoveredWindow || g.HoveredId != 0 || g.ActiveId != 0) ++m_landed;
        }
        if (m_start < 0.0) m_start = now;
        if (now - m_start < m_period) return {};
        std::string line;
        if (m_clicks >= 2 && m_landed == 0) {
            char buf[200];
            std::snprintf(buf, sizeof buf, "Clicks land nowhere: %d clicks arrived in %.0f s (%d on another window, last on window %u), Dear ImGui saw %d, none on a window. ",
                          m_clicks, now - m_start, m_otherWindow, m_lastWindow, m_seen);
            line = buf + DescribeInputState();
        }
        m_start = now; m_clicks = m_seen = m_landed = m_otherWindow = 0;
        return line;
    }
private:
    double m_period, m_start = -1.0;
    int m_clicks = 0, m_seen = 0, m_landed = 0, m_otherWindow = 0;
    unsigned m_lastWindow = 0;
};

}  // namespace okayed::modalguard
