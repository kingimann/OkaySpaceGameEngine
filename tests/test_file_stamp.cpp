#include "test_framework.hpp"
#include "okay/Core/FileStamp.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

// One file size + time helper (okay/Core/FileStamp.hpp, #422 audit item 26). The three copies it replaced are kept
// below as they were; each caller now reads StampOf and applies its old rule for a missing piece (the same lines as in
// AudioDecode.cpp, NavMeshSurface.cpp and AssetManager.cpp). Checked on a file, an empty file, a folder and a missing path.
using namespace okay;
namespace fs = std::filesystem;
namespace {

// ---- the old copies ---------------------------------------------------------------------------------------------------
void OldAudio(const std::string& path, std::uintmax_t& size, std::int64_t& time) {
    std::error_code ec;
    const auto fp = fs::u8path(path);
    size = fs::file_size(fp, ec); if (ec) size = 0;
    const auto t = fs::last_write_time(fp, ec);
    time = ec ? 0 : (std::int64_t)t.time_since_epoch().count();
}
bool OldNav(const std::string& path, std::uintmax_t& size, std::int64_t& time) {
    std::error_code ec;
    const auto fp = fs::u8path(path);
    size = fs::file_size(fp, ec);
    if (ec) return false;
    const auto t = fs::last_write_time(fp, ec);
    if (ec) return false;
    time = (std::int64_t)t.time_since_epoch().count();
    return true;
}
bool OldAsset(const std::string& path, std::int64_t& stamp, std::int64_t& size) {
    std::error_code ec;
    auto t = fs::last_write_time(fs::u8path(path), ec);
    if (ec) return false;
    auto sz = fs::file_size(fs::u8path(path), ec);
    stamp = static_cast<std::int64_t>(t.time_since_epoch().count());
    size = ec ? -1 : static_cast<std::int64_t>(sz);
    return true;
}
// ---- the callers' rules on top of StampOf (as written in the three sources) -----------------------------------------
void NewAudio(const std::string& path, std::uintmax_t& size, std::int64_t& time) {
    const FileStat s = StampOf(path);
    size = s.size; time = s.time;
}
bool NewNav(const std::string& path, std::uintmax_t& size, std::int64_t& time) {
    const FileStat s = StampOf(path);
    if (!s.sizeOk || !s.timeOk) return false;
    size = s.size; time = s.time;
    return true;
}
bool NewAsset(const std::string& path, std::int64_t& stamp, std::int64_t& size) {
    const FileStat s = StampOf(path);
    if (!s.timeOk) return false;
    stamp = s.time;
    size = s.sizeOk ? (std::int64_t)s.size : -1;
    return true;
}

} // namespace

int main() {
    RUN_SUITE("file_stamp");
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / "okay_file_stamp_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "folder", ec);
    { std::ofstream(dir / "a.bin", std::ios::binary) << std::string(1234, 'x'); }
    { std::ofstream(dir / "empty.txt", std::ios::binary); }
    const std::string paths[] = {(dir / "a.bin").u8string(), (dir / "empty.txt").u8string(), (dir / "folder").u8string(),
                                 (dir / "missing.wav").u8string(), ""};
    int same = 0, total = 0;
    for (const std::string& p : paths) {
        {   ++total;
            std::uintmax_t s1 = 7, s2 = 7; std::int64_t t1 = 7, t2 = 7;
            OldAudio(p, s1, t1); NewAudio(p, s2, t2);
            same += s1 == s2 && t1 == t2;
        }
        {   ++total;
            std::uintmax_t s1 = 7, s2 = 7; std::int64_t t1 = 7, t2 = 7;
            const bool r1 = OldNav(p, s1, t1), r2 = NewNav(p, s2, t2);
            same += r1 == r2 && (!r1 || (s1 == s2 && t1 == t2));   // its callers read the values only on success
        }
        {   ++total;
            std::int64_t s1 = 7, s2 = 7, t1 = 7, t2 = 7;
            const bool r1 = OldAsset(p, t1, s1), r2 = NewAsset(p, t2, s2);
            same += r1 == r2 && s1 == s2 && t1 == t2;
        }
    }
    std::printf("  same answers: %d of %d\n", same, total);
    CHECK(same == total);
    const FileStat f = StampOf((dir / "a.bin").u8string());
    CHECK(f.sizeOk && f.timeOk && f.size == 1234u);
    const FileStat m = StampOf((dir / "missing.wav").u8string());
    CHECK(!m.sizeOk && !m.timeOk && m.size == 0 && m.time == 0);
    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
