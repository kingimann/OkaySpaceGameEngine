#pragma once
// Small calls that code written for Dear ImGui uses a lot and that OkayUI did not have: the main viewport, colour conversion, theme
// colours as floats, content-region extents, scroll-to-cursor, window size constraints, and a few "accepted, does nothing" calls so
// ported code compiles unchanged (the same approach as the ignored StyleVar values in OkayUI.hpp). Built only on OkayUI's public
// API: no SDL, no Dear ImGui. Chosen from the ranked list in docs/imgui_port_report.md.
//
// What each call does here, and where it differs from Dear ImGui, is written at the declaration.
#include "okay/UI/OkayUI.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace OkayUI {

/// The area OkayUI draws into: position (0, 0) and the display size the host gave BeginFrame (Input::displayW/H; zeros when the host
/// gave none). OkayUI has one viewport, so there is no per-window or multi-monitor variant.
struct Viewport {
    Vec2 pos, size;
    Vec2 Pos, Size, WorkPos, WorkSize;   // the same values under Dear ImGui's field names (filled by GetMainViewport; the work area is the whole display)
    Vec2 GetCenter() const { return Vec2{ pos.x + size.x * 0.5f, pos.y + size.y * 0.5f }; }
    Vec2 GetWorkPos() const { return pos; }
    Vec2 GetWorkSize() const { return size; }
    const Viewport* operator->() const { return this; }   // so `GetMainViewport()->GetCenter()` compiles as it does with Dear ImGui's pointer
    // What a host reads and writes on a viewport (docs/ui/own_ui_toolkit.md 6.22): the id, two handles the host keeps for itself (OkayUI never touches
    // them) and the frame's draw data (what GetDrawData() converts to). These three persist in the one main-viewport object that the pointer
    // conversion below hands out.
    unsigned ID = 0x4F4B5650u;                 // fixed ("OKVP"): OkayUI has one viewport
    void* PlatformHandle = nullptr; void* RendererUserData = nullptr; void* PlatformUserData = nullptr;
    ImDrawData* DrawData = nullptr;
    /// `ImGuiViewport* vp = GetMainViewport();`: a pointer to the one persistent main-viewport object, refreshed from the display size at each call.
    operator Viewport*() const;
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
struct WindowRef;   // OkayUIInternals.hpp
struct WindowInfo {
    bool found = false; Vec2 pos{0, 0}; Vec2 size{0, 0}; bool focused = false; bool docked = false;
    // Dear ImGui's field names for the same facts, and the pointer-like use ported code makes of `FindWindowByName(...)`: `w->Pos`, `ImGuiWindow* w = ...`
    // (the pointer is into a small ring of snapshots, valid until eight more windows were looked up).
    char Name[64] = {0}; Vec2 Pos{0, 0}, Size{0, 0}; unsigned ID = 0;
    const WindowInfo* operator->() const { return this; }
    operator WindowRef*() const;
};
WindowInfo FindWindowByName(const char* title);
/// IsWindowDocked for a window by title (false for an unknown one), or for the current window (no argument; false outside a window).
bool IsWindowDocked(const char* title);
bool IsWindowDocked();
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
/// PushStyleColor to change things). Approximations are listed in docs/ui/own_ui_toolkit.md section 6.7: FramePadding, ItemInnerSpacing
/// and CellPadding are the values the layout uses, not separate settings; Alpha is always 1.
struct StyleInfo {
    float Alpha = 1.0f;
    Vec2 WindowPadding, FramePadding, ItemSpacing, ItemInnerSpacing, CellPadding;
    float ScrollbarSize = 10.0f;
    float WindowRounding = 0.0f, ChildRounding = 0.0f, FrameRounding = 0.0f;
    float WindowBorderSize = 0.0f, ChildBorderSize = 0.0f, FrameBorderSize = 0.0f;
    float FontScale = 1.0f;                // the theme's text scale (2 = 16 px glyphs), not a multiplier of 1
    // Style fields OkayUI has no setting for: filled with fixed values so reads compile and give a sensible number (a write to them is not used).
    float PopupRounding = 4.0f, PopupBorderSize = 1.0f, GrabMinSize = 10.0f, GrabRounding = 3.0f, TabRounding = 4.0f, TabBorderSize = 0.0f, TabBarBorderSize = 1.0f;
    float TabBarOverlineSize = 0.0f, TabMinWidthBase = 1.0f, TabMinWidthShrink = 80.0f, TabCloseButtonMinWidthSelected = -1.0f, TabCloseButtonMinWidthUnselected = 0.0f;
    float SeparatorTextBorderSize = 1.0f, DockingSeparatorSize = 2.0f;
    Vec2 WindowTitleAlign{0.0f, 0.5f}, ButtonTextAlign{0.5f, 0.5f}, SelectableTextAlign{0.0f, 0.0f}, DisplayWindowPadding{19.0f, 19.0f}, DisplaySafeAreaPadding{3.0f, 3.0f};
    int WindowMenuButtonPosition = 0;
    float DisabledAlpha = 0.6f;            // accepted; disabled widgets blend halfway to the panel colour
    float ScrollbarRounding = 9.0f, IndentSpacing = 21.0f;
    Vec2 SeparatorTextAlign{0.0f, 0.5f}, SeparatorTextPadding{20.0f, 3.0f};
    bool DockingNodeHasCloseButton = true;
    void ScaleAllSizes(float) {}           // accepted; OkayUI scales through the text scale and the display scale
    ImVec4 Colors[Col_COUNT];              // index with the Col ids
};
/// One style object per context. A write to Colors, WindowRounding, FrameRounding, WindowPadding.x, the border sizes or FontScale is taken in at
/// the next GetStyle() call or frame start (as set colours and theme values); a write to the other fields is not used.
StyleInfo& GetStyle();
/// Dear ImGui's name for the style type, so `const ImGuiStyle& st = GetStyle();` compiles (read-only, see above).
typedef StyleInfo ImGuiStyle;

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
enum KeyMods : int { Mod_None = 0, Mod_Ctrl = 1, Mod_Shift = 2, Mod_Alt = 4, Mod_Super = 8 };
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
/// The current window (nothing outside a window).
void SetWindowSize(Vec2 size, Cond cond = Cond_Always);

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
/// The topmost open modal popup as a window entry (Name empty, ID the popup's id), or null: usable as a yes / no answer and as `ImGuiWindow*`.
WindowRef* GetTopMostPopupModal();
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
struct Context;   // defined in OkayUIInternals.hpp
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
const char* SaveIniSettingsToMemory(std::size_t* size = nullptr);
void LoadIniSettingsFromMemory(const char* data, std::size_t size = 0);


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
/// Some popup (of any kind) is open (implemented next to the popup slots in OkayUI.cpp).
bool AnyPopupOpen();
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
/// By numeric id (a GetID value): the same child as the one named "##<id>".
bool BeginChild(unsigned id, Vec2 size, int childFlags = Child_None, int windowFlags = 0);
/// Dear ImGui's ImageButton: a size vector, uv corners, a background and a tint (uv, background and tint are ignored; the picture fills the button).
bool ImageButton(const char* id, SDL_Texture* tex, Vec2 size, Vec2 uv0 = Vec2{0, 0}, Vec2 uv1 = Vec2{1, 1}, Vec4 bg = Vec4{}, Vec4 tint = Vec4{1, 1, 1, 1});
/// PushClipRect with corners (min, max) and the intersect flag (always intersects).
void PushClipRect(Vec2 min, Vec2 max, bool intersectWithCurrent);
/// Finish the frame's drawing calls (Dear ImGui's Render): EndFrameData() without an output size. GetDrawData() then converts to an ImDrawData.
void Render();


// ---- Step 6.11 (continued): the type names host code spells ---------------------------------------------------------------------

/// A packed colour from a float colour (what GetColorU32(ImVec4) does in Dear ImGui); components are clamped to 0..1.
Color32 GetColorU32(Vec4 color);
/// A texture handle as ported code spells it: the SDL texture pointer AddImage takes.

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
    void ClipWith(const ImRect& r) { Min.x = Min.x > r.Min.x ? Min.x : r.Min.x; Min.y = Min.y > r.Min.y ? Min.y : r.Min.y; Max.x = Max.x < r.Max.x ? Max.x : r.Max.x; Max.y = Max.y < r.Max.y ? Max.y : r.Max.y; }
    void Expand(float amount) { Min.x -= amount; Min.y -= amount; Max.x += amount; Max.y += amount; }
};
/// The last item's rectangle as an ImRect.
inline ImRect GetItemImRect() { const Rect r = GetItemRect(); return ImRect(r.x, r.y, r.x + r.w, r.y + r.h); }


