#pragma once
// OkayUIInImGui: OkayUI content drawn at Dear ImGui's layer, inside an ImGui window.
//
// OkayUI normally draws its whole frame after ImGui (OkayUI::EndFrame on top of everything). A region hosted inside an ImGui
// window would then cover every ImGui popup, menu, tooltip and window that opens over it later in the frame (the script
// editor's autocomplete list, its hover help, a dragged panel). Here the region's geometry is taken out of the OkayUI frame
// (OkayUI::BeginGeometryCapture / EndGeometryCapture) and appended to the ImGui window's own draw list at the point where the
// region is laid out, so ImGui sorts it with that window: whatever ImGui draws later in the window, and every popup or window
// above it, is drawn over the code.
//
// Own code, written from the two draw-list formats (OkayUI's DrawData in okay/UI/OkayUI.hpp, ImGui's ImDrawList in imgui.h);
// nothing taken from either library's sources. Tested in okayui/tests/test_okayui_codeedit_imgui.cpp.
#include "imgui.h"
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUICodeEdit.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <functional>

namespace okayimgui {

/// Append geometry taken out of an OkayUI frame to an ImGui draw list. `textureFor` turns an OkayUI DrawCmd::texture (the
/// launcher look's atlas, a fallback font, an image) into the ImTextureID of the ImGui renderer backend; a command whose
/// texture it cannot give is skipped. Untextured geometry (the classic look) samples the white pixel of ImGui's font atlas.
/// Clip rectangles are intersected with the draw list's current one.
inline void AppendGeometry(ImDrawList* dl, const OkayUI::CapturedGeometry& g, const std::function<ImTextureID(const void*)>& textureFor) {
    if (!dl || g.indices.empty() || g.vertices.empty()) return;
    const ImVec2 white = ImGui::GetFontTexUvWhitePixel();
    const ImTextureRef fontTex = ImGui::GetIO().Fonts->TexRef;
    const int nv = (int)g.vertices.size(), ni = (int)g.indices.size();
    for (const OkayUI::DrawCmd& c : g.cmds) {
        if (c.idxCount <= 0 || c.idxOffset < 0 || c.idxOffset > ni - c.idxCount) continue;
        ImTextureRef tex = fontTex;
        const bool flat = c.texture == nullptr;
        if (!flat) {
            const ImTextureID id = textureFor ? textureFor(c.texture) : ImTextureID_Invalid;
            if (id == ImTextureID_Invalid) continue;
            tex = ImTextureRef(id);
        }
        if (c.clipOn) dl->PushClipRect(ImVec2((float)c.clipX, (float)c.clipY), ImVec2((float)(c.clipX + c.clipW), (float)(c.clipY + c.clipH)), true);
        dl->PushTexture(tex);
        auto put = [&](const OkayUI::DrawVertex& v) {
            ImDrawVert* w = dl->_VtxWritePtr++;
            w->pos.x = v.x; w->pos.y = v.y;
            if (flat) w->uv = white; else { w->uv.x = v.u; w->uv.y = v.v; }
            w->col = IM_COL32(v.r, v.g, v.b, v.a);
        };
        // In chunks of up to 4096 triangles. OkayUI writes each shape's vertices together, so a chunk's vertices are usually one
        // short run: copied once and indexed. A scattered chunk is written out vertex by vertex instead.
        const int* idx = g.indices.data();
        const OkayUI::DrawVertex* vtx = g.vertices.data();
        for (int at = c.idxOffset, end = c.idxOffset + c.idxCount; at < end;) {
            const int n = std::min(end - at, 3 * 4096) / 3 * 3;
            if (n <= 0) break;
            int vmin = INT_MAX, vmax = -1;
            for (int k = at; k < at + n; ++k) { const int ix = idx[k]; vmin = ix < vmin ? ix : vmin; vmax = ix > vmax ? ix : vmax; }
            if (vmin < 0 || vmax >= nv) { at += n; continue; }
            const int span = vmax - vmin + 1;
            if (span <= n + 64) {
                dl->PrimReserve(n, span);
                const unsigned base = dl->_VtxCurrentIdx;
                for (int v = vmin; v <= vmax; ++v) put(vtx[v]);
                ImDrawIdx* wi = dl->_IdxWritePtr;
                for (int k = at; k < at + n; ++k) *wi++ = (ImDrawIdx)(base + (unsigned)(idx[k] - vmin));
                dl->_IdxWritePtr = wi;
                dl->_VtxCurrentIdx += (unsigned)span;
            } else {
                dl->PrimReserve(n, n);
                ImDrawIdx* wi = dl->_IdxWritePtr;
                for (int k = at; k < at + n; ++k) { *wi++ = (ImDrawIdx)dl->_VtxCurrentIdx++; put(vtx[idx[k]]); }
                dl->_IdxWritePtr = wi;
            }
            at += n;
        }
        dl->PopTexture();
        if (c.clipOn) dl->PopClipRect();
    }
}

/// Run `draw` inside a bare OkayUI window at (pos, size) and put what it draws into the current ImGui window's draw list, at
/// this point of the ImGui frame. Needs an open OkayUI frame and an open ImGui window. OkayUI tooltips (the overlay) are not
/// moved: they stay on top. `geo` is scratch space kept between frames.
inline void DrawOkayUIHere(const char* id, ImVec2 pos, ImVec2 size, const std::function<void()>& draw,
                           const std::function<ImTextureID(const void*)>& textureFor, OkayUI::CapturedGeometry& geo) {
    OkayUI::BeginGeometryCapture();
    OkayUI::BareNextWindow();
    if (OkayUI::Begin(id, pos.x, pos.y, size.x, size.y)) draw();
    OkayUI::End();
    OkayUI::EndGeometryCapture(geo);
    AppendGeometry(ImGui::GetWindowDrawList(), geo, textureFor);
}

/// The texture lookup for ImGui's SDL_Renderer backend (its ImTextureID is the SDL_Texture pointer).
inline std::function<ImTextureID(const void*)> SdlRendererTextures(SDL_Renderer* renderer) {
    return [renderer](const void* t) -> ImTextureID {
        SDL_Texture* s = OkayUI::TextureForRenderer(renderer, t);
        return s ? (ImTextureID)(std::intptr_t)s : ImTextureID_Invalid;
    };
}

/// Make a CodeEdit sit in its rectangle the way ImGuiColorTextEdit sits in its child window: the child's padding around the
/// lines, ImGui's scroll bar thickness and colours, the horizontal bar always shown.
inline void StyleLikeImGuiChild(OkayUI::CodeEdit& ed, const ImGuiStyle& st) {
    ed.SetPadding(st.WindowPadding.x, st.WindowPadding.y);
    ed.SetScrollbarSize(st.ScrollbarSize);
    ed.SetAlwaysShowHorizontalScrollbar(true);
    OkayUI::CodePalette p = ed.Palette();
    p.scrollTrack = ImGui::GetColorU32(ImGuiCol_ScrollbarBg);       // ImU32 is IM_COL32 (0xAABBGGRR), OkayUI's order too
    p.scrollThumb = ImGui::GetColorU32(ImGuiCol_ScrollbarGrab);
    p.scrollThumbHot = ImGui::GetColorU32(ImGuiCol_ScrollbarGrabHovered);
    ed.SetPalette(p);
}

} // namespace okayimgui
