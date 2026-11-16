#pragma once
// The plain-logic half of our own Dear ImGui backend (docs/own_imgui_backend.md): everything that does not need
// Dear ImGui itself, so it is unit-tested headless (tests/test_imgui_own_input.cpp).
//
//   InputTranslator  okay::platform::Event  ->  a list of neutral input operations (key, modifier, mouse position,
//                    button, wheel, text, focus) that the adapter (ImGuiOwnBackend.cpp) hands to ImGuiIO.
//   ComputeDisplay   window size and drawable size -> display size and framebuffer scale.
//   FrameTimer       delta time from the platform's performance counter.
//   MapCursor        ImGui's mouse cursor number -> okay::platform::CursorShape (or hidden).
//   ClipboardCache   the "pointer valid until the next call, null on failure" clipboard contract.
//   TextInputGate    when to start and stop the platform's text input.
//   Draw helpers     clip rectangle, vertex and index conversion, alpha-only texture to RGBA.
//
// Written from the public documentation of Dear ImGui's input interface (the comments in imgui.h: io.AddKeyEvent,
// AddMousePosEvent, AddMouseButtonEvent (0 left, 1 right, 2 middle), AddMouseWheelEvent, AddInputCharactersUTF8,
// AddFocusEvent; ImGuiMouseCursor numbering; ImDrawVert / ImDrawCmd / ImTextureData semantics) and from
// okay::platform. Nothing taken from imgui_impl_sdl2.cpp, imgui_impl_sdlrenderer2.cpp or any other backend.
// Dear ImGui itself is not included here. No <mutex>/<thread> (CLAUDE.md 21a).
#include "okay/Platform/Platform.hpp"
#include "okay/UI/UIPainter.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace okay::imguibackend {

// DEF(ours, imguiSuffix): ImGuiKey_<imguiSuffix>. The adapter builds its ImGuiKey table from this same list.
#define OKAY_UIKEYS(DEF) \
    DEF(A,A) DEF(B,B) DEF(C,C) DEF(D,D) DEF(E,E) DEF(F,F) DEF(G,G) DEF(H,H) DEF(I,I) DEF(J,J) DEF(K,K) DEF(L,L) DEF(M,M) \
    DEF(N,N) DEF(O,O) DEF(P,P) DEF(Q,Q) DEF(R,R) DEF(S,S) DEF(T,T) DEF(U,U) DEF(V,V) DEF(W,W) DEF(X,X) DEF(Y,Y) DEF(Z,Z) \
    DEF(N0,0) DEF(N1,1) DEF(N2,2) DEF(N3,3) DEF(N4,4) DEF(N5,5) DEF(N6,6) DEF(N7,7) DEF(N8,8) DEF(N9,9) \
    DEF(F1,F1) DEF(F2,F2) DEF(F3,F3) DEF(F4,F4) DEF(F5,F5) DEF(F6,F6) DEF(F7,F7) DEF(F8,F8) DEF(F9,F9) DEF(F10,F10) DEF(F11,F11) DEF(F12,F12) \
    DEF(Enter,Enter) DEF(Escape,Escape) DEF(Backspace,Backspace) DEF(Tab,Tab) DEF(Space,Space) \
    DEF(Minus,Minus) DEF(Equal,Equal) DEF(LeftBracket,LeftBracket) DEF(RightBracket,RightBracket) DEF(Backslash,Backslash) \
    DEF(Semicolon,Semicolon) DEF(Apostrophe,Apostrophe) DEF(GraveAccent,GraveAccent) DEF(Comma,Comma) DEF(Period,Period) DEF(Slash,Slash) \
    DEF(CapsLock,CapsLock) DEF(ScrollLock,ScrollLock) DEF(NumLock,NumLock) DEF(PrintScreen,PrintScreen) DEF(Pause,Pause) \
    DEF(Insert,Insert) DEF(Delete,Delete) DEF(Home,Home) DEF(End,End) DEF(PageUp,PageUp) DEF(PageDown,PageDown) \
    DEF(RightArrow,RightArrow) DEF(LeftArrow,LeftArrow) DEF(DownArrow,DownArrow) DEF(UpArrow,UpArrow) \
    DEF(Keypad0,Keypad0) DEF(Keypad1,Keypad1) DEF(Keypad2,Keypad2) DEF(Keypad3,Keypad3) DEF(Keypad4,Keypad4) DEF(Keypad5,Keypad5) \
    DEF(Keypad6,Keypad6) DEF(Keypad7,Keypad7) DEF(Keypad8,Keypad8) DEF(Keypad9,Keypad9) DEF(KeypadDecimal,KeypadDecimal) \
    DEF(KeypadDivide,KeypadDivide) DEF(KeypadMultiply,KeypadMultiply) DEF(KeypadSubtract,KeypadSubtract) DEF(KeypadAdd,KeypadAdd) \
    DEF(KeypadEnter,KeypadEnter) \
    DEF(LeftCtrl,LeftCtrl) DEF(LeftShift,LeftShift) DEF(LeftAlt,LeftAlt) DEF(LeftSuper,LeftSuper) \
    DEF(RightCtrl,RightCtrl) DEF(RightShift,RightShift) DEF(RightAlt,RightAlt) DEF(RightSuper,RightSuper)

enum class UiKey : int {
    None = 0,
#define OKAY_X(ours, im) ours,
    OKAY_UIKEYS(OKAY_X)
#undef OKAY_X
    ModCtrl, ModShift, ModAlt, ModSuper,   // the modifier flags ImGui wants as separate key events (ImGuiMod_*)
    Count
};

