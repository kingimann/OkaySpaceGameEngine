// The launcher's own Dear ImGui renderer and platform glue. See ImGuiOwnBackend.hpp.
//
// Written from imgui.h's documented contract only:
//  * ImDrawData: CmdLists of ImDrawList, each with VtxBuffer / IdxBuffer and CmdBuffer; a command draws ElemCount
//    indices from IdxOffset, relative to VtxOffset, with texture GetTexID(), clipped to ClipRect (in ImGui's
//    coordinates: subtract DisplayPos, multiply by FramebufferScale). A UserCallback replaces the draw.
//  * ImTextureData (ImGuiBackendFlags_RendererHasTextures): honour WantCreate / WantUpdates / WantDestroy, then call
//    SetTexID and SetStatus.
//  * ImGuiIO input events: AddMousePosEvent, AddMouseButtonEvent, AddMouseWheelEvent (y > 0 up, x > 0 left),
//    AddKeyEvent, AddInputCharactersUTF8, AddFocusEvent; DisplaySize, DisplayFramebufferScale, DeltaTime.
#include "ImGuiOwnBackend.hpp"

#include "imgui.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace okay::launchergfx {

namespace plat = okay::platform;

namespace {
// Our marker for ImGui's "reset render state" request: nothing to reset (every command sets its own clip and texture).
void ResetRenderState(const ImDrawList*, const ImDrawCmd*) {}

inline okay::ui2d::Tex TexOf(ImTextureID id) { return (okay::ui2d::Tex)(std::uintptr_t)id; }
} // namespace

// ---- renderer ---------------------------------------------------------------------------------------

ImGuiPainterRenderer::ImGuiPainterRenderer(plat::Painter& p) : m_p(p) {
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "okay_painter";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Renderer_TextureMaxWidth = pio.Renderer_TextureMaxHeight = 8192;   // what every painter here can hold
    pio.DrawCallback_ResetRenderState = ResetRenderState;
}

ImGuiPainterRenderer::~ImGuiPainterRenderer() {
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    for (ImTextureData* t : pio.Textures) {
        if (t->RefCount != 1) continue;   // shared with another context: not ours to free
        if (t->TexID != ImTextureID_Invalid) m_p.DestroyTexture(TexOf(t->TexID));
        t->SetTexID(ImTextureID_Invalid);
        t->SetStatus(ImTextureStatus_Destroyed);
    }
    pio.DrawCallback_ResetRenderState = nullptr;
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
}

void ImGuiPainterRenderer::UpdateTexture(ImTextureData* t) {
    // RGBA32 goes up as it is; Alpha8 becomes white with that alpha (what ImGui's "alpha only" texture means).
    auto rgbaRows = [&](int x, int y, int w, int h, const std::uint8_t*& px, int& pitch) {
        if (t->Format == ImTextureFormat_RGBA32) {
            px = (const std::uint8_t*)t->GetPixelsAt(x, y);
            pitch = t->GetPitch();
            return;
        }
        m_rgba.resize((std::size_t)w * (std::size_t)h * 4);
        for (int r = 0; r < h; ++r) {
            const std::uint8_t* a = (const std::uint8_t*)t->GetPixelsAt(x, y + r);
            std::uint8_t* d = &m_rgba[(std::size_t)r * (std::size_t)w * 4];
            for (int c = 0; c < w; ++c) { d[c * 4] = d[c * 4 + 1] = d[c * 4 + 2] = 255; d[c * 4 + 3] = a[c]; }
        }
        px = m_rgba.data();
        pitch = w * 4;
    };
    if (t->Status == ImTextureStatus_WantCreate) {
        plat::TextureDesc d;
        d.width = t->Width; d.height = t->Height;
        d.blend = plat::BlendMode::Blend;
        d.scale = plat::ScaleMode::Linear;   // glyphs are oversampled: sample between texels
        okay::ui2d::Tex h = m_p.CreateTexture(d, nullptr);
        if (h) {
            const std::uint8_t* px = nullptr; int pitch = 0;
            rgbaRows(0, 0, t->Width, t->Height, px, pitch);
            m_p.UpdateTexture(h, nullptr, px, pitch);
        }
        t->SetTexID((ImTextureID)(std::uintptr_t)h);
        t->SetStatus(h ? ImTextureStatus_OK : ImTextureStatus_Destroyed);
        return;
    }
    if (t->Status == ImTextureStatus_WantUpdates) {
        okay::ui2d::Tex h = TexOf(t->TexID);
        for (const ImTextureRect& r : t->Updates) {
            const std::uint8_t* px = nullptr; int pitch = 0;
            rgbaRows(r.x, r.y, r.w, r.h, px, pitch);
            const okay::ui2d::Rect rc{r.x, r.y, r.w, r.h};
            m_p.UpdateTexture(h, &rc, px, pitch);
        }
        t->SetStatus(ImTextureStatus_OK);
        return;
    }
    if (t->Status == ImTextureStatus_WantDestroy && t->UnusedFrames > 0) {
        if (t->TexID != ImTextureID_Invalid) m_p.DestroyTexture(TexOf(t->TexID));
        t->SetTexID(ImTextureID_Invalid);
        t->SetStatus(ImTextureStatus_Destroyed);
    }
}

