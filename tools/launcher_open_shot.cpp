// Draws the launcher's version question with its REAL drawing code (launcher/OpenProjectUI.cpp) and the launcher's own look
// (shared/OkayTheme.hpp "Slate", the embedded Roboto Medium at 17 px) into a picture, with no window and no GPU: Dear ImGui on SDL's
// software renderer over an offscreen surface. It is how the screenshots of docs/project_upgrade.md and docs/try_it.md (section 7m)
// were made. The words and buttons are the real launcher/OpenProjectGate.hpp output for the sample project below.
//
//   okay_launcher_open_shot <case> <out.bmp> [width height]
//   case: older | older-missing | unversioned | newer | newer-open-with
//
// What it draws and what it does not: the question window over a plain launcher-coloured background. The rest of the launcher (the
// navigation, the project cards) is not drawn here: the window is what the launcher shows, the rest of the picture is empty on purpose.
// Run it with SDL_VIDEODRIVER=dummy on a machine without a display. Written for this engine; no outside code.
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "imgui.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include "OkayTheme.hpp"
#include "OpenProjectGate.hpp"
#include "OpenProjectUI.hpp"
#include "RobotoFont.h"   // embedded Roboto Medium (Apache 2.0), the font the launcher uses

namespace {

// The launcher's default look (DarkTheme() in launcher/main.cpp, the Slate branch): its metrics, then the shared Slate colours.
void LauncherSlateStyle(const ImVec4& accent) {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f;  s.ChildRounding = 2.0f;
    s.FrameRounding = 2.0f;   s.GrabRounding = 2.0f;
    s.PopupRounding = 2.0f;   s.TabRounding = 2.0f;
    s.ScrollbarRounding = 2.0f;
    s.WindowPadding = ImVec2(14, 12); s.FramePadding = ImVec2(10, 7);
    s.ItemSpacing = ImVec2(9, 8);     s.ItemInnerSpacing = ImVec2(7, 6);
    s.WindowBorderSize = 0.0f;        s.ChildBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;         s.PopupBorderSize = 1.0f;
    s.ScrollbarSize = 8.0f;           s.GrabMinSize = 11.0f;
    s.DisabledAlpha = 0.55f;
    okaytheme::ApplySlateMetrics(s);
    s.WindowRounding = 0.0f;
    okaytheme::ApplySlateColors(s, accent);
}

// The sample project of the pictures: made with a v2.850 release, opened with this launcher's v2.875.0 editor.
okayopen::Input Sample(const std::string& which) {
    okayopen::Input in;
    in.projectName = "MyGame";
    in.projectRoot = "C:\\Users\\you\\AppData\\Local\\OkaySpace\\Projects\\MyGame";
    in.scene = in.projectRoot + "\\Assets\\MyGame.okayscene";
    in.projectVersion = "2.850.0";
    in.editorVersion = "2.875.0";
    in.launcherPid = 4242;
    in.installed = {"2.850.1"};
    if (which == "older-missing") in.installed.clear();
    else if (which == "unversioned") in.projectVersion.clear();
    else if (which == "newer") in.projectVersion = "2.880.0";
    else if (which == "newer-open-with") { in.projectVersion = "2.875.0"; in.chosenVersion = "2.860.0"; in.installed = {"2.860.0", "2.850.1"}; }
    return in;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s <older|older-missing|unversioned|newer|newer-open-with> <out.bmp> [width height]\n", argv[0]); return 2; }
    const std::string which = argv[1];
    const char* out = argv[2];
    const int width = argc > 4 ? std::atoi(argv[3]) : 900, height = argc > 4 ? std::atoi(argv[4]) : 470;
    const okayopen::Prompt prompt = okayopen::Ask(Sample(which));
    if (prompt.question == okayopen::Question::None) { std::fprintf(stderr, "that case asks nothing: %s\n", which.c_str()); return 2; }

    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    if (!renderer) { std::fprintf(stderr, "no software renderer: %s\n", SDL_GetError()); return 1; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImFontConfig fc;
    fc.OversampleH = 2; fc.OversampleV = 2; fc.PixelSnapH = true;
    io.Fonts->AddFontFromMemoryCompressedBase85TTF(RobotoMedium_compressed_data_base85, 17.0f, &fc);   // as the launcher loads it
    okayopenui::Look look;   // the launcher's default accent (kAccent, kAccentDim)
    LauncherSlateStyle(look.accent);
    ImGui_ImplSDLRenderer2_Init(renderer);

    // A few frames: the popup opens on the first, and its size settles on the next.
    for (int frame = 0; frame < 8; ++frame) {
        io.DisplaySize = ImVec2((float)width, (float)height);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)width, (float)height));
        ImGui::Begin("##launcher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::End();
        if (!ImGui::IsPopupOpen(okayopenui::PopupName())) ImGui::OpenPopup(okayopenui::PopupName());
        (void)okayopenui::Draw(prompt, look);
        ImGui::Render();
        const ImVec4 bg = okaytheme::kBase;
        SDL_SetRenderDrawColor(renderer, (Uint8)(bg.x * 255), (Uint8)(bg.y * 255), (Uint8)(bg.z * 255), 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
    }
    const int rc = SDL_SaveBMP(surface, out);
    if (rc != 0) std::fprintf(stderr, "couldn't save %s: %s\n", out, SDL_GetError());
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    SDL_Quit();
    return rc == 0 ? 0 : 1;
}
