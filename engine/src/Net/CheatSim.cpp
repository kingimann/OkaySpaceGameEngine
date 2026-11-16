#include "okay/Net/CheatSim.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/Packet.hpp"
#include "okay/Net/UdpSocket.hpp"
#include "okay/Net/AntiCheatLog.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Core/Log.hpp"
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>   // Sleep: MinGW's win32 thread model has no <thread> (CLAUDE.md rule 21a)
#else
#  include <unistd.h>
#endif

namespace okay {

namespace fs = std::filesystem;

const char* CheatSimDefaultRules() {
    return
        "enabled true\n"
        "maxSyncSpeed 8\n"
        "maxRpcsPerSecond 10\n"
        "kickAfterViolations 4\n"
        "serverVar score\n"
        "clientVar player.\n"
        "weapon rifle\n"
        "maxHitRange 100\n"
        "detect.enabled true\n"
        "detect.maxSpeed 8\n"
        "detect.kickAt 60\n"
        "detect.banAt 140\n";
}

namespace {

// Message numbers of the wire protocol (the same values as NetworkManager.cpp's private enum).
enum : std::uint8_t { kJoin = 1, kWelcome = 2, kState = 3, kLeave = 5, kMessage = 6, kSyncVar = 8, kReject = 12, kKicked = 13 };

void Nap(int ms = 1) {
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000u);
#endif
}

// One headless server with the rules on, and what it reported.
struct Server {
    Scene scene{"CheatSimServer"};
    NetworkManager* net = nullptr;
    std::vector<CheatReport> reports;
    std::vector<NetworkManager::AntiCheatKick> kicks;
    std::map<std::string, int> evidence;   // detector evidence by kind name
    bool Open(const std::string& rules, const std::string& dir) {
        scene.physicsEnabled = false;
        net = scene.CreateGameObject("Net")->AddComponent<NetworkManager>();
        net->interpolationRate = 0.0f;
        net->maxMoveSpeed = 8.0f;       // the move check: faster is corrected and counted
        net->maxMessagesPerSecond = 0;  // the anti-cheat's own limits are the ones on show
        net->encryption = false;        // plain packets: the simulator is a logic demo, not a crypto test
        AntiCheatSettings s;
        std::string err;
        if (!AntiCheatSettings::Parse(rules, s, &err)) { lastError = err; return false; }
        if (s.banFile.empty()) s.banFile = dir + "/okay_bans.txt";
        if (s.auditLog.empty()) s.auditLog = dir + "/okay_anticheat_audit.log";
        net->anticheat.settings = s;
        net->SetCheatHandler([this](const CheatReport& r) { reports.push_back(r); });
        net->anticheat.detector.onEvidence = [this](std::uint32_t, const Evidence& e) { ++evidence[DetectKindName(e.kind)]; };
        net->SetAntiCheatKickHandler([this](const NetworkManager::AntiCheatKick& k) { kicks.push_back(k); });
        if (!net->StartServer(0)) { lastError = "couldn't open a UDP port on this machine"; return false; }
        scene.Start();
        return true;
    }
    void Pump(int frames, float dt = 0.02f) { for (int i = 0; i < frames; ++i) { scene.Update(dt); Nap(); } }
    ~Server() { if (net) net->Stop(); }
    std::string lastError;
};

// A client that is only packets: it can say anything a modified game could.
struct Rogue {
    net::UdpSocket sock;
    net::Endpoint to{};
    std::uint32_t id = 0;
    bool welcomed = false, kicked = false, rejected = false;
    std::string rejectReason, kickReason;
    bool Open(std::uint16_t port) {
        if (!sock.Open() || !sock.Bind(0)) return false;
        to = net::Endpoint{0x7F000001u, port};
        return true;
    }
    void Send(const net::Packet& p) { sock.SendTo(to, p.Data(), p.Size()); }
    void Join(const std::string& name) {
        net::Packet p(kJoin);
        p.Write(name); p.Write(std::string()); p.Write(std::string()); p.Write(std::string()); p.Write(std::string());
        Send(p);
    }
    void Move(float x, float y, float z) {
        net::Packet p(kState);
        p.Write(id); p.Write(x); p.Write(y); p.Write(z); p.Write(std::uint8_t('@'));
        Send(p);
    }
    void Msg(const std::string& channel, const std::string& data) {
        net::Packet p(kMessage);
        p.Write(id); p.Write(channel); p.Write(data);
        Send(p);
    }
    void Var(const std::string& key, const std::string& value) {
        net::Packet p(kSyncVar);
        p.Write(key); p.Write(value);
        Send(p);
    }
    void Listen() {
        std::uint8_t buf[2048]; net::Endpoint from; int n;
        while ((n = sock.RecvFrom(buf, sizeof buf, from)) > 0) {
            net::Packet p(buf, (std::size_t)n);
            const std::uint8_t t = p.ReadU8();
            if (t == kWelcome) { id = p.ReadU32(); welcomed = true; }
            else if (t == kReject) { rejected = true; rejectReason = p.ReadString(); }
            else if (t == kKicked) { kicked = true; kickReason = p.ReadString(); }
        }
    }
};

// A normal player: the real client class, chatting politely.
struct Honest {
    Scene scene{"CheatSimHonest"};
    NetworkManager* net = nullptr;
    bool Open(std::uint16_t port) {
        scene.physicsEnabled = false;
        net = scene.CreateGameObject("Net")->AddComponent<NetworkManager>();
        net->interpolationRate = 0.0f;
        net->encryption = false;
        if (!net->StartClient("127.0.0.1", port)) return false;
        scene.Start();
        return true;
    }
    ~Honest() { if (net) net->Stop(); }
};

struct Scenario {
    const char* name;
    const char* what;
};
const Scenario kScenarios[] = {
    {"speed", "moves 60 units a second where 8 is the limit"},
    {"teleport", "jumps 500 units in one step, again and again"},
    {"garbage", "sends positions and messages made of NaN, infinity and huge numbers"},
    {"servervar", "writes the server-only variable 'score' to give itself points"},
    {"flood", "sends 300 chat messages in a burst"},
    {"hitclaim", "claims hits with a weapon the server doesn't know"},
    {"macro", "presses a button 30 times at exactly 250 ms (a macro; fed to the server's detector, no packets)"},
    {"ban", "keeps cheating until it is banned, then the server restarts and it tries to rejoin"},
};

std::string Summary(const Server& s, const Rogue& r) {
    std::map<std::string, int> kinds;
    for (const auto& c : s.reports) ++kinds[CheatKindName(c.kind)];
    std::string out;
    for (const auto& [k, n] : kinds) out += (out.empty() ? "" : ", ") + k + " x" + std::to_string(n);
    for (const auto& [k, n] : s.evidence) out += (out.empty() ? "" : ", ") + k + " x" + std::to_string(n);
    if (s.net->HitsRefused() > 0) out += (out.empty() ? "" : ", ") + std::string("hit claims refused x") + std::to_string(s.net->HitsRefused()) + " (no damage done)";
    if (r.kicked) out += (out.empty() ? "" : ", ") + std::string("KICKED (") + r.kickReason + ")";
    if (r.rejected) out += (out.empty() ? "" : ", ") + std::string("REFUSED (") + r.rejectReason + ")";
    return out.empty() ? "nothing" : out;
}

std::string ReadAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

} // namespace

