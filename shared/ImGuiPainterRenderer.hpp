#pragma once
// Our own Dear ImGui renderer: turns ImDrawData into okay::platform::Painter calls, so any painter (SDL renderer, our software
// painter, later a GPU one) can draw the editor's and launcher's Dear ImGui frames without imgui_impl_sdlrenderer2.
//
// Written from Dear ImGui's public header (imgui.h: ImDrawData, ImDrawList, ImDrawCmd, ImDrawVert, ImTextureData and the texture
// request protocol it documents) and from the Painter interface. imgui_impl_sdlrenderer2 and SDL code were not read or followed.
//
// What it does for one frame (Render):
//   1. Honours the texture requests in ImDrawData::Textures: WantCreate makes a painter texture from the pixels, WantUpdates uploads
//      the changed rectangles, WantDestroy frees it; each is answered with SetTexID / SetStatus as imgui.h asks. Both RGBA32 and
//      Alpha8 atlases work (Alpha8 is expanded to white + alpha).
//   2. Draws every command list with the painter's scale set to the framebuffer scale (ImGui coordinates are logical), each command
//      with its clip rectangle and its texture (ImDrawCmd::GetTexID(), the painter's own Tex handle), using ImDrawCmd::VtxOffset
//      so 16-bit indices work with large meshes. A user callback runs; ImDrawCallback_ResetRenderState needs nothing here.
// The painter's scale and clip are put back to 1 / none afterwards.
//
// The textures are ordinary painter textures, so code that makes its own textures (the apps' thumbnails, covers, the 3D view) and
// passes them to ImGui as an ImTextureID keeps working: the id is the painter's Tex handle.
#include "imgui.h"
#include "okay/Platform/Painter.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay::imguirender {

/// Whether our own renderer runs, from OKAY_IMGUI_RENDER: "own" = yes, "sdl2" = no (imgui_impl_sdlrenderer2), unset or
/// anything else = `defaultOwn` (the launcher passes true, rule 17; the editor false, see ImGuiPlatformFeed.hpp).
inline bool OwnRequested(const char* envValue, bool defaultOwn = false) {
    if (envValue && std::string(envValue) == "own") return true;
    if (envValue && std::string(envValue) == "sdl2") return false;
    return defaultOwn;
}
/// The line the apps log at start-up so you can tell which renderer runs.
inline const char* ActiveLine(bool own) {
    return own ? "ImGui drawing: own okay::platform::Painter renderer (OKAY_IMGUI_RENDER=sdl2 for imgui_impl_sdlrenderer2)"
               : "ImGui drawing: imgui_impl_sdlrenderer2 (OKAY_IMGUI_RENDER=own for the own renderer)";
}

/// Mark the ImGui context as served by this renderer (call once after CreateContext). Textures are requested through ImTextureData and
/// meshes may use 16-bit indices with a vertex offset.
inline void Init(ImGuiIO& io) {
    io.BackendRendererName = "okay_painter";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
}

namespace detail {
inline okay::ui2d::Tex ToTex(ImTextureID id) { return (okay::ui2d::Tex)(std::uintptr_t)id; }
inline ImTextureID FromTex(okay::ui2d::Tex t) { return (ImTextureID)(std::uintptr_t)t; }

// Alpha8 -> RGBA (white with the alpha), for the rows [y0, y0 + h) and columns [x0, x0 + w) of `tex`.
inline void ExpandAlpha(const ImTextureData* tex, int x0, int y0, int w, int h, std::vector<std::uint8_t>& out) {
    out.resize((std::size_t)w * (std::size_t)h * 4);
    for (int y = 0; y < h; ++y) {
        const unsigned char* src = tex->Pixels + (std::size_t)(y0 + y) * (std::size_t)tex->Width + (std::size_t)x0;
        std::uint8_t* dst = &out[(std::size_t)y * (std::size_t)w * 4];
        for (int x = 0; x < w; ++x) { dst[x * 4] = dst[x * 4 + 1] = dst[x * 4 + 2] = 255; dst[x * 4 + 3] = src[x]; }
    }
}
}

