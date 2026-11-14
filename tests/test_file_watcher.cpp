// FileWatcher: the operating system watcher (inotify on Linux, ReadDirectoryChangesW on Windows,
// only the Linux one is run in CI so far) and the polling fallback.
#include "test_framework.hpp"
#include "okay/Core/FileWatcher.hpp"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace okay;
namespace fs = std::filesystem;

static void WriteText(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}

// Wait up to a second for a Changed answer; collects the paths seen.
static bool WaitChanged(FileWatcher& w, std::vector<std::string>* paths = nullptr) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    do {
        if (w.Take(paths) == WatchState::Changed) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < end);
    return false;
}
static bool StaysQuiet(FileWatcher& w) {
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    return w.Take() == WatchState::Quiet;
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "okay_file_watcher";
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir / "sub");

    // --- Polling fallback ---
    {
        auto p = CreatePollingFileWatcher();
        CHECK(p->Start(dir.string()));
        CHECK(!p->Active());
        CHECK(std::string(p->Name()) == "polling");
        CHECK(p->Take() == WatchState::Unknown);   // the caller keeps scanning on its timer
        std::string why;
        auto m = MakeFileWatcher((dir / "missing").string(), &why);
        CHECK(m && !m->Active());                  // a folder that is not there: polling
        CHECK(m->Take() == WatchState::Unknown);
        CHECK(!why.empty());
    }

    auto native = CreateNativeFileWatcher();
    if (!native) {
        std::printf("no native watcher on this platform: only the fallback was tested\n");
        fs::remove_all(dir, ec);
        TEST_MAIN_RESULT();
    }

    // --- Native watcher ---
    {
        std::string err;
        CHECK(!native->Start((dir / "missing").string(), &err));
        CHECK(!err.empty());
        CHECK(!native->Active());
        CHECK(native->Take() == WatchState::Unknown);   // not started

        CHECK(native->Start(dir.string(), &err));
        CHECK(native->Active());
        CHECK(StaysQuiet(*native));

        // A new file.
        WriteText(dir / "a.txt", "one");
        std::vector<std::string> paths;
        CHECK(WaitChanged(*native, &paths));
        bool sawA = false;
        for (auto& s : paths) if (s.size() >= 5 && s.compare(s.size() - 5, 5, "a.txt") == 0) sawA = true;
        CHECK(sawA);
        CHECK(StaysQuiet(*native));                     // read once: cleared

        // A write to an existing file.
        WriteText(dir / "a.txt", "two");
        CHECK(WaitChanged(*native));
        CHECK(StaysQuiet(*native));

        // A file in a folder that existed at start.
        WriteText(dir / "sub" / "b.txt", "x");
        CHECK(WaitChanged(*native));
        CHECK(StaysQuiet(*native));

        // A folder made later is watched too.
        fs::create_directories(dir / "later" / "deep");
        CHECK(WaitChanged(*native));
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        native->Take();
        WriteText(dir / "later" / "deep" / "c.txt", "x");
        CHECK(WaitChanged(*native));
        CHECK(StaysQuiet(*native));

        // Rename and delete.
        fs::rename(dir / "a.txt", dir / "a2.txt");
        CHECK(WaitChanged(*native));
        CHECK(StaysQuiet(*native));
        fs::remove(dir / "a2.txt");
        CHECK(WaitChanged(*native));
        CHECK(StaysQuiet(*native));

        // A folder moved in from outside is watched.
        const fs::path outside = fs::temp_directory_path() / "okay_file_watcher_outside";
        fs::remove_all(outside, ec); fs::create_directories(outside / "moved");
        fs::rename(outside / "moved", dir / "moved");
        CHECK(WaitChanged(*native));
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        native->Take();
        WriteText(dir / "moved" / "d.txt", "x");
        CHECK(WaitChanged(*native));
        fs::remove_all(outside, ec);

        // Stop, then start again on the same object.
        native->Stop();
        CHECK(!native->Active());
        WriteText(dir / "e.txt", "x");
        CHECK(native->Take() == WatchState::Unknown);
        CHECK(native->Start(dir.string()));
        CHECK(StaysQuiet(*native));
        WriteText(dir / "e.txt", "y");
        CHECK(WaitChanged(*native));
    }

    // --- MakeFileWatcher picks the native one ---
    {
        std::string why;
        auto w = MakeFileWatcher(dir.string(), &why);
        CHECK(w && w->Active());
        CHECK(why.empty());
    }

    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