// ---- printf-style text (the forms ported code calls: Text("%d items", n)) ----------------------------------------------------------
/// Format into a 1,024-byte buffer (longer output is cut) and show it with the plain call of the same name. With one argument the plain
/// call is chosen, so a literal "100%" stays literal. TextColored takes the colour as a Vec4 (a Dear ImGui ImVec4 converts to it).
void TextFormatted(const char* fmt, ...);
void TextDisabledFormatted(const char* fmt, ...);
void TextWrappedFormatted(const char* fmt, ...);
void BulletTextFormatted(const char* fmt, ...);
void TextColoredFormatted(Vec4 color, const char* fmt, ...);
// Templates (they need at least one argument after the format, so a call with only a string still picks the plain one).
template <class A0, class... A> inline void Text(const char* fmt, A0 a0, A... a) { TextFormatted(fmt, a0, a...); }
template <class A0, class... A> inline void TextDisabled(const char* fmt, A0 a0, A... a) { TextDisabledFormatted(fmt, a0, a...); }
template <class A0, class... A> inline void TextWrapped(const char* fmt, A0 a0, A... a) { TextWrappedFormatted(fmt, a0, a...); }
template <class A0, class... A> inline void BulletText(const char* fmt, A0 a0, A... a) { BulletTextFormatted(fmt, a0, a...); }
template <class... A> inline void TextColored(Vec4 color, const char* fmt, A... a) { TextColoredFormatted(color, fmt, a...); }


