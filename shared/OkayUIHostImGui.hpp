#pragma once
// The one part of hosting OkayUI that needs Dear ImGui's internals (so it is kept out of OkayUIHost.hpp, which the tests link
// without ImGui): is an ImGui popup, or an ImGui window drawn in front, in the way of a hosted OkayUI rectangle?
//
// OkayUI is flushed after ImGui's draw data, so it draws over every ImGui window. A modal dialog or a menu that opens above a
// hosted screen would be painted over by it. Hosts therefore ask this first, and while it answers true they draw the screen's
// Dear ImGui version instead (the launcher's What's New page, the editor's Console panel keep one): the popup is then layered
// correctly and the screen comes back to OkayUI when the popup closes.
#include "imgui.h"
#include "imgui_internal.h"

namespace okayuihost {

/// True when an open ImGui popup covers any part of the screen rectangle (x, y, w, h), or is a modal (which dims the whole view).
/// A popup that was requested this frame but has no window yet counts as covering: its first frame is when it would be painted over.
inline bool PopupCoversRect(float x, float y, float w, float h) {
    ImGuiContext& g = *GImGui;
    for (int i = 0; i < g.OpenPopupStack.Size; ++i) {
        ImGuiWindow* win = g.OpenPopupStack[i].Window;
        if (!win) return true;
        if (win->Flags & ImGuiWindowFlags_Modal) return true;
        if (!win->WasActive && !win->Active) continue;
        const ImRect r = win->Rect();
        if (r.Max.x > x && r.Min.x < x + w && r.Max.y > y && r.Min.y < y + h) return true;
    }
    return false;
}

/// True when another ImGui window that Dear ImGui draws in front of `win` overlaps the rectangle (x, y, w, h) (ImGui coordinates).
/// The same reason as for popups: OkayUI is flushed after ImGui, so a hosted body would be painted over a floating window that
/// lies on top of it (a History window behind Project Settings showed its Undo/Redo row over Project Settings' first line, and a
/// window pushed nearly off the left edge showed a column of first letters down Project Settings). Dear ImGui draws its top-level
/// windows in the order of its window list, back to front, with tooltips above them all; a docked window is drawn with its dock
/// host, so the order compared is that of `win`'s dock tree root. Windows inside the same root, child windows (drawn with their
/// parent), windows in another OS window, and windows not shown last frame do not count.
inline bool WindowInFrontCovers(const ImGuiWindow* win, float x, float y, float w, float h) {
    if (!win || w <= 0.0f || h <= 0.0f) return false;
    ImGuiContext& g = *GImGui;
    const ImGuiWindow* root = win->RootWindowDockTree ? win->RootWindowDockTree : win;
    int rootAt = -1;
    for (int i = 0; i < g.Windows.Size; ++i)
        if (g.Windows[i] == root) { rootAt = i; break; }
    for (int i = 0; i < g.Windows.Size; ++i) {
        const ImGuiWindow* o = g.Windows[i];
        if (o == root || o == win || (o->Flags & ImGuiWindowFlags_ChildWindow)) continue;
        if (i <= rootAt && !(o->Flags & ImGuiWindowFlags_Tooltip)) continue;   // behind (tooltips are always drawn on top)
        // The tooltip that follows a Dear ImGui drag does not count: a drag over the panel (a row of an OkayUI list dragged out and
        // back, an asset dragged in) must not switch the panel to its Dear ImGui version halfway.
        if ((o->Flags & ImGuiWindowFlags_Tooltip) && g.DragDropActive) continue;
        if (!o->WasActive || o->Hidden) continue;
        if (o->RootWindowDockTree == root) continue;
        if (o->Viewport && win->Viewport && o->Viewport != win->Viewport) continue;
        const ImRect r = o->Rect();
        if (r.Max.x > x && r.Min.x < x + w && r.Max.y > y && r.Min.y < y + h) return true;
    }
    return false;
}

/// Popups and windows in front together: while this is true a host draws the Dear ImGui version, which ImGui layers correctly.
inline bool CoveredInImGui(const ImGuiWindow* win, float x, float y, float w, float h) {
    return PopupCoversRect(x, y, w, h) || WindowInFrontCovers(win, x, y, w, h);
}

/// ImGui positions are desktop coordinates while multi-viewports are on (the editor turns them on): the main window's inside
/// starts at the main viewport's Pos, not at (0, 0). OkayUI draws into the main window and reads the mouse from SDL in that
/// window's own pixels, so a host moves its rectangle by this before it offers and draws it. Without viewports Pos is (0, 0).
inline ImVec2 ToMainWindowPixels(ImVec2 p) {
    const ImVec2 o = ImGui::GetMainViewport()->Pos;
    return ImVec2(p.x - o.x, p.y - o.y);
}

/// True when the ImGui window was dragged out into another OS window: OkayUI only draws into the main one.
inline bool OutsideMainWindow(const ImGuiWindow* w) { return w && w->Viewport && w->Viewport != ImGui::GetMainViewport(); }

} // namespace okayuihost
