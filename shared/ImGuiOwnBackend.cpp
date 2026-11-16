// See ImGuiOwnBackend.hpp. Written from Dear ImGui's public API (imgui.h) and okay::platform.
#include "ImGuiOwnBackend.hpp"

#include "imgui.h"

#include <cstring>
#include <memory>

namespace okay::imguibackend {
namespace {

OwnBackend* g_self = nullptr;
std::unique_ptr<ClipboardCache> g_clip;

static_assert(sizeof(DrawVertexIn) == sizeof(ImDrawVert), "ImDrawVert layout (pos, uv, col) changed");
static_assert(sizeof(okay::ui2d::Vertex) == 20, "ui2d::Vertex layout");

ImGuiKey ToImGuiKey(UiKey k) {
    switch (k) {
#define OKAY_X(ours, im) case UiKey::ours: return ImGuiKey_##im;
    OKAY_UIKEYS(OKAY_X)
#undef OKAY_X
    case UiKey::ModCtrl: return ImGuiMod_Ctrl;
    case UiKey::ModShift: return ImGuiMod_Shift;
    case UiKey::ModAlt: return ImGuiMod_Alt;
    case UiKey::ModSuper: return ImGuiMod_Super;
    default: return ImGuiKey_None;
    }
}

const char* GetClip(ImGuiContext*) { return g_clip ? g_clip->Get() : nullptr; }
void SetClip(ImGuiContext*, const char* t) { if (g_clip) g_clip->Set(t); }

} // namespace

bool OwnBackend::Init(okay::platform::Backend* backend, okay::platform::Window* window, okay::platform::Painter* painter) {
    if (m_inited || !painter || !ImGui::GetCurrentContext()) return false;
    m_backend = backend; m_window = window; m_painter = painter;
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "okay::platform";
    io.BackendRendererName = "okay::platform::Painter";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    if (backend) {
        g_clip.reset(new ClipboardCache([backend] { return backend->GetClipboardText(); },
                                        [backend](const std::string& s) { return backend->SetClipboardText(s); }));
        ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        pio.Platform_GetClipboardTextFn = &GetClip;
        pio.Platform_SetClipboardTextFn = &SetClip;
    }
    g_self = this;
    m_input.Reset();
    m_input.SetWindow(window ? window->Id() : 0);
    m_inited = true;
    return true;
}

void OwnBackend::Shutdown() {
    if (!m_inited) return;
    if (ImGui::GetCurrentContext()) {
        for (ImTextureData* t : ImGui::GetPlatformIO().Textures) if (t->RefCount == 1 || t->BackendUserData) DestroyTexture(t);
        ImGuiIO& io = ImGui::GetIO();
        io.BackendPlatformName = io.BackendRendererName = nullptr;
        io.BackendFlags &= ~(ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
        ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        pio.Platform_GetClipboardTextFn = nullptr;
        pio.Platform_SetClipboardTextFn = nullptr;
    }
    if (m_backend && m_text.On()) m_backend->StopTextInput();
    g_clip.reset();
    if (g_self == this) g_self = nullptr;
    m_inited = false;
}

void OwnBackend::ProcessEvent(const okay::platform::Event& e) {
    if (!m_inited) return;
    std::vector<InputOp> ops;
    m_input.Feed(e, ops);
    ImGuiIO& io = ImGui::GetIO();
    for (const InputOp& op : ops) {
        switch (op.kind) {
        case InputOp::Key: io.AddKeyEvent(ToImGuiKey(op.key), op.down); break;
        case InputOp::MousePos: io.AddMousePosEvent(op.x, op.y); break;
        case InputOp::MouseButton: io.AddMouseButtonEvent(op.button, op.down); break;
        case InputOp::MouseWheel: io.AddMouseWheelEvent(op.x, op.y); break;
        case InputOp::Text: io.AddInputCharactersUTF8(op.text.c_str()); break;
        case InputOp::Focus: io.AddFocusEvent(op.down); break;
        }
    }
}

float OwnBackend::ContentScale() const { return m_window ? m_window->ContentScale() : 1.0f; }

void OwnBackend::NewFrame() {
    if (!m_inited) return;
    ImGuiIO& io = ImGui::GetIO();
    int ww = 0, wh = 0, dw = 0, dh = 0;
    if (m_window) { m_window->GetSize(ww, wh); m_window->GetDrawableSize(dw, dh); }
    else { m_painter->OutputSize(&ww, &wh); dw = ww; dh = wh; }
    m_display = ComputeDisplay(ww, wh, dw, dh);
    io.DisplaySize = ImVec2(m_display.width, m_display.height);
    io.DisplayFramebufferScale = ImVec2(m_display.scaleX, m_display.scaleY);
    io.DeltaTime = m_timer.Next(okay::platform::PerfCounter(), okay::platform::PerfFrequency());
    if (!m_backend) return;
    if (!(io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)) {
        const int c = io.MouseDrawCursor ? -1 : (int)ImGui::GetMouseCursor();
        if (!m_cursorSet || c != m_cursor) {
            const CursorRequest r = MapCursor(c);
            if (!m_cursorSet || r.visible != m_cursorShown) m_backend->ShowCursor(r.visible);
            if (r.visible) m_backend->SetCursorShape(r.shape);
            m_cursor = c; m_cursorShown = r.visible; m_cursorSet = true;
        }
    }
    switch (m_text.Update(io.WantTextInput)) {
    case TextInputGate::Start: m_backend->StartTextInput(); break;
    case TextInputGate::Stop: m_backend->StopTextInput(); break;
    default: break;
    }
}

void OwnBackend::DestroyTexture(ImTextureData* t) {
    if (t->BackendUserData) m_painter->DestroyTexture(t->BackendUserData);
    t->BackendUserData = nullptr;
    t->SetTexID(ImTextureID_Invalid);
    t->SetStatus(ImTextureStatus_Destroyed);
}

void OwnBackend::UpdateTexture(ImTextureData* t) {
    if (t->Status == ImTextureStatus_WantCreate) {
        std::vector<std::uint8_t> rgba;
        const std::uint8_t* px = t->Pixels;
        if (t->Format == ImTextureFormat_Alpha8) { Alpha8ToRgba(t->Pixels, t->GetPitch(), 0, 0, t->Width, t->Height, rgba); px = rgba.data(); }
        okay::platform::TextureDesc d;
        d.width = t->Width; d.height = t->Height; d.scale = okay::platform::ScaleMode::Linear;
        okay::ui2d::Tex tex = m_painter->CreateTexture(d, px);
        t->BackendUserData = tex;
        t->SetTexID((ImTextureID)(std::uintptr_t)tex);
        t->SetStatus(tex ? ImTextureStatus_OK : ImTextureStatus_Destroyed);
    } else if (t->Status == ImTextureStatus_WantUpdates && t->BackendUserData) {
        for (const ImTextureRect& r : t->Updates) {
            std::vector<std::uint8_t> rgba;
            const std::uint8_t* src;
            int pitch;
            if (t->Format == ImTextureFormat_Alpha8) { Alpha8ToRgba(t->Pixels, t->GetPitch(), r.x, r.y, r.w, r.h, rgba); src = rgba.data(); pitch = r.w * 4; }
            else { src = (const std::uint8_t*)t->GetPixelsAt(r.x, r.y); pitch = t->GetPitch(); }
            okay::ui2d::Rect rc{r.x, r.y, r.w, r.h};
            m_painter->UpdateTexture(t->BackendUserData, &rc, src, pitch);
        }
        t->SetStatus(ImTextureStatus_OK);
    } else if (t->Status == ImTextureStatus_WantDestroy && t->UnusedFrames > 0) {
        DestroyTexture(t);
    }
}

void OwnBackend::RenderDrawData(ImDrawData* dd) {
    if (!m_inited || !dd) return;
    if (dd->Textures) for (ImTextureData* t : *dd->Textures) if (t->Status != ImTextureStatus_OK) UpdateTexture(t);
    const int dispW = (int)dd->DisplaySize.x, dispH = (int)dd->DisplaySize.y;
    if (dispW <= 0 || dispH <= 0 || dd->CmdListsCount == 0) return;
    const okay::platform::BlendMode savedBlend = m_painter->GetBlendMode();
    const bool hadClip = m_painter->IsClipEnabled();
    okay::ui2d::Rect savedClip{0, 0, 0, 0};
    m_painter->GetClipRect(&savedClip);
    m_painter->SetScale(dd->FramebufferScale.x > 0 ? dd->FramebufferScale.x : 1.0f, dd->FramebufferScale.y > 0 ? dd->FramebufferScale.y : 1.0f);
    m_painter->SetBlendMode(okay::platform::BlendMode::Blend);
    for (const ImDrawList* list : dd->CmdLists) {
        ConvertVertices(reinterpret_cast<const DrawVertexIn*>(list->VtxBuffer.Data), list->VtxBuffer.Size, m_verts);
        const bool wide = sizeof(ImDrawIdx) == 4;
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) {
                if (cmd.UserCallback != ImDrawCallback_ResetRenderState) cmd.UserCallback(list, &cmd);
                continue;
            }
            okay::ui2d::Rect clip;
            if (!ClipToRect(cmd.ClipRect.x - dd->DisplayPos.x, cmd.ClipRect.y - dd->DisplayPos.y,
                            cmd.ClipRect.z - dd->DisplayPos.x, cmd.ClipRect.w - dd->DisplayPos.y, dispW, dispH, clip)) continue;
            if ((int)cmd.VtxOffset >= (int)m_verts.size() || cmd.ElemCount < 3) continue;
            const ImDrawIdx* ib = list->IdxBuffer.Data + cmd.IdxOffset;
            ConvertIndices(wide ? nullptr : reinterpret_cast<const std::uint16_t*>(ib), wide ? reinterpret_cast<const std::uint32_t*>(ib) : nullptr, (int)cmd.ElemCount, m_idx);
            okay::ui2d::Tex tex = (okay::ui2d::Tex)(std::uintptr_t)cmd.GetTexID();
            m_painter->SetClipRect(&clip);
            m_painter->Geometry(tex, m_verts.data() + cmd.VtxOffset, (int)m_verts.size() - (int)cmd.VtxOffset, m_idx.data(), (int)m_idx.size());
        }
    }
    m_painter->SetClipRect(hadClip ? &savedClip : nullptr);
    m_painter->SetBlendMode(savedBlend);
    m_painter->SetScale(1.0f, 1.0f);
}

} // namespace okay::imguibackend