// ---- Step 6.13: the argument shapes and flag families the panel probes found --------------------------------------------------------

/// Slider and drag flags. Slider_AlwaysClamp: accepted (a slider here always clamps); Slider_Logarithmic: honoured by SliderFloat when the range
/// keeps one sign and excludes 0 (otherwise, and for drag fields and vectors, the scale stays linear: known gap); Slider_NoInput and Slider_NoRoundToFormat
/// are honoured. The `fmt` text is honoured by Drag*, SliderFloat/Int and InputFloat (see OkayUI::FormatNumber); a refused format shows the default; SliderFloat2/3/4 ignore it.
enum SliderFlags : int { Slider_None = 0, Slider_AlwaysClamp = 16, Slider_Logarithmic = 32, Slider_NoRoundToFormat = 64, Slider_NoInput = 128 };
bool SliderFloat(const char* label, float* value, float minV, float maxV, const char* fmt, int flags = 0);
bool SliderInt(const char* label, int* value, int minV, int maxV, const char* fmt, int flags = 0);
bool DragFloat(const char* label, float* v, float speed, float minV, float maxV, const char* fmt, int flags = 0);
bool DragInt(const char* label, int* v, float speed, int minV, int maxV, const char* fmt, int flags = 0);
/// Combo flags for BeginCombo(label, preview, flags): accepted, no effect (the list height is fixed; the preview always shows).
enum ComboFlags : int { Combo_None = 0, Combo_PopupAlignLeft = 1, Combo_HeightSmall = 2, Combo_HeightRegular = 4, Combo_HeightLarge = 8, Combo_HeightLargest = 16,
                        Combo_NoArrowButton = 32, Combo_NoPreview = 64 };
bool BeginCombo(const char* label, const char* preview, int flags);
/// Dear ImGui's argument order: label, the int to edit, the items, the count.
bool Combo(const char* label, int* current, const char* const* items, int count);
/// Popup flags for OpenPopup(id, flags), BeginPopupContextItem(id, flags) and BeginPopupContextWindow(id, flags). Right mouse button is the
/// only trigger OkayUI has, so Popup_MouseButtonRight and the default behave the same; MouseButtonLeft / Middle open nothing (the call
/// returns false for them). AnyPopupId / AnyPopupLevel are accepted for IsPopupOpen-style queries and change nothing.
enum PopupFlags : int { Popup_None = 0, Popup_MouseButtonLeft = 4, Popup_MouseButtonRight = 1, Popup_MouseButtonMiddle = 2,
                        Popup_NoOpenOverExistingPopup = 32, Popup_AnyPopupId = 128, Popup_AnyPopupLevel = 256 };
void OpenPopup(const char* id, int flags);
bool BeginPopupContextItem(const char* id, int flags);
bool BeginPopupContextWindow(const char* id, int flags);
/// Configuration flags a host sets on the IO (ConfigFlags_NavEnableKeyboard, ViewportsEnable, DockingEnable, ...): the names exist so ported
/// start-up code compiles; OkayUI has one viewport, one dock space and no keyboard navigation, so nothing reads them.
enum ConfigFlags : int { ConfigFlags_None = 0, ConfigFlags_NavEnableKeyboard = 1, ConfigFlags_NavEnableGamepad = 2, ConfigFlags_NoMouse = 16,
                         ConfigFlags_NoMouseCursorChange = 32, ConfigFlags_DockingEnable = 64, ConfigFlags_ViewportsEnable = 1024 };
