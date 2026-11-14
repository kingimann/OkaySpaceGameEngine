#pragma once
// The one part of hosting OkayUI that needs Dear ImGui's internals (so it is kept out of OkayUIHost.hpp, which the tests link
// without ImGui): is an ImGui popup in the way of a hosted OkayUI rectangle?
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

} // namespace okayuihost
