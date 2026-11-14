// Thumbnails for fonts and data assets (Core/ThumbnailCache): a sample string drawn with our own font
// code into the tile image, and a generated page tile. No window or GPU.
#include "test_framework.hpp"
#include "okay/Core/ThumbnailCache.hpp"
#include "okay/Graphics/TtfFont.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>

using namespace okay;
namespace fs = std::filesystem;

static std::string FontPath() { return std::string(OKAY_SOURCE_DIR) + "/assets/fonts/DejaVuSans.ttf"; }

static int Bright(const Image& img) {      // pixels clearly lighter than the dark card
    int n = 0;
    for (int y = 0; y < img.Height(); ++y)
        for (int x = 0; x < img.Width(); ++x) {
            const Color c = img.GetPixel(x, y);
            if (c.r > 0.6f && c.g > 0.6f && c.b > 0.6f) ++n;
        }
    return n;
}

static bool SameImage(const Image& a, const Image& b) {
    return a.Width() == b.Width() && a.Height() == b.Height() && a.Pixels() == b.Pixels();
}

static void Write(const fs::path& p, const std::string& bytes) {
    std::ofstream f(p, std::ios::binary);
    f << bytes;
}

int main() {
    const fs::path dir = fs::temp_directory_path() / ("okay_thumb_fd_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);

    {
        RUN_SUITE("font thumbnail");
        TtfFont font;
        CHECK(font.LoadFromFile(FontPath(), 64.0f));
        const Image img = RenderFontThumbnail(font, 128);
        CHECK(img.Valid() && img.Width() == 128 && img.Height() == 128);
        CHECK(Bright(img) > 150);                        // glyph pixels exist
        CHECK(img.GetPixel(2, 2).a > 0.99f);             // opaque tile
        // The big sample sits in the upper part, the sample line in the lower part: both have ink.
        int upper = 0, lower = 0;
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x) {
                const Color c = img.GetPixel(x, y);
                if (c.r > 0.45f && c.g > 0.45f) (y < 64 ? upper : lower)++;
            }
        CHECK(upper > 80 && lower > 15);
        CHECK(SameImage(img, RenderFontThumbnail(font, 128)));   // deterministic
        const Image small = RenderFontThumbnail(font, 32);
        CHECK(small.Valid() && small.Width() == 32 && Bright(small) > 5);
        CHECK(RenderFontThumbnail(font, 4).Width() == 16);       // tiny sizes are lifted
        TtfFont none;
        CHECK(!RenderFontThumbnail(none, 128).Valid());

        std::string err;
        const Image file = RenderFontThumbnailFile(FontPath(), 96, &err);
        CHECK(file.Valid() && file.Width() == 96 && err.empty());
        const Image missing = RenderFontThumbnailFile((dir / "nope.ttf").string(), 96, &err);
        CHECK(!missing.Valid() && !err.empty());
        Write(dir / "bad.ttf", "this is not a font at all");
        err.clear();
        CHECK(!RenderFontThumbnailFile((dir / "bad.ttf").string(), 96, &err).Valid() && !err.empty());
        Write(dir / "empty.otf", "");
        CHECK(!RenderFontThumbnailFile((dir / "empty.otf").string(), 96, nullptr).Valid());
        // A truncated real font must not crash.
        std::ifstream in(FontPath(), std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        Write(dir / "cut.ttf", bytes.substr(0, bytes.size() / 3));
        (void)RenderFontThumbnailFile((dir / "cut.ttf").string(), 96, nullptr);
        // Speed: a tile is made well inside a frame budget on a normal machine (generous bound).
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 5; ++i) (void)RenderFontThumbnailFile(FontPath(), 128, nullptr);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 5.0;
        std::cout << "  font tile: " << ms << " ms each\n";
        CHECK(ms < 500.0);
    }

    {
        RUN_SUITE("data thumbnail");
        Write(dir / "items.okayitems", "okayitems 1\nitem sword\n  damage 10\n\nitem shield\n  armor 5\n");
        Write(dir / "config.json", "{\n  \"a\": 1,\n  \"b\": [1, 2, 3]\n}\n");
        std::string bin(512, '\0');
        for (int i = 0; i < 512; ++i) bin[(std::size_t)i] = (char)(i * 7);
        Write(dir / "blob.bin", bin);
        Write(dir / "empty.txt", "");
        std::string err;
        const Image text = MakeDataThumbnail((dir / "items.okayitems").string(), 128, &err);
        CHECK(text.Valid() && text.Width() == 128 && text.Height() == 128 && err.empty());
        CHECK(Bright(text) > 1000);                                     // the page
        CHECK(SameImage(text, MakeDataThumbnail((dir / "items.okayitems").string(), 128)));   // deterministic
        const Image json = MakeDataThumbnail((dir / "config.json").string(), 128);
        CHECK(json.Valid() && !SameImage(json, text));                  // another file, another tile
        const Image blob = MakeDataThumbnail((dir / "blob.bin").string(), 128);
        CHECK(blob.Valid() && !SameImage(blob, text) && !SameImage(blob, json));
        CHECK(MakeDataThumbnail((dir / "empty.txt").string(), 128).Valid());
        CHECK(MakeDataThumbnail((dir / "items.okayitems").string(), 8).Width() == 16);
        CHECK(!MakeDataThumbnail((dir / "missing.json").string(), 128, &err).Valid() && !err.empty());
    }

    {
        RUN_SUITE("generators in the cache");
        FontThumbnailGenerator fg;
        DataThumbnailGenerator dg;
        CHECK(fg.Handles("ttf") && fg.Handles("otf") && !fg.Handles("png"));
        CHECK(dg.Handles("json") && dg.Handles("okayloc") && dg.Handles("bin") && !dg.Handles("png") && !dg.Handles("ttf"));
        fs::create_directories(dir / "p");
        fs::copy_file(FontPath(), dir / "p" / "face.ttf");
        Write(dir / "p" / "strings.json", "{\"a\": \"b\"}");
        Write(dir / "p" / "bad.ttf", "junk");
        AssetDatabase db;
        db.Scan((dir / "p").string());
        ThumbnailCache::Options o;
        o.size = 64;
        ThumbnailCache cache(o);
        const auto font = cache.Get(db, db.PathToGuid("face.ttf"));
        CHECK(font.source == ThumbnailCache::Source::Generated && !font.placeholder && !font.failed);
        CHECK(font.image && font.image->Width() == 64 && Bright(*font.image) > 20);
        const auto data = cache.Get(db, db.PathToGuid("strings.json"));
        CHECK(data.source == ThumbnailCache::Source::Generated && !data.placeholder && data.image && data.image->Width() == 64);
        const auto bad = cache.Get(db, db.PathToGuid("bad.ttf"));
        CHECK(bad.failed && bad.placeholder && bad.image && bad.image->Valid());   // a red tile, not retried
        const auto gen = cache.GetStats().generated;
        (void)cache.Get(db, db.PathToGuid("bad.ttf"));
        CHECK(cache.GetStats().generated == gen);
        // The generators can be turned off.
        ThumbnailCache::Options off = o;
        off.fontGenerator = false;
        off.dataGenerator = false;
        ThumbnailCache plain(off);
        CHECK(plain.Get(db, db.PathToGuid("face.ttf")).placeholder);
        CHECK(plain.Get(db, db.PathToGuid("strings.json")).placeholder);
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