/// Auto-layout image from Dear ImGui's argument list (size as a vector; texture coordinates, tint and border as given).
void Image(ImTextureID tex, Vec2 size, Vec2 uv0 = Vec2{0.0f, 0.0f}, Vec2 uv1 = Vec2{1.0f, 1.0f}, Vec4 tint = Vec4{1.0f, 1.0f, 1.0f, 1.0f}, Vec4 border = Vec4{});
/// BeginMenu with Dear ImGui's `enabled` argument: a disabled menu is drawn dimmed, does not open and returns false.
bool BeginMenu(const char* label, bool enabled);
/// BeginTable with the flags as a plain int (what `ImGuiTableFlags_A | ImGuiTableFlags_B` is), with and without an outer size.
bool BeginTable(const char* id, int columns, int flags);
bool BeginTable(const char* id, int columns, int flags, Vec2 outerSize, float innerWidth = 0.0f);
/// PushID with a pointer (the object's address becomes the id seed). A template so every object pointer works; `const char*` still goes to the string form.
template <class T> inline void PushID(const T* ptr) { PushID(static_cast<int>(reinterpret_cast<std::uintptr_t>(ptr) * 2654435761u >> 1)); }
template <class T> inline void PushID(T* ptr) { PushID(static_cast<const T*>(ptr)); }
/// IsMouseHoveringRect with Dear ImGui's `clip` argument (accepted; the rectangle is never clipped to the window here).
bool IsMouseHoveringRect(Vec2 min, Vec2 max, bool clip);
/// Step 6.14: the host-level calls that make sense without a platform layer. OkayUI has one viewport and no platform windows.
/// NewFrame: BeginFrame with the input of the last frame (what GetFrameInput returns; zeros before the first), for code that starts frames the
/// Dear ImGui way. A host that has this frame's input calls BeginFrame(input) itself instead.
void NewFrame();
/// UpdatePlatformWindows / RenderPlatformWindowsDefault: nothing to do (no extra windows); accepted so a host loop compiles.
void UpdatePlatformWindows();
void RenderPlatformWindowsDefault();
/// What `GetPlatformIO()` shows: one viewport (`Viewports.Size` is 1) and the renderer callbacks of a multi-viewport host as plain fields that
/// are stored and never called (OkayUI has no secondary windows to call them for).
struct PlatformViewports { int Size = 1; int size() const { return Size; } };
struct PlatformIO {
    PlatformViewports Viewports;
    void (*Renderer_CreateWindow)(Viewport*) = nullptr; void (*Renderer_DestroyWindow)(Viewport*) = nullptr;
    void (*Renderer_RenderWindow)(Viewport*, void*) = nullptr; void (*Renderer_SwapBuffers)(Viewport*, void*) = nullptr;
    void (*Platform_CreateWindow)(Viewport*) = nullptr; void (*Platform_DestroyWindow)(Viewport*) = nullptr;
    // What a platform feed (shared/ImGuiPlatformFeed.hpp) installs: the clipboard and the input-method position. OkayUI's own clipboard (GetClipboardText /
    // SetClipboardText) calls them when set.
    void* Platform_ClipboardUserData = nullptr;
    const char* (*Platform_GetClipboardTextFn)(Context*) = nullptr; void (*Platform_SetClipboardTextFn)(Context*, const char*) = nullptr;
    void* Platform_ImeUserData = nullptr;
    void (*Platform_SetImeDataFn)(Context*, Viewport*, struct PlatformImeData*) = nullptr;
};
inline void SetMouseCursor(int c) { SetMouseCursor((MouseCursor)c); }   // a platform feed passes the cursor as a plain number
struct PlatformImeData { bool WantVisible = false, WantTextInput = false; Vec2 InputPos{0, 0}; float InputLineHeight = 0.0f; };
struct WindowRef;   // OkayUIInternals.hpp
PlatformIO& GetPlatformIO();
/// io.Fonts: Dear ImGui hosts ask the atlas for fonts at start-up. OkayUI loads none here: each Add* call is RECORDED (what, how big, where from) in a
/// list the host reads with FontRequests(), and returns a stable ImFont* that works with PushFont / PopFont (it is a copy of the default font; the
/// size asked for is kept in `FontRequest::px`). A host that wants those faces builds them itself (the launcher look's atlas, UseLauncherLook) and
/// maps the returned pointers. Compressed base-85 TTF data is not decoded.
struct FontRequest { enum Kind { Default, FromMemoryTTF, FromCompressedBase85, FromFile } kind = Default; float px = 0.0f; const void* data = nullptr; int dataSize = 0; char file[260] = {0}; bool merge = false; ImFont* font = nullptr; };
/// ---- Real fonts (a multi-font store) ----
/// Register a TrueType font from bytes (copied) at a pixel size: the returned Font works with SetFont, PushFont / PopFont and DrawList::AddText(font, size, ...),
/// and text drawn or measured while it is active uses it (own rasteriser, okay::TtfFont; a glyph the font lacks is skipped). The same bytes registered
/// again at another size share one font (one glyph atlas). Up to 5 different font files and 32 sizes in all; null past that or for bytes that are no font.
/// Draw data: the glyph atlas is a fallback-font texture (FallbackFontOf(texture)), uploaded by the backend like the launcher look's fallback fonts.
Font* AddFontTtf(const unsigned char* data, std::size_t size, float px, const char* name = nullptr);
Font* AddFontTtfFile(const char* path, float px);
/// Dear ImGui hosts load the editor's two faces from base-85 compressed arrays. OkayUI has no decoder for that format (the engine dropped it on purpose;
/// the same fonts ship as our own OKF1 data, decoded by okay::EmbeddedUIFontData). A host maps each array to the decoded TTF bytes once, and
/// AddFontFromMemoryCompressedBase85TTF(array, px) then registers that font. An array that was not mapped gives the default font (and is recorded).
void RegisterBase85FontSource(const char* array, const unsigned char* ttf, std::size_t size);
struct FontAtlasRef {
    ImFont* AddFontDefault(const ImFontConfig* cfg = nullptr);
    ImFont* AddFontFromMemoryTTF(void* data, int size, float px, const ImFontConfig* cfg = nullptr, const unsigned short* ranges = nullptr);
    ImFont* AddFontFromMemoryCompressedBase85TTF(const char* data, float px, const ImFontConfig* cfg = nullptr, const unsigned short* ranges = nullptr);
    ImFont* AddFontFromFileTTF(const char* file, float px, const ImFontConfig* cfg = nullptr, const unsigned short* ranges = nullptr);
    bool Build() { return true; }
    void Clear();
    int FontCount() const;
};
FontAtlasRef* GetFontAtlasRef();
typedef FontAtlasRef ImFontAtlas;
/// The requests made so far, oldest first (up to 32; later ones are still answered with a font but not listed).
const FontRequest* FontRequests(int* count);
typedef PlatformIO ImGuiPlatformIO;
/// Popups by id (the value GetID(name) returns; a popup opened or begun by name has the same id, so the forms can be mixed). BeginPopupEx takes
/// window flags (accepted: a popup here is always auto-sized and has no title bar) and is true while the popup is open.
void OpenPopup(unsigned id, int flags = 0);
bool BeginPopupEx(unsigned id, int flags);
/// Close the open popups from stack depth `level` outward (0 closes all of them). `restoreFocus` is accepted and not used.
void ClosePopupToLevel(int level, bool restoreFocus);
/// DockContextQueueUndockWindow(ctx, window): make the window a floating one at its current place (OkayUIDock.hpp's layout). The context
/// argument is accepted and unused. False when the window is not docked or is unknown.
bool DockContextQueueUndockWindow(Context* ctx, const WindowRef* window);
/// Axis (ImGuiAxis_X / Y), sort direction and the popup / style names the probe found missing: names only.
enum Axis : int { Axis_None = -1, Axis_X = 0, Axis_Y = 1 };
enum SortDirection : int { SortDirection_None = 0, SortDirection_Ascending = 1, SortDirection_Descending = 2 };
enum PopupExtraFlags : int { Popup_NoOpenOverItems = 512 };   // accepted, no effect
/// A drop target over an ImRect (the Dear ImGui form takes the rectangle type of its own).
bool BeginDragDropTargetCustom(const ImRect& bb, unsigned id = 0);
/// InputInt with the step arguments of Dear ImGui (`stepFast` and `flags` are accepted and not used; a `step` of 0 shows no +/- buttons, as in Dear ImGui).
bool InputInt(const char* label, int* value, int step, int stepFast, int flags = 0);
void TreeNodeExFormatted(bool* out, const char* label, unsigned flags, const char* fmt, ...);
/// TreeNode with an id and a printf-style label: `TreeNode("##id", "Incoming (%d)", n)`.
template <class A0, class... A> inline bool TreeNode(const char* id, const char* fmt, A0 a0, A... a) { bool r = false; TreeNodeExFormatted(&r, id, 0u, fmt, a0, a...); return r; }
// ---- Step 6.18: argument shapes found by probing editor/main.cpp (the Hierarchy panel and the rest of the editor) ----------------------
/// `fmt` (a printf format for the shown number) is honoured by DragFloat2/3/4 and DragFloatRange2 (`fmtMax` for the second cell); `flags` NoInput and NoRoundToFormat too. SliderFloat2/3/4 ignore `fmt`.
bool DragFloat2(const char* label, float v[2], float speed, float minV, float maxV, const char* fmt, int flags = 0);
bool DragFloat3(const char* label, float v[3], float speed, float minV, float maxV, const char* fmt, int flags = 0);
bool DragFloat4(const char* label, float v[4], float speed, float minV, float maxV, const char* fmt, int flags = 0);
bool DragFloatRange2(const char* label, float* lo, float* hi, float speed, float minV, float maxV, const char* fmt, const char* fmtMax = nullptr, int flags = 0);
bool SliderFloat2(const char* label, float v[2], float minV, float maxV, const char* fmt, int flags = 0);
bool SliderFloat3(const char* label, float v[3], float minV, float maxV, const char* fmt, int flags = 0);
bool SliderFloat4(const char* label, float v[4], float minV, float maxV, const char* fmt, int flags = 0);
bool InputFloat(const char* label, float* value, float step, float stepFast, const char* fmt = "%.3f", int flags = 0);
/// SetNextItemOpen with a condition (Always, Once, FirstUseEver; Appearing counts as Always here).
void SetNextItemOpen(bool open, Cond cond);
/// IsPopupOpen with popup flags: with Popup_AnyPopupId (and an empty name) it answers "is any popup open"; with a name it is the plain form.
bool IsPopupOpen(const char* name, int flags);
/// BeginPopup with window flags (accepted: a popup here has no title bar and fits its content); SetKeyboardFocusHere(offset) (only 0, the next item, is supported).
bool BeginPopup(const char* id, int windowFlags);
void SetKeyboardFocusHere(int offset);
/// Dear ImGui's RadioButton(label, active): a radio that is shown filled when `active`; true on the click.
bool RadioButton(const char* label, bool active);
/// ColorButton with a vector colour (and, in the long form, flags and a size as a vector).
bool ColorButton(const char* id, const ImVec4& col, int flags = 0, Vec2 size = Vec2{0.0f, 0.0f});
/// PlotLines / PlotHistogram with Dear ImGui's argument list (values, count, offset, overlay, scaleMin, scaleMax, size; stride is not offered).
void PlotLines(const char* label, const float* values, int count, int offset, const char* overlay, float scaleMin, float scaleMax, Vec2 size);
void PlotHistogram(const char* label, const float* values, int count, int offset, const char* overlay, float scaleMin, float scaleMax, Vec2 size);
/// TreeNodeEx / TreeNode keyed by a pointer: `TreeNodeEx(obj, flags, "%s", name)` (the pointer is the id, the formatted text the label).
template <class T, class... A> inline bool TreeNodeEx(const T* ptrId, unsigned flags, const char* fmt, A... a) {
    char id[32]; std::snprintf(id, sizeof id, "p%llx", (unsigned long long)reinterpret_cast<std::uintptr_t>(ptrId));
    bool r = false; TreeNodeExFormatted(&r, id, flags, fmt, a...); return r;
}
template <class T, class... A> inline bool TreeNodeEx(T* ptrId, unsigned flags, const char* fmt, A... a) { return TreeNodeEx(static_cast<const T*>(ptrId), flags, fmt, a...); }
/// PushFont / PopFont: the font used for text until the matching PopFont (nests up to 8 deep; a null font means the current one). Text size is the
/// theme's text scale, not the font's pixel size, so only the typeface changes.
void PushFont(Font* font);
void PushFont(Font* font, float sizePixels);   // the size is accepted and not used (see above)
void PopFont();
/// What a text callback is handed. The InputText overloads below call it for InputText_CallbackHistory (Up / Down) and
/// InputText_CallbackCompletion (Tab) while the field has the keyboard; other callback flags never call it.
struct InputTextCallbackData {
    int EventFlag = 0, Flags = 0, EventChar = 0, EventKey = 0; void* UserData = nullptr; char* Buf = nullptr; int BufTextLen = 0, BufSize = 0; bool BufDirty = false;
    int CursorPos = 0, SelectionStart = 0, SelectionEnd = 0;
    void DeleteChars(int pos, int bytes) { if (!Buf || pos < 0 || bytes <= 0 || pos >= BufTextLen) return; if (pos + bytes > BufTextLen) bytes = BufTextLen - pos; for (int i = pos; i + bytes <= BufTextLen; ++i) Buf[i] = Buf[i + bytes]; BufTextLen -= bytes; BufDirty = true; }
    void InsertChars(int pos, const char* text, const char* end = nullptr);
    bool HasSelection() const { return SelectionStart != SelectionEnd; }
};
typedef int (*InputTextCallback)(InputTextCallbackData* data);
bool InputTextWithHint(const char* label, const char* hint, char* buf, int cap, unsigned flags, InputTextCallback callback, void* userData = nullptr);
bool InputText(const char* label, char* buf, int cap, unsigned flags, InputTextCallback callback, void* userData = nullptr);
/// TreeNodeEx with a printf-style label (the label is formatted, then used as the id as well).
void TreeNodeExFormatted(bool* out, const char* label, unsigned flags, const char* fmt, ...);
template <class... A> inline bool TreeNodeEx(const char* id, unsigned flags, const char* fmt, A... a) { bool r = false; TreeNodeExFormatted(&r, id, flags, fmt, a...); return r; }


