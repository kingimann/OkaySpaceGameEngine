#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/CheatDetect.hpp"
#include "okay/Net/Packet.hpp"
#include "okay/Net/NetSim.hpp"
#include "okay/Net/UdpSocket.hpp"
#include "okay/Net/SecureChannel.hpp"
#include "okay/Net/SessionTicket.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Components/SurvivalComponents.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace okay;
using okay::net::SessionTicket;

// ---------------------------------------------------------------------------
// Cheat simulation harness.
//
// Part 1 drives the detection engine with scripted players over a modelled
// network (latency, jitter, loss, lag spikes) in fake time, many seeded trials
// per profile. Cheating profiles must be flagged; honest profiles (high ping,
// loss, lag spikes, respawns, skilled shooters, traders) must not be. The
// detection rate per cheat and the false-positive rate per honest profile are
// printed as a table.
//
// Part 2 drives a REAL NetworkManager server in-process (loopback UDP) with
// scripted cheating clients: speed hack, teleport, fly, no-clip, rapid fire,
// shots through walls, forged ownership, ticket forgery, packet replay,
// inventory duplication and malformed packets; plus honest clients (300 ms ping,
// loss, lag spike) that must not be flagged.
// ---------------------------------------------------------------------------
namespace {

struct SimRng {
    std::uint64_t s;
    explicit SimRng(std::uint64_t seed) : s(seed * 6364136223846793005ull + 1442695040888963407ull) { for (int i = 0; i < 4; ++i) U(); }
    std::uint64_t U() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    float F() { return (float)((U() >> 11) & 0xFFFFFF) / 16777216.0f; }
    float Range(float a, float b) { return a + (b - a) * F(); }
    float Sym(float a) { return (F() * 2.0f - 1.0f) * a; }
    bool Chance(float p) { return F() < p; }
};

// ---- the world the scripted players move in ---------------------------------------
constexpr float kMaxSpeed = 8.0f;
constexpr float kWallX = 50.0f;
float GroundAt(float x) {          // flat, then a ramp (slope 0.45) up to a 4.5 m plateau, then a ledge at x = 30
    if (x < 10.0f) return 0.0f;
    if (x < 20.0f) return 0.45f * (x - 10.0f);
    if (x < 30.0f) return 4.5f;
    return 0.0f;
}
DetectSettings World() {
    DetectSettings s;
    s.enabled = true;
    s.maxSpeed = kMaxSpeed; s.gravity = 20.0f; s.maxJumpHeight = 1.2f;
    s.weapons.push_back(WeaponDef{"rifle", 10.0f, 30.0f, 2.0f, 300.0f});
    s.abilities.push_back(AbilityDef{"dash", 5.0f});
    s.economy.push_back(EconomyDef{"coins", 0.0f});
    return s;
}

// ---- a modelled network: delivery times in arrival order -------------------------
struct NetModel {
    float rtt = 0.05f, jitter = 0.01f, loss = 0.0f;
    std::vector<std::pair<double, double>> spikes;   // [from, to): everything sent is held until `to`
};
struct Channel {
    NetModel m; SimRng& r; double last = 0.0;
    Channel(const NetModel& model, SimRng& rng) : m(model), r(rng) {}
    bool Deliver(double sent, double& arrive) {
        if (m.loss > 0.0f && r.Chance(m.loss)) return false;
        arrive = sent + m.rtt * 0.5 + r.Sym(m.jitter);
        for (auto& [a, b] : m.spikes) if (sent >= a && sent < b) arrive = std::max(arrive, b + r.F() * 0.01);
        arrive = std::max(arrive, last);   // a stream stays in order
        last = arrive;
        return true;
    }
};

struct Ev { double t; int order; std::function<void()> f; };
struct Sim {
    CheatDetector d;
    std::vector<Ev> evs;
    double firstFlag = -1.0, firstKick = -1.0;
    double cheatStart = 0.0;
    bool noticed = false;
    int seq = 0;
    std::vector<std::string> notes;
    explicit Sim(const DetectSettings& s = World()) {
        d.settings = s;
        d.hooks.groundAt = [](const Vec3& p, float& y) { y = GroundAt(p.x); return true; };
        d.hooks.staticBlocked = [](const Vec3& a, const Vec3& b) { return (a.x - kWallX) * (b.x - kWallX) < 0.0f && std::fabs(a.z) < 500.0f; };
        d.onResponse = [this](const DetectResponse& r) {
            if (r.action >= DetectAction::Flag && firstFlag < 0.0) firstFlag = r.t;
            if (r.action >= DetectAction::Kick && firstKick < 0.0) firstKick = r.t;
        };
        d.onEvidence = [this](std::uint32_t, const Evidence& e) {
            noticed = true;
            if (notes.size() < 6) { char b[200]; std::snprintf(b, sizeof b, "%s t=%.2f value=%.2f thr=%.2f %s", DetectKindName(e.kind), e.t, e.value, e.threshold, e.detail.c_str()); notes.push_back(b); }
        };
    }
    void At(double t, std::function<void()> f) { evs.push_back({t, seq++, std::move(f)}); }
    void Run() {
        std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
        for (auto& e : evs) { d.Advance(e.t); e.f(); }
        evs.clear();
    }
};

struct Outcome { bool flagged = false, kicked = false, noticed = false; double sec = -1.0; float risk = 0.0f; std::array<int, (std::size_t)DetectKind::Count> kinds{}; std::vector<std::string> notes; };
Outcome Finish(Sim& s) {
    s.Run();
    Outcome o;
    o.flagged = s.firstFlag >= 0.0; o.kicked = s.firstKick >= 0.0; o.noticed = s.noticed;
    o.sec = o.flagged ? s.firstFlag - s.cheatStart : -1.0;
    o.risk = s.d.Risk(1);
    o.notes = s.notes;
    for (int k = 0; k < (int)DetectKind::Count; ++k) o.kinds[(std::size_t)k] = s.d.Count(1, (DetectKind)k);
    return o;
}

// ---- movement scripts ---------------------------------------------------------------
struct MoveOpts {
    double t0 = 0.0, t1 = 60.0;
    float speedMul = 1.0f;        // a speed hack
    float cheatSpeed = 0.0f;      // ... or an absolute speed from cheatFrom on
    double cheatFrom = 1e9;       // speedMul applies from here
    bool jumps = true, input = true, respawns = false, trek = false;
    int hz = 20;
};
void ScriptMovement(Sim& s, Channel& ch, Channel& chIn, SimRng& r, const MoveOpts& o) {
    Vec3 pos(r.Range(-20, 0), 0.0f, r.Range(-20, 20));
    float heading = r.Range(0, 6.28f), speed = 0.0f, want = 0.0f;
    double nextChange = o.t0, nextJump = o.t0 + r.Range(2, 9), nextRespawn = o.t0 + r.Range(10, 25);
    bool airborne = false; float vy = 0.0f;
    if (o.trek) { pos = Vec3(1.0f, 0.0f, 0.0f); heading = 0.0f; }
    const double dt = 1.0 / o.hz;
    for (double t = o.t0; t < o.t1; t += dt) {
        const bool cheating = t >= o.cheatFrom;
        if (t >= nextChange) {
            nextChange = t + r.Range(1.0, 3.5);
            if (cheating && o.cheatSpeed > 0.0f) { heading += r.Sym(0.6f); }
            want = r.Chance(0.2f) ? 0.0f : r.Range(3.0f, kMaxSpeed);
            if (o.trek) want = 6.5f;
            else heading += r.Sym(1.2f);
        }
        if (cheating && o.cheatSpeed > 0.0f) want = o.cheatSpeed;
        speed += std::clamp(want - speed, -60.0f * (float)dt, 40.0f * (float)dt);
        const float mul = cheating ? o.speedMul : 1.0f;
        pos.x += std::cos(heading) * speed * mul * (float)dt;
        pos.z += std::sin(heading) * speed * mul * (float)dt;
        if (o.trek) { if (pos.x > 38.0f) heading = 3.14159f; else if (pos.x < 2.0f) heading = 0.0f; }   // turn at the ends of the track
        if (!o.trek) {   // stay in the box
            if (pos.x > 8.0f) { pos.x = 8.0f; heading = 3.14159f + r.Sym(0.5f); }
            if (pos.x < -40.0f) { pos.x = -40.0f; heading = r.Sym(0.5f); }
            if (std::fabs(pos.z) > 40.0f) { pos.z = std::clamp(pos.z, -40.0f, 40.0f); heading = -heading; }
        }
        // vertical: walk on the ground, jump, step off a ledge
        const float g = GroundAt(pos.x);
        if (!airborne && o.jumps && !o.trek && t >= nextJump) { airborne = true; vy = std::sqrt(2.0f * 20.0f * 1.1f); nextJump = t + r.Range(2, 8); }
        if (airborne) {
            vy -= 20.0f * (float)dt; pos.y += vy * (float)dt;
            if (pos.y <= g) { pos.y = g; airborne = false; vy = 0.0f; }
        } else if (pos.y > g + 0.01f) {   // walked off a ledge
            airborne = true; vy = 0.0f;
        } else pos.y = g;
        if (o.respawns && t >= nextRespawn) {
            nextRespawn = t + r.Range(10, 25);
            pos = Vec3(r.Range(-30, 0), 0.0f, r.Range(-30, 30)); airborne = false; vy = 0.0f;
            s.At(t, [&s, t] { s.d.NoteRespawn(1, t); });
        }
        const Vec3 p = pos; const bool moving = speed > 0.5f;
        double arr;
        if (ch.Deliver(t, arr)) s.At(arr, [&s, p, arr] { s.d.OnMessage(1, arr); s.d.OnMove(1, "p", p, arr); });
        if (o.input && chIn.Deliver(t, arr)) s.At(arr, [&s, moving, arr] { s.d.OnMessage(1, arr); s.d.OnInput(1, 0.0f, moving ? 1.0f : 0.0f, arr); });
    }
}

// ---- combat scripts -----------------------------------------------------------------
struct ShootOpts {
    double t0 = 0.0, t1 = 120.0;
    float accuracy = 0.45f, headRate = 0.15f, rate = 6.0f;   // shots per second while firing
    bool snap = false, wall = false;
    float snapEvery = 0.0f;       // honest flicks: one per this many seconds (0 = none)
    float damage = 30.0f;
    int burstMin = 2, burstMax = 8;   // shots per burst
    float pauseMin = 0.6f, pauseMax = 2.5f;
};
void ScriptShooting(Sim& s, Channel& ch, Channel& chAim, SimRng& r, const ShootOpts& o) {
    double t = o.t0, nextFlick = o.snapEvery > 0.0f ? o.t0 + o.snapEvery : 1e9;
    float yaw = 0.0f;
    double arr;
    while (t < o.t1) {
        // a burst of up to 8 shots, then a pause
        const int burst = o.burstMin + (int)(r.F() * (float)(o.burstMax - o.burstMin + 1));
        for (int i = 0; i < burst && t < o.t1; ++i) {
            t += 1.0 / o.rate + r.Sym(0.01f);
            // aim: small tracking motion, or a snap right before a cheating shot
            const bool flick = o.snap || t >= nextFlick;
            if (flick) {
                if (t >= nextFlick) nextFlick = t + o.snapEvery * r.Range(0.7f, 1.3f);
                const double ts = t - 0.02;
                const float yawTo = yaw + (r.Chance(0.5f) ? 1.0f : -1.0f) * r.Range(40.0f, 150.0f);
                if (chAim.Deliver(ts - 0.016, arr)) s.At(arr, [&s, yaw, arr] { s.d.OnAim(1, yaw, 0.0f, arr); });
                if (chAim.Deliver(ts, arr)) s.At(arr, [&s, yawTo, arr] { s.d.OnAim(1, yawTo, 0.0f, arr); });
                yaw = yawTo;
            } else {
                yaw += r.Sym(1.5f);
                if (chAim.Deliver(t - 0.01, arr)) s.At(arr, [&s, yaw, arr] { s.d.OnAim(1, yaw, 0.0f, arr); });
            }
            ShotEvent e; e.weapon = "rifle"; e.damage = o.damage; e.distance = r.Range(8.0f, 80.0f);
            e.hit = o.accuracy >= 1.0f || r.Chance(o.accuracy);
            e.headshot = e.hit && (o.headRate >= 1.0f || r.Chance(o.headRate));
            if (e.headshot) e.damage = o.damage * 2.0f;
            if (o.wall) { e.blockedByWall = true; e.hit = true; }
            if (ch.Deliver(t, arr)) { e.t = arr; s.At(arr, [&s, e] { s.d.OnMessage(1, e.t); s.d.OnShot(1, e); }); }
        }
        t += r.Range(o.pauseMin, o.pauseMax);
        // keep the message stream alive between bursts (the player is also moving)
        for (double u = t - 1.5; u < t; u += 0.05) if (u > o.t0 && ch.Deliver(u, arr)) s.At(arr, [&s, arr] { s.d.OnMessage(1, arr); });
    }
}

// ---- the table ---------------------------------------------------------------------------
struct Row { std::string name; bool cheat; int trials = 0, flagged = 0, kicked = 0, noticed = 0; double sumSec = 0, maxSec = 0; float maxRisk = 0; std::array<int, (std::size_t)DetectKind::Count> kinds{}; std::vector<double> secs; };
std::vector<Row> g_rows;
using Scenario = std::function<Outcome(SimRng&)>;
Row& RunProfile(const std::string& name, bool cheat, int trials, const Scenario& f) {
    Row row; row.name = name; row.cheat = cheat;
    for (int i = 0; i < trials; ++i) {
        SimRng r(1000 + (std::uint64_t)i * 7919 + std::hash<std::string>{}(name) % 100000);
        const Outcome o = f(r);
        if (!cheat && o.noticed && row.noticed == 0 && std::getenv("ACSIM_VERBOSE")) { std::printf("    first evidence on an honest run (%s):\n", name.c_str()); for (const auto& n : o.notes) std::printf("      %s\n", n.c_str()); }
        ++row.trials; row.flagged += o.flagged; row.kicked += o.kicked; row.noticed += o.noticed;
        if (o.flagged) { row.sumSec += o.sec; row.maxSec = std::max(row.maxSec, o.sec); row.secs.push_back(o.sec); }
        for (std::size_t k = 0; k < row.kinds.size(); ++k) row.kinds[k] += o.kinds[k];
        row.maxRisk = std::max(row.maxRisk, o.risk);
    }
    g_rows.push_back(row);
    return g_rows.back();
}
double Rate(const Row& r) { return r.trials ? (double)r.flagged / r.trials : 0.0; }

NetModel Good() { NetModel m; m.rtt = 0.05f; m.jitter = 0.008f; return m; }
} // namespace

