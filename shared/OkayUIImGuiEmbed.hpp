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
// HostPanelWindow (below) uses the same path for a whole panel: an ImGui window whose body is OkayUI only; HostPanelSection for a
// part of an ImGui window (a section between its other items).
#include "imgui.h"
#include "imgui_internal.h"
#include "OkayUIHostImGui.hpp"   // HiddenInImGui, OutsideMainWindow, ToMainWindowPixels
#include "okay/UI/OkayUICapture.hpp"
#include "okay/UI/OkayUICompat.hpp"   // ClosePopupToLevel: the body's menus close when Dear ImGui takes over

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
    /// with multi-viewports on, else 0, 0); everything is also clipped to `clipMin`..`clipMax` (ImGui coordinates). `cmdFrom` ..
    /// `cmdTo` (-1: to the end) picks a range of the commands (CapturedGeometry::overlayFrom splits the window's own from its overlay).
    void Append(ImDrawList* dl, const OkayUI::CapturedGeometry& g, ImVec2 offset, ImVec2 clipMin, ImVec2 clipMax, int cmdFrom = 0, int cmdTo = -1) {
        if (!dl || g.Empty()) return;
        collect();
        const ImVec2 white = dl->_Data ? dl->_Data->TexUvWhitePixel : ImVec2(0, 0);
        const int to = cmdTo < 0 || cmdTo > (int)g.cmds.size() ? (int)g.cmds.size() : cmdTo;
        for (int ci = std::max(cmdFrom, 0); ci < to; ++ci) {
            const OkayUI::DrawCmd& c = g.cmds[(size_t)ci];
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

namespace detail {
/// The step HostPanelWindow and HostPanelSection share: the OkayUI body at `p` (OkayUI's pixels) drawn into `win`'s draw list,
/// clipped to `clipMin`..`clipMax` (ImGui coordinates), with `offer` (OkayUI's pixels: the part of the body the window shows) and the
/// body's overlays handed to `region`, and the body's menus closed when Dear ImGui takes over. `drawBody()` draws the body.
template <class Region, class DrawBody>
void EmbedBody(ImGuiWindow* win, ImVec2 clipMin, ImVec2 clipMax, const ImRect& offer, bool hovered, bool focused, Region& region,
               ImGuiEmbed& embed, OkayUI::CapturedGeometry& cap, DrawBody drawBody) {
    const bool mainWindow = !OutsideMainWindow(win);
    const ImVec2 mainPos = ImGui::GetMainViewport()->Pos;
    region.Offer(offer.Min.x, offer.Min.y, offer.GetWidth(), offer.GetHeight(), hovered);
    if (hovered || focused) region.NoteActive();
    const int overlays = OkayUI::GetOverlayCount();
    OkayUI::BeginCapture(mainWindow ? OkayUI::Capture_KeepOverlay : OkayUI::Capture_All);
    drawBody();
    OkayUI::EndCapture(cap);
    embed.Append(win->DrawList, cap, mainPos, clipMin, clipMax, 0, cap.overlayFrom);
    if (!mainWindow) {   // the body's menus, on top of the OS window it is in
        const ImGuiViewport* vp = win->Viewport;
        embed.Append(ImGui::GetForegroundDrawList(win->Viewport), cap, mainPos, vp->Pos,
                     ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), cap.overlayFrom);
    }
    // the overlays the body opened: they take the mouse; and whether ImGui's mouse is over one of them
    const ImVec2 mouse = ToMainWindowPixels(ImGui::GetIO().MousePos);
    bool overOverlay = false, hadOverlay = false;
    for (int i = overlays; i < OkayUI::GetOverlayCount(); ++i) {
        const OkayUI::Rect r = OkayUI::GetOverlayRect(i);
        if (r.w <= 0.0f || r.h <= 0.0f) continue;
        hadOverlay = true;
        region.Offer(r.x, r.y, r.w, r.h, true, true);
        if (mouse.x >= r.x && mouse.y >= r.y && mouse.x < r.x + r.w && mouse.y < r.y + r.h) overOverlay = true;
    }
    // Dear ImGui takes over: the body's menus close (they are redrawn closed from the next frame)
    static ImGuiStorage s_wasFocused;
    const bool wasFocused = s_wasFocused.GetBool(win->ID, false);
    s_wasFocused.SetBool(win->ID, focused);
    if (hadOverlay) {
        const ImRect wr = win->Rect();
        const bool imguiPopup = PopupCoversRect(wr.Min.x, wr.Min.y, wr.GetWidth(), wr.GetHeight());
        const bool clickAway = (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) &&
                               !hovered && !overOverlay;
        if (imguiPopup || clickAway || (wasFocused && !focused)) OkayUI::ClosePopupToLevel(0, false);
    }
}
} // namespace detail

/// A whole panel drawn by OkayUI inside a Dear ImGui window, with no Dear ImGui copy of the panel (the editor's Crash Log, Audio
/// Mixer, History, Problems, Stats and Profiler, editor/OkayPanels.cpp). The ImGui window stays: it is the dock tab, the title bar and
/// the close box, and its free area is one placeholder item that the OkayUI body fills. The body's shapes go into the window's draw
/// list, so Dear ImGui's popups, tooltips, modals and the windows in front of it are drawn over the panel as over any ImGui panel.
///
/// Any OS window: in the main one the body's own menus, lists and modals stay on OkayUI's top layer (Capture_KeepOverlay); in another
/// (the panel floats outside the editor, or is docked into a node that was dragged out) they are taken too and drawn on top of that
/// window (its foreground list). OkayUI's mouse must then be ImGui's, moved into the main window's pixels (MouseInOtherViewport, the
/// host's job): the body and its overlays are offered in those pixels wherever they are.
///
/// The body's menus close when something of Dear ImGui takes over: an ImGui popup or modal opens over the window, a click lands
/// outside the body and its menus, or the keyboard focus leaves the window (OkayUI's frame does not see those, so the menu would stay
/// above a later ImGui modal and take its clicks).
///
/// `region` gets the rectangles that may take the mouse (anything with Offer(x, y, w, h, hovered, overlay) and NoteActive(), the
/// editor's okayuihost::Region): the body's, with ImGui's own hover answer (false while a popup or a window in front is over it),
/// every overlay the body opened, and NoteActive() while the body is hovered or its window has the keyboard focus (the host gives
/// OkayUI its keys then). `firstSize`: the size a window with no saved place opens at. `draw(pos, size)` gets OkayUI's pixels (the
/// main window's). Returns true when the body was drawn this frame (false: closed, collapsed, a tab behind another, or hidden).
template <class Region, class Draw>
bool HostPanelWindow(const char* title, bool* open, const char* itemId, Region& region, ImGuiEmbed& embed, OkayUI::CapturedGeometry& cap,
                     ImVec2 firstSize, Draw draw) {
    if (open && !*open) return false;
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
            const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            detail::EmbedBody(win, win->InnerClipRect.Min, win->InnerClipRect.Max, ImRect(p.x, p.y, p.x + a.x, p.y + a.y), hovered, focused,
                              region, embed, cap, [&] { draw(p, a); });
            drawn = true;
        }
    }
    ImGui::End();
    return drawn;
}

