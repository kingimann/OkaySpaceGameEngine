#pragma once
// Drawing OkayUI inside a Dear ImGui window, in Dear ImGui's own draw order (own code).
//
// OkayUI collects its frame into one batch that the app draws after Dear ImGui, so anything OkayUI draws lies on top of every
// ImGui window, popup, tooltip and modal. A widget that lives inside an ImGui window (the Script Editor's OkayUI::CodeEdit) must
// instead lie under the popups ImGui opens over it (autocomplete, the help tooltip, "Unsaved Changes"). So the host cuts the
// widget's shapes out of the OkayUI frame (OkayUI::BeginCapture / EndCapture, okay/UI/OkayUICapture.hpp) and hands them to the
// ImGui window's draw list here: they are then drawn exactly where the window is in ImGui's order, whatever ImGui renderer the app
// uses (SDL renderer, our own ImGui renderer, the multi-window one).
//
// Textures: a shape samples either nothing (a flat colour: ImGui's white texel is used) or an OkayUI texture whose pixels OkayUI
// gives (OkayUI::GetTexturePixels: the launcher look's atlas, a TrueType font's glyph atlas). Each becomes a Dear ImGui user
// texture (ImTextureData, RegisterUserTexture) that the ImGui renderer uploads like its own font atlas; it is refreshed when the
// font bakes new characters and replaced when it grows. Shapes on a texture only a backend knows (an OkayUI Image) are skipped.
//
// HostPanelWindow (below) uses the same path for a whole panel: an ImGui window whose body is OkayUI only.
#include "imgui.h"
#include "imgui_internal.h"
#include "OkayUIHostImGui.hpp"   // HiddenInImGui, OutsideMainWindow, ToMainWindowPixels
#include "okay/UI/OkayUICapture.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace okayuihost {

class ImGuiEmbed {
public:
    ImGuiEmbed() = default;
    ImGuiEmbed(const ImGuiEmbed&) = delete;
    ImGuiEmbed& operator=(const ImGuiEmbed&) = delete;

    /// Append the captured shapes to `dl`. `offset` moves OkayUI's pixels into ImGui's coordinates (the window's viewport position
    /// with multi-viewports on, else 0, 0); everything is also clipped to `clipMin`..`clipMax` (ImGui coordinates).
    void Append(ImDrawList* dl, const OkayUI::CapturedGeometry& g, ImVec2 offset, ImVec2 clipMin, ImVec2 clipMax) {
        if (!dl || g.Empty()) return;
        collect();
        const ImVec2 white = dl->_Data ? dl->_Data->TexUvWhitePixel : ImVec2(0, 0);
        for (const OkayUI::DrawCmd& c : g.cmds) {
            if (c.idxCount <= 0 || c.idxOffset < 0 || c.idxOffset + c.idxCount > (int)g.indices.size()) continue;
            ImTextureData* td = nullptr;
            if (c.texture) { td = textureFor(c.texture); if (!td) { ++skipped_; continue; } }
            ImVec2 cmin = clipMin, cmax = clipMax;
            if (c.clipOn) {
                cmin.x = std::max(cmin.x, (float)c.clipX + offset.x); cmin.y = std::max(cmin.y, (float)c.clipY + offset.y);
                cmax.x = std::min(cmax.x, (float)(c.clipX + c.clipW) + offset.x); cmax.y = std::min(cmax.y, (float)(c.clipY + c.clipH) + offset.y);
            }
            if (cmax.x <= cmin.x || cmax.y <= cmin.y) continue;
            dl->PushClipRect(cmin, cmax, true);
            if (td) dl->PushTexture(td->GetTexRef());
            // In chunks well under 65536 vertices: with 16-bit indices one PrimReserve must stay below that (ImGui starts a new
            // vertex offset between reserves when the renderer supports it). Each corner is its own vertex: simple, and the code
            // view is a few thousand quads.
            const int kChunk = 3 * 8000;
            for (int i = 0; i < c.idxCount; i += kChunk) {
                const int n = std::min(kChunk, c.idxCount - i) / 3 * 3;
                if (n <= 0) break;
                dl->PrimReserve(n, n);
                for (int k = 0; k < n; ++k) {
                    const int vi = g.indices[(size_t)(c.idxOffset + i + k)];
                    const OkayUI::DrawVertex& v = g.vertices[(size_t)(vi >= 0 && vi < (int)g.vertices.size() ? vi : 0)];
                    dl->PrimWriteIdx((ImDrawIdx)dl->_VtxCurrentIdx);
                    dl->PrimWriteVtx(ImVec2(v.x + offset.x, v.y + offset.y), td ? ImVec2(v.u, v.v) : white, IM_COL32(v.r, v.g, v.b, v.a));
                }
            }
            if (td) dl->PopTexture();
            dl->PopClipRect();
        }
    }

