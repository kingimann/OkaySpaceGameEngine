#include "okay/UI/OkayUICompat.hpp"
#include "okay/UI/OkayUIDock.hpp"
#include "okay/UI/OkayUIInternals.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

namespace OkayUI {

namespace {
float Clamp01(float v) { return std::isfinite(v) ? (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v)) : 0.0f; }
float Finite(float v) { return std::isfinite(v) ? v : 0.0f; }
}

Viewport GetMainViewport() {
    Viewport vp;
    const IO& io = GetIO();
    vp.size = Vec2{ Finite(io.displaySize.x), Finite(io.displaySize.y) };
    vp.Pos = vp.pos; vp.Size = vp.size; vp.WorkPos = vp.pos; vp.WorkSize = vp.size;
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
    if (w.found) { w.docked = IsWindowDocked(title); std::snprintf(w.Name, sizeof w.Name, "%s", title); w.Pos = w.pos; w.Size = w.size; w.ID = GetID(title); }
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


// ---- Step 6.10 ----
Vec2 GetCursorStartPos() { return GetWindowContentRegionMin(); }
Viewport GetWindowViewport() { return GetMainViewport(); }
void SetNextWindowViewport(unsigned) {}

namespace {
struct ItemFlagRec { bool disabled; };
ItemFlagRec g_flagStack[16];
int g_flagTop = 0;
}
void PushItemFlag(int flag, bool enabled) {
    if (g_flagTop >= 16) return;
    const bool dis = (flag & ItemFlag_Disabled) != 0 && enabled;
    g_flagStack[g_flagTop++] = ItemFlagRec{ dis };
    if (dis) BeginDisabled(true);
}
void PopItemFlag() {
    if (g_flagTop <= 0) return;
    if (g_flagStack[--g_flagTop].disabled) EndDisabled();
}

void PushMultiItemsWidths(int count, float totalWidth) {
    if (count < 1) count = 1;
    if (count > 8) count = 8;
    if (!std::isfinite(totalWidth) || totalWidth < 1.0f) totalWidth = 1.0f;
    const float gap = GetStyle().ItemInnerSpacing.x;
    float one = std::floor((totalWidth - gap * (float)(count - 1)) / (float)count);
    if (one < 1.0f) one = 1.0f;
    float last = totalWidth - (one + gap) * (float)(count - 1);
    if (last < 1.0f) last = 1.0f;
    PushItemWidth(last);                                   // the last item's width goes in first, so the first item's is on top
    for (int i = 1; i < count; ++i) PushItemWidth(one);
}

const char* FindRenderedTextEnd(const char* text, const char* textEnd) {
    if (!text) return nullptr;
    const char* p = text;
    while ((!textEnd || p < textEnd) && *p) {
        if (p[0] == '#' && p[1] == '#' && (!textEnd || p + 1 < textEnd)) return p;
        ++p;
    }
    return p;
}

namespace {
bool g_ctxCurrent = true;
}
Context* CreateContext() { g_ctxCurrent = true; return RefreshContext(); }
void DestroyContext(Context* ctx) {
    if (ctx && ctx != RefreshContext()) return;
    ResetState();
    g_ctxCurrent = false;
}
Context* GetCurrentContext() { return g_ctxCurrent ? RefreshContext() : nullptr; }
void SetCurrentContext(Context* ctx) {
    if (!ctx) g_ctxCurrent = false;
    else if (ctx == RefreshContext()) g_ctxCurrent = true;
}

namespace {
void SetBase(Theme& t, const unsigned char bg[4], const unsigned char hov[4], const unsigned char down[4], const unsigned char border[4],
             const unsigned char text[4], const unsigned char panel[4], const unsigned char track[4], const unsigned char accent[4],
             const unsigned char dim[4], const unsigned char title[4], const unsigned char field[4], bool shadow) {
    std::memcpy(t.bg, bg, 4); std::memcpy(t.bgHover, hov, 4); std::memcpy(t.bgDown, down, 4); std::memcpy(t.border, border, 4);
    std::memcpy(t.text, text, 4); std::memcpy(t.panel, panel, 4); std::memcpy(t.track, track, 4); std::memcpy(t.accent, accent, 4);
    std::memcpy(t.textDim, dim, 4); std::memcpy(t.titleBg, title, 4); std::memcpy(t.field, field, 4);
    t.windowShadow = shadow;
    std::memset(t.colors, 0, sizeof t.colors); t.colorSet = 0;
}
}
void StyleColorsDark() {
    const Theme d;   // the library's default palette
    SetBase(Style(), d.bg, d.bgHover, d.bgDown, d.border, d.text, d.panel, d.track, d.accent, d.textDim, d.titleBg, d.field, d.windowShadow);
}
void StyleColorsLight() {
    static const unsigned char bg[4] = {222, 226, 234, 255}, hov[4] = {204, 212, 226, 255}, down[4] = {184, 194, 212, 255}, bd[4] = {150, 156, 170, 255},
        tx[4] = {26, 29, 36, 255}, pn[4] = {240, 242, 246, 240}, tr[4] = {208, 213, 222, 255}, ac[4] = {44, 108, 214, 255},
        dm[4] = {108, 114, 128, 255}, ti[4] = {204, 210, 224, 255}, fl[4] = {250, 251, 253, 255};
    SetBase(Style(), bg, hov, down, bd, tx, pn, tr, ac, dm, ti, fl, false);
}
void StyleColorsClassic() {
    static const unsigned char bg[4] = {46, 52, 74, 255}, hov[4] = {64, 72, 104, 255}, down[4] = {32, 36, 56, 255}, bd[4] = {20, 22, 34, 255},
        tx[4] = {230, 232, 240, 255}, pn[4] = {30, 33, 48, 240}, tr[4] = {22, 24, 38, 255}, ac[4] = {96, 120, 214, 255},
        dm[4] = {144, 150, 172, 255}, ti[4] = {40, 46, 78, 255}, fl[4] = {22, 24, 38, 255};
    SetBase(Style(), bg, hov, down, bd, tx, pn, tr, ac, dm, ti, fl, false);
}

const char* SaveIniSettingsToMemory(std::size_t* size) {
    static std::string s;
    s = DockSaveLayout();
    if (size) *size = (unsigned long long)s.size();
    return s.c_str();
}
void LoadIniSettingsFromMemory(const char* data, std::size_t size) {
    if (!data) return;
    const std::string blob = size ? std::string(data, (size_t)size) : std::string(data);
    DockLoadLayout(blob);
}

bool IsItemHovered(int) { return IsItemHovered(); }
bool IsWindowHovered(int flags) { return (flags & Hovered_AnyWindow) ? (IsWindowHovered() || GetIO().wantCaptureMouse) : IsWindowHovered(); }
bool IsWindowFocused(int flags) {
    if (flags & Focused_AnyWindow) return IsWindowFocused() || AnyWindowFocused();
    return IsWindowFocused();
}

#define OKAYUI_FORMAT(buf, fmt) char buf[1024]; { va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof buf, fmt ? fmt : "", ap); va_end(ap); }
void TextFormatted(const char* fmt, ...) { OKAYUI_FORMAT(b, fmt); Text(static_cast<const char*>(b)); }
void TextDisabledFormatted(const char* fmt, ...) { OKAYUI_FORMAT(b, fmt); TextDisabled(static_cast<const char*>(b)); }
void TextWrappedFormatted(const char* fmt, ...) { OKAYUI_FORMAT(b, fmt); TextWrapped(static_cast<const char*>(b)); }
void BulletTextFormatted(const char* fmt, ...) { OKAYUI_FORMAT(b, fmt); BulletText(static_cast<const char*>(b)); }
void TextColoredFormatted(Vec4 color, const char* fmt, ...) {
    OKAYUI_FORMAT(b, fmt);
    PushStyleColor(Col_Text, color); Text(static_cast<const char*>(b)); PopStyleColor();
}
#undef OKAYUI_FORMAT
Color32 GetColorU32(Vec4 color) { return ColorConvertFloat4ToU32(color); }

bool SliderFloat(const char* label, float* value, float minV, float maxV, const char*, int) { return SliderFloat(label, value, minV, maxV); }
bool SliderInt(const char* label, int* value, int minV, int maxV, const char*, int) { return SliderInt(label, value, minV, maxV); }
bool DragFloat(const char* label, float* v, float speed, float minV, float maxV, const char*, int) { return DragFloat(label, v, speed, minV, maxV); }
bool DragInt(const char* label, int* v, float speed, int minV, int maxV, const char*, int) { return DragInt(label, v, speed, minV, maxV); }
Vec2 CalcTextSize(const char* text, const char* textEnd, bool hideAfterHash, float) {
    if (!text) return Vec2{0, 0};
    char buf[1024]; std::size_t n = textEnd && textEnd >= text ? (std::size_t)(textEnd - text) : std::strlen(text);
    if (n > sizeof buf - 1) n = sizeof buf - 1;
    std::memcpy(buf, text, n); buf[n] = 0;
    if (hideAfterHash) { const char* e = FindRenderedTextEnd(buf); buf[e - buf] = 0; }
    return CalcTextSize(static_cast<const char*>(buf));
}
bool Combo(const char* label, int* current, const char* zs, int) {
    const char* items[64]; int count = 0;
    for (const char* p = zs; p && *p && count < 64; p += std::strlen(p) + 1) items[count++] = p;
    return Combo(label, items, count, current);
}
bool BeginMenu(const char* label, bool enabled) {
    if (enabled) return BeginMenu(label);
    BeginDisabled(true); BeginMenu(label); EndDisabled();
    return false;
}
bool BeginTable(const char* id, int columns, int flags) { return BeginTable(id, columns, static_cast<TableFlags>(flags)); }
bool BeginTable(const char* id, int columns, int flags, Vec2 outerSize, float innerWidth) { return BeginTable(id, columns, static_cast<TableFlags>(flags), outerSize, innerWidth); }
bool IsMouseHoveringRect(Vec2 mn, Vec2 mx, bool) { return IsMouseHoveringRect(mn, mx); }
void NewFrame() { Input in = GetFrameInput(); BeginFrame(in); }
void UpdatePlatformWindows() {}
void RenderPlatformWindowsDefault() {}
PlatformIO& GetPlatformIO() { static PlatformIO pio; return pio; }
bool DockContextQueueUndockWindow(Context*, const WindowRef* window) {
    if (!window || !window->Name[0]) return false;
    DockLayout& l = GetDockLayout();
    if (!l.Contains(window->Name) || l.IsFloating(window->Name)) return false;
    return l.Float(window->Name, Rect{ window->Pos.x, window->Pos.y, window->Size.x > 1.0f ? window->Size.x : 320.0f, window->Size.y > 1.0f ? window->Size.y : 240.0f });
}
bool BeginDragDropTargetCustom(const ImRect& bb, unsigned id) { return BeginDragDropTargetCustom(Rect{bb.Min.x, bb.Min.y, bb.Max.x - bb.Min.x, bb.Max.y - bb.Min.y}, id); }
bool InputInt(const char* label, int* value, int step, int, int) { return InputInt(label, value, step > 0 ? step : 1); }
bool DragFloat2(const char* l, float v[2], float sp, float mn, float mx, const char*, int) { return DragFloat2(l, v, sp, mn, mx); }
bool DragFloat3(const char* l, float v[3], float sp, float mn, float mx, const char*, int) { return DragFloat3(l, v, sp, mn, mx); }
bool DragFloat4(const char* l, float v[4], float sp, float mn, float mx, const char*, int) { return DragFloat4(l, v, sp, mn, mx); }
bool DragFloatRange2(const char* l, float* lo, float* hi, float sp, float mn, float mx, const char*, const char*, int) { return DragFloatRange2(l, lo, hi, sp, mn, mx); }
bool SliderFloat2(const char* l, float v[2], float mn, float mx, const char*, int) { return SliderFloat2(l, v, mn, mx); }
bool SliderFloat3(const char* l, float v[3], float mn, float mx, const char*, int) { return SliderFloat3(l, v, mn, mx); }
bool SliderFloat4(const char* l, float v[4], float mn, float mx, const char*, int) { return SliderFloat4(l, v, mn, mx); }
bool InputFloat(const char* l, float* v, float step, float, const char*, int) { return InputFloat(l, v, step > 0.0f ? step : 0.1f); }
void SetNextItemOpen(bool open, Cond) { SetNextItemOpen(open); }
bool IsPopupOpen(const char* name, int flags) {
    if (name && *name) return IsPopupOpen(name);
    if (flags & Popup_AnyPopupId) return AnyPopupOpen();
    return false;
}
bool BeginPopup(const char* id, int) { return BeginPopup(id); }
void SetKeyboardFocusHere(int) { SetKeyboardFocusHere(); }
bool RadioButton(const char* label, bool active) { int v = active ? 1 : 0; return RadioButton(label, &v, 1) && !active; }
bool ColorButton(const char* id, const ImVec4& c, int flags, Vec2 size) { const float rgba[4] = {c.x, c.y, c.z, c.w}; return ColorButton(id, rgba, flags, size.x > 0.0f ? size.x : 0.0f); }
void PlotLines(const char* l, const float* v, int n, int off, const char* ov, float mn, float mx, Vec2 sz) { PlotOptions o; o.valuesOffset = off; o.overlay = ov; o.scaleMin = mn; o.scaleMax = mx; o.size = sz; PlotLines(l, v, n, o); }
void PlotHistogram(const char* l, const float* v, int n, int off, const char* ov, float mn, float mx, Vec2 sz) { PlotOptions o; o.valuesOffset = off; o.overlay = ov; o.scaleMin = mn; o.scaleMax = mx; o.size = sz; PlotHistogram(l, v, n, o); }
namespace {
Font* g_fontStack[8]; const Font* g_fontBefore[8]; int g_fontTop = 0;
}
void PushFont(Font* font) {
    if (g_fontTop >= 8) return;
    g_fontBefore[g_fontTop] = GetFont(); g_fontStack[g_fontTop] = font; ++g_fontTop;
    if (font) SetFont(font);
}
void PushFont(Font* font, float) { PushFont(font); }
void PopFont() { if (g_fontTop <= 0) return; --g_fontTop; SetFont(g_fontBefore[g_fontTop]); }
void InputTextCallbackData::InsertChars(int pos, const char* text, const char* end) {
    if (!Buf || !text || pos < 0 || pos > BufTextLen) return;
    int n = end ? (int)(end - text) : (int)std::strlen(text);
    if (BufTextLen + n >= BufSize) n = BufSize - 1 - BufTextLen;
    if (n <= 0) return;
    for (int i = BufTextLen; i >= pos; --i) Buf[i + n] = Buf[i];
    std::memcpy(Buf + pos, text, (std::size_t)n); BufTextLen += n; BufDirty = true;
}
bool InputTextWithHint(const char* l, const char* h, char* b, int c, unsigned f, InputTextCallback, void*) { return InputTextWithHint(l, h, b, c, f); }
bool InputText(const char* l, char* b, int c, unsigned f, InputTextCallback, void*) { return InputText(l, b, c, f); }
bool IsWindowDocked() { const CurrentWindow w = GetCurrentWindow(); return w.valid && IsWindowDocked(w.title); }
WindowInfo::operator WindowRef*() const {
    if (!found) return nullptr;
    static WindowRef ring[8]; static int next = 0;
    WindowRef& r = ring[next]; next = (next + 1) & 7;
    std::snprintf(r.Name, sizeof r.Name, "%s", Name); r.ID = ID; r.Pos = pos; r.Size = size; r.ParentWindow = nullptr;
    return &r;
}
bool InvisibleButton(const char* id, Vec2 size, int) { return InvisibleButton(id, size.x, size.y); }
bool BeginTabBar(const char* id, int) { return BeginTabBar(id); }
bool BeginCombo(const char* label, const char* preview, int) { return BeginCombo(label, preview); }
bool Combo(const char* label, int* current, const char* const* items, int count) { return Combo(label, items, count, current); }
void OpenPopup(const char* id, int) { OpenPopup(id); }
namespace { bool RightButtonTrigger(int flags) { const int b = flags & (Popup_MouseButtonLeft | Popup_MouseButtonRight | Popup_MouseButtonMiddle); return b == 0 || (b & Popup_MouseButtonRight) != 0; } }
bool BeginPopupContextItem(const char* id, int flags) { return RightButtonTrigger(flags) ? BeginPopupContextItem(id) : false; }
bool BeginPopupContextWindow(const char* id, int flags) { return RightButtonTrigger(flags) ? BeginPopupContextWindow(id) : false; }
void Image(ImTextureID tex, Vec2 size, Vec2 uv0, Vec2 uv1, Vec4 tint, Vec4) {
    if (!tex) return;
    Dummy(size);
    const Rect r = GetItemRect();
    GetWindowDrawList().AddImage(tex, Vec2{r.x, r.y}, Vec2{r.x + r.w, r.y + r.h}, uv0, uv1, ColorConvertFloat4ToU32(tint));
}
void TreeNodeExFormatted(bool* out, const char* id, unsigned flags, const char* fmt, ...) {
    char text[512];
    va_list ap; va_start(ap, fmt); std::vsnprintf(text, sizeof text, fmt ? fmt : "", ap); va_end(ap);
    char label[640]; std::snprintf(label, sizeof label, "%s##%s", text, id ? id : "");
    *out = TreeNodeEx(label, flags);
}
} // namespace OkayUI
