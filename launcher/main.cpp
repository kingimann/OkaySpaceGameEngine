// OkaySpace Launcher — a small FiveM-style front end with three sections:
//   Create      launch the engine/editor to build a game
//   Play        run a game you've built (any game.okayscene found nearby)
//   Marketplace browse starter templates to open in the editor
//
// It looks for OkaySpaceEngine.exe (editor) and OkaySpacePlayer.exe (runtime)
// sitting next to it, and launches them. Built with Dear ImGui + SDL2.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string g_exeDir = ".";

// Find the first of `names` that exists next to the launcher.
std::string FindExe(std::initializer_list<const char*> names) {
    std::error_code ec;
    for (const char* n : names) {
        fs::path p = fs::path(g_exeDir) / n;
        if (fs::exists(p, ec)) return p.string();
    }
    return {};
}

// Launch an executable (detached), optionally with one argument.
void Launch(const std::string& exe, const std::string& arg = "") {
    if (exe.empty()) return;
#if defined(_WIN32)
    std::string cmd = "start \"\" \"" + exe + "\"";
    if (!arg.empty()) cmd += " \"" + arg + "\"";
#else
    std::string cmd = "\"" + exe + "\"";
    if (!arg.empty()) cmd += " \"" + arg + "\"";
    cmd += " >/dev/null 2>&1 &";
#endif
    std::system(cmd.c_str());
}

// Scan a few likely folders for built games (*.okayscene).
std::vector<fs::path> FindScenes() {
    std::vector<fs::path> out;
    std::error_code ec;
    auto scan = [&](const fs::path& dir) {
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return;
        for (auto it = fs::recursive_directory_iterator(dir, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it->is_regular_file(ec) && it->path().extension() == ".okayscene")
                out.push_back(it->path());
        }
    };
    fs::path base(g_exeDir);
    scan(base);
    scan(base / "games");
    scan(base / "Projects");
    return out;
}

void DarkTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f; s.FrameRounding = 5.0f; s.GrabRounding = 5.0f;
    s.FramePadding = ImVec2(10, 7); s.ItemSpacing = ImVec2(10, 10);
    ImVec4* c = s.Colors;
    ImVec4 accent(0.20f, 0.45f, 0.85f, 1.0f);
    c[ImGuiCol_WindowBg]     = ImVec4(0.09f, 0.10f, 0.13f, 1.0f);
    c[ImGuiCol_ChildBg]      = ImVec4(0.12f, 0.13f, 0.17f, 1.0f);
    c[ImGuiCol_Button]       = ImVec4(0.18f, 0.20f, 0.26f, 1.0f);
    c[ImGuiCol_ButtonHovered]= accent;
    c[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.38f, 0.72f, 1.0f);
    c[ImGuiCol_Header]       = accent;
    c[ImGuiCol_HeaderHovered]= accent;
}

} // namespace

int main(int argc, char** argv) {
    SDL_SetMainReady();
    if (argc > 0) {
        std::error_code ec;
        fs::path self = fs::absolute(argv[0], ec);
        if (!ec) g_exeDir = self.parent_path().string();
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    SDL_Window* window = SDL_CreateWindow("OkaySpace Launcher",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 600,
        SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) return 1;
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // the launcher has a fixed layout
    DarkTheme();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    const std::string editor = FindExe({"OkaySpaceEngine.exe", "okay-editor.exe", "okay-editor"});
    const std::string player = FindExe({"OkaySpacePlayer.exe", "okay-player.exe", "okay-player"});
    std::vector<fs::path> scenes = FindScenes();

    struct Template { const char* name; const char* desc; };
    const Template templates[] = {
        {"Platformer",     "Side-scrolling jump-and-run starter."},
        {"Top-Down",       "Top-down movement and rooms."},
        {"Coin Collector", "A complete pickup-the-coins game."},
        {"Main Menu (UI)", "A title screen with buttons."},
        {"Snake",          "The classic, fully playable."},
    };

    int tab = 0; // 0 Create, 1 Play, 2 Marketplace
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) running = false;
        }
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("##launcher", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        // ---- Left nav ----
        ImGui::BeginChild("nav", ImVec2(190, 0), true);
        ImGui::PushFont(nullptr);
        ImGui::TextColored(ImVec4(0.45f, 0.7f, 1.0f, 1.0f), "OkaySpace");
        ImGui::TextDisabled("game engine");
        ImGui::Dummy(ImVec2(0, 14));
        const char* navs[] = {"  Create", "  Play", "  Marketplace"};
        for (int i = 0; i < 3; ++i)
            if (ImGui::Selectable(navs[i], tab == i, 0, ImVec2(0, 34))) tab = i;
        ImGui::PopFont();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("content", ImVec2(0, 0), true);

        if (tab == 0) {                                   // ---- Create ----
            ImGui::TextColored(ImVec4(0.85f, 0.9f, 1.0f, 1.0f), "Create a game");
            ImGui::TextWrapped("Open the OkaySpace editor to build 2D or 3D scenes, "
                               "script them, and design UI. Use File > Build Game to "
                               "export a standalone game.");
            ImGui::Dummy(ImVec2(0, 16));
            if (editor.empty()) {
                ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "Editor not found next to the launcher.");
                ImGui::TextDisabled("Place OkaySpaceEngine.exe beside this launcher.");
            } else {
                if (ImGui::Button("Open Editor", ImVec2(220, 56))) Launch(editor);
                ImGui::TextDisabled("%s", editor.c_str());
            }
        } else if (tab == 1) {                            // ---- Play ----
            ImGui::TextColored(ImVec4(0.85f, 0.9f, 1.0f, 1.0f), "Play a game");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
            if (ImGui::Button("Refresh", ImVec2(80, 0))) scenes = FindScenes();
            ImGui::Separator();
            if (player.empty())
                ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "Player runtime not found next to the launcher.");
            else if (scenes.empty())
                ImGui::TextDisabled("No games found. Build one from the editor (File > Build Game), "
                                    "then put it next to the launcher or in a 'games' folder.");
            else
                for (std::size_t i = 0; i < scenes.size(); ++i) {
                    ImGui::PushID((int)i);
                    if (ImGui::Button("Play", ImVec2(70, 0))) Launch(player, scenes[i].string());
                    ImGui::SameLine();
                    ImGui::TextUnformatted(scenes[i].filename().string().c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", scenes[i].parent_path().string().c_str());
                    ImGui::PopID();
                }
        } else {                                          // ---- Marketplace ----
            ImGui::TextColored(ImVec4(0.85f, 0.9f, 1.0f, 1.0f), "Marketplace");
            ImGui::TextDisabled("Starter templates — open one in the editor (New Project) to begin.");
            ImGui::Separator();
            for (const auto& t : templates) {
                ImGui::BeginChild(t.name, ImVec2(0, 64), true);
                ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.95f, 1.0f), "%s", t.name);
                ImGui::TextDisabled("%s", t.desc);
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90);
                ImGui::PushID(t.name);
                if (ImGui::Button("Open", ImVec2(86, 0))) Launch(editor);
                ImGui::PopID();
                ImGui::EndChild();
            }
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::TextDisabled("Community content marketplace coming soon.");
        }

        ImGui::EndChild();
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 18, 20, 26, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
