// The asset database hears a new file from the operating system's directory watch.
#include "test_framework.hpp"
#include "okay/Core/AssetDatabase.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>

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
    }
    CHECK(saw);
    db.StopWatch();
    CHECK(db.PollWatch().empty());

    // Lifetime: start and stop many times, stop twice, and destroy a database whose watch is
    // still running. The watch state is freed only after the read is cancelled (on Windows the
    // kernel writes into the OVERLAPPED and buffer until then).
    for (int i = 0; i < 20; ++i) {
        CHECK(db.StartWatch(&err));
        db.StopWatch();
        db.StopWatch();
    }
    for (int i = 0; i < 5; ++i) {
        AssetDatabase tmp;
        tmp.Scan(root.string(), opt);
        CHECK(tmp.StartWatch(&err));
    }
    // A watch started after all that still hears a change.
    CHECK(db.StartWatch(&err));
    {
        std::ofstream o(root / "art" / "pong.txt");
        o << "pong";
    }
    saw = false;
    const auto deadline2 = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline2 && !saw)
        for (const std::string& p : db.PollWatch())
            if (p.find("pong.txt") != std::string::npos) saw = true;
    CHECK(saw);
    db.StopWatch();

    // Watching needs a scanned folder.
    AssetDatabase empty;
    CHECK(!empty.StartWatch(&err));
    CHECK(!err.empty());
    fs::remove_all(root);
    TEST_MAIN_RESULT();
}