CheatSimReport RunCheatSim(const CheatSimOptions& opts) {
    CheatSimReport report;
    net::Startup();   // (counted; the sockets below need it on Windows)
    auto say = [&](const std::string& line) { if (opts.out) { std::fprintf(opts.out, "%s\n", line.c_str()); std::fflush(opts.out); } };

    std::string dir = opts.dir;
    if (dir.empty()) {
        std::error_code ec;
        std::random_device rd;
        dir = (fs::temp_directory_path(ec) / ("okay_cheatsim_" + std::to_string(rd()))).string();
    }
    { std::error_code ec; fs::create_directories(dir, ec); }

    auto wanted = [&](const std::string& n) {
        if (opts.only.empty()) return true;
        std::stringstream ss(opts.only); std::string tok;
        while (std::getline(ss, tok, ',')) if (tok == n) return true;
        return false;
    };
    const std::string rules = opts.rules.empty() ? CheatSimDefaultRules() : opts.rules;
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};   // the server's own log lines would bury the report

    say("Cheat simulator: a server on this machine with the anti-cheat on, and a rogue client that cheats on purpose.");
    say("Nothing leaves this machine. Files: " + dir);
    for (const Scenario& sc : kScenarios) {
        if (!wanted(sc.name)) continue;
        CheatSimResult res;
        res.name = sc.name; res.what = sc.what;
        const std::string sdir = dir + "/" + sc.name;
        { std::error_code ec; fs::remove_all(sdir, ec); fs::create_directories(sdir, ec); }
        say(std::string("\n[") + sc.name + "] the rogue " + sc.what);

        if (res.name == "macro") {
            // Scripted into the detector: a real macro needs a player pawn and the game's input path.
            Server s;
            if (!s.Open(rules, sdir)) { res.how = "server did not start: " + s.lastError; report.results.push_back(res); say("    FAILED: " + res.how); continue; }
            s.net->anticheat.ApplyDetectSettings();
            CheatDetector& d = s.net->anticheat.detector;
            d.NoteJoin(9, 0.0);
            for (int i = 0; i < 30; ++i) d.OnPress(9, 0, 10.0 + i * 0.25, 10.0 + i * 0.25);
            res.caught = d.Count(9, DetectKind::InputRegularity) > 0;
            res.how = res.caught ? "input_regularity x" + std::to_string(d.Count(9, DetectKind::InputRegularity)) + ", risk " + std::to_string((int)d.Risk(9)) : "nothing";
            // A hand: the same button at 250 ms on average, give or take 60 ms.
            CheatDetector h; h.settings = d.settings; h.NoteJoin(1, 0.0);
            std::mt19937 g(7); std::uniform_real_distribution<double> j(-0.06, 0.06);
            double t = 10.0;
            for (int i = 0; i < 60; ++i) { t += 0.25 + j(g); h.OnPress(1, 0, t, t); }
            res.honestHarmed = h.Count(1, DetectKind::InputRegularity) > 0;
            report.results.push_back(res);
            say(std::string("    ") + (res.caught ? "CAUGHT" : "MISSED") + ": " + res.how + (res.honestHarmed ? "   (but a human-like presser was flagged too)" : "   (a human-like presser was left alone)"));
            continue;
        }

        const bool banScenario = res.name == "ban";
        std::string r2 = rules;
        if (banScenario) r2 += "banAfterKicks 1\n";
        auto srv = std::make_unique<Server>();
        if (!srv->Open(r2, sdir)) { res.how = "server did not start: " + srv->lastError; report.results.push_back(res); say("    FAILED: " + res.how); continue; }
        Honest honest;
        Rogue rogue;
        if (!honest.Open(srv->net->ServerPort()) || !rogue.Open(srv->net->ServerPort())) { res.how = "couldn't open sockets"; report.results.push_back(res); say("    FAILED: " + res.how); continue; }
        auto pump = [&](int n) { for (int i = 0; i < n; ++i) { srv->scene.Update(0.02f); honest.scene.Update(0.02f); Nap(); rogue.Listen(); } };
        rogue.Join("Rogue");
        for (int i = 0; i < 300 && !(rogue.welcomed && honest.net->Joined()); ++i) pump(1);
        if (!rogue.welcomed || !honest.net->Joined()) { res.how = "couldn't join the local server"; report.results.push_back(res); say("    FAILED: " + res.how); continue; }
        pump(60);   // past the join warm-up
        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();

        if (res.name == "speed") {
            float x = 0.0f;
            for (int i = 0; i < 200 && !rogue.kicked; ++i) { x += 1.2f; rogue.Move(x, 0.0f, 0.0f); pump(1); }
        } else if (res.name == "teleport") {
            float x = 0.0f;
            for (int i = 0; i < 60 && !rogue.kicked; ++i) { x += 500.0f; rogue.Move(x, 0.0f, 0.0f); pump(5); }
        } else if (res.name == "garbage") {
            for (int i = 0; i < 80 && !rogue.kicked; ++i) {
                rogue.Move(i % 2 ? nan : inf, 1e30f, -1e30f);
                rogue.Msg("__xform", "x|" + std::to_string(nan) + " " + std::to_string(inf) + " 1e999 0 0 0 1");
                rogue.Msg("", std::string(4000, '\xff'));
                rogue.Var("", "x"); rogue.Var(std::string(5000, 'k'), "v");
                pump(1);
            }
        } else if (res.name == "servervar") {
            for (int i = 0; i < 30 && !rogue.kicked; ++i) { rogue.Var("score", std::to_string(1000000 + i)); pump(2); }
        } else if (res.name == "flood") {
            for (int i = 0; i < 300 && !rogue.kicked; ++i) { rogue.Msg("__rpc/chat", "spam " + std::to_string(i)); if (i % 10 == 0) pump(1); }
            pump(5);
        } else if (res.name == "hitclaim") {
            for (int i = 0; i < 20 && !rogue.kicked; ++i) { rogue.Msg("__hit", "1|plasma_cannon_9000|0|0 0 0|1 0 0"); pump(2); }
        } else if (banScenario) {
            float x = 0.0f;
            for (int i = 0; i < 400 && !rogue.kicked; ++i) { x += 5.0f; rogue.Move(x, 0.0f, 0.0f); pump(1); }
        }
        pump(10);
        res.caught = rogue.kicked || !srv->reports.empty() || !srv->evidence.empty() || srv->net->HitsRefused() > 0;
        res.how = Summary(*srv, rogue);
        res.honestHarmed = honest.net->WasKicked() || !honest.net->Joined();
        say(std::string("    ") + (res.caught ? "CAUGHT" : "MISSED") + ": " + res.how);
        say(std::string("    the normal client next to it: ") + (res.honestHarmed ? "HARMED (kicked or dropped)" : "untouched, still playing"));

        if (banScenario) {
            // Restart the server (a new process would do the same): the ban is in the file, not in memory.
            srv->net->Stop();
            srv.reset();
            auto srv2 = std::make_unique<Server>();
            if (srv2->Open(r2, sdir)) {
                Rogue again;
                again.Open(srv2->net->ServerPort());
                again.Join("Rogue");
                for (int i = 0; i < 200 && !again.rejected && !again.welcomed; ++i) { srv2->scene.Update(0.02f); Nap(); again.Listen(); }
                const bool refused = again.rejected && again.rejectReason.find("banned") != std::string::npos;
                say(std::string("    after a server restart the rogue tries again: ") + (refused ? "REFUSED (" + again.rejectReason + ")" : again.welcomed ? "LET IN (the ban did not hold)" : "no answer"));
                res.caught = res.caught && refused;
                res.how += refused ? "; after a restart: refused (" + again.rejectReason + ")" : "; after a restart: NOT refused";
            } else res.caught = false;
            report.banFile = sdir + "/okay_bans.txt";
            report.auditFile = sdir + "/okay_anticheat_audit.log";
            report.auditText = ReadAll(report.auditFile);
            say("\nThe ban list file (" + report.banFile + "):");
            say(ReadAll(report.banFile));
            say("The audit log (" + report.auditFile + "); players appear only as a short tag, never a name, address or account id:");
            say(report.auditText);
        }
        report.results.push_back(res);
    }
    Log::sink = sink;
    net::Shutdown();
    int caught = 0;
    for (const auto& r : report.results) if (r.caught && !r.honestHarmed) ++caught;
    say("\nResult: " + std::to_string(caught) + " of " + std::to_string(report.results.size()) + " cheats caught, normal players left alone: " +
        (report.Ok() ? "YES. The server's anti-cheat works." : "NO. See the lines marked MISSED or HARMED above."));
    return report;
}

} // namespace okay
