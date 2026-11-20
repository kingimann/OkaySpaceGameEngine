// Headless picture of the UI-quality screen (tests/ui_quality_scene.hpp) through the same UI pass the game uses, on SDL's
// software renderer. Usage: ui_quality_shot <out.png> <width> <height> [old|fast|good|best] [crop x y w h zoom]
// With a crop, that part of the picture is cut out and magnified with nearest-neighbour so single pixels can be inspected.
#define SDL_MAIN_HANDLED
#include "ui_quality_scene.hpp"
#include "okay/UI/UIBatcher.hpp"
#include "okay/UI/UIPainterSDL.hpp"
#include "okay/UI/UIRenderer.hpp"
#include <SDL.h>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>

using namespace okay;

int main(int argc, char** argv) {
    // ui_quality_shot --save-scene <dir>: write examples/ui_quality (three scenes: Good, Best, Old look, and the picture they share).
    if (argc > 2 && std::string(argv[1]) == "--save-scene") {
        const std::string dir = argv[2];
        if (!ui_quality::WriteTexture(dir + "/ui_quality_icon.png")) { std::cerr << "cannot write the picture\n"; return 1; }
        const struct { const char* file; int q; } outs[] = {{"ui_quality.okayscene", 1}, {"ui_quality_best.okayscene", 2}, {"ui_quality_old_look.okayscene", 3}};
        for (const auto& o : outs) {
            Scene scene("ui_quality");
            ui_quality::BuildExample(scene, "ui_quality_icon.png", o.q);
            std::string err;
            if (!SceneSerializer::SaveToFile(scene, dir + "/" + o.file, &err)) { std::cerr << err << "\n"; return 1; }
            std::cout << "wrote " << dir << "/" << o.file << "\n";
        }
        return 0;
    }
    if (argc < 4) { std::cerr << "usage: ui_quality_shot <out.png> <w> <h> [old|fast|good|best] [x y w h zoom]\n"; return 2; }
    const std::string out = argv[1];
    const int W = std::atoi(argv[2]), H = std::atoi(argv[3]);
    const std::string q = argc > 4 ? argv[4] : "good";
    const int quality = q == "fast" ? 0 : q == "best" ? 2 : q == "old" ? 3 : 1;
    const bool batched = std::getenv("UI_QUALITY_BATCHED") != nullptr;
    const std::string tex = out + ".tex.png";
    ui_quality::WriteTexture(tex);
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ABGR8888);
    SDL_Renderer* ren = surf ? SDL_CreateSoftwareRenderer(surf) : nullptr;
    if (!ren) { std::cerr << "no software renderer\n"; return 1; }
    std::unordered_map<std::string, SDL_Texture*> cache;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 14, 16, 22, 255);
    SDL_RenderClear(ren);
    double ms = 0.0;
    {
        Scene scene("quality");
        if (std::getenv("UI_QUALITY_EXAMPLE")) ui_quality::BuildExample(scene, tex, quality); else ui_quality::Build(scene, tex, quality);
        ui2d::SDLPainter painter(ren, &cache, "");
        const auto t0 = std::chrono::steady_clock::now();
        if (batched) {
            ui2d::BatchPainter batch(painter);
            ui2d::RenderSceneUI(scene, batch, W, H);
            batch.Flush();
        } else {
            ui2d::RenderSceneUI(scene, painter, W, H);
        }
        ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
    SDL_RenderPresent(ren);
    Image img(W, H);
    for (int y = 0; y < H; ++y) {
        const unsigned char* row = (const unsigned char*)surf->pixels + y * (int)surf->pitch;
        for (int x = 0; x < W; ++x) img.SetPixel(x, y, Color::FromBytes(row[x * 4], row[x * 4 + 1], row[x * 4 + 2], row[x * 4 + 3]));
    }
    if (argc > 9) {
        const int cx = std::atoi(argv[5]), cy = std::atoi(argv[6]), cw = std::atoi(argv[7]), ch = std::atoi(argv[8]), z = std::max(1, std::atoi(argv[9]));
        Image c(cw * z, ch * z);
        for (int y = 0; y < ch * z; ++y)
            for (int x = 0; x < cw * z; ++x) {
                const int sx = std::min(W - 1, std::max(0, cx + x / z)), sy = std::min(H - 1, std::max(0, cy + y / z));
                c.SetPixel(x, y, img.GetPixel(sx, sy));
            }
        img = c;
    }
    std::remove(tex.c_str());
    if (!img.SavePNG(out)) { std::cerr << "failed to write " << out << "\n"; return 1; }
    std::cout << "wrote " << out << " " << img.Width() << "x" << img.Height() << " (" << q << ", UI draw " << ms << " ms)\n";
    for (auto& kv : cache) if (kv.second) SDL_DestroyTexture(kv.second);
    ui2d::SDLForgetRenderer(ren);
    SDL_DestroyRenderer(ren);
    SDL_FreeSurface(surf);
    return 0;
}
