// Linux tests for the Xbox UWP "next" pieces: the log file (XboxLog.hpp), the log sink of the backend and where the log folder is.
// Pure C++, no engine link. The Windows glue that uses them (app/main.cpp, app/engine_bridge.cpp) is uncompiled: UNVERIFIED on Windows.
#include "XboxLog.hpp"
#include "XboxPaths.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)
namespace fs = std::filesystem;
static std::string Slurp(const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

int main(int argc, char** argv) {
    const fs::path tmp = fs::path(argc > 1 ? argv[1] : ".") / "xbox_next_tmp";
    std::error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp);
    using okay::xbox::FileLog;
    // 1. opens (making the folder), writes stamped lines, flushed at once
    {
        FileLog log; CHECK(log.Open((tmp / "a/b/logs").generic_string()) && log.IsOpen());
        CHECK(log.Path() == (tmp / "a/b/logs").generic_string() + "/okayspace.log");
        log.Line("hello");
        const std::string text = Slurp(log.Path());   // read while the log is still open: every line is already on disk
        CHECK(text.size() > 8 && text[0] == '[' && text.find("] hello\n") != std::string::npos);
        log.Line("two\r\nlines");
        const std::string t2 = Slurp(log.Path());
        CHECK(t2.find("two\n") != std::string::npos && t2.find("\r") == std::string::npos && t2.find("\n            lines\n") != std::string::npos);
    }
    // 2. a second Open appends (the file of the last run is kept)
    {
        FileLog log; CHECK(log.Open((tmp / "a/b/logs").generic_string())); log.Line("second run");
        const std::string t = Slurp(log.Path()); CHECK(t.find("hello") != std::string::npos && t.find("second run") != std::string::npos);
    }
    // 3. rotation: the file never grows far past maxBytes, the previous one is kept as .1.log, older text is dropped
    {
        const std::string dir = (tmp / "rot").generic_string(); FileLog log; CHECK(log.Open(dir, "game", 1024));
        for (int i = 0; i < 100; ++i) log.Line("line number " + std::to_string(i) + " padding padding padding padding");
        CHECK(fs::exists(dir + "/game.1.log") && fs::file_size(dir + "/game.log") < 2048 && fs::file_size(dir + "/game.1.log") < 2048);
        CHECK(Slurp(dir + "/game.log").find("line number 99 ") != std::string::npos);   // the newest line is in the current file
        CHECK(Slurp(dir + "/game.1.log").find("line number 0 ") == std::string::npos);  // the oldest were dropped
        CHECK(!fs::exists(dir + "/game.2.log"));
    }
    // 4. a folder that cannot be made: Open says false, Line is harmless, the error says why
    {
        const fs::path file = tmp / "afile"; { std::ofstream f(file); f << "x"; }
        FileLog log; CHECK(!log.Open((file / "sub").generic_string())); CHECK(!log.IsOpen() && !log.Error().empty()); log.Line("dropped"); log.Close();
    }
    // 5. a line before Open is dropped, not a crash
    { FileLog log; log.Line("nobody listens"); CHECK(!log.IsOpen() && log.Path().empty()); }
    // 6. the log folder for the Device Portal: LocalState\logs, writable, and the install folder is never the place
    {
        using namespace okay::xbox; PathRoots r; r.install = "C:/WindowsApps/pkg/"; r.local = "C:/Users/x/LocalState"; r.temp = "C:/Users/x/Temp";
        PathResult p = Resolve(r, PathArea::Logs, ""); CHECK(p.ok && p.writable && p.path.find("LocalState") != std::string::npos && p.path.find("WindowsApps") == std::string::npos);
    }
    fs::remove_all(tmp, ec);
    if (g_fail) { std::printf("%d FAILED\n", g_fail); return 1; }
    std::printf("xbox_uwp_next: all passed\n");
    return 0;
}
