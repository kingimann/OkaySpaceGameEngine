// Headless pictures of the eight app widgets, drawn with the same UI pass as the game.
#define SDL_MAIN_HANDLED
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/UI/UIPainterSDL.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "okay/Graphics/Image.hpp"
#include <SDL.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>

using namespace okay;

namespace {

bool Render(const std::string& path, int width, int height, const std::function<void(Scene&)>& build) {
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ABGR8888);
    SDL_Renderer* ren = surf ? SDL_CreateSoftwareRenderer(surf) : nullptr;
    if (!ren) {
        if (surf) SDL_FreeSurface(surf);
        return false;
    }
    std::unordered_map<std::string, SDL_Texture*> cache;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 18, 22, 32, 255);
    SDL_RenderClear(ren);
    Scene scene("shot");
    scene.physicsEnabled = false;
    scene.uiFont = "pixel";
    build(scene);
    ui2d::SDLPainter painter(ren, &cache, "");
    ui2d::RenderSceneUI(scene, painter, width, height);
    SDL_RenderPresent(ren);
    Image img(width, height);
    for (int y = 0; y < height; ++y) {
        const unsigned char* row = (const unsigned char*)surf->pixels + y * (int)surf->pitch;
        for (int x = 0; x < width; ++x)
            img.SetPixel(x, y, Color::FromBytes(row[x * 4], row[x * 4 + 1], row[x * 4 + 2], row[x * 4 + 3]));
    }
    for (auto& kv : cache) if (kv.second) SDL_DestroyTexture(kv.second);
    SDL_DestroyRenderer(ren);
    SDL_FreeSurface(surf);
    int drawn = 0;
    const unsigned char* px = img.Data();
    for (int i = 0; i < width * height; ++i)
        if (px[i * 4] != 18 || px[i * 4 + 1] != 22 || px[i * 4 + 2] != 32) ++drawn;
    if (drawn < 40) return false;
    return img.SavePNG(path);
}

} // namespace

int main(int argc, char** argv) {
    RUN_SUITE("ui_widgets_more_shots");
    std::string dir = std::string(OKAY_TEST_OUTPUT_DIR) + "/ui_widgets_more";
    if (argc >= 2 && argv[1][0]) dir = argv[1];
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    CHECK(!ec);

    auto at = [&](const char* file) { return dir + "/" + file; };
    CHECK(Render(at("search.png"), 360, 200, [](Scene& s) {
        auto* w = s.CreateGameObject("Search")->AddComponent<UISearchBar>();
        w->anchor = UIAnchor::TopLeft; w->position = {20, 16}; w->size = {320, 168};
        w->itemsText = "Apple\nApricot\nBanana\nCherry"; w->query = "ap"; w->value = 0;
    }));
    CHECK(Render(at("date-time.png"), 500, 100, [](Scene& s) {
        auto* w = s.CreateGameObject("When")->AddComponent<UIDateTimePicker>();
        w->anchor = UIAnchor::TopLeft; w->position = {16, 18}; w->size = {468, 64};
        w->year = 2026; w->month = 11; w->day = 13; w->hour = 15; w->minute = 30;
    }));
    const std::filesystem::path saves = std::filesystem::temp_directory_path() / "okay-ui-shots" / "Saves";
    std::filesystem::remove_all(saves, ec);
    std::filesystem::create_directories(saves / "notes", ec);
    { std::ofstream(saves / "readme.txt") << "hi"; }
    { std::ofstream(saves / "notes" / "one.txt") << "one"; }
    CHECK(Render(at("file-picker.png"), 360, 220, [&](Scene& s) {
        auto* w = s.CreateGameObject("Files")->AddComponent<UIFilePicker>();
        w->anchor = UIAnchor::TopLeft; w->position = {20, 16}; w->size = {320, 188};
        w->SetRoot(saves.string());
    }));
    std::filesystem::remove_all(saves, ec);
    CHECK(Render(at("accordion.png"), 360, 200, [](Scene& s) {
        auto* w = s.CreateGameObject("Sections")->AddComponent<UIAccordion>();
        w->anchor = UIAnchor::TopLeft; w->position = {20, 12}; w->size = {320, 170};
        w->sectionsText = "General | Sound, picture and controls\nAdvanced | Extra options";
        w->openText = "1,0";
    }));
    CHECK(Render(at("breadcrumbs.png"), 460, 70, [](Scene& s) {
        auto* w = s.CreateGameObject("Crumbs")->AddComponent<UIBreadcrumbs>();
        w->anchor = UIAnchor::TopLeft; w->position = {16, 16}; w->size = {428, 36};
        w->pathText = "Home/Library/Books"; w->value = 1;
    }));
    CHECK(Render(at("pagination.png"), 400, 70, [](Scene& s) {
        auto* w = s.CreateGameObject("Pages")->AddComponent<UIPagination>();
        w->anchor = UIAnchor::TopLeft; w->position = {16, 16}; w->size = {368, 36};
        w->page = 3; w->pageCount = 8;
    }));
    CHECK(Render(at("skeleton.png"), 360, 120, [](Scene& s) {
        auto* w = s.CreateGameObject("Loading")->AddComponent<UISkeleton>();
        w->anchor = UIAnchor::TopLeft; w->position = {20, 12}; w->size = {320, 96};
        w->loading = true; w->shimmer = 0.35f;
    }));
    CHECK(Render(at("bottom-sheet.png"), 420, 260, [](Scene& s) {
        auto* w = s.CreateGameObject("Sheet")->AddComponent<UIBottomSheet>();
        w->anchor = UIAnchor::TopLeft; w->position = {24, 40}; w->size = {372, 200};
        w->open = true; w->progress = 1.0f; w->dim = true; w->title = "Filters";
    }));
    std::cout << "  wrote widget pictures to " << dir << "\n";
    TEST_MAIN_RESULT();
}
