// Headless pictures of the Project panel's right-click menus and Properties window (docs/images/project_menus/), drawn with the REAL
// drawing code (editor/ProjectMenuDraw.cpp) and the REAL item lists (editor/ProjectContextModel.hpp). Dear ImGui draws into a software
// SDL surface: no window, no editor, no GPU. The look is ImGui's default dark style and its built-in font with the Project panel's roomy
// spacing, NOT the editor's own skin, so colours and fonts differ from the real window; the items, their order, what is greyed and the
// tooltips are the real ones. Not part of the CMake build; build and run it by hand (<imgui> is the folder CMake fetched Dear ImGui into,
// for example build/_deps/imgui-src):
//
//   g++ -std=c++17 -O1 -Ieditor -Iengine/include -Ishared -isystem /usr/include/SDL2 -isystem <imgui> -isystem <imgui>/backends tools/project_menu_shots.cpp editor/ProjectMenuDraw.cpp <imgui>/imgui.cpp <imgui>/imgui_draw.cpp <imgui>/imgui_tables.cpp <imgui>/imgui_widgets.cpp <imgui>/backends/imgui_impl_sdlrenderer2.cpp -lSDL2 -o project_menu_shots
//   mkdir -p out && ./project_menu_shots out                    # writes out/*.bmp
//   for b in out/*.bmp; do convert "$b" -fuzz 1% -trim +repage -bordercolor "#1a1c20" -border 14 "${b%.bmp}.png"; done   # ImageMagick
//
// Written for this repository (the drawing of menus is Dear ImGui's own public API; no other project's code).
#include <SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include "imgui_impl_sdlrenderer2.h"
#include "ProjectPanelUI.hpp"

#include <cfloat>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace okaypb::ctx;

static SDL_Surface* g_surface = nullptr;
static SDL_Renderer* g_renderer = nullptr;
static const int W = 1100, H = 900;
static std::string g_outDir;

static State Base() {
    State s;
    s.fileManager = "Explorer";
    s.labelNames = {"None", "Red", "Orange", "Yellow", "Green", "Blue", "Purple"};
    return s;
}

// One picture: `body` draws the popup (or a window) every frame; `openLabel` is a submenu item to hover until it opens.
static void Capture(const std::string& file, const std::string& caption, const std::function<void(const std::string&, bool&)>& body,
                    const std::string& openLabel = std::string(), float popupX = 40.0f, float popupY = 70.0f, float hoverX = -1.0f, float hoverY = -1.0f) {
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::GetCurrentContext()->OpenPopupStack.Size > 0) ImGui::ClosePopupToLevel(0, false);   // the picture before leaves no menu open
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);                                                          // and the mouse rests nowhere
    bool found = openLabel.empty();
    int hold = 0;
    for (int f = 0; f < 400; ++f) {
        if (!openLabel.empty() && f >= 3 && !found) io.AddMousePosEvent(popupX + 36.0f, popupY + 6.0f + 5.0f * static_cast<float>(f - 3));
        if (hoverX >= 0.0f && f >= 3) io.AddMousePosEvent(hoverX, hoverY);   // rest on one item, for its tooltip
        ImGui_ImplSDLRenderer2_NewFrame();
        io.DisplaySize = ImVec2(static_cast<float>(W), static_cast<float>(H));
        io.DeltaTime = 0.1f;
        ImGui::NewFrame();
        {
            ImGui::SetNextWindowPos(ImVec2(12, 8));
            ImGui::Begin("caption", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::PushTextWrapPos(600.0f);
            ImGui::TextDisabled("%s", caption.c_str());
            ImGui::PopTextWrapPos();
            ImGui::End();
        }
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 10));   // the Project panel's roomier spacing (DrawProject)
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 8));
        bool hovered = false;
        body(openLabel, hovered);
        ImGui::PopStyleVar(2);
        if (!found && hovered) found = true;
        ImGui::Render();
        SDL_SetRenderDrawColor(g_renderer, 26, 28, 32, 255);
        SDL_RenderClear(g_renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), g_renderer);
        SDL_RenderPresent(g_renderer);
        if (f >= 6 && found && ++hold > 4) break;
    }
    const std::string bmp = g_outDir + "/" + file + ".bmp";
    SDL_SaveBMP(g_surface, bmp.c_str());
    std::printf("wrote %s\n", bmp.c_str());
}

