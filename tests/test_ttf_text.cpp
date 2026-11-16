// Text with our own TrueType code at the TtfFont level: pair kerning in measuring, the
// per-character fallback chain (FontChain: JetBrains Mono, then Roboto), and golden images
// of a few strings at each UI size drawn from TtfFont's own atlas.
//
// Goldens: tests/golden/ttf/<font>_<size>.png (grey, ink = 255). OKAY_UPDATE_GOLDEN=1
// writes them again. A pixel may differ by 2 levels (float rounding on other compilers).
#include "okay/Graphics/TtfFont.hpp"
#include "okay/Graphics/TtfRaster.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Core/ImageWrite.hpp"
#include "okay/Core/Utf8.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace okay;

namespace {

struct Gray { int w = 0, h = 0; std::vector<std::uint8_t> px; };

// Draw `text` from `f`'s atlas at its bake size: pens advance by the glyph advances plus
// kerning, each glyph lands on the whole pixel nearest its pen (how a 1:1 host draws it).
Gray DrawLine(const TtfFont& f, const std::string& text) {
    const float px = f.BakeHeight();
    const int W = (int)std::ceil(f.Measure(text.c_str(), px)) + 8;
    const int H = (int)std::ceil(f.LineHeight(px)) + 4;
    Gray g; g.w = W; g.h = H; g.px.assign((std::size_t)W * H, 0);
    const Image& atlas = f.Atlas();
    const int baseline = 2 + (int)std::lround(f.Ascent(px));
    float pen = 4.0f;
    std::uint32_t prev = 0;
    for (std::size_t i = 0; i < text.size();) {
        const std::uint32_t cp = utf8::Next(text, i);
        const TtfFont::Glyph* gl = f.GlyphFor(cp);
        if (!gl) { prev = 0; continue; }
        if (prev) pen += f.Kerning(prev, cp);
        prev = cp;
        const int ox = (int)std::floor(pen + 0.5f) + (int)gl->xoff, oy = baseline + (int)gl->yoff;
        for (int y = gl->y0; y < gl->y1; ++y)
            for (int x = gl->x0; x < gl->x1; ++x) {
                const int X = ox + (x - gl->x0), Y = oy + (y - gl->y0);
                if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
                const int a = atlas.Data()[((std::size_t)y * atlas.Width() + x) * 4 + 3];
                std::uint8_t& o = g.px[(std::size_t)Y * W + X];
                o = (std::uint8_t)std::min(255, a + o * (255 - a) / 255);
            }
        pen += gl->xadvance;
    }
    return g;
}

bool CheckGolden(const std::string& name, const Gray& g) {
    const std::string path = std::string(OKAY_SOURCE_DIR) + "/tests/golden/ttf/" + name + ".png";
    const char* upd = std::getenv("OKAY_UPDATE_GOLDEN");
    if (upd && *upd == '1') {
        const bool ok = ImageWrite::WritePNG(path, g.w, g.h, 1, g.px.data());
        std::printf("  wrote %s\n", path.c_str());
        return ok;
    }
    Image ref;
    if (!ref.Load(path)) { std::fprintf(stderr, "  missing golden %s (OKAY_UPDATE_GOLDEN=1 writes it)\n", path.c_str()); return false; }
    if (ref.Width() != g.w || ref.Height() != g.h) {
        std::fprintf(stderr, "  %s: size %dx%d, golden %dx%d\n", name.c_str(), g.w, g.h, ref.Width(), ref.Height());
        return false;
    }
    int worst = 0, off = 0;
    for (int i = 0; i < g.w * g.h; ++i) {
        const int d = std::abs((int)g.px[(std::size_t)i] - (int)ref.Data()[(std::size_t)i * 4]);
        worst = std::max(worst, d);
        if (d > 2) ++off;
    }
    if (off) std::fprintf(stderr, "  %s: %d pixels differ by more than 2 levels (worst %d)\n", name.c_str(), off, worst);
    return off == 0;
}

} // namespace

