// The editor's Problems, Stats, History, Tags and Layers, Physics, Profiler, Import settings, Post-processing, Player and Rendering
// panels and sections, Dear ImGui version against OkayUI port, headless and side by side.
//
// Both draw the same model (shared/EditorPanelModels.hpp, sample data in okayui/tests/EditorPanelFixtures.hpp) into the same
// rectangle: a 560 x 420 canvas (560 x 780 for Tags and Layers and the Profiler, 560 x 1240 for Post-processing) in the editor's panel colour, the panel's content inside the editor window's padding (16, 12).
// Dear ImGui runs with the editor's look (Roboto Medium 18 px, the Slate theme and spacing from editor/main.cpp ApplyTheme), the
// body is editor/EditorPanelViews.cpp, the one the editor itself calls. OkayUI runs with the launcher look the editor uses.
// Both pictures go through the same rasteriser (OkayUI's software render backend; Dear ImGui's vertices are handed to it as they
// are), so the comparison is between the two toolkits' geometry, not between two rasterisers.
//
// What it checks and reports:
//   - both versions draw every panel and act on the same input (a click on the same control asks for the same thing);
//   - speed: the median time per frame to build the panel (Dear ImGui: NewFrame..Render; OkayUI: BeginFrame..EndFrameData) and to
//     build and rasterise it, over many frames. Printed only: wall-clock time on a shared CI machine is too noisy to fail on.
//     OKAY_UI_COMPARE_STRICT=1 makes "OkayUI's build is not slower (10% for noise)" a check. For a figure that does not depend on
//     the machine's load, count instructions with callgrind and OKAY_UI_PROFILE (below);
//   - pixels: the average difference (0..255 per channel) and the share of pixels that are clearly different (any channel more
//     than 48 apart). Printed; the check is a ceiling that the ports must stay under;
//   - with OKAYUI_SHOT_DIR set, both pictures are written as <dir>/<panel>-imgui.png and <dir>/<panel>-okayui.png.
#include "test_framework.hpp"
#include "EditorPanelViews.hpp"
#include "EditorPanelFixtures.hpp"
#include "OkayUIEditorPanels.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIRender.hpp"
#include "okay/UI/OkayUIRenderBackends.hpp"
#include "okay/Core/ImageWrite.hpp"

#include "imgui.h"
#include "imgui_internal.h"   // ClearActiveID, as the editor's search field uses it
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

namespace UI = OkayUI;
namespace R = OkayUI::render;
namespace P = okaypanels;
using okayuitest::Bitmap;

static const int kW = 560;
static int kH = 420;   // the canvas height of the panel being compared (set before its rigs are made)
static const float kPadX = 16.0f, kPadY = 12.0f;
static const std::uint8_t kBg[3] = {30, 33, 41};   // okaytheme::kBase, the editor's panel colour