    /// How many OkayUI textures are live as ImGui textures, and how many draw commands were skipped (a texture with no pixels).
    int TextureCount() const { return (int)tex_.size(); }
    int Skipped() const { return skipped_; }
    /// The ImGui texture made for an OkayUI texture (null if none yet).
    ImTextureData* TextureOf(const void* okayTexture) const { auto it = tex_.find(okayTexture); return it == tex_.end() ? nullptr : it->second.data; }

    /// Hand every texture back to Dear ImGui for destruction (they go once the renderer has destroyed them). Call before the
    /// ImGui context goes; the object can be used again afterwards.
    void Release() {
        for (auto& kv : tex_) retire(kv.second.data);
        tex_.clear();
        collect();
    }

private:
    struct Tex { ImTextureData* data = nullptr; unsigned gen = 0; };

    ImTextureData* textureFor(const void* key) {
        OkayUI::TexturePixels p;
        if (!OkayUI::GetTexturePixels(key, p) || !p.rgba || p.w <= 0 || p.h <= 0 || p.w > 0xFFFF || p.h > 0xFFFF) return nullptr;
        Tex& t = tex_[key];
        if (t.data && (t.data->Width != p.w || t.data->Height != p.h)) { retire(t.data); t.data = nullptr; }   // the font's atlas grew
        if (!t.data) {
            t.data = new ImTextureData();
            t.data->Create(ImTextureFormat_RGBA32, p.w, p.h);
            std::memcpy(t.data->GetPixels(), p.rgba, (size_t)p.w * (size_t)p.h * 4);
            t.data->UsedRect.x = t.data->UsedRect.y = 0; t.data->UsedRect.w = (unsigned short)p.w; t.data->UsedRect.h = (unsigned short)p.h;
            ImGui::RegisterUserTexture(t.data);
            t.gen = p.generation;
        } else if (t.gen != p.generation) {   // new characters baked into the same atlas: upload it again
            std::memcpy(t.data->GetPixels(), p.rgba, (size_t)p.w * (size_t)p.h * 4);
            if (t.data->Status == ImTextureStatus_OK || t.data->Status == ImTextureStatus_WantUpdates) ImTextureDataQueueUpload(t.data, 0, 0, p.w, p.h);
            t.gen = p.generation;
        }
        return t.data;
    }
    void retire(ImTextureData* td) {
        if (!td) return;
        if (td->Status == ImTextureStatus_WantCreate && td->TexID == ImTextureID_Invalid) {   // never reached the renderer
            ImGui::UnregisterUserTexture(td);
            delete td;
            return;
        }
        td->WantDestroyNextFrame = true;   // ImGui asks the renderer to destroy it at the next frame
        retired_.push_back(td);
    }
    void collect() {   // free what the renderer has destroyed
        for (size_t i = 0; i < retired_.size();) {
            ImTextureData* td = retired_[i];
            if (td->Status == ImTextureStatus_Destroyed) { ImGui::UnregisterUserTexture(td); delete td; retired_[i] = retired_.back(); retired_.pop_back(); }
            else ++i;
        }
    }

