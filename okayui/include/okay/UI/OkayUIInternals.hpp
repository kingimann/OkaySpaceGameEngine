#pragma once
// Internals that ported code reaches for: the window being built, the active item, key ownership and a small per-window store.
// Own design, deliberately minimal: values and handles, not pointers into OkayUI's private state (OkayUI keeps its state in file-scope
// tables; there is no window or context object to hand out). Written from what the names mean. Where a call differs from Dear ImGui's
// internal one, the difference is written at the declaration. Nothing here includes SDL or Dear ImGui.
#include "okay/UI/OkayUICompat.hpp"

namespace OkayUI {

/// A snapshot of the window between Begin and End (valid == false outside one). Not a live handle: reading it later shows the window
/// as it was when GetCurrentWindow was called.
struct CurrentWindow {
    bool valid = false;
    unsigned id = 0;                      // the window id (hash of the title; the id PushID does not change)
    char title[96] = {0};
    Vec2 pos{0, 0}, size{0, 0};
    bool focused = false, hovered = false, appearing = false;
    unsigned long long z = 0;             // stacking order: higher is in front; 0 for docked content and bare host panels
};
CurrentWindow GetCurrentWindow();
/// The same call (a snapshot is always read-only); kept so ported code that names the read-only form compiles.
inline CurrentWindow GetCurrentWindowRead() { return GetCurrentWindow(); }

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
private:
    friend StateStorage GetStateStorage();
    unsigned scope_ = 0;
};
StateStorage GetStateStorage();
/// Empty every store (ResetState calls it).
void StateStorageReset();

} // namespace OkayUI
