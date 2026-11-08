#include "test_framework.hpp"
#include "okay/Core/Log.hpp"
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

static std::string ReadAll(const std::string& p) {
    std::ifstream f(p, std::ios::binary); std::ostringstream s; s << f.rdbuf(); return s.str();
}

// Captures stdout and stderr while alive.
struct Capture {
    std::ostringstream o, e;
    std::streambuf *so, *se;
    Capture() : so(std::cout.rdbuf(o.rdbuf())), se(std::cerr.rdbuf(e.rdbuf())) {}
    ~Capture() { std::cout.rdbuf(so); std::cerr.rdbuf(se); }
};

struct Probe {};
static int g_formatted = 0;
static std::ostream& operator<<(std::ostream& o, const Probe&) { ++g_formatted; return o; }

static void Reset() {
    Log::minLevel = Log::Level::Trace;
    Log::sink = nullptr;
    Log::console = true;
    Log::showTimestamps = false;
    Log::ClearCategoryLevels();
    Log::CloseFile();
}

int main() {
    RUN_SUITE("log");
    Reset();
    const std::string dir = (fs::temp_directory_path() / "okay_test_log").string();
    fs::remove_all(dir); fs::create_directories(dir);

    // Default output is unchanged: tag format, warn/error on stderr, rest on stdout.
    {
        Capture c;
        Log::Info("a=", 1);
        Log::Trace("t");
        Log::Warning("w");
        Log::Error("e");
        CHECK(c.o.str() == "[okay][INFO ] a=1\n[okay][TRACE] t\n");
        CHECK(c.e.str() == "[okay][WARN ] w\n[okay][ERROR] e\n");
    }

    // Level filter + sink gets the bare message.
    {
        std::vector<std::string> got;
        Log::sink = [&](Log::Level, const std::string& m) { got.push_back(m); };
        Log::minLevel = Log::Level::Warning;
        Capture c;
        Log::Info("no"); Log::Warning("yes"); Log::Error("yes2");
        CHECK(got.size() == 2 && got[0] == "yes" && got[1] == "yes2");
        CHECK(c.o.str().empty());
        Reset();
    }

    // Categories: shown in the line; override beats global both ways.
    {
        std::vector<std::string> got;
        Log::sink = [&](Log::Level, const std::string& m) { got.push_back(m); };
        Log::Category net("net"), phys("physics");
        {
            Capture c;
            net.Info("hello");
            CHECK(c.o.str() == "[okay][INFO ][net] hello\n");
        }
        Log::minLevel = Log::Level::Error;
        Log::SetCategoryLevel("net", Log::Level::Trace);      // more verbose than global
        Log::SetCategoryLevel("physics", Log::Level::Error);
        got.clear();
        Capture c;
        net.Trace("n1"); phys.Warning("p-no"); phys.Error("p-yes"); Log::Info("plain-no");
        CHECK(got.size() == 2 && got[0] == "n1" && got[1] == "p-yes");
        Log::minLevel = Log::Level::Trace;
        Log::SetCategoryLevel("net", Log::Level::Warning);    // quieter than global
        got.clear();
        net.Info("x"); net.Warning("y");
        CHECK(got.size() == 1 && got[0] == "y");
        CHECK(Log::EffectiveLevel("net") == Log::Level::Warning);
        CHECK(Log::EffectiveLevel("other") == Log::Level::Trace);
        Reset();
    }

    // Filtered messages are never formatted: the argument's operator<< is not called.
    {
        Log::minLevel = Log::Level::Error;
        Log::Info(Probe{});
        CHECK(g_formatted == 0);
        Log::minLevel = Log::Level::Trace;
        Log::console = false;
        Log::Info(Probe{});
        CHECK(g_formatted == 1);
        Reset();
    }

    // Concurrency: 8 threads x 1000 lines, every line whole.
    {
        std::vector<std::string> got;
        Log::sink = [&](Log::Level, const std::string& m) { got.push_back(m); };
        Log::console = false;
        std::vector<std::thread> th;
        for (int t = 0; t < 8; ++t)
            th.emplace_back([t] { for (int i = 0; i < 1000; ++i) Log::Info("thread-", t, "-line-", i, "-end"); });
        for (auto& x : th) x.join();
        CHECK(got.size() == 8000);
        int bad = 0;
        for (auto& m : got) if (m.rfind("thread-", 0) != 0 || m.size() < 6 || m.compare(m.size() - 4, 4, "-end") != 0) ++bad;
        CHECK(bad == 0);
        Reset();
    }

    // Concurrent file writes also stay whole lines.
    {
        std::string p = dir + "/conc.log";
        CHECK(Log::OpenFile(p, 100u * 1024u * 1024u));
        Log::console = false;
        std::vector<std::thread> th;
        for (int t = 0; t < 8; ++t)
            th.emplace_back([t] { for (int i = 0; i < 500; ++i) Log::Info("T", t, "-", i, "-end"); });
        for (auto& x : th) x.join();
        Log::CloseFile();
        std::istringstream in(ReadAll(p)); std::string ln; int lines = 0, bad = 0;
        while (std::getline(in, ln)) { ++lines; if (ln.rfind("[okay][INFO ] T", 0) != 0 || ln.substr(ln.size() - 4) != "-end") ++bad; }
        CHECK(lines == 4000 && bad == 0);
        Reset();
    }

    // File sink writes; flushed on warning without closing.
    {
        std::string p = dir + "/a.log";
        CHECK(Log::OpenFile(p));
        CHECK(Log::FileOpen());
        Log::console = false;
        Log::Info("to file");
        Log::Warning("flush me");
        CHECK(ReadAll(p).find("[okay][WARN ] flush me\n") != std::string::npos);   // flushed while still open
        CHECK(ReadAll(p).find("[okay][INFO ] to file\n") != std::string::npos);
        Log::CloseFile();
        CHECK(!Log::FileOpen());
        Reset();
    }

    // Rotation with a tiny limit: path.1 appears, old data moves, keep is honoured.
    {
        std::string p = dir + "/rot.log";
        CHECK(Log::OpenFile(p, 200, 2));
        Log::console = false;
        for (int i = 0; i < 60; ++i) Log::Info("rotating line number ", i);
        Log::CloseFile();
        CHECK(fs::exists(p));
        CHECK(fs::exists(p + ".1"));
        CHECK(fs::exists(p + ".2"));
        CHECK(!fs::exists(p + ".3"));
        CHECK(fs::file_size(p) <= 200 && fs::file_size(p + ".1") <= 200);
        CHECK(ReadAll(p).find("number 59") != std::string::npos);   // newest is in the live file
        Reset();
    }

    // OpenFile on an unwritable path fails cleanly and file logging stays off.
    {
        CHECK(!Log::OpenFile(dir + "/no/such/dir/x.log"));
        CHECK(!Log::FileOpen());
        Capture c;
        Log::Info("still fine");
        CHECK(c.o.str() == "[okay][INFO ] still fine\n");
        Reset();
    }

    // Works with console, sink and file together.
    {
        std::string p = dir + "/all.log";
        int sunk = 0;
        Log::sink = [&](Log::Level, const std::string&) { ++sunk; };
        CHECK(Log::OpenFile(p));
        Capture c;
        Log::Info("both");
        CHECK(sunk == 1 && c.o.str() == "[okay][INFO ] both\n");
        Log::CloseFile();
        CHECK(ReadAll(p) == "[okay][INFO ] both\n");
        Reset();
    }

    // Configure: levels, categories, case, whitespace, malformed input.
    {
        CHECK(Log::Configure("warn, NET = trace ,physics=error") == 3);
        CHECK(Log::minLevel == Log::Level::Warning);
        CHECK(Log::EffectiveLevel("NET") == Log::Level::Trace);
        CHECK(Log::EffectiveLevel("physics") == Log::Level::Error);
        Reset();
        CHECK(Log::Configure("") == 0);
        CHECK(Log::Configure(",,,=,x=,=info,bogus,a=bogus,==") == 0);
        CHECK(Log::minLevel == Log::Level::Trace);
        CHECK(Log::Configure("loud,info,net=nope,gfx=warning") == 2);
        CHECK(Log::minLevel == Log::Level::Info);
        CHECK(Log::EffectiveLevel("gfx") == Log::Level::Warning);
        CHECK(Log::EffectiveLevel("net") == Log::Level::Info);
        Reset();
    }

    // Timestamps: HH:MM:SS.mmm prefix when on, none when off.
    {
        Log::showTimestamps = true;
        std::string line;
        { Capture c; Log::Info("ts"); line = c.o.str(); }
        bool ok = line.size() > 13 && line[2] == ':' && line[5] == ':' && line[8] == '.' && line[12] == ' ' &&
                  line.compare(13, 17, "[okay][INFO ] ts\n") == 0;
        for (int i : {0, 1, 3, 4, 6, 7, 9, 10, 11}) ok = ok && line[i] >= '0' && line[i] <= '9';
        CHECK(ok);
        Log::showTimestamps = false;
        { Capture c; Log::Info("ts"); line = c.o.str(); }
        CHECK(line == "[okay][INFO ] ts\n");
        Reset();
    }

    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
