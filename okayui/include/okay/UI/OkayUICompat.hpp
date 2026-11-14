#pragma once
// Small calls that code written for Dear ImGui uses a lot and that OkayUI did not have: the main viewport, colour conversion, theme
// colours as floats, content-region extents, scroll-to-cursor, window size constraints, and a few "accepted, does nothing" calls so
// ported code compiles unchanged (the same approach as the ignored StyleVar values in OkayUI.hpp). Built only on OkayUI's public
// API: no SDL, no Dear ImGui. Chosen from the ranked list in docs/imgui_port_report.md.
//
// What each call does here, and where it differs from Dear ImGui, is written at the declaration.
#include "okay/UI/OkayUI.hpp"

namespace OkayUI {

/// The area OkayUI draws into: position (0, 0) and the display size the host gave BeginFrame (Input::displayW/H; zeros when the host
/// gave none). OkayUI has one viewport, so there is no per-window or multi-monitor variant.
struct Viewport {
    Vec2 pos, size;
    Vec2 GetCenter() const { return Vec2{ pos.x + size.x * 0.5f, pos.y + size.y * 0.5f }; }
};
Viewport GetMainViewport();

/// The far corner of the current window's content region, relative to the window (cursor position plus the space still available).
/// Dear ImGui's GetContentRegionMax is the window's full content size; here it is the remaining extent from the cursor, which is
/// what the calls in the editor use it for (fill to the edge).
Vec2 GetContentRegionMax();
Vec2 GetWindowContentRegionMax();

/// Scroll the current child window so the cursor row lands at `centerRatio` of its height (0 top, 0.5 middle, 1 bottom: the usual
/// "keep the newest line in view" call). The cursor is the top of the next item, so after the last line ratio 1 is the end. Outside a scrolling child it does nothing. The scroll applies when the child ends.
void SetScrollHereY(float centerRatio = 0.5f);
/// The visible height of the current scrolling child's content area (its height minus padding); 0 outside a child. Defined next to
/// SetScrollY in OkayUI.cpp (it needs the child stack); SetScrollHereY uses it.
float GetChildViewHeight();
/// Horizontal scrolling does not exist in OkayUI: GetScrollX is always 0 and SetScrollX does nothing.
float GetScrollX();
void SetScrollX(float x);

/// The next window gets a resize grip with `minSize` as its smallest size (ResizableNextWindow). `maxSize` is not enforced.
void SetNextWindowSizeConstraints(Vec2 minSize, Vec2 maxSize);

/// Accepted and ignored, so ported code compiles: OkayUI has no keyboard-default-focus item and no overlap rule between items.
/// (Colour conversion, GetStyleColorVec4, GetID and TableSetupScrollFreeze, which this header used to define, are real OkayUI calls now: OkayUI.hpp.)
void SetItemDefaultFocus();
void SetNextItemAllowOverlap();

// ---- Windows by name (the OkayUI form of the Dear ImGui internals the editor's panel code reads) ---------------------------------------
// `FindWindowByName`, `GetCurrentWindow`, `GImGui->NavWindow` and the fields of `ImGuiWindow` that the editor and the launcher actually
// read (Name, Pos, Size, Active, WasActive, DockId / DockNode, DockTabIsVisible, InnerClipRect, ID) as one small value type with no
// pointers into OkayUI's state. Found by grepping editor/ and launcher/ (docs/own_ui_toolkit.md, section 4.37): nothing else of ImGuiWindow
// is read by them, and the many `DC.*` / `RootWindow` / `ParentWindow` / `TabBar` uses are covered below or have no OkayUI meaning.
//  * Names follow Dear ImGui's rule: a title "Console###Console" is found by "###Console" (only "###" and what follows makes the id).
//  * OkayUI has no child windows with their own entries: a BeginChild is part of the window that began it, so there is no ParentWindow /
//    RootWindow chain; "is the focused window one of mine" is a test of GetFocusedWindowInfo().name (or GetCurrentWindowInfo() while
//    inside the window and its children).
//  * Windows are remembered by the title they were first begun with (up to 64 windows, 63 bytes of name).
struct WindowInfo {
    bool found = false;
    char name[64] = {0};              // the title as given to Begin / DockBegin
    unsigned id = 0;                  // what ImGuiWindow::ID is: the same number FindWindowByID takes
    Vec2 pos, size;                   // screen position and size as last drawn (size is the body: just the title bar when collapsed)
    bool active = false;              // Begin was called for it in this frame (so far)
    bool wasActive = false;           // it was drawn this frame or the one before (ImGuiWindow::Active || WasActive)
    bool focused = false;             // it is the focused window
    bool collapsed = false;
    int  order = 0;                   // 0 is the back-most of the windows OkayUI knows; larger is in front
    bool docked = false;              // it is a tab or the only window of a leaf of the dock space (OkayUIDock.hpp)
    int  dockNode = 0;                // that leaf's id (DockLayout node id; ImGuiWindow::DockId), 0 when floating or unknown
    bool dockTabVisible = false;      // it is the chosen tab of that leaf (ImGuiWindow::DockTabIsVisible)
};
WindowInfo FindWindowByName(const char* name);
WindowInfo FindWindowByID(unsigned id);
WindowInfo GetCurrentWindowInfo();    // found = false outside Begin / End
WindowInfo GetFocusedWindowInfo();    // found = false when no window is focused (ImGui's NavWindow)
/// Every named window OkayUI knows, back to front (what iterating `g.Windows` gave); returns how many were written (at most `cap`); with a null `out` it
/// returns how many windows there are. Docked tabs that are not the chosen one never begin a window, so they are found by name (FindWindowByName) but not listed here.
int GetWindowInfos(WindowInfo* out, int cap);
/// The id of the current window (ImGuiWindow::ID; 0 outside Begin / End), for BeginDragDropTargetCustom(GetWindowInnerRect(), GetWindowID()).
unsigned GetWindowID();
/// The screen rectangle of the current window's content area, or of the current child when inside one (ImGuiWindow::InnerClipRect).
Rect GetWindowInnerRect();
/// Raise a window in front of the others (ImGui::BringWindowToDisplayFront); nullptr raises the current window. A window that never rises
/// (NoBringToFront, a host panel) stays where it is.
void BringWindowToDisplayFront(const char* name = nullptr);
/// The width of full-width widgets in the current window when no PushItemWidth / SetNextItemWidth says otherwise, until the window ends
/// (ImGuiWindow::DC.ItemWidthDefault); a width that is not positive clears it.
void SetItemWidthDefault(float width);
/// The next window begins collapsed (or open). Cond_Always sets it every time, Cond_Once / Cond_FirstUseEver only the first time the window is seen.
void SetNextWindowCollapsed(bool collapsed, Cond cond = Cond_Always);

} // namespace OkayUI
