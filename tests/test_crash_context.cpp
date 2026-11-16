#include "test_framework.hpp"
#include "CrashContext.hpp"
#include "CrashReports.hpp"
#include <cstring>
#include <string>

// The crash context (shared/CrashContext.hpp): what the editor keeps ready for its crash handler, and the report text built from it
// without stdio or the heap. The text must parse back with shared/CrashReports.hpp (what the launcher reads).
using namespace okaycrash;

static std::string Compose(const ctx::Fault& f, int frames = 3) {
    static char buf[256 * 1024];
    ctx::Out o(buf, sizeof(buf));
    ctx::ComposeHead(o, f);
    for (int i = 0; i < frames; ++i) ctx::FrameLine(o, i, i == 0 ? "OkayEngine.exe" : "ntdll.dll", 0x1A2B3Cu + (unsigned)i * 0x10u, i == 0 ? "DrawInspector" : nullptr, 0x40u);
    ctx::ComposeTail(o, f);
    return std::string(o.data(), o.size());
}

int main() {
    RUN_SUITE("crash_context");

    // ---- Out: bounded, never overruns, always NUL terminated --------------------------------------------------------------------
    {
        char tiny[8];
        ctx::Out o(tiny, sizeof(tiny));
        o.S("abcdefghijkl");
        CHECK(o.truncated());
        CHECK(o.size() == 7 && std::strcmp(tiny, "abcdefg") == 0);
        char b[64];
        ctx::Out n(b, sizeof(b));
        n.N(-9223372036854775807LL - 1).C(' ').N(0).C(' ').U(18446744073709551615ull).C(' ').H(0xABCDEF, 8).C(' ').H(0);
        CHECK(std::string(b) == "-9223372036854775808 0 18446744073709551615 00ABCDEF 0");
        char z[1];
        ctx::Out zero(z, 0);   // a zero-size buffer is allowed and writes nothing
        zero.S("x"); CHECK(zero.size() == 0);
        ctx::Out nullbuf(nullptr, 0);
        nullbuf.S("x").N(5);
        CHECK(nullbuf.size() == 0);
    }

    // ---- Dates without localtime -----------------------------------------------------------------------------------------------------
    {
        char b[64];
        auto when = [&](std::int64_t t) { ctx::Out o(b, sizeof(b)); ctx::FormatWhen(o, t); return std::string(b); };
        CHECK(when(0) == "1970-01-01 00:00:00");
        CHECK(when(1700000000) == "2023-11-14 22:13:20");
        CHECK(when(951782400) == "2000-02-29 00:00:00");     // a leap day
        CHECK(when(4102444799) == "2099-12-31 23:59:59");
        CHECK(when(-1) == "1969-12-31 23:59:59");
        CHECK(when(86400 * 365 * 30 + 3661) == "1999-12-25 01:01:01");
    }

    // ---- Text fields: single line, cut to fit, never overflow ------------------------------------------------------------------
    ctx::Reset();
    {
        char f[8];
        ctx::SetText(f, sizeof(f), "line one\nline two\ttab");
        CHECK(std::string(f) == "line on");
        ctx::SetText(f, sizeof(f), "a\nb");
        CHECK(std::string(f) == "a b");
        ctx::SetText(f, sizeof(f), nullptr);
        CHECK(f[0] == '\0');
        ctx::SetSceneName("C:\\Users\\alice\\Games\\Space\\level1.okayscene");
        CHECK(std::string(ctx::g_state.scene) == "level1.okayscene");   // the folder (and the user's name in it) never reaches the report
        ctx::SetSceneName("/home/bob/proj/a.okayscene");
        CHECK(std::string(ctx::g_state.scene) == "a.okayscene");
        ctx::SetSceneName("");
        CHECK(ctx::g_state.scene[0] == '\0');
    }

    // ---- Breadcrumbs: repeated steps collapse, the ring keeps the newest, the last step is the newest ---------------------------
    ctx::Reset();
    CHECK(std::string(ctx::LastStep()) == "startup");
    ctx::Breadcrumb("Scene:projector");
    ctx::NextFrame();
    for (int i = 0; i < 40; ++i) ctx::Breadcrumb("Render3D:gl");   // 40 times in a row: one slot
    CHECK(std::string(ctx::LastStep()) == "Render3D:gl");
    CHECK(ctx::g_state.crumbNext.load() == 2);
    CHECK(ctx::g_state.crumbs[1].repeat == 40);
    for (int i = 0; i < 200; ++i) { std::string s = "Step" + std::to_string(i); ctx::Breadcrumb(s.c_str()); }
    CHECK(std::string(ctx::LastStep()) == "Step199");
    {
        ctx::Fault f; f.now = 1000;
        const std::string t = Compose(f);
        CHECK(t.find("Step199") != std::string::npos);
        CHECK(t.find("Step135") == std::string::npos);   // 64 slots: Step136 .. Step199 (Step135 and older are gone)
        CHECK(t.find("Step136") != std::string::npos);
        CHECK(t.find("Scene:projector") == std::string::npos);
    }
    // A step longer than a slot is cut, not overrun.
    ctx::Breadcrumb(std::string(500, 'q').c_str());
    CHECK(std::strlen(ctx::LastStep()) == (std::size_t)ctx::kCrumbLen - 1);

    // ---- Console ring: the last 200 lines, newest last, levels, cut marked -----------------------------------------------------
    ctx::Reset();
    ctx::Breadcrumb("UI.Inspector");
    for (int i = 0; i < 250; ++i) ctx::ConsoleLine(i % 50 == 0 ? 2 : i % 7 == 0 ? 1 : 0, ("line " + std::to_string(i)).c_str(), 900 + i % 100);
    ctx::ConsoleLine(2, std::string(1000, 'x').c_str(), 1000);
    ctx::ConsoleLine(0, "multi\nline\r\nmessage", 1000);
    ctx::ConsoleLine(9, "level clamps", 1000);
    ctx::ConsoleLine(0, nullptr, 1000);   // ignored
    {
        ctx::Fault f; f.now = 1000; f.exception = "0xC0000005";
        const std::string t = Compose(f);
        CHECK(t.find("--- Console (last 200 lines") != std::string::npos);
        CHECK(t.find("line 49 ") == std::string::npos && t.find("] I line 0\n") == std::string::npos);   // older than the last 200
        CHECK(t.find("line 52\n") == std::string::npos && t.find("line 53\n") != std::string::npos && t.find("line 249\n") != std::string::npos);
        CHECK(t.find("] E line 100\n") != std::string::npos);        // level 2 = E
        CHECK(t.find("] W line 91\n") != std::string::npos);         // level 1 = W
        CHECK(t.find("xxx...\n") != std::string::npos);              // the long line is cut and marked
        CHECK(t.find("multi line  message\n") != std::string::npos); // newlines never split a Console entry across report lines
        CHECK(t.find("] I level clamps") == std::string::npos && t.find("] E level clamps") != std::string::npos);
        CHECK(t.find("[-100s]") != std::string::npos || t.find("[-1s]") != std::string::npos);   // seconds before the crash
        CHECK(t.find("--- End of report ---\n") != std::string::npos);
    }

    // ---- The whole report: every promised line, in an order the parser reads ---------------------------------------------------
    ctx::Reset();
    ctx::SetVersion("2.874.0"); ctx::SetBuild("pe-6553A1B2-5F2000, built Nov 15 2026");
    ctx::SetOs("Windows 10.0.19045 (x64)"); ctx::SetCpu("Test CPU @ 3.00GHz (8 logical)");
    ctx::SetGpu("Test GPU | Test Vendor | OpenGL 4.6 | SDL renderer direct3d11");
    ctx::SetUiBackend("imgui"); ctx::SetOwnCode("platform=sdl2 http=curl OKAY_OWN_FBX=1");
    ctx::SetRamTotalMb(16384); ctx::SetMode(true, false); ctx::SetSceneName("D:\\Games\\Space\\level1.okayscene");
    ctx::SetSceneStats(128, 42); ctx::SetAutosave(1); ctx::g_state.startTime = 1000;
    for (int i = 0; i < 120; ++i) ctx::NextFrame();
    ctx::Breadcrumb("UI.Inspector"); ctx::Breadcrumb("Render3D:gl");
    ctx::ConsoleLine(0, "Welcome to OkaySpace", 1000); ctx::ConsoleLine(2, "Script error in player.okayscript", 1100);
    ctx::Fault f;
    f.version = "2.874.0"; f.when = "2026-11-15 10:00:00"; f.exe = "C:\\Users\\alice\\AppData\\Local\\OkaySpace\\Tools\\OkayEngine.exe";
    f.exception = "0xC0000005"; f.faultModule = "OkayEngine.exe + 0x1A2B3C"; f.detail = "write to address 0x0"; f.thread = "main";
    f.now = 1234; f.memMb = 812; f.memPeakMb = 1100; f.ramFreeMb = 7000;
    const std::string text = Compose(f);
    for (const char* want : {"OkaySpace crash report\n", "Version: 2.874.0\n", "Build: pe-6553A1B2-5F2000, built Nov 15 2026\n", "When: 2026-11-15 10:00:00\n",
                             "Last step: Render3D:gl\n", "Exception: 0xC0000005\n", "Fault module: OkayEngine.exe + 0x1A2B3C\n", "Fault detail: write to address 0x0\n",
                             "Thread: main\n", "OS: Windows 10.0.19045 (x64)\n", "CPU: Test CPU @ 3.00GHz (8 logical)\n", "RAM: 16384 MB total, 7000 MB free\n",
                             "Process memory: 812 MB in use, 1100 MB peak\n", "GPU: Test GPU | Test Vendor | OpenGL 4.6 | SDL renderer direct3d11\n",
                             "UI backend: imgui\n", "Own code: platform=sdl2 http=curl OKAY_OWN_FBX=1\n", "Mode: play\n", "Scene: level1.okayscene\n",
                             "Objects: 128\n", "Assets loaded: 42\n", "Uptime: 234 s, 120 frames\n", "Recovery copy: present\n", "Backtrace:\n",
                             "  #00 OkayEngine.exe + 0x1A2B3C  DrawInspector +0x40\n", "  #01 ntdll.dll + 0x1A2B4C\n",
                             "--- Breadcrumbs (oldest first: frame, step) ---\nframe 120: UI.Inspector\nframe 120: Render3D:gl\n",
                             "[-234s] I Welcome to OkaySpace\n", "[-134s] E Script error in player.okayscript\n"}) {
        const bool has = text.find(want) != std::string::npos;
        CHECK(has);
        if (!has) std::cerr << "  missing: " << want;
    }
    CHECK(text.find("D:\\Games") == std::string::npos);   // the scene's folder is not in the report
    CHECK(text.find("Backtrace:") < text.find("--- Breadcrumbs"));

    // ... and the launcher's parser reads it all back.
    {
        const Report r = ParseOne(text);
        CHECK(r.version == "2.874.0" && r.build == "pe-6553A1B2-5F2000, built Nov 15 2026");
        CHECK(r.when == "2026-11-15 10:00:00" && r.lastStep == "Render3D:gl" && r.exception == "0xC0000005");
        CHECK(r.module == "OkayEngine.exe + 0x1A2B3C" && r.os == "Windows 10.0.19045 (x64)");
        CHECK(r.frames == 3 && r.backtrace.size() == 3);   // the sections after the backtrace are not frames
        CHECK(r.Info("GPU").find("Test GPU") == 0 && r.Info("Scene") == "level1.okayscene" && r.Info("Mode") == "play");
        CHECK(r.Info("Uptime") == "234 s, 120 frames" && r.Info("Objects") == "128" && r.Info("Own code").find("OKAY_OWN_FBX=1") != std::string::npos);
        CHECK(r.breadcrumbs.size() == 2 && r.breadcrumbs[1] == "frame 120: Render3D:gl");
        CHECK(r.console.size() == 2 && r.console[1].find("Script error") != std::string::npos);
        CHECK(r.signature.size() == 16);
        CHECK(TopFrame(r) == "OkayEngine.exe!DrawInspector");
    }

    // ---- A tiny buffer: truncated, still NUL terminated, and what is there still parses -----------------------------------------
    {
        char small[600];
        ctx::Out o(small, sizeof(small));
        ctx::ComposeHead(o, f);
        ctx::ComposeTail(o, f);
        CHECK(o.truncated() && o.size() == sizeof(small) - 1 && small[sizeof(small) - 1] == '\0');
        CHECK(ParseOne(std::string(small, o.size())).version == "2.874.0");
    }
    // An empty context still gives a report with the fields that exist.
    ctx::Reset();
    {
        ctx::Fault g; g.exception = "SIGSEGV (signal 11)"; g.now = 5;
        const std::string t = Compose(g, 1);
        CHECK(t.find("Last step: startup\n") != std::string::npos && t.find("Mode: edit\n") != std::string::npos);
        CHECK(t.find("GPU:") == std::string::npos && t.find("Scene:") == std::string::npos && t.find("Uptime:") == std::string::npos);
    }

    // ---- Which OKAY_* switches are on: switches yes, paths and keys never -----------------------------------------------------------
    {
        const char* env[] = {"PATH=/usr/bin", "OKAY_OWN_FBX=1", "OKAY_UI_BACKEND=okayui", "OKAY_ACCOUNT_API_KEY=sb_publishable_abc", "OKAY_CRASH_DIR=C:\\Users\\alice\\x",
                             "OKAY_OWN_WAV=0", "OKAY_SOMETHING=a/b", "OKAY_LONGVALUE=abcdefghijklmnopqrstuvwxyz", "OKAY_TOKEN_X=1", "OKAY_EMPTY=", "OKAY_=1",
                             "okay_lower=1", "OKAY_PLATFORM_BACKEND=own", "OKAY_GITHUB_PASSWORD=1", "OKAY_ACCOUNT_SERVER=https://x.supabase.co", nullptr};
        char out[512];
        ctx::FormatSwitches(out, sizeof(out), env);
        CHECK(std::string(out) == "OKAY_OWN_FBX=1 OKAY_UI_BACKEND=okayui OKAY_OWN_WAV=0 OKAY_PLATFORM_BACKEND=own");
        char small[40];
        ctx::FormatSwitches(small, sizeof(small), env);
        CHECK(std::strlen(small) < sizeof(small) && std::string(small) == "OKAY_OWN_FBX=1 OKAY_UI_BACKEND=okayui");
        ctx::FormatSwitches(small, sizeof(small), nullptr);
        CHECK(small[0] == '\0');
    }

    TEST_MAIN_RESULT();
}