/// The ImGui-side name of a key, for tests and logs ("A", "LeftArrow", "ModCtrl"); "" for None.
const char* UiKeyName(UiKey k);
/// Physical platform key -> UI key (None when ImGui has no such key).
UiKey UiKeyFromPlatform(okay::platform::Key k);
/// Platform mouse button (1 left, 2 middle, 3 right, 4, 5) -> ImGui button (0 left, 1 right, 2 middle, 3, 4); -1 unknown.
int ImGuiButtonFromPlatform(int platformButton);

struct InputOp {
    enum Kind { Key, MousePos, MouseButton, MouseWheel, Text, Focus } kind = Key;
    UiKey key = UiKey::None;
    bool down = false;       // Key, MouseButton, Focus (true = gained)
    float x = 0, y = 0;      // MousePos; MouseWheel (x, y)
    int button = 0;          // MouseButton (ImGui numbering)
    std::string text;        // Text (UTF-8)
};

/// Turns platform events into input operations, in the order ImGui wants them: modifier changes before the key or
/// mouse event they belong to; the position before a button press. Only events of `windowId` are used (0 = any).
/// Gamepad, touch and file-drop events are ignored here.
class InputTranslator {
public:
    void SetWindow(std::uint32_t windowId) { m_window = windowId; }
    /// Multiply mouse coordinates by this (1 = events are already in the units of the display size).
    void SetMouseScale(float sx, float sy) { m_sx = sx; m_sy = sy; }
    void Feed(const okay::platform::Event& e, std::vector<InputOp>& out);
    /// Forget held state (a new ImGui context): the next event re-sends the modifiers.
    void Reset() { m_mods = 0; m_haveMods = false; }
private:
    void SyncMods(std::uint32_t mods, std::vector<InputOp>& out);
    std::uint32_t m_window = 0;
    float m_sx = 1.0f, m_sy = 1.0f;
    std::uint32_t m_mods = 0;
    bool m_haveMods = false;
};

struct DisplayMetrics {
    float width = 0, height = 0;             // ImGuiIO::DisplaySize (window units)
    float scaleX = 1, scaleY = 1;            // ImGuiIO::DisplayFramebufferScale (drawable pixels per window unit)
};
/// A zero-sized (minimised) window gives size 0 and scale 1 so ImGui is not fed a division by zero.
DisplayMetrics ComputeDisplay(int windowW, int windowH, int drawableW, int drawableH);

/// Seconds since the previous frame from a performance counter. The first frame is 1/60; a counter that did not move
/// or went backwards gives 1/1000000 (ImGui asserts on a delta of 0 or less); a stall longer than 1 s is capped at 1 s.
class FrameTimer {
public:
    float Next(std::uint64_t counter, std::uint64_t frequency);
private:
    std::uint64_t m_last = 0;
    bool m_have = false;
};

struct CursorRequest { bool visible = true; okay::platform::CursorShape shape = okay::platform::CursorShape::Arrow; };
/// ImGuiMouseCursor_ number (-1 None, 0 Arrow, 1 TextInput, 2 ResizeAll, 3 ResizeNS, 4 ResizeEW, 5 ResizeNESW,
/// 6 ResizeNWSE, 7 Hand, 8 NotAllowed) to a request; an unknown number is the arrow.
CursorRequest MapCursor(int imguiCursor);

/// ImGui's clipboard contract: the text callback returns a pointer that stays valid until the next call and null on
/// failure (nothing or not text). `get` / `set` are the platform's (okay::platform::Backend::Get/SetClipboardText).
class ClipboardCache {
public:
    ClipboardCache(std::function<std::string()> get, std::function<bool(const std::string&)> set) : m_get(std::move(get)), m_set(std::move(set)) {}
    const char* Get();
    void Set(const char* text);
private:
    std::function<std::string()> m_get;
    std::function<bool(const std::string&)> m_set;
    std::string m_text;
};

/// io.WantTextInput -> platform text input. Calls start / stop only when the state changes.
class TextInputGate {
public:
    enum Action { Nothing, Start, Stop };
    Action Update(bool wantTextInput) {
        if (wantTextInput == m_on) return Nothing;
        m_on = wantTextInput;
        return m_on ? Start : Stop;
    }
    bool On() const { return m_on; }
private:
    bool m_on = false;
};

// ---- drawing ------------------------------------------------------------------------------------------------
/// ImDrawCmd::ClipRect (min / max in window units) to an integer rectangle inside the display, rounded outwards.
/// False when nothing is left (the command draws nothing).
bool ClipToRect(float minX, float minY, float maxX, float maxY, int displayW, int displayH, okay::ui2d::Rect& out);

/// The memory layout of ImDrawVert (checked by a static_assert in the adapter): position, texture coordinate, then a
/// colour packed as 0xAABBGGRR.
struct DrawVertexIn { float x, y, u, v; std::uint32_t col; };
void ConvertVertices(const DrawVertexIn* in, int count, std::vector<okay::ui2d::Vertex>& out);
/// Index buffer slice to int indices. `idx16` or `idx32` is used (the other is null).
void ConvertIndices(const std::uint16_t* idx16, const std::uint32_t* idx32, int count, std::vector<int>& out);
/// 8-bit alpha texture to RGBA8 white + alpha (what the painter textures hold). `rect` limits the copy to a region
/// of the source (x, y, w, h); the output is tightly packed w * h * 4.
void Alpha8ToRgba(const std::uint8_t* src, int srcPitch, int x, int y, int w, int h, std::vector<std::uint8_t>& out);

} // namespace okay::imguibackend
