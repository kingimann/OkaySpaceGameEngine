#include "OkayUI_ContextState.hpp"
#include <memory>
#include <vector>
#include "okay/Graphics/TtfFont.hpp"
#include "okay/UI/OkayUICompat.hpp"
#include "okay/UI/OkayUIDock.hpp"
#include "okay/UI/OkayUIInternals.hpp"
#include "OkayUIInternal.hpp"   // wrappedTextSize

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
namespace { Viewport g_mainViewport; }
Viewport::operator Viewport*() const {
    const Viewport now = GetMainViewport();
    g_mainViewport.pos = now.pos; g_mainViewport.size = now.size; g_mainViewport.Pos = now.Pos; g_mainViewport.Size = now.Size;
    g_mainViewport.WorkPos = now.WorkPos; g_mainViewport.WorkSize = now.WorkSize;
    return &g_mainViewport;
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
    if (!w.found && IsWindowDocked(title)) {   // a docked tab never drawn yet (never the chosen one): it exists, as in Dear ImGui
        if (const WindowRef* r = FindWindowRefByName(title)) { const ImRect rc = r->Rect(); w.found = true; w.pos = rc.Min; w.size = Vec2{rc.Max.x - rc.Min.x, rc.Max.y - rc.Min.y}; }
    }
    if (w.found) { w.docked = IsWindowDocked(title); std::snprintf(w.Name, sizeof w.Name, "%s", title); w.Pos = w.pos; w.Size = w.size; w.ID = GetID(title); }
    return w;
}
bool IsWindowDocked(const char* title) {
    if (!title || !*title) return false;
    const DockLayout& l = GetDockLayout();
    return l.Contains(title) && !l.IsFloating(title);
}
} // namespace OkayUI

