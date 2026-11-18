#pragma once
// Internals that ported code reaches for: the window being built, the active item, key ownership and a small per-window store.
// Own design, deliberately minimal: values and handles, not pointers into OkayUI's private state (OkayUI keeps its state in file-scope
// tables; there is no window or context object to hand out). Written from what the names mean. Where a call differs from Dear ImGui's
// internal one, the difference is written at the declaration. Nothing here includes SDL or Dear ImGui.
#include "okay/UI/OkayUICompat.hpp"
#include "okay/UI/OkayUIDockBuilder.hpp"

#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace OkayUI {

struct WindowRef;
struct DockNodeRef;

/// A snapshot of the window between Begin and End (valid == false outside one). Not a live handle: reading it later shows the window
/// as it was when GetCurrentWindow was called.
struct CurrentWindow {
    const CurrentWindow* operator->() const { return this; }   // `GetCurrentWindow()->Pos` as ported code writes it
    bool valid = false;
    unsigned id = 0;                      // the window id (hash of the title; the id PushID does not change)
    char title[96] = {0};
    Vec2 pos{0, 0}, size{0, 0};
    bool focused = false, hovered = false, appearing = false;
    unsigned long long z = 0;             // stacking order: higher is in front; 0 for docked content and bare host panels
    // Dear ImGui's field names for the same facts, and the pointer-like use of `ImGuiWindow* w = GetCurrentWindow();` (the window table entry,
    // see WindowRef below; null outside a window).
    unsigned ID = 0;
    Vec2 Pos{0, 0}, Size{0, 0};
    const WindowRef* RootWindow = nullptr;   // the window's own table entry: OkayUI has no child windows, so every window is its own root
    operator WindowRef*() const;
};
CurrentWindow GetCurrentWindow();
/// The same call (a snapshot is always read-only); kept so ported code that names the read-only form compiles.
inline CurrentWindow GetCurrentWindowRead() { return GetCurrentWindow(); }

/// What Dear ImGui code reads from `ImGuiWindow*` and `ImGuiContext` (the Inspector panel: `w->Name`, `w->ParentWindow`, `g.ActiveId`,
/// `g.ActiveIdWindow`, `g.HoveredWindow`), as plain values refreshed by RefreshContext(). OkayUI has no window or context objects, so
/// these are snapshots: a pointer is valid until the next RefreshContext() call (use it within the frame). Field by field:
///   WindowRef::Name        the title the window was begun with (up to 63 characters).
///   WindowRef::ID          GetID of the title (the same id CurrentWindow::id gives).
///   WindowRef::Pos / Size  where the window was last drawn.
///   WindowRef::ParentWindow always null: a child region (BeginChild) is not a window here, so a walk up the parents ends at the window itself.
///   Context::ActiveId      the item being pressed or dragged (GetActiveID()).
///   Context::ActiveIdWindow the focused window while an item is active (the window the press began in is not tracked; a press in a window
///                          gives it the focus first, so this is the same window in practice), else null.
///   Context::HoveredWindow the topmost window under the pointer, or null (no window, or the pointer is blocked).
///   Context::HoveredId     the item under the pointer, 0 for none.
///   Context::CurrentWindow the window between Begin and End, else null.
///   Context::FrameCount / Time  GetFrameCount() / GetTime().
///
/// Round 7 (docs/own_ui_toolkit.md 6.20 to 6.23): the snapshots are now ENTRIES OF ONE WINDOW TABLE that lives as long as the program. An entry
/// keeps its address for the same window name (a pointer from `FindWindowByName("Inspector")` is the one `g.Windows` holds and the one a dock
/// node lists), and its fields are refreshed in place whenever the frame number or the dock layout changes (and by RefreshContext). Entries
/// for windows that are in the dock layout but have not been drawn exist too (Active false). The table holds 256 windows; more are not listed.
/// Fields that ported code WRITES (WindowClass.DockNodeFlagsOverrideSet, DC.ItemWidth, DC.ItemWidthDefault) are applied the next time the table
/// is refreshed (see OkayUIDockNodes in the notes), not at the moment of the write.
struct WindowDC {
    /// Where the window's tab is on its dock node's strip (the last DockSpace); an empty rectangle when the window is not docked or has no tab drawn.
    ImRect DockTabItemRect;
    /// Writing either one sets the default width of the value column for the rest of the window (what PushItemWidth does, undone by End).
    struct WidthRef { float v = 0.0f; WidthRef& operator=(float f); operator float() const { return v; } } ItemWidth, ItemWidthDefault;
    Vec2 CursorPos{0, 0}, CursorStartPos{0, 0}, CursorMaxPos{0, 0};   // read-only (values at the refresh)
};
struct WindowClassRef {
    unsigned ClassId = 0;
    int DockNodeFlagsOverrideSet = 0;     // DockNode_NoTabBar here hides the tab strip of the node the window sits in (applied at the next DockSpace)
    int DockNodeFlagsOverrideClear = 0;
};
typedef WindowClassRef ImGuiWindowClass;
struct WindowRef {
    char Name[64] = {0};
    unsigned ID = 0;
    Vec2 Pos{0, 0}, Size{0, 0};
    WindowRef* ParentWindow = nullptr;     // always null here (no child windows)
    WindowRef* RootWindow = nullptr;       // the entry itself
    bool Active = false;                   // drawn this frame
    bool WasActive = false;                // drawn this frame or the one before
    float WindowBorderSize = 1.0f;
    ImRect InnerClipRect;                  // the window's content area (its rectangle without the title bar)
    ImDrawList* DrawList = nullptr;        // the window draw list (the one GetWindowDrawList gives; OkayUI has one batch for all windows)
    // docking (what the dock layout says at the refresh)
    DockNodeRef* DockNode = nullptr;       // the leaf the window is a tab of; null when it floats or is unknown
    unsigned DockId = 0;                   // that node's id, kept after the window floats again (the last node it sat in)
    bool DockIsActive = false;             // docked in a leaf that has been laid out
    bool DockTabIsVisible = false;         // docked and the chosen tab of its leaf
    int DockOrder = -1;                    // its place among its leaf's tabs, -1 when not docked
    unsigned TabId = 0;                    // the id of its tab item (the same value TabItemRef::ID has)
    WindowDC DC;
    WindowClassRef WindowClass;
};
typedef WindowRef ImGuiWindow;