// ---- the editor's search field ----------------------------------------------------------------------------------------------
// editor/EditorWidgets.cpp SearchField and its magnifier icon (DrawTbIcon Search), repeated here because that file needs the whole
// editor to compile. Keep the two in step.
static bool EditorSearchField(const char* id, char* buf, std::size_t bufSize, const char* hint, float width) {
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
static Diff Compare(const Bitmap& a, const Bitmap& b) {
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
static double Median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v.empty() ? 0.0 : v[v.size() / 2]; }
static double Us(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double, std::micro>(b - a).count();
}
static void Save(const char* panel, const char* which, const Bitmap& b) {
    const char* dir = std::getenv("OKAYUI_SHOT_DIR");
    if (!dir || !dir[0]) return;
    okay::ImageWrite::WritePNG(std::string(dir) + "/" + panel + "-" + which + ".png", b.w, b.h, 4, b.rgba.data());
}

struct Timing { double imBuild = 0, okBuild = 0, imAll = 0, okAll = 0; };

// Time `frames` frames of each, alternating in blocks so a busy machine slows both alike.
static Timing Time(ImGuiRig& im, OkayRig& ok, Raster& ras, const std::function<void()>& imBody, const std::function<void()>& okBody, int frames) {
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

struct Row { const char* panel; Timing t; Diff d; };
static std::vector<Row> g_rows;

static void Report(const char* panel, const Bitmap& imPic, const Bitmap& okPic, const Timing& t) {
    const Diff d = Compare(imPic, okPic);
    Save(panel, "imgui", imPic);
    Save(panel, "okayui", okPic);
    g_rows.push_back(Row{panel, t, d});
}

static bool Strict() { const char* s = std::getenv("OKAY_UI_COMPARE_STRICT"); return s && s[0] == '1'; }
static int Frames() { const char* s = std::getenv("OKAY_UI_COMPARE_FRAMES"); const int n = s ? std::atoi(s) : 0; return n >= 50 ? n : 150; }

// For an instruction count that does not depend on how busy the machine is: OKAY_UI_PROFILE=<panel>:<imgui|okayui> runs 200 frames of
// one side only through ProfiledFrames and exits, so `valgrind --tool=callgrind --collect-atstart=no
// --toggle-collect='ProfiledFrames*'` counts just those frames (build, no rasterising).
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void ProfiledFrames(const std::function<void()>& frame) { for (int i = 0; i < 200; ++i) frame(); }

static int Profile(const char* spec) {
    const std::string s = spec;
    const bool im = s.find(":imgui") != std::string::npos;
    if (s.rfind("tags", 0) == 0 || s.rfind("profiler", 0) == 0) kH = 780;
    if (s.rfind("post", 0) == 0) kH = 1240;
    ImGuiRig imr; OkayRig ok;
    const float bx = kPadX, by = kPadY, bw = kW - 2 * kPadX, bh = kH - 2 * kPadY;
    oked::PanelViewHooks hooks{nullptr, nullptr};
    hooks.searchField = EditorSearchField;
    P::ProblemsModel pm = panelfix::Problems(); P::ProblemsState pst;
    panelfix::StatsData sd; P::HistoryModel hm = panelfix::History();
    const P::TagsLayersModel tm = panelfix::TagsLayers(); P::TagsLayersState tst;
    panelfix::PhysicsData phd; panelfix::ProfilerData prd;
    panelfix::ImportData imd; panelfix::PostData pod; panelfix::PlayerData pld; panelfix::RenderingData rnd;
    std::function<void()> imBody, okBody;
    if (s.rfind("problems", 0) == 0) { imBody = [&] { oked::DrawProblemsBodyImGui(pm, pst, hooks); }; okBody = [&] { okayuiscreens::DrawProblems(pm, pst, bx, by, bw, bh); }; }
    else if (s.rfind("stats", 0) == 0) { imBody = [&] { oked::DrawStatsBodyImGui(sd.model, hooks); }; okBody = [&] { okayuiscreens::DrawStats(sd.model, bx, by, bw, bh); }; }
    else if (s.rfind("tags", 0) == 0) { imBody = [&] { oked::DrawTagsLayersBodyImGui(tm, tst, hooks); }; okBody = [&] { okayuiscreens::DrawTagsLayers(tm, tst, bx, by, bw, bh); }; }
    else if (s.rfind("physics", 0) == 0) { imBody = [&] { oked::DrawPhysicsSettingsBodyImGui(phd.model, hooks); }; okBody = [&] { okayuiscreens::DrawPhysicsSettings(phd.model, bx, by, bw, bh); }; }
    else if (s.rfind("profiler", 0) == 0) { imBody = [&] { oked::DrawProfilerBodyImGui(prd.model, hooks); }; okBody = [&] { okayuiscreens::DrawProfiler(prd.model, bx, by, bw, bh); }; }
    else if (s.rfind("import", 0) == 0) { imBody = [&] { oked::DrawImportSettingsBodyImGui(imd.model, imd.state, hooks); }; okBody = [&] { okayuiscreens::DrawImportSettings(imd.model, imd.state, bx, by, bw, bh); }; }
    else if (s.rfind("post", 0) == 0) { imBody = [&] { oked::DrawPostProcessBodyImGui(pod.model, hooks); }; okBody = [&] { okayuiscreens::DrawPostProcess(pod.model, bx, by, bw, bh); }; }
    else if (s.rfind("player", 0) == 0) { imBody = [&] { oked::DrawPlayerSettingsBodyImGui(pld.model, hooks); }; okBody = [&] { okayuiscreens::DrawPlayerSettings(pld.model, bx, by, bw, bh); }; }
    else if (s.rfind("rendering", 0) == 0) { imBody = [&] { oked::DrawRenderingSettingsBodyImGui(rnd.model, hooks); }; okBody = [&] { okayuiscreens::DrawRenderingSettings(rnd.model, bx, by, bw, bh); }; }
    else { imBody = [&] { oked::DrawHistoryBodyImGui(hm, hooks); }; okBody = [&] { okayuiscreens::DrawHistory(hm, bx, by, bw, bh); }; }
    for (int i = 0; i < 10; ++i) { if (im) imr.Frame(imBody); else ok.Frame(okBody); }   // warm up: atlases, tables, layouts
    if (im) ProfiledFrames([&] { imr.Frame(imBody); }); else ProfiledFrames([&] { ok.Frame(okBody); });
    return 0;
}

int main() {
    if (const char* prof = std::getenv("OKAY_UI_PROFILE")) return Profile(prof);
    RUN_SUITE("okayui editor panels against dear imgui");
    oked::PanelViewHooks hooks{nullptr, nullptr};
    hooks.searchField = EditorSearchField;   // the editor's own field on the Dear ImGui side (the OkayUI side has SearchField)
    const float bx = kPadX, by = kPadY, bw = kW - 2 * kPadX;
    float bh = kH - 2 * kPadY;

    // ================= Problems =================
    {
        const P::ProblemsModel m = panelfix::Problems();
        P::ProblemsState imSt, okSt;
        ImGuiRig im; OkayRig ok; Raster ras;
        oked::PanelViewHooks h = hooks;
        P::ProblemsIntent imIntent; okayuiscreens::ProblemsResult okRes;
        auto imBody = [&] { imIntent = oked::DrawProblemsBodyImGui(m, imSt, h); };
        auto okBody = [&] { okRes = okayuiscreens::DrawProblems(m, okSt, bx, by, bw, bh); };
        for (int i = 0; i < 4; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        ras.DrawImGui(); const Bitmap imPic = ras.bmp;
        ras.DrawOkay();  const Bitmap okPic = ras.bmp;
        CHECK(okRes.rows.size() == 6);
        // the same click on both: the Player row opens model row 2 in both
        P::ProblemsIntent imSeen, okSeen;
        auto imClickBody = [&] { const P::ProblemsIntent in = oked::DrawProblemsBodyImGui(m, imSt, h); if (in.open >= 0) imSeen.open = in.open; };
        auto okClickBody = [&] { okRes = okayuiscreens::DrawProblems(m, okSt, bx, by, bw, bh); if (okRes.intent.open >= 0) okSeen.open = okRes.intent.open; };
        const UI::Rect target = okRes.rows[2];
        ok.sim.Move(target.x + 30, target.y + target.h * 0.5f); ok.Frame(okClickBody); ok.sim.Down(); ok.Frame(okClickBody); ok.sim.Up(); ok.Frame(okClickBody);
        // Dear ImGui's rows are where its layout put them: click down the text column and note where row 2 opens
        float imRowY = -1.0f;
        for (float y = by + 50.0f; y < bh && imRowY < 0.0f; y += 3.0f) {
            imSeen.open = -1;
            im.Click(bx + 120.0f, y, imClickBody);
            if (imSeen.open == 2) imRowY = y;
        }
        CHECK(okSeen.open == 2 && imRowY > 0.0f);
        CHECK(std::abs(imRowY - (target.y + target.h * 0.5f)) < 16.0f);   // and the two layouts put that row within a few pixels
        const Timing t = Time(im, ok, ras, imBody, okBody, Frames());
        Report("problems", imPic, okPic, t);
        // the right-click menu on the same row in both (the menu's look: its colour, padding, row height and width)
        const float mx = target.x + 30.0f, my = target.y + target.h * 0.5f;
        im.RightClick(mx, my, imBody);
        ok.sim.Move(mx, my); ok.Frame(okBody); ok.sim.rdown = true; ok.Frame(okBody); ok.sim.rdown = false; ok.Frame(okBody);
        im.mx = ok.sim.mx = mx + 400.0f; im.my = ok.sim.my = my + 300.0f;   // the pointer away, so no row is hovered
        for (int i = 0; i < 2; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        CHECK(okRes.menuOpen.w > 0.0f);
        ras.DrawImGui(); const Bitmap imMenu = ras.bmp;
        ras.DrawOkay();  const Bitmap okMenu = ras.bmp;
        Report("problems-menu", imMenu, okMenu, Timing{});
    }

    // ================= Stats =================
    {
        panelfix::StatsData imD, okD;
        ImGuiRig im; OkayRig ok; Raster ras;
        oked::PanelViewHooks h = hooks; h.headingFont = im.heading;
        okayuiscreens::StatsResult okRes;
        auto imBody = [&] { oked::DrawStatsBodyImGui(imD.model, h); };
        auto okBody = [&] { okRes = okayuiscreens::DrawStats(okD.model, bx, by, bw, bh); };
        for (int i = 0; i < 4; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        ras.DrawImGui(); const Bitmap imPic = ras.bmp;
        ras.DrawOkay();  const Bitmap okPic = ras.bmp;
        CHECK(okRes.valueCells.size() == okD.model.counters.size());
        const Timing t = Time(im, ok, ras, imBody, okBody, Frames());
        Report("stats", imPic, okPic, t);
    }

    // ================= History =================
    {
        const P::HistoryModel m = panelfix::History();
        ImGuiRig im; OkayRig ok; Raster ras;
        okayuiscreens::HistoryResult okRes;
        P::HistoryIntent imSeen, okSeen;
        auto imBody = [&] { const P::HistoryIntent in = oked::DrawHistoryBodyImGui(m, hooks); if (in.undo || in.redo) imSeen = in; };
        auto okBody = [&] { okRes = okayuiscreens::DrawHistory(m, bx, by, bw, bh); if (okRes.intent.undo || okRes.intent.redo) okSeen = okRes.intent; };
        for (int i = 0; i < 4; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        ras.DrawImGui(); const Bitmap imPic = ras.bmp;
        ras.DrawOkay();  const Bitmap okPic = ras.bmp;
        CHECK(okRes.rows.size() == 8);
        // a click on the sixth row ("Before: Rename Crate", three undos) on both
        const UI::Rect r6 = okRes.rows[5];
        ok.sim.Move(r6.x + 60, r6.y + r6.h * 0.5f); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody); ok.sim.Up(); ok.Frame(okBody);
        float imRowY = -1.0f;
        for (float y = by + 40.0f; y < bh && imRowY < 0.0f; y += 3.0f) {
            imSeen = P::HistoryIntent();
            im.Click(bx + 120.0f, y, imBody);
            if (imSeen.undo == 3) imRowY = y;
        }
        CHECK(okSeen.undo == 3 && imRowY > 0.0f);
        if (std::abs(imRowY - (r6.y + r6.h * 0.5f)) >= 16.0f) std::printf("history row: imgui %.1f okayui %.1f (%.1f %.1f)\n", imRowY, r6.y + r6.h * 0.5f, r6.y, r6.h);
        CHECK(std::abs(imRowY - (r6.y + r6.h * 0.5f)) < 16.0f);
        const Timing t = Time(im, ok, ras, imBody, okBody, Frames());
        Report("history", imPic, okPic, t);
    }

    // ================= Tags and Layers (a taller canvas: the section is tall) =================
    kH = 780; bh = kH - 2 * kPadY;
    {
        const P::TagsLayersModel m = panelfix::TagsLayers();
        P::TagsLayersState imSt, okSt;
        ImGuiRig im; OkayRig ok; Raster ras;
        okayuiscreens::TagsLayersResult okRes;
        P::TagsLayersIntent imSeen, okSeen;
        auto imBody = [&] { const P::TagsLayersIntent in = oked::DrawTagsLayersBodyImGui(m, imSt, hooks); if (in.removeTag >= 0) imSeen.removeTag = in.removeTag; if (in.addTag) imSeen.addTag = true; };
        auto okBody = [&] { okRes = okayuiscreens::DrawTagsLayers(m, okSt, bx, by, bw, bh); if (okRes.intent.removeTag >= 0) okSeen.removeTag = okRes.intent.removeTag; if (okRes.intent.addTag) okSeen.addTag = true; };
        for (int i = 0; i < 4; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        ras.DrawImGui(); const Bitmap imPic = ras.bmp;
        ras.DrawOkay();  const Bitmap okPic = ras.bmp;
        CHECK(okRes.removeButtons.size() == 4 && okRes.layerFields.size() == 32);
        // the same clicks on both, at the OkayUI control's centre: Remove on the second tag, then Add tag
        const UI::Rect rm = okRes.removeButtons[1], add = okRes.addTagButton;
        ok.sim.Move(rm.x + rm.w * 0.5f, rm.y + rm.h * 0.5f); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody); ok.sim.Up(); ok.Frame(okBody);
        im.Click(rm.x + rm.w * 0.5f, rm.y + rm.h * 0.5f, imBody);
        CHECK(okSeen.removeTag == 1 && imSeen.removeTag == 1);
        ok.sim.Move(add.x + add.w * 0.5f, add.y + add.h * 0.5f); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody); ok.sim.Up(); ok.Frame(okBody);
        im.Click(add.x + add.w * 0.5f, add.y + add.h * 0.5f, imBody);
        CHECK(okSeen.addTag && imSeen.addTag);
        im.mx = im.my = ok.sim.mx = ok.sim.my = -100.0f;
        const Timing t = Time(im, ok, ras, imBody, okBody, Frames());
        Report("tagslayers", imPic, okPic, t);
    }

    // ================= Physics =================
    kH = 420; bh = kH - 2 * kPadY;
    {
        panelfix::PhysicsData imD, okD;
        ImGuiRig im; OkayRig ok; Raster ras;
        okayuiscreens::PhysicsSettingsResult okRes;
        bool imApply = false, okApply = false;
        auto imBody = [&] { if (oked::DrawPhysicsSettingsBodyImGui(imD.model, hooks).apply) imApply = true; };
        auto okBody = [&] { okRes = okayuiscreens::DrawPhysicsSettings(okD.model, bx, by, bw, bh); if (okRes.intent.apply) okApply = true; };
        for (int i = 0; i < 4; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        ras.DrawImGui(); const Bitmap imPic = ras.bmp;
        ras.DrawOkay();  const Bitmap okPic = ras.bmp;
        CHECK(okRes.iterations.w > 0 && okRes.continuous.w > 0);
        const UI::Rect a = okRes.apply;
        ok.sim.Move(a.x + a.w * 0.5f, a.y + a.h * 0.5f); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody); ok.sim.Up(); ok.Frame(okBody);
        im.Click(a.x + a.w * 0.5f, a.y + a.h * 0.5f, imBody);
        CHECK(okApply && imApply);
        // a drag on the step rate moves both the same way (one per pixel)
        const UI::Rect r = okRes.stepRate;
        const float sx = r.x + 40.0f, sy = r.y + r.h * 0.5f;
        ok.sim.Move(sx, sy); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody); ok.sim.Move(sx + 10.0f, sy); ok.Frame(okBody); ok.sim.Up(); ok.Frame(okBody);
        im.mx = sx; im.my = sy; im.Frame(imBody); im.down = true; im.Frame(imBody); im.mx = sx + 10.0f; im.Frame(imBody); im.down = false; im.Frame(imBody);
        CHECK(okD.hz > 55.0f && imD.hz > 55.0f);
        imD.hz = okD.hz = 50.0f;
        im.mx = im.my = ok.sim.mx = ok.sim.my = -100.0f;
        const Timing t = Time(im, ok, ras, imBody, okBody, Frames());
        Report("physics", imPic, okPic, t);
    }

    // ================= Profiler (a taller canvas) =================
    kH = 780; bh = kH - 2 * kPadY;
    {
        panelfix::ProfilerData imD, okD;
        ImGuiRig im; OkayRig ok; Raster ras;
        okayuiscreens::ProfilerResult okRes;
        auto imBody = [&] { oked::DrawProfilerBodyImGui(imD.model, hooks); };
        auto okBody = [&] { okRes = okayuiscreens::DrawProfiler(okD.model, bx, by, bw, bh); };
        for (int i = 0; i < 4; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        ras.DrawImGui(); const Bitmap imPic = ras.bmp;
        ras.DrawOkay();  const Bitmap okPic = ras.bmp;
        CHECK(okRes.rows.size() == okD.model.sections.size() && okRes.guide60 > 0.0f && okRes.guide30 == 0.0f);   // the scale (27.6 ms) is under 33.3
        // the same click on Pause pauses both, and both then scrub from the newest frame
        const UI::Rect pb = okRes.pauseBox;
        ok.sim.Move(pb.x + 12.0f, pb.y + pb.h * 0.5f); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody); ok.sim.Up(); ok.Frame(okBody);
        im.Click(pb.x + 12.0f, pb.y + pb.h * 0.5f, imBody);
        CHECK(okD.paused && imD.paused && okD.selected == 239 && imD.selected == 239);
        okD.paused = imD.paused = false;
        im.mx = im.my = ok.sim.mx = ok.sim.my = -100.0f;
        for (int i = 0; i < 2; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        const Timing t = Time(im, ok, ras, imBody, okBody, Frames());
        Report("profiler", imPic, okPic, t);
    }

    // ================= Import settings (a model; then a texture's and a sound's, pictures only) =================
    kH = 420; bh = kH - 2 * kPadY;
    {
        panelfix::ImportData imD, okD;
        ImGuiRig im; OkayRig ok; Raster ras;
        okayuiscreens::ImportSettingsResult okRes;
        bool imApply = false, okApply = false;
        auto imBody = [&] { if (oked::DrawImportSettingsBodyImGui(imD.model, imD.state, hooks).apply) imApply = true; };
        auto okBody = [&] { okRes = okayuiscreens::DrawImportSettings(okD.model, okD.state, bx, by, bw, bh); if (okRes.intent.apply) okApply = true; };
        for (int i = 0; i < 4; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        ras.DrawImGui(); const Bitmap imPic = ras.bmp;
        ras.DrawOkay();  const Bitmap okPic = ras.bmp;
        CHECK(okRes.smoothing.w > 0 && okRes.animations.w > 0 && !okRes.enabled);
        // the same click on "Import animations" turns it off in both; Apply is then enabled in both and the same click asks for it
        const UI::Rect a = okRes.animations;
        ok.sim.Move(a.x + 12.0f, a.y + a.h * 0.5f); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody); ok.sim.Up(); ok.Frame(okBody);
        im.Click(a.x + 12.0f, a.y + a.h * 0.5f, imBody);
        CHECK(!okD.edit.importAnimations && !imD.edit.importAnimations);
        ok.Frame(okBody);
        const UI::Rect ap = okRes.apply;
        ok.sim.Move(ap.x + ap.w * 0.5f, ap.y + ap.h * 0.5f); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody); ok.sim.Up(); ok.Frame(okBody);
        im.Click(ap.x + ap.w * 0.5f, ap.y + ap.h * 0.5f, imBody);
        CHECK(okApply && imApply);
        imD.edit.importAnimations = okD.edit.importAnimations = true;
        im.mx = im.my = ok.sim.mx = ok.sim.my = -100.0f;
        const Timing t = Time(im, ok, ras, imBody, okBody, Frames());
        Report("import", imPic, okPic, t);
    }
    for (int kind : {P::ImportTexture, P::ImportAudio}) {
        panelfix::ImportData imD(kind), okD(kind);
        ImGuiRig im; OkayRig ok; Raster ras;
        auto imBody = [&] { oked::DrawImportSettingsBodyImGui(imD.model, imD.state, hooks); };
        auto okBody = [&] { okayuiscreens::DrawImportSettings(okD.model, okD.state, bx, by, bw, bh); };
        for (int i = 0; i < 4; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        ras.DrawImGui(); const Bitmap imPic = ras.bmp;
        ras.DrawOkay();  const Bitmap okPic = ras.bmp;
        Report(kind == P::ImportTexture ? "import-texture" : "import-audio", imPic, okPic, Timing{});
    }

    // ================= Post-processing (the tallest canvas) =================
    kH = 1240; bh = kH - 2 * kPadY;
    {
        panelfix::PostData imD, okD;
        ImGuiRig im; OkayRig ok; Raster ras;
        okayuiscreens::PostProcessResult okRes;
        auto imBody = [&] { oked::DrawPostProcessBodyImGui(imD.model, hooks); };
        auto okBody = [&] { okRes = okayuiscreens::DrawPostProcess(okD.model, bx, by, bw, bh); };
        for (int i = 0; i < 4; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        ras.DrawImGui(); const Bitmap imPic = ras.bmp;
        ras.DrawOkay();  const Bitmap okPic = ras.bmp;
        CHECK(okRes.lift.w > 0 && okRes.note.w > 0 && okRes.contentHeight < bh);
        // the same click on the Bloom box turns bloom off in both; a click on the exposure slider's left end sets both near 0.1
        const UI::Rect b = okRes.bloom;
        ok.sim.Move(b.x + 12.0f, b.y + b.h * 0.5f); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody); ok.sim.Up(); ok.Frame(okBody);
        im.Click(b.x + 12.0f, b.y + b.h * 0.5f, imBody);
        CHECK(!okD.v.bloom && !imD.v.bloom);
        const UI::Rect e = okRes.exposure;
        ok.sim.Move(e.x + 3.0f, e.y + e.h * 0.5f); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody); ok.sim.Up(); ok.Frame(okBody);
        im.Click(e.x + 3.0f, e.y + e.h * 0.5f, imBody);
        CHECK(okD.v.exposure < 0.2f && imD.v.exposure < 0.2f);
        imD.v.bloom = okD.v.bloom = true; imD.v.exposure = okD.v.exposure = 1.25f;
        im.mx = im.my = ok.sim.mx = ok.sim.my = -100.0f;
        for (int i = 0; i < 2; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        const Timing t = Time(im, ok, ras, imBody, okBody, Frames());
        Report("post", imPic, okPic, t);
    }

    // ================= Player and Rendering (Project Settings) =================
    kH = 420; bh = kH - 2 * kPadY;
    {
        panelfix::PlayerData imD, okD;
        ImGuiRig im; OkayRig ok; Raster ras;
        okayuiscreens::PlayerSettingsResult okRes;
        auto imBody = [&] { oked::DrawPlayerSettingsBodyImGui(imD.model, hooks); };
        auto okBody = [&] { okRes = okayuiscreens::DrawPlayerSettings(okD.model, bx, by, bw, bh); };
        for (int i = 0; i < 4; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        ras.DrawImGui(); const Bitmap imPic = ras.bmp;
        ras.DrawOkay();  const Bitmap okPic = ras.bmp;
        CHECK(okRes.width.w > 0 && okRes.height.x > okRes.width.x + okRes.width.w);
        // the same click on the Width "+" (the field's right end less the label and a step button) steps both by one
        const UI::Rect wp = okRes.widthPlus;
        ok.sim.Move(wp.x + wp.w * 0.5f, wp.y + wp.h * 0.5f); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody); ok.sim.Up(); ok.Frame(okBody);
        im.Click(wp.x + wp.w * 0.5f, wp.y + wp.h * 0.5f, imBody);
        if (okD.width != 1281 || imD.width != 1281) std::printf("width +: okayui %d imgui %d\n", okD.width, imD.width);
        CHECK(okD.width == 1281 && imD.width == 1281);
        imD.width = okD.width = 1280;
        im.mx = im.my = ok.sim.mx = ok.sim.my = -100.0f;
        const Timing t = Time(im, ok, ras, imBody, okBody, Frames());
        Report("player", imPic, okPic, t);
    }
    {
        panelfix::RenderingData imD, okD;
        ImGuiRig im; OkayRig ok; Raster ras;
        okayuiscreens::RenderingSettingsResult okRes;
        bool imApply = false, okApply = false;
        auto imBody = [&] { if (oked::DrawRenderingSettingsBodyImGui(imD.model, hooks).apply) imApply = true; };
        auto okBody = [&] { okRes = okayuiscreens::DrawRenderingSettings(okD.model, bx, by, bw, bh); if (okRes.intent.apply) okApply = true; };
        for (int i = 0; i < 4; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        ras.DrawImGui(); const Bitmap imPic = ras.bmp;
        ras.DrawOkay();  const Bitmap okPic = ras.bmp;
        const UI::Rect a = okRes.apply, sk = okRes.skybox;
        ok.sim.Move(a.x + a.w * 0.5f, a.y + a.h * 0.5f); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody); ok.sim.Up(); ok.Frame(okBody);
        im.Click(a.x + a.w * 0.5f, a.y + a.h * 0.5f, imBody);
        ok.sim.Move(sk.x + 12.0f, sk.y + sk.h * 0.5f); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody); ok.sim.Up(); ok.Frame(okBody);
        im.Click(sk.x + 12.0f, sk.y + sk.h * 0.5f, imBody);
        CHECK(okApply && imApply && !okD.skybox && !imD.skybox);
        imD.skybox = okD.skybox = true;
        im.mx = im.my = ok.sim.mx = ok.sim.my = -100.0f;
        const Timing t = Time(im, ok, ras, imBody, okBody, Frames());
        Report("rendering", imPic, okPic, t);
    }

    // ================= the tables =================
    std::printf("\n| Panel | Dear ImGui build | OkayUI build | Dear ImGui build + render | OkayUI build + render | Faster? |\n|---|---:|---:|---:|---:|---|\n");
    for (const Row& r : g_rows) {
        if (r.t.imBuild <= 0.0) continue;   // a picture only (the menu)
        std::printf("| %s | %.1f us | %.1f us | %.1f us | %.1f us | %s |\n", r.panel, r.t.imBuild, r.t.okBuild, r.t.imAll, r.t.okAll,
                    r.t.okBuild <= r.t.imBuild && r.t.okAll <= r.t.imAll ? "met" : "not met");
    }
    std::printf("\n| Panel | Average difference (0-255) | Clearly different pixels |\n|---|---:|---:|\n");
    for (const Row& r : g_rows) std::printf("| %s | %.2f | %.2f%% |\n", r.panel, r.d.avg, r.d.clearly);
    std::printf("\n");
    for (const Row& r : g_rows) {
        if (Strict() && r.t.imBuild > 0.0) CHECK(r.t.okBuild <= r.t.imBuild * 1.10);   // 10% for timer noise
        CHECK(r.d.clearly < 12.0);                                 // the ceiling the ports must stay under
    }
    TEST_MAIN_RESULT();
}
