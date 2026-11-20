// Test-only picture helper (NOT part of the product, not in CMake): draws the beginner Modeling view with the same widgets and
// sizes the editor uses, into PNG-less BMP files, headless (SDL dummy video, software renderer). Build and run:
//   g++ -std=c++17 -O1 -Ieditor -Iengine/include -I<imgui> -I<imgui>/backends -I/usr/include/SDL2 tests/modeling_easy_shot.cpp \
//       editor/ModelingEasyUi.cpp <imgui>/imgui*.cpp <imgui>/backends/imgui_impl_sdl2.cpp <imgui>/backends/imgui_impl_sdlrenderer2.cpp -lSDL2 -o shot
//   SDL_VIDEODRIVER=dummy ./shot <outdir> <font.ttf>
#include "ModelingEasyUi.hpp"
#include "ModelingStarters.hpp"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include <SDL.h>
#include "okay/Platform/BmpWriter.hpp"
#include <cstdio>
#include <string>

static void Save(SDL_Renderer* r, int w, int h, const std::string& path) {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    okay::platform::WriteBmp32Bgra(path.c_str(), s->pixels, w, h, s->pitch); SDL_FreeSurface(s);
}

int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : ".";
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* win = SDL_CreateWindow("shot", 0, 0, 1280, 720, SDL_WINDOW_HIDDEN);
    SDL_Renderer* rd = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); io.IniFilename = nullptr;
    if (argc > 2) io.Fonts->AddFontFromFileTTF(argv[2], 16.0f);
    ImGui_ImplSDL2_InitForSDLRenderer(win, rd); ImGui_ImplSDLRenderer2_Init(rd);

    struct Scene { const char* name; bool object, edit; int mode, count, tab; std::vector<std::string> recent; };
    const Scene scenes[] = {
        {"01_nothing_picked_shape_tab", false, false, 2, 0, 0, {}},
        {"02_model_faces_picked_details", true, true, 2, 2, 2, {}},
        {"03_model_whole_reshape", true, false, 2, 0, 1, {}},
    };
    oked::easymodel::EditState ex;
    for (const Scene& sc : scenes) {
        oked::easymodel_ui::Host h;
        okay::Mesh mesh = oked::easymodel::BuildStarter("house");
        h.mesh = sc.object ? &mesh : nullptr;
        h.state.haveObject = sc.object; h.state.editMode = sc.edit; h.state.selMode = sc.mode; h.state.selCount = sc.count;
        h.canUndo = [] { return true; }; h.canRedo = [] { return false; };
        h.openTab = sc.tab;
        for (int f = 0; f < 4; ++f) {   // a few frames so tabs and layout settle
            ImGui_ImplSDLRenderer2_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(1280, 720));
            ImGui::Begin("Modeling", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
            oked::easymodel_ui::DrawEasyModeling(h);
            ImGui::End();
            ImGui::Render();
            SDL_SetRenderDrawColor(rd, 30, 30, 34, 255); SDL_RenderClear(rd);
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), rd);
            if (f == 3) Save(rd, 1280, 720, out + "/" + sc.name + ".bmp");
            SDL_RenderPresent(rd);
        }
    }
    return 0;
}
