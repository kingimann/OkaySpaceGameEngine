// Golden-image test for the engine UI renderer (okay::ui2d::RenderSceneUI).
//
// Each scene in ui_golden_scenes.hpp is drawn through the SDL painter onto SDL's
// software renderer and compared pixel for pixel with tests/golden/ui/<scene>.png.
// Those PNGs were captured from the player's own UI pass (player/main.cpp) before it
// moved into the engine, so this pins "the engine renderer draws what the player
// drew". A later change that alters UI pixels on purpose regenerates them with:
//   test_ui_renderer_golden --update <dir>     (then review and commit the PNGs)
#define SDL_MAIN_HANDLED
#include "test_framework.hpp"
#include "ui_golden_scenes.hpp"
#include "okay/UI/UIPainterSDL.hpp"
#include "okay/UI/UIRenderer.hpp"
#include <SDL.h>
#include <cstdlib>
#include <string>
#include <unordered_map>

using namespace okay;

namespace {

// Render scene `i` into `out` (RGBA). One renderer is shared by every scene, as in
// the player, because font atlases are cached per process.
void RenderScene(SDL_Renderer* ren, SDL_Surface* surf, int i, const std::string& tex, Image& out) {
    std::unordered_map<std::string, SDL_Texture*> cache;
    SDL_RenderSetClipRect(ren, nullptr);
    SDL_RenderSetScale(ren, 1.0f, 1.0f);
    {
        Scene scene("golden");
        ui_golden::Build(i, scene, tex);
        SDL_SetRenderDrawColor(ren, 20, 24, 30, 255);
        SDL_RenderClear(ren);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        ui2d::SDLPainter painter(ren, &cache, "");
        ui2d::RenderSceneUI(scene, painter, ui_golden::kWidth, ui_golden::kHeight);
        SDL_RenderPresent(ren);
    }
    out = Image(ui_golden::kWidth, ui_golden::kHeight);
    for (int y = 0; y < ui_golden::kHeight; ++y) {
        const unsigned char* row = (const unsigned char*)surf->pixels + y * surf->pitch;
        for (int x = 0; x < ui_golden::kWidth; ++x)
            out.SetPixel(x, y, Color::FromBytes(row[x * 4], row[x * 4 + 1], row[x * 4 + 2], row[x * 4 + 3]));
    }
    for (auto& kv : cache) if (kv.second) SDL_DestroyTexture(kv.second);
}

} // namespace

int main(int argc, char** argv) {
    RUN_SUITE("ui_renderer_golden");
    std::string updateDir;
    for (int a = 1; a + 1 < argc; ++a)
        if (std::string(argv[a]) == "--update") updateDir = argv[a + 1];

    const std::string tex = "ui_golden_texture.png";
    CHECK(ui_golden::WriteTestTexture(tex));
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, ui_golden::kWidth, ui_golden::kHeight, 32,
                                                       SDL_PIXELFORMAT_ABGR8888);
    SDL_Renderer* ren = surf ? SDL_CreateSoftwareRenderer(surf) : nullptr;
    CHECK(ren != nullptr);
    if (!ren) TEST_MAIN_RESULT();

    for (int i = 0; i < ui_golden::SceneCount(); ++i) {
        const std::string name = ui_golden::SceneName(i);
        Image got;
        RenderScene(ren, surf, i, tex, got);
        if (!updateDir.empty()) {
            CHECK(got.SavePNG(updateDir + "/" + name + ".png"));
            std::cout << "  wrote " << name << ".png\n";
            continue;
        }
        Image want;
        const std::string path = std::string(OKAY_SOURCE_DIR) + "/tests/golden/ui/" + name + ".png";
        CHECK(want.Load(path));
        CHECK(want.Width() == got.Width() && want.Height() == got.Height());
        if (want.Width() != got.Width() || want.Height() != got.Height()) continue;
        int diff = 0, maxd = 0, drawn = 0, firstX = -1, firstY = -1;
        const unsigned char* a = want.Data();
        const unsigned char* b = got.Data();
        for (int p = 0; p < got.Width() * got.Height(); ++p) {
            int d = 0;
            for (int c = 0; c < 4; ++c) d = std::max(d, std::abs((int)a[p * 4 + c] - (int)b[p * 4 + c]));
            if (d > 0 && diff++ == 0) { firstX = p % got.Width(); firstY = p / got.Width(); }
            maxd = std::max(maxd, d);
            if (b[p * 4] != 20 || b[p * 4 + 1] != 24 || b[p * 4 + 2] != 30) ++drawn;
        }
        std::cout << "  " << name << ": " << diff << " differing pixels (max channel diff " << maxd
                  << "), " << drawn << " drawn\n";
        if (diff) std::cerr << "    first difference at (" << firstX << ", " << firstY << ")\n";
        CHECK(diff == 0);          // the extraction must be pixel-identical
        CHECK(drawn > 2000);       // and the scene must actually draw something
    }
    SDL_DestroyRenderer(ren);
    SDL_FreeSurface(surf);
    TEST_MAIN_RESULT();
}
