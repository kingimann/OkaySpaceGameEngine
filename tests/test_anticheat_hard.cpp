// Anti-cheat hardening (2026-11 audit follow-up): the ban list file, the audit log, the packet ceiling,
// the input-regularity (bot / macro) detector, the cheat simulator, and a hostile-field fuzz of a live
// server. See docs/anticheat_hardening_status.md for the item list. Headless; loopback only.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/AntiCheat.hpp"
#include "okay/Net/AntiCheatLog.hpp"
#include "okay/Net/CheatDetect.hpp"
#include "okay/Net/CheatSim.hpp"
#include "okay/Net/DedicatedServer.hpp"
#include "okay/Net/Packet.hpp"
#include "okay/Net/UdpSocket.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <thread>

using namespace okay;
namespace fs = std::filesystem;

namespace {
std::string TmpDir(const char* name) {
    std::random_device rd;
    const fs::path d = fs::temp_directory_path() / (std::string("okay_hard_") + name + "_" + std::to_string(rd()));
    fs::create_directories(d);
    return d.string();
}
std::string Slurp(const std::string& path) { std::ifstream f(path, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
void Spit(const std::string& path, const std::string& text) { std::ofstream f(path, std::ios::binary | std::ios::trunc); f << text; }
void Nap(int ms = 1) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// A client that is only packets (the wire numbers are NetworkManager's).
struct Rogue {
    net::UdpSocket sock; net::Endpoint to{};
    std::uint32_t id = 0; bool welcomed = false, kicked = false, rejected = false; std::string rejectReason;
    bool Open(std::uint16_t port) { if (!sock.Open() || !sock.Bind(0)) return false; to = net::Endpoint{0x7F000001u, port}; return true; }
    void Send(const net::Packet& p) { sock.SendTo(to, p.Data(), p.Size()); }
    void Join(const std::string& name = "Rogue") {
        net::Packet p(1); p.Write(name); for (int i = 0; i < 4; ++i) p.Write(std::string());
        Send(p);
    }
    void Listen() {
        std::uint8_t buf[2048]; net::Endpoint from; int n;
        while ((n = sock.RecvFrom(buf, sizeof buf, from)) > 0) {
            net::Packet p(buf, (std::size_t)n);
            const std::uint8_t t = p.ReadU8();
            if (t == 2) { id = p.ReadU32(); welcomed = true; }
            else if (t == 12) { rejected = true; rejectReason = p.ReadString(); }
            else if (t == 13) kicked = true;
        }
    }
};

struct Srv {
    Scene scene{"S"};
    NetworkManager* net = nullptr;
    std::vector<CheatReport> reports;
    bool Open(const std::string& rules) {
        scene.physicsEnabled = false;
        net = scene.CreateGameObject("Net")->AddComponent<NetworkManager>();
        net->interpolationRate = 0.0f; net->encryption = false; net->maxMoveSpeed = 8.0f;
        AntiCheatSettings s; std::string err;
        if (!AntiCheatSettings::Parse(rules, s, &err)) { std::cerr << "rules: " << err << "\n"; return false; }
        net->anticheat.settings = s;
        net->SetCheatHandler([this](const CheatReport& r) { reports.push_back(r); });
        if (!net->StartServer(0)) return false;
        scene.Start();
        return true;
    }
    void Pump(int n) { for (int i = 0; i < n; ++i) { scene.Update(0.02f); Nap(); } }
    ~Srv() { if (net) net->Stop(); }
};
} // namespace

int main() {
    RUN_SUITE("anticheat_hard");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // ---- ban list file -------------------------------------------------------------------------
    {
        const std::string dir = TmpDir("ban");
        const std::string path = dir + "/bans.txt";
        BanList b;
        CHECK(b.Load(path));                       // a missing file is an empty list
        CHECK(b.Size() == 0);
        CHECK(b.Add("u:alice-1", 1000, 0.0, "kicked 3 times"));          // forever
        CHECK(b.Add("a:2130706433", 1000, 1.0, "tab\there\nnewline"));   // one hour; the reason can't break the file
        CHECK(!b.Add("bad ident", 1000, 0.0, "x"));
        CHECK(!b.Add("u:", 1000, 0.0, "x"));
        CHECK(!b.Add("x:abc", 1000, 0.0, "x"));
        CHECK(!b.Add("u:has\ttab", 1000, 0.0, "x"));
        CHECK(b.Save());
        BanList c;
        CHECK(c.Load(path));
        CHECK(c.Size() == 2 && c.Skipped() == 0);
        CHECK(c.IsBanned("u:alice-1", 5000000));                   // forever
        CHECK(c.IsBanned("a:2130706433", 1000 + 3599));
        CHECK(!c.IsBanned("a:2130706433", 1000 + 3600));           // ended
        CHECK(!c.IsBanned("u:bob", 1000));
        BanEntry e; CHECK(c.IsBanned("a:2130706433", 1500, &e) && e.reason.find('\t') == std::string::npos && e.reason.find('\n') == std::string::npos);
        // hand edits and damage: unreadable lines are skipped, readable ones stay
        Spit(path, "# comment\nu:ok\t5\t0\tby hand\ngarbage line\nu:bad\tNaN\t0\tx\n\n" + std::string(2000, 'z') + "\nu:two\t5\t0\n");
        BanList d;
        CHECK(d.Load(path));
        CHECK(d.Size() == 2 && d.Skipped() == 3);
        CHECK(d.IsBanned("u:ok", 10) && d.IsBanned("u:two", 10));
        // a longer ban is not shortened by a later short one; removing lifts it
        BanList f; f.Add("u:x", 0, 0.0, "forever"); f.Add("u:x", 10, 1.0, "short");
        CHECK(f.IsBanned("u:x", 99999999));
        CHECK(f.Remove("u:x") && !f.IsBanned("u:x", 0));
        // bounded: a flood of bans can't grow without end
        BanList g;
        for (std::size_t i = 0; i < BanList::kMaxEntries + 50; ++i) g.Add("u:n" + std::to_string(i), 0, 0.0, "x");
        CHECK(g.Size() == BanList::kMaxEntries);
        // a file that is a directory can't be read: a clear error, no crash
        BanList h; std::string err; CHECK(!h.Load(dir, &err) && !err.empty());
    }

    // ---- audit log -----------------------------------------------------------------------------
    {
        const std::string dir = TmpDir("audit");
        const std::string path = dir + "/audit.log";
        AuditLog a; a.Open(path);
        a.Write(1700000000, "cheat", "u:alice-account-id", "kind=speed hack peer=3 x\nforged line");
        a.Write(1700000001, "kick", "a:2130706433", "");
        a.Write(1700000002, "start", "", "");
        const std::string text = Slurp(path);
        CHECK(a.Lines() == 3);
        CHECK(text.find("alice") == std::string::npos && text.find("2130706433") == std::string::npos);   // no account id, no address
        CHECK(text.find("2023-11-14T22:13:20Z cheat who=") == 0);
        CHECK(text.find("who=" + a.Tag("u:alice-account-id")) != std::string::npos);
        CHECK(a.Tag("u:alice-account-id") != a.Tag("u:bob") && a.Tag("u:alice-account-id").size() == 10);
        std::size_t lines = 0; for (char ch : text) lines += ch == '\n';
        CHECK(lines == 3);                                                                                  // a newline in a detail can't forge a line
        AuditLog again; again.Open(path);
        CHECK(again.Tag("u:alice-account-id") == a.Tag("u:alice-account-id"));                              // the tag survives a restart
        AuditLog small; small.Open(dir + "/small.log", 300);                                                // rotation keeps the file bounded
        for (int i = 0; i < 50; ++i) small.Write(1700000000 + i, "cheat", "u:x", "kind=speed hack peer=1");
        CHECK(fs::file_size(dir + "/small.log") <= 300 && fs::exists(dir + "/small.log.1"));
        AuditLog off; off.Write(0, "x", "", ""); CHECK(!off.IsOpen() && off.Lines() == 0);
    }

    // ---- settings ------------------------------------------------------------------------------
    {
        AntiCheatSettings s; std::string err;
        CHECK(AntiCheatSettings::Parse("enabled true\nbanFile bans.txt\nauditLog a.log\nbanAfterKicks 2\nbanHours 12\nbanOnRequest false\nmaxPacketsPerSecond 300\n", s, &err));
        CHECK(s.banFile == "bans.txt" && s.auditLog == "a.log" && s.banAfterKicks == 2 && s.banHours == 12.0f && !s.banOnRequest && s.maxPacketsPerSecond == 300);
        AntiCheatSettings t;
        CHECK(AntiCheatSettings::Parse(s.ToText(), t, &err));
        CHECK(t.banFile == "bans.txt" && t.auditLog == "a.log" && t.banAfterKicks == 2 && t.banHours == 12.0f && !t.banOnRequest && t.maxPacketsPerSecond == 300);
        AntiCheatSettings d;
        CHECK(d.banAfterKicks == 3 && d.banHours == 168.0f && d.banOnRequest && d.maxPacketsPerSecond == 600 && d.banFile.empty());
        CHECK(!AntiCheatSettings::Parse("banHours abc\n", s, &err));
        CHECK(!AntiCheatSettings::Parse("banAfterKicks -1\n", s, &err));
        CHECK(!AntiCheatSettings::Parse("maxPacketsPerSecond 1.5\n", s, &err));
        CHECK(!AntiCheatSettings::Parse("banOnRequest maybe\n", s, &err));
        CHECK(AntiCheatSettings::Parse("enabled true\ndetect.regularityPresses 12\ndetect.regularityMaxCv 0.05\n", s, &err));
        CHECK(s.detect.regularityPresses == 12 && s.detect.regularityMaxCv == 0.05f);
    }

    // ---- bot / macro heuristic: input regularity -------------------------------------------------
    {
        auto base = [] { CheatDetector d; d.settings.enabled = true; d.settings.maxSpeed = 8.0f; return d; };
        CheatDetector bot = base(); bot.NoteJoin(1, 0.0);
        for (int i = 0; i < 40; ++i) bot.OnPress(1, 0, 10.0 + i * 0.25, 10.0 + i * 0.25);      // exactly 250 ms apart
        CHECK(bot.Count(1, DetectKind::InputRegularity) > 0);
        CHECK(bot.Risk(1) > 0.0f);
        const Evidence& ev = bot.EvidenceOf(1)->back();
        CHECK(ev.kind == DetectKind::InputRegularity && ev.detail.find("button 0") != std::string::npos);
        CHECK(std::string(DetectKindName(DetectKind::InputRegularity)) == "input_regularity");
        DetectKind k; CHECK(DetectKindFromName("input_regularity", k) && k == DetectKind::InputRegularity);

        CheatDetector human = base(); human.NoteJoin(1, 0.0);                                    // a hand: 250 ms, give or take 60 ms
        std::mt19937 g(1); std::uniform_real_distribution<double> j(-0.06, 0.06);
        double t = 10.0;
        for (int i = 0; i < 400; ++i) { t += 0.25 + j(g); human.OnPress(1, 0, t, t); }
        CHECK(human.Count(1, DetectKind::InputRegularity) == 0 && human.Risk(1) == 0.0f);

        CheatDetector held = base(); held.NoteJoin(1, 0.0);                                      // a held key repeating every tick is not a macro
        for (int i = 0; i < 100; ++i) held.OnPress(1, 0, 10.0 + i * 0.033, 10.0 + i * 0.033);
        CHECK(held.Count(1, DetectKind::InputRegularity) == 0);

        CheatDetector fire = base(); fire.NoteJoin(1, 0.0);                                      // Fire (bit 4) is left out by default: auto-fire is regular on purpose
        for (int i = 0; i < 40; ++i) fire.OnPress(1, 4, 10.0 + i * 0.25, 10.0 + i * 0.25);
        CHECK(fire.Count(1, DetectKind::InputRegularity) == 0);
        fire.settings.regularityButtons |= 0x30;                                                 // ...a click-to-fire game turns it on
        for (int i = 0; i < 40; ++i) fire.OnPress(1, 4, 30.0 + i * 0.25, 30.0 + i * 0.25);
        CHECK(fire.Count(1, DetectKind::InputRegularity) > 0);

        CheatDetector off = base(); off.settings.regularityPresses = 0; off.NoteJoin(1, 0.0);    // can be switched off
        for (int i = 0; i < 40; ++i) off.OnPress(1, 0, 10.0 + i * 0.25, 10.0 + i * 0.25);
        CHECK(off.Count(1, DetectKind::InputRegularity) == 0);

        CheatDetector bad = base(); bad.NoteJoin(1, 0.0);                                        // hostile numbers and buttons: ignored, no crash
        const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
        bad.OnPress(1, 0, nan, 1.0); bad.OnPress(1, 0, inf, 1.0); bad.OnPress(1, -5, 1.0, 1.0); bad.OnPress(1, 99, 1.0, 1.0);
        bad.OnPress(1, 0, 5.0, 5.0); bad.OnPress(1, 0, 4.0, 5.0); bad.OnPress(1, 0, 5.0, 5.0);   // out of order / repeated
        CHECK(bad.Count(1, DetectKind::InputRegularity) == 0);

        CheatDetector slow = base(); slow.NoteJoin(1, 0.0);                                      // once in 10 s is not a pattern we judge
        for (int i = 0; i < 40; ++i) slow.OnPress(1, 0, 10.0 + i * 10.0, 10.0 + i * 10.0);
        CHECK(slow.Count(1, DetectKind::InputRegularity) == 0);

        CheatDetector tk = base(); tk.NoteJoin(1, 0.0);                                          // the presses are timed on the client's tick clock: arrival jitter can't hide a macro
        std::mt19937 g2(5); std::uniform_real_distribution<double> arrive(0.0, 0.08);
        for (int i = 0; i < 40; ++i) tk.OnPress(1, 0, 10.0 + i * 0.25, 10.0 + i * 0.25 + arrive(g2));
        CHECK(tk.Count(1, DetectKind::InputRegularity) > 0);
    }

    // ---- the packet ceiling (every message type counts) ---------------------------------------------
    {
        Srv s;
        CHECK(s.Open("enabled true\nmaxPacketsPerSecond 100\nkickAfterViolations 0\n"));
        Rogue r; CHECK(r.Open(s.net->ServerPort()));
        r.Join();
        for (int i = 0; i < 300 && !r.welcomed; ++i) { s.Pump(1); r.Listen(); }
        CHECK(r.welcomed);
        for (int i = 0; i < 2000; ++i) { net::Packet p(9); p.Write(std::uint32_t(i)); r.Send(p); }   // 2000 pings in no time
        s.Pump(5);
        bool flood = false;
        for (const auto& c : s.reports) if (c.kind == CheatKind::RpcFlood && c.detail.find("packet flood") != std::string::npos) flood = true;
        CHECK(flood);
        CHECK(s.net->Peers().size() == 1);   // still joined: kickAfterViolations 0 only reports

        Srv s2;                              // the ceiling can be switched off
        CHECK(s2.Open("enabled true\nmaxPacketsPerSecond 0\n"));
        Rogue r2; CHECK(r2.Open(s2.net->ServerPort())); r2.Join();
        for (int i = 0; i < 300 && !r2.welcomed; ++i) { s2.Pump(1); r2.Listen(); }
        for (int i = 0; i < 2000; ++i) { net::Packet p(9); p.Write(std::uint32_t(i)); r2.Send(p); }
        s2.Pump(5);
        bool flood2 = false; for (const auto& c : s2.reports) if (c.detail.find("packet flood") != std::string::npos) flood2 = true;
        CHECK(!flood2);

        Srv s3;                              // a sustained flood is kicked by the normal rules
        CHECK(s3.Open("enabled true\nmaxPacketsPerSecond 100\nkickAfterViolations 2\n"));
        Rogue r3; CHECK(r3.Open(s3.net->ServerPort())); r3.Join();
        for (int i = 0; i < 300 && !r3.welcomed; ++i) { s3.Pump(1); r3.Listen(); }
        for (int round = 0; round < 30 && !r3.kicked; ++round) {
            for (int i = 0; i < 400; ++i) { net::Packet p(9); p.Write(std::uint32_t(i)); r3.Send(p); }
            for (int f = 0; f < 55; ++f) { s3.Pump(1); r3.Listen(); }   // ~1.1 s of server time: the next window
        }
        CHECK(r3.kicked);
    }

    // ---- ban list on a live server: written by hand, enforced at the join -----------------------------
    {
        const std::string dir = TmpDir("join");
        const std::string bans = dir + "/bans.txt";
        Spit(bans, "a:2130706433\t1\t0\tput there by an admin\n");           // 127.0.0.1, forever
        Srv s;
        CHECK(s.Open("enabled true\nbanFile " + bans + "\nauditLog " + dir + "/audit.log\n"));
        Rogue r; CHECK(r.Open(s.net->ServerPort())); r.Join();
        for (int i = 0; i < 100 && !r.welcomed && !r.rejected; ++i) { s.Pump(1); r.Listen(); }
        CHECK(!r.welcomed && r.rejected && r.rejectReason.find("banned") != std::string::npos);
        CHECK(Slurp(dir + "/audit.log").find("join_refused") != std::string::npos);
        Srv s2;                                                              // an ended ban lets the player in
        Spit(bans, "a:2130706433\t1\t100\told ban\n");
        CHECK(s2.Open("enabled true\nbanFile " + bans + "\n"));
        Rogue r2; CHECK(r2.Open(s2.net->ServerPort())); r2.Join();
        for (int i = 0; i < 100 && !r2.welcomed && !r2.rejected; ++i) { s2.Pump(1); r2.Listen(); }
        CHECK(r2.welcomed);
        Srv s3;                                                              // anti-cheat off: the list is not consulted (documented)
        Spit(bans, "a:2130706433\t1\t0\tx\n");
        CHECK(s3.Open("enabled false\nbanFile " + bans + "\n"));
        Rogue r3; CHECK(r3.Open(s3.net->ServerPort())); r3.Join();
        for (int i = 0; i < 100 && !r3.welcomed && !r3.rejected; ++i) { s3.Pump(1); r3.Listen(); }
        CHECK(r3.welcomed);
    }

    // ---- limits that were on main without a test of their own: fail closed, SyncVar key limit, kick cooldown ----
    {
        Srv s;                                                          // rules that didn't load: every join is refused
        CHECK(s.Open("enabled true\n"));
        s.net->anticheat.configError = "test: rules file missing";
        Rogue r; CHECK(r.Open(s.net->ServerPort())); r.Join();
        for (int i = 0; i < 100 && !r.welcomed && !r.rejected; ++i) { s.Pump(1); r.Listen(); }
        CHECK(!r.welcomed && r.rejected && r.rejectReason.find("misconfigured") != std::string::npos);
    }
    {
        Srv s;                                                          // a client can't make unlimited keys, or oversized ones
        CHECK(s.Open("enabled true\nclientVar player.\nmaxPacketsPerSecond 0\nkickAfterViolations 0\n"));
        Rogue r; CHECK(r.Open(s.net->ServerPort())); r.Join();
        for (int i = 0; i < 300 && !r.welcomed; ++i) { s.Pump(1); r.Listen(); }
        for (int i = 0; i < 600; ++i) { net::Packet p(8); p.Write("player.k" + std::to_string(i)); p.Write(std::string("v")); r.Send(p); if (i % 20 == 0) s.Pump(1); }
        { net::Packet p(8); p.Write(std::string("player.") + std::string(100000, 'k')); p.Write(std::string("v")); r.Send(p); }
        s.Pump(10);
        CHECK(s.net->SyncVars().size() <= 256);
        for (const auto& kv : s.net->SyncVars()) CHECK(kv.first.size() <= 128 && kv.second.size() <= 4096);
    }
    {
        Srv s;                                                          // kicked: the rejoin is refused for a while (and a hand-banned list is not needed for that)
        CHECK(s.Open("enabled true\nkickAfterViolations 2\nserverVar score\nmaxPacketsPerSecond 0\n"));
        Rogue r; CHECK(r.Open(s.net->ServerPort())); r.Join();
        for (int i = 0; i < 300 && !r.welcomed; ++i) { s.Pump(1); r.Listen(); }
        for (int i = 0; i < 20 && !r.kicked; ++i) { net::Packet p(8); p.Write(std::string("score")); p.Write(std::string("999999")); r.Send(p); s.Pump(2); r.Listen(); }
        CHECK(r.kicked);
        Rogue again; CHECK(again.Open(s.net->ServerPort())); again.Join();
        for (int i = 0; i < 100 && !again.welcomed && !again.rejected; ++i) { s.Pump(1); again.Listen(); }
        CHECK(!again.welcomed && again.rejected && again.rejectReason.find("kicked by the anti-cheat") != std::string::npos);
    }

    {   // identity binding: the sender id a client writes in a packet is ignored; the server uses the one it gave that address
        Srv s;
        CHECK(s.Open("enabled true\nmaxPacketsPerSecond 0\nkickAfterViolations 0\n"));
        Scene cs("C"); cs.physicsEnabled = false;
        auto* honest = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        honest->interpolationRate = 0.0f; honest->encryption = false;
        CHECK(honest->StartClient("127.0.0.1", s.net->ServerPort()));
        cs.Start();
        Rogue r; CHECK(r.Open(s.net->ServerPort())); r.Join();
        for (int i = 0; i < 400 && !(r.welcomed && honest->Joined()); ++i) { s.Pump(1); cs.Update(0.02f); r.Listen(); }
        CHECK(r.welcomed && honest->Joined() && r.id != honest->LocalId());
        net::Packet m(6); m.Write(honest->LocalId()); m.Write(std::string("chat")); m.Write(std::string("i am the other player"));   // claims to be the honest client
        r.Send(m);
        net::Packet st(3); st.Write(honest->LocalId()); st.Write(500.0f); st.Write(0.0f); st.Write(0.0f); st.Write(std::uint8_t('@'));
        r.Send(st);                                                                                                                  // and moves "the honest client"
        bool seen = false; std::uint32_t from = 0;
        for (int i = 0; i < 200 && !seen; ++i) { s.Pump(1); cs.Update(0.02f); r.Listen(); NetworkManager::NetMessage nm; while (honest->PopMessage(nm)) if (nm.channel == "chat") { seen = true; from = nm.from; } }
        CHECK(seen && from == r.id);                                  // it arrives as the rogue's own id
        for (const auto& p : s.net->Peers()) CHECK(p.id == r.id || p.id == honest->LocalId());
        honest->Stop();
    }

    {   // the detection engine's kick is acted on for a player who only sends positions (it used to stay in the game)
        Srv s;
        CHECK(s.Open("enabled true\nmaxPacketsPerSecond 0\ndetect.enabled true\ndetect.maxSpeed 8\ndetect.kickAt 40\nbanOnRequest false\n"));
        Rogue r; CHECK(r.Open(s.net->ServerPort())); r.Join();
        for (int i = 0; i < 300 && !r.welcomed; ++i) { s.Pump(1); r.Listen(); }
        for (int i = 0; i < 100; ++i) { s.Pump(1); r.Listen(); }   // past the join warm-up
        float x = 0.0f;
        for (int i = 0; i < 400 && !r.kicked; ++i) { x += 1.2f; net::Packet st(3); st.Write(r.id); st.Write(x); st.Write(0.0f); st.Write(0.0f); st.Write(std::uint8_t('@')); r.Send(st); s.Pump(1); r.Listen(); }
        CHECK(r.kicked);
        CHECK(s.net->Peers().empty());
    }

    // ---- the dedicated server's switches ------------------------------------------------------------------
    {
        auto parse = [](std::vector<std::string> args, DedicatedServerOptions& o, std::string* err = nullptr) {
            std::vector<char*> argv; args.insert(args.begin(), "okay-player");
            for (auto& a : args) argv.push_back(a.data());
            std::string e; const bool server = ParseDedicatedServerArgs((int)argv.size(), argv.data(), o, &e);
            if (err) *err = e;
            return server && e.empty();
        };
        DedicatedServerOptions a; CHECK(parse({"--server", "--cheat-sim"}, a)); CHECK(a.cheatSim && a.cheatSimOnly.empty() && !a.noBanFile && !a.noAuditLog);
        DedicatedServerOptions b; CHECK(parse({"--server", "--cheat-sim-only", "speed,flood"}, b)); CHECK(b.cheatSim && b.cheatSimOnly == "speed,flood");
        DedicatedServerOptions c; CHECK(parse({"--server", "--no-ban-file", "--no-audit-log"}, c)); CHECK(!c.cheatSim && c.noBanFile && c.noAuditLog);
        DedicatedServerOptions d; std::string err; CHECK(!parse({"--server", "--cheat-sim-only"}, d, &err) && !err.empty());
        DedicatedServerOptions n; CHECK(parse({"--server"}, n)); CHECK(!n.cheatSim);
        std::string usage = DedicatedServerUsage();
        CHECK(usage.find("--cheat-sim") != std::string::npos && usage.find("--no-ban-file") != std::string::npos);
    }

    // ---- the cheat simulator, end to end --------------------------------------------------------------
    {
        CheatSimOptions o;
        o.dir = TmpDir("sim");
        if (std::getenv("OKAY_TEST_SIM_VERBOSE")) o.out = stdout;   // see the report the owner sees: OKAY_TEST_SIM_VERBOSE=1 test_anticheat_hard
        const CheatSimReport rep = RunCheatSim(o);
        CHECK(rep.results.size() == 8);
        for (const auto& r : rep.results) {
            if (!r.caught || r.honestHarmed) std::cerr << "  sim: " << r.name << " caught=" << r.caught << " harmed=" << r.honestHarmed << " how=" << r.how << "\n";
            CHECK(r.caught);
            CHECK(!r.honestHarmed);
        }
        CHECK(rep.Ok());
        // the ban scenario wrote the files, and the audit log holds no personal data
        const std::string bans = Slurp(rep.banFile);
        CHECK(bans.find("a:2130706433") != std::string::npos);
        if (rep.auditText.find(" ban who=") == std::string::npos) std::cerr << "audit text:\n" << rep.auditText << "\n";
        CHECK(rep.auditText.find(" ban who=") != std::string::npos && rep.auditText.find(" strike who=") != std::string::npos && rep.auditText.find(" kicked who=") != std::string::npos);
        CHECK(rep.auditText.find("Rogue") == std::string::npos && rep.auditText.find("127.0.0.1") == std::string::npos && rep.auditText.find("2130706433") == std::string::npos);
        CheatSimOptions one; one.only = "flood"; one.dir = TmpDir("sim2");        // pick one cheat
        const CheatSimReport r1 = RunCheatSim(one);
        CHECK(r1.results.size() == 1 && r1.results[0].name == "flood" && r1.Ok());
    }

    // ---- a hostile client: typed messages with bad fields, against an authoritative-style server ------
    {
        Srv s;
        CHECK(s.Open("enabled true\nmaxRpcsPerSecond 50\nkickAfterViolations 0\nmaxPacketsPerSecond 0\nserverVar score\nclientVar player.\n"
                     "detect.enabled true\ndetect.maxSpeed 8\ndetect.kickAt 0\ndetect.banAt 0\n"));
        Scene cs("C"); cs.physicsEnabled = false;
        auto* honest = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        honest->interpolationRate = 0.0f; honest->encryption = false;
        CHECK(honest->StartClient("127.0.0.1", s.net->ServerPort()));
        cs.Start();
        Rogue r; CHECK(r.Open(s.net->ServerPort())); r.Join();
        for (int i = 0; i < 400 && !(r.welcomed && honest->Joined()); ++i) { s.Pump(1); cs.Update(0.02f); r.Listen(); }
        CHECK(r.welcomed && honest->Joined());
        std::mt19937 g(0xBADC0DEu);
        const float nanf = std::numeric_limits<float>::quiet_NaN(), inff = std::numeric_limits<float>::infinity();
        auto F = [&]() -> float {
            switch (g() % 9) { case 0: return nanf; case 1: return inff; case 2: return -inff; case 3: return 1e38f; case 4: return -1e38f;
                               case 5: return 0.0f; case 6: return -1.0f; case 7: return 1e-38f; default: return (float)(g() % 2000) - 1000.0f; }
        };
        auto Str = [&]() -> std::string {
            switch (g() % 8) { case 0: return std::string(); case 1: return std::string(70000, 'a'); case 2: return std::string("\0\0\0", 3);
                               case 3: return "nan inf -inf 1e999 \xFF\xFE"; case 4: return std::string(300, '\n'); case 5: return "../../etc/passwd";
                               case 6: return "%s%s%n|||0|0 0 0"; default: return std::to_string(g()); }
        };
        const char* channels[] = {"__xform", "__hit", "__spawn", "__despawn", "__dmg", "__respawn", "__rpc/fire", "chat", "__score", "__roster", "srv.x", ""};
        const auto t0 = std::chrono::steady_clock::now();
        const int N = 6000;
        for (int i = 0; i < N; ++i) {
            net::Packet p;
            switch (g() % 9) {
                case 0: p = net::Packet(3); p.Write(F() > 0 ? 7u : 0xFFFFFFFFu); p.Write(F()); p.Write(F()); p.Write(F()); p.Write((std::uint8_t)(g() % 256)); break;   // State
                case 1: p = net::Packet(6); p.Write((std::uint32_t)g()); p.Write(std::string(channels[g() % 12])); p.Write(Str()); break;                           // Message
                case 2: p = net::Packet(7); p.Write((std::uint32_t)g()); p.Write((std::uint32_t)g()); p.Write(std::string(channels[g() % 12])); p.Write(Str()); break;   // DirectMessage
                case 3: p = net::Packet(8); p.Write(g() % 2 ? std::string("score") : Str()); p.Write(Str()); break;                                                   // SyncVar
                case 4: p = net::Packet(28); p.Write(Str()); p.Write((std::uint8_t)(g() % 20));                                                                         // InputCmd with a bad pawn and counts
                        for (int k = 0; k < 3; ++k) { p.Write((std::uint32_t)g()); p.Write((std::uint32_t)g()); p.Write(F()); p.Write(F()); p.Write(F()); p.Write(F()); p.Write((std::uint32_t)g()); p.Write((std::uint32_t)g()); } break;
                case 5: p = net::Packet(27); p.Write((std::uint32_t)g()); p.Write((std::uint32_t)g()); p.Write((std::uint32_t)g()); p.Write((std::uint32_t)g()); p.Write(Str()); break;   // ClockPong
                case 6: p = net::Packet(14); p.Write((std::uint32_t)g()); p.Write(std::string(channels[g() % 12])); p.Write(Str()); break;                              // ReliableMsg
                case 7: p = net::Packet(9); p.Write((std::uint32_t)g()); p.Write(F()); break;                                                                           // Ping
                default: p = net::Packet((std::uint8_t)(g() % 40)); p.Write(Str()); p.Write(F()); break;                                                                // anything
            }
            r.Send(p);
            if (i % 25 == 0) { s.Pump(1); cs.Update(0.02f); r.Listen(); }
        }
        for (int i = 0; i < 100; ++i) { s.Pump(1); cs.Update(0.02f); r.Listen(); }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(secs < 60.0);                                          // bounded CPU: 6000 hostile messages do not hang the server
        CHECK(s.net->IsServer());
        CHECK(honest->Joined() && !honest->WasKicked());            // the honest player is untouched
        CHECK(s.net->Peers().size() <= 2);                          // no phantom players
        CHECK(s.scene.Roots().size() < 64);                 // no object flood
        // the server's own state holds no number a client could poison
        for (const auto& [k, v] : s.net->SyncVars()) { CHECK(k.size() <= 256); CHECK(v.size() <= 65536); }
        CHECK(s.net->SyncVars().size() <= 64);
        honest->SendReliable("alive", "yes");                        // and it still serves
        bool got = false;
        for (int i = 0; i < 300 && !got; ++i) { s.Pump(1); cs.Update(0.02f); NetworkManager::NetMessage m; while (s.net->PopMessage(m)) got |= m.channel == "alive"; }
        CHECK(got);
        CHECK(!s.reports.empty());                                   // and the hostile client was noticed
        honest->Stop();
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
