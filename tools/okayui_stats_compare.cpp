// A headless side-by-side of the editor's Stats window: the Dear ImGui version on the left, the OkayUI port (okayui/screens/OkayUIPanels.cpp,
// DrawStats) on the right, both drawn into software pictures with no window and no GPU, with the same fixed numbers. Written for this
// repository (docs/own_ui_toolkit.md section 4.40); the drawing is Dear ImGui's and OkayUI's own public API.
//
// HONEST LIMITS: the left picture is the SAME ImGui CALLS the editor's DrawStats makes (copied here with fixed data instead of a scene), drawn
// in ImGui's default dark style and its built-in font, NOT the editor's own skin and font, and not the real editor window. The right picture
// is the real OkayUI screen code through OkayUI's software renderer (the headless test backend), in OkayUI's launcher look (Roboto), which is
// what the editor starts OkayUI in unless OKAY_CLASSIC_UI is set. So fonts and colours differ from the left; the content, the order and the
// controls are what is being compared.
//
// Not part of the CMake build; build and run it by hand (<b> is a build folder that already built okay_ui_screens and okay_ui_soft,
// <imgui> is the folder CMake fetched Dear ImGui into, for example <b>/_deps/imgui-src):
//
//   g++ -std=c++17 -O1 -Iokayui/include -Iokayui/screens -Iokayui/soft -Iengine/include -I<imgui> -I<imgui>/backends $(pkg-config --cflags sdl2) \
//       tools/okayui_stats_compare.cpp <imgui>/imgui.cpp <imgui>/imgui_draw.cpp <imgui>/imgui_tables.cpp <imgui>/imgui_widgets.cpp \
//       <imgui>/backends/imgui_impl_sdlrenderer2.cpp <b>/okayui/libokay_ui_soft.a <b>/okayui/libokay_ui_screens.a <b>/okayui/libokay_ui.a \
//       <b>/engine/libokay_engine.a $(pkg-config --libs sdl2) -lpthread -ldl -o okayui_stats_compare
//   ./okayui_stats_compare out/          # writes out/stats_imgui.png, out/stats_okayui.png, out/stats_side_by_side.png
#include <SDL.h>
#include <imgui.h>
#include "imgui_impl_sdlrenderer2.h"
#include "okay/Core/ImageWrite.hpp"
#include "okay/UI/OkayUI.hpp"
#include "OkayUI_Soft.hpp"
#include "OkayUIPanels.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace S = okayuiscreens;
static const int W = 560, H = 480;

struct Data {
    float hist[120] = {0};
    int used = 100;
    float fps = 58.0f;
    bool frustum = true, occlusion = false, stat = true, dyn = false;
    std::vector<std::pair<std::string, int>> rows;
    Data() {
        for (int i = 0; i < used; ++i) hist[i] = 40.0f + 30.0f * (0.5f + 0.5f * std::sin(i * 0.2f));
        const char* names[] = {"Objects", "Triangles", "Meshes", "Sprites", "Lights", "Colliders", "Scripts", "NPCs", "Particles", "Drawn", "Culled", "Occluded", "LOD culled", "LOD0", "LOD1+", "Batches", "Draws saved"};
        const int vals[] = {42, 1500000, 17, 0, 3, 12000, 5, 0, 250, 30, 12, 0, 0, 30, 0, 6, 24};
        for (int i = 0; i < 17; ++i) rows.push_back({names[i], vals[i]});
    }
};

