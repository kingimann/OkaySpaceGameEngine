// Our own Dear ImGui renderer on an okay::platform::Painter, shared by the launcher (OKAY_LAUNCHER_RENDERER=own) and the editor
// (OKAY_EDITOR_RENDERER=own). See ImGuiOwnRenderer.hpp. (Moved here from launcher/ImGuiOwnBackend.cpp, where it was written; the
// platform glue is shared/ImGuiOwnPlatform.cpp.)
//
// Written from imgui.h's documented contract only:
//  * ImDrawData: CmdLists of ImDrawList, each with VtxBuffer / IdxBuffer and CmdBuffer; a command draws ElemCount
//    indices from IdxOffset, relative to VtxOffset, with texture GetTexID(), clipped to ClipRect (in ImGui's
//    coordinates: subtract DisplayPos, multiply by FramebufferScale). A UserCallback replaces the draw.
//  * ImTextureData (ImGuiBackendFlags_RendererHasTextures): honour WantCreate / WantUpdates / WantDestroy, then call
//    SetTexID and SetStatus.
//  * ImGuiIO input events: AddMousePosEvent, AddMouseButtonEvent, AddMouseWheelEvent (y > 0 up, x > 0 left),
//    AddKeyEvent, AddInputCharactersUTF8, AddFocusEvent; DisplaySize, DisplayFramebufferScale, DeltaTime.
//  * ImGuiPlatformIO::Platform_SetImeDataFn: called at the end of a frame with the caret position (InputPos, in ImGui's
//    coordinates, minus the viewport's Pos) and the line height while a text field wants the input method (WantVisible).
//  * Multi-window panels: ImGuiPlatformIO's Renderer_RenderWindow ("clear framebuffer, setup render target, then render the
//    viewport->DrawData") and Renderer_SwapBuffers, called by ImGui::RenderPlatformWindowsDefault for each panel window;
//    ImGuiBackendFlags_RendererHasViewports; ImGuiViewportFlags_NoRendererClear.
#include "ImGuiOwnRenderer.hpp"

#include "imgui.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace okay::imguiown {

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
    if (m_windowOf) {
        if (m_onPanel) m_p.SetTarget(nullptr);
        pio.Renderer_RenderWindow = nullptr;
        pio.Renderer_SwapBuffers = nullptr;
        ImGui::GetIO().BackendRendererUserData = nullptr;
        ImGui::GetIO().BackendFlags &= ~ImGuiBackendFlags_RendererHasViewports;
        m_windowOf = nullptr;
    }
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

} // namespace okay::imguiown

namespace okay::imguiown {

// ---- multi-window panels ----------------------------------------------------------------------------------

bool ImGuiPainterRenderer::EnableViewports(ViewportWindowFn windowOf, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (!windowOf) return false;
    // Only a painter that can draw into another window: the OpenGL painter (Painter::SetTarget).
    if (m_p.Kind() != plat::PainterKind::OpenGL) return false;
    m_windowOf = windowOf;
    m_clear[0] = r; m_clear[1] = g; m_clear[2] = b;
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererUserData = this;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Renderer_RenderWindow = VpRender;
    pio.Renderer_SwapBuffers = VpSwap;
    return true;
}

// RenderPlatformWindowsDefault calls, for each panel window: (platform render), VpRender, (platform swap), VpSwap. The painter is
// pointed at the panel's window for the draw and back at the main window after the swap, so nothing else ever draws into a panel.
void ImGuiPainterRenderer::VpRender(ImGuiViewport* vp, void*) {
    ImGuiPainterRenderer* self = (ImGuiPainterRenderer*)ImGui::GetIO().BackendRendererUserData;
    if (!self || !self->m_windowOf || !vp) return;
    plat::Window* w = self->m_windowOf(vp);
    if (!w || !self->m_p.SetTarget(w)) { ++self->m_panelsFailed; self->m_onPanel = false; return; }
    self->m_onPanel = true;
    if (!(vp->Flags & ImGuiViewportFlags_NoRendererClear)) {
        self->m_p.SetScale(1.0f, 1.0f);
        self->m_p.SetClipRect(nullptr);
        self->m_p.SetDrawColor(self->m_clear[0], self->m_clear[1], self->m_clear[2], 255);
        self->m_p.Clear();
    }
    self->Render(vp->DrawData);
    ++self->m_panelsDrawn;
}

void ImGuiPainterRenderer::VpSwap(ImGuiViewport* vp, void*) {
    ImGuiPainterRenderer* self = (ImGuiPainterRenderer*)ImGui::GetIO().BackendRendererUserData;
    if (!self || !vp || !self->m_onPanel) return;
    self->m_p.Present();             // the panel window's buffers
    self->m_p.SetTarget(nullptr);    // the main window again
    self->m_onPanel = false;
}

} // namespace okay::imguiown