// ===========================================================================================
// Part 2 helpers: a real server and real clients
// ===========================================================================================
namespace {
void Nap(int ms = 2) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
struct Room {
    Scene ss{"Server"};
    Scene ca{"A"}, cb{"B"};
    NetworkManager* srv = nullptr; NetworkManager* a = nullptr; NetworkManager* b = nullptr;
    std::vector<CheatReport> reports;
    std::vector<NetworkManager::AntiCheatKick> kicks;
    int realtimeMs = 2;
    std::map<std::pair<std::uint32_t, int>, int> tally;   // evidence ever scored, per peer and kind (a kicked peer's record is forgotten)
    void Pump(int frames, float dt = 0.02f) {
        for (int i = 0; i < frames; ++i) { ss.Update(0.02f); ca.Update(dt); cb.Update(dt); Nap(realtimeMs); }
    }
    bool Open(const std::string& rules, const std::string& crate, bool encrypt = false) {
        srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->interpolationRate = 0.0f; srv->maxMessagesPerSecond = 0;
        srv->maxMoveSpeed = 1000.0f;   // a move check exists, so client hit claims are judged
        srv->spawnablePrefabs = {crate};
        srv->encryption = encrypt;
        std::string err;
        AntiCheatSettings s;
        if (!AntiCheatSettings::Parse(rules, s, &err)) { std::printf("  rules: %s\n", err.c_str()); return false; }
        srv->anticheat.settings = s;
        srv->SetCheatHandler([this](const CheatReport& r) { reports.push_back(r); });
        srv->anticheat.detector.onEvidence = [this](std::uint32_t peer, const Evidence& e) { ++tally[{peer, (int)e.kind}]; };
        srv->SetAntiCheatKickHandler([this](const NetworkManager::AntiCheatKick& k) { kicks.push_back(k); });
        if (!srv->StartServer(0)) return false;
        ss.Start();
        a = ca.CreateGameObject("Net")->AddComponent<NetworkManager>();
        b = cb.CreateGameObject("Net")->AddComponent<NetworkManager>();
        a->interpolationRate = b->interpolationRate = 0.0f;
        a->encryption = b->encryption = encrypt;
        if (!a->StartClient("127.0.0.1", srv->ServerPort()) || !b->StartClient("127.0.0.1", srv->ServerPort())) return false;
        ca.Start(); cb.Start();
        for (int i = 0; i < 400 && !(a->Joined() && b->Joined()); ++i) Pump(1);
        Pump(40 + 175);   // (past the join warm-up)
        return a->Joined() && b->Joined();
    }
    const CheatDetector& Det() const { return srv->anticheat.detector; }
    int Scored(std::uint32_t peer, DetectKind k) const { auto it = tally.find({peer, (int)k}); return it == tally.end() ? 0 : it->second; }
    int AnyScored(std::uint32_t peer) const {
        int n = 0;
        for (int k = 0; k < (int)DetectKind::Count; ++k) n += Scored(peer, (DetectKind)k);
        return n;
    }
    ~Room() { if (a) a->Stop(); if (b) b->Stop(); if (srv) srv->Stop(); }
};
std::string Kinds(const Room& r) {
    std::string out;
    for (std::uint32_t id : {r.a ? r.a->LocalId() : 0u, r.b ? r.b->LocalId() : 0u}) {
        for (int k = 0; k < (int)DetectKind::Count; ++k) if (const int n = r.Scored(id, (DetectKind)k)) out += " p" + std::to_string(id) + ":" + DetectKindName((DetectKind)k) + "x" + std::to_string(n);
    }
    return out;
}
struct Result2 { std::string name; bool cheat; bool detected; std::string how; };
std::vector<Result2> g_b;
void Record(const std::string& name, bool cheat, bool detected, const std::string& how) {
    g_b.push_back({name, cheat, detected, how});
}

const char* kRules =
    "enabled true\n"
    "spawn okay_sim_crate.okayprefab\n"
    "maxSyncSpeed 8\n"
    "kickAfterViolations 0\n"
    "clientVar player.\n"
    "weapon rifle\n"
    "maxHitRange 100\n"
    "detect.enabled true\n"
    "detect.gravity 20\n"
    "detect.maxJumpHeight 1.2\n"
    "detect.weapon rifle 10 30 2\n"
    "detect.economy player.coins 0\n"
    "detect.kickAt 90\n";
} // namespace