// ---- Step 6.13 (continued): flag names. Each says whether it does anything. ----------------------------------------------------------

/// Window flags beyond OkayUI's own. Window_NoDecoration is real (no title bar, no resize, no scrollbar, no collapse); NoBringToFrontOnFocus is
/// Window_NoBringToFront. The rest are accepted and change nothing: no saved settings file, no navigation, no docking opt-out (the dock
/// space decides), no menu bar flag (use BeginMenuBar), no wheel opt-out, no horizontal scrollbar.
enum WindowFlagNames : int {
    Window_NoDecoration = Window_NoTitleBar | Window_NoResize | Window_NoScrollbar | Window_NoCollapse,
    Window_NoBringToFrontOnFocus = Window_NoBringToFront,
    Window_NoSavedSettings = 1 << 9, Window_NoScrollWithMouse = 1 << 10, Window_NoNav = 1 << 11, Window_NoNavInputs = 1 << 12, Window_NoNavFocus = 1 << 13,
    Window_NoDocking = 1 << 14, Window_NoInputs = 1 << 15, Window_MenuBar = 1 << 16, Window_HorizontalScrollbar = 1 << 17,
    Window_AlwaysVerticalScrollbar = 1 << 18, Window_Modal = 1 << 19, Window_ChildMenu = 1 << 20
};
/// Table flags beyond OkayUI's own, accepted and without effect except that Table_Resizable is the default (0), Table_Borders is Table_BordersInnerH and
/// Table_BordersOuter draws a one-pixel frame in Col_TableBorderStrong around the table (or its scrolling box): column sizing policies, vertical
/// borders, outer padding, the saved-settings opt-out and column reordering (there is none yet).
enum TableFlagNames : int {
    Table_Resizable = 0, Table_Borders = Table_BordersInnerH,
    Table_BordersInnerV = 1 << 9, Table_BordersOuter = 1 << 10, Table_SizingFixedFit = 1 << 11, Table_SizingStretchProp = 1 << 12, Table_SizingStretchSame = 1 << 13,
    Table_PadOuterX = 1 << 14, Table_NoSavedSettings = 1 << 15, Table_Reorderable = 1 << 16
};
enum TableColumnExtraFlags : int { TableColumn_NoResize = 64, TableColumn_NoHide = 128 };   // accepted, no effect
/// Selectable flags: Selectable_DontClosePopups is the default here (0); AllowDoubleClick and AllowOverlap are accepted and change nothing.
enum SelectableFlagNames : int { Selectable_DontClosePopups = 0, Selectable_AllowDoubleClick = 16, Selectable_AllowOverlap = 32 };
/// Tree node, colour edit, drag and drop, button and tab bar flags: accepted, no effect, except ColorEdit_DisplayHex (= ColorEdit_Hex), ColorEdit_AlphaPreview
/// (0, the default) and ColorEdit_Float (accepted; the sliders show 0..1 anyway).
enum TreeNodeFlagNames : int { TreeNode_AllowOverlap = 1 << 12 };
enum ColorEditFlagNames : int { ColorEdit_DisplayHex = ColorEdit_Hex, ColorEdit_AlphaPreview = 0, ColorEdit_Float = 1 << 12 };
enum DragDropFlagNames : int { DragDrop_AcceptNoDrawDefaultRect = 1 << 8, DragDrop_SourceNoPreviewTooltip = 1 << 9, DragDrop_SourceAllowNullID = 1 << 10 };
/// Button flags for InvisibleButton(id, size, flags): the mouse buttons are accepted; the button reports left clicks (use IsItemClicked(1) for right).
enum ButtonFlags : int { Button_None = 0, Button_MouseButtonLeft = 1, Button_MouseButtonRight = 2, Button_MouseButtonMiddle = 4 };
bool InvisibleButton(const char* id, Vec2 size, int flags = 0);
enum TabBarFlags : int { TabBar_None = 0, TabBar_Reorderable = 1, TabBar_FittingPolicyResizeDown = 2, TabBar_FittingPolicyScroll = 4, TabBar_IsFocused = 8,
                         TabBar_AutoSelectNewTabs = 16, TabBar_NoCloseWithMiddleMouseButton = 32 };