/// A tab on a dock node's strip, a tab strip, and a dock node, as plain values (Dear ImGui's ImGuiTabItem, ImGuiTabBar and ImGuiDockNode under
/// the field names editor code reads). Built from the dock layout (OkayUIDock.hpp) and the geometry of the last DockSpace; every pointer points
/// into the same tables as the window entries do. Writes that mean something are applied at the next refresh: `SizeRef[axis]` (the node's size
/// along its parent's split, as DockBuilderSetNodeSize would), and a different first entry of `Windows` (that window becomes the chosen tab).
/// Everything else is read-only. Where OkayUI has no counterpart the field is a fixed value, written next to it.
struct TabItemRef {
    unsigned ID = 0;                       // the tab's id (a hash of the window name; == WindowRef::TabId)
    int Flags = 0;                         // always 0 (only real tabs are listed, never the buttons)
    WindowRef* Window = nullptr;
    float Offset = 0.0f, Width = 0.0f, ContentWidth = 0.0f;   // along the strip, screen pixels relative to the strip's left edge
    bool operator==(const TabItemRef& o) const { return ID == o.ID; }
};
struct TabBarRef {
    unsigned ID = 0;
    int Flags = 0;                         // TabBar_IsFocused when the node's chosen window is the focused window
    unsigned VisibleTabId = 0, SelectedTabId = 0;   // the chosen tab (both the same here)
    ImRect BarRect;                        // the strip, screen pixels
    float WidthAllTabsIdeal = 0.0f;        // what all tabs would take unsqueezed (plus the room kept for a trailing button)
    ImVector<TabItemRef> Tabs;
};
typedef TabBarRef ImGuiTabBar;
typedef TabItemRef ImGuiTabItem;
struct DockNodeRef {
    unsigned ID = 0;                       // the builder id when the node came from a plan or is the DockSpace root, else a derived id (DockBuilderGetNode takes either)
    DockNodeRef* ParentNode = nullptr;
    DockNodeRef* ChildNodes[2] = {nullptr, nullptr};   // [0] is the left / top half
    ImVector<WindowRef*> Windows;          // leaf: its tabs in strip order
    TabBarRef* TabBar = nullptr;           // leaf with a strip, else null
    WindowRef* HostWindow = nullptr;       // the dock space's host entry (name "##DockSpaceHost"), the same for every node
    Vec2 Pos{0, 0}, Size{0, 0};
    Vec2 SizeRef{0, 0};                    // the size the node asks for (read as Size; write it to resize, see above)
    int SplitAxis = Axis_None;             // split: Axis_X when the halves sit side by side, Axis_Y when stacked
    int LocalFlags = 0, MergedFlags = 0;   // DockNode_DockSpace on the root, DockNode_NoTabBar when the strip is hidden
    bool IsVisible = false;                // a leaf that holds a window and has room
    bool WantLockSizeOnce = false;         // accepted and unused (a size set through SizeRef is not locked)
    int LastFrameActive = 0;               // the frame of the last DockSpace
    bool IsRootNode() const { return ParentNode == nullptr; }
    bool IsDockSpace() const { return (LocalFlags & DockNode_DockSpace) != 0; }
    bool IsLeafNode() const { return ChildNodes[0] == nullptr; }
    bool IsSplitNode() const { return ChildNodes[0] != nullptr; }
    bool IsEmpty() const { return ChildNodes[0] == nullptr && Windows.Size == 0; }
    bool IsCentralNode() const { return false; }     // there is no pass-through central node in OkayUI's dock
    bool IsNoTabBar() const { return (MergedFlags & DockNode_NoTabBar) != 0; }
    bool IsHiddenTabBar() const { return IsLeafNode() && TabBar == nullptr && !IsNoTabBar() && Windows.Size > 0; }   // never true: a strip is always drawn unless NoTabBar
    bool IsFloatingNode() const { return false; }
};
typedef DockNodeRef ImGuiDockNode;
/// A storage pair as Dear ImGui lists them, enough for `g.DockContext.Nodes.Data[i].val_p`.
struct StoragePairRef { unsigned key = 0; void* val_p = nullptr; };
struct DockContextRef {
    struct NodeMap { ImVector<StoragePairRef> Data; } Nodes;   // every node of the live layout, keyed by node id (val_p is a DockNodeRef*)
};

