#include "test_framework.hpp"
#include "CrashReports.hpp"
#include <filesystem>
#include <fstream>

// The launcher's Crash reports page reads the history the editor's crash handlers
// append to (Windows SEH and the Linux / macOS signal handler write one format).
int main() {
    RUN_SUITE("crash_reports");
    namespace fs = std::filesystem;

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