bool BeginTabBar(const char* id, int flags);
/// Where a dragged tab lands in a reorderable bar (TabBar_Reorderable): the tabs' widths in their order, the dragged one's index, the bar's left edge,
/// the gap between tabs and the pointer's x. Returns the index the dragged tab moves to (the same index when it stays). Pure, so it is tested alone.
int TabDragTarget(const float* widths, int n, int from, float left, float gap, float mouseX);
enum TabItemExtraFlags : int { TabItem_UnsavedDocument = 8, TabItem_Button = 16, TabItem_NoCloseWithMiddleMouseButton = 32, TabItem_NoReorder = 64, TabItem_Leading = 128 };
/// Input flags (Shortcut and key queries): Input_Repeat is Shortcut_AllowRepeat.
enum InputFlags : int { Input_None = 0, Input_Repeat = Shortcut_AllowRepeat };
/// Backend flags a host reports on the IO: accepted, nothing reads them.
enum BackendFlags : int { BackendFlags_None = 0, BackendFlags_HasMouseCursors = 1, BackendFlags_HasSetMousePos = 4, BackendFlags_PlatformHasViewports = 1 << 10, BackendFlags_RendererHasViewports = 1 << 11 };
/// Item flag names under Dear ImGui's spelling (the ItemFlag_* values are the same).
enum ItemFlagNames : int { Item_None = 0, Item_NoTabStop = ItemFlag_NoTabStop, Item_ButtonRepeat = ItemFlag_ButtonRepeat, Item_Disabled = ItemFlag_Disabled,
                           Item_NoNav = ItemFlag_NoNav, Item_NoNavDefaultFocus = ItemFlag_NoNavDefaultFocus, Item_MixedValue = ItemFlag_MixedValue };
