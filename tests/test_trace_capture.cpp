// Frame capture (TraceCapture.hpp): nested scopes on several threads, ring overwrite, counters, frame counting,
// the Chrome trace JSON, and that nothing is recorded while no capture runs. Checks run on the main thread only.
#include "test_framework.hpp"
#include "okay/Core/Profiler.hpp"
#include "okay/Core/Jobs.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

using namespace okay;

namespace {
// A small strict JSON syntax check (objects, arrays, strings with escapes, numbers, literals).
struct Json {
    const std::string& s; std::size_t i = 0;
    explicit Json(const std::string& t) : s(t) {}
    void Ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r')) ++i; }
    bool Str() {
        if (i >= s.size() || s[i] != '"') return false;
        for (++i; i < s.size(); ++i) {
            if (s[i] == '\\') { ++i; continue; }
            if ((unsigned char)s[i] < 0x20) return false;
            if (s[i] == '"') { ++i; return true; }
        }
        return false;
    }
    bool Val() {
        Ws();
        if (i >= s.size()) return false;
        if (s[i] == '{') {
            ++i; Ws();
            if (s[i] == '}') { ++i; return true; }
            for (;;) { Ws(); if (!Str()) return false; Ws(); if (s[i++] != ':') return false; if (!Val()) return false; Ws();
                if (s[i] == ',') { ++i; continue; } if (s[i] == '}') { ++i; return true; } return false; }
        }
        if (s[i] == '[') {
            ++i; Ws();
            if (s[i] == ']') { ++i; return true; }
            for (;;) { if (!Val()) return false; Ws(); if (s[i] == ',') { ++i; continue; } if (s[i] == ']') { ++i; return true; } return false; }
        }
        if (s[i] == '"') return Str();
        std::size_t st = i;
        while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '-' || s[i] == '+' || s[i] == '.')) ++i;
        return i > st;
    }
    bool Ok() { bool v = Val(); Ws(); return v && i == s.size(); }
};
std::size_t Count(const std::string& s, const std::string& what) {
    std::size_t n = 0, p = 0;
    while ((p = s.find(what, p)) != std::string::npos) { ++n; p += what.size(); }
    return n;
}
void Burn() { volatile int x = 0; for (int i = 0; i < 2000; ++i) x = x + i; }

void TestOffRecordsNothing() {
    RUN_SUITE("capture off");
    TraceCapture::Stop();
    { OKAY_PROFILE("never"); }
    Prof().Counter("never", 1);
    TraceCapture::Start();
    TraceCapture::Stop();
    CHECK(TraceCapture::EventCount() == 0);   // Start clears; nothing was recorded between
}

void TestNesting() {
    RUN_SUITE("nested scopes");
    TraceCapture::Start();
    {
        OKAY_PROFILE("outer");
        Burn();
        { OKAY_PROFILE("inner"); Burn(); { OKAY_PROFILE("leaf"); Burn(); } }
        { OKAY_PROFILE("inner2"); Burn(); }
    }
    TraceCapture::Stop();
    std::string js;
    const std::size_t n = TraceCapture::ExportJson(js);
    CHECK(n == 4);
    CHECK(Json(js).Ok());
    CHECK(js.find("\"traceEvents\"") != std::string::npos);
    CHECK(Count(js, "\"ph\":\"X\"") == 4);
    CHECK(Count(js, "\"ph\":\"M\"") == 1);   // one thread track
    // outer must start no later than inner, and contain it: find durations
    auto durOf = [&](const char* nm) {
        const std::size_t p = js.find(std::string("\"name\":\"") + nm + "\"");
        const std::size_t d = js.find("\"dur\":", p);
        return std::atof(js.c_str() + d + 6);
    };
    CHECK(durOf("outer") >= durOf("inner"));
    CHECK(durOf("inner") >= durOf("leaf"));
    CHECK(durOf("leaf") > 0.0);
}

