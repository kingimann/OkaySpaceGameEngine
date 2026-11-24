// The asset database hears a new file from the operating system's directory watch.
#include "test_framework.hpp"
#include "okay/Core/AssetDatabase.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace okay;
namespace fs = std::filesystem;

int main() {
    RUN_SUITE("dir_watch");
    const fs::path root = fs::temp_directory_path() / "okay_dir_watch";
    fs::remove_all(root);
    fs::create_directories(root / "art");
    {
        std::ofstream o(root / "art" / "a.txt");
        o << "a";
    }
    AssetDatabase db;
    ScanOptions opt;
    opt.logWarnings = false;
    opt.createMetas = false;
    db.Scan(root.string(), opt);
    std::string err;
    CHECK(db.StartWatch(&err));
    CHECK(err.empty());
    {
        std::ofstream o(root / "art" / "ping.txt");
        o << "ping";
    }
    bool saw = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && !saw) {
        for (const std::string& p : db.PollWatch())
            if (p.find("ping.txt") != std::string::npos) saw = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(saw);
    db.StopWatch();
    fs::remove_all(root);
    TEST_MAIN_RESULT();
}
