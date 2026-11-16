// The editor's Problems, Stats and History panels, Dear ImGui version against OkayUI port, headless and side by side.
//
// Both draw the same model (shared/EditorPanelModels.hpp, sample data in okayui/tests/EditorPanelFixtures.hpp) into the same
// rectangle: a 560 x 420 canvas in the editor's panel colour, the panel's content inside the editor window's padding (16, 12).
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
#include "OkayTheme.hpp"
#include "RobotoFont.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace UI = OkayUI;
namespace R = OkayUI::render;
namespace P = okaypanels;
using okayuitest::Bitmap;

static const int kW = 560, kH = 420;
static const float kPadX = 16.0f, kPadY = 12.0f;
static const std::uint8_t kBg[3] = {30, 33, 41};   // okaytheme::kBase, the editor's panel colour

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
    ImGuiRig imr; OkayRig ok;
    const float bx = kPadX, by = kPadY, bw = kW - 2 * kPadX, bh = kH - 2 * kPadY;
    const oked::PanelViewHooks hooks{nullptr, nullptr};
    P::ProblemsModel pm = panelfix::Problems(); P::ProblemsState pst;
    panelfix::StatsData sd; P::HistoryModel hm = panelfix::History();
    std::function<void()> imBody, okBody;
    if (s.rfind("problems", 0) == 0) { imBody = [&] { oked::DrawProblemsBodyImGui(pm, pst, hooks); }; okBody = [&] { okayuiscreens::DrawProblems(pm, pst, bx, by, bw, bh); }; }
    else if (s.rfind("stats", 0) == 0) { imBody = [&] { oked::DrawStatsBodyImGui(sd.model, hooks); }; okBody = [&] { okayuiscreens::DrawStats(sd.model, bx, by, bw, bh); }; }
    else { imBody = [&] { oked::DrawHistoryBodyImGui(hm, hooks); }; okBody = [&] { okayuiscreens::DrawHistory(hm, bx, by, bw, bh); }; }
    for (int i = 0; i < 10; ++i) { if (im) imr.Frame(imBody); else ok.Frame(okBody); }   // warm up: atlases, tables, layouts
    if (im) ProfiledFrames([&] { imr.Frame(imBody); }); else ProfiledFrames([&] { ok.Frame(okBody); });
    return 0;
}

int main() {
    if (const char* prof = std::getenv("OKAY_UI_PROFILE")) return Profile(prof);
    RUN_SUITE("okayui editor panels against dear imgui");
    const oked::PanelViewHooks hooks{nullptr, nullptr};
    const float bx = kPadX, by = kPadY, bw = kW - 2 * kPadX, bh = kH - 2 * kPadY;

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
        CHECK(std::abs(imRowY - (r6.y + r6.h * 0.5f)) < 16.0f);
        const Timing t = Time(im, ok, ras, imBody, okBody, Frames());
        Report("history", imPic, okPic, t);
    }

    // ================= the tables =================
    std::printf("\n| Panel | Dear ImGui build | OkayUI build | Dear ImGui build + render | OkayUI build + render | Faster? |\n|---|---:|---:|---:|---:|---|\n");
    for (const Row& r : g_rows)
        std::printf("| %s | %.1f us | %.1f us | %.1f us | %.1f us | %s |\n", r.panel, r.t.imBuild, r.t.okBuild, r.t.imAll, r.t.okAll,
                    r.t.okBuild <= r.t.imBuild && r.t.okAll <= r.t.imAll ? "met" : "not met");
    std::printf("\n| Panel | Average difference (0-255) | Clearly different pixels |\n|---|---:|---:|\n");
    for (const Row& r : g_rows) std::printf("| %s | %.2f | %.2f%% |\n", r.panel, r.d.avg, r.d.clearly);
    std::printf("\n");
    for (const Row& r : g_rows) {
        if (Strict()) CHECK(r.t.okBuild <= r.t.imBuild * 1.10);   // 10% for timer noise
        CHECK(r.d.clearly < 12.0);                                 // the ceiling the ports must stay under
    }
    TEST_MAIN_RESULT();
}
