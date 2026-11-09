#include "test_framework.hpp"
#include <Okay.hpp>
#include <filesystem>

using namespace okay;
using Strs = std::vector<std::string>;

int main() {
    RUN_SUITE("core_cli");

    // ---- CommandLine ----
    {
        CommandLine cl;
        cl.Flag("windowed", "run in a window").Short('w');
        cl.Flag("verbose", "chatty").Short('v');
        cl.Value("width", "N", "window width", "1280");
        cl.Value("log", "FILE", "log file");
        cl.Value("cvar", "NAME=VALUE", "set a cvar");
        cl.OptionalValue("find-match", "MODE", "matchmaking");
        Strs args = {"scene.okayscene", "--windowed", "--width=800", "--log", "out.txt", "--cvar", "a=1",
                     "--cvar=b=2", "extra", "--", "--not-an-option", "-x"};
        CHECK(cl.Parse(args));
        CHECK(cl.Has("windowed") && cl.GetBool("windowed"));
        CHECK(!cl.Has("verbose") && !cl.GetBool("verbose"));
        CHECK(cl.GetInt("width", 0) == 800);
        CHECK(cl.Get("log") == "out.txt");
        CHECK((cl.GetAll("cvar") == Strs{"a=1", "b=2"}));
        CHECK((cl.Positionals() == Strs{"scene.okayscene", "extra", "--not-an-option", "-x"}));   // positional anywhere; -- ends options
        CHECK(cl.Positional(0) == "scene.okayscene" && cl.Positional(9, "none") == "none");
    }
    {   // defaults, typed getters, bad values
        CommandLine cl;
        cl.Value("width", "N", "", "1280").Value("scale", "F").Flag("vsync");
        CHECK(cl.Parse(Strs{}));
        CHECK(cl.GetInt("width", 5) == 1280);           // declared default
        CHECK(cl.GetInt("scale", 7) == 7);              // caller default
        CHECK(cl.Parse(Strs{"--width=abc", "--scale", "-1.5"}));
        CHECK(cl.GetInt("width", 9) == 9);              // malformed -> default
        CHECK_NEAR(cl.GetFloat("scale", 0), -1.5, 1e-6);   // a Value takes a negative number
        int w = 0; CHECK(!cl.TryGetInt("width", 1, 100, w));
        CHECK(!cl.Error().empty());
        CHECK(cl.Parse(Strs{"--width=50"}));
        CHECK(cl.TryGetInt("width", 1, 100, w) && w == 50);
        CHECK(cl.Parse(Strs{"--width=500"}));
        CHECK(!cl.TryGetInt("width", 1, 100, w));
        CHECK(cl.Parse(Strs{"--vsync=false"})); CHECK(!cl.GetBool("vsync", true));
        CHECK(cl.Parse(Strs{"--vsync=on"}));    CHECK(cl.GetBool("vsync"));
    }
    {   // errors
        CommandLine cl; cl.Value("log", "FILE").Flag("a").Short('a').Flag("b").Short('b');
        CHECK(!cl.Parse(Strs{"--log"}));                CHECK(cl.Error().find("--log") != std::string::npos);
        CHECK(!cl.Parse(Strs{"--nope"}));               CHECK(cl.Error().find("--nope") != std::string::npos);
        CHECK(!cl.Parse(Strs{"-z"}));
        CHECK(cl.Parse(Strs{"-ab"}));                   CHECK(cl.Has("a") && cl.Has("b"));   // grouped short flags
        CHECK(cl.Parse(Strs{"-"}));                     CHECK(cl.Positional(0) == "-");      // lone dash is a positional
        cl.AllowUnknown();
        CHECK(cl.Parse(Strs{"--nope", "--k=v", "pos"}));
        CHECK(cl.Has("nope") && cl.Get("k") == "v" && cl.Positional(0) == "pos");
    }
    {   // optional value (--find-match [MODE])
        CommandLine cl; cl.OptionalValue("find-match", "MODE").Flag("x");
        CHECK(cl.Parse(Strs{"--find-match", "ranked", "scene"}));
        CHECK(cl.Get("find-match") == "ranked" && cl.Positional(0) == "scene");
        CHECK(cl.Parse(Strs{"--find-match", "--x"}));
        CHECK(cl.Has("find-match") && cl.Get("find-match") == "" && cl.Has("x"));
        CHECK(cl.Parse(Strs{"--find-match"}));
        CHECK(cl.Has("find-match"));
    }
    {   // argv form + usage text
        CommandLine cl;
        cl.Flag("windowed", "run in a window").Short('w').Value("width", "N", "window width", "1280").OptionalValue("find-match", "MODE");
        const char* argv[] = {"player", "-w", "game.okayscene"};
        CHECK(cl.Parse(3, argv));
        CHECK(cl.Has("windowed") && cl.Positional(0) == "game.okayscene");
        const std::string u = cl.Usage("player", "Runs a game.");
        CHECK(u.find("Usage: player") != std::string::npos);
        CHECK(u.find("-w, --windowed") != std::string::npos);
        CHECK(u.find("--width N") != std::string::npos && u.find("(default 1280)") != std::string::npos);
        CHECK(u.find("--find-match [MODE]") != std::string::npos);
        CHECK(u.find("Runs a game.") != std::string::npos);
    }

    // ---- Paths: every platform from a given environment ----
    {
        Paths::Env e; e.home = "/home/u"; e.appData = "C:\\Users\\u\\AppData\\Roaming"; e.localAppData = "C:\\Users\\u\\AppData\\Local";
        using P = Paths::Platform;
        CHECK(Paths::UserDataDirFor(P::Windows, e, "Game") == "C:/Users/u/AppData/Roaming/Game");
        CHECK(Paths::ConfigDirFor(P::Windows, e, "Game") == "C:/Users/u/AppData/Roaming/Game/config");
        CHECK(Paths::CacheDirFor(P::Windows, e, "Game") == "C:/Users/u/AppData/Local/Game/cache");
        CHECK(Paths::LogDirFor(P::Windows, e, "Game") == "C:/Users/u/AppData/Local/Game/logs");
        CHECK(Paths::UserDataDirFor(P::Linux, e, "Game") == "/home/u/.local/share/Game");
        CHECK(Paths::ConfigDirFor(P::Linux, e, "Game") == "/home/u/.config/Game");
        CHECK(Paths::CacheDirFor(P::Linux, e, "Game") == "/home/u/.cache/Game");
        CHECK(Paths::LogDirFor(P::Linux, e, "Game") == "/home/u/.local/state/Game/logs");
        e.xdgData = "/x/data"; e.xdgConfig = "/x/cfg"; e.xdgCache = "/x/cache"; e.xdgState = "/x/state/";
        CHECK(Paths::UserDataDirFor(P::Linux, e, "Game") == "/x/data/Game");
        CHECK(Paths::ConfigDirFor(P::Linux, e, "Game") == "/x/cfg/Game");
        CHECK(Paths::CacheDirFor(P::Linux, e, "Game") == "/x/cache/Game");
        CHECK(Paths::LogDirFor(P::Linux, e, "Game") == "/x/state/Game/logs");
        CHECK(Paths::UserDataDirFor(P::MacOS, e, "Game") == "/home/u/Library/Application Support/Game");
        CHECK(Paths::ConfigDirFor(P::MacOS, e, "Game") == "/home/u/Library/Application Support/Game/config");
        CHECK(Paths::CacheDirFor(P::MacOS, e, "Game") == "/home/u/Library/Caches/Game");
        CHECK(Paths::LogDirFor(P::MacOS, e, "Game") == "/home/u/Library/Logs/Game");
        Paths::Env bare; bare.home = "C:\\Users\\v";   // no APPDATA set
        CHECK(Paths::UserDataDirFor(P::Windows, bare, "G") == "C:/Users/v/AppData/Roaming/G");
        CHECK(Paths::CacheDirFor(P::Windows, bare, "G") == "C:/Users/v/AppData/Local/G/cache");
    }
    {   // current platform, app name, EnsureDir, Join
        Paths::SetAppName("OkayTestApp");
        CHECK(Paths::AppName() == "OkayTestApp");
        CHECK(Paths::UserDataDir().find("OkayTestApp") != std::string::npos);
        CHECK(Paths::LogDir().find("OkayTestApp") != std::string::npos);
        CHECK(Paths::Join("a", "b") == "a/b" && Paths::Join("a/", "b") == "a/b" && Paths::Join("a/", "/b") == "a/b");
        CHECK(Paths::Join("", "b") == "b" && Paths::Join("a", "") == "a");
        const std::string d = (std::filesystem::temp_directory_path() / "okay_paths_test" / "x" / "y").string();
        std::filesystem::remove_all(std::filesystem::temp_directory_path() / "okay_paths_test");
        CHECK(Paths::EnsureDir(d) && std::filesystem::is_directory(d));
        std::filesystem::remove_all(std::filesystem::temp_directory_path() / "okay_paths_test");
        Paths::SetAppName("OkaySpace");
    }
    {   // asset base dir: one place, listeners notified, Resolve
        Paths::SetAssetBaseDir("");
        std::string seen = "unset"; int calls = 0;
        const int id = Paths::AddAssetBaseDirListener([&](const std::string& d) { seen = d; ++calls; });
        CHECK(calls == 1 && seen.empty());               // told the current value straight away
        Paths::SetAssetBaseDir("/game/Data");
        CHECK(Paths::AssetBaseDir() == "/game/Data" && seen == "/game/Data" && calls == 2);
        CHECK(Paths::Resolve("scenes/a.okayscene") == "/game/Data/scenes/a.okayscene");
        CHECK(Paths::Resolve("/abs/file") == "/abs/file");
        CHECK(Paths::Resolve("C:/abs/file") == "C:/abs/file");
        CHECK(Paths::IsAbsolute("D:\\x") && !Paths::IsAbsolute("rel/x") && !Paths::IsAbsolute(""));
        Paths::RemoveAssetBaseDirListener(id);
        Paths::SetAssetBaseDir("/other");
        CHECK(calls == 2);
        Paths::SetAssetBaseDir("");
        CHECK(Paths::Resolve("a") == "a");
    }

    // ---- BuildInfo ----
    {
        CHECK(std::string(BuildInfo::Version()).size() > 0);
        CHECK(std::string(BuildInfo::Version()) != "dev");           // wired to the CMake version
        CHECK(std::string(BuildInfo::Version()) == std::string(OKAY_ENGINE_VERSION_FOR_TEST));
        CHECK(std::string(BuildInfo::Compiler()).size() > 0);
        CHECK(std::string(BuildInfo::Config()) == "Debug" || std::string(BuildInfo::Config()) == "Release");
        CHECK(std::string(BuildInfo::Platform()).size() > 0);
        CHECK(BuildInfo::Summary().find(BuildInfo::Version()) != std::string::npos);
        int a, b, c;
        CHECK(BuildInfo::ParseVersion("2.847.1", a, b, c) && a == 2 && b == 847 && c == 1);
        CHECK(BuildInfo::ParseVersion("3.1", a, b, c) && a == 3 && b == 1 && c == 0);
        CHECK(!BuildInfo::ParseVersion("dev", a, b, c) && !BuildInfo::ParseVersion("1.2.3.4", a, b, c) && !BuildInfo::ParseVersion("1..2", a, b, c));
        CHECK(BuildInfo::CompareVersions("2.10.0", "2.9.9") > 0);
        CHECK(BuildInfo::CompareVersions("1.0", "1.0.0") == 0);
        CHECK(BuildInfo::CompareVersions("dev", "1.0.0") < 0);
        CHECK(BuildInfo::IsCompatible(BuildInfo::Version()));
        CHECK(!BuildInfo::IsCompatible("0.0.1"));
    }

    // ---- Stopwatch (driven by a fake clock) ----
    {
        double now = 100.0;
        Stopwatch sw([&] { return now; });
        CHECK(!sw.IsRunning() && sw.Elapsed() == 0.0f);
        sw.Start(); now += 2.0;
        CHECK_NEAR(sw.Elapsed(), 2.0, 1e-6);
        CHECK_NEAR(sw.Lap(), 2.0, 1e-6);
        now += 0.5;
        CHECK_NEAR(sw.Lap(), 0.5, 1e-6);
        sw.Stop(); now += 10.0;
        CHECK_NEAR(sw.Elapsed(), 2.5, 1e-6);                        // stopped: frozen
        sw.Start(); now += 1.0;
        CHECK_NEAR(sw.Elapsed(), 3.5, 1e-6);                        // resumes, keeps the total
        sw.Start(); now += 1.0;                                     // Start while running: no reset
        CHECK_NEAR(sw.ElapsedMilliseconds(), 4500.0, 1e-3);
        CHECK(sw.Laps().size() == 2);
        sw.Restart();
        CHECK(sw.IsRunning() && sw.Elapsed() == 0.0f && sw.Laps().empty());
        sw.Reset(); CHECK(!sw.IsRunning());
        Stopwatch real = Stopwatch::StartNew();
        CHECK(real.IsRunning() && real.Elapsed() >= 0.0f);
        Stopwatch game = Stopwatch::GameTime();
        game.Start();
        const float before = game.Elapsed();
        CHECK(game.Elapsed() >= before);
    }

    // ---- StateMachine ----
    {
        enum class S { Idle, Run, Jump };
        std::string log;
        StateMachine<S> sm;
        sm.Add(S::Idle, {[&] { log += "+I "; }, [&](float) { log += "uI "; }, [&] { log += "-I "; }});
        sm.Add(S::Run,  {[&] { log += "+R "; }, nullptr, [&] { log += "-R "; }});
        CHECK(!sm.HasState());
        sm.Update(1.0f);                                  // no state: nothing happens
        sm.Start(S::Idle);
        CHECK(sm.HasState() && sm.Is(S::Idle) && log == "+I ");
        sm.Update(0.5f); sm.Update(0.25f);
        CHECK_NEAR(sm.TimeInState(), 0.75, 1e-6);
        CHECK(!sm.Change(S::Idle));                       // same state: no-op
        CHECK(sm.Change(S::Run));
        CHECK(log == "+I uI uI -I +R ");
        CHECK(sm.Current() == S::Run && sm.Previous() == S::Idle && sm.TimeInState() == 0.0f);
        sm.Update(1.0f);                                  // Run has no update callback: fine
        CHECK(sm.Change(S::Jump));                        // a state with no handlers works
        CHECK(sm.Is(S::Jump));
        sm.Stop(); CHECK(!sm.HasState());
    }
    {   // restricted transitions, callback, change from an enter callback
        StateMachine<std::string> sm;
        std::string trace;
        sm.Allow("a", "b"); sm.Allow("b", "c");
        sm.OnTransition([&](const std::string& f, const std::string& t) { trace += f + ">" + t + " "; });
        sm.Start("a");
        CHECK(!sm.CanChange("c") && !sm.Change("c") && sm.Is("a"));
        CHECK(sm.Change("b") && sm.Change("c"));
        CHECK(sm.CanChange("zzz"));                       // "c" has no Allow list: unrestricted
        CHECK(trace == "a>b b>c ");

        StateMachine<int> chain;
        std::string order;
        chain.Add(1, {[&] { order += "e1 "; chain.Change(2); order += "after "; }, nullptr, [&] { order += "x1 "; }});
        chain.Add(2, {[&] { order += "e2 "; }, nullptr, nullptr});
        chain.Start(1);
        CHECK(chain.Is(2));
        CHECK(order == "e1 after x1 e2 ");               // the nested change waits for the enter callback to return
    }

    TEST_MAIN_RESULT();
}