    std::unordered_map<const void*, Tex> tex_;
    std::vector<ImTextureData*> retired_;
    int skipped_ = 0;
};

/// The one embed the editor's hosted OkayUI widgets share (one set of textures).
inline ImGuiEmbed& SharedImGuiEmbed() { static ImGuiEmbed* e = new ImGuiEmbed(); return *e; }

/// A whole panel drawn by OkayUI inside a Dear ImGui window, with no Dear ImGui copy of the panel (the editor's Crash Log, Audio
/// Mixer and History, editor/OkayPanels.cpp). The ImGui window stays: it is the dock tab, the title bar and the close box, and its
/// free area is one placeholder item that the OkayUI body fills. The body's shapes go into the window's draw list (Capture_KeepOverlay),
/// so Dear ImGui's popups, tooltips, modals and the windows in front of it are drawn over the panel as over any ImGui panel; the
/// body's own menus, lists and modals stay on OkayUI's top layer. Earlier hosts drew the panel's Dear ImGui version while something
/// covered it; this one needs none.
///
/// `region` gets the rectangles that may take the mouse (anything with Offer(x, y, w, h, hovered, overlay), the editor's
/// okayuihost::Region): the body's, with ImGui's own hover answer (false while a popup or a window in front is over it), and every
/// overlay the body opened. A window that is not docked is kept in the main OS window (OkayUI reads the mouse in that window's
/// pixels); one docked into a node that was dragged out to another OS window is drawn there but does not take the mouse.
/// `firstSize`: the size a window with no saved place opens at. `draw(pos, size)` gets OkayUI's pixels (the main window's).
/// Returns true when the body was drawn this frame (false: the window is closed, collapsed, a tab behind another, or hidden).
template <class Region, class Draw>
bool HostPanelWindow(const char* title, bool* open, const char* itemId, Region& region, ImGuiEmbed& embed, OkayUI::CapturedGeometry& cap,
                     ImVec2 firstSize, Draw draw) {
    if (open && !*open) return false;
    const ImGuiWindow* before = ImGui::FindWindowByName(title);
    if (!before || !before->DockIsActive) ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::SetNextWindowSize(firstSize, ImGuiCond_FirstUseEver);
    bool drawn = false;
    if (ImGui::Begin(title, open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        const ImVec2 p = ToMainWindowPixels(ImGui::GetCursorScreenPos());   // OkayUI's pixels, not ImGui's desktop ones
        ImVec2 a = ImGui::GetContentRegionAvail();
        a.x = std::max(a.x, 8.0f); a.y = std::max(a.y, 8.0f);
        ImGui::InvisibleButton(itemId, a);                     // an ImGui item, so ImGui does not move or scroll the window under it
        if (!HiddenInImGui(win)) {
            ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);      // the wheel scrolls the OkayUI body, not the ImGui window
            const bool mainWindow = !OutsideMainWindow(win);
            region.Offer(p.x, p.y, a.x, a.y, mainWindow && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem));
            const int overlays = OkayUI::GetOverlayCount();
            OkayUI::BeginCapture(OkayUI::Capture_KeepOverlay);
            draw(p, a);
            OkayUI::EndCapture(cap);
            embed.Append(win->DrawList, cap, ImGui::GetMainViewport()->Pos, win->InnerClipRect.Min, win->InnerClipRect.Max);
            for (int i = overlays; mainWindow && i < OkayUI::GetOverlayCount(); ++i) {   // a menu or list the body opened: it takes the mouse
                const OkayUI::Rect r = OkayUI::GetOverlayRect(i);
                if (r.w > 0.0f && r.h > 0.0f) region.Offer(r.x, r.y, r.w, r.h, true, true);
            }
            drawn = true;
        }
    }
    ImGui::End();
    return drawn;
}

} // namespace okayuihost
