// The editor's hang watchdog (editor/HangWatchdog.hpp, .cpp): owner bug 2026-11-25, "sometimes the editor freezes and i gotta use task
// manager to close it". The rules run on a fake clock (a loop that stops beating is reported at 5 s and asked about at 15 s, one that
// keeps beating never is, time in a native dialog does not count, the thresholds come from the environment); then the real thread
// watches a fake main loop that stops inside a profiler scope, and its report must name that scope within the time.
// The thread test uses <thread> for the fake main loop's sleeps only (tests are not built for Windows with it; rule 21a is for the
// programs).
#include "test_framework.hpp"
#include "HangWatchdog.hpp"
#include "CrashContext.hpp"
#include "CrashHandler.hpp"
#include "okay/Core/Profiler.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;
using namespace okay::editor::hang;

namespace {
bool Has(const std::string& hay, const std::string& n) { return hay.find(n) != std::string::npos; }
std::string ReadAll(const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
}

int main() {
    RUN_SUITE("thresholds from the environment");
    {
        const Settings d = Settings::From(nullptr, nullptr);
        CHECK_NEAR(d.reportSeconds, 5.0, 1e-9); CHECK_NEAR(d.askSeconds, 15.0, 1e-9);
        const Settings off = Settings::From("0", "0");
        CHECK(off.reportSeconds == 0.0 && off.askSeconds == 0.0);
        const Settings low = Settings::From("0.2", "1");   // floors: 1 s and 2 s
        CHECK_NEAR(low.reportSeconds, 1.0, 1e-9); CHECK_NEAR(low.askSeconds, 2.0, 1e-9);
        const Settings bad = Settings::From("soon", "nan");
        CHECK_NEAR(bad.reportSeconds, 5.0, 1e-9); CHECK_NEAR(bad.askSeconds, 15.0, 1e-9);
        const Settings order = Settings::From("20", "10");   // the question never before the report
        CHECK_NEAR(order.askSeconds, 20.0, 1e-9);
        CHECK_NEAR(Settings::From("8", "30").reportSeconds, 8.0, 1e-9);
    }

    RUN_SUITE("a loop that keeps beating is never reported");
    {
        Tracker t; Settings s;
        int actions = 0;
        for (int i = 0; i < 400; ++i) if (t.Step((std::uint32_t)i, i * 0.25, false, s) != Action::None) ++actions;   // 100 s, a beat every look
        CHECK(actions == 0);
        // A slow but moving loop (one frame every 4 s) is not a hang either.
        Tracker u; int a2 = 0;
        for (int i = 0; i < 400; ++i) if (u.Step((std::uint32_t)(i / 16), i * 0.25, false, s) != Action::None) ++a2;
        CHECK(a2 == 0);
    }

    RUN_SUITE("a loop that stops: report at 5 s, the question at 15 s and again after Wait, a line when it moves again");
    {
        Tracker t; Settings s;
        double reportAt = -1, askAt = -1, ask2At = -1;
        for (int i = 0; i <= 160; ++i) {   // stuck from t = 0 for 40 s
            const double now = i * 0.25;
            const Action a = t.Step(7, now, false, s);
            if (a == Action::Report) { CHECK(reportAt < 0); reportAt = now; }
            if (a == Action::Ask) { if (askAt < 0) { askAt = now; t.Waited(now, s); } else if (ask2At < 0) ask2At = now; }
        }
        CHECK_NEAR(reportAt, 5.0, 0.26);
        CHECK_NEAR(askAt, 15.0, 0.26);
        CHECK_NEAR(ask2At, 30.0, 0.26);
        CHECK(t.Step(8, 40.25, false, s) == Action::Recovered);
        CHECK_NEAR(t.stuckFor, 40.25, 0.01);
        CHECK(t.Step(9, 40.5, false, s) == Action::None);
    }

    RUN_SUITE("waiting on the person (a native file dialog) is not a hang");
    {
        Tracker t; Settings s;
        int actions = 0;
        for (int i = 0; i < 200; ++i) if (t.Step(3, i * 0.25, true, s) != Action::None) ++actions;
        CHECK(actions == 0);
        // the dialog closes and the loop stays stuck: counted from the moment it closed
        double reportAt = -1;
        for (int i = 200; i < 240; ++i) if (t.Step(3, i * 0.25, false, s) == Action::Report) reportAt = i * 0.25;
        CHECK_NEAR(reportAt, 50.0 + 5.0, 0.26);
    }

    RUN_SUITE("report text names the stage, the scopes and the breadcrumbs");
    {
        CHECK(StageText({"Editor.Update", "UI.Project"}, "x") == "Editor.Update > UI.Project");
        CHECK(StageText({}, "Project open: loading the scene") == "Project open: loading the scene");
        CHECK(StageText({}, "") == "startup");
        const std::string r = ReportText("2.9.0", "now", 6.5, 42, "UI.Project", {"UI.Project"}, {"frame 41: Project open"}, "main.okayscene", {});
        CHECK(Has(r, "Stuck in: UI.Project") && Has(r, "No frame for: 6.5 s") && Has(r, "frame 41: Project open") && Has(r, "main.okayscene"));
        CHECK(Has(LogLine(Action::Report, 5.2, 9, "A > B"), "HANG: no frame for 5.2 s") && Has(LogLine(Action::Report, 5.2, 9, "A > B"), "A > B"));
        CHECK(Has(LogLine(Action::Recovered, 7.0, 10, "A"), "moved again after 7.0 s"));
        CHECK(Has(AskText(15, "UI.Project"), "stuck in: UI.Project"));
    }

    RUN_SUITE("the real thread: a fake main loop stops inside a scope; okay_hang.txt names it within the time");
    {
        const fs::path dir = fs::temp_directory_path() / ("okay_hang_test_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir);
        const std::string d = dir.string() + "/";
        std::snprintf(okaycrash::handler::g_cfg.dir, sizeof(okaycrash::handler::g_cfg.dir), "%s", d.c_str());
#if defined(_WIN32)
        std::wcsncpy(okaycrash::handler::g_cfg.dirW, dir.wstring().c_str(), 1000);
#endif
        setenv("OKAY_HANG_REPORT_S", "1", 1);
        setenv("OKAY_HANG_ASK_S", "0", 1);   // no question in a test
        okay::Profiler& pr = okay::Prof();
        pr.enabled = true;
        CHECK(Start("test"));
        // 0.8 s of healthy frames: no report
        for (int i = 0; i < 40; ++i) {
            okaycrash::ctx::NextFrame();
            pr.BeginFrame();
            { OKAY_PROFILE("Editor.Update"); }
            pr.EndFrame(16.0);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        CHECK(!fs::exists(dir / "okay_hang.txt"));
        // now the frame gets stuck inside two scopes after a project-open breadcrumb
        okaycrash::ctx::NextFrame();
        pr.BeginFrame();
        okaycrash::ctx::Breadcrumb("Project open: indexing the assets");
        pr.Begin("Editor.Update");
        pr.Begin("UI.Project");
        const auto t0 = std::chrono::steady_clock::now();
        bool seen = false;
        while (!seen && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(4)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            seen = fs::exists(dir / "okay_hang.txt");
        }
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(seen);
        CHECK(took >= 0.9 && took < 2.5);   // 1 s threshold, looked at every 250 ms
        std::this_thread::sleep_for(std::chrono::milliseconds(100));   // the file is written in one go; let it close
        const std::string rep = ReadAll(dir / "okay_hang.txt");
        CHECK(Has(rep, "Stuck in: Editor.Update > UI.Project"));
        CHECK(Has(rep, "Project open: indexing the assets"));
        pr.End(); pr.End();
        pr.EndFrame(1000.0);
        okaycrash::ctx::NextFrame();   // it moves again
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        Stop();
        Stop();   // twice is fine
        pr.enabled = false;
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    TEST_MAIN_RESULT();
}
