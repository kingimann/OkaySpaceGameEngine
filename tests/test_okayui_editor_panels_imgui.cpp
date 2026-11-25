// The editor's Problems, Stats, History, Tags and Layers, Physics, Profiler, Import settings, Post-processing, Player and Rendering
// panels and sections, Dear ImGui version against OkayUI port, headless and side by side.
//
// Both draw the same model (shared/EditorPanelModels.hpp, sample data in okayui/tests/EditorPanelFixtures.hpp) into the same
// rectangle: a 560 x 420 canvas (560 x 780 for Tags and Layers and the Profiler, 560 x 1240 for Post-processing) in the editor's panel colour, the panel's content inside the editor window's padding (16, 12).
// Dear ImGui runs with the editor's look (Roboto Medium 18 px, the Slate theme and spacing from editor/main.cpp ApplyTheme), the
// body is editor/EditorPanelViews.cpp, the one the editor itself calls (History's, which the editor no longer has, is below). OkayUI runs with the launcher look the editor uses.
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
#include "imgui.h"            // first: OkayUI.hpp keeps Dear ImGui's ImTextureID_Invalid when imgui.h came before it
#include "imgui_internal.h"
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

#include "okayui_compare_rig.hpp"
using namespace okaycompare;

// ---- History: the Dear ImGui body the editor had --------------------------------------------------------------------------
// The editor's History panel is drawn by OkayUI only now (editor/OkayPanels.cpp, docs/ui/own_ui_toolkit.md section 24); its Dear
// ImGui body was moved here from editor/EditorPanelViews.cpp, unchanged, as the reference this comparison measures the port against.
// It is not in the editor any more.
static P::HistoryIntent HistoryBodyImGuiReference(const P::HistoryModel& m) {
    P::HistoryIntent out;
    ImGui::BeginDisabled(m.undoDepth <= 0);
    if (ImGui::Button("Undo##hist")) out.undo = 1;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Ctrl+Z");
    ImGui::SameLine();
    ImGui::BeginDisabled(m.redoDepth <= 0);
    if (ImGui::Button("Redo##hist")) out.redo = 1;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Ctrl+Y");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", P::HistoryStatusText(m).c_str());
    ImGui::Separator();
    ImGui::BeginChild("##histlist");
    if (m.undoDepth <= 0 && m.redoDepth <= 0) {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextDisabled("%s", P::kHistoryEmpty);
        ImGui::EndChild();
        return out;
    }
    // Timeline rail down the left: a node per state, the current one filled with the accent colour, future (redo) states
    // hollow and dimmed.
    ImDrawList* hdl = ImGui::GetWindowDrawList();
    const float railX = ImGui::GetCursorScreenPos().x + 9.0f;
    const float railTopY = ImGui::GetCursorScreenPos().y;
    const std::vector<P::HistoryRow> rows = P::HistoryRows(m);
    {
        const float rowH = ImGui::GetTextLineHeightWithSpacing();
        hdl->AddLine(ImVec2(railX, railTopY + 4.0f), ImVec2(railX, railTopY + (float)rows.size() * rowH - 4.0f),
                     ImGui::GetColorU32(ImGuiCol_Border), 1.0f);
    }
    for (std::size_t r = 0; r < rows.size(); ++r) {
        const P::HistoryRow& row = rows[r];
        const bool current = row.kind == P::HistoryCurrent, ahead = row.kind == P::HistoryAhead;
        char lbl[220]; std::snprintf(lbl, sizeof(lbl), "     %s##hrow%d", row.label.c_str(), (int)r);
        if (ahead) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        const bool hit = ImGui::Selectable(lbl, current);
        if (ahead) ImGui::PopStyleColor();
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        const float cy = (mn.y + mx.y) * 0.5f;
        if (current) hdl->AddCircleFilled(ImVec2(railX, cy), 4.0f, ImGui::GetColorU32(ImGuiCol_CheckMark), 12);
        else hdl->AddCircle(ImVec2(railX, cy), 3.0f,
                            ahead ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : ImGui::GetColorU32(ImGuiCol_Text, 0.55f), 12, 1.5f);
        bool jump = hit;
        if (ImGui::BeginPopupContextItem()) {
            if (!current && ImGui::MenuItem("Jump to this state")) jump = true;
            if (ImGui::MenuItem("Copy name")) out.copy = (int)r;
            ImGui::EndPopup();
        }
        if (jump && !current && out.undo == 0 && out.redo == 0) {
            if (ahead) out.redo = row.steps; else out.undo = row.steps;
        }
    }
    ImGui::EndChild();
    return out;
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
    else { imBody = [&] { HistoryBodyImGuiReference(hm); }; okBody = [&] { okayuiscreens::DrawHistory(hm, bx, by, bw, bh); }; }
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
        auto imBody = [&] { const P::HistoryIntent in = HistoryBodyImGuiReference(m); if (in.undo || in.redo) imSeen = in; };
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
        CHECK(okRes.iterations.w > 0);   // (one physics path: no quality list and no continuous box any more)
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
    {   // a sound's, picture only
        panelfix::ImportData imD(P::ImportAudio), okD(P::ImportAudio);
        ImGuiRig im; OkayRig ok; Raster ras;
        auto imBody = [&] { oked::DrawImportSettingsBodyImGui(imD.model, imD.state, hooks); };
        auto okBody = [&] { okayuiscreens::DrawImportSettings(okD.model, okD.state, bx, by, bw, bh); };
        for (int i = 0; i < 4; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        ras.DrawImGui(); const Bitmap imPic = ras.bmp;
        ras.DrawOkay();  const Bitmap okPic = ras.bmp;
        Report("import-audio", imPic, okPic, Timing{});
    }
    kH = 1120; bh = kH - 2 * kPadY;
    {   // a texture with every row (Kind, Anisotropic, Mip filter, compression, what is loaded, the tools): the picture, and the same clicks
        panelfix::ImportData imD(P::ImportTexture), okD(P::ImportTexture);
        ImGuiRig im; OkayRig ok; Raster ras;
        okayuiscreens::ImportSettingsResult okRes;
        bool imGen = false, okGen = false;
        auto imBody = [&] { if (oked::DrawImportSettingsBodyImGui(imD.model, imD.state, hooks).generate) imGen = true; };
        auto okBody = [&] { okRes = okayuiscreens::DrawImportSettings(okD.model, okD.state, bx, by, bw, bh); if (okRes.intent.generate) okGen = true; };
        for (int i = 0; i < 4; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        ras.DrawImGui(); const Bitmap imPic = ras.bmp;
        ras.DrawOkay();  const Bitmap okPic = ras.bmp;
        CHECK(okRes.anisoCombo.w > 0 && okRes.formatCombo.w > 0 && okRes.generate.w > 0 && okRes.contentHeight < bh);
        auto both = [&](const UI::Rect& q) {
            ok.sim.Move(q.x + 12.0f, q.y + q.h * 0.5f); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody); ok.sim.Up(); ok.Frame(okBody);
            im.Click(q.x + 12.0f, q.y + q.h * 0.5f, imBody);
        };
        // the same click on the Cut-out box ticks it in both, and on Generate asks for the .dds in both
        both(okRes.ddsCutout);
        CHECK(okD.state.ddsCutout && imD.state.ddsCutout);
        both(okRes.generate);
        CHECK(okGen && imGen);
        // the same click on "Compress on the GPU" turns it off in both (the Format row goes)
        both(okRes.compress);
        CHECK(!okD.edit.compress && !imD.edit.compress);
        ok.Frame(okBody); CHECK(okRes.formatCombo.w == 0);
        okD.edit.compress = imD.edit.compress = true; okD.state.ddsCutout = imD.state.ddsCutout = false;
        im.mx = im.my = ok.sim.mx = ok.sim.my = -100.0f;
        Report("import-texture", imPic, okPic, Timing{});
    }
    kH = 420; bh = kH - 2 * kPadY;

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
        // no steppers in either (main's plain five-digit fields): a click at the field's right end, where "+" used to be, changes
        // neither value
        const UI::Rect wf = okRes.width;
        const float lw = UI::CalcTextWidth("Width") * okayuiscreens::kEditorTextScale + 8.0f;
        const float cx = wf.x + wf.w - lw - 6.0f, cy = wf.y + wf.h * 0.5f;
        ok.sim.Move(cx, cy); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody); ok.sim.Up(); ok.Frame(okBody);
        im.Click(cx, cy, imBody);
        if (okD.width != 1280 || imD.width != 1280) std::printf("width click: okayui %d imgui %d\n", okD.width, imD.width);
        CHECK(okD.width == 1280 && imD.width == 1280);
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

    // ================= Loading screen (Project Settings) =================
    kH = 420; bh = kH - 2 * kPadY;
    {
        panelfix::LoadingData imD, okD;
        ImGuiRig im; OkayRig ok; Raster ras;
        okayuiscreens::LoadingScreenResult okRes;
        bool imBrowse = false, okBrowse = false;
        auto imBody = [&] { if (oked::DrawLoadingScreenBodyImGui(imD.model, hooks).browse) imBrowse = true; };
        auto okBody = [&] { okRes = okayuiscreens::DrawLoadingScreen(okD.model, bx, by, bw, bh); if (okRes.intent.browse) okBrowse = true; };
        for (int i = 0; i < 4; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        ras.DrawImGui(); const Bitmap imPic = ras.bmp;
        ras.DrawOkay();  const Bitmap okPic = ras.bmp;
        CHECK(okRes.browse.w > 0 && okRes.showBar.w > 0);
        auto both = [&](float x, float y) {
            ok.sim.Move(x, y); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody); ok.sim.Up(); ok.Frame(okBody);
            im.Click(x, y, imBody);
        };
        // the same clicks: Browse asks the host in both; the progress bar box turns it off in both; Clear empties the logo in both
        both(okRes.browse.x + okRes.browse.w * 0.5f, okRes.browse.y + okRes.browse.h * 0.5f);
        CHECK(okBrowse && imBrowse);
        both(okRes.showBar.x + 12.0f, okRes.showBar.y + okRes.showBar.h * 0.5f);
        CHECK(!okD.showBar && !imD.showBar);
        both(okRes.clear.x + okRes.clear.w * 0.5f, okRes.clear.y + okRes.clear.h * 0.5f);
        CHECK(okD.logo[0] == '\0' && imD.logo[0] == '\0');
        im.mx = im.my = ok.sim.mx = ok.sim.my = -100.0f;
        Report("loadingscreen", imPic, okPic, Timing{});
    }

    // ================= section heights (the hosted sections of Project Settings, the Inspector and the Lighting window) =========
    // A host puts one Dear ImGui item as tall as the OkayUI body says it is, and Dear ImGui adds its item spacing after it. The body's
    // height must therefore be the Dear ImGui body's from its first row to the bottom of its last (without the spacing after it), or
    // the next header moves down. Checked with the editor's spacing and with the Inspector's (Appearance > Density, Comfortable:
    // items 10 and 12 apart, frames 9 and 8, labels 8 after; a 250-pixel value column), which the host passes in EditorMetrics.
    kH = 1240; bh = kH - 2 * kPadY;
    {
        struct Look { const char* name; okayuiscreens::EditorMetrics m; };
        okayuiscreens::EditorMetrics insp;
        insp.itemSpacing = UI::Vec2{10.0f, 12.0f}; insp.framePadding = UI::Vec2{9.0f, 8.0f}; insp.innerSpacing = UI::Vec2{8.0f, 6.0f};
        insp.itemWidth = 250.0f;
        const Look looks[] = {{"editor", okayuiscreens::EditorMetrics{}}, {"inspector", insp}};
        for (const Look& look : looks) {
            const bool custom = look.m.itemWidth > 0.0f;
            // One Dear ImGui frame with the look pushed; answers the body's height without the spacing after its last row.
            auto imHeight = [&](ImGuiRig& im, const std::function<void()>& body) {
                float h = -1.0f;
                im.Frame([&] {
                    if (custom) {
                        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(look.m.itemSpacing.x, look.m.itemSpacing.y));
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(look.m.framePadding.x, look.m.framePadding.y));
                        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(look.m.innerSpacing.x, look.m.innerSpacing.y));
                        ImGui::PushItemWidth(look.m.itemWidth);
                    }
                    const float y0 = ImGui::GetCursorScreenPos().y;
                    body();
                    h = ImGui::GetCursorScreenPos().y - y0 - ImGui::GetStyle().ItemSpacing.y;
                    if (custom) { ImGui::PopItemWidth(); ImGui::PopStyleVar(3); }
                });
                return h;
            };
            auto same = [&](const char* what, float imH, float okH) {
                if (std::fabs(imH - okH) > 1.0f) std::printf("height %s (%s): imgui %.1f okayui %.1f\n", what, look.name, imH, okH);
                CHECK(std::fabs(imH - okH) <= 1.0f);
            };
            okayuiscreens::SetEditorMetrics(look.m);
            {
                panelfix::PlayerData imD, okD; ImGuiRig im; OkayRig ok; float okH = 0.0f, imH = 0.0f;
                for (int i = 0; i < 3; ++i) {
                    imH = imHeight(im, [&] { oked::DrawPlayerSettingsBodyImGui(imD.model, hooks); });
                    ok.Frame([&] { okH = okayuiscreens::DrawPlayerSettings(okD.model, bx, by, bw, bh).contentHeight; });
                }
                same("player", imH, okH);
            }
            {
                panelfix::RenderingData imD, okD; ImGuiRig im; OkayRig ok; float okH = 0.0f, imH = 0.0f;
                for (int i = 0; i < 3; ++i) {
                    imH = imHeight(im, [&] { oked::DrawRenderingSettingsBodyImGui(imD.model, hooks); });
                    ok.Frame([&] { okH = okayuiscreens::DrawRenderingSettings(okD.model, bx, by, bw, bh).contentHeight; });
                }
                same("rendering", imH, okH);
            }
            {
                panelfix::PhysicsData imD, okD; ImGuiRig im; OkayRig ok; float okH = 0.0f, imH = 0.0f;
                for (int i = 0; i < 3; ++i) {
                    imH = imHeight(im, [&] { oked::DrawPhysicsSettingsBodyImGui(imD.model, hooks); });
                    ok.Frame([&] { okH = okayuiscreens::DrawPhysicsSettings(okD.model, bx, by, bw, bh).contentHeight; });
                }
                same("physics", imH, okH);
            }
            {
                const P::TagsLayersModel tm = panelfix::TagsLayers(); P::TagsLayersState imSt, okSt; ImGuiRig im; OkayRig ok;
                float okH = 0.0f, imH = 0.0f;
                for (int i = 0; i < 3; ++i) {
                    imH = imHeight(im, [&] { oked::DrawTagsLayersBodyImGui(tm, imSt, hooks); });
                    ok.Frame([&] { okH = okayuiscreens::DrawTagsLayers(tm, okSt, bx, by, bw, bh).contentHeight; });
                }
                same("tags and layers", imH, okH);
            }
            for (int kind : {P::ImportModel, P::ImportTexture, P::ImportAudio}) {
                panelfix::ImportData imD(kind), okD(kind); ImGuiRig im; OkayRig ok; Raster ras; float okH = 0.0f, imH = 0.0f;
                for (int i = 0; i < 3; ++i) {
                    imH = imHeight(im, [&] { oked::DrawImportSettingsBodyImGui(imD.model, imD.state, hooks); });
                    ok.Frame([&] { okH = okayuiscreens::DrawImportSettings(okD.model, okD.state, bx, by, bw, bh).contentHeight; });
                }
                same(kind == P::ImportModel ? "import model" : kind == P::ImportTexture ? "import texture" : "import sound", imH, okH);
                if (custom && kind == P::ImportAudio) {   // the Inspector's sound panel as a picture too (the one that was furthest off)
                    ras.DrawImGui(); const Bitmap imPic = ras.bmp;
                    ras.DrawOkay();  const Bitmap okPic = ras.bmp;
                    Report("import-audio-inspector", imPic, okPic, Timing{});
                }
            }
            {
                panelfix::LoadingData imD, okD; ImGuiRig im; OkayRig ok; float okH = 0.0f, imH = 0.0f;
                for (int i = 0; i < 3; ++i) {
                    imH = imHeight(im, [&] { oked::DrawLoadingScreenBodyImGui(imD.model, hooks); });
                    ok.Frame([&] { okH = okayuiscreens::DrawLoadingScreen(okD.model, bx, by, bw, bh).contentHeight; });
                }
                same("loading screen", imH, okH);
            }
            {
                panelfix::PostData imD, okD; ImGuiRig im; OkayRig ok; float okH = 0.0f, imH = 0.0f;
                for (int i = 0; i < 3; ++i) {
                    imH = imHeight(im, [&] { oked::DrawPostProcessBodyImGui(imD.model, hooks); });
                    ok.Frame([&] { okH = okayuiscreens::DrawPostProcess(okD.model, bx, by, bw, bh).contentHeight; });
                }
                same("post-processing", imH, okH);
            }
        }
        okayuiscreens::SetEditorMetrics(okayuiscreens::EditorMetrics{});
        // hostile metrics fall back to the defaults
        okayuiscreens::EditorMetrics bad;
        bad.itemSpacing = UI::Vec2{std::nanf(""), -3.0f}; bad.framePadding = UI::Vec2{1e30f, 8.0f}; bad.itemWidth = -5.0f;
        okayuiscreens::SetEditorMetrics(bad);
        const okayuiscreens::EditorMetrics& got = okayuiscreens::GetEditorMetrics();
        CHECK(got.itemSpacing.x == 12.0f && got.itemSpacing.y == 8.0f && got.framePadding.x == 12.0f && got.framePadding.y == 8.0f &&
              got.itemWidth == 0.0f);
        okayuiscreens::SetEditorMetrics(okayuiscreens::EditorMetrics{});
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