/// Colour slot names of newer Dear ImGui versions, mapped onto the closest OkayUI slot.
enum ColNames : int { Col_TabDimmed = Col_TabUnfocused, Col_TabDimmedSelected = Col_TabUnfocusedActive, Col_TabDimmedSelectedOverline = Col_TabUnfocusedActive,
                      Col_TabSelectedOverline = Col_TabActive, Col_TextLink = Col_Accent, Col_NavCursor = Col_NavHighlight, Col_InputTextCursor = Col_Text };
enum DirNone : int { Dir_None = -1 };
typedef unsigned short ImWchar;
/// CalcTextSize with Dear ImGui's extra arguments: `textEnd` (null = to the zero), `hideTextAfterDoubleHash` (measure only the text before "##"), `wrapWidth` (not used).
Vec2 CalcTextSize(const char* text, const char* textEnd, bool hideTextAfterDoubleHash = false, float wrapWidth = -1.0f);
/// Combo from one string of items separated by a zero byte and ended by two ("A\0B\0C\0"), as Dear ImGui takes it; up to 64 items.
bool Combo(const char* label, int* current, const char* itemsSeparatedByZeros, int popupMaxHeightInItems = -1);
/// A colour as ported code spells it: floats inside, converts to the vector and to the packed form.
struct ImColor {
    ImVec4 Value;
    ImColor() {}
    ImColor(float r, float g, float b, float a = 1.0f) : Value(r, g, b, a) {}
    ImColor(int r, int g, int b, int a = 255) : Value(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f) {}
    ImColor(const ImVec4& v) : Value(v) {}
    ImColor(unsigned packed) : Value(ColorConvertU32ToFloat4(packed)) {}   // from a packed 0xAABBGGRR colour
    operator ImVec4() const { return Value; }
    operator Vec4() const { return Value; }
    operator Color32() const { return ColorConvertFloat4ToU32(Value); }
};

} // namespace OkayUI