struct Context {
    unsigned ActiveId = 0, HoveredId = 0;
    float HoveredIdTimer = 0.0f;           // seconds the pointer has been over the same item (worked out from the times RefreshContext is called; call it each frame for an accurate value)
    WindowRef* ActiveIdWindow = nullptr;
    WindowRef* HoveredWindow = nullptr;    // the window under the pointer; over the dock space's tab strips and empty nodes: the dock host entry
    WindowRef* CurrentWindow = nullptr;
    WindowRef* NavWindow = nullptr;        // the focused window (what SetWindowFocus / a click chose), else null
    int FrameCount = 0;
    double Time = 0.0;
    int ConfigNavWindowingKeyNext = 0, ConfigNavWindowingKeyPrev = 0;   // accepted (OkayUI has no window-switching keys of its own)
    ImVector<WindowRef*> Windows;          // every window of the table, back to front (floating windows by stacking order, docked and undrawn ones first)
    DockContextRef DockContext;
    StyleInfo Style;                       // a copy of GetStyle() at the refresh
};
/// The windows OkayUI has a slot for (everything Begin was called for and not yet forgotten), for OkayUIDockNodes.cpp: not for hosts.
struct WindowSlotInfo { char title[64] = {0}; unsigned id = 0; Vec2 pos{0, 0}, size{0, 0}; unsigned long long lastFrame = 0, z = 0; bool focused = false; };
int ListWindowSlots(WindowSlotInfo* out, int cap);
unsigned WindowIdOfTitle(const char* title);          // the id a window of this title has (GetID with no PushID in force)
/// Set the default width of the value column for the rest of the current window (PushItemWidth, undone when the window ends). Nothing outside a window.
void SetWindowItemWidthDefault(float width);
/// The strip a TabItemButton outside a tab bar is placed on (DockNodeBeginAmendTabBar sets it, DockNodeEndAmendTabBar clears it); buttons go right to left from its right edge. `TabAmendUsed` is the room they took. Not for hosts.
void SetTabAmend(bool on, Rect strip);
float TabAmendUsed();
/// Called by RefreshContext after it knows which slots are hovered, active, current and focused (0 = none); fills the pointers and lists.
void FillContextFromWindowTable(Context& c, unsigned hoveredId, unsigned activeId, unsigned currentId, unsigned focusedId);
/// Writes ported code made to snapshots (SizeRef, Windows order, WindowClass flags) turned into layout edits; DockSpace calls this first. Not for hosts.
void ApplyDockNodeEdits();

/// Fill the one global context and its window snapshots from OkayUI's state and return it. `GImGui` (OkayUIImGuiNames.hpp) calls this.
Context* RefreshContext();

