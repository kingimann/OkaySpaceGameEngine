#include "test_framework.hpp"
#include "okay/Net/AntiCheat.hpp"
#include "okay/Net/CheatDetect.hpp"
#include <Okay.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace okay;

// The detection engine, driven with fake time: each detector fires on the
// thing it exists for and stays quiet for the honest version of it; risk
// decays; the response ladder escalates and releases; the evidence bundle
// round-trips and survives hostile input.
namespace {
DetectSettings Base() {
    DetectSettings s;
    s.enabled = true;
    s.maxSpeed = 8.0f;
    s.gravity = 20.0f;
    s.maxJumpHeight = 1.2f;
    s.graceJoin = 0.0f;   // (the warm-up has its own test)
    return s;
}

struct Walker {   // a player walking along +x at `speed`, sampled at `hz`
    CheatDetector& d; std::uint32_t peer; double t = 0.0; Vec3 pos{0, 0, 0}; float speed; float hz = 20.0f;
    Walker(CheatDetector& det, std::uint32_t p, float sp) : d(det), peer(p), speed(sp) {}
    void Step() { t += 1.0 / hz; pos.x += speed / hz; d.OnMove(peer, "p", pos, t); }
    void Run(double seconds) { const int n = (int)(seconds * hz); for (int i = 0; i < n; ++i) Step(); }
};
int Total(const CheatDetector& d, std::uint32_t peer) {
    int n = 0;
    for (int k = 0; k < (int)DetectKind::Count; ++k) n += d.Count(peer, (DetectKind)k);
    return n;
}
void Dump(const CheatDetector& d, std::uint32_t peer) {   // what scored, for a failing check
    if (auto* ev = d.EvidenceOf(peer)) for (const Evidence& e : *ev) std::fprintf(stderr, "    %s t=%.2f value=%g threshold=%g %s\n", DetectKindName(e.kind), e.t, e.value, e.threshold, e.detail.c_str());
}
std::uint32_t g_rng = 12345;
float Rand() { g_rng = g_rng * 1664525u + 1013904223u; return (float)((g_rng >> 8) & 0xFFFF) / 65536.0f; }
} // namespace

