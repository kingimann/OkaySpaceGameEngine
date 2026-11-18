// Picture tool (not a test): draws the Controls screen on the typed input system with the game's own UI pass on SDL's software
// renderer (no window, no GPU) and writes controls-screen-<size>.png into the folder given as the first argument or $OKAY_SHOT_DIR.
// Nothing is written without one: pictures are not kept in the repository.
#define SDL_MAIN_HANDLED
#include <Okay.hpp>
#include "okay/Scene/UIGallery.hpp"
#include "okay/UI/UIPainterSDL.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "okay/Graphics/Image.hpp"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

using namespace okay;

static bool Shot(const std::string& path, int w, int h) {
    InputRuntime::Reset();
    Scene scene("controls"); scene.physicsEnabled = false;
    UIGallery::AddControlsScreen(scene);
    scene.Start();
    Input::FeedKeys({}); InputActions::Tick(0.016f); scene.Update(0.016f);
    // one field waits for a key, one has been rebound
    InputRuntime::Set(0)->Rebind("Sprint", 0, 0, "key:v");
    if (GameObject* g = scene.Find("Key Jump")) if (auto* kb = g->GetComponent<UIKeybindField>()) kb->Listen();
    for (int i = 0; i < 4; ++i) { InputActions::Tick(0.016f); scene.Update(0.016f); }
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
    SDL_Renderer* ren = surf ? SDL_CreateSoftwareRenderer(surf) : nullptr;
    if (!ren) return false;
    std::unordered_map<std::string, SDL_Texture*> cache;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 18, 22, 32, 255);
    SDL_RenderClear(ren);
    UICanvas::Set((float)w, (float)h);
    ui2d::SDLPainter painter(ren, &cache, "");
    ui2d::UIRenderOptions opt; opt.interactive = false;
    ui2d::RenderSceneUI(scene, painter, w, h, opt);
    SDL_RenderPresent(ren);
    Image img(w, h);
    for (int y = 0; y < h; ++y) {
        const unsigned char* row = (const unsigned char*)surf->pixels + y * (int)surf->pitch;
        for (int x = 0; x < w; ++x) img.SetPixel(x, y, Color::FromBytes(row[x * 4], row[x * 4 + 1], row[x * 4 + 2], row[x * 4 + 3]));
    }
    for (auto& kv : cache) if (kv.second) SDL_DestroyTexture(kv.second);
    SDL_DestroyRenderer(ren); SDL_FreeSurface(surf);
    const bool ok = img.SavePNG(path);
    std::printf("wrote %s%s\n", path.c_str(), ok ? "" : " (FAILED)");
    return ok;
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 && argv[1][0] ? argv[1] : std::getenv("OKAY_SHOT_DIR");
    if (!dir) { std::printf("give an output folder (argument or OKAY_SHOT_DIR); no picture is kept in the repository\n"); return 0; }
    bool ok = Shot(std::string(dir) + "/controls-screen-1280x720.png", 1280, 720);
    ok = Shot(std::string(dir) + "/controls-screen-1920x1080.png", 1920, 1080) && ok;
    return ok ? 0 : 1;
}