/// The table entry for a window by title or by id (GetID of the title); null when the window is neither drawn nor in the dock layout.
WindowRef* FindWindowRefByName(const char* title);
WindowRef* FindWindowRefByID(unsigned id);
/// The dock node of the current window (null outside a window, or when it floats), and the root above any node (the node itself for a root, null for null).
DockNodeRef* GetWindowDockNode();
DockNodeRef* DockNodeGetRootNode(const DockNodeRef* node);
/// The node with this id in the live layout (a builder id, the DockSpace id, or the derived id a node of a user-made split has), else the plan
/// node of that id (windows from the plan, no tab strip, geometry from the plan), else null. Same lifetime as the window entries.
DockNodeRef* DockBuilderGetNode(DockId id);
/// Tab-strip amendments: after DockNodeBeginAmendTabBar(node) returns true, a TabItemButton / SetNextItemWidth pair puts a button at the right end of
/// that node's strip (the strip keeps the room from the next DockSpace on) until DockNodeEndAmendTabBar(). False for a node without a strip.
bool DockNodeBeginAmendTabBar(DockNodeRef* node);
void DockNodeEndAmendTabBar();
/// The tab with this id on the strip, or null.
TabItemRef* TabBarFindTabByID(TabBarRef* bar, unsigned tabId);
/// A hash of a string for use as an id (not the value GetID gives; use it with the id-taking calls: OpenPopup(id), BeginPopupEx(id, ...)). `size` 0 = to the zero byte.
unsigned ImHashStr(const char* data, size_t size = 0, unsigned seed = 0);
/// Lay a dock space out for `id` at the cursor: `size` 0 or less means "what is left minus that much" (0 = all of it). Calls DockSpace(Rect), reserves the
/// room as an item and remembers `id` as the live root's id. `flags` is accepted (DockNode_NoTabBar on the root hides the strips of the whole tree).
/// Returns `id`.
DockId DockSpace(DockId id, Vec2 size = Vec2{0, 0}, int flags = DockNode_None);

/// The id of the item being pressed or dragged now (the same ids GetID / GetItemID give), 0 when none. Text fields count while they
/// have the keyboard only through ClearActiveID / GetKeyboardFocusId, not here.
unsigned GetActiveID();
/// Let go of whatever is pressed and take the keyboard away from the focused text field (what the editor's search box uses to
/// "drop focus after Escape"). The release shows as IsItemDeactivated on the frame after, as a normal loss of focus does.
void ClearActiveID();

/// Make the LAST item the owner of `key` for this frame and the next: Shortcut() will not fire for a chord with that key while an item
/// owns it (a console that uses Up / Down for its history, a text box that uses Ctrl+S itself). Does nothing after an item that has
/// no id, or for a bad key id. Ownership is only consulted by Shortcut and GetKeyOwner; IsKeyPressed / IsKeyDown still report the
/// raw key (OkayUI has no global keyboard navigation that would also need to be told to stay away).
void SetItemKeyOwner(KeyId key);
/// Give `key` to the item with this id (e.g. a GetID value) for this frame and the next, without needing the item to be the last one. `ownerId` 0 clears it.
void SetKeyOwner(KeyId key, unsigned ownerId);
/// The id of the item that owns `key` now (0 for none or a bad key id).
unsigned GetKeyOwner(KeyId key);

/// A small store of values by id for the current window, kept between frames (a tree-open flag, a remembered tab, a scroll value that
/// a custom widget keeps for itself). Ids are yours (GetID gives a good one). Outside a window the store is a global one. Each call
/// looks the value up again, so the handle is cheap to copy and stays valid across frames. Limits: 1024 values in all (a full store
/// drops new values; Get returns the default). ResetState empties it.
class StateStorage {
public:
    int   GetInt(unsigned id, int def = 0) const;
    void  SetInt(unsigned id, int value);
    bool  GetBool(unsigned id, bool def = false) const { return GetInt(id, def ? 1 : 0) != 0; }
    void  SetBool(unsigned id, bool value) { SetInt(id, value ? 1 : 0); }
    float GetFloat(unsigned id, float def = 0.0f) const;
    void  SetFloat(unsigned id, float value);
    void* GetVoidPtr(unsigned id) const;           // null when never set
    void  SetVoidPtr(unsigned id, void* ptr);
    /// Forget every value of this store (the current window's only).
    void  Clear();
    /// Pointer-like use (`GetStateStorage()->SetInt(...)`, `ImGuiStorage* st = GetStateStorage();`): the pointer is to a small ring of copies of the handle (valid for the next four calls).
    StateStorage* operator->() { return this; }
    const StateStorage* operator->() const { return this; }
    operator StateStorage*() const;
private:
    friend StateStorage GetStateStorage();
    unsigned scope_ = 0;
};
StateStorage GetStateStorage();
/// Empty every store (ResetState calls it).
void StateStorageReset();

} // namespace OkayUI
