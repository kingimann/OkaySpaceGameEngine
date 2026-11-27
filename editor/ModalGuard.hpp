#pragma once
// Modals and clicks that go nowhere (owner, v2.903.8.118: "i just cant interact with anything or click anything" after a project
// opened; frames kept coming, so the hang watchdog stayed quiet). Header-only, over OkayUI's popups, so tests run it headless
// (okayui/tests/test_okayui_port_modalguard.cpp).
//
//  - Sweep: called once a frame right after ImGui::NewFrame. A modal that is open but has not been drawn (submitted) for a few
//    frames still holds the screen for OpenModalWhenFree, so the guard closes it and names it.
//  - ModalTrail: which modals opened and closed, for the crash breadcrumbs (okay_hang.txt and okay_crash.txt show the trail).
//  - InputProbe: counts the clicks the window system delivered and whether the UI saw them land on a window or an item. While
//    clicks arrive and none lands, Describe() writes what the UI thinks (the mouse, the open popups, the top modal) once every few
//    seconds, for okay_editor_perf.log.
// Written from Dear ImGui's documented popup rules (popups stay open until closed; a modal blocks the windows behind it).
#include "imgui.h"
#include "imgui_internal.h"
#include "okay/UI/OkayUIInternals.hpp"   // ListOpenPopups, ClosePopup, CurrentPopupDepth

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace okayed::modalguard {

/// Frames a modal may go undrawn before the guard closes it (one or two frames happen when a modal is opened in one place and drawn
/// in another later in the frame).
constexpr int kUndrawnFrames = 3;

/// The guard over OkayUI's popups (okay/UI/OkayUIInternals.hpp:
/// ListOpenPopups, ClosePopup). OkayUI's rules differ in two ways that matter here: a modal blocks the windows behind it only in a frame it is
/// drawn (its blocking overlay is registered by BeginPopupModal), and a modal is always placed in the middle of the display. So an undrawn
/// modal cannot swallow clicks; it can still hold the screen for OpenModalWhenFree, so the sweep still closes it. Nothing is ever off screen.
inline bool CloseIfOpen(const char* name) { return OkayUI::ClosePopup(name); }

/// The first question asked keeps the screen until it is answered; the next one waits. OkayUI's open popups
/// carry no level, so "this level is taken" means another open modal when asked outside any popup, and nothing when asked inside one.
inline bool OpenModalWhenFree(const char* name) {
    if (ImGui::IsPopupOpen(name)) return true;
    if (OkayUI::CurrentPopupDepth() == 0) {
        OkayUI::PopupSlotInfo open[32];
        const int n = OkayUI::ListOpenPopups(open, 32);
        for (int i = 0; i < n; ++i) if (open[i].modal) return false;   // another question is on screen: wait
    }
    ImGui::OpenPopup(name);
    return true;
}

/// Frames a drawn modal may stay hidden (never shown) before the guard calls it a blocking popup (not used on OkayUI: a drawn modal shows).
constexpr int kHiddenFrames = 30;

/// Once a frame, right after ImGui::NewFrame: closes a modal that is open but has not been drawn for kUndrawnFrames frames and tells
/// `closed(name)`. `moved` and `blocking` are never called on OkayUI (see above). Returns how many were closed.
inline int Sweep(const std::function<void(const char*)>& closed, const std::function<void(const char*)>& moved = {},
                 const std::function<void(const char*)>& blocking = {}) {
    (void)moved; (void)blocking;
    OkayUI::PopupSlotInfo open[32];
    const int n = OkayUI::ListOpenPopups(open, 32);
    const unsigned long long now = (unsigned long long)ImGui::GetFrameCount();
    for (int i = 0; i < n; ++i) {
        const OkayUI::PopupSlotInfo& p = open[i];
        if (!p.modal || now < p.openedFrame + kUndrawnFrames + 1) continue;
        if (p.drawnFrame + kUndrawnFrames < now) {
            const std::string name = p.name[0] ? p.name : "(unnamed)";
            OkayUI::ClosePopupById(p.id);
            if (closed) closed(name.c_str());
            return 1;
        }
    }
    return 0;
}

/// The names of the modals open now (ModalTrail compares them frame to frame).
inline std::vector<std::string> OpenModalNames() {
    std::vector<std::string> now;
    OkayUI::PopupSlotInfo open[32];
    const int n = OkayUI::ListOpenPopups(open, 32);
    for (int i = 0; i < n; ++i) if (open[i].modal) now.push_back(open[i].name);
    return now;
}

/// What OkayUI thinks about the mouse right now, on one line.
inline std::string DescribeInputState() {
    ImGuiContext& g = *GImGui;
    const ImGuiIO& io = ImGui::GetIO();
    auto name = [](const ImGuiWindow* w) { return w ? std::string(w->Name) : std::string("none"); };
    char buf[512];
    OkayUI::PopupSlotInfo open[32];
    const int n = OkayUI::ListOpenPopups(open, 32);
    std::snprintf(buf, sizeof buf, "mouse %.0f,%.0f, display %.0fx%.0f, WantCaptureMouse %d, hovered window %s, focused window %s, active item %08X, open popups %d",
                  io.MousePos.x, io.MousePos.y, io.DisplaySize.x, io.DisplaySize.y, io.WantCaptureMouse ? 1 : 0, name(g.HoveredWindow).c_str(),
                  name(g.NavWindow).c_str(), (unsigned)g.ActiveId, n);
    std::string s = buf;
    const OkayUI::PopupSlotInfo* top = nullptr;
    for (int i = 0; i < n; ++i) if (open[i].modal) top = &open[i];
    if (top) {
        std::snprintf(buf, sizeof buf, ", top modal \"%s\", drawn %llu frames ago", top->name,
                      (unsigned long long)ImGui::GetFrameCount() - top->drawnFrame);
        s += buf;
    } else {
        s += ", no modal";
    }
    return s;
}


/// The modals that opened and closed since the last call, as breadcrumb lines ("Modal opened: <name>", "Modal closed: <name>").
class ModalTrail {
public:
    std::vector<std::string> Update() {
        std::vector<std::string> out, now = OpenModalNames();
        for (const std::string& n : now) if (!Has(m_open, n)) out.push_back("Modal opened: " + n);
        for (const std::string& n : m_open) if (!Has(now, n)) out.push_back("Modal closed: " + n);
        m_open.swap(now);
        return out;
    }
private:
    static bool Has(const std::vector<std::string>& v, const std::string& s) { for (const std::string& x : v) if (x == s) return true; return false; }
    std::vector<std::string> m_open;
};

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
        for (int b = 0; b < 3; ++b) clicked |= ImGui::IsMouseClicked(b);
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