int main() {
    RUN_SUITE("cheat_detect");

    // ---- settings: parse, round-trip, reject junk, off by default ----------------------
    {
        AntiCheatSettings s;
        std::string err;
        CHECK(!s.detect.enabled && s.detect.ToText().empty());
        CHECK(s.ToText().find("detect") == std::string::npos);   // nothing new in an untouched file
        CHECK(AntiCheatSettings::Parse("enabled true\ndetect.enabled true\ndetect.maxSpeed 8.5\ndetect.gravity 20\n"
                                       "detect.maxJumpHeight 1.2\ndetect.weapon rifle 10 30 2 300\ndetect.weapon pistol 4 20\n"
                                       "detect.ability dash 5\ndetect.economy coins 50\ndetect.weight.teleport 40\n"
                                       "detect.kickAt 70\ndetect.confirmations 3\n", s, &err));
        CHECK(s.detect.enabled && s.detect.maxSpeed == 8.5f && s.detect.weapons.size() == 2 && s.detect.abilities.size() == 1);
        CHECK(s.detect.FindWeapon("rifle") && s.detect.FindWeapon("rifle")->headMultiplier == 2.0f && s.detect.FindWeapon("rifle")->maxRange == 300.0f);
        CHECK(s.detect.weight[(int)DetectKind::Teleport] == 40.0f && s.detect.kickAt == 70.0f && s.detect.confirmations == 3);
        AntiCheatSettings back;
        CHECK(AntiCheatSettings::Parse(s.ToText(), back) && back.ToText() == s.ToText());
        AntiCheatSettings keep = s;
        CHECK(!AntiCheatSettings::Parse("detect.maxSpeed fast\n", keep, &err) && err.find("detect.maxSpeed") != std::string::npos);
        CHECK(!AntiCheatSettings::Parse("detect.nothing 1\n", keep) && !AntiCheatSettings::Parse("detect.weight.nothing 1\n", keep));
        CHECK(!AntiCheatSettings::Parse("detect.weapon rifle 10\n", keep) && !AntiCheatSettings::Parse("detect.maxSpeed -1\n", keep));
        CHECK(!AntiCheatSettings::Parse("detect.maxSpeed nan\n", keep) && !AntiCheatSettings::Parse("detect.confirmations 2.5\n", keep));
        CHECK(!AntiCheatSettings::Parse("detect.enabled maybe\n", keep) && !AntiCheatSettings::Parse("detect.ability dash\n", keep));
        CHECK(keep.ToText() == s.ToText());                      // a bad file changes nothing
        for (int k = 0; k < (int)DetectKind::Count; ++k) {
            DetectKind back2;
            CHECK(DetectKindFromName(DetectKindName((DetectKind)k), back2) && back2 == (DetectKind)k);
        }
        DetectKind none;
        CHECK(!DetectKindFromName("speedhack", none));
    }

    // ---- the Anti-Cheat Config component keeps the detection keys in a saved scene ------------
    {
        const std::string path = "okay_cheat_detect_scene.okayscene";
        {
            Scene sc("S");
            auto* ac = sc.CreateGameObject("Rules")->AddComponent<AntiCheatConfig>();
            std::string err;
            CHECK(AntiCheatSettings::Parse("enabled true\ndetect.enabled true\ndetect.maxSpeed 7.5\ndetect.weapon rifle 10 30 2 300\n"
                                           "detect.ability dash 5\ndetect.economy coins 2\ndetect.kickAt 70\ndetect.weight.fly 15\n", ac->settings, &err));
            CHECK(SceneSerializer::SaveToFile(sc, path));
        }
        Scene back("B");
        CHECK(SceneSerializer::LoadFromFile(back, path));
        AntiCheatConfig* ac = nullptr;
        for (const auto& g : back.Objects()) if (auto* c = g->GetComponent<AntiCheatConfig>()) ac = c;
        CHECK(ac != nullptr);
        if (ac) {
            const DetectSettings& d = ac->settings.detect;
            CHECK(d.enabled && d.maxSpeed == 7.5f && d.kickAt == 70.0f && d.weight[(int)DetectKind::Fly] == 15.0f);
            CHECK(d.weapons.size() == 1 && d.weapons[0].maxRange == 300.0f && d.abilities.size() == 1 && d.economy.size() == 1);
        }
        std::remove(path.c_str());
        Scene plain("P");   // a scene saved without any detect keys loads with the engine off
        plain.CreateGameObject("R")->AddComponent<AntiCheatConfig>();
        CHECK(SceneSerializer::SaveToFile(plain, path));
        Scene again("A");
        CHECK(SceneSerializer::LoadFromFile(again, path));
        for (const auto& g : again.Objects()) if (auto* c = g->GetComponent<AntiCheatConfig>()) CHECK(!c->settings.detect.enabled);
        std::remove(path.c_str());
    }

    // ---- off: nothing is stored ---------------------------------------------------------
    {
        CheatDetector d;
        d.OnMove(1, "p", Vec3(0, 0, 0), 0.0); d.OnMove(1, "p", Vec3(900, 0, 0), 0.05);
        d.OnShot(1, ShotEvent{});
        CHECK(d.Peers() == 0 && d.Risk(1) == 0.0f && d.Action(1) == DetectAction::None);
        d.settings.enabled = true;
        CHECK(d.OnMessage(1, 0.0));
        d.settings.enabled = false;
        d.Reset();
        CHECK(d.Peers() == 0);
    }

    // ---- movement ---------------------------------------------------------------------
    {   // honest walker at the speed limit, with jitter, at 300 ms ping: never flagged
        CheatDetector d; d.settings = Base();
        d.SetPing(1, 0.3f);
        double t = 0.0; Vec3 pos(0, 0, 0);
        for (int i = 0; i < 600; ++i) {
            const double step = 0.05 + (Rand() - 0.5) * 0.03;   // 20 Hz with +-15 ms of jitter
            t += step; pos.x += 8.0f * (float)step; pos.z += 0.5f * (float)step;
            d.OnMove(1, "p", pos, t);
        }
        CHECK(Total(d, 1) == 0 && d.Risk(1) == 0.0f);
    }
    {   // a speed hack at 2x is caught within two seconds and escalates
        CheatDetector d; d.settings = Base();
        Walker w(d, 1, 8.0f); w.Run(5.0);
        CHECK(Total(d, 1) == 0);
        w.speed = 16.0f;
        double caught = -1.0;
        for (int i = 0; i < 400 && d.Action(1) < DetectAction::Kick; ++i) { w.Step(); if (caught < 0.0 && Total(d, 1) > 0) caught = w.t - 5.0; }
        CHECK(caught > 0.0 && caught < 2.0);
        CHECK(d.Action(1) >= DetectAction::Kick && d.KickRequested(1));
        CHECK(d.Count(1, DetectKind::SpeedWindow) + d.Count(1, DetectKind::SpeedBurst) > 0);
        const Evidence& e = d.EvidenceOf(1)->back();
        CHECK(e.value > e.threshold && e.weight > 0.0f && !e.snaps.empty());
    }
    {   // a new player is not judged for graceJoin seconds (its first positions are re-anchored)
        CheatDetector d; d.settings = Base(); d.settings.graceJoin = 3.0f;
        d.NoteJoin(1, 0.0);
        Walker w(d, 1, 8.0f); w.Run(1.0);
        w.pos.x += 90.0f; w.Step();
        CHECK(Total(d, 1) == 0);
        w.Run(4.0);
        w.pos.x += 90.0f; w.Step();
        CHECK(d.Count(1, DetectKind::Teleport) == 1);
    }
    {   // a mild 1.4x speed hack: the windows catch what a tolerant per-tick check would not
        CheatDetector d; d.settings = Base();
        Walker w(d, 1, 11.2f); w.Run(12.0);
        CHECK(d.Count(1, DetectKind::SpeedWindow) > 0);
    }
    {   // teleport; the same jump right after a legitimate teleport / respawn / host migration is not
        CheatDetector d; d.settings = Base();
        Walker w(d, 1, 8.0f); w.Run(4.0);
        w.pos.x += 90.0f; w.Step();
        CHECK(d.Count(1, DetectKind::Teleport) == 1 && d.Action(1) >= DetectAction::Flag);

        CheatDetector e; e.settings = Base();
        Walker v(e, 1, 8.0f); v.Run(4.0);
        e.NoteTeleport(1, v.t); v.pos.x += 90.0f; v.Run(3.0);
        e.NoteRespawn(1, v.t); v.pos.x -= 200.0f; v.Run(4.0);
        e.NoteHostMigration(v.t); v.pos.z += 300.0f; v.Run(8.0);
        CHECK(Total(e, 1) == 0);
    }
    {   // a speed boost the game grants is allowed, only until it ends
        CheatDetector d; d.settings = Base();
        Walker w(d, 1, 8.0f); w.Run(3.0);
        d.NoteSpeedBoost(1, 2.0f, w.t + 5.0); w.speed = 15.0f; w.Run(4.0);
        CHECK(Total(d, 1) == 0);
        w.Run(8.0);
        CHECK(Total(d, 1) > 0);
    }
    {   // lag spikes: silence then a burst of the positions queued meanwhile
        CheatDetector d; d.settings = Base();
        d.SetPing(1, 0.3f);
        Walker w(d, 1, 7.0f); w.Run(4.0);
        for (int spike = 0; spike < 3; ++spike) {
            const int queued = 24;                               // 1.2 s
            for (int i = 0; i < queued; ++i) w.pos.x += 7.0f / 20.0f;
            w.t += 1.2;
            Vec3 p = w.pos; p.x -= 7.0f / 20.0f * (queued - 1);
            for (int i = 0; i < queued; ++i) { d.OnMove(1, "p", p, w.t); p.x += 7.0f / 20.0f; }
            w.Run(6.0);
        }
        CHECK(Total(d, 1) == 0);
        // ...but a client that fakes silence to bank a jump gets nothing for it
        CheatDetector c; c.settings = Base();
        Walker v(c, 1, 8.0f); v.Run(4.0);
        v.t += 0.5; v.pos.x += 60.0f; c.OnMove(1, "p", v.pos, v.t);
        CHECK(c.Count(1, DetectKind::Teleport) == 1);
    }
    {   // fly / wall-climb with a ground query
        auto mk = [](CheatDetector& d) { d.settings = Base(); d.hooks.groundAt = [](const Vec3&, float& y) { y = 0.0f; return true; }; };
        CheatDetector honest; mk(honest);
        double t = 0.0;
        for (int jump = 0; jump < 6; ++jump) {                     // stand, jump (a parabola), land
            for (int i = 0; i < 20; ++i) { t += 0.05; honest.OnMove(1, "p", Vec3((float)t, 0.0f, 0.0f), t); }
            const float v0 = std::sqrt(2.0f * 20.0f * 1.2f);
            for (double a = 0.05; a < 2.0 * v0 / 20.0; a += 0.05) { t += 0.05; honest.OnMove(1, "p", Vec3((float)t, v0 * (float)a - 10.0f * (float)(a * a), 0.0f), t); }
        }
        { // a long fall off a 12 m ledge: it stands on the platform, steps off, falls
            float ledge = 1e9f;
            honest.hooks.groundAt = [&](const Vec3& p, float& y) { y = p.x < ledge ? 12.0f : 0.0f; return true; };
            const float h0 = 12.0f;
            for (int i = 0; i < 20; ++i) { t += 0.05; honest.OnMove(1, "p", Vec3((float)t, h0, 0.0f), t); }
            ledge = (float)t + 0.01f;
            for (double a = 0.05; a < std::sqrt(2.0f * h0 / 20.0f); a += 0.05) { t += 0.05; honest.OnMove(1, "p", Vec3((float)t, h0 - 10.0f * (float)(a * a), 0.0f), t); }
            t += 0.05; honest.OnMove(1, "p", Vec3((float)t, 0.0f, 0.0f), t);
        }
        if (Total(honest, 1)) Dump(honest, 1);
        CHECK(Total(honest, 1) == 0);

        CheatDetector hover; mk(hover);                            // flying at a steady 4 m
        double th = 0.0;
        for (int i = 0; i < 60; ++i) { th += 0.05; hover.OnMove(1, "p", Vec3((float)th * 3.0f, i < 20 ? 0.2f * i : 4.0f, 0.0f), th); }
        CHECK(hover.Count(1, DetectKind::Fly) + hover.Count(1, DetectKind::WallClimb) > 0);

        CheatDetector low; mk(low);                                // hovering just under jump height, never falling
        double tl = 0.0;
        for (int i = 0; i < 80; ++i) { tl += 0.05; low.OnMove(1, "p", Vec3(0.0f, i < 8 ? 0.2f * i : 1.4f, 0.0f), tl); }
        CHECK(low.Count(1, DetectKind::Fly) > 0);

        CheatDetector wall; mk(wall);                              // straight up a wall at 3 u/s
        double tw = 0.0;
        for (int i = 0; i < 80; ++i) { tw += 0.05; wall.OnMove(1, "p", Vec3(5.0f, 0.15f * i, 0.0f), tw); }
        CHECK(wall.Count(1, DetectKind::WallClimb) > 0);

        CheatDetector allowed; mk(allowed);                        // a jetpack the game allows
        allowed.NoteFlightAllowed(1, 100.0);
        double ta = 0.0;
        for (int i = 0; i < 80; ++i) { ta += 0.05; allowed.OnMove(1, "p", Vec3(0.0f, 0.15f * i, 0.0f), ta); }
        CHECK(Total(allowed, 1) == 0);
    }
    {   // no ground query: only a steep sustained climb is impossible; stairs / ramps are fine
        CheatDetector d; d.settings = Base();
        double t = 0.0;
        for (int i = 0; i < 200; ++i) { t += 0.05; d.OnMove(1, "ramp", Vec3(8.0f * (float)t, 0.45f * 8.0f * (float)t, 0.0f), t); }   // slope 0.45
        CHECK(Total(d, 1) == 0);
        CheatDetector c; c.settings = Base();
        t = 0.0;
        for (int i = 0; i < 60; ++i) { t += 0.05; c.OnMove(1, "wall", Vec3(0.0f, 3.0f * (float)t, 0.0f), t); }
        CHECK(c.Count(1, DetectKind::WallClimb) > 0);
    }
    {   // no-clip: a step through a static solid, twice in a few seconds
        CheatDetector d; d.settings = Base();
        d.hooks.staticBlocked = [](const Vec3& a, const Vec3& b) { return (a.x - 10.0f) * (b.x - 10.0f) < 0.0f; };
        Walker w(d, 1, 6.0f);
        for (int i = 0; i < 40; ++i) { w.Step(); if (w.pos.x > 9.5f) break; }   // walks up to the wall and stops
        const double stop = w.t; (void)stop;
        w.pos.x = 9.8f; d.OnMove(1, "p", w.pos, w.t);
        w.speed = 6.0f; w.Run(1.0);                                              // crosses it ...
        CHECK(d.Count(1, DetectKind::NoClip) == 0);                              // (one hit is unconfirmed)
        w.speed = -6.0f; w.Run(1.2);                                             // ... and back
        CHECK(d.Count(1, DetectKind::NoClip) > 0);
        CheatDetector h; h.settings = Base(); h.hooks.staticBlocked = d.hooks.staticBlocked;
        Walker v(h, 1, 6.0f); v.pos.x = 0.0f; v.Run(1.5);                       // honest: stays on its side
        CHECK(Total(h, 1) == 0);
    }
    {   // ghost movement (moving with no input), honest stop, stale input
        CheatDetector d; d.settings = Base();
        Walker w(d, 1, 8.0f);
        double ti = 0.0;
        for (int i = 0; i < 80; ++i) { w.Step(); if (w.t - ti >= 0.05) { d.OnInput(1, 0.0f, 0.0f, w.t); ti = w.t; } }
        CHECK(d.Count(1, DetectKind::InputMismatch) > 0);
        CheatDetector h; h.settings = Base();
        Walker v(h, 1, 8.0f);
        for (int i = 0; i < 60; ++i) { v.Step(); h.OnInput(1, 0.0f, 1.0f, v.t); }
        v.speed = 3.0f;                                          // lets go: slides for 0.4 s, then stops
        for (int i = 0; i < 8; ++i) { v.Step(); h.OnInput(1, 0.0f, 0.0f, v.t); }
        v.speed = 0.0f;
        for (int i = 0; i < 80; ++i) { v.Step(); h.OnInput(1, 0.0f, 0.0f, v.t); }
        CHECK(Total(h, 1) == 0);
        CheatDetector s; s.settings = Base();                    // input stopped arriving: says nothing
        Walker x(s, 1, 8.0f); s.OnInput(1, 0.0f, 0.0f, 0.0); x.Run(5.0);
        CHECK(s.Count(1, DetectKind::InputMismatch) == 0);
        CheatDetector e; e.settings = Base();
        Walker y(e, 1, 8.0f); y.Run(1.0);
        e.NoteExternalForce(1, y.t + 3.0); y.speed = 14.0f;      // knockback
        for (int i = 0; i < 40; ++i) { y.Step(); e.OnInput(1, 0.0f, 0.0f, y.t); }
        CHECK(Total(e, 1) == 0);
    }
    {   // jump rate
        CheatDetector d; d.settings = Base();
        for (int i = 0; i < 30; ++i) d.OnJump(1, i * 0.4);        // 2.5 per second: a bunny hopper
        CHECK(d.Count(1, DetectKind::JumpRate) == 0);
        for (int i = 0; i < 40; ++i) d.OnJump(1, 20.0 + i * 0.05);   // 20 per second
        CHECK(d.Count(1, DetectKind::JumpRate) > 0);
    }

    // ---- combat ---------------------------------------------------------------------------
    {
        auto mk = [](CheatDetector& d) {
            d.settings = Base();
            d.settings.weapons.push_back(WeaponDef{"rifle", 10.0f, 30.0f, 2.0f, 300.0f});
        };
        { CheatDetector d; mk(d);   // honest: 8 shots/s bursts, a quarter hit, one in eight a headshot
            double t = 0.0;
            for (int i = 0; i < 400; ++i) {
                t += 0.125;
                ShotEvent s; s.t = t; s.weapon = "rifle"; s.hit = Rand() < 0.45f; s.headshot = Rand() < 0.15f; s.damage = s.headshot ? 60.0f : 30.0f; s.distance = 20.0f;
                d.OnShot(1, s);
            }
            CHECK(Total(d, 1) == 0); }
        { CheatDetector d; mk(d);   // rapid fire: 30 shots/s from a 10/s weapon
            double t = 0.0;
            for (int i = 0; i < 90; ++i) { t += 1.0 / 30.0; ShotEvent s; s.t = t; s.weapon = "rifle"; s.damage = 30.0f; d.OnShot(1, s); }
            CHECK(d.Count(1, DetectKind::FireRate) > 0 && d.Action(1) >= DetectAction::Flag); }
        { CheatDetector d; mk(d);   // too much damage, beyond range, through walls (twice), out of reach
            ShotEvent s; s.t = 1.0; s.weapon = "rifle"; s.hit = true; s.damage = 300.0f; d.OnShot(1, s);
            CHECK(d.Count(1, DetectKind::DamageExceeds) == 1);
            s = ShotEvent{}; s.t = 5.0; s.weapon = "rifle"; s.hit = true; s.damage = 60.0f; s.headshot = true; d.OnShot(1, s);
            CHECK(d.Count(1, DetectKind::DamageExceeds) == 1);            // 60 is a legal headshot
            s = ShotEvent{}; s.t = 6.0; s.weapon = "rifle"; s.blockedByWall = true; d.OnShot(1, s);
            CHECK(d.Count(1, DetectKind::ThroughWall) == 0);              // once: unconfirmed
            s.t = 6.8; d.OnShot(1, s);
            CHECK(d.Count(1, DetectKind::ThroughWall) == 1);
            s = ShotEvent{}; s.weapon = "rifle"; s.hit = true; s.distance = 900.0f;
            s.t = 10.0; d.OnShot(1, s); s.t = 10.5; d.OnShot(1, s);
            s = ShotEvent{}; s.weapon = "rifle"; s.offRay = 5.0f; s.allowedOffRay = 1.5f; s.t = 20.0; d.OnShot(1, s); s.t = 20.5; d.OnShot(1, s);
            CHECK(d.Count(1, DetectKind::HitOutOfReach) >= 2); }
        { CheatDetector d; mk(d);   // accuracy and headshot ratio over a window
            double t = 0.0;
            for (int i = 0; i < 60; ++i) { t += 0.2; ShotEvent s; s.t = t; s.weapon = "rifle"; s.hit = true; s.headshot = i % 10 != 0; s.damage = 60.0f; s.distance = 30.0f; d.OnShot(1, s); }
            CHECK(d.Count(1, DetectKind::Accuracy) > 0 && d.Count(1, DetectKind::Headshot) > 0); }
        { CheatDetector d; mk(d);   // a skilled honest player: 75% accuracy, 35% headshots
            double t = 0.0;
            for (int i = 0; i < 300; ++i) { t += 0.3; ShotEvent s; s.t = t; s.weapon = "rifle"; s.hit = Rand() < 0.75f; s.headshot = Rand() < 0.35f; s.damage = 30.0f; s.distance = 30.0f; d.OnShot(1, s); }
            CHECK(Total(d, 1) == 0); }
        { CheatDetector d; mk(d);   // aim snap: a flick then a hit, repeatedly; a single flick shot is honest
            double t = 0.0; float yaw = 0.0f;
            d.OnAim(1, yaw, 0.0f, t);
            auto flick = [&](bool shoot) {
                for (int i = 0; i < 30; ++i) { t += 0.016; d.OnMessage(1, t); }   // (traffic keeps flowing, so it isn't a lag spike)
                for (int i = 0; i < 10; ++i) { t += 0.016; yaw += 0.2f; d.OnAim(1, yaw, 0.0f, t); }
                t += 0.016; yaw += 90.0f; d.OnAim(1, yaw, 0.0f, t);            // 90 degrees in one 16 ms sample
                if (shoot) { ShotEvent s; s.t = t + 0.04; s.weapon = "rifle"; s.hit = true; s.damage = 30.0f; d.OnShot(1, s); }
            };
            flick(true);
            CHECK(Total(d, 1) == 0);
            for (int i = 0; i < 5; ++i) flick(true);
            CHECK(d.Count(1, DetectKind::AimSnap) > 0); }
        { CheatDetector d; mk(d);   // respawn grace: the camera jumps, nothing is judged
            double t = 0.0; float yaw = 0.0f;
            d.OnAim(1, yaw, 0.0f, t);
            for (int i = 0; i < 8; ++i) {
                t += 3.0; d.NoteRespawn(1, t); yaw += 120.0f; t += 0.016; d.OnAim(1, yaw, 0.0f, t);
                ShotEvent s; s.t = t + 0.03; s.weapon = "rifle"; s.hit = true; s.damage = 30.0f; d.OnShot(1, s);
            }
            CHECK(Total(d, 1) == 0); }
    }

    // ---- economy / state ----------------------------------------------------------------
    {
        CheatDetector d; d.settings = Base();
        d.settings.abilities.push_back(AbilityDef{"dash", 5.0f});
        d.settings.economy.push_back(EconomyDef{"coins", 0.0f});
        d.settings.economy.push_back(EconomyDef{"scrap", 2.0f});
        d.SetBalance(1, "coins", 100.0f, 0.0);
        d.OnGrant(1, "coins", 50.0f, 1.0);
        d.OnClaimedValue(1, "coins", 150.0f, 1.5);
        d.OnClaimedValue(1, "coins", 120.0f, 3.0);                // spent some
        CHECK(Total(d, 1) == 0);
        d.OnClaimedValue(1, "coins", 5000.0f, 4.0);               // from nowhere
        CHECK(d.Count(1, DetectKind::EconomyDelta) == 1 && d.Action(1) >= DetectAction::Flag);
        d.SetBalance(1, "scrap", 0.0f, 0.0);                      // a counter with an allowed rate: 2 / s
        d.OnClaimedValue(1, "scrap", 9.0f, 5.0);
        CHECK(d.Count(1, DetectKind::EconomyDelta) == 1);
        d.OnClaimedValue(1, "scrap", 300.0f, 6.0);
        CHECK(d.Count(1, DetectKind::EconomyDelta) == 2);
        d.OnClaimedValue(1, "coins", std::nanf(""), 7.0);         // not a number: malformed, not a crash
        CHECK(d.Count(1, DetectKind::EconomyDelta) == 2);

        d.OnItemAcquire(2, "uid-1", "sword", 10.0);
        d.OnItemRelease(2, "uid-1");
        d.OnItemAcquire(3, "uid-1", "sword", 11.0);               // a trade: released first
        CHECK(d.Count(3, DetectKind::DuplicateItem) == 0);
        d.OnItemAcquire(2, "uid-1", "sword", 12.0);               // ... but the old holder still has it too
        CHECK(d.Count(2, DetectKind::DuplicateItem) == 1);
        d.OnItemAcquire(3, "uid-1", "sword", 13.0);               // held twice by the same player
        CHECK(d.Count(3, DetectKind::DuplicateItem) == 1);
        for (int i = 0; i < 30; ++i) d.OnItemAcquire(4, "", "potion", 20.0 + i * 0.02);
        CHECK(d.Count(4, DetectKind::DuplicateItem) > 0);

        d.OnAbility(5, "dash", 0.0);
        d.OnAbility(5, "dash", 5.2);                              // honest
        d.OnAbility(5, "dash", 10.5);
        CHECK(Total(d, 5) == 0);
        for (int i = 0; i < 6; ++i) d.OnAbility(5, "dash", 11.0 + i * 1.0);
        CHECK(d.Count(5, DetectKind::CooldownBypass) > 0);
        d.OnAbility(5, "unknown", 50.0);                          // no definition: nothing to check
    }

    // ---- network ---------------------------------------------------------------------------
    {
        CheatDetector d; d.settings = Base();
        for (int i = 0; i < 600; ++i) d.OnMessage(1, i / 60.0);   // 60 / s for ten seconds
        CHECK(Total(d, 1) == 0);
        int dropped = 0;
        for (int i = 0; i < 3000; ++i) dropped += !d.OnMessage(2, 20.0 + i / 600.0);   // 600 / s
        CHECK(d.Count(2, DetectKind::MessageBurst) > 0 && dropped > 0 && d.RateLimited(2));
        CHECK(dropped < 3000);                                    // (not everything: it only drops past the reduced budget)

        CheatDetector r; r.settings = Base();
        CHECK(r.OnSequence(1, 5, 0.0) && r.OnSequence(1, 7, 0.1) && r.OnSequence(1, 6, 0.2));   // late but new
        CHECK(!r.OnSequence(1, 6, 0.3) && !r.OnSequence(1, 7, 0.4));
        CHECK(r.Count(1, DetectKind::Replay) >= 1);
        for (std::uint64_t i = 8; i < 400; ++i) r.OnSequence(1, i, 1.0 + i * 0.01);
        CHECK(!r.OnSequence(1, 8, 9.0));                          // far too old

        CheatDetector m; m.settings = Base();
        for (int i = 0; i < 10; ++i) m.OnMalformed(1, "junk", i * 1.0);
        CHECK(Total(m, 1) == 0);                                  // a few are normal
        for (int i = 0; i < 30; ++i) m.OnMalformed(1, "junk", 10.0 + i * 0.5);
        CHECK(m.Count(1, DetectKind::Malformed) > 0);

        CheatDetector t; t.settings = Base();
        for (std::uint64_t i = 0; i < 100; ++i) t.OnClientTick(1, 1000 + i, i * 0.016);
        t.OnClientTick(1, 500, 2.0); t.OnClientTick(1, 100, 2.5);
        CHECK(t.Count(1, DetectKind::DesyncForce) > 0);
        CheatDetector c; c.settings = Base();
        for (int i = 0; i < 30; ++i) c.OnCorrection(1, i * 0.5);
        CHECK(c.Count(1, DetectKind::DesyncForce) > 0);

        CheatDetector k; k.settings = Base();                     // a clock that runs 1.5x
        for (int i = 0; i <= 80; ++i) k.OnClock(1, i * 0.5 * 1.5, i * 0.5);
        CHECK(k.Count(1, DetectKind::TimeManipulation) > 0);
        CheatDetector h; h.settings = Base(); h.SetPing(1, 0.3f);
        for (int i = 0; i <= 120; ++i) h.OnClock(1, i * 0.5 * 1.03 + (Rand() - 0.5) * 0.06, i * 0.5);   // 3% crystal drift, 30 ms jitter
        CHECK(Total(h, 1) == 0);
    }

    // ---- suspicion model ----------------------------------------------------------------
    {
        CheatDetector d; d.settings = Base();
        std::vector<DetectAction> seen;
        d.onResponse = [&](const DetectResponse& r) { seen.push_back(r.action); };
        int evs = 0;
        d.onEvidence = [&](std::uint32_t, const Evidence&) { ++evs; };
        d.OnViolation(1, "forged transform", "car", 0.0);
        const float one = d.Risk(1);
        CHECK(one >= 5.0f && evs == 1);
        // throttled: a flood of the same kind within the interval scores once
        for (int i = 0; i < 100; ++i) d.OnViolation(1, "forged transform", "car", 0.01 * i * 0.04);
        CHECK(evs == 1 && d.Suppressed(1, DetectKind::Violation) > 50);
        // it decays: half after one half-life
        d.Advance(90.0);
        CHECK(d.Risk(1) < one * 0.55f && d.Risk(1) > one * 0.45f);
        // a ladder: each rung is announced once, in order, and the lower ones release as risk falls
        for (int i = 0; i < 40; ++i) d.OnViolation(1, "x", "", 100.0 + i);
        CHECK(!seen.empty() && d.Action(1) >= DetectAction::Flag);
        for (std::size_t i = 1; i < seen.size(); ++i) CHECK(seen[i] > seen[i - 1]);
        const DetectAction peak = d.Action(1);
        if (peak < DetectAction::Kick) {
            d.Advance(100.0 + 40.0 + 600.0);
            CHECK(d.Action(1) < peak && d.Risk(1) < 5.0f);
        }
        // Kick and BanRequest latch (the peer is gone); rungs set to 0 are off
        CheatDetector k; k.settings = Base(); k.settings.kickAt = 20.0f; k.settings.banAt = 0.0f;
        k.OnViolation(1, "a", "", 0.0, 10.0f);
        CHECK(k.KickRequested(1) && !k.BanRequested(1));
        k.Advance(100000.0);
        CHECK(k.KickRequested(1));
        k.ForgetPeer(1);
        CHECK(k.Action(1) == DetectAction::None);
        // the evidence ring is capped; the snapshot ring is time- and count-capped
        CheatDetector e; e.settings = Base(); e.settings.evidenceCap = 8; e.settings.evidenceInterval = 0.0f;
        Walker w(e, 1, 8.0f); w.Run(20.0);
        for (int i = 0; i < 100; ++i) e.OnViolation(1, "v", "", 20.0 + i);
        CHECK(e.EvidenceOf(1)->size() == 8);
        const auto ring = e.SnapshotsOf(1);
        CHECK(!ring.empty() && ring.size() <= 160 && ring.back().t - ring.front().t <= e.settings.snapshotSeconds + 0.2);
        // risk is parked with a leaving peer (AntiCheat): leaving does not reset it
        AntiCheat ac; ac.settings.enabled = true; ac.settings.detect = Base(); ac.ApplyDetectSettings();
        ac.RestorePeer(7, "acct-7");
        ac.detector.OnViolation(7, "x", "", ac.Clock(), 3.0f);
        const float r0 = ac.detector.Risk(7);
        ac.ParkPeer(7, "acct-7"); ac.ForgetPeer(7);
        CHECK(ac.detector.Risk(7) == 0.0f);
        ac.RestorePeer(8, "acct-7");
        CHECK(ac.detector.Risk(8) > r0 * 0.9f);
    }

    // ---- AntiCheat feeds the detector ----------------------------------------------------
    {
        AntiCheat ac;
        ac.settings.enabled = true;
        ac.settings.maxSyncSpeed = 0.0f;
        ac.settings.detect = Base();
        ac.ApplyDetectSettings();
        ac.exists = [](const std::string&) { return true; };
        ac.SetOwner("p1", 3);
        // refusals become violations in the risk score
        ac.ValidateMessage(4, "__xform", "p1|1 2 3 0 0 0 1");
        CHECK(ac.detector.Count(4, DetectKind::Violation) == 1);
        // moves become samples: a teleport shows up
        for (int i = 0; i < 40; ++i) { ac.Tick(0.05f); ac.ValidateMessage(3, "__xform", "p1|" + std::to_string(i * 0.3) + " 0 0 0 0 0 1"); }
        CHECK(ac.detector.Count(3, DetectKind::Teleport) == 0);
        ac.Tick(0.05f); ac.ValidateMessage(3, "__xform", "p1|400 0 0 0 0 0 1");
        CHECK(ac.detector.Count(3, DetectKind::Teleport) == 1);
        // maxSyncSpeed is the speed limit when detect.maxSpeed isn't set
        AntiCheat b; b.settings.enabled = true; b.settings.maxSyncSpeed = 5.0f; b.settings.detect.enabled = true; b.ApplyDetectSettings();
        CHECK(b.detector.settings.maxSpeed == 5.0f);
        // detect off: the AntiCheat behaves exactly as before and stores nothing
        AntiCheat off; off.settings.enabled = true; off.exists = ac.exists; off.SetOwner("p1", 3);
        for (int i = 0; i < 5; ++i) off.ValidateMessage(3, "__xform", "p1|" + std::to_string(i * 50) + " 0 0 0 0 0 1");
        off.ValidateMessage(4, "__xform", "p1|1 2 3 0 0 0 1");
        CHECK(off.detector.Peers() == 0 && off.TotalReports() == 1);
    }

    // ---- evidence bundle -----------------------------------------------------------------
    {
        CheatDetector d; d.settings = Base(); d.settings.evidenceInterval = 0.0f;
        Walker w(d, 4, 8.0f); w.Run(3.0);
        d.OnViolation(4, "forged transform", "okt1.c3ViPXVzZXItYiZhdWQ9KiZpYXQ9MTgwMDAwMDAwMCZleHA9MTgwMDAwMDA2MA.LAZeykukw34CuxXjIc6hGhvKl2TAl5PuJc", w.t);
        w.pos.x += 100.0f; w.Step();
        d.SetPing(4, 0.12f);
        BundleOptions o; o.server = "arena-3"; o.game = "demo";
        EvidenceBundle b = d.Bundle(4, o, "acct-secret-account-id", "203.0.113.9:4000");
        CHECK(b.peer == 4 && b.evidence.size() >= 2 && !b.snaps.empty() && b.account.empty() && b.address.empty());
        const std::string json = b.ToJson();
        CHECK(json.find("203.0.113.9") == std::string::npos && json.find("acct-secret") == std::string::npos);
        CHECK(json.find("okt1.") == std::string::npos && json.find("[redacted]") != std::string::npos);   // a ticket never rides along
        CHECK(json.find("arena-3") != std::string::npos);
        EvidenceBundle back; std::string err;
        CHECK(EvidenceBundle::Parse(json, back, &err));
        CHECK(back.peer == 4 && back.evidence.size() == b.evidence.size() && back.snaps.size() == b.snaps.size());
        CHECK(back.evidence[0].kind == b.evidence[0].kind && back.server == "arena-3" && back.action == b.action);
        CHECK_NEAR(back.risk, b.risk, 0.01);
        CHECK_NEAR(back.snaps.back().pos.x, b.snaps.back().pos.x, 0.01);
        CHECK(back.ToJson() == EvidenceBundle(back).ToJson());                   // stable
        // identity only on opt-in
        BundleOptions in = o; in.includeAccount = true; in.includeAddress = true;
        const std::string withId = d.Bundle(4, in, "acct-9", "203.0.113.9:4000").ToJson();
        CHECK(withId.find("acct-9") != std::string::npos && withId.find("203.0.113.9") != std::string::npos);
        EvidenceBundle idb; CHECK(EvidenceBundle::Parse(withId, idb) && idb.account == "acct-9" && idb.address == "203.0.113.9:4000");
        // a size cap: the output is always within it, flagged truncated, and still parses
        CheatDetector big; big.settings = Base(); big.settings.evidenceInterval = 0.0f; big.settings.evidenceCap = 400;
        Walker bw(big, 1, 8.0f); bw.Run(10.0);
        for (int i = 0; i < 300; ++i) big.OnViolation(1, "violation number " + std::to_string(i), "some detail text", 10.0 + i);
        BundleOptions wide; wide.maxEvidence = 300; wide.maxSnapshots = 100; wide.maxBytes = 1 << 20;
        const EvidenceBundle bigb = big.Bundle(1, wide);
        const std::string full = bigb.ToJson();
        CHECK(full.size() > 20000);
        for (std::size_t cap : {std::size_t(300), std::size_t(2000), std::size_t(8000), std::size_t(12288)}) {
            const std::string cut = bigb.ToJson(cap);
            CHECK(cut.size() <= cap);
            EvidenceBundle p; CHECK(EvidenceBundle::Parse(cut, p) && p.truncated);
        }
        CHECK(EvidenceBundle::Parse(full + " \n", back, nullptr));              // trailing whitespace is fine
        // free text is cleaned
        CHECK(EvidenceBundle::Scrub(std::string("a\nb\x01\xff") + "c").find_first_of("\n\x01\xff") == std::string::npos);
        CHECK(EvidenceBundle::Scrub(std::string(500, 'x')).size() <= 120);
        CHECK(EvidenceBundle::Scrub("ok " + std::string(40, 'A')) == "ok [redacted]");
    }

    // ---- the parser against hostile input --------------------------------------------------
    {
        EvidenceBundle out; std::string err;
        const std::string good = "{\"v\":1,\"peer\":3,\"risk\":12.5,\"action\":\"flag\",\"t\":10,\"ev\":[{\"k\":\"teleport\",\"t\":9,\"val\":50,\"d\":\"x\"}]}";
        CHECK(EvidenceBundle::Parse(good, out, &err) && out.evidence.size() == 1);
        const char* bad[] = {
            "", "{", "[]", "null", "{\"v\":1}", "{\"v\":2,\"peer\":1,\"risk\":1,\"t\":1}",
            "{\"v\":1,\"peer\":-1,\"risk\":1,\"t\":1}", "{\"v\":1,\"peer\":99999999999,\"risk\":1,\"t\":1}",
            "{\"v\":1,\"peer\":1,\"risk\":1e999,\"t\":1}", "{\"v\":1,\"peer\":1,\"risk\":NaN,\"t\":1}",
            "{\"v\":1,\"peer\":1,\"risk\":1,\"t\":1} trailing", "{\"v\":1,\"peer\":1,\"risk\":1,\"t\":1,\"ev\":{}}",
            "{\"v\":1,\"peer\":1,\"risk\":1,\"t\":1,\"ev\":[{\"k\":\"made_up\",\"t\":1,\"val\":1}]}",
            "{\"v\":1,\"peer\":1,\"risk\":1,\"t\":1,\"ev\":[{\"k\":5,\"t\":1,\"val\":1}]}",
            "{\"v\":1,\"peer\":1,\"risk\":1,\"t\":1,\"snap\":[[1,2,3]]}",
            "{\"v\":1,\"peer\":1,\"risk\":1,\"t\":1,\"action\":\"\\q\"}", "{\"v\":1,\"peer\":1,\"risk\":1,\"t\":1,\"action\":\"\\u12\"}",
            "{\"v\":1,\"peer\":1,\"risk\":1,\"t\":1,\"counts\":{\"a\":\"b\"}}", "{\"v\":1,\"peer\":01,\"risk\":1,\"t\":1}x",
        };
        for (const char* b : bad) { err.clear(); CHECK(!EvidenceBundle::Parse(b, out, &err)); CHECK(!err.empty()); }
        CHECK(!EvidenceBundle::Parse(std::string(300 * 1024, ' '), out));                      // too big
        std::string deep; for (int i = 0; i < 5000; ++i) deep += "["; CHECK(!EvidenceBundle::Parse(deep, out));   // not a stack overflow
        std::string deepObj; for (int i = 0; i < 5000; ++i) deepObj += "{\"a\":"; CHECK(!EvidenceBundle::Parse(deepObj, out));
        std::string wide = "{\"v\":1,\"peer\":1,\"risk\":1,\"t\":1,\"snap\":["; for (int i = 0; i < 2000; ++i) wide += "[1,1,1,1,1,1,1],"; wide += "[1,1,1,1,1,1,1]]}";
        CHECK(!EvidenceBundle::Parse(wide, out));                                               // too many elements
        std::string longStr = "{\"v\":1,\"peer\":1,\"risk\":1,\"t\":1,\"action\":\"" + std::string(100000, 'a') + "\"}";
        CHECK(!EvidenceBundle::Parse(longStr, out));
        // unknown keys are ignored; strings are cleaned
        CHECK(EvidenceBundle::Parse("{\"v\":1,\"peer\":1,\"risk\":1,\"t\":1,\"zzz\":[1,2,{\"a\":null}],\"game\":\"a\\nb\\u0001c\"}", out, &err));
        CHECK(out.game.find_first_of("\n\x01") == std::string::npos);
        // a failed parse leaves `out` as it was
        EvidenceBundle keep = out; keep.peer = 77;
        CHECK(!EvidenceBundle::Parse("{\"v\":1,", keep) && keep.peer == 77);
        // mutation fuzz: flip, drop and insert bytes in a real bundle; never a crash, and what parses re-serializes stably
        CheatDetector d; d.settings = Base(); d.settings.evidenceInterval = 0.0f;
        Walker w(d, 1, 8.0f); w.Run(2.0);
        for (int i = 0; i < 5; ++i) d.OnViolation(1, "fuzz", "text \"quoted\" \\ slash", w.t + i);
        const std::string seed = d.Bundle(1, BundleOptions{}).ToJson();
        int parsed = 0;
        for (int i = 0; i < 3000; ++i) {
            std::string m = seed;
            const int edits = 1 + (int)(Rand() * 4);
            for (int e = 0; e < edits && !m.empty(); ++e) {
                const std::size_t at = (std::size_t)(Rand() * (float)m.size()) % m.size();
                switch ((int)(Rand() * 4)) {
                    case 0: m[at] = (char)(Rand() * 255.0f); break;
                    case 1: m.erase(at, 1 + (std::size_t)(Rand() * 8.0f)); break;
                    case 2: m.insert(at, 1, "{}[],:\"\\-.e0"[(int)(Rand() * 12.0f) % 12]); break;
                    default: m.resize(at); break;
                }
            }
            EvidenceBundle p;
            if (EvidenceBundle::Parse(m, p)) {
                ++parsed;
                EvidenceBundle q;
                CHECK(EvidenceBundle::Parse(p.ToJson(), q) && q.ToJson() == p.ToJson());
            }
        }
        CHECK(parsed < 3000);
    }

    TEST_MAIN_RESULT();
}