void ImGuiPainterRenderer::Render(ImDrawData* dd) {
    m_lastCmds = 0;
    if (!dd) return;
    if (dd->Textures)
        for (ImTextureData* t : *dd->Textures)
            if (t->Status != ImTextureStatus_OK) UpdateTexture(t);

    const ImVec2 off = dd->DisplayPos, sc = dd->FramebufferScale;
    const float fbW = dd->DisplaySize.x * sc.x, fbH = dd->DisplaySize.y * sc.y;
    if (!(fbW > 0 && fbH > 0)) return;
    m_p.SetScale(1.0f, 1.0f);   // we hand the painter framebuffer pixels
    const ImDrawCallback reset = ImGui::GetPlatformIO().DrawCallback_ResetRenderState;

    for (const ImDrawList* list : dd->CmdLists) {
        // The vertices of the whole list, once, in framebuffer pixels.
        const int nv = list->VtxBuffer.Size;
        m_vtx.resize((std::size_t)nv);
        for (int i = 0; i < nv; ++i) {
            const ImDrawVert& s = list->VtxBuffer.Data[i];
            okay::ui2d::Vertex& d = m_vtx[(std::size_t)i];
            d.position = {(s.pos.x - off.x) * sc.x, (s.pos.y - off.y) * sc.y};
            // IM_COL32 packs R in the low byte, then G, B, A.
            d.color = {(std::uint8_t)(s.col >> IM_COL32_R_SHIFT), (std::uint8_t)(s.col >> IM_COL32_G_SHIFT),
                       (std::uint8_t)(s.col >> IM_COL32_B_SHIFT), (std::uint8_t)(s.col >> IM_COL32_A_SHIFT)};
            d.tex_coord = {s.uv.x, s.uv.y};
        }
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) {
                if (cmd.UserCallback != reset) cmd.UserCallback(list, &cmd);
                continue;
            }
            if (cmd.ElemCount == 0) continue;
            // Clip rectangle in framebuffer pixels. A pixel is drawn when its centre is inside, so the edges round to
            // the nearest pixel boundary; then it is cut to the framebuffer.
            float x0 = (cmd.ClipRect.x - off.x) * sc.x, y0 = (cmd.ClipRect.y - off.y) * sc.y;
            float x1 = (cmd.ClipRect.z - off.x) * sc.x, y1 = (cmd.ClipRect.w - off.y) * sc.y;
            x0 = x0 < 0 ? 0 : x0; y0 = y0 < 0 ? 0 : y0;
            x1 = x1 > fbW ? fbW : x1; y1 = y1 > fbH ? fbH : y1;
            const int ix0 = (int)std::ceil(x0 - 0.5f), iy0 = (int)std::ceil(y0 - 0.5f);
            const int ix1 = (int)std::ceil(x1 - 0.5f), iy1 = (int)std::ceil(y1 - 0.5f);
            if (ix1 <= ix0 || iy1 <= iy0) continue;
            const okay::ui2d::Rect clip{ix0, iy0, ix1 - ix0, iy1 - iy0};
            m_p.SetClipRect(&clip);
            // Indices, relative to VtxOffset (the painter takes ints).
            const ImDrawIdx* src = list->IdxBuffer.Data + cmd.IdxOffset;
            m_idx.resize(cmd.ElemCount);
            for (unsigned i = 0; i < cmd.ElemCount; ++i) m_idx[i] = (int)src[i];
            const int base = (int)cmd.VtxOffset;
            if (base >= nv) continue;
            m_p.Geometry(TexOf(cmd.GetTexID()), m_vtx.data() + base, nv - base, m_idx.data(), (int)cmd.ElemCount);
            ++m_lastCmds;
        }
    }
    m_p.SetClipRect(nullptr);
}

