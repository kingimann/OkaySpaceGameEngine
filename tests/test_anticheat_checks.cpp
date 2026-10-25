#include "test_framework.hpp"
#include "AntiCheatChecks.hpp"
#include <cmath>

// Anti-cheat P3: the runtime checks. The probes are Windows-only, but the logic
// that decides what a probe's answer means runs (and is tested) everywhere.
namespace {
using namespace okayac;

const std::string kKey = "session key for this test";

// A verifier and a client whose clock runs `rate` times as fast as the
// verifier's; `rtt` ms between asking and hearing back.
struct Sim {
    ClockHeartbeat hb;
    double rate;
    std::uint64_t rtt;
    std::uint64_t serverMs = 1000000, clientBase = 5000;
    Sim(double r, std::uint64_t rt = 0) : hb(kKey), rate(r), rtt(rt) {}
    std::uint64_t ClientNow(std::uint64_t serverAt) const {
        return clientBase + (std::uint64_t)std::llround((double)(serverAt - 1000000) * rate);
    }
    // One round: challenge, the client answers at the half-way point, the reply lands.
    ClockHeartbeat::Verdict Round(std::uint64_t stepMs) {
        serverMs += stepMs;
        const ClockChallenge c = hb.Issue(serverMs);
        const ClockResponse r = AnswerChallenge(c, ClientNow(serverMs + rtt / 2), kKey);
        return hb.Accept(r, serverMs + rtt);
    }
    ClockHeartbeat::Verdict Run(int rounds, std::uint64_t stepMs) {
        ClockHeartbeat::Verdict v = ClockHeartbeat::Verdict::Pending;
        for (int i = 0; i < rounds; ++i) {
            const auto x = Round(stepMs);
            if (x != ClockHeartbeat::Verdict::Pending) v = x;
            if (x == ClockHeartbeat::Verdict::Drift || x == ClockHeartbeat::Verdict::BadResponse) return x;
        }
        return v;
    }
};

void TestClockDrift() {
    RUN_SUITE("clock drift heartbeat");
    using V = ClockHeartbeat::Verdict;
    CHECK(Sim(1.0).Run(10, 1000) == V::Ok);
    CHECK(Sim(1.02).Run(10, 1000) == V::Ok);      // inside 5%
    CHECK(Sim(0.97).Run(10, 1000) == V::Ok);
    CHECK(Sim(1.049).Run(10, 1000) == V::Ok);     // just inside
    CHECK(Sim(1.06).Run(10, 1000) == V::Drift);   // just over: flagged
    CHECK(Sim(0.93).Run(10, 1000) == V::Drift);   // slowed down too
    CHECK(Sim(2.0).Run(10, 1000) == V::Drift);    // a 2x speedhack
    {
        Sim s(1.10);
        CHECK(s.Run(10, 1000) == V::Drift);
        CHECK(std::fabs(s.hb.LastDrift() - 0.10) < 0.01);
        CHECK(s.hb.Flags() >= 1);
    }
    {   // Network delay alone (200 ms each way of jitter-free RTT) isn't drift.
        CHECK(Sim(1.0, 400).Run(10, 1000) == V::Ok);
        CHECK(Sim(1.03, 300).Run(10, 1000) == V::Ok);
        CHECK(Sim(1.20, 300).Run(10, 1000) == V::Drift);
    }
    {   // Too short a window to judge: still pending.
        Sim s(3.0);
        CHECK(s.Round(100) == V::Pending);
        CHECK(s.Round(100) == V::Pending);
    }
}

void TestChallengeResponse() {
    RUN_SUITE("clock heartbeat challenge-response");
    using V = ClockHeartbeat::Verdict;
    ClockHeartbeat hb(kKey);
    const ClockChallenge a = hb.Issue(1000);
    const ClockChallenge b = hb.Issue(1001);
    CHECK(a.nonce != b.nonce && b.seq == a.seq + 1);

    ClockResponse ok = AnswerChallenge(a, 50, kKey);
    // Wrong key (a forged answer), tampered time, unknown nonce, replay: all bad.
    CHECK(hb.Accept(AnswerChallenge(b, 51, "other key"), 1100) == V::BadResponse);
    ClockResponse forged = ok; forged.clientMs += 500;
    CHECK(hb.Accept(forged, 1100) == V::BadResponse);
    ClockResponse unknown = ok; unknown.nonce ^= 0x1234;
    CHECK(hb.Accept(unknown, 1100) == V::BadResponse);
    CHECK(hb.Accept(ok, 1100) == V::Pending);
    CHECK(hb.Accept(ok, 1200) == V::BadResponse);   // each challenge answers once
    CHECK(hb.BadResponses() == 4);
    // A challenge that's never answered expires instead of piling up.
    for (int i = 0; i < 100; ++i) hb.Issue(2000 + i);
    CHECK(hb.Outstanding() <= 16);
}

void TestDebugger() {
    RUN_SUITE("debugger detection (probes injected)");
    DebuggerProbes none;
    CHECK(CheckDebugger(none).empty());   // not checked (non-Windows): nothing reported
    DebuggerProbes clean;
    clean.local = [] { return false; };
    clean.remote = [] { return false; };
    CHECK(CheckDebugger(clean).empty());
    DebuggerProbes attached = clean;
    attached.local = [] { return true; };
    auto f = CheckDebugger(attached);
    CHECK(f.size() == 1 && f[0].check == "debugger");
    attached.remote = [] { return true; };
    CHECK(CheckDebugger(attached).size() == 2);
#if !defined(_WIN32)
    const DebuggerProbes sys = SystemDebuggerProbes();
    CHECK(!sys.local && !sys.remote);   // compiled out
    CHECK(LoadedModules().empty());
#endif
}

void TestModules() {
    RUN_SUITE("loaded-module allowlist");
    CHECK(NormalizeModulePath("C:\\Windows\\System32\\KERNEL32.DLL") == "c:/windows/system32/kernel32.dll");
    ModuleAllowlist al;
    AllowDir(al, "C:\\Windows");
    AllowFile(al, "D:\\Games\\My Game\\My Game.exe");
    AllowFile(al, "D:\\Games\\My Game\\SDL2.dll");
    AllowName(al, "GameOverlayRenderer64.dll");
    CHECK(ModuleAllowed(al, "C:\\Windows\\System32\\ntdll.dll"));
    CHECK(ModuleAllowed(al, "c:/windows/WinSxS/x/comctl32.dll"));
    CHECK(!ModuleAllowed(al, "C:\\WindowsEvil\\x.dll"));          // a prefix, not a folder
    CHECK(ModuleAllowed(al, "D:\\Games\\My Game\\sdl2.DLL"));      // case-insensitive (Windows)
    CHECK(!ModuleAllowed(al, "D:\\Games\\My Game\\version.dll"));   // beside the game, not shipped
    CHECK(ModuleAllowed(al, "C:\\Program Files (x86)\\Steam\\GameOverlayRenderer64.dll"));
    CHECK(!ModuleAllowed(al, "C:\\Users\\me\\AppData\\Local\\Temp\\speedhack-x86_64.dll"));
    CHECK(!ModuleAllowed(al, ""));

    const std::vector<std::string> loaded = {"D:\\Games\\My Game\\My Game.exe", "C:\\Windows\\System32\\ntdll.dll",
                                             "C:\\Users\\me\\AppData\\Local\\Temp\\speedhack-x86_64.dll",
                                             "D:\\Games\\My Game\\SDL2.dll"};
    const auto f = CheckModules(loaded, al);
    CHECK(f.size() == 1 && f[0].check == "module" && f[0].detail.find("speedhack") != std::string::npos);

    // Built from the verified manifest: the game's own exe / DLLs are allowed.
    Integrity m;
    m.files = {{"My Game.exe", 1, std::string(64, 'a')}, {"SDL2.dll", 1, std::string(64, 'b')},
               {"Data/game.okayscene", 1, std::string(64, 'c')}};
    const ModuleAllowlist d = DefaultModuleAllowlist("D:\\Games\\My Game", "C:\\Windows", &m);
    CHECK(ModuleAllowed(d, "D:\\Games\\My Game\\SDL2.dll") && ModuleAllowed(d, "C:\\Windows\\System32\\user32.dll"));
    CHECK(!ModuleAllowed(d, "D:\\Games\\My Game\\Data\\game.okayscene"));   // not code: not on the list
    CHECK(!ModuleAllowed(d, "D:\\Games\\My Game\\dinput8.dll"));
}

// The guard the player runs each frame, with every probe faked.
struct Fake {
    bool debugger = false;
    std::vector<std::string> modules = {"C:\\Windows\\System32\\ntdll.dll"};
    std::uint64_t trusted = 0, game = 0;
    double rate = 1.0;
    GuardProbes Probes() {
        GuardProbes p;
        p.debugger.local = [this] { return debugger; };
        p.debugger.remote = [] { return false; };
        p.modules = [this] { return modules; };
        p.trustedMs = [this] { return trusted; };
        p.gameMs = [this] { return game; };
        p.allowlist.trustedDirs.push_back("c:/windows/");
        return p;
    }
    void Advance(std::uint64_t ms) { trusted += ms; game += (std::uint64_t)std::llround(ms * rate); }
};

void TestGuard() {
    RUN_SUITE("runtime guard: report by default, enforce when set");
    {   // Off: no probe is even called.
        Fake fk;
        GuardProbes p = fk.Probes();
        int calls = 0;
        p.debugger.local = [&] { ++calls; return true; };
        RuntimeGuard g(Mode::Off, p);
        for (int i = 0; i < 50; ++i) { fk.Advance(500); CHECK(!g.Tick()); }
        CHECK(calls == 0 && g.Findings().empty());
    }
    {   // Report: a debugger is reported once, the game keeps running.
        Fake fk; fk.debugger = true;
        std::vector<std::string> logged;
        RuntimeGuard g(Mode::Report, fk.Probes(), [&](const std::string& s) { logged.push_back(s); });
        bool quit = false;
        for (int i = 0; i < 40; ++i) { fk.Advance(500); quit = quit || g.Tick(); }
        CHECK(!quit);
        CHECK(g.Findings().size() == 1 && g.Findings()[0].check == "debugger");
        CHECK(logged.size() == 1);
    }
    {   // Enforce: the same thing asks the game to quit.
        Fake fk; fk.debugger = true;
        RuntimeGuard g(Mode::Enforce, fk.Probes());
        bool quit = false;
        for (int i = 0; i < 40 && !quit; ++i) { fk.Advance(500); quit = g.Tick(); }
        CHECK(quit);
    }
    {   // An injected module, found on a later scan.
        Fake fk;
        RuntimeGuard g(Mode::Enforce, fk.Probes());
        for (int i = 0; i < 10; ++i) { fk.Advance(500); CHECK(!g.Tick()); }
        fk.modules.push_back("C:\\Temp\\inject.dll");
        bool quit = false;
        for (int i = 0; i < 200 && !quit; ++i) { fk.Advance(500); quit = g.Tick(); }
        CHECK(quit && g.Findings().back().check == "module");
    }
    {   // A sped-up game clock (speedhack) against the trusted clock.
        Fake fk; fk.rate = 1.5;
        RuntimeGuard g(Mode::Report, fk.Probes());
        for (int i = 0; i < 60; ++i) { fk.Advance(500); g.Tick(); }
        bool drift = false;
        for (const auto& f : g.Findings()) drift = drift || f.check == "clock";
        CHECK(drift);
    }
    {   // A normal clock: nothing at all.
        Fake fk; fk.rate = 1.01;
        RuntimeGuard g(Mode::Enforce, fk.Probes());
        bool quit = false;
        for (int i = 0; i < 120; ++i) { fk.Advance(500); quit = quit || g.Tick(); }
        CHECK(!quit && g.Findings().empty());
    }
}
} // namespace

int main() {
    TestClockDrift();
    TestChallengeResponse();
    TestDebugger();
    TestModules();
    TestGuard();
    TEST_MAIN_RESULT();
}