int main() {
    RUN_SUITE("anticheat_sim");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // =======================================================================================
    // Part 1: scripted players against the detection engine
    // =======================================================================================
    const int N = 40;
    // ---- cheats -----------------------------------------------------------------------
    RunProfile("speedhack 1.5x-3x", true, N, [](SimRng& r) {
        Sim s; s.cheatStart = 20.0; NetModel m = Good(); Channel c(m, r), ci(m, r);
        MoveOpts o; o.t1 = 60.0; o.speedMul = r.Range(1.5f, 3.0f); o.cheatFrom = s.cheatStart; o.trek = false;
        ScriptMovement(s, c, ci, r, o); return Finish(s); });
    for (float k : {1.3f, 1.15f}) {
        char nm[64]; std::snprintf(nm, sizeof nm, "speedhack: runs at %.2fx the top speed, all the time", k);
        RunProfile(nm, true, N, [k](SimRng& r) {
            Sim s; s.cheatStart = 20.0; NetModel m = Good(); Channel c(m, r), ci(m, r);
            MoveOpts o; o.t1 = 80.0; o.cheatFrom = s.cheatStart; o.cheatSpeed = kMaxSpeed * k;
            ScriptMovement(s, c, ci, r, o); return Finish(s); });
    }
    RunProfile("teleport 40-250 units", true, N, [](SimRng& r) {
        Sim s; s.cheatStart = 20.0; NetModel m = Good(); Channel c(m, r), ci(m, r), c2(m, r);
        MoveOpts o; o.t1 = 20.0; ScriptMovement(s, c, ci, r, o);
        double arr; float off = 0.0f;   // from 20 s on, the position jumps every 3 s (and walks a little in between)
        for (double t = 20.0; t < 40.0; t += 0.05) {
            if (std::fmod(t - 20.0, 3.0) < 0.05) off += r.Range(40.0f, 250.0f) * (r.Chance(0.5f) ? 1.0f : -1.0f);
            const Vec3 p(-10.0f + off, 0.0f, 0.0f);
            if (c2.Deliver(t, arr)) s.At(arr, [&s, p, arr] { s.d.OnMessage(1, arr); s.d.OnMove(1, "p", p, arr); });
        }
        return Finish(s); });
    RunProfile("fly (climb + hover)", true, N, [](SimRng& r) {
        Sim s; s.cheatStart = 10.0; NetModel m = Good(); Channel c(m, r);
        double arr; float y = 0.0f, x = -20.0f; const float climb = r.Range(2.0f, 6.0f), top = r.Range(3.0f, 25.0f);
        for (double t = 0.0; t < 40.0; t += 0.05) {
            if (t >= 10.0) { y = std::min(top, y + climb * 0.05f); x += (t > 14.0 ? r.Range(0.0f, 6.0f) : 0.0f) * 0.05f; }
            const Vec3 p(x, y, 0.0f);
            if (c.Deliver(t, arr)) s.At(arr, [&s, p, arr] { s.d.OnMessage(1, arr); s.d.OnMove(1, "p", p, arr); });
        }
        return Finish(s); });
    RunProfile("fly (low hover, never falls)", true, N, [](SimRng& r) {
        Sim s; s.cheatStart = 10.0; NetModel m = Good(); Channel c(m, r);
        double arr; float y = 0.0f; const float hover = r.Range(1.3f, 1.8f);
        for (double t = 0.0; t < 40.0; t += 0.05) {
            if (t >= 10.0) y = std::min(hover, y + 0.3f);
            const Vec3 p(-20.0f, y, 0.0f);
            if (c.Deliver(t, arr)) s.At(arr, [&s, p, arr] { s.d.OnMessage(1, arr); s.d.OnMove(1, "p", p, arr); });
        }
        return Finish(s); });
    RunProfile("wall climb", true, N, [](SimRng& r) {
        Sim s; s.cheatStart = 10.0; NetModel m = Good(); Channel c(m, r);
        double arr; float y = 0.0f; const float rate = r.Range(2.0f, 5.0f);
        for (double t = 0.0; t < 30.0; t += 0.05) {
            if (t >= 10.0) y += rate * 0.05f;
            const Vec3 p(-20.0f, y, 3.0f);
            if (c.Deliver(t, arr)) s.At(arr, [&s, p, arr] { s.d.OnMessage(1, arr); s.d.OnMove(1, "p", p, arr); });
        }
        return Finish(s); });
    RunProfile("no-clip through walls", true, N, [](SimRng& r) {
        Sim s; s.cheatStart = 10.0; NetModel m = Good(); Channel c(m, r);
        double arr; float x = 40.0f; float dir = 1.0f;
        for (double t = 0.0; t < 40.0; t += 0.05) {
            const float v = t >= 10.0 ? 7.0f : 0.0f;
            if (x > kWallX + 6.0f) dir = -1.0f;
            if (x < kWallX - 6.0f) dir = 1.0f;
            x += dir * v * 0.05f;
            const Vec3 p(x, 0.0f, 0.0f);
            if (c.Deliver(t, arr)) s.At(arr, [&s, p, arr] { s.d.OnMessage(1, arr); s.d.OnMove(1, "p", p, arr); });
        }
        return Finish(s); });
    RunProfile("ghost movement (no input)", true, N, [](SimRng& r) {
        Sim s; s.cheatStart = 10.0; NetModel m = Good(); Channel c(m, r), ci(m, r);
        double arr; float x = -30.0f;
        for (double t = 0.0; t < 30.0; t += 0.05) {
            const bool cheat = t >= 10.0;
            if (cheat) x += 6.0f * 0.05f * (std::fmod(t, 6.0) < 3.0 ? 1.0f : -1.0f);
            const Vec3 p(x, 0.0f, 0.0f);
            if (c.Deliver(t, arr)) s.At(arr, [&s, p, arr] { s.d.OnMessage(1, arr); s.d.OnMove(1, "p", p, arr); });
            if (ci.Deliver(t, arr)) s.At(arr, [&s, arr] { s.d.OnInput(1, 0.0f, 0.0f, arr); });
        }
        return Finish(s); });
    RunProfile("jump spam", true, N, [](SimRng& r) {
        Sim s; s.cheatStart = 10.0; NetModel m = Good(); Channel c(m, r); double arr;
        for (double t = 10.0; t < 20.0; t += r.Range(0.03f, 0.1f)) if (c.Deliver(t, arr)) s.At(arr, [&s, arr] { s.d.OnJump(1, arr); });
        return Finish(s); });
    RunProfile("aimbot (100% hits, 95% headshots)", true, N, [](SimRng& r) {
        Sim s; s.cheatStart = 0.0; NetModel m = Good(); Channel c(m, r), ca(m, r);
        ShootOpts o; o.t1 = 120.0; o.accuracy = 1.0f; o.headRate = 0.95f; o.damage = 30.0f;
        ScriptShooting(s, c, ca, r, o); return Finish(s); });
    RunProfile("aimbot snap-and-shoot", true, N, [](SimRng& r) {
        Sim s; s.cheatStart = 0.0; NetModel m = Good(); Channel c(m, r), ca(m, r);
        ShootOpts o; o.t1 = 120.0; o.accuracy = 0.6f; o.headRate = 0.2f; o.snap = true;
        ScriptShooting(s, c, ca, r, o); return Finish(s); });
    RunProfile("rapid fire 15-40/s", true, N, [](SimRng& r) {
        Sim s; s.cheatStart = 0.0; NetModel m = Good(); Channel c(m, r), ca(m, r);
        ShootOpts o; o.t1 = 60.0; o.rate = r.Range(15.0f, 40.0f); o.accuracy = 0.4f; o.burstMin = 40; o.burstMax = 120; o.pauseMin = 0.5f; o.pauseMax = 1.5f;
        ScriptShooting(s, c, ca, r, o); return Finish(s); });
    RunProfile("hits through walls", true, N, [](SimRng& r) {
        Sim s; s.cheatStart = 0.0; NetModel m = Good(); Channel c(m, r), ca(m, r);
        ShootOpts o; o.t1 = 90.0; o.rate = 3.0f; o.wall = true;
        ScriptShooting(s, c, ca, r, o); return Finish(s); });
    RunProfile("damage hack (x6)", true, N, [](SimRng& r) {
        Sim s; s.cheatStart = 0.0; NetModel m = Good(); Channel c(m, r), ca(m, r);
        ShootOpts o; o.t1 = 60.0; o.damage = 180.0f; ScriptShooting(s, c, ca, r, o); return Finish(s); });
    RunProfile("packet replay", true, N, [](SimRng& r) {
        Sim s; s.cheatStart = 10.0; NetModel m = Good(); Channel c(m, r); double arr;
        std::uint64_t sq = 0;
        for (double t = 0.0; t < 30.0; t += 0.02) {
            if (!c.Deliver(t, arr)) continue;
            const std::uint64_t n = ++sq;
            s.At(arr, [&s, n, arr] { s.d.OnMessage(1, arr); s.d.OnSequence(1, n, arr); });
            if (t >= 10.0 && t < 20.0 && r.Chance(0.15f)) {   // resend an old packet
                const std::uint64_t old = 1 + (std::uint64_t)(r.F() * (float)(n - 1));
                s.At(arr + 0.001, [&s, old, arr] { s.d.OnSequence(1, old, arr + 0.001); });
            }
        }
        return Finish(s); });
    RunProfile("forged ownership claims", true, N, [](SimRng& r) {
        Sim s; s.cheatStart = 10.0; NetModel m = Good(); Channel c(m, r); double arr;
        for (double t = 10.0; t < 20.0; t += r.Range(0.3f, 0.8f)) if (c.Deliver(t, arr)) s.At(arr, [&s, arr] { s.d.OnViolation(1, "forged transform", "sp/9/1", arr, 2.0f); });
        return Finish(s); });
    RunProfile("inventory duplication", true, N, [](SimRng& r) {
        Sim s; s.cheatStart = 10.0; NetModel m = Good(); Channel c(m, r); double arr = 0.0;
        s.At(0.0, [&s] { s.d.SetBalance(1, "coins", 100.0f, 0.0); });
        float coins = 100.0f;
        for (double t = 1.0; t < 30.0; t += 1.0) {
            if (t > 10.0 && t < 18.0) coins += r.Range(400.0f, 5000.0f);   // from nowhere
            const float v = coins;
            if (c.Deliver(t, arr)) s.At(arr, [&s, v, arr] { s.d.OnClaimedValue(1, "coins", v, arr); });
        }
        int n = 0;
        for (double t = 10.0; t < 14.0; t += 0.4) {   // the same item uid acquired again and again, and by a second account
            ++n; if (c.Deliver(t, arr)) s.At(arr, [&s, arr, n] { s.d.OnItemAcquire(1, "uid-sword", "sword", arr); (void)n; });
            if (n == 3) s.At(arr + 0.1, [&s, arr] { s.d.OnItemAcquire(2, "uid-sword", "sword", arr + 0.1); });
        }
        return Finish(s); });
    RunProfile("cooldown bypass", true, N, [](SimRng& r) {
        Sim s; s.cheatStart = 10.0; NetModel m = Good(); Channel c(m, r); double arr;
        for (double t = 0.0; t < 40.0; t += (t < 10.0 ? 5.2 : r.Range(0.4f, 2.0f))) if (c.Deliver(t, arr)) s.At(arr, [&s, arr] { s.d.OnAbility(1, "dash", arr); });
        return Finish(s); });
    RunProfile("message flood", true, N, [](SimRng& r) {
        Sim s; s.cheatStart = 10.0; NetModel m = Good(); Channel c(m, r); double arr;
        for (double t = 0.0; t < 30.0; t += (t < 10.0 ? 0.02 : 1.0 / r.Range(300.0f, 1000.0f))) if (c.Deliver(t, arr)) s.At(arr, [&s, arr] { s.d.OnMessage(1, arr); });
        return Finish(s); });
    RunProfile("malformed packet fuzz", true, N, [](SimRng& r) {
        Sim s; s.cheatStart = 10.0; NetModel m = Good(); Channel c(m, r); double arr;
        for (double t = 10.0; t < 30.0; t += r.Range(0.05f, 0.3f)) if (c.Deliver(t, arr)) s.At(arr, [&s, arr] { s.d.OnMalformed(1, "fuzz", arr); });
        return Finish(s); });
    RunProfile("time manipulation 1.3x-2x", true, N, [](SimRng& r) {
        Sim s; s.cheatStart = 10.0; NetModel m = Good(); Channel c(m, r); double arr; double clock = 0.0;
        const float k = r.Range(1.3f, 2.0f);
        for (double t = 0.0; t < 60.0; t += 0.5) {
            clock += 0.5 * (t >= 10.0 ? k : 1.0);
            const double cv = clock;
            if (c.Deliver(t, arr)) s.At(arr, [&s, cv, arr] { s.d.OnClock(1, cv, arr); });
        }
        return Finish(s); });

    // ---- honest profiles: none of these may be flagged ---------------------------------------
    const int H = 40;
    RunProfile("honest: typical mover", false, H, [](SimRng& r) {
        Sim s; NetModel m = Good(); Channel c(m, r), ci(m, r);
        MoveOpts o; o.t1 = 150.0; ScriptMovement(s, c, ci, r, o); return Finish(s); });
    RunProfile("honest: 300 ms ping, 60 ms jitter", false, H, [](SimRng& r) {
        Sim s; NetModel m; m.rtt = 0.3f; m.jitter = 0.06f; Channel c(m, r), ci(m, r); s.d.SetPing(1, 0.3f);
        MoveOpts o; o.t1 = 150.0; ScriptMovement(s, c, ci, r, o); return Finish(s); });
    RunProfile("honest: 8% packet loss", false, H, [](SimRng& r) {
        Sim s; NetModel m = Good(); m.loss = 0.08f; Channel c(m, r), ci(m, r);
        MoveOpts o; o.t1 = 150.0; ScriptMovement(s, c, ci, r, o); return Finish(s); });
    RunProfile("honest: lag spikes 0.5-1.8 s", false, H, [](SimRng& r) {
        Sim s; NetModel m; m.rtt = 0.2f; m.jitter = 0.04f;
        for (double t = r.Range(8, 20); t < 150.0; t += r.Range(15, 35)) m.spikes.push_back({t, t + r.Range(0.5f, 1.8f)});
        Channel c(m, r), ci(m, r); s.d.SetPing(1, 0.2f);
        MoveOpts o; o.t1 = 150.0; ScriptMovement(s, c, ci, r, o); return Finish(s); });
    RunProfile("honest: respawns / teleports", false, H, [](SimRng& r) {
        Sim s; NetModel m = Good(); Channel c(m, r), ci(m, r);
        MoveOpts o; o.t1 = 150.0; o.respawns = true; ScriptMovement(s, c, ci, r, o); return Finish(s); });
    RunProfile("honest: ramps, ledges, falls", false, H, [](SimRng& r) {
        Sim s; NetModel m = Good(); Channel c(m, r), ci(m, r);
        MoveOpts o; o.t1 = 150.0; o.trek = true; o.jumps = false; ScriptMovement(s, c, ci, r, o); return Finish(s); });
    RunProfile("honest: skilled shooter (75% acc, 35% head, flicks)", false, H, [](SimRng& r) {
        Sim s; NetModel m = Good(); Channel c(m, r), ca(m, r);
        ShootOpts o; o.t1 = 300.0; o.accuracy = 0.75f; o.headRate = 0.35f; o.rate = 8.0f; o.snapEvery = 25.0f;
        ScriptShooting(s, c, ca, r, o); return Finish(s); });
    RunProfile("honest: trader (grants, items, cooldowns)", false, H, [](SimRng& r) {
        Sim s; NetModel m; m.rtt = 0.15f; m.jitter = 0.03f; Channel c(m, r), cAb(m, r); double arr;
        s.d.SetPing(1, 0.15f);
        s.At(0.0, [&s] { s.d.SetBalance(1, "coins", 100.0f, 0.0); });
        float coins = 100.0f; int uid = 0; std::vector<std::string> held;
        for (double t = 1.0; t < 150.0; t += r.Range(0.8f, 2.5f)) {
            const int what = (int)(r.F() * 4.0f);
            if (what == 0) { const float g = r.Range(5, 200); coins += g; s.At(t, [&s, g, t] { s.d.OnGrant(1, "coins", g, t); }); }
            if (what == 1 && coins > 20) { coins -= 15.0f; }
            if (what == 2) { const std::string id = "u" + std::to_string(++uid); held.push_back(id); s.At(t, [&s, id, t] { s.d.OnItemAcquire(1, id, "gem", t); }); }
            if (what == 3 && !held.empty()) { const std::string id = held.back(); held.pop_back(); s.At(t, [&s, id] { s.d.OnItemRelease(1, id); }); }
            const float v = coins;
            if (c.Deliver(t + 0.01, arr)) s.At(arr, [&s, v, arr] { s.d.OnClaimedValue(1, "coins", v, arr); });
        }
        for (double t = 0.0; t < 150.0; t += 5.0 + r.Range(0.0f, 1.0f)) if (cAb.Deliver(t, arr)) s.At(arr, [&s, arr] { s.d.OnAbility(1, "dash", arr); });
        return Finish(s); });
    RunProfile("honest: chatty lobby (60 msg/s, bursts to 150/s)", false, H, [](SimRng& r) {
        Sim s; NetModel m = Good(); Channel c(m, r), cB(m, r); double arr;
        for (double t = 0.0; t < 150.0; t += 1.0 / 60.0) if (c.Deliver(t, arr)) s.At(arr, [&s, arr] { s.d.OnMessage(1, arr); });
        for (double t0 = 10.0; t0 < 150.0; t0 += 20.0) for (double t = t0; t < t0 + 0.3; t += 1.0 / 150.0) if (cB.Deliver(t, arr)) s.At(arr, [&s, arr] { s.d.OnMessage(1, arr); });
        return Finish(s); });
    RunProfile("honest: clock drift 3% + jitter", false, H, [](SimRng& r) {
        Sim s; NetModel m; m.rtt = 0.25f; m.jitter = 0.05f; Channel c(m, r); double arr; s.d.SetPing(1, 0.25f);
        for (double t = 0.0; t < 150.0; t += 0.5) { const double cv = t * 1.03; if (c.Deliver(t, arr)) s.At(arr, [&s, cv, arr] { s.d.OnClock(1, cv, arr); }); }
        return Finish(s); });
    RunProfile("honest: everything at once (300 ms, 5% loss, spikes, shooter, 10 min)", false, 10, [](SimRng& r) {
        Sim s; NetModel m; m.rtt = 0.3f; m.jitter = 0.05f; m.loss = 0.05f;
        for (double t = 20.0; t < 600.0; t += r.Range(20, 50)) m.spikes.push_back({t, t + r.Range(0.4f, 1.5f)});
        Channel c(m, r), ci(m, r), cs(m, r), ca(m, r); s.d.SetPing(1, 0.3f);
        MoveOpts o; o.t1 = 600.0; o.respawns = true; ScriptMovement(s, c, ci, r, o);
        ShootOpts so; so.t1 = 600.0; so.accuracy = 0.55f; so.headRate = 0.2f; so.rate = 7.0f; so.snapEvery = 40.0f;
        ScriptShooting(s, cs, ca, r, so);
        return Finish(s); });

    // ---- print the table ----------------------------------------------------------------------
    std::printf("\n  Detection engine, simulated players (%d trials per cheat, %d per honest profile)\n", N, H);
    std::printf("  %-66s %9s %8s %9s %9s\n", "profile", "flagged", "kicked", "median s", "worst s");
    std::printf("  %s\n", std::string(106, '-').c_str());
    for (const Row& r : g_rows) {
        std::vector<double> sv = r.secs; std::sort(sv.begin(), sv.end());
        const double mean = sv.empty() ? 0.0 : sv[sv.size() / 2];
        if (r.cheat) std::printf("  %-66s %4d/%-4d %4d/%-3d %9.2f %9.2f\n", r.name.c_str(), r.flagged, r.trials, r.kicked, r.trials, mean, r.maxSec);
        else {
            std::printf("  %-66s %4d/%-4d %4d/%-3d   (noticed %d, max risk %.1f)\n", r.name.c_str(), r.flagged, r.trials, r.kicked, r.trials, r.noticed, r.maxRisk);
            if (r.noticed) { std::printf("      scored:"); for (std::size_t k = 0; k < r.kinds.size(); ++k) if (r.kinds[k]) std::printf(" %s x%d", DetectKindName((DetectKind)k), r.kinds[k]); std::printf("\n"); }
        }
    }
    {
        int cheatTrials = 0, cheatFlagged = 0, honTrials = 0, honFlagged = 0;
        for (const Row& r : g_rows) { (r.cheat ? cheatTrials : honTrials) += r.trials; (r.cheat ? cheatFlagged : honFlagged) += r.flagged; }
        std::printf("  overall: cheats flagged %d/%d (%.1f%%), honest flagged %d/%d (%.2f%%)\n\n", cheatFlagged, cheatTrials,
                    100.0 * cheatFlagged / std::max(1, cheatTrials), honFlagged, honTrials, 100.0 * honFlagged / std::max(1, honTrials));
    }
    for (const Row& r : g_rows) {
        if (r.cheat) { if (Rate(r) < 0.9) std::printf("  LOW detection: %s %.0f%%\n", r.name.c_str(), 100.0 * Rate(r)); CHECK(Rate(r) >= 0.9); }
        else { if (r.flagged) std::printf("  FALSE POSITIVE: %s flagged %d/%d\n", r.name.c_str(), r.flagged, r.trials); CHECK(r.flagged == 0); }
    }
    // honest play is not even kicked, and a cheat that is flagged is flagged quickly
    for (const Row& r : g_rows) if (r.cheat && r.flagged) CHECK(r.maxSec < 40.0);

    // =======================================================================================
    // Part 2: a real NetworkManager server, scripted cheating and honest clients
    // =======================================================================================
    if (std::getenv("ACSIM_QUICK")) { Log::sink = sink; TEST_MAIN_RESULT(); }   // (just the engine table, while tuning)
    const std::string crate = "okay_sim_crate.okayprefab";
    {
        Scene t("T");
        CHECK(SceneSerializer::SaveObjectToFile(*t.CreateGameObject("Crate"), crate));
    }
    auto Mk = [&](Room& r) { return r.Open(kRules, crate); };

    // -- speed hack: its game runs 3x fast; no violation-count kick is configured, so only the engine can act
    {
        Room r; CHECK(Mk(r));
        const std::uint32_t B = r.b->LocalId();
        GameObject* mine = r.b->SpawnOwned(crate, {0.0f, 0.0f, 0.0f});
        CHECK(mine != nullptr);
        r.Pump(40);
        for (int i = 0; i < 400 && !r.b->WasKicked(); ++i) { mine->transform->localPosition.x += 24.0f * 0.02f; r.Pump(1); }
        CHECK(r.Scored(B, DetectKind::SpeedWindow) + r.Scored(B, DetectKind::SpeedBurst) + r.Scored(B, DetectKind::Teleport) > 0);
        CHECK(r.b->WasKicked() && !r.kicks.empty() && r.kicks.back().risk >= 90.0f);
        CHECK(!r.kicks.empty() && r.kicks.back().evidence.find("\"ev\"") != std::string::npos);
        EvidenceBundle bundle; CHECK(!r.kicks.empty() && EvidenceBundle::Parse(r.kicks.back().evidence, bundle) && !bundle.evidence.empty());
        CHECK(r.AnyScored(r.a->LocalId()) == 0 && !r.a->WasKicked());
        std::printf("    [%s] scored:%s\n", "speed hack (3x)", Kinds(r).c_str());
        Record("speed hack (3x)", true, r.b->WasKicked(), "kicked by risk score; evidence bundle parsed");
    }
    // -- teleport
    {
        Room r; CHECK(Mk(r));
        const std::uint32_t B = r.b->LocalId();
        GameObject* mine = r.b->SpawnOwned(crate, {0.0f, 0.0f, 0.0f});
        r.Pump(40);
        for (int i = 0; i < 40; ++i) { mine->transform->localPosition.x += 0.08f; r.Pump(1); }
        for (int k = 0; k < 4; ++k) { mine->transform->localPosition.x += 120.0f; r.Pump(60); }
        CHECK(r.Scored(B, DetectKind::Teleport) + r.Scored(B, DetectKind::SpeedBurst) > 0);
        CHECK(r.Det().Action(B) >= DetectAction::Flag || r.b->WasKicked());
        std::printf("    [%s] scored:%s\n", "teleport (+120 u x4)", Kinds(r).c_str());
        Record("teleport (+120 u x4)", true, r.Det().Action(B) >= DetectAction::Flag || r.b->WasKicked(), "risk >= flag");
    }
    // -- fly (the server's ground query is flat ground)
    {
        Room r; CHECK(Mk(r));
        r.srv->anticheat.detector.hooks.groundAt = [](const Vec3&, float& y) { y = 0.0f; return true; };
        const std::uint32_t B = r.b->LocalId();
        GameObject* mine = r.b->SpawnOwned(crate, {0.0f, 0.0f, 0.0f});
        r.Pump(40);
        for (int i = 0; i < 250 && !r.b->WasKicked(); ++i) { if (i > 20) mine->transform->localPosition.y = std::min(12.0f, mine->transform->localPosition.y + 0.06f); r.Pump(1); }
        CHECK(r.Scored(B, DetectKind::Fly) + r.Scored(B, DetectKind::WallClimb) > 0);
        std::printf("    [%s] scored:%s\n", "fly (climb to 12 m, hover)", Kinds(r).c_str());
        Record("fly (climb to 12 m, hover)", true, r.Scored(B, DetectKind::Fly) + r.Scored(B, DetectKind::WallClimb) > 0, "fly / wall-climb evidence");
    }
    // -- no-clip: a real static wall collider in the server's scene; it walks back and forth through it
    {
        Room r; CHECK(Mk(r));
        GameObject* wall = r.ss.CreateGameObject("Wall");
        wall->transform->localPosition = Vec3(10.0f, 0.0f, 0.0f);
        wall->AddComponent<BoxCollider3D>()->size = Vec3(1.0f, 10.0f, 40.0f);
        const std::uint32_t B = r.b->LocalId();
        GameObject* mine = r.b->SpawnOwned(crate, {4.0f, 0.5f, 0.0f});
        r.Pump(40);
        float dir = 1.0f;
        for (int i = 0; i < 400 && !r.b->WasKicked(); ++i) {
            float& x = mine->transform->localPosition.x;
            x += dir * 3.5f * 0.02f;
            if (x > 16.0f) dir = -1.0f;
            if (x < 4.0f) dir = 1.0f;
            r.Pump(1);
        }
        CHECK(r.Scored(B, DetectKind::NoClip) > 0);
        std::printf("    [%s] scored:%s\n", "no-clip through a collider", Kinds(r).c_str());
        Record("no-clip through a collider", true, r.Scored(B, DetectKind::NoClip) > 0, "server linecast against static colliders");
        // the honest client walking up to the same wall is not touched
    }
    // -- hits: rapid fire, and shots through a wall
    {
        Room r; CHECK(Mk(r));
        GameObject* dummy = r.ss.CreateGameObject("Dummy");
        dummy->transform->localPosition = Vec3(10.0f, 0.0f, 0.0f);
        dummy->AddComponent<NetworkSync>()->netId = "dummy";
        auto* hp = dummy->AddComponent<HealthStat>(); hp->health = hp->maxHealth = 1e6f;
        const std::uint32_t B = r.b->LocalId();
        // the cheater's client needs the target too (it claims hits against it)
        GameObject* copy = r.cb.CreateGameObject("Dummy");
        copy->transform->localPosition = Vec3(10.0f, 0.0f, 0.0f);
        copy->AddComponent<NetworkSync>()->netId = "dummy";
        copy->AddComponent<HealthStat>();
        r.Pump(60);
        const Vec3 at(0.0f, 0.5f, 0.0f), east(1.0f, 0.0f, 0.0f);
        for (int i = 0; i < 160 && !r.b->WasKicked(); ++i) { r.b->ClaimHit("dummy", "rifle", at, east); r.Pump(1); }   // 50 claims a second, the rifle is 10 a second
        CHECK(r.Scored(B, DetectKind::FireRate) > 0);
        std::printf("    [%s] scored:%s\n", "rapid fire (50/s vs a 10/s rifle)", Kinds(r).c_str());
        Record("rapid fire (50/s vs a 10/s rifle)", true, r.Scored(B, DetectKind::FireRate) > 0, "fire-rate bucket per weapon");

        Room w; CHECK(Mk(w));
        GameObject* d2 = w.ss.CreateGameObject("Dummy");
        d2->transform->localPosition = Vec3(10.0f, 0.0f, 0.0f);
        d2->AddComponent<NetworkSync>()->netId = "dummy";
        d2->AddComponent<HealthStat>();
        GameObject* c2 = w.cb.CreateGameObject("Dummy");
        c2->transform->localPosition = Vec3(10.0f, 0.0f, 0.0f);
        c2->AddComponent<NetworkSync>()->netId = "dummy";
        c2->AddComponent<HealthStat>();
        GameObject* wall = w.ss.CreateGameObject("Wall");
        wall->transform->localPosition = Vec3(5.0f, 0.0f, 0.0f);
        wall->AddComponent<BoxCollider3D>()->size = Vec3(1.0f, 4.0f, 4.0f);
        w.Pump(60);
        const std::uint32_t WB = w.b->LocalId();
        for (int i = 0; i < 12 && !w.b->WasKicked(); ++i) { w.b->ClaimHit("dummy", "rifle", at, east); w.Pump(12); }
        CHECK(w.Scored(WB, DetectKind::ThroughWall) > 0);
        CHECK(w.srv->HitsApplied() == 0);
        std::printf("    [%s] scored:%s\n", "hits through a wall (server raycast)", Kinds(w).c_str());
        Record("hits through a wall (server raycast)", true, w.Scored(WB, DetectKind::ThroughWall) > 0, "refused and scored");
    }
    // -- forged ownership: B drives and writes A's things
    {
        Room r; CHECK(Mk(r));
        const std::uint32_t A = r.a->LocalId(), B = r.b->LocalId();
        GameObject* crateA = r.a->SpawnOwned(crate, {1.0f, 0.0f, 0.0f});
        const std::string aId = crateA ? crateA->GetComponent<NetworkSync>()->netId : "";
        r.Pump(40);
        crateA->GetComponent<NetworkSync>()->SetOwned(false);
        for (int i = 0; i < 60 && !r.b->WasKicked(); ++i) { r.b->Send("__xform", aId + "|99 0 0 0 0 0 1"); r.b->Send("__despawn", aId); r.Pump(3); }
        CHECK(r.Scored(B, DetectKind::Violation) > 0 && r.Det().Action(B) >= DetectAction::Flag);
        CHECK(r.AnyScored(A) == 0);
        std::printf("    [%s] scored:%s\n", "forged transform / despawn of another player's object", Kinds(r).c_str());
        Record("forged transform / despawn of another player's object", true, r.Det().Action(B) >= DetectAction::Flag, "refused + risk score");
    }
    // -- ticket forgery: the join gate
    {
        std::vector<std::uint8_t> pk(32, 7);
        Scene ss("S");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        AntiCheatSettings s; AntiCheatSettings::Parse("enabled true\nticketAudience arena-1\n", s);
        s.ticketKey = SessionTicket::Base64UrlEncode(pk);
        srv->anticheat.settings = s;
        CHECK(srv->StartServer(0));
        ss.Start();
        int rejected = 0, joined = 0;
        for (int i = 0; i < 3; ++i) {   // random tokens, a token shaped right but signed with junk, an empty one
            Scene cs("C");
            auto* c = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
            const std::string tok = i == 0 ? "okt1.c3ViPXVzZXItYiZhdWQ9YXJlbmEtMSZpYXQ9MTgwMDAwMDAwMCZleHA9MTgwMDAwMDYwMA.AAAA" : (i == 1 ? "not-a-ticket" : "");
            c->SetAuthToken(tok);
            c->StartClient("127.0.0.1", srv->ServerPort());
            cs.Start();
            for (int f = 0; f < 200 && !c->JoinRejected() && !c->Joined(); ++f) { ss.Update(0.02f); cs.Update(0.02f); Nap(); }
            rejected += c->JoinRejected(); joined += c->Joined();
            c->Stop();
            for (int f = 0; f < 60; ++f) { ss.Update(0.02f); Nap(1); }
        }
        CHECK(joined == 0 && rejected >= 2);
        Record("ticket forgery (3 bad tokens)", true, joined == 0, "refused at the join gate");
        srv->Stop();
    }
    // -- inventory duplication: the client claims a currency from nowhere
    {
        Room r; CHECK(Mk(r));
        const std::uint32_t B = r.b->LocalId();
        // the item duplicate: the game's RPC handler reports each acquisition
        r.srv->OnRpc("trade/receive", [&](std::uint32_t peer, const std::string& uid) {
            r.srv->anticheat.detector.OnItemAcquire(peer, uid, "sword", r.srv->anticheat.Clock());
        });
        r.a->RpcReliable("trade/receive", "uid-77"); r.Pump(10);
        r.b->RpcReliable("trade/receive", "uid-77"); r.Pump(10);     // the same unique item, held by two players
        CHECK(r.Scored(B, DetectKind::DuplicateItem) == 1);
        std::printf("    [%s] scored:%s\n", "inventory duplication (currency + item uid)", Kinds(r).c_str());
        r.srv->anticheat.detector.SetBalance(B, "player.coins", 100.0f, r.srv->anticheat.Clock());
        r.b->SetVar("player.coins", "100"); r.Pump(10);
        r.b->SetVar("player.coins", "90"); r.Pump(10);       // spent some: fine
        CHECK(r.Scored(B, DetectKind::EconomyDelta) == 0);
        for (int i = 0; i < 6; ++i) { r.b->SetVar("player.coins", std::to_string(10000 * (i + 1))); r.Pump(40); }
        CHECK(r.Scored(B, DetectKind::EconomyDelta) > 0);
        Record("inventory duplication (currency + item uid)", true, r.Scored(B, DetectKind::EconomyDelta) > 0 && r.Scored(B, DetectKind::DuplicateItem) > 0, "expected-balance + unique-id ledger");
    }
    // -- malformed fuzz and a replayed sealed packet
    {
        Room r; CHECK(Mk(r));
        const std::uint32_t A = r.a->LocalId(), B = r.b->LocalId();
        SimRng rng(99);
        const char* channels[] = {"__xform", "__hit", "__spawn", "__despawn", "__rpc/fire", "chat", "__dmg", "__respawn"};
        for (int i = 0; i < 400; ++i) {
            std::string junk((std::size_t)(rng.F() * 60.0f), 'x');
            for (char& c : junk) c = (char)(rng.F() * 255.0f);
            const std::string ch = channels[(int)(rng.F() * 8.0f) % 8];
            if (i % 3 == 0) r.b->SendReliable(ch, junk); else r.b->Send(ch, junk);
            if (i % 4 == 0) r.b->SendReliable("__hit", "dummy|rifle|1|nope|1 0 0");
            r.Pump(1);
        }
        r.Pump(20);
        // the server is alive, the honest client untouched, and the fuzzer scored
        CHECK(r.srv->IsServer());
        CHECK(r.AnyScored(A) == 0 && r.a->Joined());
        CHECK(r.AnyScored(B) > 0 || r.b->WasKicked());
        std::printf("    [%s] scored:%s\n", "malformed packet fuzz (400 messages)", Kinds(r).c_str());
        Record("malformed packet fuzz (400 messages)", true, r.AnyScored(B) > 0 || r.b->WasKicked(), "survived; scored as malformed / refused");
    }
    if (net::SecureChannel::Available()) {   // a captured sealed datagram sent again, through a recording relay in the middle
        Scene ss("S");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->encryption = true;
        AntiCheatSettings s; AntiCheatSettings::Parse("enabled true\nrequireEncryption true\ndetect.enabled true\n", s);
        srv->anticheat.settings = s;
        CHECK(srv->StartServer(0));
        ss.Start();
        Scene cs("C");
        auto* c = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        c->encryption = true;
        // A relay between them records what the client sends, so it can be sent again.
        const net::Endpoint srvEp{0x7F000001u, srv->ServerPort()};
        net::Endpoint cliEp{};
        bool haveCli = false;
        std::vector<std::vector<std::uint8_t>> fromClient;
        net::UdpSocket relay;
        CHECK(relay.Open());
        const bool bound = relay.Bind(0);
        const std::uint16_t relayPort = relay.LocalPort();
        if (bound) {
            c->StartClient("127.0.0.1", relayPort);
            cs.Start();
            auto forward = [&] {
                std::uint8_t buf[65536]; net::Endpoint from; int n;
                while ((n = relay.RecvFrom(buf, sizeof buf, from)) > 0) {
                    if (from.address == srvEp.address && from.port == srvEp.port) { if (haveCli) relay.SendTo(cliEp, buf, (std::size_t)n); }
                    else { cliEp = from; haveCli = true; fromClient.emplace_back(buf, buf + n); relay.SendTo(srvEp, buf, (std::size_t)n); }
                }
            };
            for (int f = 0; f < 400 && !c->Joined(); ++f) { ss.Update(0.02f); cs.Update(0.02f); forward(); Nap(); }
            CHECK(c->Joined());
            for (int f = 0; f < 60; ++f) { c->Send("chat", "hello"); ss.Update(0.02f); cs.Update(0.02f); forward(); Nap(); }
            const std::uint32_t P = c->LocalId();
            const std::size_t have = fromClient.size();
            // replay the last 20 sealed datagrams
            for (std::size_t i = have > 20 ? have - 20 : 0; i < have; ++i) relay.SendTo(srvEp, fromClient[i].data(), fromClient[i].size());
            for (int f = 0; f < 20; ++f) { ss.Update(0.02f); cs.Update(0.02f); forward(); Nap(); }
            CHECK(srv->anticheat.detector.Count(P, DetectKind::Replay) > 0);
            Record("packet replay (sealed datagrams re-sent)", true, srv->anticheat.detector.Count(P, DetectKind::Replay) > 0, "secure-channel window -> evidence");
            c->Stop();
        }
        srv->Stop();
    }

    // ---- honest clients on the real server -------------------------------------------------
    {
        Room r; CHECK(Mk(r));
        const std::uint32_t A = r.a->LocalId(), B = r.b->LocalId();
        GameObject* ma = r.a->SpawnOwned(crate, {0.0f, 0.0f, 0.0f});
        GameObject* mb = r.b->SpawnOwned(crate, {0.0f, 0.0f, 20.0f});
        r.Pump(40);
        // both walk at 6 u/s (the limit is 8); A with a 1 s lag spike (no frames), B pausing then resuming; both respawn once
        for (int i = 0; i < 500; ++i) {
            ma->transform->localPosition.x += 6.0f * 0.02f;
            mb->transform->localPosition.z += 6.0f * 0.02f * ((i / 100) % 2 ? -1.0f : 1.0f);
            if (i >= 200 && i < 225) {   // A's packets stop for half a second (it keeps playing), then carry on
                r.ss.Update(0.02f); r.cb.Update(0.02f); Nap(r.realtimeMs);
                continue;
            }
            r.Pump(1);
        }
        r.srv->ServerTeleport(A, Vec3(0, 0, 0));
        r.Pump(30);
        CHECK(r.AnyScored(A) == 0 && r.AnyScored(B) == 0 && !r.a->WasKicked() && !r.b->WasKicked() && r.srv->anticheat.Violations(A) == 0);
        std::printf("    [%s] scored:%s\n", "honest players (walk, lag burst, teleport)", Kinds(r).c_str());
        Record("honest players (walk, lag burst, teleport)", false, r.AnyScored(A) + r.AnyScored(B) > 0, "no evidence on either");
    }
    {
        Room r; r.realtimeMs = 20;   // the simulated network runs in real time: 300 ms ping, jitter, 4% loss
        auto& sim = net::Simulator();
        sim.enabled = true; sim.latencyMs = 150.0f; sim.jitterMs = 40.0f; sim.loss = 0.04f; sim.duplicate = 0.01f;
        const bool opened = Mk(r);
        CHECK(opened);
        if (opened) {
            const std::uint32_t A = r.a->LocalId();
            GameObject* ma = r.a->SpawnOwned(crate, {0.0f, 0.0f, 0.0f});
            r.Pump(30);
            for (int i = 0; i < 300; ++i) { ma->transform->localPosition.x += 5.0f * 0.02f * ((i / 75) % 2 ? -1.0f : 1.0f); r.Pump(1); }
            CHECK(r.AnyScored(A) == 0 && r.AnyScored(r.b->LocalId()) == 0 && !r.a->WasKicked());
            std::printf("    [%s] scored:%s\n", "honest player at 300 ms ping, jitter, 4% loss, duplicates", Kinds(r).c_str());
            Record("honest player at 300 ms ping, jitter, 4% loss, duplicates", false, r.AnyScored(A) > 0, "no evidence");
        }
        sim.enabled = false; net::ResetSimulator();
    }

    std::printf("\n  Real NetworkManager server, scripted clients (loopback UDP)\n");
    std::printf("  %-62s %-9s %s\n", "script", "result", "how");
    std::printf("  %s\n", std::string(106, '-').c_str());
    for (const Result2& x : g_b) {
        const bool ok = x.cheat ? x.detected : !x.detected;
        std::printf("  %-62s %-9s %s\n", x.name.c_str(), x.cheat ? (x.detected ? "detected" : "MISSED") : (x.detected ? "FLAGGED" : "clean"), x.how.c_str());
        CHECK(ok);
    }
    std::printf("\n");
    Log::sink = sink;
    TEST_MAIN_RESULT();
}