// The ImGui side: the calls DrawStats makes in editor/main.cpp, with the data above.
static void ImGuiStats(Data& d, bool showTooltip) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)W, (float)H));
    ImGui::Begin("Stats", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    float fmin = 1e9f, fmax = 0.0f, fsum = 0.0f; int fcnt = 0;
    for (float v : d.hist) if (v > 0.0f) { fmin = std::min(fmin, v); fmax = std::max(fmax, v); fsum += v; ++fcnt; }
    float fscale = fmax * 1.2f < 75.0f ? 75.0f : fmax * 1.2f;
    ImVec4 fpsCol = d.fps >= 55.0f ? ImVec4(0.45f, 0.85f, 0.55f, 1.0f) : d.fps >= 30.0f ? ImVec4(0.95f, 0.78f, 0.35f, 1.0f) : ImVec4(0.96f, 0.45f, 0.42f, 1.0f);
    ImGui::SetWindowFontScale(2.0f);
    ImGui::TextColored(fpsCol, "%.0f", d.fps);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("FPS   %.2f ms", 1000.0f / d.fps);
    char mm[48]; std::snprintf(mm, sizeof mm, "avg %.0f  min %.0f", fcnt ? fsum / fcnt : 0.0f, fcnt ? fmin : 0.0f);
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(mm).x);
    ImGui::TextDisabled("%s", mm);
    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.45f, 0.70f, 1.0f, 1.0f));
    ImGui::PlotLines("##fps", d.hist, 120, d.used, nullptr, 0.0f, fscale, ImVec2(-1, 48));
    ImGui::PopStyleColor();
    {
        ImVec2 gmn = ImGui::GetItemRectMin(), gmx = ImGui::GetItemRectMax();
        if (fscale > 60.0f) { const float gy = gmx.y - (60.0f / fscale) * (gmx.y - gmn.y); ImGui::GetWindowDrawList()->AddLine(ImVec2(gmn.x, gy), ImVec2(gmx.x, gy), IM_COL32(110, 220, 130, 110), 1.0f); }
    }
    ImGui::Spacing();
    if (ImGui::BeginTable("##statgrid", 4, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("n1", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("v1", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("n2", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("v2", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        for (int i = 0; i < (int)d.rows.size(); ++i) {
            if (i % 2 == 0) ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", d.rows[(size_t)i].first.c_str());
            ImGui::TableNextColumn();
            const int v = d.rows[(size_t)i].second; char num[24];
            if (v >= 1000000) std::snprintf(num, sizeof num, "%.1fM", v / 1e6); else if (v >= 10000) std::snprintf(num, sizeof num, "%.1fk", v / 1e3); else std::snprintf(num, sizeof num, "%d", v);
            if (v == 0) ImGui::TextDisabled("%s", num); else ImGui::TextUnformatted(num);
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::Checkbox("Frustum culling", &d.frustum);
    ImGui::SameLine();
    ImGui::Checkbox("Occlusion", &d.occlusion);
    if (showTooltip) ImGui::SetTooltip("Skip meshes hidden behind an Occluder. Off, or with no Occluder\nin the scene, nothing extra is hidden.");
    ImGui::Checkbox("Static batching", &d.stat);
    ImGui::SameLine();
    ImGui::Checkbox("Dynamic batching", &d.dyn);
    ImGui::SameLine();
    ImGui::TextDisabled("| skinning: %s", "GPU");
    ImGui::Spacing();
    ImGui::TextWrapped("Sky, lighting, post FX and debug view moved to the Lighting tab.");
    ImGui::End();
}

static std::vector<std::uint8_t> Side(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    std::vector<std::uint8_t> out((size_t)(W * 2 + 12) * H * 4, 20);
    for (int y = 0; y < H; ++y) {
        std::copy(a.begin() + (size_t)y * W * 4, a.begin() + (size_t)(y + 1) * W * 4, out.begin() + (size_t)y * (W * 2 + 12) * 4);
        std::copy(b.begin() + (size_t)y * W * 4, b.begin() + (size_t)(y + 1) * W * 4, out.begin() + ((size_t)y * (W * 2 + 12) + W + 12) * 4);
    }
    for (size_t i = 3; i < out.size(); i += 4) out[i] = 255;
    return out;
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ABGR8888);
    SDL_Renderer* ren = SDL_CreateSoftwareRenderer(surf);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImFontConfig fc; fc.SizePixels = 15.0f;
    io.Fonts->AddFontDefault(&fc);
    ImGui::StyleColorsDark();
    ImGui_ImplSDLRenderer2_Init(ren);
    Data d;
    auto imguiShot = [&](bool tip) {
        io.AddMousePosEvent(tip ? 130.0f : -1000.0f, tip ? 424.0f : -1000.0f);
        for (int f = 0; f < 8; ++f) {
            ImGui_ImplSDLRenderer2_NewFrame();
            io.DisplaySize = ImVec2((float)W, (float)H); io.DeltaTime = 0.1f;
            ImGui::NewFrame();
            ImGuiStats(d, tip && f >= 6);
            ImGui::Render();
            SDL_SetRenderDrawColor(ren, 20, 20, 20, 255); SDL_RenderClear(ren);
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren);
        }
        std::vector<std::uint8_t> px((size_t)W * H * 4);
        SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ABGR8888, px.data(), W * 4);
        for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
        return px;
    };
    const std::vector<std::uint8_t> a = imguiShot(false);

    // the OkayUI side
    OkayUI::ResetState();
    OkayUI::UseLauncherLook(true);   // the editor starts OkayUI in the launcher look unless OKAY_CLASSIC_UI is set (editor/main.cpp)
    OkayUI::Style().caretBlink = false;
    S::StatsModel m;
    m.framerate = d.fps; m.history = d.hist; m.historyCount = 120; m.historyOffset = d.used;
    for (const auto& r : d.rows) m.rows.push_back({r.first, r.second});
    bool fr = true, oc = false, st = true, dy = false;
    m.frustumCulling = &fr; m.occlusionCulling = &oc; m.staticBatching = &st; m.dynamicBatching = &dy; m.skinning = "GPU";
    OkayUI::soft::Bitmap bmp(W, H);
    for (int f = 0; f < 6; ++f) {
        OkayUI::Input in; in.displayW = (float)W; in.displayH = (float)H; in.mouseX = -1000; in.mouseY = -1000;
        OkayUI::BeginFrame(in);
        S::DrawStats(m, 0, 0, (float)W, (float)H);
        OkayUI::EndFrameData();
        bmp.Clear(28, 30, 36);
        OkayUI::soft::Render(OkayUI::GetDrawData(), bmp);
    }
    std::vector<std::uint8_t> b(bmp.rgba.begin(), bmp.rgba.end());
    for (size_t i = 3; i < b.size(); i += 4) b[i] = 255;

    okay::ImageWrite::WritePNG(dir + "/stats_imgui.png", W, H, 4, a.data());
    okay::ImageWrite::WritePNG(dir + "/stats_okayui.png", W, H, 4, b.data());
    const std::vector<std::uint8_t> sbs = Side(a, b);
    okay::ImageWrite::WritePNG(dir + "/stats_side_by_side.png", W * 2 + 12, H, 4, sbs.data());
    std::printf("wrote %s/stats_imgui.png, stats_okayui.png, stats_side_by_side.png\n", dir.c_str());
    return 0;
}