// ---- platform glue ------------------------------------------------------------------------------------

int ImGuiKeyFor(plat::Key key) {
    const int k = (int)key;
    if (k >= 4 && k <= 29) return ImGuiKey_A + (k - 4);              // letters
    if (k >= 30 && k <= 38) return ImGuiKey_1 + (k - 30);            // 1..9
    if (k == 39) return ImGuiKey_0;
    if (k >= 58 && k <= 69) return ImGuiKey_F1 + (k - 58);           // F1..F12
    if (k >= 104 && k <= 115) return ImGuiKey_F13 + (k - 104);       // F13..F24
    if (k >= 89 && k <= 97) return ImGuiKey_Keypad1 + (k - 89);      // keypad 1..9
    switch (k) {
    case 98: return ImGuiKey_Keypad0;
    case 99: return ImGuiKey_KeypadDecimal;
    case 84: return ImGuiKey_KeypadDivide;
    case 85: return ImGuiKey_KeypadMultiply;
    case 86: return ImGuiKey_KeypadSubtract;
    case 87: return ImGuiKey_KeypadAdd;
    case 88: return ImGuiKey_KeypadEnter;
    case 103: return ImGuiKey_KeypadEqual;
    case 40: return ImGuiKey_Enter;
    case 41: return ImGuiKey_Escape;
    case 42: return ImGuiKey_Backspace;
    case 43: return ImGuiKey_Tab;
    case 44: return ImGuiKey_Space;
    case 45: return ImGuiKey_Minus;
    case 46: return ImGuiKey_Equal;
    case 47: return ImGuiKey_LeftBracket;
    case 48: return ImGuiKey_RightBracket;
    case 49: return ImGuiKey_Backslash;
    case 51: return ImGuiKey_Semicolon;
    case 52: return ImGuiKey_Apostrophe;
    case 53: return ImGuiKey_GraveAccent;
    case 54: return ImGuiKey_Comma;
    case 55: return ImGuiKey_Period;
    case 56: return ImGuiKey_Slash;
    case 57: return ImGuiKey_CapsLock;
    case 70: return ImGuiKey_PrintScreen;
    case 71: return ImGuiKey_ScrollLock;
    case 72: return ImGuiKey_Pause;
    case 73: return ImGuiKey_Insert;
    case 74: return ImGuiKey_Home;
    case 75: return ImGuiKey_PageUp;
    case 76: return ImGuiKey_Delete;
    case 77: return ImGuiKey_End;
    case 78: return ImGuiKey_PageDown;
    case 79: return ImGuiKey_RightArrow;
    case 80: return ImGuiKey_LeftArrow;
    case 81: return ImGuiKey_DownArrow;
    case 82: return ImGuiKey_UpArrow;
    case 83: return ImGuiKey_NumLock;
    case 100: return ImGuiKey_Oem102;
    case 101: return ImGuiKey_Menu;
    case 224: return ImGuiKey_LeftCtrl;
    case 225: return ImGuiKey_LeftShift;
    case 226: return ImGuiKey_LeftAlt;
    case 227: return ImGuiKey_LeftSuper;
    case 228: return ImGuiKey_RightCtrl;
    case 229: return ImGuiKey_RightShift;
    case 230: return ImGuiKey_RightAlt;
    case 231: return ImGuiKey_RightSuper;
    default: return ImGuiKey_None;
    }
}

