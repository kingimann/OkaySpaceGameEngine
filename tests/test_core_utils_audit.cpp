// Core wave 6, 1.18 Core runtime utilities: audit fixes and additions.
//   - CommandLine::GetBool honours the default an option was declared with (like Get / GetInt).
//   - Cvar: AsInt / AsFloat saturate instead of undefined casts, "inf" and out-of-range text are refused for a float,
//     a value too small for a double reads as 0, a settings file or script with a byte order mark loads its first line.
//   - Cvar flags: kReadOnly, kCheat (needs Cvars::SetCheatsEnabled), kRestartRequired with RestartPending.
//   - Console: a command that runs itself ends with an error instead of overflowing the stack; toggle and reset obey
//     the flags.
//   - StateMachine: a Change made from an exit callback waits for the transition instead of recursing.
#include "test_framework.hpp"
#include "okay/Core/CommandLine.hpp"
#include "okay/Core/Console.hpp"
#include "okay/Core/Cvar.hpp"
#include "okay/Core/DebugOverlay.hpp"
#include "okay/Core/StateMachine.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

static void PutFile(const std::string& path, const std::string& bytes) {
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    o << bytes;
}
static std::string ReadAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main() {
    RUN_SUITE("core_utils_audit");

    const fs::path dir = fs::temp_directory_path() / ("okay_core_utils_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);

    // --- CommandLine::GetBool and the declared default ---
    {
        CommandLine cl;
        cl.Value("vsync", "B", "vertical sync", "true");
        cl.Value("shadows", "B", "shadows", "off");
        cl.Value("plain", "B", "no default");
        cl.Flag("windowed");
        CHECK(cl.Parse(std::vector<std::string>{}));
        CHECK(cl.GetBool("vsync", false));              // the declared default "true" (it used to be ignored: false)
        CHECK(!cl.GetBool("shadows", true));            // the declared default "off"
        CHECK(cl.GetBool("plain", true) && !cl.GetBool("plain", false));   // none declared: the caller's default
        CHECK(!cl.GetBool("windowed") && cl.GetBool("windowed", true));
        CHECK(!cl.GetBool("undeclared") && cl.GetBool("undeclared", true));
        CHECK(cl.Parse(std::vector<std::string>{"--vsync=0", "--shadows", "yes", "--windowed"}));
        CHECK(!cl.GetBool("vsync", true) && cl.GetBool("shadows") && cl.GetBool("windowed"));
        CHECK(cl.Parse(std::vector<std::string>{"--vsync", "FALSE"}));
        CHECK(!cl.GetBool("vsync", true));

        // Odd command lines: nothing crashes, the rules in the header hold.
        CommandLine c2;
        c2.Value("n", "N", "", "5").Short('n');
        c2.Flag("x").Short('x');
        c2.OptionalValue("mode", "M");
        CHECK(c2.Parse(std::vector<std::string>{"", "-", "--", "--x", "-n", "-3"}));
        CHECK(c2.Positionals().size() == 5 && c2.Positional(0) == "" && c2.Positional(1) == "-" && c2.Positional(4) == "-3");
        CHECK(c2.Parse(std::vector<std::string>{"-n", "-3", "-x"}));
        CHECK(c2.GetInt("n") == -3 && c2.Has("x"));
        CHECK(!c2.Parse(std::vector<std::string>{"-n"}) && !c2.Error().empty());
        CHECK(c2.Parse(std::vector<std::string>{"--n=99999999999"}));
        CHECK(c2.GetInt("n", 7) == 7);                     // too big for an int: the default
        int out = 0;
        CHECK(!c2.TryGetInt("n", 0, 10, out));
        CHECK(c2.Parse(std::vector<std::string>{"--mode", "--x"}));
        CHECK(c2.Has("mode") && c2.Get("mode").empty() && c2.Has("x"));
        const char* argv[] = {"prog", nullptr, "--x"};
        CHECK(c2.Parse(3, argv));                          // a null argument is an empty one
        CHECK(c2.Positional(0) == "" && c2.Has("x"));
    }

    // --- Cvar: conversions and parsing at the extremes ---
    {
        Cvars::Clear();
        Cvar& big = Cvars::Float("t.big", 1e20f);
        CHECK(big.AsInt() == std::numeric_limits<int>::max());   // (int)1e20 is undefined behaviour; it gave INT_MIN
        big.SetFloat(-1e20f);
        CHECK(big.AsInt() == std::numeric_limits<int>::min());
        big.SetFloat(2.9f);
        CHECK(big.AsInt() == 2);
        Cvar& s = Cvars::String("t.str", "1e30");
        CHECK(s.AsInt() == std::numeric_limits<int>::max() && std::isfinite(s.AsFloat()));
        s.SetString("-1e300");
        CHECK(s.AsInt() == std::numeric_limits<int>::min());
        CHECK(s.AsFloat() == -std::numeric_limits<float>::infinity());   // out of range: infinity, not undefined
        s.SetString("nan");
        CHECK(s.AsInt() == 0 && s.AsFloat() == 0.0f);

        Cvar& f = Cvars::Float("t.f", 1.0f);
        std::string err;
        CHECK(!f.Set("inf", &err) && f.AsFloat() == 1.0f);        // "inf" used to be stored and saved
        CHECK(!f.Set("-infinity") && !f.Set("nan") && !f.Set("1e999") && !f.Set("1e39", &err) && f.AsFloat() == 1.0f);
        CHECK(!err.empty());
        CHECK(f.Set("1e-999") && f.AsFloat() == 0.0f);             // too small for a double: just 0 (it used to be refused)
        CHECK(f.Set("3.4e38") && std::isfinite(f.AsFloat()));
        CHECK(f.Set("  2.5  ") && f.AsFloat() == 2.5f);

        Cvar& n = Cvars::Int("t.n", 1);
        CHECK(n.Set("2147483647") && n.AsInt() == std::numeric_limits<int>::max());
        CHECK(n.Set("-2147483648") && n.AsInt() == std::numeric_limits<int>::min());
        CHECK(n.Set("3000000000") && n.AsInt() == std::numeric_limits<int>::max());   // clamped
        CHECK(!n.Set("99999999999999999999999"));                                     // too big for anything
        CHECK(!n.Set("1.5") && !n.Set("") && !n.Set("abc") && n.AsInt() == std::numeric_limits<int>::max());
        Cvars::Clear();
    }

    // --- Cvar: a byte order mark in front of the first line of a settings file ---
    {
        Cvars::Clear();
        Cvar& w = Cvars::Int("gfx.width", 1280, "", 0, 0, true);
        const std::string path = (dir / "settings.cfg").string();
        PutFile(path, "\xEF\xBB\xBF" "gfx.width 1920\n");
        CHECK(Cvars::Load(path));
        CHECK(w.AsInt() == 1920);                                  // the first line used to be "<BOM>gfx.width": unknown
        Cvars::Clear();
    }

    // --- Cvar flags ---
    {
        Cvars::Clear();
        Cvar& ro = Cvars::String("build.version", "1.0").SetFlags(Cvar::kReadOnly);
        CHECK(ro.ReadOnly() && !ro.Cheat() && !ro.RestartRequired() && ro.Flags() == Cvar::kReadOnly);
        std::string why;
        CHECK(!ro.Set("2.0", &why) && why.find("read only") != std::string::npos);
        CHECK(ro.AsString() == "1.0");
        CHECK(!Cvars::Set("build.version", "2.0", &why) && ro.AsString() == "1.0");
        CHECK(!Cvars::SetFromAssignment("build.version=2.0") && ro.AsString() == "1.0");
        CHECK(!ro.CanSetFromText(&why));
        ro.SetString("1.1");                                       // code may: the engine publishes the value
        CHECK(ro.AsString() == "1.1");
        ro.Reset();
        CHECK(ro.AsString() == "1.0");
        CHECK(!Console::Execute("build.version 3").ok && ro.AsString() == "1.0");
        CHECK(!Console::Execute("set build.version 3").ok && ro.AsString() == "1.0");
        CHECK(!Console::Execute("reset build.version").ok);
        CHECK(Console::Execute("get build.version").output.find("read only") != std::string::npos);

        // Cheat: refused until cheats are on.
        Cvar& god = Cvars::Bool("cheat.god", false, "take no damage").SetFlags(Cvar::kCheat);
        CHECK(!Cvars::CheatsEnabled());
        CHECK(!god.Set("1", &why) && why.find("cheats") != std::string::npos && !god.AsBool());
        CHECK(!Console::Execute("toggle cheat.god").ok && !god.AsBool());      // toggle used to bypass any check
        CHECK(!Console::Execute("cheat.god true").ok && !god.AsBool());
        god.SetBool(true);                                                      // code can
        CHECK(god.AsBool());
        god.Reset();
        Cvars::SetCheatsEnabled(true);
        CHECK(Cvars::CheatsEnabled());
        CHECK(Console::Execute("toggle cheat.god").ok && god.AsBool());
        CHECK(Console::Execute("reset cheat.god").ok && !god.AsBool());
        CHECK(Console::Execute("cheat.god on").ok && god.AsBool());
        CHECK(Console::Execute("get cheat.god").output.find("cheat") != std::string::npos);
        Cvars::SetCheatsEnabled(false);
        CHECK(!Console::Execute("cheat.god off").ok && god.AsBool());           // turning cheats off does not undo, and blocks edits

        // Restart required: changes any time, RestartPending says the new value is not in use yet.
        Cvar& msaa = Cvars::Int("gfx.msaa", 4, "anti-aliasing", 0, 8, true).SetFlags(Cvar::kRestartRequired);
        CHECK(msaa.RestartRequired() && !msaa.RestartPending() && Cvars::RestartPending().empty());
        Console::Result r = Console::Execute("gfx.msaa 8");
        CHECK(r.ok && msaa.AsInt() == 8 && msaa.RestartPending());
        CHECK(r.output.find("restart") != std::string::npos);
        CHECK(Cvars::RestartPending().size() == 1 && Cvars::RestartPending()[0] == "gfx.msaa");
        CHECK(Console::Execute("get gfx.msaa").output.find("restart required") != std::string::npos);
        msaa.SetInt(4);                                                         // back to the value in use: nothing pending
        CHECK(!msaa.RestartPending());
        msaa.SetInt(2);
        CHECK(msaa.RestartPending());
        Cvars::MarkAllApplied();                                                // e.g. after loading the settings at startup
        CHECK(!msaa.RestartPending() && Cvars::RestartPending().empty());
        msaa.SetInt(6);
        CHECK(msaa.RestartPending());
        // A string one compares text.
        Cvar& lang = Cvars::String("ui.lang", "en").SetFlags(Cvar::kRestartRequired);
        CHECK(!lang.RestartPending());
        lang.SetString("de");
        CHECK(lang.RestartPending() && Cvars::RestartPending().size() == 2);
        // A variable without the flag never reports pending.
        Cvar& plain = Cvars::Int("plain.n", 1);
        plain.SetInt(5);
        CHECK(!plain.RestartPending() && !plain.ReadOnly() && !plain.Cheat());

        // Unknown bits are dropped.
        Cvar& odd = Cvars::Int("odd.n", 1).SetFlags(0xFFu);
        CHECK(odd.Flags() == (Cvar::kReadOnly | Cvar::kCheat | Cvar::kRestartRequired));
        Cvars::Clear();
        CHECK(!Cvars::CheatsEnabled());                                         // Clear resets the switch too
    }

    // --- Cvar flags and the settings file ---
    {
        Cvars::Clear();
        const std::string path = (dir / "flags.cfg").string();
        Cvar& msaa = Cvars::Int("gfx.msaa", 4, "", 0, 8, true).SetFlags(Cvar::kRestartRequired);
        Cvar& ro = Cvars::Int("sys.ro", 1, "", 0, 0, true).SetFlags(Cvar::kReadOnly);
        Cvar& cheat = Cvars::Int("sys.cheat", 1, "", 0, 0, true).SetFlags(Cvar::kCheat);
        Cvar& norm = Cvars::Int("sys.norm", 1, "", 0, 0, true);
        msaa.SetInt(8); norm.SetInt(7);
        ro.SetInt(99); cheat.SetInt(99);                                        // code changed them
        CHECK(Cvars::Save(path));
        const std::string saved = ReadAll(path);
        CHECK(saved.find("gfx.msaa 8") != std::string::npos && saved.find("sys.norm 7") != std::string::npos);
        CHECK(saved.find("sys.ro") == std::string::npos && saved.find("sys.cheat") == std::string::npos);   // not the player's to keep
        // A hand-edited file cannot set them.
        PutFile(path, "gfx.msaa 2\nsys.ro 5\nsys.cheat 5\nsys.norm 3\n");
        Cvars::ResetAll();
        CHECK(Cvars::Load(path));
        CHECK(msaa.AsInt() == 2 && norm.AsInt() == 3 && ro.AsInt() == 1 && cheat.AsInt() == 1);
        CHECK(msaa.RestartPending());                                           // loaded after startup: not in use yet
        Cvars::MarkAllApplied();
        CHECK(!msaa.RestartPending());

        // Loaded before the variable exists (held), then registered read-only: the file's value does not stand.
        Cvars::Clear();
        PutFile(path, "late.ro 42\nlate.restart 9\nlate.plain 8\n");
        CHECK(Cvars::Load(path));
        Cvar& lro = Cvars::Int("late.ro", 1, "", 0, 0, true).SetFlags(Cvar::kReadOnly);
        Cvar& lrs = Cvars::Int("late.restart", 1, "", 0, 0, true).SetFlags(Cvar::kRestartRequired);
        Cvar& lpl = Cvars::Int("late.plain", 1, "", 0, 0, true);
        CHECK(lro.AsInt() == 1);
        CHECK(lrs.AsInt() == 9 && !lrs.RestartPending());                       // registration takes the loaded value as the one in use
        CHECK(lpl.AsInt() == 8);
        Cvars::Clear();
    }

    // --- Console: a command that runs itself, deep nesting, scripts with a byte order mark ---
    {
        Console::Reset();
        Console::Register("loop", [](const std::vector<std::string>&) { return Console::Execute("loop").output; });
        Console::Result r = Console::Execute("loop");              // used to overflow the stack
        CHECK(r.output.find("error:") == 0 && r.output.find("nested") != std::string::npos);
        CHECK(!r.ok);
        // The depth counter comes back down: ordinary lines run normally afterwards.
        CHECK(Console::Execute("echo fine").output == "fine");
        int depth = 0, maxDepth = 0;
        Console::Register("down", [&](const std::vector<std::string>& a) {
            ++depth; if (depth > maxDepth) maxDepth = depth;
            std::string out = "ok";
            if (!a.empty() && a[0] != "0") out = Console::Execute("down " + std::to_string(std::atoi(a[0].c_str()) - 1)).output;
            --depth;
            return out;
        });
        CHECK(Console::Execute("down 10").output == "ok");          // ten levels deep is fine
        CHECK(maxDepth == 11);

        std::vector<Console::Result> res = Console::ExecuteScript("\xEF\xBB\xBF" "echo first\necho second\n");
        CHECK(res.size() == 3 && res[0].ok && res[0].output == "first" && res[1].output == "second");
        Console::Reset();
    }

    // --- Console exec: run a script file ---
    {
        Console::Reset(); Cvars::Clear();
        Cvar& a = Cvars::Int("ex.a", 1);
        Cvar& b = Cvars::String("ex.b", "x");
        const std::string script = (dir / "autoexec.cfg").string();
        PutFile(script, "\xEF\xBB\xBF# a comment\nex.a 5\n\n// another\nex.b \"two words\"\nno_such_command\necho done\n");
        Console::Result r = Console::Execute("exec \"" + script + "\"");
        CHECK(r.ok && r.output == "ran 4 lines (1 failed)");        // the comments and the blank line are not counted
        CHECK(a.AsInt() == 5 && b.AsString() == "two words");        // the first line (after the byte order mark) ran
        bool echoed = false;
        for (const std::string& l : Console::Lines()) if (l == "done") echoed = true;
        CHECK(echoed);
        CHECK(Console::Execute("exec " + (dir / "missing.cfg").string()).output.find("error: cannot read") == 0);
        CHECK(!Console::Execute("exec").ok);
        CHECK(Console::HasCommand("exec"));

        // A script that runs itself ends at the nesting limit instead of overflowing the stack.
        const std::string selfPath = (dir / "self.cfg").string();
        PutFile(selfPath, "exec \"" + selfPath + "\"\n");
        r = Console::Execute("exec \"" + selfPath + "\"");
        CHECK(r.ok || !r.output.empty());
        CHECK(Console::Execute("echo still fine").output == "still fine");
        Console::Reset(); Cvars::Clear();
    }

    // --- DebugOverlay: a line provider that adds or removes lines while the overlay is being built ---
    {
        DebugOverlay::ClearLines();
        DebugOverlay::Stats st;
        int id = 0, runs = 0;
        id = DebugOverlay::AddLine([&]() -> std::string {
            ++runs;
            DebugOverlay::RemoveLine(id);                                      // a one-shot line removes itself ...
            for (int i = 0; i < 20; ++i) DebugOverlay::AddLine([] { return std::string("extra"); });   // ... and adds more
            return "once";
        });
        std::vector<std::string> first = DebugOverlay::BuildLines(st, false, true);   // used to walk a list it was changing
        CHECK(runs == 1);
        int once = 0, extra = 0;
        for (const std::string& l : first) { if (l == "once") ++once; if (l == "extra") ++extra; }
        CHECK(once == 1 && extra == 0);                                        // the new lines show from the next call
        std::vector<std::string> second = DebugOverlay::BuildLines(st, false, true);
        once = 0; extra = 0;
        for (const std::string& l : second) { if (l == "once") ++once; if (l == "extra") ++extra; }
        CHECK(runs == 1 && once == 0 && extra == 20);
        DebugOverlay::ClearLines();
    }

    // --- StateMachine: Change from inside an exit callback ---
    {
        StateMachine<int> sm;
        int exits = 0, enters2 = 0, enters3 = 0;
        std::vector<std::pair<int, int>> transitions;
        sm.Add(1, {nullptr, nullptr, [&] { ++exits; sm.Change(3); }});     // leaving 1 asks to go on to 3
        sm.Add(2, {[&] { ++enters2; }, nullptr, nullptr});
        sm.Add(3, {[&] { ++enters3; }, nullptr, nullptr});
        sm.OnTransition([&](const int& a, const int& b) { transitions.push_back({a, b}); });
        sm.Start(1);
        CHECK(sm.Change(2));                                               // used to recurse into the exit callback forever
        CHECK(exits == 1);
        CHECK(enters2 == 1 && enters3 == 1);                               // 2 was entered, then the request moved on to 3
        CHECK(sm.Is(3) && sm.Previous() == 2);
        CHECK(transitions.size() == 2);
        CHECK(sm.Change(1) && sm.Is(1));
        CHECK(sm.Change(2));
        CHECK(sm.Is(3));
        CHECK(exits == 2);

        // With no enter callbacks the request is still carried out, and Stop drops a request made while leaving.
        StateMachine<int> s2;
        int out2 = 0;
        s2.Add(1, {nullptr, nullptr, [&] { ++out2; s2.Change(9); }});
        s2.Start(1);
        CHECK(s2.Change(2) && s2.Is(9) && out2 == 1);
        StateMachine<int> s3;
        int out3 = 0;
        s3.Add(1, {nullptr, nullptr, [&] { ++out3; s3.Change(5); }});
        s3.Start(1);
        s3.Stop();
        CHECK(!s3.HasState() && out3 == 1);
        s3.Start(7);
        CHECK(s3.Is(7));                                                   // the stale request did not fire
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
