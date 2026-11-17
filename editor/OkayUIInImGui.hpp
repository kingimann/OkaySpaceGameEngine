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
#include "imgui_internal.h"   // ImGui::SetKeyOwner (the per-key owner ImGui's own text field uses; main.cpp uses it too)
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUICodeEdit.hpp"
#include "okay/Platform/Painter.hpp"
#include "okay/Graphics/TtfFont.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <functional>
#include <unordered_map>

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
/// moved: they stay on top; a CodeEdit's own hover help is moved into ImGui's tooltip layer with SetHostDrawsTooltip and
/// ShowCodeHover. `geo` is scratch space kept between frames.
inline void DrawOkayUIHere(const char* id, ImVec2 pos, ImVec2 size, const std::function<void()>& draw,
                           const std::function<ImTextureID(const void*)>& textureFor, OkayUI::CapturedGeometry& geo) {
    OkayUI::BeginGeometryCapture();
    OkayUI::BareNextWindow();
    if (OkayUI::Begin(id, pos.x, pos.y, size.x, size.y)) draw();
    OkayUI::End();
    OkayUI::EndGeometryCapture(geo);
    AppendGeometry(ImGui::GetWindowDrawList(), geo, textureFor);
}

#ifdef OKAY_PLATFORM_HAVE_SDL2
/// The texture lookup for ImGui's SDL_Renderer backend (its ImTextureID is the SDL_Texture pointer).
inline std::function<ImTextureID(const void*)> SdlRendererTextures(SDL_Renderer* renderer) {
    return [renderer](const void* t) -> ImTextureID {
        SDL_Texture* s = OkayUI::TextureForRenderer(renderer, t);
        return s ? (ImTextureID)(std::intptr_t)s : ImTextureID_Invalid;
    };
}
#endif

/// The texture lookup for our own ImGui renderer on an okay::platform::Painter (shared/ImGuiOwnRenderer: its ImTextureID is the
/// painter's okay::ui2d::Tex). OkayUI's glyph atlas and its fallback fonts' atlases are uploaded to the painter the first time
/// they are asked for and again when their size (or a font's generation) changes, the way OkayUI::render::Renderer does it.
/// One cache per painter; a different painter starts a new cache (the old painter owned and freed its textures).
class PainterTextureCache {
public:
    ImTextureID Get(okay::platform::Painter* p, const void* key) {
        if (!p || !key) return ImTextureID_Invalid;
        if (p != painter_) { m_.clear(); painter_ = p; }
        const unsigned char* rgba = nullptr; int w = 0, h = 0; std::uint32_t gen = 0;
        const OkayUI::AtlasView av = OkayUI::GetAtlasView();
        if (key == av.key) { rgba = av.rgba; w = av.w; h = av.h; }
        else if (okay::TtfFont* f = OkayUI::FallbackFontOf(key)) {
            if (!f->Valid()) return ImTextureID_Invalid;
            rgba = f->Atlas().Data(); w = f->Atlas().Width(); h = f->Atlas().Height(); gen = f->Generation();
        } else return ImTextureID_Invalid;   // an image key: hosted code never draws one
        if (!rgba || w <= 0 || h <= 0) return ImTextureID_Invalid;
        Entry& e = m_[key];
        if (e.tex && (e.w != w || e.h != h)) { p->DestroyTexture(e.tex); e = Entry{}; }
        if (e.tex && e.gen != gen) { if (p->UpdateTexture(e.tex, nullptr, rgba, w * 4)) e.gen = gen; }
        if (!e.tex) {
            okay::platform::TextureDesc d; d.width = w; d.height = h; d.blend = okay::platform::BlendMode::Blend;
            d.scale = okay::platform::ScaleMode::Linear;
            e.tex = p->CreateTexture(d, rgba); e.w = w; e.h = h; e.gen = gen;
        }
        return e.tex ? (ImTextureID)(std::uintptr_t)e.tex : ImTextureID_Invalid;
    }
private:
    struct Entry { okay::ui2d::Tex tex = nullptr; int w = 0, h = 0; std::uint32_t gen = 0; };
    okay::platform::Painter* painter_ = nullptr;
    std::unordered_map<const void*, Entry> m_;
};

