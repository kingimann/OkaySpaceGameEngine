// Headless 1280x720 picture of a menu drawn by the same UI pass as the game.
// Usage: crisp_ui_shot <out.png>
#define SDL_MAIN_HANDLED
#include <Okay.hpp>
#include "okay/UI/UIPainterSDL.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "okay/Graphics/Image.hpp"
#include <SDL.h>
#include <iostream>
#include <string>
#include <unordered_map>

using namespace okay;

int main(int argc, char** argv) {
    if (argc < 2 || !argv[1][0]) {
        std::cerr << "usage: crisp_ui_shot <out.png>\n";
        return 2;
    }
    const int W = 1280, H = 720;
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ABGR8888);
    SDL_Renderer* ren = surf ? SDL_CreateSoftwareRenderer(surf) : nullptr;
    if (!ren) {
        std::cerr << "no software renderer\n";
        if (surf) SDL_FreeSurface(surf);
        return 1;
    }
    std::unordered_map<std::string, SDL_Texture*> cache;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 14, 16, 22, 255);
    SDL_RenderClear(ren);

    Scene scene("Menu");
    scene.physicsEnabled = false;
    // Empty uiFont is the shipped default (the built-in TrueType face). "pixel" is opt-in.
    scene.uiFont.clear();

    GameObject* cv = scene.CreateGameObject("Canvas");
    cv->AddComponent<Canvas>()->scaleMode = Canvas::ScaleMode::ConstantPixelSize;
    auto add = [&](const char* name) {
        GameObject* g = scene.CreateGameObject(name);
        g->transform->SetParent(cv->transform, false);
        return g;
    };

    auto* card = add("Card")->AddComponent<UIPanel>();
    card->position = {340, 70};
    card->size = {600, 560};
    card->shape = UIShape::Rounded;
    card->cornerRadius = 18.0f;
    card->color = Color::FromBytes(28, 32, 44, 255);
    card->borderWidth = 2.0f;
    card->borderColor = Color::FromBytes(70, 82, 110, 255);

    auto* title = add("Title")->AddComponent<TextRenderer>();
    title->screenSpace = true;
    title->text = "OkaySpace";
    title->screenPos = {500, 100};
    title->size = {280, 48};
    title->pixelSize = 4.0f;
    title->color = Color::White;
    title->shadow = false;
    title->outline = false;

    auto* sub = add("Sub")->AddComponent<TextRenderer>();
    sub->screenSpace = true;
    sub->text = "A menu with smooth controls";
    sub->screenPos = {470, 152};
    sub->size = {360, 32};
    sub->pixelSize = 2.0f;
    sub->color = Color::FromBytes(170, 182, 206, 255);
    sub->shadow = false;
    sub->outline = false;

    auto* play = add("Continue")->AddComponent<UIButton>();
    play->position = {400, 220};
    play->size = {480, 56};
    play->label = "Continue";
    play->shape = UIShape::Rounded;
    play->cornerRadius = 12.0f;
    play->fontScale = 2.5f;
    play->color = Color::FromBytes(66, 140, 250, 255);
    play->textColor = Color::White;
    play->SetFocused(true);

    auto* opt = add("Options")->AddComponent<UIButton>();
    opt->position = {400, 292};
    opt->size = {480, 56};
    opt->label = "Options";
    opt->shape = UIShape::Rounded;
    opt->cornerRadius = 12.0f;
    opt->fontScale = 2.5f;
    opt->color = Color::FromBytes(46, 52, 68, 255);
    opt->textColor = Color::FromBytes(230, 234, 242, 255);
    opt->borderWidth = 2.0f;
    opt->borderColor = Color::FromBytes(90, 104, 136, 255);

    auto* music = add("MusicLabel")->AddComponent<TextRenderer>();
    music->screenSpace = true;
    music->text = "Music";
    music->screenPos = {400, 380};
    music->size = {120, 28};
    music->pixelSize = 2.0f;
    music->color = Color::FromBytes(210, 216, 230, 255);
    music->shadow = false;
    music->outline = false;

    auto* slider = add("Music")->AddComponent<UISlider>();
    slider->position = {400, 414};
    slider->size = {480, 18};
    slider->value = 0.62f;
    slider->trackShape = UIShape::Pill;
    slider->roundKnob = true;
    slider->knobSize = 1.35f;
    slider->background = Color::FromBytes(40, 46, 62, 255);
    slider->fill = Color::FromBytes(66, 140, 250, 255);
    slider->knob = Color::FromBytes(245, 247, 252, 255);

    auto* search = add("Search")->AddComponent<UIInputField>();
    search->position = {400, 470};
    search->size = {480, 52};
    search->text = "castle";
    search->placeholder = "Search";
    search->shape = UIShape::Rounded;
    search->cornerRadius = 12.0f;
    search->color = Color::FromBytes(20, 24, 34, 255);
    search->textColor = Color::White;
    search->borderWidth = 2.0f;
    search->borderColor = Color::FromBytes(90, 104, 136, 255);

    ui2d::SDLPainter painter(ren, &cache, "");
    ui2d::RenderSceneUI(scene, painter, W, H);
    ui2d::DrawUICross(painter, 828, 486, 18, ui2d::RGBA{186, 196, 214, 255});

    SDL_RenderPresent(ren);
    Image img(W, H);
    for (int y = 0; y < H; ++y) {
        const unsigned char* row = (const unsigned char*)surf->pixels + y * (int)surf->pitch;
        for (int x = 0; x < W; ++x)
            img.SetPixel(x, y, Color::FromBytes(row[x * 4], row[x * 4 + 1], row[x * 4 + 2], row[x * 4 + 3]));
    }
    for (auto& kv : cache) if (kv.second) SDL_DestroyTexture(kv.second);
    SDL_DestroyRenderer(ren);
    SDL_FreeSurface(surf);
    if (!img.SavePNG(argv[1])) {
        std::cerr << "failed to write " << argv[1] << "\n";
        return 1;
    }
    std::cout << "wrote " << argv[1] << " " << W << "x" << H << "\n";
    return 0;
}
