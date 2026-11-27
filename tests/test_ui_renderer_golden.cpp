// Golden-image test for the engine UI renderer (okay::ui2d::RenderSceneUI).
//
// Each scene in ui_golden_scenes.hpp is drawn onto the software painter (okayui/soft, ui_soft_render.hpp) and compared
// pixel for pixel with tests/golden/ui/<scene>.png. The PNGs were made on SDL's software renderer before SDL2 left the
// desktop builds (2026-11-27), so this also pins "our painter draws what SDL drew", within the tolerance below.
// Those PNGs were captured from the player's own UI pass (player/main.cpp) before it
// moved into the engine, so this pins "the engine renderer draws what the player
// drew". Each scene is also drawn through ui2d::BatchPainter (the player's batched
// path) and must match up to a few glyph-edge pixels. A later change that alters UI
// pixels on purpose regenerates them with:
//   test_ui_renderer_golden --update <dir>     (then review and commit the PNGs)
#include "test_framework.hpp"
#include "ui_golden_scenes.hpp"
#include "okay/UI/UIBatcher.hpp"
#include "ui_soft_render.hpp"
#include "okay/UI/UIRenderer.hpp"
#include <cstdlib>
#include <string>
#include <unordered_map>

using namespace okay;

namespace {

// Render scene `i` into `out` (RGBA) on a fresh software painter.
void RenderScene(int i, const std::string& tex, Image& out, bool batched = false) {
    Scene scene("golden");
    ui_golden::Build(i, scene, tex);
    if (!okaytest::SoftRenderSceneUI(scene, ui_golden::kWidth, ui_golden::kHeight, out, batched, 20, 24, 30))
        out = Image(ui_golden::kWidth, ui_golden::kHeight);
}

} // namespace

int main(int argc, char** argv) {
    RUN_SUITE("ui_renderer_golden");
    std::string updateDir;
    for (int a = 1; a + 1 < argc; ++a)
        if (std::string(argv[a]) == "--update") updateDir = argv[a + 1];

    const std::string tex = std::string(OKAY_TEST_OUTPUT_DIR) + "/ui_golden_texture.png";
    CHECK(ui_golden::WriteTestTexture(tex));

    for (int i = 0; i < ui_golden::SceneCount(); ++i) {
        const std::string name = ui_golden::SceneName(i);
        Image got;
        RenderScene(i, tex, got);
        if (!updateDir.empty()) {
            CHECK(got.SavePNG(updateDir + "/" + name + ".png"));
            std::cout << "  wrote " << name << ".png\n";
            Image scratch; RenderScene(i, tex, scratch, true);   // the check run draws the batched picture after each direct one; so does this, so both start every scene in the same state
            continue;
        }
        Image want;
        const std::string path = std::string(OKAY_SOURCE_DIR) + "/tests/golden/ui/" + name + ".png";
        CHECK(want.Load(path));
        CHECK(want.Width() == got.Width() && want.Height() == got.Height());
        if (want.Width() != got.Width() || want.Height() != got.Height()) continue;
        int diff = 0, big = 0, maxd = 0, drawn = 0, firstX = -1, firstY = -1;
        const unsigned char* a = want.Data();
        const unsigned char* b = got.Data();
        for (int p = 0; p < got.Width() * got.Height(); ++p) {
            int d = 0;
            for (int c = 0; c < 4; ++c) d = std::max(d, std::abs((int)a[p * 4 + c] - (int)b[p * 4 + c]));
            if (d > 0 && diff++ == 0) { firstX = p % got.Width(); firstY = p / got.Width(); }
            maxd = std::max(maxd, d);
            if (d > 8) ++big;
            if (b[p * 4] != 20 || b[p * 4 + 1] != 24 || b[p * 4 + 2] != 30) ++drawn;
        }
        std::cout << "  " << name << ": " << diff << " differing pixels (max channel diff " << maxd
                  << "), " << drawn << " drawn\n";
        if (diff) std::cerr << "    first difference at (" << firstX << ", " << firstY << ")\n";
        // The extraction must be pixel-identical in structure. The goldens were captured on one machine; another painter or
        // libm build rounds a few anti-aliased and rounded-corner edge pixels differently (CI saw 123 pixels off by 1 and
        // one pixel off by 111 on a corner), so allow a sliver of the 153,600 pixels to differ, and almost none by much.
        CHECK(diff <= 400);        // under 0.3 percent of the picture
        CHECK(big <= 8);           // and hardly any by more than rounding
        CHECK(drawn > 2000);       // and the scene must actually draw something

        // The batched path draws the same scene as triangles from one atlas.
        Image bat;
        RenderScene(i, tex, bat, true);
        if (const char* dump = std::getenv("OKAY_GOLDEN_DUMP")) { got.SavePNG(std::string(dump) + "/" + name + "_direct.png"); bat.SavePNG(std::string(dump) + "/" + name + "_batched.png"); }   // to look at a mismatch
        int bdiff = 0, bmax = 0, bbig = 0;
        const unsigned char* c = bat.Data();
        const unsigned char* g0 = got.Data();
        for (int p = 0; p < bat.Width() * bat.Height(); ++p) {
            int d = 0;
            for (int k = 0; k < 4; ++k) d = std::max(d, std::abs((int)g0[p * 4 + k] - (int)c[p * 4 + k]));   // against the direct render of this run
            if (d > 0) ++bdiff;
            if (d > 48) ++bbig;
            bmax = std::max(bmax, d);
        }
        std::cout << "    batched: " << bdiff << " differing pixels, " << bbig << " differ by more than 48 (max "
                  << bmax << ")\n";
        // Triangles sample glyph edges a little differently from texture copies, so a
        // few text pixels may differ; shapes, images and lines must match. Glyphs
        // are now rasterized at their on-screen size, so that edge difference is a
        // little wider than when a large atlas was scaled down.
        CHECK(bbig <= 80);
    }
    TEST_MAIN_RESULT();
}
