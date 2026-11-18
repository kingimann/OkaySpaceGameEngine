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

// ---- Step 6.9: small calls (written from what the names mean; tested in test_okayui_scopes.cpp) ----------------------------------

/// A key chord for Shortcut: one named key plus the modifiers that must be held with it. `Chord(KeyId_S, Mod_Ctrl | Mod_Shift)`.
/// (Own encoding: the key id in the low byte, the modifier bits above it; not Dear ImGui's chord values.)
using KeyChord = int;
enum KeyMods : int { Mod_None = 0, Mod_Ctrl = 1, Mod_Shift = 2, Mod_Alt = 4 };
constexpr KeyChord Chord(KeyId key, int mods = Mod_None) { return (int)key | ((mods & 7) << 8); }
enum ShortcutFlags : int { Shortcut_None = 0, Shortcut_AllowRepeat = 1, Shortcut_RouteAlways = 2 };
/// True on the frame the chord's key goes down while exactly the chord's modifiers are held (a chord without Ctrl does not fire while
/// Ctrl is held). It stays false while a text field has the keyboard (typing "s" must not save), unless Shortcut_RouteAlways is given,
/// and while an item owns the chord's key (SetItemKeyOwner in OkayUIInternals.hpp). Shortcut_AllowRepeat also fires on key repeat.
/// A chord with no key, or a bad key id, is never true. Ignores the pointer and the window: it is a global shortcut.
bool Shortcut(KeyChord chord, int flags = Shortcut_None);

/// Resize a window by title, as SetNextWindowSize would for its next Begin, but at any time (e.g. from a menu). Returns false for an
/// unknown, null or empty title or a non-finite size; true when the window was found (even if `cond` said not to apply it). Sizes are
/// kept between 1 and 1e6. Cond_Always applies now; Once and FirstUseEver apply only when the window has never been sized by the
/// host; Appearing only on the frame the window reappears. Takes effect when the window is next begun.
bool SetWindowSize(const char* title, Vec2 size, Cond cond = Cond_Always);

/// The top-left of the current window's content region in window coordinates (the padding, and the title bar's height). Zero outside a
/// window. Note the pair is not symmetric here: GetWindowContentRegionMax stays "cursor plus the space left" measured from the content
/// origin (see its comment), so use GetWindowContentRegionMin only for where content starts, not as the other end of a span.
Vec2 GetWindowContentRegionMin();

/// Where the pointer was when the innermost open popup being built was opened (its top-left corner); the pointer's position now when
/// called outside a popup. Lets a context menu place things at the click ("add node here").
Vec2 GetMousePosOnOpeningCurrentPopup();

/// The payload of the drag in progress (type, a copy of the bytes, size; `delivery` is false), or null when nothing is being dragged
/// or the source has not set a payload yet. For hover feedback before the drop ("this does not fit here"). The pointer is valid until
/// the next call.
const DragPayload* GetDragDropPayload();

/// A drop target over a rectangle you give (screen pixels) instead of the last item: for things the auto-layout does not know about
/// (a node on a canvas, a timeline lane). Same rules as BeginDragDropTarget (outline, then AcceptDragDropPayload, then
/// EndDragDropTarget); `id` is accepted for source compatibility and not used. False for an empty or non-finite rectangle.
bool BeginDragDropTargetCustom(Rect bb, unsigned id = 0);

// ---- Step 6.10: window, frame, context and style calls (written from what the names mean; tested in test_okayui_calls.cpp) ----------