/// The lookup for the own renderer: `painter` is called each time (the editor's painter can change when the renderer restarts).
inline std::function<ImTextureID(const void*)> PainterTextures(okay::platform::Painter* (*painter)()) {
    static PainterTextureCache cache;
    return [painter](const void* t) -> ImTextureID { return cache.Get(painter ? painter() : nullptr, t); };
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

/// The keys our code editor acts on that Dear ImGui's keyboard navigation also reads: the arrows (move the nav cursor to
/// another button), Space and Enter (press the button the nav cursor is on), Tab (focus the next widget), Escape (leave the
/// window), Page Up / Down and Home / End (scroll the window).
inline const ImGuiKey* CodeEditorKeys(int& count) {
    static const ImGuiKey k[] = {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow,
                                 ImGuiKey_PageUp, ImGuiKey_PageDown, ImGuiKey_Home, ImGuiKey_End,
                                 ImGuiKey_Space, ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Tab, ImGuiKey_Escape,
                                 ImGuiKey_Backspace, ImGuiKey_Delete, ImGuiKey_Insert};
    count = (int)(sizeof k / sizeof k[0]);
    return k;
}

/// Our code editor has the keyboard (call after it drew, every frame it does): claim it from Dear ImGui the way an ImGui text
/// field does.
///  - The editing keys (CodeEditorKeys) get `owner` (the ImGui item that holds the code) as their key owner, so ImGui's keyboard
///    navigation, which only reads keys nobody owns, leaves them alone from the next frame on. Code that asks
///    ImGui::IsKeyPressed(key) without an owner (the script editor's own shortcuts, the autocomplete keys) still sees them.
///  - io.WantCaptureKeyboard and io.WantTextInput are set for the rest of this frame and for the host's event loop before the
///    next one: the editor's global shortcuts, the game during Play and the scene view's keys stay out (the same flags
///    ImGuiColorTextEdit sets while its window is focused).
///  - The characters typed this frame are taken out of ImGui's queue (ours has used them), so nothing drawn later types them.
inline void ClaimKeyboardForCode(ImGuiID owner) {
    if (owner == 0) return;
    int n = 0;
    const ImGuiKey* keys = CodeEditorKeys(n);
    for (int i = 0; i < n; ++i) ImGui::SetKeyOwner(keys[i], owner);
    ImGuiIO& io = ImGui::GetIO();
    io.WantCaptureKeyboard = true;
    io.WantTextInput = true;
    io.InputQueueCharacters.resize(0);
}

/// Run `draw` (OkayUI widgets drawn this frame) and give back the mouse cursor shape they asked for, leaving OkayUI's own cursor
/// as it was before (other OkayUI content of the frame keeps its choice).
inline OkayUI::MouseCursor CursorOf(const std::function<void()>& draw) {
    const OkayUI::MouseCursor before = OkayUI::GetMouseCursor();
    OkayUI::SetMouseCursor(OkayUI::MouseCursor_Arrow);
    draw();
    const OkayUI::MouseCursor mine = OkayUI::GetMouseCursor();
    if (mine == OkayUI::MouseCursor_Arrow) OkayUI::SetMouseCursor(before);
    return mine;
}

/// Show an OkayUI cursor shape through Dear ImGui (whose backend sets the system cursor; the editor does not read OkayUI's).
/// The arrow changes nothing (ImGui's own choice stays).
inline void SetImGuiCursor(OkayUI::MouseCursor c) {
    switch (c) {
    case OkayUI::MouseCursor_TextInput:  ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput); break;
    case OkayUI::MouseCursor_ResizeAll:  ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll); break;
    case OkayUI::MouseCursor_ResizeNS:   ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS); break;
    case OkayUI::MouseCursor_ResizeEW:   ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); break;
    case OkayUI::MouseCursor_ResizeNESW: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW); break;
    case OkayUI::MouseCursor_ResizeNWSE: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE); break;
    case OkayUI::MouseCursor_Hand:       ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); break;
    case OkayUI::MouseCursor_NotAllowed: ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed); break;
    default: break;
    }
}

/// Draw our code editor's hover help (CodeEdit::Hover, with CodeEdit::SetHostDrawsTooltip on) as a Dear ImGui tooltip, in ImGui's
/// tooltip layer: above the windows and popups that are open, the way ImGuiColorTextEdit shows it (an error or warning title in
/// red or amber, a separator, the message; a built-in's help as plain text). Uses the current ImGui font (the script editor's
/// code font while it is pushed). Returns true when a tooltip was shown.
inline bool ShowCodeHover(const OkayUI::CodeHover& h) {
    if (h.kind == OkayUI::CodeHover::None || h.text.empty()) return false;
    ImGui::BeginTooltip();
    if (h.kind == OkayUI::CodeHover::Info) {
        ImGui::TextUnformatted(h.text.c_str());
    } else {
        const bool warn = h.kind == OkayUI::CodeHover::Warning;
        ImGui::PushStyleColor(ImGuiCol_Text, warn ? ImVec4(1.0f, 0.75f, 0.25f, 1.0f) : ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        ImGui::Text(warn ? "Warning at line %d:" : "Error at line %d:", h.line + 1);
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.92f, 0.92f, 1.0f));
        ImGui::TextUnformatted(h.text.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndTooltip();
    return true;
}

} // namespace okayimgui
