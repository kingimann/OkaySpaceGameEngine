// Headless pictures of the module pages (docs/images/modular/): the New Project form's "Modules" part and
// Edit > Project Settings > Modules, drawn with the REAL drawing code (editor/ModulesPanelUI.hpp) and the REAL module
// list (the engine's ModuleRegistry, built from engine/modules/*/module.okaymodule). Dear ImGui draws into a software
// SDL surface: no window, no editor, no GPU. The look is ImGui's default dark style and built-in font, NOT the editor's
// own skin; the rows, the order, what is locked and the texts are the real ones. Not part of the CMake build; build and
// run it by hand (<imgui> is the folder CMake fetched Dear ImGui into, for example build/_deps/imgui-src; <build> a
// configured build tree; only its engine/modules_enabled.cpp is used, so no engine build is needed):
//
//   g++ -std=c++17 -O1 -Ieditor -Iengine/include -Ishared -isystem /usr/include/SDL2 -isystem <imgui> -isystem <imgui>/backends \
//       tools/modules_shots.cpp <imgui>/imgui.cpp <imgui>/imgui_draw.cpp <imgui>/imgui_tables.cpp <imgui>/imgui_widgets.cpp \
//       <imgui>/backends/imgui_impl_sdlrenderer2.cpp engine/src/Modules/ModuleManifest.cpp engine/src/Modules/ModuleRegistry.cpp <build>/engine/modules_enabled.cpp \
//       -lSDL2 -o modules_shots
//   mkdir -p out && ./modules_shots out                       # writes out/*.bmp
//   for b in out/*.bmp; do convert "$b" -fuzz 1% -trim +repage -bordercolor "#1a1c20" -border 14 "${b%.bmp}.png"; done
//
// Written for this repository (Dear ImGui's own public API only).
#include <SDL.h>
#include <imgui.h>
#include "imgui_impl_sdlrenderer2.h"
#include "ModulesPanelUI.hpp"
#include "OkayTheme.hpp"   // the editor's own Slate skin (shared/OkayTheme.hpp)

#include <cfloat>
#include <cstdio>
#include <functional>
#include <string>

static SDL_Surface* g_surface = nullptr;
static SDL_Renderer* g_renderer = nullptr;
static const int W = 960, H = 820;
static std::string g_outDir;

static void Capture(const std::string& file, const std::function<void()>& body) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    for (int f = 0; f < 6; ++f) {   // a few frames so layout settles
        ImGui_ImplSDLRenderer2_NewFrame();
        io.DisplaySize = ImVec2(static_cast<float>(W), static_cast<float>(H));
        io.DeltaTime = 0.1f;
        ImGui::NewFrame();
        body();
        ImGui::Render();
        SDL_SetRenderDrawColor(g_renderer, 26, 28, 32, 255);
        SDL_RenderClear(g_renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), g_renderer);
        SDL_RenderPresent(g_renderer);
    }
    const std::string bmp = g_outDir + "/" + file + ".bmp";
    SDL_SaveBMP(g_surface, bmp.c_str());
    std::printf("wrote %s\n", bmp.c_str());
}

static void Window(const char* title, const std::function<void()>& inside) {
    ImGui::SetNextWindowPos(ImVec2(8, 8));
    ImGui::SetNextWindowSize(ImVec2(W - 16, 0));
    ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
    inside();
    ImGui::End();
}

int main(int argc, char** argv) {
    g_outDir = argc > 1 ? argv[1] : ".";
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    g_surface = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    g_renderer = SDL_CreateSoftwareRenderer(g_surface);
    if (!g_renderer) { std::fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImFontConfig fc; fc.SizePixels = 16.0f;
    io.Fonts->AddFontDefault(&fc);
    ImGui::StyleColorsDark();
    {   // the editor's own look: ApplyTheme() in editor/main.cpp (Slate metrics and colours, default "Classic" accent)
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowPadding = ImVec2(16, 12); s.FramePadding = ImVec2(12, 8); s.ItemSpacing = ImVec2(12, 8);
        s.ItemInnerSpacing = ImVec2(8, 6); s.CellPadding = ImVec2(10, 6); s.IndentSpacing = 22.0f;
        s.ScrollbarSize = 14.0f; s.GrabMinSize = 16.0f; s.WindowBorderSize = 0.0f; s.FrameBorderSize = 0.0f; s.PopupBorderSize = 1.0f;
        okaytheme::ApplySlateMetrics(s);
        okaytheme::ApplySlateColors(s, ImVec4(0.235f, 0.475f, 0.800f, 1.0f));
        s.Colors[ImGuiCol_TextDisabled] = ImVec4(0.70f, 0.74f, 0.82f, 1.0f);
    }
    ImGui_ImplSDLRenderer2_Init(g_renderer);

    auto& reg = okay::ModuleRegistry::Get();
    const size_t total = okaymod::Selection(reg).Rows().size();

    // 1. New Project, modules part, the "3D action" preset, list collapsed
    {
        okaymod::Selection sel(reg);
        sel.ApplyPreset("3d_action");
        std::string note;
        Capture("new-project-modules-preset", [&] {
            Window("New Project", [&] {
                ImGui::TextDisabled("MODULES  (what goes into your game; a module that is off is left out of it)");
                okaymod::DrawPresetButtons(sel);
                ImGui::TextDisabled("%s", okaymod::SummaryText(sel, total).c_str());
                ImGui::TextDisabled("> Choose modules one by one");
            });
        });
    }
    // 2. New Project, list open, "2D game" preset
    {
        okaymod::Selection sel(reg);
        sel.ApplyPreset("2d");
        std::string note;
        Capture("new-project-modules-list-2d", [&] {
            Window("New Project", [&] {
                ImGui::TextDisabled("MODULES  (what goes into your game; a module that is off is left out of it)");
                okaymod::DrawPresetButtons(sel);
                ImGui::TextDisabled("%s", okaymod::SummaryText(sel, total).c_str());
                okaymod::DrawChecklist(sel, nullptr, note, 480.0f);
            });
        });
    }
    // 3. Project Settings > Modules with scene usage, health needed by weapons (locked), a note after ticking
    {
        okaymod::Selection sel(reg);
        sel.ApplyPreset("3d_action");
        sel.Set("health_damage", true);
        std::string note = "Weapons also needs: Health and damage, 3D physics (turned on too).";
        std::map<std::string, int> usage = {{"health_damage", 4}, {"weapons", 2}, {"interaction", 7}, {"audio", 3}, {"networking", 1}};
        Capture("project-settings-modules", [&] {
            Window("Project Settings", [&] {
                ImGui::TextWrapped("The parts of the engine this game uses. A module that is off is left out of the exported game. "
                                   "Turning one on also turns on what it needs; a module that others need stays on.");
                okaymod::DrawPresetButtons(sel);
                ImGui::TextDisabled("%s", okaymod::SummaryText(sel, total).c_str());
                okaymod::DrawChecklist(sel, &usage, note, 440.0f);
                okaymod::WrappedColored(ImVec4(0.95f, 0.80f, 0.35f, 1.0f), note);
                okaymod::WrappedColored(ImVec4(0.96f, 0.55f, 0.50f, 1.0f),
                                   "The open scene uses 1 object(s) of a module that is off: Networking. They stay in the scene and are left out of the exported game.");
            });
        });
    }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui::DestroyContext();
    SDL_Quit();
    return 0;
}