/// The next ImGui-style Begin folds (true) or opens (false) the window. `cond` as for SetNextWindowPos: Always every time, Once and
/// FirstUseEver only the first time the window is ever seen, Appearing each time it comes back after a frame without it.
/// A window with Window_NoCollapse or Window_NoTitleBar is never collapsed.
void SetNextWindowCollapsed(bool collapsed, Cond cond = Cond_Always);
/// The next ImGui-style Begin draws its body (background and border) with this opacity (0 to 1; a non-finite value does nothing). The
/// title bar and the content are not changed. One request, used up by the next Begin.
void SetNextWindowBgAlpha(float alpha);
/// Move a window by title (any time) or the current one. It takes effect when the window is next begun, so a call from inside the window
/// moves it one frame later. False for an unknown, null or empty title or a non-finite position. Cond as for SetNextWindowPos
/// (Always, Once / FirstUseEver, Appearing); a condition that says no still returns true (the window was found).
bool SetWindowPos(const char* title, Vec2 pos, Cond cond = Cond_Always);
void SetWindowPos(Vec2 pos, Cond cond = Cond_Always);
/// Raise a window above the other windows without giving it the focus. False for an unknown title. Windows that sit at the bottom by
/// design (docked content, NoBringToFront) stay there. The CurrentWindow form takes what GetCurrentWindow() returned.
bool BringWindowToDisplayFront(const char* title);
struct CurrentWindow;
bool BringWindowToDisplayFront(const CurrentWindow& w);
/// FindWindowByName for a window id (CurrentWindow::id, GetID of the title). `docked` is not filled (false).
WindowInfo FindWindowByID(unsigned id);
/// True while an open popup is a modal (BeginPopupModal): Dear ImGui returns the window, OkayUI has no window objects, so this is the
/// yes / no answer callers use (`if (GetTopMostPopupModal() && ...)`).
bool GetTopMostPopupModal();
/// For the next frame only, report IO::wantCaptureKeyboard as `want` whatever the widgets say (a host that wants a shortcut to wait or
/// to go through). Used up by the next BeginFrame.
void SetNextFrameWantCaptureKeyboard(bool want);
/// The space from a tree node's left edge to where its label starts (the arrow's width): the arrow column of TreeNode.
float GetTreeNodeToLabelSpacing();
/// Where the current window's content starts, in window coordinates (the same value as GetWindowContentRegionMin; zero outside a window).
Vec2 GetCursorStartPos();

/// OkayUI has one viewport (the display). GetWindowViewport is GetMainViewport; SetNextWindowViewport is accepted and does nothing.
Viewport GetWindowViewport();
void SetNextWindowViewport(unsigned viewportId);

/// Item flags for PushItemFlag / PopItemFlag (our own values). Only ItemFlag_Disabled does something (it is BeginDisabled / EndDisabled
/// for the items in between); the others are accepted and ignored: OkayUI has no keyboard navigation to opt out of, no repeat-on-hold
/// buttons and no mixed-value display. Up to 16 pushes at once; pops are matched to pushes.
enum ItemFlags : int { ItemFlag_None = 0, ItemFlag_NoTabStop = 1, ItemFlag_ButtonRepeat = 2, ItemFlag_Disabled = 4, ItemFlag_NoNav = 8,
                       ItemFlag_NoNavDefaultFocus = 16, ItemFlag_MixedValue = 32 };
void PushItemFlag(int flag, bool enabled);
void PopItemFlag();
/// Width for `count` side-by-side items (a Vec3 field): `totalWidth` is shared out with the inner spacing between items, and one
/// PushItemWidth per item is made, the first item's on top, so the caller pops after each item (PopItemWidth) exactly as with Dear
/// ImGui. count is kept between 1 and 8 (the width stack holds 16). Pass CalcItemWidth() as the total.
void PushMultiItemsWidths(int count, float totalWidth);

/// Where a label's visible text ends: the first "##" (the hidden id part) or the end of the text. With `textEnd` the search stops there
/// (it returns textEnd when no "##" is found before it). Null in gives null out.
const char* FindRenderedTextEnd(const char* text, const char* textEnd = nullptr);

