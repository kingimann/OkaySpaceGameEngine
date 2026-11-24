// .okaydata gets a real thumbnail, and two preview sizes do not share one cached picture.
#include "test_framework.hpp"
#include "okay/Core/ThumbnailCache.hpp"
#include "okay/Core/AssetDatabase.hpp"
#include <filesystem>
#include <fstream>

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

    small.SetSize(96);
    const auto c = small.Get(id, 1, file.string(), AssetType::Data);
    CHECK(!c.placeholder && c.image && c.image->Width() == 96);
    CHECK(c.image->Width() != a.image->Width());
    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