ImGuiPlatformGlue::ImGuiPlatformGlue(plat::Backend& b, plat::Window& w) : m_b(b), m_w(w) {
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "okay_platform";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_GetClipboardTextFn = GetClipboard;
    pio.Platform_SetClipboardTextFn = SetClipboard;
    pio.Platform_ClipboardUserData = this;
    m_lastTicks = m_b.PerfCounter();
    m_b.StopTextInput();   // on only while an ImGui text field wants it (NewFrame)
}

ImGuiPlatformGlue::~ImGuiPlatformGlue() {
    if (m_textOn) m_b.StopTextInput();
    m_b.SetSystemCursor(plat::SystemCursor::Arrow);
    m_b.ShowCursor(true);
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_GetClipboardTextFn = nullptr;
    pio.Platform_SetClipboardTextFn = nullptr;
    pio.Platform_ClipboardUserData = nullptr;
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = nullptr;
    io.BackendFlags &= ~ImGuiBackendFlags_HasMouseCursors;
}

const char* ImGuiPlatformGlue::GetClipboard(ImGuiContext*) {
    ImGuiPlatformGlue* g = (ImGuiPlatformGlue*)ImGui::GetPlatformIO().Platform_ClipboardUserData;
    if (!g) return nullptr;
    g->m_clip = g->m_b.GetClipboardText();
    return g->m_clip.c_str();
}

void ImGuiPlatformGlue::SetClipboard(ImGuiContext*, const char* text) {
    ImGuiPlatformGlue* g = (ImGuiPlatformGlue*)ImGui::GetPlatformIO().Platform_ClipboardUserData;
    if (g) g->m_b.SetClipboardText(text ? text : "");
}

bool ImGuiPlatformGlue::ProcessEvent(const plat::Event& e) {
    if (e.windowId != 0 && e.windowId != m_w.Id()) return false;
    ImGuiIO& io = ImGui::GetIO();
    switch (e.type) {
    case plat::EventType::MouseMove:
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMousePosEvent((float)e.x, (float)e.y);
        m_mouseInside = true;
        return true;
    case plat::EventType::MouseButtonDown:
    case plat::EventType::MouseButtonUp: {
        // ImGui numbers buttons 0 = left, 1 = right, 2 = middle, then the two extra ones.
        int b = -1;
        switch (e.button) {
        case plat::MouseLeft: b = 0; break;
        case plat::MouseRight: b = 1; break;
        case plat::MouseMiddle: b = 2; break;
        case plat::MouseX1: b = 3; break;
        case plat::MouseX2: b = 4; break;
        default: break;
        }
        if (b < 0) return false;
        const bool down = e.type == plat::EventType::MouseButtonDown;
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMousePosEvent((float)e.x, (float)e.y);
        io.AddMouseButtonEvent(b, down);
        if (m_log) std::fprintf(stderr, "launcher-input: mouse %d %s at %d,%d\n", b, down ? "down" : "up", e.x, e.y);
        return true;
    }
    case plat::EventType::MouseWheel:
        // Ours: y > 0 = up, x > 0 = right. ImGui's: y > 0 = up, x > 0 = left.
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMouseWheelEvent(-e.wheelX, e.wheelY);
        return true;
    case plat::EventType::KeyDown:
    case plat::EventType::KeyUp: {
        const bool down = e.type == plat::EventType::KeyDown;
        io.AddKeyEvent(ImGuiMod_Ctrl, (e.mods & plat::ModCtrl) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (e.mods & plat::ModShift) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (e.mods & plat::ModAlt) != 0);
        io.AddKeyEvent(ImGuiMod_Super, (e.mods & plat::ModGui) != 0);
        const int k = ImGuiKeyFor(e.key);
        if (k != ImGuiKey_None) io.AddKeyEvent((ImGuiKey)k, down);
        if (m_log) std::fprintf(stderr, "launcher-input: key %d %s\n", (int)e.key, down ? "down" : "up");
        return true;
    }
    case plat::EventType::TextInput:
        io.AddInputCharactersUTF8(e.text.c_str());
        if (m_log) std::fprintf(stderr, "launcher-input: text \"%s\"\n", e.text.c_str());
        return true;
    case plat::EventType::WindowFocusGained: io.AddFocusEvent(true); return true;
    case plat::EventType::WindowFocusLost: io.AddFocusEvent(false); return true;
    default: return false;
    }
}

