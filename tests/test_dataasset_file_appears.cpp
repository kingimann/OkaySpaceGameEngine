// A data asset a script fills in before the file exists must pick the file up once it appears.
// Area 1.14: an entry Set() after a failed Cached() kept the script values and never read a file created later.
#include "test_framework.hpp"
#include "okay/Core/DataAsset.hpp"
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

static void PutFile(const std::string& path, const std::string& bytes) {
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    o << bytes;
}

int main() {
    RUN_SUITE("dataasset_file_appears");
    const fs::path dir = fs::temp_directory_path() / ("okay_da_appear_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    const fs::path old = fs::current_path();
    fs::current_path(dir);
    DataAsset::InvalidateAll();

    DataAsset& scratch = DataAsset::Cached("stats.okaydata");
    CHECK(!scratch.Has("hp"));
    scratch.Set("hp", "10");
    scratch.Set("name", "script");
    CHECK(DataAsset::Cached("stats.okaydata").GetNumber("hp") == 10.0);   // still no file: the script's values stay

    PutFile("stats.okaydata", "hp = 40\nname = file\n");
    DataAsset& loaded = DataAsset::Cached("stats.okaydata");
    CHECK(&loaded == &scratch);                                            // the same cached object
    CHECK(loaded.GetNumber("hp") == 40.0);                                 // the file that appeared wins
    CHECK(loaded.GetString("name") == "file");

    // A second call does not read the file again: an edit on disk waits for Reload.
    PutFile("stats.okaydata", "hp = 99\n");
    CHECK(DataAsset::Cached("stats.okaydata").GetNumber("hp") == 40.0);

    DataAsset::InvalidateAll();
    fs::current_path(old);
    std::error_code ec;
    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