void TestStartMidScope() {
    RUN_SUITE("capture started inside a scope");
    {
        OKAY_PROFILE("opened before");
        TraceCapture::Start();
        { OKAY_PROFILE("opened after"); }
    }   // "opened before" ends while the capture runs: it has no start in the capture and is dropped
    TraceCapture::Stop();
    std::string js;
    CHECK(TraceCapture::ExportJson(js) == 1);
    CHECK(js.find("opened after") != std::string::npos);
    CHECK(js.find("opened before") == std::string::npos);
}

void TestThreadsAndJobs() {
    RUN_SUITE("threads and jobs");
    TraceCapture::Start();
    std::vector<std::thread> ts;
    for (int t = 0; t < 3; ++t) ts.emplace_back([t] {
        TraceCapture::NameThisThread(("custom " + std::to_string(t)).c_str());
        for (int i = 0; i < 50; ++i) { OKAY_PROFILE("threadwork"); { OKAY_PROFILE("threadinner"); Burn(); } }
    });
    for (auto& t : ts) t.join();
    JobSystem js(2);
    JobCounter c;
    for (int i = 0; i < 20; ++i) js.Fork(c, [] { OKAY_PROFILE("forked"); Burn(); });
    js.Join(c);
    TraceCapture::Stop();
    std::string js2;
    const std::size_t n = TraceCapture::ExportJson(js2);
    CHECK(Json(js2).Ok());
    CHECK(Count(js2, "\"name\":\"threadwork\"") == 150);
    CHECK(Count(js2, "\"name\":\"forked\"") == 20);
    CHECK(Count(js2, "\"name\":\"task\"") == 20);
    CHECK(Count(js2, "\"ph\":\"M\"") >= 4);   // main + 3 threads (+ workers that ran something)
    CHECK(js2.find("custom 0") != std::string::npos);
    CHECK(n >= 150 * 2 + 40);
}

void TestRingWraps() {
    RUN_SUITE("ring buffer");
    TraceCapture::Start();
    const std::size_t total = TraceCapture::kRingEvents + 1000;
    for (std::size_t i = 0; i < total; ++i) { OKAY_PROFILE("spin"); }
    TraceCapture::Stop();
    std::string js;
    CHECK(TraceCapture::ExportJson(js) == TraceCapture::kRingEvents);   // the oldest 1000 were overwritten
    CHECK(Json(js).Ok());
}

void TestCountersAndFrames() {
    RUN_SUITE("counters and frames");
    const std::string path = "trace_capture_test_out.json";
    std::remove(path.c_str());
    TraceCapture::Start(3, path);
    for (int f = 0; f < 5; ++f) {
        Prof().BeginFrame();
        Prof().Stat3(10 + f, 1000, 2);
        Prof().Counter("scene.objects", 100 + f);
        { OKAY_PROFILE("work \"quoted\" \\ name"); Burn(); }
        Prof().EndFrame(16.0);
    }
    CHECK(!TraceCapture::Active());   // stopped itself after 3 frames
    CHECK(TraceCapture::FramesSeen() == 3);
    CHECK(TraceCapture::LastFile() == path);
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss; ss << in.rdbuf();
    const std::string js = ss.str();
    CHECK(!js.empty());
    CHECK(Json(js).Ok());
    CHECK(Count(js, "\"name\":\"Frame\"") == 3);
    CHECK(Count(js, "\"name\":\"scene.objects\"") == 3);
    CHECK(Count(js, "\"name\":\"draw.objects\"") == 3);
    CHECK(Count(js, "\"ph\":\"C\"") == 3 * 5);   // scene.objects + frame.ms + draw.objects + draw.triangles + particles
    CHECK(js.find("\\\"quoted\\\"") != std::string::npos);
    std::remove(path.c_str());
    std::printf("  sample: %.200s...\n", js.c_str());
}

void TestDefaultPath() {
    CHECK(TraceCapture::DefaultPath("dir") != TraceCapture::DefaultPath("dir"));
    CHECK(TraceCapture::DefaultPath("dir/").find("dir/okay-trace-") == 0);
}
} // namespace

int main() {
    TestOffRecordsNothing();
    TestNesting();
    TestStartMidScope();
    TestThreadsAndJobs();
    TestRingWraps();
    TestCountersAndFrames();
    TestDefaultPath();
    TEST_MAIN_RESULT();
}