/// A rectangle of the current Dear ImGui window drawn by OkayUI, `size` big at the cursor: HostPanelWindow's way for a page that
/// fills part of a window the host lays out itself (the launcher's What's New and Tips pages, which sit in its "content" child
/// between the page header and the footer). One placeholder item; the body's shapes go into the window's draw list, clipped to what
/// the window shows, so Dear ImGui's tooltips, popups and windows in front are drawn over the page and the page never has to
/// switch to another version of itself while they are open (the launcher's What's New did: an ImGui tooltip over a corner of it
/// swapped the whole page to its Dear ImGui copy for as long as it showed, 2026-11-26). The mouse wheel is the body's.
/// `draw(pos, size)` gets OkayUI's pixels (the main window's). Returns true when the body was drawn this frame.
template <class Region, class Draw>
bool HostPanelArea(const char* itemId, ImVec2 size, Region& region, ImGuiEmbed& embed, OkayUI::CapturedGeometry& cap, Draw draw) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (!win || win->SkipItems) return false;
    const ImVec2 sp = ImGui::GetCursorScreenPos();
    const ImVec2 p = ToMainWindowPixels(sp);                  // OkayUI's pixels, not ImGui's desktop ones
    const ImVec2 a(std::max(size.x, 8.0f), std::max(size.y, 8.0f));
    ImGui::InvisibleButton(itemId, a);                        // an ImGui item, so ImGui does not move or scroll the window under it
    if (HiddenInImGui(win)) return false;
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);             // the wheel scrolls the OkayUI body, not the ImGui window
    ImRect shown(sp.x, sp.y, sp.x + a.x, sp.y + a.y);         // the part of it the window shows (ImGui's coordinates)
    shown.ClipWithFull(win->InnerClipRect);
    if (shown.GetWidth() <= 0.0f || shown.GetHeight() <= 0.0f) return false;
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    detail::EmbedBody(win, shown.Min, shown.Max, ImRect(ToMainWindowPixels(shown.Min), ToMainWindowPixels(shown.Max)), hovered, focused,
                      region, embed, cap, [&] { draw(p, a); });
    return true;
}

