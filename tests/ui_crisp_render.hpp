#pragma once
// Headless render of the crisp-UI scene through the UI pass onto SDL's software renderer.
#define SDL_MAIN_HANDLED
#include "ui_crisp_scene.hpp"
#include "okay/UI/UIBatcher.hpp"
#include "okay/UI/UIPainterSDL.hpp"
#include "okay/UI/UIRenderer.hpp"
#include <SDL.h>
#include <string>
#include <unordered_map>

namespace ui_crisp {
using namespace okay;
/// Draw the scene on a W x H target (RGBA out). `pixel` = the scene's pixel-art look.
inline bool Render(int W, int H, const std::string& tex, bool pixel, bool batched, Image& out) {
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ABGR8888);
    SDL_Renderer* ren = surf ? SDL_CreateSoftwareRenderer(surf) : nullptr;
    if (!ren) { if (surf) SDL_FreeSurface(surf); return false; }
    std::unordered_map<std::string, SDL_Texture*> cache;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 14, 16, 22, 255);
    SDL_RenderClear(ren);
    {
        Scene scene("crisp");
        ui_crisp::Build(scene, tex, pixel);
        ui2d::SDLPainter painter(ren, &cache, "");
        if (batched) {
            ui2d::BatchPainter batch(painter);
            ui2d::RenderSceneUI(scene, batch, W, H);
            batch.Flush();
        } else {
            ui2d::RenderSceneUI(scene, painter, W, H);
        }
    }
    SDL_RenderPresent(ren);
    out = Image(W, H);
    for (int y = 0; y < H; ++y) {
        const unsigned char* row = (const unsigned char*)surf->pixels + y * (int)surf->pitch;
        for (int x = 0; x < W; ++x)
            out.SetPixel(x, y, Color::FromBytes(row[x * 4], row[x * 4 + 1], row[x * 4 + 2], row[x * 4 + 3]));
    }
    for (auto& kv : cache) if (kv.second) SDL_DestroyTexture(kv.second);
    SDL_DestroyRenderer(ren);
    SDL_FreeSurface(surf);
    return true;
}


} // namespace ui_crisp