void ImGuiPlatformGlue::NewFrame() {
    ImGuiIO& io = ImGui::GetIO();
    int w = 0, h = 0, dw = 0, dh = 0;
    m_w.GetSize(w, h);
    m_w.GetDrawableSize(dw, dh);
    if (m_w.IsMinimized()) w = h = 0;
    io.DisplaySize = ImVec2((float)w, (float)h);
    if (w > 0 && h > 0 && dw > 0 && dh > 0) io.DisplayFramebufferScale = ImVec2((float)dw / w, (float)dh / h);

    const std::uint64_t now = m_b.PerfCounter(), freq = m_b.PerfFrequency();
    double dt = freq ? (double)(now - m_lastTicks) / (double)freq : 1.0 / 60.0;
    if (!(dt > 0)) dt = 1.0 / 1000.0;   // ImGui needs a step above zero
    io.DeltaTime = (float)dt;
    m_lastTicks = now;

    // The pointer left the window: ImGui must stop hovering what it last saw.
    const bool inside = m_w.HasMouseFocus();
    if (m_mouseInside && !inside) io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    m_mouseInside = inside;

    // Text input only while a text field has the keyboard (IME candidates and on-screen keyboards follow it).
    if (io.WantTextInput != m_textOn) {
        m_textOn = io.WantTextInput;
        if (m_textOn) m_b.StartTextInput(); else m_b.StopTextInput();
    }

    if (!(io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)) {
        const int c = io.MouseDrawCursor ? (int)ImGuiMouseCursor_None : (int)ImGui::GetMouseCursor();
        if (c != m_cursor) {
            m_cursor = c;
            if (c == ImGuiMouseCursor_None) m_b.ShowCursor(false);
            else {
                plat::SystemCursor sc = plat::SystemCursor::Arrow;
                switch (c) {
                case ImGuiMouseCursor_TextInput: sc = plat::SystemCursor::TextInput; break;
                case ImGuiMouseCursor_ResizeAll: sc = plat::SystemCursor::ResizeAll; break;
                case ImGuiMouseCursor_ResizeNS: sc = plat::SystemCursor::ResizeNS; break;
                case ImGuiMouseCursor_ResizeEW: sc = plat::SystemCursor::ResizeEW; break;
                case ImGuiMouseCursor_ResizeNESW: sc = plat::SystemCursor::ResizeNESW; break;
                case ImGuiMouseCursor_ResizeNWSE: sc = plat::SystemCursor::ResizeNWSE; break;
                case ImGuiMouseCursor_Hand: sc = plat::SystemCursor::Hand; break;
                case ImGuiMouseCursor_Wait: case ImGuiMouseCursor_Progress: sc = plat::SystemCursor::Wait; break;
                case ImGuiMouseCursor_NotAllowed: sc = plat::SystemCursor::NotAllowed; break;
                default: break;
                }
                m_b.SetSystemCursor(sc);
                m_b.ShowCursor(true);
            }
        }
    }
}

} // namespace okay::launchergfx
