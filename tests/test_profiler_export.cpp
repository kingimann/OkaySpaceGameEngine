#include "test_framework.hpp"
#include "okay/Core/Profiler.hpp"
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

using namespace okay;

static void Sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// Minimal JSON sanity: balanced braces/brackets outside strings, strings terminated.
static bool Balanced(const std::string& s) {
    int curly = 0, square = 0; bool inStr = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (inStr) { if (c == '\\') ++i; else if (c == '"') inStr = false; continue; }
        if (c == '"') inStr = true;
        else if (c == '{') ++curly; else if (c == '}') { if (--curly < 0) return false; }
        else if (c == '[') ++square; else if (c == ']') { if (--square < 0) return false; }
    }
    return !inStr && curly == 0 && square == 0;
}
static int CountOf(const std::string& s, const std::string& pat) {
    int n = 0; for (size_t p = s.find(pat); p != std::string::npos; p = s.find(pat, p + pat.size())) ++n; return n;
}
static std::vector<double> NumsAfter(const std::string& s, const std::string& key) {
    std::vector<double> v;
    for (size_t p = s.find(key); p != std::string::npos; p = s.find(key, p + key.size()))
        v.push_back(std::atof(s.c_str() + p + key.size()));
    return v;
}

int main() {
    RUN_SUITE("profiler_export");
    Profiler P;   // a private instance; the API is the same one the engine drives

    // Disabled: nothing recorded, exports are empty but valid.
    {
        P.enabled = false;
        P.BeginFrame();
        { P.Begin("x"); P.End(); }
        P.EndFrame(1.0);
        CHECK(P.Count() == 0);
        std::string js; CHECK(P.ExportChromeTrace(js, 0, 10) == 0);
        CHECK(Balanced(js) && js.find("\"traceEvents\":[]") != std::string::npos);
        std::string csv; CHECK(P.ExportCsv(csv) == 0);
        CHECK(csv == "frame,section,ms,calls,depth,self_ms\n");
        CHECK(P.DumpFrame(0) == "(no such frame)\n");
    }

    // Three frames: Update (with nested Physics and two Render calls) and a tricky name.
    P.enabled = true;
    for (int f = 0; f < 3; ++f) {
        P.BeginFrame();
        P.Begin("Update");
          Sleep(2);
          P.Begin("Physics"); Sleep(3); P.End();
        P.End();
        P.Begin("we\"ird\\name\n"); Sleep(1); P.End();
        P.EndFrame(10.0);
    }
    CHECK(P.Count() == 3);

    size_t buckets = 0;
    for (int i = 0; i < 3; ++i) buckets += P.FrameAt(i).buckets.size();
    CHECK(buckets == 9);

    // Start offsets: child starts after its parent, frames advance on the timeline.
    {
        const Profiler::Frame& f = P.FrameAt(0);
        double upStart = -1, phStart = -1;
        for (auto& b : f.buckets) { if (b.name == "Update") upStart = b.startMs; if (b.name == "Physics") phStart = b.startMs; }
        CHECK(upStart >= 0 && phStart >= upStart + 1.5);
        CHECK(P.FrameAt(1).startMs > P.FrameAt(0).startMs);
    }

    // Chrome trace: valid, one event per bucket, durations match to 1 us, names escaped.
    {
        std::string js;
        int n = P.ExportChromeTrace(js, 0, 2);
        CHECK(n == 9);
        CHECK(Balanced(js));
        CHECK(CountOf(js, "\"ph\":\"X\"") == 9);
        CHECK(CountOf(js, "\"name\":") == 9);
        CHECK(js.find("we\\\"ird\\\\name\\n") != std::string::npos);
        CHECK(js.find('\n') == std::string::npos);   // the raw newline was escaped
        auto durs = NumsAfter(js, "\"dur\":");
        CHECK(durs.size() == 9);
        size_t k = 0; bool match = true;
        for (int i = 0; i < 3; ++i)
            for (auto& b : P.FrameAt(i).buckets) {
                if (k >= durs.size() || std::fabs(durs[k] - b.inclusive * 1000.0) > 1.0) match = false;
                ++k;
            }
        CHECK(match);
        auto ts = NumsAfter(js, "\"ts\":");
        bool mono = ts.size() == 9;
        for (size_t i = 1; i < ts.size(); ++i) if (ts[i] < 0) mono = false;
        CHECK(mono);
        // Frame range selection.
        std::string one; CHECK(P.ExportChromeTrace(one, 1, 1) == 3 && Balanced(one));
        std::string clamped; CHECK(P.ExportChromeTrace(clamped, -5, 99) == 9);
        std::string none; CHECK(P.ExportChromeTrace(none, 2, 1) == 0 && Balanced(none));

        std::string path = (std::filesystem::temp_directory_path() / "okay_prof_trace.json").string();
        CHECK(P.ExportChromeTraceToFile(path, 0, 2));
        std::ifstream f(path); std::stringstream ss; ss << f.rdbuf();
        CHECK(ss.str() == js);
        std::filesystem::remove(path);
        CHECK(!P.ExportChromeTraceToFile("/no/such/dir/x.json", 0, 2));
    }

    // CSV: header + one row per bucket; the odd name is quoted.
    {
        std::string csv; CHECK(P.ExportCsv(csv) == 9);
        CHECK(CountOf(csv, "\n") == 1 + 9 + 3);   // 3 names contain an embedded newline
        CHECK(csv.rfind("frame,section,ms,calls,depth,self_ms\n", 0) == 0);
        CHECK(csv.find("\"we\"\"ird\\name\n\"") != std::string::npos);
        CHECK(csv.find("0,Update,") != std::string::npos);
        std::string r; CHECK(P.ExportCsv(r, 2, 2) == 3);
    }

    // DumpFrame: parents before children, indentation follows depth.
    {
        std::string d = P.DumpFrame(0);
        size_t up = d.find("\n  Update"), ph = d.find("\n    Physics");
        CHECK(up != std::string::npos && ph != std::string::npos && up < ph);
        CHECK(d.find("Frame 0:") == 0);
        CHECK(d.find("\n      Physics") == std::string::npos);
        CHECK(P.DumpFrame(3) == "(no such frame)\n" && P.DumpFrame(-1) == "(no such frame)\n");
    }

    // Summary lists each section with min/avg/max.
    {
        std::string s = P.Summary();
        CHECK(s.find("3 frame(s)") != std::string::npos);
        CHECK(s.find("Update") != std::string::npos && s.find("Physics") != std::string::npos);
        CHECK(s.find("min") != std::string::npos && s.find("avg") != std::string::npos && s.find("max") != std::string::npos);
    }

    // Disabling again records nothing more.
    {
        P.enabled = false;
        int before = P.Count();
        P.BeginFrame(); P.Begin("a"); P.End(); P.EndFrame(5.0);
        CHECK(P.Count() == before);
    }

    TEST_MAIN_RESULT();
}
