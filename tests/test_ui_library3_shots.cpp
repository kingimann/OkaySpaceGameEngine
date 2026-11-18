// Headless pictures (software renderer, same UI pass as the game) of the UI library slice 3 parts and of the whole example scene.
// Not a real window.
#define SDL_MAIN_HANDLED
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/UILibrary.hpp"
#include "okay/UI/UILayoutUpdate.hpp"
#include "okay/UI/UIPainterSDL.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "okay/Graphics/Image.hpp"
#include <SDL.h>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>

using namespace okay;

namespace {

bool Render(const std::string& path, int width, int height, const std::function<void(Scene&)>& build,
            const std::function<void(Scene&)>& after = nullptr) {
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
    scene.Start();
    UICanvas::Set((float)width, (float)height);
    UpdateUILayouts(scene);
    scene.Update(0.016f);
    if (after) after(scene);
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
    RUN_SUITE("ui_library3_shots");
    std::string dir = std::string(OKAY_TEST_OUTPUT_DIR) + "/ui_library3";
    if (argc >= 2 && argv[1][0]) dir = argv[1];
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    CHECK(!ec);
    auto at = [&](const char* file) { return dir + "/" + file; };
    auto part = [](Scene& s, const char* name) { return s.CreateGameObject(name); };

    CHECK(Render(at("menubar-open.png"), 520, 260, [&](Scene& s) {
        auto* w = part(s, "Bar")->AddComponent<UIMenuBar>();
        w->anchor = UIAnchor::TopLeft; w->position = {0, 0}; w->size = {520, 32};
        w->menus = "File:New scene|Open@Ctrl+O|Save@Ctrl+S|-|~Export (soon)|Quit;Edit:Undo|Redo;Help:About";
        w->Open(0);
        w->StepHover(1);
    }));
    CHECK(Render(at("toolbar.png"), 520, 70, [&](Scene& s) {
        auto* w = part(s, "Tools")->AddComponent<UIToolbar>();
        w->anchor = UIAnchor::TopLeft; w->position = {10, 12}; w->size = {420, 44};
        w->items = "New@N|Open@O|Save@S|-|Bold@B*|Italic@I*|Underline@U*|-|Left@L=|Center@C=|Right@R=";
        w->states = "00000100010"; w->showLabels = false;
    }));
    CHECK(Render(at("toolbar-labels.png"), 920, 70, [&](Scene& s) {
        auto* w = part(s, "Tools")->AddComponent<UIToolbar>();
        w->anchor = UIAnchor::TopLeft; w->position = {10, 12}; w->size = {900, 44};
        w->items = "New@N|Open@O|~Save@S|-|Bold@B*|Italic@I*|-|Left=|Center=|Right=";
        w->states = "00001000100"; w->showLabels = true;
    }));
    CHECK(Render(at("tree.png"), 360, 230, [&](Scene& s) {
        auto* w = part(s, "Tree")->AddComponent<UITreeView>();
        w->anchor = UIAnchor::TopLeft; w->position = {20, 20}; w->size = {320, 190};
        w->nodes = "World|>Forest|>>Wolf den|>>Old oak|>Castle|>>Gate|>>Throne room|Settings|>Audio|>Video";
        w->expanded = "0,1,4"; w->Select(3); w->SetActive(true);
    }));
    CHECK(Render(at("table.png"), 560, 240, [&](Scene& s) {
        auto* w = part(s, "Table")->AddComponent<UIDataTable>();
        w->anchor = UIAnchor::TopLeft; w->position = {20, 20}; w->size = {520, 200};
        w->columns = "Name:200|Class:180|Level";
        w->rows = "Ava|Mage|42;Bo|Rogue|7;Cy|Knight|30;Di|Mage|8;Eli|Rogue|25;Fen|Knight|51;Gia|Mage|3;Hux|Rogue|12";
        w->SortBy(2, false); w->Select(2);
    }));
    CHECK(Render(at("form-errors.png"), 560, 250, [&](Scene& s) {
        GameObject* g = part(s, "Sign");
        auto* f = g->AddComponent<UIFormGroup>();
        f->anchor = UIAnchor::TopLeft; f->position = {20, 20}; f->size = {520, 200};
        f->fields = "Name:text|Email:email|Age:int:13..120"; f->Rebuild();
        f->SetValue(0, "Ann"); f->SetValue(1, "ann@"); f->SetValue(2, "9");
        f->Submit();
    }));
    CHECK(Render(at("wizard.png"), 700, 280, [&](Scene& s) {
        GameObject* g = part(s, "Wiz");
        auto* w = g->AddComponent<UIWizard>();
        w->anchor = UIAnchor::TopLeft; w->position = {20, 20}; w->size = {660, 240};
        w->steps = "Account|Profile|Confirm|Done"; w->step = 1;
        w->validate = ";if:never";
    }));
    CHECK(Render(at("color-advanced.png"), 520, 330, [&](Scene& s) {
        GameObject* g = part(s, "Picker");
        auto* cp = g->AddComponent<UIColorPicker>();
        cp->position = {20, 20}; cp->advanced = true; cp->Rebuild();
        cp->SetHSVA(200.0f, 0.65f, 0.9f, 1.0f);
    }, [&](Scene& s) { s.Update(0.016f); }));
    // the whole example scene
    for (int variant = 0; variant < 2; ++variant) {
        const int h = variant == 0 ? 720 : 900;
        CHECK(Render(at(variant == 0 ? "example3-1280x720.png" : "example3-1280x900.png"), 1280, h, [&](Scene& s) { UILibrary::BuildSlice3(s); },
                     [&](Scene& s) {
                         if (auto* t = s.Find("Tree")) t->GetComponent<UITreeView>()->Select(2);
                         if (auto* t = s.Find("Table")) t->GetComponent<UIDataTable>()->SortBy(2, false);
                         if (auto* b = s.Find("MenuBar")) { b->GetComponent<UIMenuBar>()->Open(1); b->GetComponent<UIMenuBar>()->StepHover(1); }
                         s.Update(0.016f);
                     }));
    }
    TEST_MAIN_RESULT();
}
