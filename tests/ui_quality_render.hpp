#pragma once
// Headless render of any Scene through the UI pass onto SDL's software renderer, for the UI quality test.
#define SDL_MAIN_HANDLED
#include "ui_quality_scene.hpp"
#include "okay/UI/UIBatcher.hpp"
#include "okay/UI/UIPainterSDL.hpp"
#include "okay/UI/UIRenderer.hpp"
#include <SDL.h>
#include <string>
#include <unordered_map>

namespace ui_quality {
using namespace okay;

/// Draw `scene` on a W x H target cleared to (bgR, bgG, bgB); RGBA out. `batched` = the player's batched path.
inline bool RenderScene(Scene& scene, int W, int H, Image& out, bool batched = false, int bgR = 14, int bgG = 16, int bgB = 22) {
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ABGR8888);
    SDL_Renderer* ren = surf ? SDL_CreateSoftwareRenderer(surf) : nullptr;
    if (!ren) { if (surf) SDL_FreeSurface(surf); return false; }
    std::unordered_map<std::string, SDL_Texture*> cache;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, (Uint8)bgR, (Uint8)bgG, (Uint8)bgB, 255);
    SDL_RenderClear(ren);
    {
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
    ui2d::SDLForgetRenderer(ren);
    SDL_DestroyRenderer(ren);
    SDL_FreeSurface(surf);
    return true;
}

inline int Byte(const Image& im, int x, int y, int c) {
    const Color p = im.GetPixel(x, y);
    const float v = c == 0 ? p.r : (c == 1 ? p.g : p.b);
    return (int)(v * 255.0f + 0.5f);
}

} // namespace ui_quality
