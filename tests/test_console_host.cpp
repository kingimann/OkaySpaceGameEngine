// The console with a real host (okay/Core/ConsoleHost.hpp): a GameLoop with its LoopConsole commands, the typed
// line (history, completion, Enter), `--cvar` from the command line, the standard cvars and the profiler sections a
// player-style frame records. What the player's ` console and the editor's Console command line run.
#include "test_framework.hpp"
#include "okay/Core/CommandLine.hpp"
#include "okay/Core/Console.hpp"
#include "okay/Core/ConsoleHost.hpp"
#include "okay/Core/Cvar.hpp"
#include "okay/Core/DisplaySettings.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/GameLoop.hpp"
#include "okay/Core/LoopConsole.hpp"
#include "okay/Core/Profiler.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Core/Time.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;

namespace {

bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

void Fresh() {
    Console::Reset();
    Cvars::Clear();
    Game::Reset();
    Time::ResetForTesting();
}

// Some work the compiler cannot drop, so a profiled scope takes a measurable time.
volatile double g_sink = 0.0;
void Work(int n) { double s = 0.0; for (int i = 0; i < n; ++i) s += (double)i * 0.5; g_sink = g_sink + s; }

}  // namespace

int main() {
    RUN_SUITE("console_host");

    // ---- a GameLoop host: help and the loop's time commands --------------------------------------------
    {
        Fresh();
        GameLoop loop;
        LoopConsole loopCmds(loop);
        consolehost::RegisterStandard();
        for (int i = 0; i < 30; ++i) loop.BeginFrame(1.0 / 60.0);

        const Console::Result help = Console::Execute("help");
        CHECK(help.ok);
        CHECK(Has(help.output, "time.stats"));
        CHECK(Has(help.output, "time.scale"));
        CHECK(Has(help.output, "prof.save"));
        const Console::Result stats = Console::Execute("time.stats");
        CHECK(stats.ok);
        CHECK(Has(stats.output, "30 frames"));
        CHECK(Has(stats.output, "fps"));
        const Console::Result scale = Console::Execute("time.scale 0.5");
        CHECK(scale.ok && Time::TimeScale() == 0.5f);
    }

    // ---- the typed line: Enter echoes and runs, history, Tab completion -------------------------------
    {
        Fresh();
        consolehost::RegisterStandard();
        ConsoleInput in;
        in.Type("game.sp");
        std::vector<std::string> m = in.Complete();
        CHECK(m.size() == 1 && in.text == "game.speed ");
        in.Type("0.25");
        const Console::Result r = in.Submit();
        CHECK(r.ok && in.text.empty());
        CHECK(Time::TimeScale() == 0.25f);
        const std::vector<std::string>& lines = Console::Lines();
        CHECK(lines.size() >= 2 && lines[lines.size() - 2] == "> game.speed 0.25");
        CHECK(Has(lines.back(), "game.speed = 0.25"));

        in.Type("help");
        in.Submit();
        in.HistoryPrev();
        CHECK(in.text == "help");
        in.HistoryPrev();
        CHECK(in.text == "game.speed 0.25");
        in.HistoryPrev();                       // stays on the oldest
        CHECK(in.text == "game.speed 0.25");
        in.HistoryNext();
        CHECK(in.text == "help");
        in.HistoryNext();                       // past the newest: the empty line again
        CHECK(in.text.empty());

        // several matches: extend to the common part and list them
        in.Type("pr");
        m = in.Complete();
        CHECK(m.size() == 2 && in.text == "prof.s");   // prof.save, prof.sections
        in.text.clear();
        in.Type("debug.");
        m = in.Complete();
        CHECK(m.size() >= 2 && in.text == "debug.");
        CHECK(Has(Console::Lines().back(), "debug."));

        // the backquote that opens the console is not typed into it; Backspace removes a whole UTF-8 character
        in.text.clear();
        in.Type("`a\xC3\xA9");
        CHECK(in.text == "a\xC3\xA9");
        in.Backspace();
        CHECK(in.text == "a");
        in.Backspace(); in.Backspace();
        CHECK(in.text.empty());
        CHECK(!in.Submit().output.size());       // an empty line does nothing
    }

    // ---- --cvar on the command line ----------------------------------------------------------------------
    {
        Fresh();
        DisplaySettings::SetVSync(true);
        consolehost::RegisterStandard();
        CommandLine cl;
        consolehost::DeclareOptions(cl);
        const std::vector<std::string> args = {"--cvar", "game.speed=0.5", "--cvar=r.vsync=0", "--cvar", "nope.x=1",
                                               "--cvar", "game.speed2", "--console"};
        CHECK(cl.Parse(args));
        CHECK(cl.GetBool("console"));
        const std::vector<std::string> warn = consolehost::ApplyCvarArgs(cl, true);
        CHECK(warn.size() == 2);
        CHECK(warn.size() == 2 && Has(warn[0], "nope.x") && Has(warn[0], "unknown"));
        CHECK(warn.size() == 2 && Has(warn[1], "game.speed2"));
        CHECK(Time::TimeScale() == 0.5f);
        CHECK(!DisplaySettings::VSync());
        Cvar* v = Cvars::Find("r.vsync");
        CHECK(v && !v->AsBool());

        // Settings changed elsewhere (the in-game Settings screen) show in the console after a sync
        DisplaySettings::SetVSync(true);
        Time::SetTimeScale(2.0f);
        consolehost::SyncFromEngine();
        CHECK(v && v->AsBool());
        CHECK(Cvars::Find("game.speed") && Cvars::Find("game.speed")->AsFloat() == 2.0f);
        DisplaySettings::SetVSync(true);
    }

    // ---- a shipped game without the console ignores --cvar (one warning), so game.speed cannot be changed ----
    {
        Fresh();
        consolehost::RegisterStandard();
        CommandLine cl;
        consolehost::DeclareOptions(cl);
        CHECK(cl.Parse(std::vector<std::string>{"--cvar", "game.speed=0.25", "--cvar", "debug.show_fps=1"}));
        const std::vector<std::string> warn = consolehost::ApplyCvarArgs(cl, false);
        CHECK(warn.size() == 1);
        CHECK(warn.size() == 1 && Has(warn[0], "ignored") && Has(warn[0], "--console"));
        CHECK(Time::TimeScale() == 1.0f);
        CHECK(Cvars::Find("game.speed") && Cvars::Find("game.speed")->AsFloat() == 1.0f);
        CHECK(Cvars::Find("debug.show_fps") && !Cvars::Find("debug.show_fps")->AsBool());
        CommandLine none;
        consolehost::DeclareOptions(none);
        CHECK(none.Parse(std::vector<std::string>{}));
        CHECK(consolehost::ApplyCvarArgs(none, false).empty());   // nothing given: nothing to warn about
        CHECK(consolehost::ApplyCvarArgs(cl, true).empty());       // allowed: both apply
        CHECK(Time::TimeScale() == 0.25f);
    }

    // ---- r.vsync is for this run only: the saved display settings keep the player's own choice ----------
    {
        Fresh();
        Prefs::SetInt("video.vsync", 1);
        DisplaySettings::Load();
        CHECK(DisplaySettings::VSync());
        consolehost::RegisterStandard();
        CHECK(consolehost::ApplyCvarArgs(std::vector<std::string>{"r.vsync=0"}).empty());
        CHECK(!DisplaySettings::VSync());
        CHECK(Prefs::GetInt("video.vsync", -1) == 1);
        DisplaySettings::SetQuality(1);                    // another setting saved: vsync still saved as the player chose
        CHECK(!DisplaySettings::VSync());
        CHECK(Prefs::GetInt("video.vsync", -1) == 1);
        CHECK(Console::Execute("r.vsync 1").ok);           // typed in the console: also this run only
        CHECK(DisplaySettings::VSync() && Prefs::GetInt("video.vsync", -1) == 1);
        CHECK(Console::Execute("r.vsync 0").ok);
        DisplaySettings::SetVSync(false);                  // the player picks it in Settings: that one is saved
        CHECK(!DisplaySettings::VSync() && Prefs::GetInt("video.vsync", -1) == 0);
        DisplaySettings::SetVSync(true);
        CHECK(Prefs::GetInt("video.vsync", -1) == 1);
    }

    // ---- who may open the player console ----------------------------------------------------------------
    {
        CHECK(!consolehost::PlayerConsoleAllowed(false, false, nullptr));
        CHECK(consolehost::PlayerConsoleAllowed(true, false, nullptr));
        CHECK(consolehost::PlayerConsoleAllowed(false, true, nullptr));
        CHECK(consolehost::PlayerConsoleAllowed(false, false, "1"));
        CHECK(!consolehost::PlayerConsoleAllowed(false, false, "0"));
        CHECK(!consolehost::PlayerConsoleAllowed(false, false, ""));
    }

    // ---- a player-style loop records Render and Audio sections ------------------------------------------
    // The order of player/main.cpp's frame: profiler gate, BeginFrame, the loop (scene update), audio mix,
    // render submit, UI, present, EndFrame. With debug.profile on the profiler records every frame even with
    // the F3 systems page closed.
    {
        Fresh();
        Profiler& p = Prof();
        p.enabled = false;
        consolehost::RegisterStandard();
        CHECK(!consolehost::ProfilerWanted(false));
        CHECK(consolehost::ProfilerWanted(true));
        CHECK(Console::Execute("debug.profile 1").ok);
        CHECK(consolehost::ProfilerWanted(false));

        GameLoop loop;
        loop.OnUpdate = [](float) { OKAY_PROFILE("Scripts.Update"); Work(2000); };
        const int before = p.Count();
        for (int f = 0; f < 5; ++f) {
            p.enabled = consolehost::ProfilerWanted(false);
            p.BeginFrame();
            loop.BeginFrame(1.0 / 60.0);
            { OKAY_PROFILE("Audio.Mix"); Work(3000); }
            { OKAY_PROFILE("Render.Submit"); Work(5000); }
            { OKAY_PROFILE("UI.Draw"); Work(1000); }
            { OKAY_PROFILE("Render.Present"); Work(1000); }
            p.EndFrame(16.0);
        }
        CHECK(p.Count() >= before + 5 || p.Count() == Profiler::kHistory);
        const Profiler::Frame& last = p.FrameAt(p.Count() - 1);
        bool render = false, audio = false, scripts = false;
        for (const Profiler::Bucket& b : last.buckets) {
            if (b.name == "Render.Submit" && b.calls == 1) render = true;
            if (b.name == "Audio.Mix" && b.calls == 1) audio = true;
            if (b.name == "Scripts.Update") scripts = true;
        }
        CHECK(render);
        CHECK(audio);
        CHECK(scripts);
        CHECK(Profiler::CategorySelf(last, "Render") > 0.0);
        CHECK(Profiler::CategorySelf(last, "Audio") > 0.0);

        // prof.save writes the recorded frames as a Chrome trace
        const std::string path = "test_console_host_trace.json";
        std::remove(path.c_str());
        const Console::Result saved = Console::Execute("prof.save " + path);
        CHECK(saved.ok);
        std::ifstream f(path);
        std::stringstream ss; ss << f.rdbuf();
        CHECK(Has(ss.str(), "\"Render.Submit\"") && Has(ss.str(), "\"Audio.Mix\""));
        f.close();
        std::remove(path.c_str());
        const Console::Result sections = Console::Execute("prof.sections");
        CHECK(sections.ok && Has(sections.output, "Render.Submit") && Has(sections.output, "Audio.Mix"));

        CHECK(Console::Execute("debug.profile 0").ok);
        CHECK(!consolehost::ProfilerWanted(false));
        p.enabled = false;
    }

    Fresh();
    TEST_MAIN_RESULT();
}
