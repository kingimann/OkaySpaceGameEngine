// Core wave 6, 1.10 Logging and profiling (runtime side): audit fixes and additions.
//   - $OKAY_LOG never overrides a Configure call made by the program (it used to, when nothing was logged first).
//   - Log file: maxBytes 0 means "never rotate", Flush(), a failing disk is counted (and later lines try again).
//   - A sink that logs cannot recurse.
//   - Profiler: timeline accessors are bounds checked, scopes from other threads are ignored, one bad frame time does
//     not poison the averages, and the Chrome trace is valid JSON (and valid UTF-8) whatever the section names hold.
// The first thing main does is set $OKAY_LOG, before anything can have logged: run this file as its own process.
#include "test_framework.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Profiler.hpp"
#include "okay/Core/Utf8.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

static std::string ReadAll(const std::string& p) {
    std::ifstream f(p, std::ios::binary); std::ostringstream s; s << f.rdbuf(); return s.str();
}
static int CountLines(const std::string& text) {
    int n = 0; for (char c : text) if (c == '\n') ++n; return n;
}
static void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

int main() {
    // Must happen before the first log call of the process.
    SetEnv("OKAY_LOG", "error");

    RUN_SUITE("core_log_profiler");
    Log::console = false;

    // --- $OKAY_LOG is the starting point; a Configure call in the program wins over it ---
    {
        int got = 0;
        Log::sink = [&](Log::Level, const std::string&) { ++got; };
        Log::Configure("trace");               // the first Log call of the process
        Log::Trace("now visible");
        CHECK(got == 1);                       // the environment's "error" used to be applied after this and hide it
        Log::Configure("error");
        Log::Info("hidden again");
        CHECK(got == 1);
        Log::Configure("trace");
        Log::sink = nullptr;
    }

    const fs::path dir = fs::temp_directory_path() / "okay_core_log_profiler";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const std::string path = (dir / "game.log").string();

    // --- maxBytes 0: never rotate ---
    {
        CHECK(Log::OpenFile(path, 0, 2));
        for (int i = 0; i < 300; ++i) Log::Info("a line of some length ", i);
        Log::CloseFile();
        CHECK(CountLines(ReadAll(path)) == 300);        // it used to rotate before every line, keeping only the last
        CHECK(!fs::exists(path + ".1"));
        fs::remove(path);
    }

    // --- rotation still caps the disk use ---
    {
        CHECK(Log::OpenFile(path, 400, 2));
        for (int i = 0; i < 100; ++i) Log::Info("rotating line number ", i);
        Log::CloseFile();
        CHECK(fs::exists(path) && fs::exists(path + ".1") && fs::exists(path + ".2") && !fs::exists(path + ".3"));
        CHECK(fs::file_size(path) <= 400 && fs::file_size(path + ".1") <= 400 && fs::file_size(path + ".2") <= 400);
        const std::string last = ReadAll(path);
        CHECK(last.find("line number 99") != std::string::npos);
        for (const char* ext : {"", ".1", ".2"}) fs::remove(path + ext);
    }

    // --- Flush writes what is buffered without closing ---
    {
        CHECK(Log::OpenFile(path));
        Log::Info("buffered info line");
        Log::Flush();
        CHECK(ReadAll(path).find("buffered info line") != std::string::npos);
        Log::Flush();                                   // twice is fine
        Log::CloseFile();
        Log::Flush();                                   // with no file open is fine too
        fs::remove(path);
    }

    // --- a full disk is counted and does not break later lines ---
#if defined(__linux__)
    if (fs::exists("/dev/full")) {
        const std::size_t before = Log::FileWriteFailures();
        CHECK(Log::OpenFile("/dev/full", 1024 * 1024, 1));
        Log::Error("this cannot be written");
        Log::Error("neither can this");
        CHECK(Log::FileWriteFailures() > before);
        int sunk = 0;
        Log::sink = [&](Log::Level, const std::string&) { ++sunk; };
        Log::Error("the sink still hears it");
        Log::sink = nullptr;
        CHECK(sunk == 1);
        Log::CloseFile();
        // Back on a real file all is well again.
        CHECK(Log::OpenFile(path));
        const std::size_t mid = Log::FileWriteFailures();
        Log::Error("written fine");
        Log::CloseFile();
        CHECK(Log::FileWriteFailures() == mid);
        CHECK(ReadAll(path).find("written fine") != std::string::npos);
        fs::remove(path);
    }
#endif
    CHECK(!Log::OpenFile(dir.string()));                // a folder is not a log file
    CHECK(!Log::FileOpen());

    // --- a sink that logs does not recurse; the nested line still reaches the file ---
    {
        CHECK(Log::OpenFile(path));
        int calls = 0;
        Log::sink = [&](Log::Level, const std::string& msg) {
            ++calls;
            if (msg == "outer") Log::Info("inner");     // would call this sink again, and again...
        };
        Log::Info("outer");
        Log::sink = nullptr;
        Log::CloseFile();
        CHECK(calls == 1);
        const std::string text = ReadAll(path);
        CHECK(text.find("outer") != std::string::npos && text.find("inner") != std::string::npos);
        fs::remove(path);
    }

    // --- Profiler: bounds ---
    {
        Profiler p; p.enabled = true;
        CHECK(p.Count() == 0);
        CHECK(p.FrameAt(0).buckets.empty() && p.FrameAt(-1).buckets.empty());   // an empty ring
        CHECK(p.SeriesTotal(0) == 0.0 && p.SeriesCpu(-5) == 0.0);
        for (int f = 0; f < 3; ++f) { p.BeginFrame(); p.Begin("x"); p.End(); p.EndFrame(10.0 + f); }
        CHECK(p.Count() == 3);
        CHECK(p.FrameAt(0).total == 10.0 && p.FrameAt(2).total == 12.0);
        // Out-of-range timeline indexes used to read the ring out of bounds (negative) or a wrong frame (past the end).
        CHECK(p.FrameAt(-1).buckets.empty() && p.FrameAt(3).buckets.empty() && p.FrameAt(1000000).buckets.empty());
        CHECK(p.FrameAt(std::numeric_limits<int>::min()).buckets.empty() && p.FrameAt(std::numeric_limits<int>::max()).buckets.empty());
        CHECK(p.SeriesTotal(-1) == 0.0 && p.SeriesGpu(3) == 0.0 && p.SeriesTotal(2) == 12.0);
        // A full ring that wrapped keeps indexing right.
        for (int f = 0; f < Profiler::kHistory * 2 + 7; ++f) { p.BeginFrame(); p.EndFrame((double)f); }
        CHECK(p.Count() == Profiler::kHistory);
        CHECK(p.FrameAt(Profiler::kHistory - 1).total == (double)(Profiler::kHistory * 2 + 6));
        CHECK(p.FrameAt(0).total == (double)(Profiler::kHistory + 7));
        CHECK(p.FrameAt(Profiler::kHistory).buckets.empty() && p.FrameAt(-1).total == 0.0);
        // Exports over silly ranges are fine.
        std::string out;
        CHECK(p.ExportChromeTrace(out, -50, 1000000) == 0);   // these frames have no sections
        CHECK(p.ExportCsv(out, 5, 2) == 0);
        CHECK(p.DumpFrame(-1) == "(no such frame)\n" && p.DumpFrame(Profiler::kHistory) == "(no such frame)\n");
    }

    // --- Profiler: a bad frame time or GPU time does not poison the averages ---
    {
        Profiler p; p.enabled = true;
        p.BeginFrame(); p.EndFrame(10.0);
        p.BeginFrame(); p.EndFrame(std::numeric_limits<double>::quiet_NaN());
        p.BeginFrame(); p.EndFrame(-4.0);
        p.BeginFrame(); p.EndFrame(std::numeric_limits<double>::infinity());
        p.Gpu(std::numeric_limits<double>::quiet_NaN());
        p.BeginFrame(); p.EndFrame(20.0);
        double avg = 0, peak = 0; p.Range(avg, peak);
        CHECK(std::isfinite(avg) && std::isfinite(peak));
        CHECK_NEAR(avg, 6.0, 1e-9);                            // (10 + 0 + 0 + 0 + 20) / 5
        CHECK(peak == 20.0);
        CHECK(p.FrameAt(4).gpu == 0.0);
        p.Gpu(2.5);
        CHECK(p.gpuMs == 2.5);
    }

    // --- Profiler: only the thread that began the frame is profiled ---
    {
        Profiler p; p.enabled = true;
        p.BeginFrame();
        p.Begin("main");
        std::thread worker([&] {
            p.Begin("worker");                                 // used to push onto the main thread's scope stack
            p.Begin("worker.inner");
            p.End();
            p.End();
            p.EndFrame(999.0);                                 // and used to end the main thread's frame
        });
        worker.join();
        p.End();
        p.EndFrame(16.0);
        CHECK(p.Count() == 1);
        const Profiler::Frame& f = p.FrameAt(0);
        CHECK(f.total == 16.0);
        CHECK(f.buckets.size() == 1 && f.buckets[0].name == "main" && f.buckets[0].calls == 1);

        // The same thread keeps working frame after frame, and a global OKAY_PROFILE on a worker is ignored too.
        Prof().enabled = true;
        Prof().BeginFrame();
        { OKAY_PROFILE("global.main"); }
        std::thread w2([] { OKAY_PROFILE("global.worker"); });
        w2.join();
        Prof().EndFrame(1.0);
        bool sawMain = false, sawWorker = false;
        for (const auto& b : Prof().FrameAt(Prof().Count() - 1).buckets) {
            if (b.name == "global.main") sawMain = true;
            if (b.name == "global.worker") sawWorker = true;
        }
        CHECK(sawMain && !sawWorker);
        Prof().enabled = false;

        // A null name is recorded under a placeholder instead of crashing.
        Profiler q; q.enabled = true;
        q.BeginFrame(); q.Begin(nullptr); q.End(); q.EndFrame(1.0);
        CHECK(q.FrameAt(0).buckets.size() == 1 && q.FrameAt(0).buckets[0].name == "(null)");
    }

    // --- Profiler: the trace is valid JSON and valid UTF-8 whatever the names hold ---
    {
        Profiler p; p.enabled = true;
        const std::string nasty = std::string("q\"b\\n\n\t\x01 caf\xC3\xA9 bad\xFF\xFE end \xE2\x82");   // quote, backslash, controls, valid and broken UTF-8
        p.BeginFrame();
        p.Begin(nasty.c_str()); p.End();
        p.Begin("plain"); p.End();
        p.EndFrame(1.0);
        std::string json;
        CHECK(p.ExportChromeTrace(json, 0, 0) == 2);
        CHECK(utf8::IsValid(json));                            // a stray 0xFF made the whole file unopenable in a viewer
        CHECK(json.find("\\\"") != std::string::npos);         // the quote is escaped
        CHECK(json.find("\\u0001") != std::string::npos);
        CHECK(json.find("caf\xC3\xA9") != std::string::npos);  // good UTF-8 is kept as it is
        CHECK(json.find('\xFF') == std::string::npos);
        // No raw control characters anywhere, and the braces and brackets balance outside the strings.
        bool inStr = false, esc = false; int depth = 0; bool raw = false, balanced = true;
        for (unsigned char c : json) {
            if (inStr) {
                if (esc) esc = false;
                else if (c == '\\') esc = true;
                else if (c == '"') inStr = false;
                else if (c < 0x20) raw = true;
            } else if (c == '"') inStr = true;
            else if (c == '{' || c == '[') ++depth;
            else if (c == '}' || c == ']') { if (--depth < 0) balanced = false; }
        }
        CHECK(!inStr && depth == 0 && balanced && !raw);
        const std::string tracePath = (dir / "trace.json").string();
        CHECK(p.ExportChromeTraceToFile(tracePath, 0, 0));
        CHECK(ReadAll(tracePath) == json);
        CHECK(!p.ExportChromeTraceToFile((dir / "no_such_dir" / "t.json").string(), 0, 0));
    }

    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