// A context menu popup at a fixed place, items from the model.
static std::function<void(const std::string&, bool&)> Popup(const std::vector<Item>& items, float x = 40.0f, float y = 70.0f) {
    return [items, x, y](const std::string& openLabel, bool& hovered) {
        static int frame = 0;
        ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
        if (!ImGui::IsPopupOpen("ctx")) ImGui::OpenPopup("ctx");
        if (ImGui::BeginPopup("ctx", ImGuiWindowFlags_NoMove)) {
            oked::projectpanel::DrawContextMenu(items);
            if (!openLabel.empty()) {
                const ImGuiID want = ImGui::GetID(openLabel.c_str());
                ImGuiContext& g = *ImGui::GetCurrentContext();
                if (g.HoveredIdPreviousFrame == want && g.OpenPopupStack.Size > 1) hovered = true;
            }
            ImGui::EndPopup();
        }
        ++frame;
    };
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
    ImFontConfig fc; fc.SizePixels = 17.0f;
    io.Fonts->AddFontDefault(&fc);
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 6.0f;
    ImGui::GetStyle().PopupRounding = 6.0f;
    ImGui_ImplSDLRenderer2_Init(g_renderer);

    // 1. one picture
    {
        State s = Base(); s.kind = Kind::Image; s.canOpen = false; s.count = 1; s.tags = {"hero"}; s.label = 2;
        Capture("asset-menu-picture", "Right-click a picture (Assets/Textures/Hero.png)", Popup(ItemMenu(s)));
    }
    // 2. three items selected, two things on the clipboard
    {
        State s = Base(); s.kind = Kind::Image; s.canOpen = false; s.count = 3; s.hasClipboard = true; s.clipboardCount = 2; s.canUndo = true;
        Capture("asset-menu-three-selected", "Three pictures selected, two files copied earlier", Popup(ItemMenu(s)));
    }
    // 3. a script: both outside-editor items offered
    {
        State s = Base(); s.kind = Kind::Script; s.canOpen = true; s.count = 1; s.hierarchySelection = true;
        Capture("asset-menu-script", "Right-click a script (Assets/Scripts/Player.cs)", Popup(ItemMenu(s)));
    }
    // 4. a batch file: runs code, so no editor item and the default app is greyed
    {
        State s = Base(); s.kind = Kind::Other; s.canOpen = false; s.count = 1; s.runsCode = true;
        Capture("asset-menu-program-or-script", "Right-click tool.bat: Open with Default App is greyed (it would run it); no external editor item. Hovering it says why",
                Popup(ItemMenu(s)), "", 40.0f, 70.0f, 110.0f, 112.0f);
    }
    // 5. a folder in the grid, Create submenu open
    {
        State s = Base(); s.kind = Kind::Folder; s.canOpen = true; s.count = 1; s.hasSubfolders = true; s.hierarchySelection = true;
        Capture("folder-menu-create", "Right-click a folder in the grid, with Create open", Popup(ItemMenu(s)), "Create");
    }
    // 6. the Assets folder in the tree
    {
        State s = Base(); s.where = Where::Tree; s.kind = Kind::Folder; s.canOpen = true; s.atRoot = true; s.hasSubfolders = true; s.labelNames.clear();
        Capture("tree-assets-menu", "Right-click Assets in the folder tree: what cannot change is greyed", Popup(FolderTreeMenu(s)));
    }
    // 7. empty space with View open
    {
        State s = Base(); s.where = Where::Background; s.kind = Kind::Folder; s.shownCount = 12; s.canUndo = true; s.undoLabel = "Rename Hero.png"; s.view = 0; s.sort = 3; s.tilePreset = 2;
        Capture("empty-space-menu-view", "Right-click empty space, with View open", Popup(BackgroundMenu(s)), "View");
    }
    // 8. the Create list (also the toolbar +)
    {
        State s = Base(); s.where = Where::Background; s.kind = Kind::Folder; s.hierarchySelection = true;
        Capture("create-menu", "The Create list (empty-space menu, a folder's menu and the toolbar +)", Popup(CreateItems(s)));
    }
    // 9. empty space in a smart view
    {
        State s = Base(); s.where = Where::Background; s.kind = Kind::Folder; s.shownCount = 40; s.smartView = true;
        Capture("empty-space-smart-view", "Right-click empty space while a smart filter (All Models) is showing", Popup(BackgroundMenu(s)));
    }
    // 10. a starred folder
    {
        State s = Base(); s.where = Where::Favourite; s.kind = Kind::Folder; s.favourite = true;
        Capture("favourites-menu", "Right-click a starred folder under FAVOURITES", Popup(FavouriteMenu(s)));
    }
    // 11. Properties
    {
        PropertyInput in;
        in.name = "Hero.png"; in.typeName = "Texture"; in.projectPath = "Assets/Textures/Hero.png"; in.fullPath = "C:/Games/Demo/Assets/Textures/Hero.png";
        in.sizeText = "412 KB"; in.bytes = 421888; in.modified = 1763196180; in.now = 1763196180 + 2 * 86400 + 600;
        in.guid = "3f2a9c41d07b4e1c8a55b6e902f4d118"; in.hasMeta = true; in.uses = 0; in.usedBy = 3; in.missing = 0; in.tags = {"hero", "wip"};
        in.colourLabel = "Orange"; in.favourite = true;
        const std::vector<Property> rows = PropertyRows(in);
        Capture("properties-window", "Properties... of Hero.png", [rows](const std::string&, bool&) {
            ImGui::SetNextWindowPos(ImVec2(40, 60), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_Always);
            ImGui::Begin("Properties##shot", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
            ImGui::Text("Properties: %s", "Hero.png");
            ImGui::Spacing();
            oked::projectpanel::DrawPropertyRows(rows);
            ImGui::Spacing();
            ImGui::Button("Copy Path", ImVec2(110, 0)); ImGui::SameLine(); ImGui::Button("Close", ImVec2(110, 0));
            ImGui::End();
        });
        PropertyInput fo;
        fo.name = "Textures"; fo.typeName = "Folder"; fo.isFolder = true; fo.projectPath = "Assets/Textures"; fo.fullPath = "C:/Games/Demo/Assets/Textures";
        fo.files = 23; fo.folders = 4; fo.sizeText = "18 MB"; fo.bytes = 18874368;
        const std::vector<Property> frows = PropertyRows(fo);
        Capture("properties-folder", "Properties... of a folder", [frows](const std::string&, bool&) {
            ImGui::SetNextWindowPos(ImVec2(40, 60), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_Always);
            ImGui::Begin("Properties##shot2", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
            ImGui::Text("Properties: %s", "Textures");
            ImGui::Spacing();
            oked::projectpanel::DrawPropertyRows(frows);
            ImGui::Spacing();
            ImGui::Button("Copy Path", ImVec2(110, 0)); ImGui::SameLine(); ImGui::Button("Close", ImVec2(110, 0));
            ImGui::End();
        });
        PropertyInput broken = in;
        broken.name = "Level1.okayscene"; broken.typeName = "Scene"; broken.projectPath = "Assets/Scenes/Level1.okayscene"; broken.fullPath = "C:/Games/Demo/Assets/Scenes/Level1.okayscene";
        broken.missing = 2; broken.uses = 5; broken.usesAll = 9; broken.usedBy = 0; broken.tags.clear(); broken.colourLabel.clear(); broken.favourite = false; broken.startScene = true; broken.sizeText = "38 KB"; broken.bytes = 38912;
        const std::vector<Property> brows = PropertyRows(broken);
        Capture("properties-scene", "Properties... of a scene with broken references", [brows](const std::string&, bool&) {
            ImGui::SetNextWindowPos(ImVec2(40, 60), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_Always);
            ImGui::Begin("Properties##shot3", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
            ImGui::Text("Properties: %s", "Level1.okayscene");
            ImGui::Spacing();
            oked::projectpanel::DrawPropertyRows(brows);
            ImGui::Spacing();
            ImGui::Button("Copy Path", ImVec2(110, 0)); ImGui::SameLine(); ImGui::Button("Close", ImVec2(110, 0));
            ImGui::End();
        });
    }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(g_renderer);
    SDL_FreeSurface(g_surface);
    SDL_Quit();
    return 0;
}
