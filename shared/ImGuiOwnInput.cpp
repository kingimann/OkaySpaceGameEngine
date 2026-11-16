// See ImGuiOwnInput.hpp. Plain logic, no Dear ImGui, no operating-system call.
#include "ImGuiOwnInput.hpp"

#include <algorithm>
#include <cmath>

namespace okay::imguibackend {
using okay::platform::EventType;
using okay::platform::Key;

const char* UiKeyName(UiKey k) {
    switch (k) {
#define OKAY_X(ours, im) case UiKey::ours: return #ours;
    OKAY_UIKEYS(OKAY_X)
#undef OKAY_X
    case UiKey::ModCtrl: return "ModCtrl";
    case UiKey::ModShift: return "ModShift";
    case UiKey::ModAlt: return "ModAlt";
    case UiKey::ModSuper: return "ModSuper";
    default: return "";
    }
}

UiKey UiKeyFromPlatform(Key k) {
    const int v = (int)k;
    if (v >= (int)Key::A && v <= (int)Key::Z) return (UiKey)((int)UiKey::A + (v - (int)Key::A));
    if (v >= (int)Key::N1 && v <= (int)Key::N9) return (UiKey)((int)UiKey::N1 + (v - (int)Key::N1));
    if (k == Key::N0) return UiKey::N0;
    if (v >= (int)Key::F1 && v <= (int)Key::F12) return (UiKey)((int)UiKey::F1 + (v - (int)Key::F1));
    if (v >= (int)Key::Kp1 && v <= (int)Key::Kp9) return (UiKey)((int)UiKey::Keypad1 + (v - (int)Key::Kp1));
    switch (k) {
    case Key::Return: return UiKey::Enter;
    case Key::Escape: return UiKey::Escape;
    case Key::Backspace: return UiKey::Backspace;
    case Key::Tab: return UiKey::Tab;
    case Key::Space: return UiKey::Space;
    case Key::Minus: return UiKey::Minus;
    case Key::Equals: return UiKey::Equal;
    case Key::LeftBracket: return UiKey::LeftBracket;
    case Key::RightBracket: return UiKey::RightBracket;
    case Key::Backslash: return UiKey::Backslash;
    case Key::Semicolon: return UiKey::Semicolon;
    case Key::Apostrophe: return UiKey::Apostrophe;
    case Key::Grave: return UiKey::GraveAccent;
    case Key::Comma: return UiKey::Comma;
    case Key::Period: return UiKey::Period;
    case Key::Slash: return UiKey::Slash;
    case Key::CapsLock: return UiKey::CapsLock;
    case Key::ScrollLock: return UiKey::ScrollLock;
    case Key::NumLock: return UiKey::NumLock;
    case Key::PrintScreen: return UiKey::PrintScreen;
    case Key::Pause: return UiKey::Pause;
    case Key::Insert: return UiKey::Insert;
    case Key::Delete: return UiKey::Delete;
    case Key::Home: return UiKey::Home;
    case Key::End: return UiKey::End;
    case Key::PageUp: return UiKey::PageUp;
    case Key::PageDown: return UiKey::PageDown;
    case Key::Right: return UiKey::RightArrow;
    case Key::Left: return UiKey::LeftArrow;
    case Key::Down: return UiKey::DownArrow;
    case Key::Up: return UiKey::UpArrow;
    case Key::Kp0: return UiKey::Keypad0;
    case Key::KpPeriod: return UiKey::KeypadDecimal;
    case Key::KpDivide: return UiKey::KeypadDivide;
    case Key::KpMultiply: return UiKey::KeypadMultiply;
    case Key::KpMinus: return UiKey::KeypadSubtract;
    case Key::KpPlus: return UiKey::KeypadAdd;
    case Key::KpEnter: return UiKey::KeypadEnter;
    case Key::LCtrl: return UiKey::LeftCtrl;
    case Key::LShift: return UiKey::LeftShift;
    case Key::LAlt: return UiKey::LeftAlt;
    case Key::LGui: return UiKey::LeftSuper;
    case Key::RCtrl: return UiKey::RightCtrl;
    case Key::RShift: return UiKey::RightShift;
    case Key::RAlt: return UiKey::RightAlt;
    case Key::RGui: return UiKey::RightSuper;
    default: return UiKey::None;
    }
}

int ImGuiButtonFromPlatform(int b) {
    switch (b) {
    case okay::platform::MouseLeft: return 0;
    case okay::platform::MouseRight: return 1;
    case okay::platform::MouseMiddle: return 2;
    case okay::platform::MouseX1: return 3;
    case okay::platform::MouseX2: return 4;
    default: return -1;
    }
}

void InputTranslator::SyncMods(std::uint32_t mods, std::vector<InputOp>& out) {
    struct M { std::uint32_t bit; UiKey key; };
    static const M kMods[] = {{okay::platform::ModCtrl, UiKey::ModCtrl}, {okay::platform::ModShift, UiKey::ModShift},
                              {okay::platform::ModAlt, UiKey::ModAlt}, {okay::platform::ModGui, UiKey::ModSuper}};
    for (const M& m : kMods) {
        const bool now = (mods & m.bit) != 0, was = (m_mods & m.bit) != 0;
        if (!m_haveMods || now != was) { InputOp op; op.kind = InputOp::Key; op.key = m.key; op.down = now; out.push_back(op); }
    }
    m_mods = mods & (okay::platform::ModCtrl | okay::platform::ModShift | okay::platform::ModAlt | okay::platform::ModGui);
    m_haveMods = true;
}

void InputTranslator::Feed(const okay::platform::Event& e, std::vector<InputOp>& out) {
    if (m_window && e.windowId && e.windowId != m_window) return;
    InputOp op;
    switch (e.type) {
    case EventType::KeyDown: case EventType::KeyUp: {
        const UiKey k = UiKeyFromPlatform(e.key);
        SyncMods(e.mods, out);
        if (k == UiKey::None) return;
        op.kind = InputOp::Key; op.key = k; op.down = e.type == EventType::KeyDown;
        out.push_back(op);
        return;
    }
    case EventType::TextInput:
        if (e.text.empty()) return;
        op.kind = InputOp::Text; op.text = e.text;
        out.push_back(op);
        return;
    case EventType::MouseMove:
        op.kind = InputOp::MousePos; op.x = (float)e.x * m_sx; op.y = (float)e.y * m_sy;
        out.push_back(op);
        return;
    case EventType::MouseButtonDown: case EventType::MouseButtonUp: {
        const int b = ImGuiButtonFromPlatform(e.button);
        if (b < 0) return;
        InputOp pos; pos.kind = InputOp::MousePos; pos.x = (float)e.x * m_sx; pos.y = (float)e.y * m_sy;
        out.push_back(pos);   // the position of the click first: a press with no earlier move lands where it happened
        op.kind = InputOp::MouseButton; op.button = b; op.down = e.type == EventType::MouseButtonDown;
        out.push_back(op);
        return;
    }
    case EventType::MouseWheel:
        op.kind = InputOp::MouseWheel; op.x = e.wheelX; op.y = e.wheelY;
        out.push_back(op);
        return;
    case EventType::WindowFocusGained: case EventType::WindowFocusLost:
        op.kind = InputOp::Focus; op.down = e.type == EventType::WindowFocusGained;
        out.push_back(op);
        if (!op.down) m_haveMods = false;   // after losing the focus the modifiers are unknown: send them again
        return;
    default: return;
    }
}

DisplayMetrics ComputeDisplay(int ww, int wh, int dw, int dh) {
    DisplayMetrics m;
    if (ww <= 0 || wh <= 0) return m;
    m.width = (float)ww; m.height = (float)wh;
    m.scaleX = dw > 0 ? (float)dw / (float)ww : 1.0f;
    m.scaleY = dh > 0 ? (float)dh / (float)wh : 1.0f;
    return m;
}

float FrameTimer::Next(std::uint64_t counter, std::uint64_t freq) {
    if (!m_have || freq == 0) { m_have = freq != 0; m_last = counter; return 1.0f / 60.0f; }
    if (counter <= m_last) { m_last = counter; return 1.0f / 1000000.0f; }
    const double dt = (double)(counter - m_last) / (double)freq;
    m_last = counter;
    return dt > 1.0 ? 1.0f : (float)std::max(dt, 1e-6);
}

CursorRequest MapCursor(int c) {
    using S = okay::platform::CursorShape;
    CursorRequest r;
    if (c == -1) { r.visible = false; return r; }
    static const S kShapes[] = {S::Arrow, S::TextInput, S::ResizeAll, S::ResizeNS, S::ResizeEW, S::ResizeNESW, S::ResizeNWSE, S::Hand, S::NotAllowed};
    r.shape = c >= 0 && c <= 8 ? kShapes[c] : S::Arrow;
    return r;
}

const char* ClipboardCache::Get() {
    m_text = m_get ? m_get() : std::string();
    return m_text.empty() ? nullptr : m_text.c_str();
}
void ClipboardCache::Set(const char* text) { if (m_set) m_set(text ? std::string(text) : std::string()); }

bool ClipToRect(float x0, float y0, float x1, float y1, int dw, int dh, okay::ui2d::Rect& out) {
    if (!(x0 == x0 && y0 == y0 && x1 == x1 && y1 == y1)) return false;   // NaN
    const float fx0 = std::max(0.0f, std::floor(x0)), fy0 = std::max(0.0f, std::floor(y0));
    const float fx1 = std::min((float)dw, std::ceil(x1)), fy1 = std::min((float)dh, std::ceil(y1));
    if (!(fx1 > fx0) || !(fy1 > fy0)) return false;
    out.x = (int)fx0; out.y = (int)fy0; out.w = (int)(fx1 - fx0); out.h = (int)(fy1 - fy0);
    return true;
}

void ConvertVertices(const DrawVertexIn* in, int count, std::vector<okay::ui2d::Vertex>& out) {
    out.resize(count > 0 ? (size_t)count : 0);
    for (int i = 0; i < count; ++i) {
        okay::ui2d::Vertex& v = out[(size_t)i];
        v.position = {in[i].x, in[i].y};
        v.tex_coord = {in[i].u, in[i].v};
        v.color = {(std::uint8_t)(in[i].col & 0xFF), (std::uint8_t)((in[i].col >> 8) & 0xFF),
                   (std::uint8_t)((in[i].col >> 16) & 0xFF), (std::uint8_t)((in[i].col >> 24) & 0xFF)};
    }
}

void ConvertIndices(const std::uint16_t* i16, const std::uint32_t* i32, int count, std::vector<int>& out) {
    out.resize(count > 0 ? (size_t)count : 0);
    for (int i = 0; i < count; ++i) out[(size_t)i] = i16 ? (int)i16[i] : (int)i32[i];
}

void Alpha8ToRgba(const std::uint8_t* src, int pitch, int x, int y, int w, int h, std::vector<std::uint8_t>& out) {
    out.assign(w > 0 && h > 0 ? (size_t)w * (size_t)h * 4 : 0, 255);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) out[((size_t)j * (size_t)w + (size_t)i) * 4 + 3] = src[(size_t)(y + j) * (size_t)pitch + (size_t)(x + i)];
}

} // namespace okay::imguibackend
