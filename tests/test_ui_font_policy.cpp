// Default in-game text is a TrueType face rasterized at the requested pixel size,
// the glyph atlas is sampled linearly, and the 8x8 pixel-art look is opt-in.
#include "test_framework.hpp"
#include "okay/Graphics/TtfFont.hpp"
#include "okay/UI/UIFontPolicy.hpp"
#include "okay/Graphics/Image.hpp"
#include <cmath>
#include <string>

using namespace okay;

int main() {
    RUN_SUITE("ui_font_policy");

    // Empty scene font = the built-in TrueType. "pixel" = the bitmap, on purpose.
    TtfFont* ui = SceneUIFont("");
    CHECK(ui && ui->Valid());
    CHECK(SceneUIFont("pixel") == nullptr);
    CHECK(BuiltinUIFont() == ui);

    const UITextRaster smooth = UITextRasterFor("", 2.0f, 1.0f);
    CHECK(smooth.trueType);
    CHECK(!smooth.pixelArt);
    CHECK(smooth.filter == UIAtlasFilter::Linear);
    CHECK(smooth.pixelHeight == 16.0f);                 // 2 font-pixels × the 8px cell

    const UITextRaster hi = UITextRasterFor("", 2.0f, 2.0f);
    CHECK(hi.trueType && hi.filter == UIAtlasFilter::Linear);
    CHECK(hi.pixelHeight == 32.0f);                     // scales with the canvas / DPI

    const UITextRaster pixel = UITextRasterFor("pixel", 2.0f, 2.0f);
    CHECK(pixel.pixelArt);
    CHECK(!pixel.trueType);
    CHECK(pixel.filter == UIAtlasFilter::Nearest);

    CHECK(UIGlyphAtlasLinearSampling());

    // The face used for that size is baked at that size, not a fixed atlas scaled later.
    TtfFont* at16 = ui->ForPixelHeight(smooth.pixelHeight);
    CHECK(at16 && at16->Valid());
    CHECK(std::fabs(at16->BakeHeight() - 16.0f) < 0.01f);
    TtfFont* at32 = ui->ForPixelHeight(hi.pixelHeight);
    CHECK(at32 && at32 != at16);
    CHECK(std::fabs(at32->BakeHeight() - 32.0f) < 0.01f);
    CHECK(ui->ForPixelHeight(16.2f) == at16);           // cached, same object

    // The raster is anti-aliased: a curved glyph has grey edge pixels.
    const TtfFont::Glyph* g = at16->Get('O');
    CHECK(g != nullptr);
    if (g) {
        const Image& atlas = at16->Atlas();
        int partial = 0;
        for (int y = g->y0; y < g->y1; ++y)
            for (int x = g->x0; x < g->x1; ++x) {
                const unsigned char a = atlas.Data()[(y * atlas.Width() + x) * 4 + 3];
                if (a > 8 && a < 247) ++partial;
            }
        CHECK(partial > 4);
    }

    // CJK still goes through the existing fallback. で is not in the UI face;
    // a font that has it is used, and that face can be baked at the screen size.
    const std::string kana = "\xE3\x81\xA7";             // で
    TtfFont* picked = FontForText(ui, kana);
    if (picked && picked->Has(0x3067)) {
        TtfFont* sized = picked->ForPixelHeight(18.0f);
        CHECK(sized && sized->Valid() && sized->Has(0x3067));
        CHECK(std::fabs(sized->BakeHeight() - 18.0f) < 0.01f);
    } else {
        CHECK(picked == ui);                            // nothing has it: same as before
    }

    TEST_MAIN_RESULT();
}
