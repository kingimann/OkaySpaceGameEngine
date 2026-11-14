// Texture import settings that can be applied without a GPU: `compress` (software block codec, the
// picture shows what a compressed texture looks like) and reading `srgb`.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/ImportSidecar.hpp"
#include "okay/Graphics/TextureCompress.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace okay;
namespace fs = std::filesystem;

static void WriteText(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}
static int MaxDiff(const Image& a, const Image& b) {
    int m = 0;
    for (std::size_t i = 0; i < a.Pixels().size(); ++i) m = std::max(m, std::abs(int(a.Pixels()[i]) - int(b.Pixels()[i])));
    return m;
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "okay_tex_import";
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir);

    // --- the block codec ---
    {
        CHECK(CompressedByteSize(0, 4, false) == 0);
        CHECK(CompressedByteSize(4, 4, false) == 8);
        CHECK(CompressedByteSize(5, 4, false) == 16);
        CHECK(CompressedByteSize(8, 8, true) == 64);

        Image none;
        CHECK(CompressRoundTrip(none) == 0);

        Image flat(8, 8);
        flat.Fill(Color(0.4f, 0.6f, 0.2f, 1.0f));
        const Image before = flat;
        CHECK(CompressRoundTrip(flat) == 32);
        CHECK(MaxDiff(before, flat) <= 8);          // only the 5:6:5 rounding
        CHECK(flat.GetPixel(3, 3).a == 1.0f);        // opaque stays opaque

        // A block with two colours keeps both (up to 5:6:5 rounding).
        Image two(4, 4);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) two.SetPixel(x, y, x < 2 ? Color(1, 0, 0, 1) : Color(0, 0, 1, 1));
        const Image twoBefore = two;
        CompressRoundTrip(two);
        CHECK(MaxDiff(twoBefore, two) <= 12);

        // A smooth ramp is changed (banded) but stays close.
        Image ramp(16, 16);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) ramp.SetPixel(x, y, Color(x / 15.0f, y / 15.0f, 0.5f, 1));
        const Image rampBefore = ramp;
        CompressRoundTrip(ramp);
        const int d = MaxDiff(rampBefore, ramp);
        CHECK(d > 0 && d < 80);

        // Alpha: a picture with transparency keeps its alpha range and gets 16 bytes per block.
        Image al(4, 4);
        for (int i = 0; i < 16; ++i) al.SetPixel(i & 3, i >> 2, Color(1, 1, 1, i / 15.0f));
        CHECK(ImageHasAlpha(al));
        CHECK(CompressRoundTrip(al) == 16);
        CHECK(al.Pixels()[3] == 0);
        CHECK(al.Pixels()[(15) * 4 + 3] == 255);

        // A picture that is not a multiple of 4 wide keeps its size.
        Image odd(7, 5);
        odd.Fill(Color(0.2f, 0.2f, 0.2f, 1));
        CompressRoundTrip(odd);
        CHECK(odd.Width() == 7 && odd.Height() == 5 && odd.Valid());
    }

    // --- reading the settings ---
    {
        const std::string none = (dir / "none.png").string();
        TextureSampling t = TextureImportSampling(none);
        CHECK(t.srgb && !t.srgbSet && !t.compress);   // no sidecar: the long-standing behaviour

        const std::string a = (dir / "a.png").string();
        WriteText(a + ".okaymeta", "importer texture\nset srgb 0\nset compress 1\n");
        t = TextureImportSampling(a);
        CHECK(!t.srgb && t.srgbSet && t.compress);

        const std::string b = (dir / "b.png").string();
        WriteText(b + ".okaymeta", "importer texture\nset srgb 1\nset compress 0\n");
        t = TextureImportSampling(b);
        CHECK(t.srgb && t.srgbSet && !t.compress);

        const std::string c = (dir / "c.png").string();      // the earliest layout: `linear` is the inverse
        WriteText(c + ".okaymeta", "importer texture\nset linear 1\n");
        t = TextureImportSampling(c);
        CHECK(!t.srgb && t.srgbSet);

        const std::string e = (dir / "e.png").string();      // a model's meta is not a texture's
        WriteText(e + ".okaymeta", "importer model\nset compress 1\nset srgb 0\n");
        t = TextureImportSampling(e);
        CHECK(t.srgb && !t.srgbSet && !t.compress);
    }

    // --- ApplyTextureImportSettings: compress ---
    {
        const std::string p = (dir / "ramp.png").string();
        Image ramp(16, 16);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) ramp.SetPixel(x, y, Color(x / 15.0f, y / 15.0f, 0.5f, 1));
        Image plain = ramp;
        ApplyTextureImportSettings(p, plain);                // no sidecar: untouched
        CHECK(MaxDiff(plain, ramp) == 0);
        WriteText(p + ".okaymeta", "importer texture\nset compress 1\n");
        Image comp = ramp;
        ApplyTextureImportSettings(p, comp);
        CHECK(MaxDiff(comp, ramp) > 0);
        WriteText(p + ".okaymeta", "importer texture\nset compress 0\n");
        Image off = ramp;
        ApplyTextureImportSettings(p, off);
        CHECK(MaxDiff(off, ramp) == 0);
    }

    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
