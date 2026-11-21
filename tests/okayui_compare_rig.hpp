#pragma once
// The rig the Dear ImGui against OkayUI comparisons share (tests/test_okayui_editor_panels_imgui.cpp, test_okayui_hierarchy_imgui.cpp):
// Dear ImGui headless with the editor's look, OkayUI with the launcher look, one software rasteriser for both pictures, the pixel
// difference, the timing, the pictures written with OKAYUI_SHOT_DIR. Include after the toolkits' headers.
#include "OkayUITestKit.hpp"
#include "OkayUIEditorPanels.hpp"
#include "okay/UI/OkayUIRender.hpp"
#include "okay/UI/OkayUIRenderBackends.hpp"
#include "okay/Core/ImageWrite.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "OkayTheme.hpp"
#include "RobotoFont.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace okaycompare {
namespace UI = OkayUI;
namespace R = OkayUI::render;
using okayuitest::Bitmap;

inline const int kW = 560;
inline int kH = 420;   // the canvas height of the panel being compared (set before its rigs are made)
inline const float kPadX = 16.0f, kPadY = 12.0f;
inline const std::uint8_t kBg[3] = {30, 33, 41};   // okaytheme::kBase, the editor's panel colour

// ---- the editor's search field ----------------------------------------------------------------------------------------------
// editor/EditorWidgets.cpp SearchField and its magnifier icon (DrawTbIcon Search), repeated here because that file needs the whole
// editor to compile. Keep the two in step.
inline bool EditorSearchField(const char* id, char* buf, std::size_t bufSize, const char* hint, float width) {
    ImGuiStyle& st = ImGui::GetStyle();
    const float h = ImGui::GetFrameHeight();
    float w = width > 0.0f ? width : ImGui::GetContentRegionAvail().x + width;
    if (w < h * 3.0f) w = h * 3.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(h * 0.95f, st.FramePadding.y));
    ImGui::SetNextItemWidth(w);
    bool changed = ImGui::InputTextWithHint(id, hint, buf, bufSize);
    ImGui::PopStyleVar();
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    {
        const ImVec2 c(mn.x + h * 0.5f, (mn.y + mx.y) * 0.5f);
        const float s = h * 0.42f, t = 1.6f, r = s * 0.27f;
        const ImVec2 o(c.x - s * 0.08f, c.y - s * 0.08f);
        const ImU32 col = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        dl->AddCircle(o, r, col, 14, t);
        dl->AddLine(ImVec2(o.x + r * 0.7f, o.y + r * 0.7f), ImVec2(c.x + s * 0.4f, c.y + s * 0.4f), col, t + 0.6f);
    }
    if (buf[0]) {
        const ImVec2 xc(mx.x - h * 0.5f, (mn.y + mx.y) * 0.5f);
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const bool hot = std::fabs(mp.x - xc.x) < h * 0.45f && std::fabs(mp.y - xc.y) < h * 0.45f &&
                         ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const float r = h * 0.17f;
        const ImU32 xcCol = ImGui::GetColorU32(hot ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        dl->AddLine(ImVec2(xc.x - r, xc.y - r), ImVec2(xc.x + r, xc.y + r), xcCol, 1.6f);
        dl->AddLine(ImVec2(xc.x - r, xc.y + r), ImVec2(xc.x + r, xc.y - r), xcCol, 1.6f);
        if (hot && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { buf[0] = '\0'; changed = true; ImGui::ClearActiveID(); }
    }
    return changed;
}

// ---- Dear ImGui, headless --------------------------------------------------------------------------------------------------
struct ImGuiRig {
    ImGuiContext* ctx = nullptr;
    ImFont* heading = nullptr;
    float mx = -100, my = -100; bool down = false, rdown = false;
    float wheel = 0.0f;
    std::string text;
    explicit ImGuiRig() {
        ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; io.LogFilename = nullptr;
        io.DisplaySize = ImVec2((float)kW, (float)kH);
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        ImFontConfig fc; fc.OversampleH = 2; fc.OversampleV = 2; fc.PixelSnapH = true;
        io.Fonts->AddFontFromMemoryCompressedBase85TTF(RobotoMedium_compressed_data_base85, 18.0f, &fc);
        heading = io.Fonts->AddFontFromMemoryCompressedBase85TTF(RobotoMedium_compressed_data_base85, 22.0f, &fc);
        // editor/main.cpp ApplyTheme with the default settings: Slate theme, the first accent, comfortable density, scale 1
        ImGui::StyleColorsDark();
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowPadding = ImVec2(16, 12); s.FramePadding = ImVec2(12, 8);
        s.ItemSpacing = ImVec2(12, 8);    s.ItemInnerSpacing = ImVec2(8, 6);
        s.CellPadding = ImVec2(10, 6);    s.IndentSpacing = 22.0f;
        s.ScrollbarSize = 14.0f;          s.GrabMinSize = 16.0f;
        s.WindowBorderSize = 0.0f;        s.FrameBorderSize = 0.0f;
        s.PopupBorderSize = 1.0f;
        s.ButtonTextAlign = ImVec2(0.5f, 0.5f);
        okaytheme::ApplySlateMetrics(s);
        s.WindowBorderSize = 0.0f;
        okaytheme::ApplySlateColors(s, ImVec4(0.235f, 0.475f, 0.800f, 1.0f));
        s.Colors[ImGuiCol_TextDisabled] = ImVec4(0.70f, 0.74f, 0.82f, 1.0f);
    }
    ~ImGuiRig() { ImGui::DestroyContext(ctx); }
    ImGuiRig(const ImGuiRig&) = delete;
    // One frame: the panel body inside a window that fills the canvas, the way a docked editor panel's content area does.
    void Frame(const std::function<void()>& body) {
        ImGui::SetCurrentContext(ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = 1.0f / 60.0f;
        io.AddMousePosEvent(mx, my);
        io.AddMouseButtonEvent(0, down);
        io.AddMouseButtonEvent(1, rdown);
        if (wheel != 0.0f) io.AddMouseWheelEvent(0.0f, wheel);
        if (!text.empty()) io.AddInputCharactersUTF8(text.c_str());
        wheel = 0.0f; text.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)kW, (float)kH));
        ImGui::Begin("##panel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        body();
        ImGui::End();
        ImGui::Render();
    }
    void Click(float x, float y, const std::function<void()>& body) { mx = x; my = y; Frame(body); down = true; Frame(body); down = false; Frame(body); }
    void RightClick(float x, float y, const std::function<void()>& body) { mx = x; my = y; Frame(body); rdown = true; Frame(body); rdown = false; Frame(body); }
};