/// A part of a Dear ImGui window drawn by OkayUI only, with no Dear ImGui copy (the editor's Project Settings sections Player,
/// Physics, Rendering and Tags and Layers, editor/OkayPanels.cpp): HostPanelWindow's way for a piece between the window's other
/// items. Call it inside the window, where the part goes. It places one placeholder item as tall as the body drew on the last frame
/// (the window lays it out and scrolls it like any item) and puts the body's shapes into the window's draw list there, clipped to
/// what the window shows, so Dear ImGui's popups, tooltips and the windows in front are drawn over it. Only the part of the body the
/// window shows is offered to `region`, with ImGui's hover answer, and so are the overlays the body opens (they stay on OkayUI's top
/// layer in the main OS window, as in HostPanelWindow). The mouse wheel stays the window's (it scrolls the window).
///
/// `draw(x, y, w, h)` (OkayUI's pixels) draws the body and returns how tall it drew. The first frame only measures: the body is
/// drawn into an empty clip and nothing of it is shown, so a new window fits itself to the right height at once. Returns true when
/// the body was drawn (or measured) this frame; false when the window shows nothing of it (collapsed, a tab behind another).
template <class Region, class Draw>
bool HostPanelSection(const char* itemId, Region& region, ImGuiEmbed& embed, OkayUI::CapturedGeometry& cap, Draw draw) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (!win || win->SkipItems) return false;
    float* height = ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID(itemId), -1.0f);   // -1: not measured yet
    const ImVec2 sp = ImGui::GetCursorScreenPos();
    const ImVec2 p = ToMainWindowPixels(sp);                  // OkayUI's pixels, not ImGui's desktop ones
    const float w = std::max(ImGui::GetContentRegionAvail().x, 8.0f);
    auto measured = [](float hgt) { return (hgt > 1.0f && hgt == hgt && hgt < 20000.0f) ? hgt : (hgt >= 20000.0f ? 20000.0f : 300.0f); };
    if (*height < 0.0f) {
        OkayUI::BeginCapture(OkayUI::Capture_All);            // nothing of the measuring pass is shown
        OkayUI::PushClipRect(p.x, p.y, 0.0f, 0.0f);
        const float hgt = draw(p.x, p.y, w, 20000.0f);
        OkayUI::PopClipRect();
        OkayUI::EndCapture(cap);
        *height = measured(hgt);
        ImGui::InvisibleButton(itemId, ImVec2(w, std::max(*height, 8.0f)));
        return true;
    }
    const float h = std::max(*height, 8.0f);
    ImGui::InvisibleButton(itemId, ImVec2(w, h));
    if (HiddenInImGui(win)) return true;                      // a frame ImGui lays out but keeps hidden: the space stays, nothing is drawn
    ImRect shown(sp.x, sp.y, sp.x + w, sp.y + h);             // the part of it the window shows (ImGui's coordinates)
    shown.ClipWithFull(win->InnerClipRect);
    if (shown.GetWidth() <= 0.0f || shown.GetHeight() <= 0.0f) return true;   // scrolled out of view: the space stays, nothing is drawn
    const ImVec2 o0 = ToMainWindowPixels(shown.Min), o1 = ToMainWindowPixels(shown.Max);
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    float hgt = *height;
    detail::EmbedBody(win, shown.Min, shown.Max, ImRect(o0, o1), hovered, focused, region, embed, cap, [&] {
        OkayUI::PushClipRect(o0.x, o0.y, o1.x - o0.x, o1.y - o0.y);
        hgt = draw(p.x, p.y, w, h + 400.0f);                  // room below: the height is measured, not cut
        OkayUI::PopClipRect();
    });
    if (hgt > 1.0f && hgt == hgt) *height = std::min(hgt, 20000.0f);   // (a body that answered nothing useful keeps the last height)
    return true;
}

} // namespace okayuihost
