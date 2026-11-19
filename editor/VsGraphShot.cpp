// Headless-enough capture of the Visual Script window: load the example graphs,
// play them, and save a screenshot of each with the live run highlight.
//   OkayEngine --vs-shot <outdir> [graph-directory]
#include "VisualScriptEditor.hpp"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

#include <SDL.h>
#include "okay/Platform/Platform.hpp"
#include "PainterBridge.hpp"
#include <cstdio>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

// A 32-bit BMP (BGRA, bottom-up) from RGBA bytes, top row first: what SDL_SaveBMP wrote for these pictures, without SDL.
static bool WriteBmp(const std::string& path, const std::vector<std::uint8_t>& rgba, int w, int h) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const std::uint32_t imageSize = (std::uint32_t)w * (std::uint32_t)h * 4u, fileSize = 54u + imageSize;
    unsigned char hdr[54] = {'B', 'M'};
    auto put32 = [&](int at, std::uint32_t v) { for (int i = 0; i < 4; ++i) hdr[at + i] = (unsigned char)(v >> (8 * i)); };
    put32(2, fileSize); put32(10, 54); put32(14, 40); put32(18, (std::uint32_t)w); put32(22, (std::uint32_t)h);
    hdr[26] = 1; hdr[28] = 32; put32(34, imageSize);
    bool ok = std::fwrite(hdr, 1, 54, f) == 54;
    std::vector<unsigned char> row((size_t)w * 4);
    for (int y = h - 1; y >= 0 && ok; --y) {
        const std::uint8_t* src = &rgba[(size_t)y * (size_t)w * 4];
        for (int x = 0; x < w; ++x) { row[(size_t)x * 4] = src[x * 4 + 2]; row[(size_t)x * 4 + 1] = src[x * 4 + 1]; row[(size_t)x * 4 + 2] = src[x * 4]; row[(size_t)x * 4 + 3] = src[x * 4 + 3]; }
        ok = std::fwrite(row.data(), 1, row.size(), f) == row.size();
    }
    return std::fclose(f) == 0 && ok;
}

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

    okay::platform::Backend* pbe = okay::platform::Get(okay::platform::BackendKind::Sdl2);   // SDL's ImGui backends below need an SDL window
    if (!pbe) { std::cerr << "SDL2 platform backend is not available\n"; return 1; }
    { okay::platform::InitFlags f; f.audio = false; f.gamepad = false;
      if (!pbe->Init(f)) { std::cerr << "platform init: " << pbe->LastError() << "\n"; return 1; } }
    pbe->ReportUnmappedEvents(true);   // imgui_impl_sdl2 wants every event
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    okay::platform::WindowDesc wd; wd.title = "OkayVS"; wd.width = 1680; wd.height = 1000;
    std::unique_ptr<okay::platform::Window> winSeam = pbe->MakeWindow(wd);
    SDL_Window* window = winSeam ? (SDL_Window*)winSeam->NativeHandle() : nullptr;
    SDL_Renderer* renderer = winSeam ? okay::pb::CreateRenderer(*winSeam) : nullptr;   // software (the hint); its painter owns it
    if (!window || !renderer) { std::cerr << "window: " << pbe->LastError() << "\n"; return 1; }

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

        {   // drain the window's events through the platform layer (Dear ImGui's SDL backend takes the SDL record it keeps)
            okay::platform::Event pe;
            while (pbe->PollEvent(pe)) if (pe.native) ImGui_ImplSDL2_ProcessEvent((const SDL_Event*)pe.native);
        }
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        bool open = true;
        okay::editor::vsed::Draw(ed, &open);
        ImGui::Render();
        okay::pb::SetDrawColor(renderer, 24, 26, 32, 255);
        okay::pb::Clear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        okay::pb::Present(renderer);

        std::vector<std::uint8_t> px; int w = 0, h = 0;
        if (okay::pb::ReadPixels(renderer, px, w, h) && w > 0 && h > 0) {
            const std::string bmp = (fs::path(outDir) / (it.stem + ".bmp")).string();
            if (WriteBmp(bmp, px, w, h)) std::cout << "wrote " << bmp << "\n";
            else std::cerr << "save " << bmp << ": cannot write the file\n";
        }
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    okay::pb::DestroyRenderer(renderer);   // its painter owns it
    winSeam.reset();
    pbe->Shutdown();
    return 0;
}