// ---- Step 6.9: Shortcut, key ownership, the state store ------
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
StateStorage::operator StateStorage*() const { static StateStorage ring[4]; static int next = 0; StateStorage& r = ring[next]; next = (next + 1) & 3; r = *this; return &r; }
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
float* StateStorage::GetFloatRef(unsigned id, float def) {
    Entry* e = Find(scope_, id, false);
    if (!e) { e = Find(scope_, id, true); if (!e) return nullptr; SetFloat(id, def); }
    return reinterpret_cast<float*>(&e->bits);   // the low 32 bits (little-endian targets; the store reads and writes the same bytes with memcpy)
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

// CreateContext, DestroyContext, GetCurrentContext and SetCurrentContext: OkayUI_Context.cpp.

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

// The `fmt` text and the NoInput / NoRoundToFormat flags go to the widget through SetNextNumberFormat (Slider_Logarithmic works on sliders whose range keeps one sign and excludes 0; a drag field stays linear).
static void nextNum(const char* fmt, int flags, const char* fmtMax = nullptr) { SetNextNumberFormat(fmt, (flags & Slider_NoInput) != 0, (flags & Slider_NoRoundToFormat) != 0, fmtMax, (flags & Slider_Logarithmic) != 0); }
bool SliderFloat(const char* label, float* value, float minV, float maxV, const char* fmt, int flags) {
    if (FramedNumbersOn()) return FramedSliderFloat(label, value, minV, maxV, fmt, (flags & Slider_Logarithmic) != 0);   // Dear ImGui's framed look (StyleVar_FramedWidgets)
    nextNum(fmt, flags); return SliderFloat(label, value, minV, maxV);
}
bool SliderInt(const char* label, int* value, int minV, int maxV, const char* fmt, int flags) {
    if (FramedNumbersOn()) return FramedSliderInt(label, value, minV, maxV, fmt);
    nextNum(fmt, flags); return SliderInt(label, value, minV, maxV);
}
bool DragFloat(const char* label, float* v, float speed, float minV, float maxV, const char* fmt, int flags) {
    if (FramedNumbersOn()) return FramedDragFloatN(label, v, 1, speed, minV, maxV, fmt, (flags & Slider_Logarithmic) != 0);
    nextNum(fmt, flags); return DragFloat(label, v, speed, minV, maxV);
}
bool DragInt(const char* label, int* v, float speed, int minV, int maxV, const char* fmt, int flags) { nextNum(fmt, flags); return DragInt(label, v, speed, minV, maxV); }
// With a wrap width, or line breaks in the text: Dear ImGui's measure of the wrapped lines (the widest line, a text line per line).
Vec2 CalcTextSize(const char* text, const char* textEnd, bool hideAfterHash, float wrapWidth) {
    if (!text) return Vec2{0, 0};
    // The common call (a whole string, no "##" to hide, no wrap): measured in place. It runs for every label every frame, and the
    // copy below was a heap allocation for each label longer than 15 bytes.
    const bool wraps = std::isfinite(wrapWidth) && wrapWidth > 0.0f;
    if (!wraps && (!textEnd || *textEnd == '\0') && (!hideAfterHash || !std::strstr(text, "##")) && !std::strchr(text, '\n'))
        return CalcTextSize(text);
    std::string buf(text, textEnd && textEnd >= text ? (std::size_t)(textEnd - text) : std::strlen(text));
    if (hideAfterHash) buf.resize((std::size_t)(FindRenderedTextEnd(buf.c_str()) - buf.c_str()));
    if ((std::isfinite(wrapWidth) && wrapWidth > 0.0f) || buf.find('\n') != std::string::npos)
        return ui_core::wrappedTextSize(buf.c_str(), std::isfinite(wrapWidth) ? wrapWidth : 0.0f, Style().textScale);
    return CalcTextSize(buf.c_str());
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
void MergePendingIo(Input& in);   // OkayUI.cpp: IO::AddKeyEvent / AddInputCharacter
IO& IoRaw();
namespace { double g_newFrameClock = 0.0; }   // NewFrame's clock: the frame times a platform feed wrote, added up (one per context)
void NewFrame() {
    Input in = GetFrameInput(); MergePendingIo(in);
    double& clock = g_newFrameClock;
    const IO& r = IoRaw();   // a platform feed (shared/ImGuiPlatformFeed.hpp) writes the display size and the frame time here

    if (r.DisplaySize.x > 0.0f && r.DisplaySize.y > 0.0f) { in.displayW = r.DisplaySize.x; in.displayH = r.DisplaySize.y; }
    if (r.DeltaTime > 0.0f && r.DeltaTime < 1.0f) { clock += (double)r.DeltaTime; in.time = clock; }
    BeginFrame(in);
}
void UpdatePlatformWindows() {}
void RenderPlatformWindowsDefault() {}
PlatformIO& GetPlatformIO() { static PlatformIO pio; return pio; }
bool PlatformClipboardSet(const char* text) { PlatformIO& p = GetPlatformIO(); if (!p.Platform_SetClipboardTextFn) return false; p.Platform_SetClipboardTextFn(nullptr, text); return true; }
const char* PlatformClipboardGet(bool* installed) { PlatformIO& p = GetPlatformIO(); if (installed) *installed = p.Platform_GetClipboardTextFn != nullptr; return p.Platform_GetClipboardTextFn ? p.Platform_GetClipboardTextFn(nullptr) : nullptr; }
bool DockContextQueueUndockWindow(Context*, const WindowRef* window) {
    if (!window || !window->Name[0]) return false;
    DockLayout& l = GetDockLayout();
    if (!l.Contains(window->Name) || l.IsFloating(window->Name)) return false;
    return l.Float(window->Name, Rect{ window->Pos.x, window->Pos.y, window->Size.x > 1.0f ? window->Size.x : 320.0f, window->Size.y > 1.0f ? window->Size.y : 240.0f });
}
bool BeginDragDropTargetCustom(const ImRect& bb, unsigned id) { return BeginDragDropTargetCustom(Rect{bb.Min.x, bb.Min.y, bb.Max.x - bb.Min.x, bb.Max.y - bb.Min.y}, id); }
bool InputInt(const char* label, int* value, int step, int, int) { return InputInt(label, value, step); }
bool DragFloat2(const char* l, float v[2], float sp, float mn, float mx, const char* fmt, int flags) { if (FramedNumbersOn()) return FramedDragFloatN(l, v, 2, sp, mn, mx, fmt, false); nextNum(fmt, flags); return DragFloat2(l, v, sp, mn, mx); }
bool DragFloat3(const char* l, float v[3], float sp, float mn, float mx, const char* fmt, int flags) { if (FramedNumbersOn()) return FramedDragFloatN(l, v, 3, sp, mn, mx, fmt, false); nextNum(fmt, flags); return DragFloat3(l, v, sp, mn, mx); }
bool DragFloat4(const char* l, float v[4], float sp, float mn, float mx, const char* fmt, int flags) { nextNum(fmt, flags); return DragFloat4(l, v, sp, mn, mx); }
bool DragFloatRange2(const char* l, float* lo, float* hi, float sp, float mn, float mx, const char* fmt, const char* fmtMax, int flags) { nextNum(fmt, flags, fmtMax ? fmtMax : fmt); return DragFloatRange2(l, lo, hi, sp, mn, mx); }
bool SliderFloat2(const char* l, float v[2], float mn, float mx, const char* fmt, int) { if (FramedNumbersOn()) return FramedSliderFloatN(l, v, 2, mn, mx, fmt); return SliderFloat2(l, v, mn, mx); }
bool SliderFloat3(const char* l, float v[3], float mn, float mx, const char* fmt, int) { if (FramedNumbersOn()) return FramedSliderFloatN(l, v, 3, mn, mx, fmt); return SliderFloat3(l, v, mn, mx); }
bool SliderFloat4(const char* l, float v[4], float mn, float mx, const char*, int) { return SliderFloat4(l, v, mn, mx); }
bool InputFloat(const char* l, float* v, float step, float, const char* fmt, int) { SetNextNumberFormat(fmt); return InputFloat(l, v, step > 0.0f ? step : 0.1f); }
void SetNextItemOpen(bool open, Cond) { SetNextItemOpen(open); }
bool IsPopupOpen(const char* name, int flags) {
    if (name && *name) return IsPopupOpen(name);
    if (flags & Popup_AnyPopupId) return AnyPopupOpen();
    return false;
}
bool BeginPopup(const char* id, int) { return BeginPopup(id); }
bool MenuItem(const char* label, const char* shortcut, bool* p_selected, bool enabled) {
    if (!MenuItem(label, shortcut, p_selected ? *p_selected : false, enabled)) return false;
    if (p_selected) *p_selected = !*p_selected;
    return true;
}
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
// InputText / InputTextWithHint with a callback: defined in OkayUI.cpp (they need the text field state).
bool IsWindowDocked() { const CurrentWindow w = GetCurrentWindow(); return w.valid && IsWindowDocked(w.title); }
WindowInfo::operator WindowRef*() const { return found ? FindWindowRefByName(Name) : nullptr; }
bool InvisibleButton(const char* id, Vec2 size, int) { return InvisibleButton(id, size.x, size.y); }
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

// ---- io.Fonts: recorded requests -------------------------------------------------------------------------------------------------------------------
namespace OkayUI {
namespace {
struct TtfEntry { const unsigned char* key = nullptr; std::unique_ptr<okay::TtfFont> font; };
TtfEntry g_ttfs[5]; int g_nTtfs = 0;
Font g_userFonts[32]; int g_nUserFonts = 0;
struct B85 { const char* array; const unsigned char* ttf; std::size_t size; };
B85 g_b85[8]; int g_nB85 = 0;
FontRequest g_fontReqs[32]; int g_nFontReqs = 0; ImFont g_fontPool[33]; int g_nFontPool = 0;
ImFont* Record(FontRequest::Kind kind, float px, const void* data, int size, const char* file, const ImFontConfig* cfg, ImFont* made) {
    ImFont* f = made;
    if (!f) { f = g_nFontPool < 33 ? &g_fontPool[g_nFontPool++] : &g_fontPool[32]; *f = *FontDefault(); }   // no real font: a copy of the default (PushFont keeps the look)
    if (g_nFontReqs < 32) {
        FontRequest& r = g_fontReqs[g_nFontReqs++];
        r = FontRequest(); r.kind = kind; r.px = px > 0.0f ? px : (cfg && cfg->SizePixels > 0.0f ? cfg->SizePixels : 13.0f); r.data = data; r.dataSize = size; r.merge = cfg && cfg->MergeMode; r.font = f;
        if (file) std::snprintf(r.file, sizeof r.file, "%s", file);
    }
    return f;
}
FontAtlasRef g_atlasRef;
}
Font* AddFontTtf(const unsigned char* data, std::size_t size, float px, const char* name) {
    if (!data || size < 12 || !(px >= 4.0f && px <= 512.0f) || g_nUserFonts >= 32) return nullptr;
    TtfEntry* e = nullptr;
    for (int i = 0; i < g_nTtfs; ++i) if (g_ttfs[i].key == data) e = &g_ttfs[i];
    if (!e) {
        if (g_nTtfs >= 5) return nullptr;
        auto f = std::make_unique<okay::TtfFont>();
        if (!f->LoadFromMemory(data, size, 48.0f, name ? name : "(user font)")) return nullptr;
        g_ttfs[g_nTtfs].key = data; g_ttfs[g_nTtfs].font = std::move(f); e = &g_ttfs[g_nTtfs++];
    }
    Font& u = g_userFonts[g_nUserFonts++];
    u = *FontDefault(); u.ttf = e->font.get(); u.px = px;
    return &u;
}
namespace {
std::vector<std::vector<unsigned char>>& KeptFontFiles() { static std::vector<std::vector<unsigned char>> keep; return keep; }
}
std::size_t UserFontMemoryBytes() {
    std::size_t n = 0;
    for (int i = 0; i < g_nTtfs; ++i) if (g_ttfs[i].font) n += g_ttfs[i].font->MemoryBytes();
    for (const auto& f : KeptFontFiles()) n += f.capacity();
    return n;
}
Font* AddFontTtfFile(const char* path, float px) {
    if (!path || !*path) return nullptr;
    FILE* fp = std::fopen(path, "rb");
    if (!fp) return nullptr;
    auto& keep = KeptFontFiles();   // the bytes of files read here (the font copies them; the key must stay unique per file)
    std::vector<unsigned char> b; unsigned char tmp[4096]; std::size_t n;
    while ((n = std::fread(tmp, 1, sizeof tmp, fp)) > 0 && b.size() < (64u << 20)) b.insert(b.end(), tmp, tmp + n);
    std::fclose(fp);
    if (keep.size() >= 8) return nullptr;
    keep.push_back(std::move(b));
    return AddFontTtf(keep.back().data(), keep.back().size(), px, path);
}
void RegisterBase85FontSource(const char* array, const unsigned char* ttf, std::size_t size) {
    if (!array || !ttf || size == 0) return;
    for (int i = 0; i < g_nB85; ++i) if (g_b85[i].array == array) { g_b85[i] = B85{array, ttf, size}; return; }
    if (g_nB85 < 8) g_b85[g_nB85++] = B85{array, ttf, size};
}
ImFont* FontAtlasRef::AddFontDefault(const ImFontConfig* cfg) { return Record(FontRequest::Default, 13.0f, nullptr, 0, nullptr, cfg, nullptr); }
// OkayUI never merges one font into another (text a font lacks is drawn by the fallback font, okay::FontForText), so a MergeMode
// request is only recorded: loading it would read the file, copy it and bake an atlas that nothing draws (the editor's CJK merge and
// the launcher's emoji merge held 2 copies of a 10 to 20 MB system font each, for nothing).
static bool Merging(const ImFontConfig* cfg) { return cfg && cfg->MergeMode; }
ImFont* FontAtlasRef::AddFontFromMemoryTTF(void* data, int size, float px, const ImFontConfig* cfg, const unsigned short*) {
    return Record(FontRequest::FromMemoryTTF, px, data, size, nullptr, cfg,
                  size > 0 && !Merging(cfg) ? AddFontTtf(static_cast<const unsigned char*>(data), (std::size_t)size, px) : nullptr);
}
ImFont* FontAtlasRef::AddFontFromMemoryCompressedBase85TTF(const char* data, float px, const ImFontConfig* cfg, const unsigned short*) {
    ImFont* made = nullptr;
    for (int i = 0; i < g_nB85 && !Merging(cfg); ++i) if (g_b85[i].array == data) made = AddFontTtf(g_b85[i].ttf, g_b85[i].size, px > 0.0f ? px : (cfg ? cfg->SizePixels : 0.0f));
    return Record(FontRequest::FromCompressedBase85, px, data, data ? (int)std::strlen(data) : 0, nullptr, cfg, made);
}
ImFont* FontAtlasRef::AddFontFromFileTTF(const char* file, float px, const ImFontConfig* cfg, const unsigned short*) {
    return Record(FontRequest::FromFile, px, nullptr, 0, file, cfg, Merging(cfg) ? nullptr : AddFontTtfFile(file, px));
}
void FontAtlasRef::Clear() { g_nFontReqs = 0; g_nFontPool = 0; }
int FontAtlasRef::FontCount() const { return g_nFontPool + g_nUserFonts; }
FontAtlasRef* GetFontAtlasRef() { return &g_atlasRef; }
const FontRequest* FontRequests(int* count) { if (count) *count = g_nFontReqs; return g_fontReqs; }
}   // namespace OkayUI

namespace OkayUI { void ColorConvertHSVtoRGB(float h, float s, float v, float& r, float& g, float& b) { ColorConvertHSVtoRGB(h, s, v, &r, &g, &b); } }
namespace OkayUI { void Columns(int count, const char*, bool) { Columns(count); } }

// ---- What belongs to one context (OkayUI_ContextState.hpp: each context keeps its own copy of these) ----
namespace OkayUI {
OKAYUI_CONTEXT_STATE(
    OKAYUI_CTX_VAR(g_flagStack),
    OKAYUI_CTX_VAR(g_flagTop),
    OKAYUI_CTX_VAR(g_fontBefore),
    OKAYUI_CTX_VAR(g_fontStack),
    OKAYUI_CTX_VAR(g_fontTop),
    OKAYUI_CTX_VAR(g_keyOwner),
    OKAYUI_CTX_VAR(g_keyOwnerFrame),
    OKAYUI_CTX_VAR(g_mainViewport),
    OKAYUI_CTX_VAR(g_newFrameClock),
    OKAYUI_CTX_VAR(g_store))
} // namespace OkayUI
