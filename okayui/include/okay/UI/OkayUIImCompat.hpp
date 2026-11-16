#pragma once
// The names code written for an immediate-mode toolkit with ImGui-shaped calls expects, so a panel can be ported with its structure
// unchanged: the flag and enum types (mouse buttons, hover and focus flags, child flags, draw flags, directions, key names, popup
// and button flags), and the calls that take them. Own code, written from how these calls are used in our own editor and from general
// knowledge of how such toolkits behave; nothing here is copied or ported from Dear ImGui (see docs/own_ui_toolkit.md section 4.36).
//
// Every enum says at its declaration which flags OkayUI really honours and which are accepted so the call compiles (a flag that
// names something OkayUI does not have, like a second viewport, is accepted and does nothing). The calls say where they differ.
// Numbers are OkayUI's own: nothing may depend on the values, only on the names.
//
// Off by default like the rest of OkayUI: nothing in the editor, launcher or player calls this unless the opt-in backend is on.
#include "okay/UI/OkayUI.hpp"

namespace OkayUI {

// ---- Types ------------------------------------------------------------------------------------------------------------
/// A colour of four floats 0..1. OkayUI::Vec4 (OkayUI.hpp) is the type; this is only here so the report finds it by its usual name.
/// An id of an item or window (GetID, GetItemID, GetActiveID).
using ID = unsigned;
/// A texture handle for Image and the draw list. In OkayUI it is the SDL texture pointer the backend gave out.
using TextureID = SDL_Texture*;
/// Named keys (KeyId in OkayUI.hpp) under the usual spellings. Honoured: IsKeyDown, IsKeyPressed, IsKeyReleased.
using Key = KeyId;
constexpr KeyId Key_None = KeyId_None, Key_Tab = KeyId_Tab, Key_LeftArrow = KeyId_Left, Key_RightArrow = KeyId_Right,
                Key_UpArrow = KeyId_Up, Key_DownArrow = KeyId_Down, Key_PageUp = KeyId_PageUp, Key_PageDown = KeyId_PageDown,
                Key_Home = KeyId_Home, Key_End = KeyId_End, Key_Insert = KeyId_Insert, Key_Delete = KeyId_Delete,
                Key_Backspace = KeyId_Backspace, Key_Space = KeyId_Space, Key_Enter = KeyId_Enter, Key_KeypadEnter = KeyId_Enter,
                Key_Escape = KeyId_Escape, Key_LeftCtrl = KeyId_Ctrl, Key_LeftShift = KeyId_Shift, Key_LeftAlt = KeyId_Alt,
                Key_A = KeyId_A, Key_C = KeyId_C, Key_V = KeyId_V, Key_X = KeyId_X, Key_Y = KeyId_Y, Key_Z = KeyId_Z;

/// Mouse buttons for IsMouseDown / IsMouseClicked / IsMouseReleased / IsMouseDragging / GetMouseDragDelta / IsItemClicked. All three
/// work (Input::middleDown is the middle button; no host in the editor or launcher forwards it yet, so today it is the tests that press it).
/// Double click and wheel are left-button / wheel only.
enum MouseButton : int { MouseButton_Left = 0, MouseButton_Right = 1, MouseButton_Middle = 2, MouseButton_COUNT = 3 };

/// A direction (arrows, split sides). ArrowButton takes these as ints (0 left, 1 right, 2 up, 3 down) as before.
enum Dir : int { Dir_None = -1, Dir_Left = 0, Dir_Right = 1, Dir_Up = 2, Dir_Down = 3 };

// ---- Hover and focus flags ----------------------------------------------------------------------------------------------
/// For IsItemHovered(flags) and IsWindowHovered(flags).
/// Honoured: AllowWhenDisabled (hover inside BeginDisabled), AllowWhenBlockedByPopup (hover under an open popup),
/// AllowWhenBlockedByActiveItem (hover while another item is held), AllowWhenOverlapped (hover under a window above),
/// RectOnly (all four: only the rectangle counts), ChildWindows / RootWindow / RootAndChildWindows (a child region is part of its
/// window here, so these are the same as none), AnyWindow (IsWindowHovered: the pointer is over any window), Stationary (the pointer has
/// not moved for 0.15 s), DelayShort (0.15 s of hovering) and DelayNormal (0.4 s), NoSharedDelay (accepted: delays are per item here).
/// ForTooltip means Stationary plus DelayNormal.
enum HoveredFlags : int {
    HoveredFlags_None = 0,
    HoveredFlags_ChildWindows = 1,
    HoveredFlags_RootWindow = 2,
    HoveredFlags_AnyWindow = 4,
    HoveredFlags_RootAndChildWindows = HoveredFlags_RootWindow | HoveredFlags_ChildWindows,
    HoveredFlags_AllowWhenBlockedByPopup = 8,
    HoveredFlags_AllowWhenBlockedByActiveItem = 16,
    HoveredFlags_AllowWhenOverlapped = 32,
    HoveredFlags_AllowWhenDisabled = 64,
    HoveredFlags_RectOnly = HoveredFlags_AllowWhenBlockedByPopup | HoveredFlags_AllowWhenBlockedByActiveItem | HoveredFlags_AllowWhenOverlapped,
    HoveredFlags_NoSharedDelay = 128,
    HoveredFlags_Stationary = 256,
    HoveredFlags_DelayShort = 512,
    HoveredFlags_DelayNormal = 1024,
    HoveredFlags_ForTooltip = HoveredFlags_Stationary | HoveredFlags_DelayNormal,
};
/// For IsWindowFocused(flags). ChildWindows / RootWindow / RootAndChildWindows: same as none (a child region is part of its window);
/// AnyWindow: some window has the focus.
enum FocusedFlags : int {
    FocusedFlags_None = 0, FocusedFlags_ChildWindows = 1, FocusedFlags_RootWindow = 2, FocusedFlags_AnyWindow = 4,
    FocusedFlags_NoPopupHierarchy = 8,   // accepted
    FocusedFlags_RootAndChildWindows = FocusedFlags_RootWindow | FocusedFlags_ChildWindows,
};

/// IsItemHovered with flags (the plain IsItemHovered() in OkayUI.hpp keeps its meaning). Like most toolkits of this kind it is false
/// while another item is being held (a drag) unless HoveredFlags_AllowWhenBlockedByActiveItem is given.
bool IsItemHovered(int flags);
/// IsWindowHovered with flags (see HoveredFlags). With no flags it is false while an item is held, and under an open popup.
bool IsWindowHovered(int flags);
bool IsWindowFocused(int flags);
/// Some item was under the pointer in this frame so far (widgets drawn before the call).
bool IsAnyItemHovered();

// ---- Mouse ------------------------------------------------------------------------------------------------------------
/// How far the pointer has moved since `button` went down, while it is held and has moved at least `lockThreshold` pixels (default 4);
/// (0, 0) otherwise. ResetMouseDragDelta restarts the measurement from the pointer's position now.
Vec2 GetMouseDragDelta(int button = 0, float lockThreshold = -1.0f);
void ResetMouseDragDelta(int button = 0);

// ---- Child windows ----------------------------------------------------------------------------------------------------
/// For BeginChild(id, size, childFlags, windowFlags). Honoured: Borders (the filled, bordered panel; without it the child has no
/// background), AutoResizeY (the height is last frame's content height plus the padding; the first frame is one row), FrameStyle
/// (same as Borders). Accepted: AlwaysUseWindowPadding (OkayUI children always have padding), ResizeX, ResizeY, AutoResizeX,
/// AlwaysAutoResize, NavFlattened. `windowFlags` are accepted and ignored (a child is not a window here).
enum ChildFlags : int {
    ChildFlags_None = 0, ChildFlags_Borders = 1, ChildFlags_AlwaysUseWindowPadding = 2, ChildFlags_ResizeX = 4, ChildFlags_ResizeY = 8,
    ChildFlags_AutoResizeX = 16, ChildFlags_AutoResizeY = 32, ChildFlags_AlwaysAutoResize = 64, ChildFlags_FrameStyle = 128,
    ChildFlags_NavFlattened = 256,
};
/// A child region of `size` (Dear ImGui's sizes: 0 in an axis fills what is left, a negative value leaves that many pixels free). Pair with
/// EndChild. ChildFlags_Borders (or ChildFlags_FrameStyle) draws Dear ImGui's frame, a one-pixel rounded outline in Col_Border filled with
/// Col_ChildBg when that is set, and pads the content (6 pixels, or a pushed StyleVar_WindowPadding x and y); ChildFlags_AlwaysUseWindowPadding
/// pads without a frame; with neither the content starts at the child's edge. ChildFlags_AutoResizeY makes the child as tall as its content
/// was on the last frame (one row on the first), so it never scrolls. Room for the scroll bar is taken only when the content overflowed on
/// the last frame, and the gap after the last item is not content. The other flags and the window flags are accepted and ignored.
bool BeginChild(const char* id, Vec2 size, int childFlags, int windowFlags = 0);

// ---- Edit tracking and item width ---------------------------------------------------------------------------------------
/// The last item changed its value this frame (the same moment its widget returned true). Value widgets: checkbox, radio button,
/// sliders, drag fields, number inputs, text fields, colour editors, combo.
bool IsItemEdited();
/// The last item was let go (or, for a text field, lost the keyboard) after its value was edited since it was picked up.
bool IsItemDeactivatedAfterEdit();
/// The width the next item would get: what is left on the line, or what SetNextItemWidth / PushItemWidth say.
float CalcItemWidth();

// ---- Tooltips with widgets in them -------------------------------------------------------------------------------------
/// A tooltip you fill with any widgets: `if (IsItemHovered() && BeginTooltip()) { Text("..."); Button... ; EndTooltip(); }`. It follows
/// the pointer (flips to stay in the display), never takes the pointer itself, and is sized from last frame's content (so the first frame
/// is one row high). Returns false when it cannot begin. EndTooltip only after a true BeginTooltip.
bool BeginTooltip();
void EndTooltip();
/// `IsItemHovered(flags) && BeginTooltip()`. The default flags are ForTooltip's (a short delay).
bool BeginItemTooltip(int hoverFlags = HoveredFlags_ForTooltip);

// ---- Popups -------------------------------------------------------------------------------------------------------------
/// For BeginPopupContextItem / BeginPopupContextWindow / IsPopupOpen. The low two bits choose the button (none: right, 1 left,
/// 2 right, 3 middle). NoOpenOverItems: BeginPopupContextWindow does not open over an item. AnyPopupId and AnyPopupLevel: IsPopupOpen
/// asks about any popup. NoReopen and NoOpenOverExistingPopup are accepted.
enum PopupFlags : int {
    PopupFlags_None = 0, PopupFlags_MouseButtonLeft = 1, PopupFlags_MouseButtonRight = 2, PopupFlags_MouseButtonMiddle = 3,
    PopupFlags_NoReopen = 8, PopupFlags_NoOpenOverExistingPopup = 32, PopupFlags_NoOpenOverItems = 64,
    PopupFlags_AnyPopupId = 128, PopupFlags_AnyPopupLevel = 256,
};
/// A press of the chosen button on the window's empty space (or on an item, unless NoOpenOverItems) opens the popup at the pointer; call
/// it where the window's items are done. `id` null: one popup per window.
bool BeginPopupContextWindow(const char* id = nullptr, int popupFlags = PopupFlags_MouseButtonRight);
bool BeginPopupContextItem(const char* id, int popupFlags);
bool IsPopupOpen(const char* id, int popupFlags);

/// For ButtonFlags in code that passes them to InvisibleButton / ImageButton: accepted, the left button is what clicks.
enum ButtonFlags : int { ButtonFlags_None = 0, ButtonFlags_MouseButtonLeft = 1, ButtonFlags_MouseButtonRight = 2, ButtonFlags_MouseButtonMiddle = 4 };

// ---- Draw flags (AddRect, AddRectFilled, AddPolyline) -------------------------------------------------------------------
/// Closed: a polyline joins its end to its start. The RoundCorners flags choose which corners a rectangle rounds (none given: all;
/// RoundCornersNone: none). Used by the flagged DrawList::AddRect / AddRectFilled below.
enum DrawFlags : int {
    DrawFlags_None = 0, DrawFlags_Closed = 1,
    DrawFlags_RoundCornersTopLeft = 16, DrawFlags_RoundCornersTopRight = 32, DrawFlags_RoundCornersBottomLeft = 64, DrawFlags_RoundCornersBottomRight = 128,
    DrawFlags_RoundCornersNone = 256,
    DrawFlags_RoundCornersTop = DrawFlags_RoundCornersTopLeft | DrawFlags_RoundCornersTopRight,
    DrawFlags_RoundCornersBottom = DrawFlags_RoundCornersBottomLeft | DrawFlags_RoundCornersBottomRight,
    DrawFlags_RoundCornersLeft = DrawFlags_RoundCornersTopLeft | DrawFlags_RoundCornersBottomLeft,
    DrawFlags_RoundCornersRight = DrawFlags_RoundCornersTopRight | DrawFlags_RoundCornersBottomRight,
    DrawFlags_RoundCornersAll = DrawFlags_RoundCornersTop | DrawFlags_RoundCornersBottom,
};
/// DrawList::AddRect(min, max, col, rounding, drawFlags, thickness), AddRectFilled(min, max, col, rounding, drawFlags), AddNgon and
/// AddNgonFilled are members of DrawList (OkayUI.hpp) and take these flags. Rounded corners are drawn as a path (a polygon of a few
/// segments per corner); flags 0 or all four corners draw the same as the one-call versions.

// ---- Tab bar flags ------------------------------------------------------------------------------------------------------
/// For BeginTabBar(id, flags). Honoured: Reorderable (drag a tab along the bar to move it; the order is remembered per bar),
/// AutoSelectNewTabs (a tab that appears after the first frame becomes the chosen one), NoCloseWithMiddleMouseButton (closable
/// tabs otherwise close with a middle click). Accepted: the fitting policies, the tab-list button, no tooltip (there is no scroll
/// strip and no tab list here: tabs past the bar's width are cut off).
enum TabBarFlags : int {
    TabBarFlags_None = 0, TabBarFlags_Reorderable = 1, TabBarFlags_AutoSelectNewTabs = 2, TabBarFlags_TabListPopupButton = 4,
    TabBarFlags_NoCloseWithMiddleMouseButton = 8, TabBarFlags_NoTabListScrollingButtons = 16, TabBarFlags_NoTooltip = 32,
    TabBarFlags_FittingPolicyResizeDown = 64, TabBarFlags_FittingPolicyScroll = 128,
};
bool BeginTabBar(const char* id, int tabBarFlags);
/// More tab item flags (Tab_SetSelected, Tab_Trailing, Tab_NoTooltip are in OkayUI.hpp): UnsavedDocument draws a dot after the label,
/// NoCloseWithMiddleMouseButton and NoReorder do what they say, Leading is accepted (the tab sits where it is called).
enum TabItemFlagsMore : int { TabItem_UnsavedDocument = 8, TabItem_NoCloseWithMiddleMouseButton = 16, TabItem_NoReorder = 32, TabItem_Leading = 64 };
/// Pure helper behind reordering, public so it can be tested alone: tabs of `widths[0..n)` laid out left to right from `left` with `gap`
/// between them; the tab `from` is being dragged and the pointer is at `mouseX`. Returns the index it should now have (the tab under the
/// pointer, clamped to 0..n-1; `from` when the pointer is over it or outside the strip's ends keeps the end tab).
int TabDragTarget(const float* widths, int n, int from, float left, float gap, float mouseX);

} // namespace OkayUI
