#include "test_framework.hpp"
#include "CrashReports.hpp"
#include <filesystem>
#include <fstream>

// The launcher's Crash reports page reads the history the editor's crash handlers
// append to (Windows SEH and the Linux / macOS signal handler write one format).
int main() {
    RUN_SUITE("crash_reports");
    namespace fs = std::filesystem;
    using namespace okaycrash;

    const std::string win =
        "\n========== crash @ 2026-10-20 09:15:02 (v2.790.0) ==========\n"
        "OkaySpace crash report\nVersion: 2.790.0\nWhen: 2026-10-20 09:15:02\nExe: C:\\OkaySpace\\OkayEngine.exe\n"
        "Last step: UI.Inspector\nException: 0xC0000005\nFault module: C:\\OkaySpace\\OkayEngine.exe + 0x1A2B3C\n\n"
        "Backtrace:\n  #00 OkayEngine.exe + 0x1A2B3C  DrawInspector +0x40\n  #01 OkayEngine.exe + 0x22  main +0x9\n\n";
    const std::string lin =
        "\n========== crash @ 2026-10-23 21:52:49 (v2.800.0) ==========\n"
        "OkaySpace crash report\nVersion: 2.800.0\nWhen: 2026-10-23 21:52:49\nExe: /opt/okay/OkayEngine\n"
        "Last step: Play.Tick\nException: SIGSEGV (signal 11)\nFault module: /opt/okay/OkayEngine\n\n"
        "Backtrace:\n./OkayEngine(+0xdb42f)[0x55]\n./OkayEngine(main+0x8ac)[0x56]\n/lib/libc.so.6(+0x2a1ca)[0x7f]\n\n";
    // A report cut short (the process died mid-write): the header still dates it.
    const std::string cut = "\n========== crash @ 2026-10-24 08:00:00 (v2.801.0) ==========\nOkaySpace crash report\n";

    auto v = okaycrash::ParseLog(win + lin + cut);
    CHECK(v.size() == 3);
    if (v.size() == 3) {
        CHECK(v[0].when == "2026-10-24 08:00:00" && v[0].version == "2.801.0");   // newest first
        CHECK(v[1].lastStep == "Play.Tick" && v[1].frames == 3);
        CHECK(v[1].exception == "SIGSEGV (signal 11)");
        CHECK(v[2].lastStep == "UI.Inspector" && v[2].version == "2.790.0" && v[2].frames == 2);
        CHECK(v[2].module.find("+ 0x1A2B3C") != std::string::npos);
        CHECK(v[2].text.find("DrawInspector") != std::string::npos);
        CHECK(v[2].text.find("==========") == std::string::npos);   // the separator isn't part of a report
    }
    CHECK(okaycrash::ExceptionName("0xC0000005") == "Access violation");
    CHECK(okaycrash::ExceptionName("SIGSEGV (signal 11)") == "Access violation");
    CHECK(okaycrash::ExceptionName("0xC00000FD") == "Stack overflow");
    CHECK(okaycrash::ExceptionName("SIGABRT (signal 6)") == "Aborted");
    CHECK(okaycrash::ExceptionName("0x12345678") == "0x12345678");
    CHECK(okaycrash::ParseLog("").empty());
    CHECK(okaycrash::ParseLog("garbage without a marker").empty());


    // ---- Crash signature: the same crash is the same name, a different one is not ---------------------------------------------------
    {
        auto report = [](const std::string& exc, const std::string& step, const std::string& frames, const std::string& ver = "2.874.0") {
            return "OkaySpace crash report\nVersion: " + ver + "\nWhen: 2026-11-15 10:00:00\nLast step: " + step + "\nException: " + exc +
                   "\nFault module: OkayEngine.exe + 0x1\n\nBacktrace:\n" + frames;
        };
        const std::string f1 = "  #00 OkayEngine.exe + 0x1A2B3C  DrawInspector +0x40\n  #01 OkayEngine.exe + 0x22  main +0x9\n  #02 kernel32.dll + 0x17\n  #03 ntdll.dll + 0x5\n";
        const std::string f1b = "  #00 OkayEngine.exe + 0x1A2B3C  DrawInspector +0x99\n  #01 OkayEngine.exe + 0x22  main +0x1\n  #02 kernel32.dll + 0x99\n";   // other instruction in the same functions
        const std::string f2 = "  #00 OkayEngine.exe + 0x7777  DrawHierarchy +0x40\n  #01 OkayEngine.exe + 0x22  main +0x9\n";
        const Report a = ParseOne(report("0xC0000005", "UI.Inspector", f1));
        const Report b = ParseOne(report("0xC0000005", "Play.Tick", f1b, "2.875.0"));   // another step, another version, same functions
        const Report c = ParseOne(report("0xC0000005", "UI.Inspector", f2));
        const Report d = ParseOne(report("0xC00000FD", "UI.Inspector", f1));            // same frames, other exception
        CHECK(a.signature.size() == 16 && a.signature.find_first_not_of("0123456789abcdef") == std::string::npos);
        CHECK(a.signature == b.signature);
        CHECK(a.signature != c.signature);
        CHECK(a.signature != d.signature);
        CHECK(a.frames == 4 && a.backtrace.size() == 4);
        CHECK(TopFrame(a) == "OkayEngine.exe!DrawInspector");

        // Offsets only (no symbols): the offset is the name; leading zeros and the case of the hex do not matter.
        const std::string o1 = "  #00 OkayEngine.exe + 0x001a2b3c\n  #01 OkayEngine.exe + 0x22\n";
        const std::string o2 = "  #00 OkayEngine.exe + 0x1A2B3C\n  #01 OkayEngine.exe + 0x00022\n";
        const std::string o3 = "  #00 OkayEngine.exe + 0x1A2B3D\n  #01 OkayEngine.exe + 0x22\n";
        CHECK(ParseOne(report("0xC0000005", "x", o1)).signature == ParseOne(report("0xC0000005", "y", o2)).signature);
        CHECK(ParseOne(report("0xC0000005", "x", o1)).signature != ParseOne(report("0xC0000005", "x", o3)).signature);
        CHECK(NormalizeFrame("  #00 OkayEngine.exe + 0x1A2B3C") == "OkayEngine.exe+0x1a2b3c");
        CHECK(NormalizeFrame("#07 C:\\Program Files\\OkaySpace\\OkayEngine.exe + 0x0  Foo<int, std::allocator<int> >::bar() +0x10  (C:\\src\\a.cpp:12)") ==
              "OkayEngine.exe!Foo<int, std::allocator<int> >::bar()");

        // Linux layout, with and without a function name.
        CHECK(NormalizeFrame("./OkayEngine(main+0x8ac)[0x55d0]") == "OkayEngine!main");
        CHECK(NormalizeFrame("./OkayEngine(+0xdb42f)[0x55d0]") == "OkayEngine+0xdb42f");
        CHECK(NormalizeFrame("/lib/x86_64-linux-gnu/libc.so.6(+0x2a1ca)[0x7f1c]") == "libc.so.6+0x2a1ca");
        CHECK(NormalizeFrame("[0x7f1c2b3d]").empty() && NormalizeFrame("").empty() && NormalizeFrame("   ").empty());

        // System frames and the handler's own frames at the top of the stack do not count.
        const std::string withSys = "  #00 ntdll.dll + 0x100\n  #01 OkayEngine.exe + 0x10  OkayCrashFilter +0x4\n  #02 OkayEngine.exe + 0x1A2B3C  DrawInspector +0x40\n  #03 OkayEngine.exe + 0x22  main +0x9\n";
        const Report ws = ParseOne(report("0xC0000005", "x", withSys));
        CHECK(TopFrame(ws) == "OkayEngine.exe!DrawInspector");
        CHECK(ws.signature == ParseOne(report("0xC0000005", "x", "  #00 OkayEngine.exe + 0x1A2B3C  DrawInspector +0x40\n  #01 OkayEngine.exe + 0x22  main +0x9\n")).signature);   // handler and system frames do not change the name
        const std::string linuxA = "./OkayEngine(_Z15OkayCrashSignali+0x30)[0x1]\n/lib/x86_64-linux-gnu/libc.so.6(+0x3c050)[0x2]\n./OkayEngine(_Z11DrawInspectorv+0x40)[0x3]\n./OkayEngine(main+0x8ac)[0x4]\n";
        CHECK(TopFrame(ParseOne(report("SIGSEGV (signal 11)", "x", linuxA))) == "OkayEngine!_Z11DrawInspectorv");

        // abort() and an uncaught C++ exception look the same whatever went wrong: the last step is part of the name.
        const std::string ab = "  #00 OkayEngine.exe + 0x500  Helper +0x4\n";
        CHECK(ParseOne(report("SIGABRT (signal 6)", "Play.Tick", ab)).signature != ParseOne(report("SIGABRT (signal 6)", "UI.Inspector", ab)).signature);
        CHECK(ParseOne(report("SIGSEGV (signal 11)", "Play.Tick", ab)).signature == ParseOne(report("SIGSEGV (signal 11)", "UI.Inspector", ab)).signature);
        CHECK(ParseOne(report("C++ exception (std::terminate): out of range", "Play.Tick", ab)).signature ==
              ParseOne(report("C++ exception (std::terminate): other text", "Play.Tick", ab)).signature);   // the message does not split the crash
        // No readable frame: exception + step + version.
        const Report n1 = ParseOne(report("0xC0000005", "UI.Inspector", ""));
        CHECK(n1.signature.size() == 16 && n1.signature != ParseOne(report("0xC0000005", "UI.Inspector", "", "2.875.0")).signature);
        CHECK(n1.signature != ParseOne(report("0xC0000005", "Play.Tick", "")).signature);
        CHECK(ParseOne("").signature.size() == 16);
        CHECK(HashHex("") == "cbf29ce484222325");   // FNV-1a 64 of the empty string
        CHECK(ExceptionClass("0xC0000005") == "0xC0000005" && ExceptionClass("SIGSEGV (signal 11)") == "SIGSEGV" && ExceptionClass("") == "UNKNOWN");
        CHECK(ExceptionName("C++ exception (std::terminate): bad_alloc") == "Unhandled C++ exception");
    }

    // ---- The detailed layout: new header lines, then sections that are not backtrace frames -----------------------------------------
    {
        const std::string det =
            "\n========== crash @ 2026-11-15 10:00:00 (v2.874.0) ==========\n"
            "OkaySpace crash report\nVersion: 2.874.0\nBuild: pe-1-2\nWhen: 2026-11-15 10:00:00\nExe: C:\\x\\OkayEngine.exe\nLast step: Render3D:gl\n"
            "Exception: 0xC0000005\nFault module: OkayEngine.exe + 0x1A\nOS: Windows 10.0.19045 (x64)\nGPU: Test | Vendor\nScene: level1.okayscene\n\n"
            "Backtrace:\n  #00 OkayEngine.exe + 0x1A  Foo +0x1\n  #01 OkayEngine.exe + 0x22  main +0x9\n\n"
            "--- Breadcrumbs (oldest first: frame, step) ---\nframe 3: UI.Inspector\nframe 4: Render3D:gl x9\n"
            "--- Console (last 2 lines, oldest first: seconds before the crash, level) ---\n[-5s] I hello\n[-1s] E boom: at 12:30\n"
            "--- End of report ---\n";
        auto v = ParseLog(det + lin);
        CHECK(v.size() == 2);
        if (v.size() == 2) {
            const Report& r = v[1];
            CHECK(r.frames == 2 && r.build == "pe-1-2" && r.os == "Windows 10.0.19045 (x64)" && r.Info("GPU") == "Test | Vendor" && r.Info("Scene") == "level1.okayscene");
            CHECK(r.Info("Exe") == "C:\\x\\OkayEngine.exe");
            CHECK(r.breadcrumbs.size() == 2 && r.breadcrumbs[1] == "frame 4: Render3D:gl x9");
            CHECK(r.console.size() == 2 && r.console[1] == "[-1s] E boom: at 12:30");   // a colon inside a Console line is not a header key
            CHECK(r.text.find("--- End of report ---") != std::string::npos);
            CHECK(r.signature.size() == 16 && r.signature == ParseOne(det.substr(det.find("OkaySpace crash report"))).signature);
        }
        // An older report (no new keys, no sections) still parses, and has a signature.
        const Report old = ParseOne(lin.substr(lin.find("OkaySpace crash report")));
        CHECK(old.build.empty() && old.breadcrumbs.empty() && old.console.empty() && old.frames == 3 && old.signature.size() == 16);
    }

    // Files beside the editor: the history wins; a lone okay_crash.txt still shows;
    // Clear removes both.
    fs::path dir = fs::temp_directory_path() / "okay_crash_test";
    fs::remove_all(dir); fs::create_directories(dir);
    CHECK(okaycrash::Load(dir).empty());
    { std::ofstream(dir / "okay_crash.txt") << lin.substr(lin.find("OkaySpace crash report")); }
    auto one = okaycrash::Load(dir);
    CHECK(one.size() == 1 && one[0].lastStep == "Play.Tick");
    { std::ofstream(dir / "okay_crashlog.txt") << win + lin; }
    CHECK(okaycrash::Load(dir).size() == 2);
    CHECK(okaycrash::Clear(dir));
    CHECK(okaycrash::Load(dir).empty());
    CHECK(!fs::exists(dir / "okay_crash.txt") && !fs::exists(dir / "okay_crashlog.txt"));
    fs::remove_all(dir);

    TEST_MAIN_RESULT();
}
