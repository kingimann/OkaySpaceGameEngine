// Headless-enough capture of the Visual Script window: load the example graphs,
// play them, and save a screenshot of each with the live run highlight.
//   OkayEngine --vs-shot <outdir> [graph-directory]
#include "VisualScriptEditor.hpp"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

#include <SDL.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int RunVsGraphShot(int argc, char** argv) {
    std::string outDir = "vs-shots";
    std::string graphDir = "examples/visual_script";
    bool saw = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) != "--vs-shot") continue;
        saw = true;
        if (i + 1 < argc) outDir = argv[++i];
        if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) graphDir = argv[++i];
    }
    if (!saw) return 2;

    const char* names[] = {"door.okayvs", "patrol.okayvs", "inventory.okayvs", "foreach_break.okayvs"};
    okay::editor::EditorState ed;
    ed.setProjectDir(fs::absolute(graphDir).parent_path().string());
    struct Item { std::string path; std::string stem; okay::GameObject* go; };
    std::vector<Item> items;
    for (const char* name : names) {
        fs::path p = fs::absolute(fs::path(graphDir) / name);
        if (!fs::exists(p)) { std::cerr << "missing " << p << "\n"; return 1; }
        auto* go = ed.scene().CreateGameObject(p.stem().string());
        auto* vc = go->AddComponent<okay::VisualScriptComponent>();
        std::string err;
        if (!vc->LoadFromFile(p.string(), &err)) { std::cerr << err << "\n"; return 1; }
        items.push_back({p.string(), p.stem().string(), go});
    }
    ed.Play();
    for (int i = 0; i < 45; ++i) ed.Tick(1.0f / 60.0f);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    SDL_Window* window = SDL_CreateWindow("OkayVS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          1680, 1000, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!window || !renderer) { std::cerr << "window: " << SDL_GetError() << "\n"; return 1; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    fs::create_directories(outDir);
    for (const Item& it : items) {
        okay::editor::vsed::OpenFile(it.path);
        okay::editor::vsed::State().frameAll = true;
        ed.Select(it.go);

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) ImGui_ImplSDL2_ProcessEvent(&ev);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        bool open = true;
        okay::editor::vsed::Draw(ed, &open);
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 24, 26, 32, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);

        int w = 0, h = 0;
        SDL_GetRendererOutputSize(renderer, &w, &h);
        SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (surf && SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch) == 0) {
            const std::string bmp = (fs::path(outDir) / (it.stem + ".bmp")).string();
            if (SDL_SaveBMP(surf, bmp.c_str()) == 0) std::cout << "wrote " << bmp << "\n";
            else std::cerr << "save " << bmp << ": " << SDL_GetError() << "\n";
        }
        if (surf) SDL_FreeSurface(surf);
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