/// One global context. CreateContext returns a handle for it (the same one every time: OkayUI cannot hold two), DestroyContext resets
/// OkayUI's state (ResetState) and makes GetCurrentContext return null until CreateContext again, SetCurrentContext(null) does the same
/// without resetting, SetCurrentContext(handle) makes it current. Any other handle is ignored. These exist so host code that guards on
/// "is there a context" compiles; there is no NewFrame (call BeginFrame with the frame's Input).
struct Context;
Context* CreateContext();
void DestroyContext(Context* ctx = nullptr);
Context* GetCurrentContext();
void SetCurrentContext(Context* ctx);

/// The look presets: StyleColorsDark is the default OkayUI palette, StyleColorsLight a light one, StyleColorsClassic the dark blue-grey
/// one. Only the colours change (the eight base colours, text-dim, title, field and the shadow flag); sizes, rounding and the text scale
/// stay, and colours set with SetStyleColor / PushStyleColor are cleared. Calling one between a PushStyleColor and its pop is the caller's problem.
void StyleColorsDark();
void StyleColorsLight();
void StyleColorsClassic();

/// The dock layout as text (DockSaveLayout) and back. Only the dock layout is stored (not window positions or sizes). The returned
/// pointer is valid until the next call. `size` (optional) receives the length. LoadIniSettingsFromMemory takes `size` 0 as "up to the
/// terminating zero"; text that does not load leaves the layout as it was.
const char* SaveIniSettingsToMemory(unsigned long long* size = nullptr);
void LoadIniSettingsFromMemory(const char* data, unsigned long long size = 0);


// ---- Step 6.11: the names ported code uses for buttons, directions, hover / focus flags and child windows --------------------------

/// Mouse buttons by name. OkayUI tracks the left and right buttons; Middle is accepted everywhere a button is taken and is never down
/// or clicked (a middle-click feature needs the host to give OkayUI that button first). Before this step any button number but 1
/// behaved like the left button; now only 0 and 1 do anything.
enum MouseButton : int { MouseButton_Left = 0, MouseButton_Right = 1, MouseButton_Middle = 2 };
/// Directions for ArrowButton (the values it takes).
enum Dir : int { Dir_Left = 0, Dir_Right = 1, Dir_Up = 2, Dir_Down = 3 };

/// Flags for IsItemHovered(flags) and IsWindowHovered(flags). All are accepted. What they change here (OkayUI's item hover is a plain
/// "is the pointer over the item's rectangle", which already ignores disabled state, an active item elsewhere and overlapping items):
///   Hovered_AnyWindow (window form): true when the pointer is over any OkayUI window (IO::wantCaptureMouse).
///   Hovered_ChildWindows / Hovered_RootWindow / Hovered_RootAndChildWindows: no change (a child is part of its window's rectangle).
///   Hovered_AllowWhenDisabled, Hovered_AllowWhenBlockedByActiveItem, Hovered_AllowWhenOverlapped, Hovered_RectOnly: no change (already so).
///   Hovered_AllowWhenBlockedByPopup: no change (the item form does not look at popups).
///   Hovered_Stationary, Hovered_DelayShort, Hovered_DelayNormal, Hovered_ForTooltip, Hovered_NoSharedDelay: no delay is applied.
/// (Our own values.)
enum HoveredFlags : int {
    Hovered_None = 0, Hovered_ChildWindows = 1, Hovered_RootWindow = 2, Hovered_RootAndChildWindows = 3, Hovered_AnyWindow = 4,
    Hovered_AllowWhenBlockedByPopup = 8, Hovered_AllowWhenBlockedByActiveItem = 16, Hovered_AllowWhenOverlapped = 32,
    Hovered_AllowWhenDisabled = 64, Hovered_RectOnly = 128, Hovered_Stationary = 256, Hovered_DelayShort = 512, Hovered_DelayNormal = 1024,
    Hovered_ForTooltip = 2048, Hovered_NoSharedDelay = 4096
};
bool IsItemHovered(int flags);
bool IsWindowHovered(int flags);
/// Flags for IsWindowFocused(flags): all accepted; Focused_AnyWindow is true when any OkayUI window has the focus; the root / child
/// flags change nothing (a child is part of its window).
enum FocusedFlags : int { Focused_None = 0, Focused_ChildWindows = 1, Focused_RootWindow = 2, Focused_RootAndChildWindows = 3, Focused_AnyWindow = 4,
                          Focused_NoPopupHierarchy = 8 };
