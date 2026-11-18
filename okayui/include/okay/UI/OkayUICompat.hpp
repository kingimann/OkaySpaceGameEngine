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
/// Hook for FindWindowByName (implemented next to the window slots in OkayUI.cpp): false when no window of that title has been drawn.
bool FindWindowSlot(const char* title, Vec2& pos, Vec2& size, bool& focused);
/// Horizontal scrolling does not exist in OkayUI: GetScrollX is always 0 and SetScrollX does nothing.
float GetScrollX();
void SetScrollX(float x);

/// The next window gets a resize grip with `minSize` as its smallest size (ResizableNextWindow). `maxSize` is not enforced.
void SetNextWindowSizeConstraints(Vec2 minSize, Vec2 maxSize);

/// Accepted and ignored, so ported code compiles: OkayUI has no keyboard-default-focus item and no overlap rule between items.
/// (Colour conversion, GetStyleColorVec4, GetID and TableSetupScrollFreeze, which this header used to define, are real OkayUI calls now: OkayUI.hpp.)
void SetItemDefaultFocus();
void SetNextItemAllowOverlap();


/// What the host can ask about a window by its title (ImGui's FindWindowByName, as a plain value instead of a pointer to internals).
/// `found` is false for a title that was never drawn (and for null or empty). `pos` and `size` are where the window was last drawn;
/// `focused` is the window SetNextWindowFocus / SetWindowFocus / a click gave the focus to; `docked` is true while the window sits in a
/// leaf of the dock layout (OkayUIDock.hpp). A snapshot: nothing here changes the window.
struct WindowInfo { bool found = false; Vec2 pos{0, 0}; Vec2 size{0, 0}; bool focused = false; bool docked = false; };
WindowInfo FindWindowByName(const char* title);
/// IsWindowDocked for a window by title (false for an unknown one).
bool IsWindowDocked(const char* title);
/// The next ImGui-style Begin(title, p_open, flags) takes the focus and comes to the front (as SetWindowFocus). Dropped by a Begin with a
/// null title; one request, used up by the next Begin.
void SetNextWindowFocus();
/// How far the pointer has moved since the button went down, once that is more than `threshold` pixels (default 4); (0, 0) before that, after
/// release, and for any button but the left one (0), the only one OkayUI tracks. Like IsMouseDragging, zero while the pointer is blocked.
Vec2 GetMouseDragDelta(int button = 0, float threshold = -1.0f);
/// Forget the drag so far: GetMouseDragDelta is zero until the pointer moves again from where it is now.
void ResetMouseDragDelta(int button = 0);
/// Some item was under the pointer (and not blocked by a window above) during this frame so far or the frame before.
bool IsAnyItemHovered();

/// A tooltip that takes widgets: `if (IsItemHovered() && BeginTooltip()) { Text("..."); Separator(); Text("..."); EndTooltip(); }`.
/// The widgets between the calls are laid out in a box next to the cursor (on top of everything, flipped left / above when it would
/// run off the display), sized to what they cover, with input switched off (a tooltip is for reading: buttons in it do not click).
/// BeginTooltip returns false only outside a window; EndTooltip is safe to call when it returned false, and a tooltip begun inside
/// a tooltip draws into the outer one. While a window covers the pointer (Input::blocked) the widgets still run but nothing is shown.
/// Limits: no scrolling child inside (the clip rectangle does not apply to the overlay); a full-width item such as Separator makes
/// the box as wide as the largest allowed (480 px, or the display less 40).
bool BeginTooltip();
void EndTooltip();

/// The style as the fields ported code reads (Dear ImGui names), filled from OkayUI's own settings at each call, so it follows
/// PushStyleVar and Style(). READ ONLY: a write to the returned struct is lost at the next call (use Style(), PushStyleVar and
/// PushStyleColor to change things). Approximations are listed in docs/own_ui_toolkit.md section 6.7: FramePadding, ItemInnerSpacing
/// and CellPadding are the values the layout uses, not separate settings; Alpha is always 1.
struct StyleInfo {
    float Alpha = 1.0f;
    Vec2 WindowPadding, FramePadding, ItemSpacing, ItemInnerSpacing, CellPadding;
    float ScrollbarSize = 10.0f;
    float WindowRounding = 0.0f, ChildRounding = 0.0f, FrameRounding = 0.0f;
    float WindowBorderSize = 0.0f, ChildBorderSize = 0.0f, FrameBorderSize = 0.0f;
    float FontScale = 1.0f;                // the theme's text scale (2 = 16 px glyphs), not a multiplier of 1
    Vec4 Colors[Col_COUNT];                // index with the Col ids
};
const StyleInfo& GetStyle();

/// Lay out only the visible rows of a long list that is not a table. Usage:
///     ListClipper clip; clip.Begin(count);                  // or Begin(count, rowStep) when every row has the same step
///     while (clip.Step()) for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) DrawRow(i);
/// Without a row step, row 0 is shown alone first and measured (the cursor's advance, spacing included), then the range is chosen.
/// The visible span is the innermost clip rectangle (a BeginChild or a clipped window), else the window, else the display, widened by
/// one row each side. Skipped rows are replaced by moving the cursor, so scroll extents match the full list. Rows must all have the same
/// step (a different-sized row is placed where the step says, not where its neighbours are). ForceDisplayRangeByIndices keeps rows
/// (e.g. the one being edited) in the range. The destructor ends the clipper; Begin again re-uses it.
struct ListClipper {
    int ItemsCount = 0, DisplayStart = 0, DisplayEnd = 0;
    float ItemsHeight = -1.0f;             // the row step; -1 until known
    ListClipper(); ~ListClipper();
    void Begin(int itemsCount, float itemHeight = -1.0f);
    bool Step();
    void End();
    void ForceDisplayRangeByIndices(int itemMin, int itemMax);   // inclusive
private:
    bool mActive = false, mMeasured = false; int mStage = 0, mForceMin = -1, mForceMax = -1; float mStartY = 0.0f;
    ListClipper(const ListClipper&); ListClipper& operator=(const ListClipper&);   // not copyable (declared, never defined)
};

} // namespace OkayUI