// Dear ImGui's draw data as OkayUI draw data (same vertex layout after conversion), drawn by OkayUI's render front end.
struct ImGuiToOkay {
    std::vector<UI::DrawVertex> vtx;
    std::vector<int> idx;
    std::vector<UI::DrawCmd> cmds;
    void Textures(R::Renderer& r) {
        ImDrawData* dd = ImGui::GetDrawData();
        if (!dd || !dd->Textures) return;
        for (ImTextureData* t : *dd->Textures) {
            if (t->Status == ImTextureStatus_WantCreate || t->Status == ImTextureStatus_WantUpdates) {
                std::vector<std::uint8_t> rgba((std::size_t)t->Width * t->Height * 4);
                const std::uint8_t* src = (const std::uint8_t*)t->GetPixels();
                if (t->Format == ImTextureFormat_RGBA32) std::memcpy(rgba.data(), src, rgba.size());
                else for (int i = 0; i < t->Width * t->Height; ++i) { rgba[(std::size_t)i * 4] = rgba[(std::size_t)i * 4 + 1] = rgba[(std::size_t)i * 4 + 2] = 255; rgba[(std::size_t)i * 4 + 3] = src[i]; }
                r.SetImage(t, t->Width, t->Height, rgba.data(), true);
                t->SetTexID((ImTextureID)(std::uintptr_t)t);
                t->SetStatus(ImTextureStatus_OK);
            } else if (t->Status == ImTextureStatus_WantDestroy && t->UnusedFrames > 0) {
                r.RemoveImage(t);
                t->SetTexID(ImTextureID_Invalid);
                t->SetStatus(ImTextureStatus_Destroyed);
            }
        }
    }
    UI::DrawData Convert() {
        vtx.clear(); idx.clear(); cmds.clear();
        ImDrawData* dd = ImGui::GetDrawData();
        for (const ImDrawList* l : dd->CmdLists) {
            const int base = (int)vtx.size();
            for (const ImDrawVert& v : l->VtxBuffer) {
                UI::DrawVertex o;
                o.x = v.pos.x; o.y = v.pos.y; o.u = v.uv.x; o.v = v.uv.y;
                o.r = (std::uint8_t)(v.col >> IM_COL32_R_SHIFT); o.g = (std::uint8_t)(v.col >> IM_COL32_G_SHIFT);
                o.b = (std::uint8_t)(v.col >> IM_COL32_B_SHIFT); o.a = (std::uint8_t)(v.col >> IM_COL32_A_SHIFT);
                vtx.push_back(o);
            }
            for (const ImDrawCmd& c : l->CmdBuffer) {
                if (c.UserCallback || c.ElemCount == 0) continue;
                UI::DrawCmd o;
                o.idxOffset = (int)idx.size(); o.idxCount = (int)c.ElemCount;
                for (unsigned i = 0; i < c.ElemCount; ++i) idx.push_back(base + (int)c.VtxOffset + (int)l->IdxBuffer[(int)(c.IdxOffset + i)]);
                o.clipOn = true;
                o.clipX = (int)std::floor(c.ClipRect.x); o.clipY = (int)std::floor(c.ClipRect.y);
                o.clipW = (int)std::ceil(c.ClipRect.z) - o.clipX; o.clipH = (int)std::ceil(c.ClipRect.w) - o.clipY;
                o.texture = (const void*)(std::uintptr_t)c.GetTexID();
                cmds.push_back(o);
            }
        }
        UI::DrawData out;
        out.vertices = vtx.data(); out.vertexCount = (int)vtx.size();
        out.indices = idx.data(); out.indexCount = (int)idx.size(); out.vertexStride = (int)sizeof(UI::DrawVertex);
        out.cmds = cmds.data(); out.cmdCount = (int)cmds.size();
        return out;
    }
};

