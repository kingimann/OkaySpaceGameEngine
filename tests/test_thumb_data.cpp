// .okaydata gets a real thumbnail, and two preview sizes do not share one cached picture.
#include "test_framework.hpp"
#include "okay/Core/ThumbnailCache.hpp"
#include "okay/Core/AssetDatabase.hpp"
#include "okay/Graphics/Image.hpp"
#include <filesystem>
#include <fstream>

// The cyan field bars, as a box. right/bottom are the last bar pixel, or -1 when there is none.
static void BarExtent(const okay::Image& img, int& right, int& bottom) {
    right = -1;
    bottom = -1;
    for (int y = 0; y < img.Height(); ++y) {
        for (int x = 0; x < img.Width(); ++x) {
            const okay::Color c = img.GetPixel(x, y);
            if (c.g > 0.5f && c.b > 0.5f) {
                if (x > right) right = x;
                if (y > bottom) bottom = y;
            }
        }
    }
}

using namespace okay;
namespace fs = std::filesystem;

int main() {
    RUN_SUITE("thumb_data");
    const fs::path dir = fs::temp_directory_path() / "okay_thumb_data";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path file = dir / "stats.okaydata";
    {
        std::ofstream o(file);
        o << "# okaydata 2\nname = Goblin\nhp = 30\nspeed = 3.5\n";
    }
    const Guid id = Guid::Generate();
    ThumbnailCache::Options smallOpt;
    smallOpt.size = 32;
    smallOpt.imageGenerator = false;
    smallOpt.audioGenerator = false;
    ThumbnailCache small(smallOpt);
    const auto a = small.Get(id, 1, file.string(), AssetType::Data);
    CHECK(!a.placeholder && !a.failed);
    CHECK(a.image && a.image->Width() == 32 && a.image->Height() == 32);

    ThumbnailCache::Options bigOpt = smallOpt;
    bigOpt.size = 96;
    ThumbnailCache big(bigOpt);
    const auto b = big.Get(id, 1, file.string(), AssetType::Data);
    CHECK(!b.placeholder && b.image && b.image->Width() == 96);

    // The bars have to grow with the tile and reach across it, not stay a small mark in the corner.
    int ar = -1, ab = -1, br = -1, bb = -1;
    BarExtent(*a.image, ar, ab);
    BarExtent(*b.image, br, bb);
    CHECK(ar * 3 > a.image->Width() * 2);
    CHECK(ab * 3 > a.image->Height() * 2);
    CHECK(br * 3 > b.image->Width() * 2);
    CHECK(bb * 3 > b.image->Height() * 2);
    CHECK(br > ar * 2);
    CHECK(bb > ab * 2);

    small.SetSize(96);
    const auto c = small.Get(id, 1, file.string(), AssetType::Data);
    CHECK(!c.placeholder && c.image && c.image->Width() == 96);
    CHECK(c.image->Width() != a.image->Width());

    // A font file gets its own picture (the file name), not the plain placeholder tile.
    const fs::path font = dir / "Body.ttf";
    { std::ofstream o(font); o << "not-a-font"; }
    ThumbnailCache::Options fontOpt = smallOpt;
    fontOpt.size = 64;
    ThumbnailCache fonts(fontOpt);
    const auto f = fonts.Get(Guid::Generate(), 3, font.string(), AssetType::Font);
    CHECK(!f.placeholder && !f.failed && f.image && f.image->Width() == 64 && f.image->Height() == 64);
    int ink = 0;
    for (int y = 0; y < f.image->Height(); ++y)
        for (int x = 0; x < f.image->Width(); ++x) {
            const Color px = f.image->GetPixel(x, y);
            if (px.r > 0.8f && px.g > 0.8f) ++ink;
        }
    CHECK(ink > 8);
    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