/// Answer one texture request. Safe to call for any status.
inline void UpdateTexture(okay::platform::Painter& p, ImTextureData* tex) {
    if (!tex) return;
    switch (tex->Status) {
    case ImTextureStatus_WantCreate: {
        okay::platform::TextureDesc d;
        d.width = tex->Width; d.height = tex->Height;
        d.blend = okay::platform::BlendMode::Blend;
        d.scale = okay::platform::ScaleMode::Linear;
        okay::ui2d::Tex t = nullptr;
        if (tex->Pixels && tex->Width > 0 && tex->Height > 0) {
            if (tex->Format == ImTextureFormat_Alpha8) {
                std::vector<std::uint8_t> rgba;
                detail::ExpandAlpha(tex, 0, 0, tex->Width, tex->Height, rgba);
                t = p.CreateTexture(d, rgba.data());
            } else {
                t = p.CreateTexture(d, (const std::uint8_t*)tex->Pixels);
            }
        }
        if (!t) return;   // stays WantCreate: tried again next frame
        tex->SetTexID(detail::FromTex(t));
        tex->SetStatus(ImTextureStatus_OK);
        break;
    }
    case ImTextureStatus_WantUpdates: {
        okay::ui2d::Tex t = detail::ToTex(tex->TexID);
        if (!t || !tex->Pixels) { tex->SetStatus(ImTextureStatus_WantCreate); return; }   // lost it: make it again
        for (const ImTextureRect& r : tex->Updates) {
            if (r.w == 0 || r.h == 0) continue;
            okay::ui2d::Rect rc{(int)r.x, (int)r.y, (int)r.w, (int)r.h};
            if (tex->Format == ImTextureFormat_Alpha8) {
                std::vector<std::uint8_t> rgba;
                detail::ExpandAlpha(tex, rc.x, rc.y, rc.w, rc.h, rgba);
                p.UpdateTexture(t, &rc, rgba.data(), rc.w * 4);
            } else {
                p.UpdateTexture(t, &rc, tex->GetPixelsAt(rc.x, rc.y), tex->GetPitch());
            }
        }
        tex->SetStatus(ImTextureStatus_OK);
        break;
    }
    case ImTextureStatus_WantDestroy:
        if (tex->UnusedFrames > 0) {   // ImGui says nothing in flight draws it any more
            if (tex->TexID != ImTextureID_Invalid) p.DestroyTexture(detail::ToTex(tex->TexID));
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
        break;
    default: break;
    }
}

/// Draw one frame's draw data. `p` is the painter of the window the data is for.
inline void Render(okay::platform::Painter& p, ImDrawData* dd) {
    if (!dd || !dd->Valid) return;
    if (dd->Textures)
        for (ImTextureData* tex : *dd->Textures)
            if (tex->Status != ImTextureStatus_OK) UpdateTexture(p, tex);
    const ImVec2 off = dd->DisplayPos;
    const ImVec2 scale = dd->FramebufferScale;
    const float sx = scale.x > 0 ? scale.x : 1.0f, sy = scale.y > 0 ? scale.y : 1.0f;
    if (dd->DisplaySize.x <= 0 || dd->DisplaySize.y <= 0) return;
    p.SetScale(sx, sy);
    p.SetClipRect(nullptr);
    std::vector<ImDrawVert> rebased;   // vertices moved so DisplayPos is the origin (only when it is not 0, 0)
    for (const ImDrawList* dl : dd->CmdLists) {
        const ImDrawVert* vtx = dl->VtxBuffer.Data;
        if (off.x != 0.0f || off.y != 0.0f) {
            rebased.assign(dl->VtxBuffer.Data, dl->VtxBuffer.Data + dl->VtxBuffer.Size);
            for (ImDrawVert& v : rebased) { v.pos.x -= off.x; v.pos.y -= off.y; }
            vtx = rebased.data();
        }
        const ImDrawIdx* idx = dl->IdxBuffer.Data;
        for (int ci = 0; ci < dl->CmdBuffer.Size; ++ci) {
            const ImDrawCmd* cmd = &dl->CmdBuffer[ci];
            if (cmd->UserCallback) {
                if (cmd->UserCallback != ImDrawCallback_ResetRenderState) cmd->UserCallback(dl, cmd);
                continue;
            }
            // The clip rectangle in logical units (the painter's scale turns it into pixels), cut to the display.
            float x0 = cmd->ClipRect.x - off.x, y0 = cmd->ClipRect.y - off.y, x1 = cmd->ClipRect.z - off.x, y1 = cmd->ClipRect.w - off.y;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > dd->DisplaySize.x) x1 = dd->DisplaySize.x;
            if (y1 > dd->DisplaySize.y) y1 = dd->DisplaySize.y;
            if (x1 <= x0 || y1 <= y0) continue;
            okay::ui2d::Rect clip{(int)x0, (int)y0, (int)(x1 - x0 + 0.999f), (int)(y1 - y0 + 0.999f)};   // round outwards: never cut a pixel ImGui meant to show
            p.SetClipRect(&clip);
            const char* vbase = (const char*)(vtx + cmd->VtxOffset);
            const int nv = dl->VtxBuffer.Size - (int)cmd->VtxOffset;
            if (nv <= 0 || cmd->ElemCount == 0) continue;
            p.GeometryRaw(detail::ToTex(cmd->GetTexID()),
                          (const float*)(const void*)(vbase + offsetof(ImDrawVert, pos)), (int)sizeof(ImDrawVert),
                          (const std::uint8_t*)(const void*)(vbase + offsetof(ImDrawVert, col)), (int)sizeof(ImDrawVert),
                          (const float*)(const void*)(vbase + offsetof(ImDrawVert, uv)), (int)sizeof(ImDrawVert),
                          nv, idx + cmd->IdxOffset, (int)cmd->ElemCount, (int)sizeof(ImDrawIdx));
        }
    }
    p.SetClipRect(nullptr);
    p.SetScale(1.0f, 1.0f);
}

/// Free every texture this renderer made (call before the painter or the ImGui context goes away).
inline void Shutdown(okay::platform::Painter& p, ImGuiIO& io) {
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
        if (tex->RefCount != 1) continue;   // another context still uses it
        if (tex->TexID != ImTextureID_Invalid) p.DestroyTexture(detail::ToTex(tex->TexID));
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
    }
    io.BackendRendererName = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
}

} // namespace okay::imguirender
