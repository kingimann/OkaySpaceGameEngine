// Headless test for the TTF rasterizer (okay::TtfFont). Loads a system font if one
// is available; if none is found the test skips its assertions (still passes), so CI
// without fonts stays green while a dev box exercises the real path.
#include "okay/Graphics/TtfFont.hpp"
#include "test_framework.hpp"

#include <cstdio>
#include <filesystem>

using namespace okay;

int main() {
    RUN_SUITE("ttf");
    namespace fs = std::filesystem;
    // Common font locations across distros + this dev container.
    const char* candidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/Library/Fonts/Arial.ttf",
        "/mnt/skills/examples/canvas-design/canvas-fonts/ArsenalSC-Regular.ttf",
    };
    std::string path;
    for (const char* c : candidates) { std::error_code ec; if (fs::exists(c, ec)) { path = c; break; } }

    if (path.empty()) {
        std::printf("[test_ttf] no system font found; skipping TTF assertions (pass)\n");
        TEST_MAIN_RESULT();
    }
    std::printf("[test_ttf] using %s\n", path.c_str());

    TtfFont font;
    CHECK(font.LoadFromFile(path, 48.0f));
    CHECK(font.Valid());
    CHECK(font.BakeHeight() == 48.0f);

    // The atlas is a non-empty RGBA image with some non-zero coverage (alpha).
    const Image& atlas = font.Atlas();
    CHECK(atlas.Width() > 0 && atlas.Height() > 0);
    long alphaSum = 0;
    const std::uint8_t* d = atlas.Data();
    for (std::size_t i = 0; i < (std::size_t)atlas.Width() * atlas.Height(); ++i) alphaSum += d[i * 4 + 3];
    CHECK(alphaSum > 0);   // at least some glyph coverage was baked

    // 'A' should have a real glyph rect and a positive advance.
    const TtfFont::Glyph* A = font.Get('A');
    CHECK(A != nullptr);
    CHECK(A->x1 > A->x0 && A->y1 > A->y0);
    CHECK(A->xadvance > 0.0f);

    // A space advances but draws nothing.
    const TtfFont::Glyph* sp = font.Get(' ');
    CHECK(sp != nullptr);
    CHECK(sp->xadvance > 0.0f);

    // Measuring more text is wider than less; newlines split lines (width = max line).
    float wA  = font.Measure("A", 24.0f);
    float wAB = font.Measure("AB", 24.0f);
    CHECK(wAB > wA);
    CHECK(font.Measure("A\nABC", 24.0f) >= font.Measure("ABC", 24.0f) - 0.01f);

    // Scaling is linear in pixel height.
    float w24 = font.Measure("Hello", 24.0f), w48 = font.Measure("Hello", 48.0f);
    CHECK(w48 > w24 * 1.8f && w48 < w24 * 2.2f);

    // Out-of-range chars return null; the cache hands back the same instance.
    CHECK(font.Get((char)200) == nullptr);
    CHECK(GetFont(path) != nullptr);
    CHECK(GetFont(path) == GetFont(path));
    CHECK(GetFont("nonexistent-font-xyz.ttf") == nullptr);

    std::printf("[test_ttf] ok\n");
    TEST_MAIN_RESULT();
}