bool IsWindowFocused(int flags);
/// Some OkayUI window has the focus (implemented next to the window slots in OkayUI.cpp; IsWindowFocused(Focused_AnyWindow) uses it).
bool AnyWindowFocused();

/// Flags for the ImGui-style BeginChild(id, size, childFlags, windowFlags). Supported: Child_Borders (the child's background panel) and
/// Child_AutoResizeY (the height follows the content, one frame behind: the first frame shows one row; a positive size.y is the smallest
/// height). Accepted and ignored: Child_AlwaysUseWindowPadding (a child always has its padding), Child_ResizeX / Child_ResizeY,
/// Child_AutoResizeX (width does not follow content: use the size), Child_AlwaysAutoResize, Child_FrameStyle. `windowFlags` is ignored.
/// A size of 0 on an axis (not auto-resized) means the space left, as in the (w, h, border) form.
enum ChildFlags : int { Child_None = 0, Child_Borders = 1, Child_AlwaysUseWindowPadding = 2, Child_ResizeX = 4, Child_ResizeY = 8,
                        Child_AutoResizeX = 16, Child_AutoResizeY = 32, Child_AlwaysAutoResize = 64, Child_FrameStyle = 128 };
bool BeginChild(const char* id, Vec2 size, int childFlags = Child_None, int windowFlags = 0);


// ---- Step 6.11 (continued): the type names host code spells ---------------------------------------------------------------------

/// Dear ImGui's colour-vector type under its own name, for ported code that writes `ImVec4(r, g, b, a)` and reads `.x .y .z .w`. It
/// converts both ways with Vec4 (so it can be passed to PushStyleColor and assigned from GetStyleColorVec4).
struct ImVec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
    ImVec4() {}
    ImVec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    ImVec4(const Vec4& v) : x(v.x), y(v.y), z(v.z), w(v.w) {}
    operator Vec4() const { return Vec4{x, y, z, w}; }
};
/// A texture handle as ported code spells it: the SDL texture pointer AddImage takes.
typedef SDL_Texture* ImTextureID;
/// An id (what GetID returns).
typedef unsigned ImGuiID;
/// A rectangle by its two corners (Dear ImGui's internal type under its name; own code, the usual helpers).
struct ImRect {
    Vec2 Min, Max;
    ImRect() {}
    ImRect(Vec2 mn, Vec2 mx) : Min(mn), Max(mx) {}
    ImRect(float x1, float y1, float x2, float y2) : Min{x1, y1}, Max{x2, y2} {}
    Vec2 GetCenter() const { return Vec2{(Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f}; }
    Vec2 GetSize() const { return Vec2{Max.x - Min.x, Max.y - Min.y}; }
    float GetWidth() const { return Max.x - Min.x; }
    float GetHeight() const { return Max.y - Min.y; }
    bool Contains(Vec2 p) const { return p.x >= Min.x && p.y >= Min.y && p.x < Max.x && p.y < Max.y; }
    bool Overlaps(const ImRect& r) const { return r.Min.y < Max.y && r.Max.y > Min.y && r.Min.x < Max.x && r.Max.x > Min.x; }
    void Expand(float amount) { Min.x -= amount; Min.y -= amount; Max.x += amount; Max.y += amount; }
};
/// The last item's rectangle as an ImRect.
inline ImRect GetItemImRect() { const Rect r = GetItemRect(); return ImRect(r.x, r.y, r.x + r.w, r.y + r.h); }

} // namespace OkayUI
