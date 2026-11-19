// Linux tests for the non-blocking Xbox SDK check: the state machine (checking -> done, refresh while checking ignored, hand-over), the proof that
// the UI-thread call returns at once while a slow probe runs, the vswhere JSON reader and the probe cache file. UNVERIFIED on Windows: the
// CreateThread branch and the vswhere call were never run; a fake probe stands in here.
#include "XboxAsyncProbe.hpp"
#include "XboxVswhereJson.hpp"
#include <chrono>
#include <cstdio>
#include <thread>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)
using namespace okay::xbox;
using Clock = std::chrono::steady_clock;
static double Ms(Clock::time_point a) { return std::chrono::duration<double, std::milli>(Clock::now() - a).count(); }

static SdkInputs FakeInputs() {   // a PC with nothing installed: the report is "unavailable", which is fine for state tests
    SdkInputs in; in.fs.list = [](const std::string&) { return std::vector<std::string>(); }; return in;
}
static bool WaitDone(AsyncSdkProbe& p, int ms) { const auto t = Clock::now(); while (Ms(t) < ms) { if (!p.Get().checking) return true; std::this_thread::sleep_for(std::chrono::milliseconds(5)); } return false; }

int main() {
    // 1. the first Get starts the probe and returns at once (< 5 ms) even though the probe takes 300 ms; no probe runs before the first Get
    {
        std::atomic<int> calls{0}; std::atomic<bool> lastFresh{false};
        AsyncSdkProbe p([&](bool fresh) { ++calls; lastFresh = fresh; std::this_thread::sleep_for(std::chrono::milliseconds(300)); SdkInputs in = FakeInputs(); in.probeNote = "fake note"; return in; });
        CHECK(p.Probes() == 0 && !p.Checking() && !p.HaveResult());
        const auto t0 = Clock::now();
        const SdkReport& r = p.Get();
        const double first = Ms(t0);
        CHECK(first < 5.0); if (first >= 5.0) std::printf("  first Get took %.2f ms\n", first);
        CHECK(r.checking && !r.available && r.message.find("Checking your PC for the Xbox tools") == 0 && r.items.empty());
        CHECK(p.Probes() == 1 && p.Checking());
        // 2. more Gets and refresh requests while it runs: still fast, still "checking", NO second probe
        const auto t1 = Clock::now();
        for (int i = 0; i < 100; ++i) { const SdkReport& x = p.Get(i % 2 == 0); CHECK(x.checking); }
        CHECK(Ms(t1) < 5.0 * 100 / 10);   // 100 calls well under 50 ms in total
        CHECK(p.Probes() == 1);
        // 3. hand-over: after the probe is done the next Get returns the finished report, and the worker's note is in it
        CHECK(WaitDone(p, 3000));
        const SdkReport& d = p.Get();
        CHECK(!d.checking && !d.items.empty() && d.message.find("Note: fake note.") != std::string::npos && p.HaveResult() && calls == 1 && !lastFresh);
        // 4. later Gets are plain reads: no new probe, instant
        const auto t2 = Clock::now(); for (int i = 0; i < 1000; ++i) CHECK(!p.Get().checking); CHECK(Ms(t2) < 20.0 && p.Probes() == 1 && calls == 1);
        // 5. Check again starts a new probe (fresh = true), shows "checking" again, and the new result replaces the old one
        const SdkReport& again = p.Get(true);
        CHECK(again.checking && p.Probes() == 2);
        CHECK(WaitDone(p, 3000)); CHECK(!p.Get().checking && calls == 2 && lastFresh);
    }
    // 6. a probe that throws becomes a plain failure report, not a crash, and checking ends
    {
        AsyncSdkProbe p([](bool) -> SdkInputs { throw 1; });
        CHECK(p.Get().checking); CHECK(WaitDone(p, 3000));
        const SdkReport& r = p.Get(); CHECK(!r.checking && !r.available && r.message.find("failed unexpectedly") != std::string::npos);
    }
    // 7. the spawn function is replaceable: a synchronous spawn gives "done" on the second Get with no thread at all (the state machine alone)
    {
        int calls = 0; AsyncSdkProbe p([&](bool) { ++calls; return FakeInputs(); }, [](std::function<void()> f) { f(); });
        CHECK(p.Get().checking);          // started (ran inline); the result is handed over on the next call
        CHECK(!p.Get().checking && calls == 1 && !p.Get().checking && calls == 1);
    }
    // 8. vswhere JSON: two installs (one with an escape and a nested object/array that must be skipped), the order is kept
    {
        const std::string j = "\xEF\xBB\xBF[\n  {\n    \"instanceId\": \"a1\",\n    \"installationPath\": \"C:\\\\Program Files\\\\Microsoft Visual Studio\\\\2022\\\\Community\",\n"
            "    \"installationVersion\": \"17.9.34728.123\",\n    \"displayName\": \"Visual Studio Community 2022\",\n    \"catalog\": { \"productDisplayVersion\": \"17.9.2\", \"x\": [1, 2, {\"installationPath\": \"WRONG\"}] },\n    \"properties\": { \"nickname\": \"\" }\n  },\n"
            "  {\n    \"installationPath\": \"D:\\\\VS\\\\\\u00e9\",\n    \"installationVersion\": \"18.0.1.2\",\n    \"displayName\": \"Visual Studio \\\"Next\\\"\"\n  },\n  { \"displayName\": \"no path\" }\n]";
        std::vector<VsInstall> v; CHECK(ParseVswhereJson(j, v) && v.size() == 2);
        CHECK(v.size() == 2 && v[0].path == "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community" && v[0].version == "17.9.34728.123" && v[0].displayName == "Visual Studio Community 2022");
        CHECK(v.size() == 2 && v[1].path == "D:\\VS\\\xC3\xA9" && v[1].version == "18.0.1.2" && v[1].displayName == "Visual Studio \"Next\"");
        CHECK(ParseVswhereJson("[]", v) && v.empty());
        CHECK(!ParseVswhereJson("", v) && !ParseVswhereJson("not json", v) && !ParseVswhereJson("[{\"installationPath\": \"C:\\", v) && !ParseVswhereJson("[{", v));
    }
    // 9. the cache file: same key = hit; a changed key, a damaged head or an empty body = miss
    {
        const std::string f = MakeProbeCache("123|456|789", "[{\"installationPath\":\"C:\\\\x\"}]"); std::string j;
        CHECK(ReadProbeCache(f, "123|456|789", j) && j == "[{\"installationPath\":\"C:\\\\x\"}]");
        CHECK(!ReadProbeCache(f, "123|456|790", j) && !ReadProbeCache("garbage", "k", j) && !ReadProbeCache(MakeProbeCache("k", ""), "k", j) && !ReadProbeCache("", "k", j));
    }
    // 10. a note from the probe reaches the message of an unavailable report (the "vswhere did not answer" text)
    { SdkInputs in = FakeInputs(); in.probeNote = "vswhere did not answer within 5 seconds (it was stopped)"; SdkReport r = EvaluateSdk(in); CHECK(!r.available && r.message.find("vswhere did not answer") != std::string::npos); }
    if (g_fail) { std::printf("%d FAILED\n", g_fail); return 1; }
    std::printf("xbox_uwp_async: all passed\n");
    return 0;
}