// One rasteriser for both pictures.
struct Raster {
    Bitmap bmp{kW, kH};
    std::unique_ptr<R::Backend> backend = R::CreateSoftBackend(bmp);
    R::Renderer renderer{*backend};
    ImGuiToOkay conv;
    R::FrameInfo fi;
    Raster() { fi.fbWidth = kW; fi.fbHeight = kH; fi.dpi = 1.0f; }
    Raster(const Raster&) = delete;
    void Clear() { bmp.Clear(kBg[0], kBg[1], kBg[2]); }
    R::Stats DrawImGui() { Clear(); conv.Textures(renderer); return renderer.Render(conv.Convert(), fi); }
    R::Stats DrawOkay() { Clear(); return renderer.Render(UI::GetDrawData(), fi); }
};

// ---- OkayUI ------------------------------------------------------------------------------------------------------------------
struct OkayRig {
    okayuitest::Sim sim{(float)kW, (float)kH};
    OkayRig() { okayuitest::Reset(); UI::UseLauncherLook(true); UI::Style().caretBlink = false; }
    ~OkayRig() { UI::UseLauncherLook(false); }
    void Frame(const std::function<void()>& body) { sim.Frame([&] { UI::PushTextScale(okayuiscreens::kEditorTextScale); body(); UI::PopTextScale(); }); }
};

// ---- measuring --------------------------------------------------------------------------------------------------------------
struct Diff { double avg = 0.0; double clearly = 0.0; };
inline Diff Compare(const Bitmap& a, const Bitmap& b) {
    Diff d; double sum = 0.0; std::size_t clear = 0;
    const std::size_t n = (std::size_t)a.w * a.h;
    for (std::size_t i = 0; i < n; ++i) {
        int mx = 0;
        for (int k = 0; k < 3; ++k) { const int e = std::abs((int)a.rgba[i * 4 + k] - (int)b.rgba[i * 4 + k]); sum += e; mx = std::max(mx, e); }
        if (mx > 48) ++clear;
    }
    d.avg = sum / (double)(n * 3);
    d.clearly = 100.0 * (double)clear / (double)n;
    return d;
}
inline double Median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v.empty() ? 0.0 : v[v.size() / 2]; }
inline double Us(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double, std::micro>(b - a).count();
}
inline void Save(const char* panel, const char* which, const Bitmap& b) {
    const char* dir = std::getenv("OKAYUI_SHOT_DIR");
    if (!dir || !dir[0]) return;
    okay::ImageWrite::WritePNG(std::string(dir) + "/" + panel + "-" + which + ".png", b.w, b.h, 4, b.rgba.data());
}

struct Timing { double imBuild = 0, okBuild = 0, imAll = 0, okAll = 0; };

// Time `frames` frames of each, alternating in blocks so a busy machine slows both alike.
inline Timing Time(ImGuiRig& im, OkayRig& ok, Raster& ras, const std::function<void()>& imBody, const std::function<void()>& okBody, int frames) {
    std::vector<double> ib, ob, ia, oa;
    for (int block = 0; block < frames / 25; ++block) {
        for (int i = 0; i < 25; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            im.Frame(imBody);
            const auto t1 = std::chrono::steady_clock::now();
            ras.DrawImGui();
            const auto t2 = std::chrono::steady_clock::now();
            ib.push_back(Us(t0, t1)); ia.push_back(Us(t0, t2));
        }
        for (int i = 0; i < 25; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            ok.Frame(okBody);
            const auto t1 = std::chrono::steady_clock::now();
            ras.DrawOkay();
            const auto t2 = std::chrono::steady_clock::now();
            ob.push_back(Us(t0, t1)); oa.push_back(Us(t0, t2));
        }
    }
    return Timing{Median(ib), Median(ob), Median(ia), Median(oa)};
}


} // namespace okaycompare
