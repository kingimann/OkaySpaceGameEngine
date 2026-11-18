#include "okay/UI/OkayUICompat.hpp"
#include "okay/UI/OkayUIDock.hpp"
#include "okay/UI/OkayUIInternals.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace OkayUI {

namespace {
float Clamp01(float v) { return std::isfinite(v) ? (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v)) : 0.0f; }
float Finite(float v) { return std::isfinite(v) ? v : 0.0f; }
}

Viewport GetMainViewport() {
    Viewport vp;
    const IO& io = GetIO();
    vp.size = Vec2{ Finite(io.displaySize.x), Finite(io.displaySize.y) };
    return vp;
}

Vec2 GetContentRegionMax() {
    const Vec2 p = GetCursorPos(), a = GetContentRegionAvail();
    return Vec2{ p.x + a.x, p.y + a.y };
}
Vec2 GetWindowContentRegionMax() { return GetContentRegionMax(); }

void SetScrollHereY(float centerRatio) {
    const float view = GetChildViewHeight();
    if (!(view > 0.0f)) return;   // not inside a scrolling child
    // Inside a child the cursor position is measured from the top of the unscrolled content, so it is where the next item goes.
    SetScrollY(GetCursorPosY() - Clamp01(centerRatio) * view);
}
float GetScrollX() { return 0.0f; }
void SetScrollX(float) {}

void SetNextWindowSizeConstraints(Vec2 minSize, Vec2) { ResizableNextWindow(Finite(minSize.x), Finite(minSize.y)); }

void SetItemDefaultFocus() {}
void SetNextItemAllowOverlap() {}

} // namespace OkayUI

namespace OkayUI {
WindowInfo FindWindowByName(const char* title) {
    WindowInfo w;
    w.found = FindWindowSlot(title, w.pos, w.size, w.focused);
    if (w.found) w.docked = IsWindowDocked(title);
    return w;
}
bool IsWindowDocked(const char* title) {
    if (!title || !*title) return false;
    const DockLayout& l = GetDockLayout();
    return l.Contains(title) && !l.IsFloating(title);
}
} // namespace OkayUI

// ---- Step 6.9: Shortcut, key ownership, the state store ------------------------------------------------------------------------
namespace OkayUI {

namespace {
unsigned g_keyOwner[KeyId_COUNT] = {};
long long g_keyOwnerFrame[KeyId_COUNT] = {};
bool KeyOwnedNow(int key) {
    if (key <= 0 || key >= KeyId_COUNT || g_keyOwner[key] == 0) return false;
    const long long f = g_keyOwnerFrame[key], now = GetFrameCount();
    return now == f || now == f + 1;   // this frame and the next
}
}

bool Shortcut(KeyChord chord, int flags) {
    const int key = chord & 0xFF, mods = (chord >> 8) & 7;
    if (key <= KeyId_None || key >= KeyId_Ctrl) return false;      // no key, a bad id, or a modifier as the key
    const bool ctrl = IsKeyDown(KeyId_Ctrl), shift = IsKeyDown(KeyId_Shift), alt = IsKeyDown(KeyId_Alt);
    if (ctrl != ((mods & Mod_Ctrl) != 0) || shift != ((mods & Mod_Shift) != 0) || alt != ((mods & Mod_Alt) != 0)) return false;
    if (!(flags & Shortcut_RouteAlways) && (WantsTextInput() || GetKeyboardFocusId() != 0)) return false;
    if (KeyOwnedNow(key)) return false;
    return IsKeyPressed((KeyId)key, (flags & Shortcut_AllowRepeat) != 0);
}

void SetItemKeyOwner(KeyId key) {
    const unsigned id = GetItemID();
    if (id == 0 || (int)key <= 0 || (int)key >= KeyId_COUNT) return;
    g_keyOwner[(int)key] = id; g_keyOwnerFrame[(int)key] = GetFrameCount();
}
void SetKeyOwner(KeyId key, unsigned ownerId) {
    if ((int)key <= 0 || (int)key >= KeyId_COUNT) return;
    g_keyOwner[(int)key] = ownerId; g_keyOwnerFrame[(int)key] = GetFrameCount();
}
unsigned GetKeyOwner(KeyId key) { return KeyOwnedNow((int)key) ? g_keyOwner[(int)key] : 0u; }

namespace {
struct Entry { bool used = false; unsigned scope = 0, id = 0; std::uint64_t bits = 0; };
constexpr int kStoreMax = 1024;
Entry g_store[kStoreMax];
Entry* Find(unsigned scope, unsigned id, bool create) {
    Entry* freeSlot = nullptr;
    for (Entry& e : g_store) {
        if (e.used) { if (e.scope == scope && e.id == id) return &e; }
        else if (!freeSlot) freeSlot = &e;
    }
    if (!create || !freeSlot) return nullptr;
    freeSlot->used = true; freeSlot->scope = scope; freeSlot->id = id; freeSlot->bits = 0;
    return freeSlot;
}
}
StateStorage GetStateStorage() {
    StateStorage s;
    const CurrentWindow w = GetCurrentWindow();
    s.scope_ = w.valid ? w.id + 1u : 0u;
    return s;
}
int StateStorage::GetInt(unsigned id, int def) const {
    const Entry* e = Find(scope_, id, false);
    if (!e) return def;
    std::int32_t v; const std::uint32_t lo = (std::uint32_t)e->bits; std::memcpy(&v, &lo, sizeof v); return v;
}
void StateStorage::SetInt(unsigned id, int value) {
    Entry* e = Find(scope_, id, true);
    if (!e) return;
    std::int32_t v = value; std::uint32_t lo; std::memcpy(&lo, &v, sizeof lo); e->bits = lo;
}
float StateStorage::GetFloat(unsigned id, float def) const {
    const Entry* e = Find(scope_, id, false);
    if (!e) return def;
    const std::uint32_t lo = (std::uint32_t)e->bits; float v; std::memcpy(&v, &lo, sizeof v); return v;
}
void StateStorage::SetFloat(unsigned id, float value) {
    Entry* e = Find(scope_, id, true);
    if (!e) return;
    std::uint32_t lo; std::memcpy(&lo, &value, sizeof lo); e->bits = lo;
}
void* StateStorage::GetVoidPtr(unsigned id) const {
    const Entry* e = Find(scope_, id, false);
    return e ? reinterpret_cast<void*>((std::uintptr_t)e->bits) : nullptr;
}
void StateStorage::SetVoidPtr(unsigned id, void* ptr) {
    Entry* e = Find(scope_, id, true);
    if (e) e->bits = (std::uint64_t)reinterpret_cast<std::uintptr_t>(ptr);
}
void StateStorage::Clear() { for (Entry& e : g_store) if (e.used && e.scope == scope_) e = Entry(); }
void StateStorageReset() {
    for (Entry& e : g_store) e = Entry();
    for (int i = 0; i < KeyId_COUNT; ++i) { g_keyOwner[i] = 0; g_keyOwnerFrame[i] = 0; }
}

} // namespace OkayUI