int main() {
    TtfFont* ui = BuiltinUIFont();
    TtfFont* code = BuiltinCodeFont();
    CHECK(ui && ui->Valid());
    CHECK(code && code->Valid());
    if (!ui || !code) TEST_MAIN_RESULT();

    RUN_SUITE("kerning in TtfFont");
    {
        CHECK(ui->HasKerning());
        CHECK(!code->HasKerning());   // monospaced: no pairs
        const float s = 64.0f / (float)(1900 + 500);   // Roboto: hhea ascent 1900, descent -500 (font units)
        CHECK_NEAR(ui->Kerning('A', 'V'), -77 * s, 1e-3);
        CHECK_NEAR(ui->Kerning('T', 'o'), -208 * s, 1e-3);
        CHECK_NEAR(ui->Kerning('o', 'T'), 0.0, 1e-6);
        // Measure includes it: "AV" is narrower than A + V, by exactly the pair's value.
        const float a = ui->Measure("A", 32.0f), v = ui->Measure("V", 32.0f), av = ui->Measure("AV", 32.0f);
        CHECK_NEAR(av, a + v + ui->Kerning('A', 'V') * ui->ScaleFor(32.0f), 1e-3);
        CHECK(av < a + v - 0.5f);
        CHECK_NEAR(ui->Measure("AV\nAV", 32.0f), av, 1e-4);   // no kerning across a line break
        CHECK_NEAR(code->Measure("AV", 32.0f), code->Measure("A", 32.0f) * 2, 1e-3);
    }

    RUN_SUITE("fallback chain");
    {
        FontChain chain({code, ui});
        CHECK(chain.Fonts().size() == 2);
        chain.Add(code);   // no duplicates
        chain.Add(nullptr);
        CHECK(chain.Fonts().size() == 2);
        // A character JetBrains Mono lacks and Roboto has.
        std::uint32_t onlyUi = 0;
        for (std::uint32_t cp = 0x21; cp < 0x3000 && !onlyUi; ++cp) if (!code->Has(cp) && ui->Has(cp)) onlyUi = cp;
        CHECK(onlyUi != 0);
        CHECK(chain.FontFor('m') == code);
        CHECK(chain.FontFor(' ') == code);
        CHECK(chain.FontFor(onlyUi) == ui);
        CHECK(chain.FontFor(0x4E00) == nullptr);   // CJK: neither has it
        std::string text = "ab";
        utf8::Append(text, onlyUi);
        text += "cd";
        const auto runs = chain.Split(text);
        CHECK(runs.size() == 3);
        if (runs.size() == 3) {
            CHECK(runs[0].font == code && runs[0].text == "ab");
            CHECK(runs[1].font == ui);
            CHECK(runs[2].font == code && runs[2].text == "cd");
        }
        std::string one; utf8::Append(one, onlyUi);
        const float px = 20.0f;
        CHECK_NEAR(chain.Measure(text.c_str(), px),
                   code->Measure("ab", px) + ui->Measure(one.c_str(), px) + code->Measure("cd", px), 1e-3);
        std::string cjk = "a"; utf8::Append(cjk, 0x4E00);
        CHECK_NEAR(chain.Measure(cjk.c_str(), px), code->Measure("a", px) + px * 0.5f, 1e-3);   // a gap for the missing one
        // The order matters: Roboto first takes the letters too.
        FontChain other({ui, code});
        CHECK(other.FontFor('m') == ui);
        CHECK(FontChain().FontFor('a') == nullptr && FontChain().Measure("abc", 12.0f) == 0.0f);
    }

    RUN_SUITE("golden images of our rasteriser");
    {
        const int sizes[] = {12, 14, 16, 18, 24, 32, 48};
        for (int px : sizes) {
            TtfFont* u = ui->ForPixelHeight((float)px);
            TtfFont* c = code->ForPixelHeight((float)px);
            CHECK(u && c && std::fabs(u->BakeHeight() - (float)px) < 0.51f);
            if (!u || !c) continue;
            CHECK(CheckGolden("roboto_" + std::to_string(px), DrawLine(*u, "AVATAR To Ty: The quick brown fox 0123456789 \xC3\xA9\xC3\xA8\xC3\xBC\xC3\x87")));
            CHECK(CheckGolden("mono_" + std::to_string(px), DrawLine(*c, "fn f(x) { return x != 0 && y[i] <= 0x1F; } // 0O 1lI")));
        }
    }
    TEST_MAIN_RESULT();
}
