// Cached data assets stay put until something asks, and ReloadIfChanged re-reads only when the
// file's size or modification time moved. Area 1.14: there was no file-time check at all.
#include "test_framework.hpp"
#include "okay/Core/DataAsset.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

static void PutFile(const fs::path& path, const std::string& bytes) {
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    o << bytes;
}

static void BumpTime(const fs::path& path, int secondsAhead) {
    std::error_code ec;
    // An absolute offset from now, not "plus a bit on the current stamp": two edits in the same
    // second would otherwise land on the same time and the check would correctly see no change.
    fs::last_write_time(path, fs::file_time_type::clock::now() + std::chrono::seconds(secondsAhead), ec);
}

int main() {
    RUN_SUITE("dataasset_reload_changed");
    const fs::path dir = fs::temp_directory_path() / ("okay_da_stamp_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    const fs::path old = fs::current_path();
    fs::current_path(dir);
    DataAsset::InvalidateAll();

    PutFile("unit.okaydata", "x = 1\n");
    DataAsset& held = DataAsset::Cached("unit.okaydata");
    CHECK(held.GetNumber("x") == 1.0);
    CHECK(!DataAsset::ReloadIfChanged("unit.okaydata"));          // nothing on disk moved
    CHECK(held.GetNumber("x") == 1.0);

    PutFile("unit.okaydata", "x = 2\ny = 3\n");
    BumpTime("unit.okaydata", 30);
    CHECK(DataAsset::Cached("unit.okaydata").GetNumber("x") == 1.0);   // Cached itself still does not follow the disk
    CHECK(DataAsset::ReloadIfChanged("unit.okaydata"));
    CHECK(&DataAsset::Cached("unit.okaydata") == &held);
    CHECK(held.GetNumber("x") == 2.0);
    CHECK(held.GetNumber("y") == 3.0);
    CHECK(!DataAsset::ReloadIfChanged("unit.okaydata"));          // the new stamp is remembered

    // Same number of bytes: only the modification time says the file changed.
    PutFile("unit.okaydata", "x = 8\ny = 4\n");
    BumpTime("unit.okaydata", 90);
    CHECK(DataAsset::ReloadIfChanged("unit.okaydata"));
    CHECK(held.GetNumber("x") == 8.0);
    CHECK(held.GetNumber("y") == 4.0);

    // A script's values in an asset that has no file are not wiped by the check.
    DataAsset& scratch = DataAsset::Cached("scratch.okaydata");
    scratch.Set("score", "10");
    CHECK(!DataAsset::ReloadIfChanged("scratch.okaydata"));
    CHECK(scratch.GetNumber("score") == 10.0);
    CHECK(!DataAsset::ReloadIfChanged("never_existed.okaydata"));

    DataAsset& edit = DataAsset::Cached("edit.okaydata");
    edit.Set("a", "1");
    CHECK(edit.Save("edit.okaydata"));
    edit.Set("a", "5");
    CHECK(DataAsset::Cached("edit.okaydata").GetNumber("a") == 5.0);
    CHECK(!DataAsset::ReloadIfChanged("edit.okaydata"));          // its own save is not a change
    CHECK(edit.GetNumber("a") == 5.0);

    // Saving a cached asset under another name does not mark that entry as loaded from this file.
    DataAsset& other = DataAsset::Cached("other.okaydata");
    other.Set("b", "2");
    CHECK(other.Save("copy.okaydata"));
    PutFile("other.okaydata", "b = 7\n");
    CHECK(DataAsset::Cached("other.okaydata").GetNumber("b") == 7.0);

    DataAsset::InvalidateAll();
    fs::current_path(old);
    std::error_code ec;
    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
