#include "okay/Net/CheatDetect.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace okay {

namespace {
constexpr std::size_t kKinds = (std::size_t)DetectKind::Count;
const char* const kKindNames[kKinds] = {
    "speed_window", "speed_burst", "teleport", "fly", "noclip", "wall_climb", "input_mismatch", "jump_rate",
    "fire_rate", "accuracy", "headshot", "aim_snap", "through_wall", "damage_exceeds", "hit_out_of_reach",
    "economy_delta", "duplicate_item", "cooldown_bypass",
    "message_burst", "replay", "malformed", "desync_force", "time_manipulation",
    "violation",
};
const char* const kActionNames[] = {"none", "log", "flag", "rate_limit", "shadow_restrict", "kick", "ban_request"};

float Ratio(float a, float b) { return b > 1e-6f ? a / b : 1.0f; }
// For counts that can be off by orders of magnitude (currency, damage): one wild value must not count as a hundred small ones.
float LogSev(float ratio) { return ratio <= 1.0f ? 1.0f : 1.0f + 0.5f * std::log10(ratio); }
bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
float HDist(const Vec3& a, const Vec3& b) { const float dx = a.x - b.x, dz = a.z - b.z; return std::sqrt(dx * dx + dz * dz); }

bool ParseF(const std::string& v, float& out) {
    if (v.empty()) return false;
    char* end = nullptr;
    const float f = std::strtof(v.c_str(), &end);
    if (end != v.c_str() + v.size() || !std::isfinite(f) || f < 0.0f || f > 1e9f) return false;
    out = f; return true;
}
bool ParseI(const std::string& v, int& out) {
    int i = 0; auto r = std::from_chars(v.data(), v.data() + v.size(), i);
    if (r.ec != std::errc() || r.ptr != v.data() + v.size() || i < 0 || i > 1000000) return false;
    out = i; return true;
}
bool ParseB(const std::string& v, bool& out) {
    if (v == "true" || v == "1" || v == "on" || v == "yes") { out = true; return true; }
    if (v == "false" || v == "0" || v == "off" || v == "no") { out = false; return true; }
    return false;
}
std::vector<std::string> Words(const std::string& v) {
    std::vector<std::string> w;
    std::istringstream in(v);
    for (std::string s; in >> s;) w.push_back(s);
    return w;
}
bool BadName(const std::string& n) { return n.empty() || n.size() > 64 || n.find('|') != std::string::npos; }

// One tunable: its .okayac name and where it lives in a DetectSettings.
struct FloatKey { const char* name; float DetectSettings::* p; };
struct IntKey { const char* name; int DetectSettings::* p; };
const FloatKey kFloats[] = {
    {"maxSpeed", &DetectSettings::maxSpeed}, {"speedTolerance", &DetectSettings::speedTolerance},
    {"speedToleranceMid", &DetectSettings::speedToleranceMid}, {"speedToleranceLong", &DetectSettings::speedToleranceLong},
    {"speedWindowShort", &DetectSettings::speedWindowShort}, {"speedWindowMid", &DetectSettings::speedWindowMid},
    {"speedWindowLong", &DetectSettings::speedWindowLong}, {"burstTolerance", &DetectSettings::burstTolerance},
    {"burstBankSeconds", &DetectSettings::burstBankSeconds}, {"teleportDistance", &DetectSettings::teleportDistance},
    {"maxVerticalSpeed", &DetectSettings::maxVerticalSpeed}, {"gravity", &DetectSettings::gravity},
    {"maxJumpHeight", &DetectSettings::maxJumpHeight}, {"stepHeight", &DetectSettings::stepHeight},
    {"slopeLimit", &DetectSettings::slopeLimit}, {"groundEpsilon", &DetectSettings::groundEpsilon},
    {"hoverWindow", &DetectSettings::hoverWindow}, {"noclipMinStep", &DetectSettings::noclipMinStep},
    {"noclipMaxGap", &DetectSettings::noclipMaxGap}, {"ghostSpeedFraction", &DetectSettings::ghostSpeedFraction},
    {"ghostWindow", &DetectSettings::ghostWindow}, {"staleInputSeconds", &DetectSettings::staleInputSeconds},
    {"maxJumpsPerSecond", &DetectSettings::maxJumpsPerSecond},
    {"fireRateTolerance", &DetectSettings::fireRateTolerance}, {"fireBankSeconds", &DetectSettings::fireBankSeconds},
    {"maxAccuracy", &DetectSettings::maxAccuracy}, {"maxHeadshotRatio", &DetectSettings::maxHeadshotRatio},
    {"accuracyMinRange", &DetectSettings::accuracyMinRange}, {"aimSnapDegrees", &DetectSettings::aimSnapDegrees},
    {"aimSnapSpeed", &DetectSettings::aimSnapSpeed}, {"aimSnapShotWindow", &DetectSettings::aimSnapShotWindow},
    {"aimSnapWithin", &DetectSettings::aimSnapWithin},
    {"cooldownTolerance", &DetectSettings::cooldownTolerance}, {"economyTolerance", &DetectSettings::economyTolerance},
    {"maxItemsPerSecond", &DetectSettings::maxItemsPerSecond},
    {"maxMessagesPerSecond", &DetectSettings::maxMessagesPerSecond}, {"messageBurstSeconds", &DetectSettings::messageBurstSeconds},
    {"maxMalformedPerMinute", &DetectSettings::maxMalformedPerMinute}, {"maxCorrectionsPerMinute", &DetectSettings::maxCorrectionsPerMinute},
    {"clockDrift", &DetectSettings::clockDrift}, {"clockWindow", &DetectSettings::clockWindow},
    {"replayWindow", &DetectSettings::replayWindow},
    {"riskHalfLife", &DetectSettings::riskHalfLife}, {"riskCap", &DetectSettings::riskCap},
    {"evidenceInterval", &DetectSettings::evidenceInterval}, {"severityCap", &DetectSettings::severityCap},
    {"confirmWindow", &DetectSettings::confirmWindow}, {"snapshotSeconds", &DetectSettings::snapshotSeconds},
    {"logAt", &DetectSettings::logAt}, {"flagAt", &DetectSettings::flagAt}, {"rateLimitAt", &DetectSettings::rateLimitAt},
    {"shadowAt", &DetectSettings::shadowAt}, {"kickAt", &DetectSettings::kickAt}, {"banAt", &DetectSettings::banAt},
    {"releaseRatio", &DetectSettings::releaseRatio}, {"rateLimitFactor", &DetectSettings::rateLimitFactor},
    {"graceTeleport", &DetectSettings::graceTeleport}, {"graceRespawn", &DetectSettings::graceRespawn},
    {"graceMigration", &DetectSettings::graceMigration}, {"graceJoin", &DetectSettings::graceJoin},
    {"lagSpikeGap", &DetectSettings::lagSpikeGap}, {"graceLag", &DetectSettings::graceLag},
    {"pingTolerance", &DetectSettings::pingTolerance}, {"maxToleratedRtt", &DetectSettings::maxToleratedRtt},
};
const IntKey kInts[] = {
    {"accuracyWindow", &DetectSettings::accuracyWindow}, {"accuracyMinShots", &DetectSettings::accuracyMinShots},
    {"headshotMinHits", &DetectSettings::headshotMinHits}, {"aimSnapConfirm", &DetectSettings::aimSnapConfirm},
    {"confirmations", &DetectSettings::confirmations}, {"evidenceCap", &DetectSettings::evidenceCap},
    {"snapshotCap", &DetectSettings::snapshotCap}, {"evidenceSnapshots", &DetectSettings::evidenceSnapshots},
    {"maxLagGracesPerMinute", &DetectSettings::maxLagGracesPerMinute},
};
} // namespace

const char* DetectKindName(DetectKind k) { return (std::size_t)k < kKinds ? kKindNames[(std::size_t)k] : "unknown"; }
bool DetectKindFromName(const std::string& name, DetectKind& out) {
    for (std::size_t i = 0; i < kKinds; ++i) if (name == kKindNames[i]) { out = (DetectKind)i; return true; }
    return false;
}
const char* DetectActionName(DetectAction a) { return (std::size_t)a < 7 ? kActionNames[(std::size_t)a] : "none"; }

// ---------------------------------------------------------------------------
// settings
// ---------------------------------------------------------------------------
DetectSettings::DetectSettings() {
    weight.fill(6.0f);
    auto w = [&](DetectKind k, float v) { weight[(std::size_t)k] = v; };
    w(DetectKind::SpeedWindow, 8);   w(DetectKind::SpeedBurst, 6);   w(DetectKind::Teleport, 30);
    w(DetectKind::Fly, 10);          w(DetectKind::NoClip, 12);      w(DetectKind::WallClimb, 10);
    w(DetectKind::InputMismatch, 6); w(DetectKind::JumpRate, 5);
    w(DetectKind::FireRate, 6);      w(DetectKind::Accuracy, 14);    w(DetectKind::Headshot, 12);
    w(DetectKind::AimSnap, 14);      w(DetectKind::ThroughWall, 18); w(DetectKind::DamageExceeds, 25);
    w(DetectKind::HitOutOfReach, 6);
    w(DetectKind::EconomyDelta, 25); w(DetectKind::DuplicateItem, 30); w(DetectKind::CooldownBypass, 8);
    w(DetectKind::MessageBurst, 4);  w(DetectKind::Replay, 6);       w(DetectKind::Malformed, 3);
    w(DetectKind::DesyncForce, 8);   w(DetectKind::TimeManipulation, 20);
    w(DetectKind::Violation, 6);
}

const WeaponDef* DetectSettings::FindWeapon(const std::string& n) const {
    for (const auto& w : weapons) if (w.name == n) return &w;
    return nullptr;
}
const AbilityDef* DetectSettings::FindAbility(const std::string& n) const {
    for (const auto& a : abilities) if (a.name == n) return &a;
    return nullptr;
}
const EconomyDef* DetectSettings::FindEconomy(const std::string& k) const {
    for (const auto& e : economy) if (e.key == k) return &e;
    return nullptr;
}

bool DetectSettings::ParseLine(const std::string& key, const std::string& value, std::string* error) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    if (key == "enabled") { if (!ParseB(value, enabled)) return fail("detect.enabled needs true or false"); return true; }
    for (const auto& f : kFloats)
        if (key == f.name) { if (!ParseF(value, this->*f.p)) return fail(std::string("detect.") + key + " needs a number >= 0"); return true; }
    for (const auto& f : kInts)
        if (key == f.name) { if (!ParseI(value, this->*f.p)) return fail(std::string("detect.") + key + " needs a whole number >= 0"); return true; }
    if (key.rfind("weight.", 0) == 0) {
        DetectKind k;
        if (!DetectKindFromName(key.substr(7), k)) return fail("detect." + key + ": unknown detector");
        float v = 0.0f;
        if (!ParseF(value, v)) return fail("detect." + key + " needs a number >= 0");
        weight[(std::size_t)k] = v;
        return true;
    }
    if (key == "weapon") {   // name shotsPerSecond maxDamage [headMultiplier [maxRange]]
        const auto w = Words(value);
        WeaponDef d;
        if (w.size() < 3 || w.size() > 5 || BadName(w[0])) return fail("detect.weapon needs: <name> <shotsPerSecond> <maxDamage> [headMultiplier [maxRange]]");
        d.name = w[0];
        if (!ParseF(w[1], d.shotsPerSecond) || !ParseF(w[2], d.maxDamage)) return fail("detect.weapon: shotsPerSecond and maxDamage must be numbers >= 0");
        if (w.size() > 3 && !ParseF(w[3], d.headMultiplier)) return fail("detect.weapon: headMultiplier must be a number >= 0");
        if (w.size() > 4 && !ParseF(w[4], d.maxRange)) return fail("detect.weapon: maxRange must be a number >= 0");
        for (auto& e : weapons) if (e.name == d.name) { e = d; return true; }
        if (weapons.size() >= 256) return fail("detect.weapon: too many weapons");
        weapons.push_back(d);
        return true;
    }
    if (key == "ability") {
        const auto w = Words(value);
        AbilityDef d;
        if (w.size() != 2 || BadName(w[0]) || !ParseF(w[1], d.cooldownSeconds)) return fail("detect.ability needs: <name> <cooldownSeconds>");
        d.name = w[0];
        for (auto& e : abilities) if (e.name == d.name) { e = d; return true; }
        if (abilities.size() >= 256) return fail("detect.ability: too many abilities");
        abilities.push_back(d);
        return true;
    }
    if (key == "economy") {
        const auto w = Words(value);
        EconomyDef d;
        if (w.size() < 1 || w.size() > 2 || BadName(w[0])) return fail("detect.economy needs: <key> [maxGainPerSecond]");
        d.key = w[0];
        if (w.size() == 2 && !ParseF(w[1], d.maxGainPerSecond)) return fail("detect.economy: maxGainPerSecond must be a number >= 0");
        for (auto& e : economy) if (e.key == d.key) { e = d; return true; }
        if (economy.size() >= 256) return fail("detect.economy: too many keys");
        economy.push_back(d);
        return true;
    }
    return fail("unknown setting 'detect." + key + "'");
}

std::string DetectSettings::ToText() const {
    const DetectSettings d;
    std::ostringstream o;
    if (enabled) o << "detect.enabled true\n";
    for (const auto& f : kFloats) if (this->*f.p != d.*f.p) o << "detect." << f.name << " " << this->*f.p << "\n";
    for (const auto& f : kInts) if (this->*f.p != d.*f.p) o << "detect." << f.name << " " << this->*f.p << "\n";
    for (std::size_t i = 0; i < kKinds; ++i) if (weight[i] != d.weight[i]) o << "detect.weight." << kKindNames[i] << " " << weight[i] << "\n";
    for (const auto& w : weapons) {
        o << "detect.weapon " << w.name << " " << w.shotsPerSecond << " " << w.maxDamage;
        if (w.headMultiplier != 1.0f || w.maxRange != 0.0f) o << " " << w.headMultiplier;
        if (w.maxRange != 0.0f) o << " " << w.maxRange;
        o << "\n";
    }
    for (const auto& a : abilities) o << "detect.ability " << a.name << " " << a.cooldownSeconds << "\n";
    for (const auto& e : economy) o << "detect.economy " << e.key << " " << e.maxGainPerSecond << "\n";
    const std::string s = o.str();
    return s;
}

// ---------------------------------------------------------------------------
// the detector: players, grace, scoring
// ---------------------------------------------------------------------------
CheatDetector::Player& CheatDetector::P(std::uint32_t peer, double t) {
    auto it = m_players.find(peer);
    if (it == m_players.end()) {
        Player p;
        p.joinedAt = t; p.lastDecay = t;
        it = m_players.emplace(peer, std::move(p)).first;
    }
    return it->second;
}

float CheatDetector::PingSlack(const Player& p) const {
    return 0.1f + settings.pingTolerance * std::min(std::max(p.rtt, 0.0f), settings.maxToleratedRtt);
}

void CheatDetector::Advance(double now) {
    if (now > m_now) m_now = now;
    if (!settings.enabled) return;
    for (auto& [id, p] : m_players) DecayTo(p, m_now);
}

void CheatDetector::DecayTo(Player& p, double t) {
    const double dt = t - p.lastDecay;
    if (dt <= 0.0) return;
    p.lastDecay = t;
    if (settings.riskHalfLife > 0.0f && p.risk > 0.0f) p.risk *= (float)std::pow(0.5, dt / settings.riskHalfLife);
    if (p.risk < 0.01f) p.risk = 0.0f;
    // Leave a rung once risk has fallen well below it (Kick / BanRequest are latched).
    const float thr[] = {0.0f, settings.logAt, settings.flagAt, settings.rateLimitAt, settings.shadowAt};
    while (p.action > DetectAction::None && p.action < DetectAction::Kick) {
        const float at = thr[(std::size_t)p.action];
        if (at > 0.0f && p.risk >= at * settings.releaseRatio) break;
        p.action = (DetectAction)((int)p.action - 1);
    }
}

void CheatDetector::Touch(Player& p, std::uint32_t, double t) {
    if (t > m_now) m_now = t;
    if (p.lastEvent >= 0.0 && t - p.lastEvent > settings.lagSpikeGap && t - p.lastEvent <= 8.0 && t >= p.lastEvent) {
        // A silence then traffic again: a lag spike. It buys grace, but only so often.
        auto& g = p.lagGraces;
        g.erase(std::remove_if(g.begin(), g.end(), [&](double x) { return t - x > 60.0; }), g.end());
        if ((int)g.size() < settings.maxLagGracesPerMinute) {
            g.push_back(t);
            p.lagGraceUntil = t + settings.graceLag;
            for (auto& [id, tr] : p.tracks) tr.window.clear();   // bunched arrivals say nothing about speed
        }
    }
    if (t >= p.lastEvent) p.lastEvent = t;
    ++p.events;
    DecayTo(p, t);
}

void CheatDetector::PushSnap(Player& p, const Vec3& pos, double t) {
    if (t - p.lastSnapT < 0.1) return;
    p.lastSnapT = t;
    p.ring.push_back(StateSnap{t, pos, p.yaw, p.pitch, p.rtt});
    while (!p.ring.empty() && (t - p.ring.front().t > settings.snapshotSeconds || (int)p.ring.size() > std::max(1, settings.snapshotCap)))
        p.ring.pop_front();
}

bool CheatDetector::Raise(std::uint32_t peer, Player& p, DetectKind k, double t, float value, float threshold,
                          float severity, const std::string& detail, bool soft, float scale) {
    const std::size_t i = (std::size_t)k;
    if (soft) {
        auto& dq = p.recent[i];
        dq.push_back(t);
        while (!dq.empty() && t - dq.front() > settings.confirmWindow) dq.pop_front();
        if ((int)dq.size() < settings.confirmations) { ++p.suppressed[i]; return false; }
    }
    if (p.scored[i] > 0 && t - p.lastScore[i] < settings.evidenceInterval) { ++p.suppressed[i]; return false; }
    const float sev = std::min(std::max(severity, 1.0f), std::max(settings.severityCap, 1.0f));
    const float w = settings.weight[i] * sev * scale;
    p.lastScore[i] = t;
    ++p.scored[i];
    p.risk = std::min(settings.riskCap, p.risk + w);

    Evidence e;
    e.kind = k; e.t = t; e.tick = p.events; e.value = value; e.threshold = threshold; e.weight = w;
    e.detail = EvidenceBundle::Scrub(detail);
    const int want = std::max(0, settings.evidenceSnapshots);
    if (want > 0 && !p.ring.empty()) {
        const std::size_t n = p.ring.size();
        if ((int)n <= want) e.snaps.assign(p.ring.begin(), p.ring.end());
        else for (int j = 0; j < want; ++j) e.snaps.push_back(p.ring[(std::size_t)((double)j * (double)(n - 1) / (double)(want - 1 > 0 ? want - 1 : 1))]);
    }
    p.evidence.push_back(std::move(e));
    while ((int)p.evidence.size() > std::max(1, settings.evidenceCap)) p.evidence.pop_front();
    if (onEvidence) onEvidence(peer, p.evidence.back());
    Escalate(peer, p, t);
    return true;
}

void CheatDetector::Escalate(std::uint32_t peer, Player& p, double t) {
    const struct { DetectAction a; float at; } rungs[] = {
        {DetectAction::Log, settings.logAt}, {DetectAction::Flag, settings.flagAt}, {DetectAction::RateLimit, settings.rateLimitAt},
        {DetectAction::ShadowRestrict, settings.shadowAt}, {DetectAction::Kick, settings.kickAt}, {DetectAction::BanRequest, settings.banAt},
    };
    DetectAction target = DetectAction::None;
    for (const auto& r : rungs) if (r.at > 0.0f && p.risk >= r.at) target = r.a;
    if (target > p.action) {
        p.action = target;
        if (onResponse) onResponse(DetectResponse{peer, target, p.risk, t});
    }
}

// ---------------------------------------------------------------------------
// join / leave / grace
// ---------------------------------------------------------------------------
void CheatDetector::NoteJoin(std::uint32_t peer, double t) {
    if (!settings.enabled) return;
    Player& p = P(peer, t);
    p.joinedAt = t;
}

void CheatDetector::ForgetPeer(std::uint32_t peer) {
    m_players.erase(peer);
    for (auto it = m_itemHolder.begin(); it != m_itemHolder.end();) it = it->second == peer ? m_itemHolder.erase(it) : std::next(it);
}

void CheatDetector::SeedRisk(std::uint32_t peer, float risk, double t) {
    if (!settings.enabled || !std::isfinite(risk) || risk <= 0.0f) return;
    Player& p = P(peer, t);
    p.risk = std::min(settings.riskCap, std::max(p.risk, risk));
    Escalate(peer, p, t);
}

void CheatDetector::SetPing(std::uint32_t peer, float rtt) {
    if (!settings.enabled || !std::isfinite(rtt)) return;
    P(peer, m_now).rtt = std::min(std::max(rtt, 0.0f), 5.0f);
}

void CheatDetector::NoteTeleport(std::uint32_t peer, double t) {
    if (!settings.enabled) return;
    Player& p = P(peer, t);
    p.graceUntil = std::max(p.graceUntil, t + settings.graceTeleport);
    for (auto& [id, tr] : p.tracks) tr.have = false;
}
void CheatDetector::NoteRespawn(std::uint32_t peer, double t) {
    if (!settings.enabled) return;
    Player& p = P(peer, t);
    p.graceUntil = std::max(p.graceUntil, t + settings.graceRespawn);
    for (auto& [id, tr] : p.tracks) tr.have = false;
    p.shots.clear(); p.haveAim = false; p.snapT = -1e9;
}
void CheatDetector::NoteHostMigration(double t) {
    if (!settings.enabled) return;
    m_migrationUntil = std::max(m_migrationUntil, t + settings.graceMigration);
    for (auto& [id, p] : m_players) {
        for (auto& [sid, tr] : p.tracks) tr.have = false;
        p.haveAim = false; p.haveClock = false; p.haveTick = false;
    }
}
void CheatDetector::NoteSpeedBoost(std::uint32_t peer, float mult, double until) {
    if (!settings.enabled || !std::isfinite(mult)) return;
    Player& p = P(peer, m_now);
    p.boost = std::min(std::max(mult, 1.0f), 10.0f);
    p.boostUntil = until;
}
void CheatDetector::NoteExternalForce(std::uint32_t peer, double until) {
    if (!settings.enabled) return;
    Player& p = P(peer, m_now);
    p.forceUntil = std::max(p.forceUntil, until);
}
void CheatDetector::NoteFlightAllowed(std::uint32_t peer, double until) {
    if (!settings.enabled) return;
    Player& p = P(peer, m_now);
    p.flightUntil = std::max(p.flightUntil, until);
}

// ---------------------------------------------------------------------------
// movement
// ---------------------------------------------------------------------------
void CheatDetector::OnInput(std::uint32_t peer, float mx, float my, double t) {
    if (!settings.enabled) return;
    Player& p = P(peer, t);
    Touch(p, peer, t);
    if (!std::isfinite(mx) || !std::isfinite(my)) { OnMalformed(peer, "input: not finite", t); return; }
    const float mag = std::min(1.0f, std::sqrt(mx * mx + my * my));
    p.inMag = mag; p.inT = t;
    if (mag < 0.05f) { if (p.inIdleSince < 0.0) p.inIdleSince = t; }
    else p.inIdleSince = -1.0;
}

void CheatDetector::OnJump(std::uint32_t peer, double t) {
    if (!settings.enabled) return;
    Player& p = P(peer, t);
    Touch(p, peer, t);
    if (settings.maxJumpsPerSecond <= 0.0f || InGrace(p, t)) return;
    p.jumps.push_back(t);
    while (!p.jumps.empty() && t - p.jumps.front() > 1.0) p.jumps.pop_front();
    const float n = (float)p.jumps.size();
    if (n > settings.maxJumpsPerSecond && t >= p.lagGraceUntil)
        Raise(peer, p, DetectKind::JumpRate, t, n, settings.maxJumpsPerSecond, Ratio(n, settings.maxJumpsPerSecond),
              "jumps in one second", true);
}

void CheatDetector::OnMove(std::uint32_t peer, const std::string& subject, const Vec3& pos, double t) {
    if (!settings.enabled) return;
    Player& p = P(peer, t);
    Touch(p, peer, t);
    if (!Finite(pos) || std::fabs(pos.x) > 1e7f || std::fabs(pos.y) > 1e7f || std::fabs(pos.z) > 1e7f) {
        OnMalformed(peer, "move: bad position", t);
        return;
    }
    if (p.primary.empty()) p.primary = subject;
    auto it = p.tracks.find(subject);
    if (it == p.tracks.end()) {
        if (p.tracks.size() >= 16)   // a client can't make this grow: drop an old one
            for (auto e = p.tracks.begin(); e != p.tracks.end(); ++e) if (e->first != p.primary) { p.tracks.erase(e); break; }
        it = p.tracks.emplace(subject, Track{}).first;
    }
    Track& tr = it->second;
    const bool primary = subject == p.primary;
    const float boost = t < p.boostUntil ? p.boost : 1.0f;
    const float maxSp = settings.maxSpeed * boost;
    const float refill = maxSp * settings.burstTolerance;
    const float slack = PingSlack(p);
    const float cap = refill * (settings.burstBankSeconds + slack);

    auto anchor = [&] {
        tr.have = true; tr.pos = pos; tr.t = t; tr.budget = cap; tr.window.clear();
        tr.window.push_back({t, pos});
        tr.airSince = -1.0; tr.ascentSince = -1.0; tr.ghostSince = -1.0; tr.lastY = pos.y; tr.hasGroundY = false;
        if (primary) PushSnap(p, pos, t);
    };
    if (!tr.have || t < tr.t) { anchor(); return; }
    if (InGrace(p, t) || t < p.forceUntil) { anchor(); return; }

    const Vec3 prev = tr.pos;
    const double dt = t - tr.t;
    // A silence ends an ascent run and restarts the airborne clock: how long a
    // jump lasted is measured in samples, not in the time nothing arrived.
    if (dt > settings.lagSpikeGap) { if (tr.airSince >= 0.0) tr.airSince = t; tr.ascentSince = -1.0; }
    const float hd = HDist(prev, pos);
    tr.pos = pos; tr.t = t;
    if (primary) PushSnap(p, pos, t);
    tr.window.push_back({t, pos});
    const double keep = std::max<double>(settings.speedWindowLong, settings.hoverWindow) + 1.0;
    while (tr.window.size() > 2 && t - tr.window[1].t > keep) tr.window.pop_front();
    if (tr.window.size() > 512) tr.window.pop_front();

    if (maxSp > 0.0f && t >= p.flightUntil) {
        // Travel budget: refills with time, so bunched packets after a lag spike
        // are paid for by the silence before them, while a jump is not.
        tr.budget = std::min(cap, tr.budget + refill * (float)dt);
        const float teleportAt = refill * (float)dt + cap + settings.teleportDistance;
        if (hd > teleportAt) {
            Raise(peer, p, DetectKind::Teleport, t, hd, teleportAt, Ratio(hd, teleportAt), "moved " + std::to_string((int)hd) + " units in " + std::to_string((int)(dt * 1000.0)) + " ms", false);
            tr.budget = 0.0f;
        } else if (hd > tr.budget + 0.05f) {
            Raise(peer, p, DetectKind::SpeedBurst, t, hd, tr.budget, Ratio(hd, tr.budget + 0.25f), "faster than the travel budget", true);
            tr.budget = 0.0f;
        } else tr.budget -= hd;

        // Sustained speed over three windows, tighter the longer the window.
        if (t >= p.lagGraceUntil) {
            const float wins[3] = {settings.speedWindowShort, settings.speedWindowMid, settings.speedWindowLong};
            const float tols[3] = {settings.speedTolerance, settings.speedToleranceMid, settings.speedToleranceLong};
            for (int w = 0; w < 3; ++w) {
                if (wins[w] <= 0.0f) continue;
                // newest sample at or before t - W: the window then really spans W
                int start = -1;
                for (int j = (int)tr.window.size() - 1; j >= 0; --j) if (tr.window[(std::size_t)j].t <= t - wins[w]) { start = j; break; }
                if (start < 0) continue;
                float path = 0.0f;
                for (std::size_t j = (std::size_t)start + 1; j < tr.window.size(); ++j) path += HDist(tr.window[j - 1].pos, tr.window[j].pos);
                const float span = (float)(t - tr.window[(std::size_t)start].t);
                const float allowed = maxSp * tols[w] * span + maxSp * (slack + (wins[w] <= 2.5f ? settings.lagSpikeGap : 0.0f));   // (a lag spike just under the gap bunches a few packets: the short windows allow for them)
                if (path > allowed) {
                    Raise(peer, p, DetectKind::SpeedWindow, t, path / span, allowed / span, Ratio(path, allowed),
                          std::to_string((int)wins[w]) + "s window", false);
                    break;
                }
            }
        }

        // Moving with no input (ghost movement): needs the game to feed OnInput.
        if (settings.ghostWindow > 0.0f) {
            const bool inputFresh = t - p.inT <= settings.staleInputSeconds + slack;
            const float v = hd / (float)std::max(dt, 0.05);
            const bool moving = v > settings.ghostSpeedFraction * maxSp;
            if (moving && inputFresh && p.inIdleSince >= 0.0) {
                if (tr.ghostSince < 0.0) tr.ghostSince = std::max(prev.x == pos.x ? t : t - dt, p.inIdleSince);
                if (t - tr.ghostSince >= settings.ghostWindow + slack && p.inIdleSince <= tr.ghostSince + 1e-9)
                    Raise(peer, p, DetectKind::InputMismatch, t, v, settings.ghostSpeedFraction * maxSp, Ratio(v, settings.ghostSpeedFraction * maxSp),
                          "moving with no input", false);
            } else tr.ghostSince = -1.0;
        }
    }

    if (settings.gravity > 0.0f && settings.maxJumpHeight > 0.0f && t >= p.flightUntil)
        CheckVertical(peer, p, tr, prev, pos, dt, t);
    else { tr.airSince = -1.0; tr.ascentSince = -1.0; }

    if (hooks.staticBlocked && t >= p.flightUntil && dt <= settings.noclipMaxGap) {
        const float step = (pos - prev).Magnitude();
        if (step >= settings.noclipMinStep && hooks.staticBlocked(prev, pos))
            Raise(peer, p, DetectKind::NoClip, t, step, settings.noclipMinStep, 2.0f, "moved through a solid", true);
    }
    tr.lastY = pos.y;
}

void CheatDetector::CheckVertical(std::uint32_t peer, Player& p, Track& tr, const Vec3& prev, const Vec3& pos, double dt, double t) {
    const float g = settings.gravity, H = settings.maxJumpHeight;
    const float vJump = std::sqrt(2.0f * g * H);
    const float apexTime = vJump / g;
    float groundY = 0.0f;
    const bool haveGround = hooks.groundAt && hooks.groundAt(pos, groundY);
    const float hSpeed = HDist(prev, pos) / (float)std::max(dt, 0.02);

    if (!haveGround) {
        // Without a ground query the only impossible thing is a climb that is too
        // steep to walk: a long continuous rise with little run.
        const float dy = pos.y - prev.y;
        if (dy > 0.02f) {
            if (tr.ascentSince < 0.0) { tr.ascentSince = tr.window.size() > 1 ? tr.window[tr.window.size() - 2].t : t; tr.ascentY = prev.y; tr.ascentRun = 0.0f; }
            tr.ascentRun += HDist(prev, pos);
            const float rise = pos.y - tr.ascentY;
            const float limit = H * 1.15f + settings.stepHeight;
            if (rise > limit && rise > tr.ascentRun * settings.slopeLimit * 1.15f && t - tr.ascentSince >= 0.25)
                Raise(peer, p, DetectKind::WallClimb, t, rise, limit, Ratio(rise, limit), "steep sustained climb", false);
        } else if (dy < -0.05f) tr.ascentSince = -1.0;
        return;
    }

    const float h = pos.y - groundY;
    if (h <= settings.groundEpsilon) { tr.airSince = -1.0; tr.ascentSince = -1.0; tr.lastGroundY = pos.y; tr.hasGroundY = true; return; }
    if (tr.airSince < 0.0) {
        tr.airSince = tr.window.size() > 1 ? tr.window[tr.window.size() - 2].t : t;
        tr.airStartY = tr.hasGroundY ? tr.lastGroundY : prev.y;   // where it left the ground (a ledge it walked off counts from the ledge)
        tr.apexY = pos.y; tr.ascentSince = -1.0;
    }
    if (pos.y > tr.apexY) tr.apexY = pos.y;

    // 1. risen further above where it left the ground than any jump reaches
    const float rise = pos.y - tr.airStartY;
    const float limit = H * 1.15f + settings.stepHeight;
    if (rise > limit) {
        const bool climbing = hSpeed < settings.maxSpeed * 0.35f;
        Raise(peer, p, climbing ? DetectKind::WallClimb : DetectKind::Fly, t, rise, limit, Ratio(rise, limit),
              climbing ? "climbing, hardly moving sideways" : "risen above a jump's reach", false);
        return;
    }
    // 2. rising for longer than a jump lasts
    if (pos.y > prev.y + 0.02f) {
        if (tr.ascentSince < 0.0) tr.ascentSince = tr.window.size() > 1 ? tr.window[tr.window.size() - 2].t : t;
        const double up = t - tr.ascentSince;
        if (up > apexTime * 1.3 + 0.15)
            Raise(peer, p, DetectKind::Fly, t, (float)up, apexTime * 1.3f + 0.15f, Ratio((float)up, apexTime * 1.3f + 0.15f), "rising longer than a jump", false);
    } else if (pos.y < prev.y - 0.05f) tr.ascentSince = -1.0;
    // 3. airborne well past the apex of a jump, yet not falling (hover / slow fall)
    const double airTime = t - tr.airSince;
    if (airTime > apexTime * 1.2 + settings.hoverWindow) {
        int old = -1;
        for (int j = (int)tr.window.size() - 1; j >= 0; --j) if (tr.window[(std::size_t)j].t <= t - settings.hoverWindow) { old = j; break; }
        if (old >= 0) {
            const float span = (float)(t - tr.window[(std::size_t)old].t);
            const float vy = (pos.y - tr.window[(std::size_t)old].pos.y) / span;
            const float need = 0.3f * g * settings.hoverWindow;   // the least fall speed gravity leaves
            if (vy > -need)
                Raise(peer, p, DetectKind::Fly, t, 1.0f + (vy + need) / need, 1.0f, 1.0f + (vy + need) / need, "not falling", false);
        }
    }
}

void CheatDetector::OnAim(std::uint32_t peer, float yaw, float pitch, double t) {
    if (!settings.enabled) return;
    Player& p = P(peer, t);
    Touch(p, peer, t);
    if (!std::isfinite(yaw) || !std::isfinite(pitch)) { OnMalformed(peer, "aim: not finite", t); return; }
    if (p.haveAim && t > p.aimT) {
        const double dt = t - p.aimT;
        const float dyaw = std::remainder(yaw - p.yaw, 360.0f), dp = pitch - p.pitch;
        const float deg = std::sqrt(dyaw * dyaw + dp * dp);
        const float speed = deg / (float)std::max(dt, 0.008);
        if (deg >= settings.aimSnapDegrees && speed >= settings.aimSnapSpeed && dt <= 0.25 && !InGrace(p, t) && t >= p.lagGraceUntil) {
            p.snapT = t; p.snapSpeed = speed;
        }
    }
    p.yaw = yaw; p.pitch = pitch; p.aimT = t; p.haveAim = true;
}

// ---------------------------------------------------------------------------
// combat
// ---------------------------------------------------------------------------
void CheatDetector::OnShot(std::uint32_t peer, const ShotEvent& s) {
    if (!settings.enabled) return;
    const double t = s.t;
    Player& p = P(peer, t);
    Touch(p, peer, t);
    const bool grace = InGrace(p, t);
    const float slack = PingSlack(p);
    const WeaponDef* w = settings.FindWeapon(s.weapon);

    // what the server knows to be impossible: never graced
    if (s.blockedByWall)
        Raise(peer, p, DetectKind::ThroughWall, t, 1.0f, 0.0f, 1.0f, "shot through a wall: " + s.weapon, true);
    if (w && w->maxDamage > 0.0f && std::isfinite(s.damage)) {
        const float allowed = w->maxDamage * (s.headshot ? std::max(w->headMultiplier, 1.0f) : 1.0f) * 1.02f;
        if (s.damage > allowed)
            Raise(peer, p, DetectKind::DamageExceeds, t, s.damage, allowed, LogSev(Ratio(s.damage, allowed)), "damage " + std::to_string((int)s.damage) + " with " + s.weapon, false);
    }
    if (w && w->maxRange > 0.0f && s.distance > w->maxRange * 1.05f)
        Raise(peer, p, DetectKind::HitOutOfReach, t, s.distance, w->maxRange, Ratio(s.distance, w->maxRange), "beyond the weapon's range", true);
    if (!grace) {
        if (s.allowedOffRay > 0.0f && s.offRay > s.allowedOffRay)
            Raise(peer, p, DetectKind::HitOutOfReach, t, s.offRay, s.allowedOffRay, Ratio(s.offRay, s.allowedOffRay), "target not where the shot says", true);
        if (s.badOrigin)
            Raise(peer, p, DetectKind::HitOutOfReach, t, 1.0f, 0.0f, 1.0f, "shot from where the shooter isn't", true);
    }

    // fire rate against the weapon's definition
    if (w && w->shotsPerSecond > 0.0f) {
        Bucket& b = p.fire[s.weapon];
        const float refill = w->shotsPerSecond * settings.fireRateTolerance;
        const float cap = 1.0f + w->shotsPerSecond * (settings.fireBankSeconds + slack);
        if (!b.init) { b.init = true; b.tokens = cap; b.t = t; }
        const double dt = std::max(0.0, t - b.t);
        b.tokens = std::min(cap, b.tokens + refill * (float)dt) - 1.0f;
        const double interval = dt;
        b.t = t;
        if (b.tokens < 0.0f) {
            b.tokens = 0.0f;
            if (!grace && t >= p.lagGraceUntil) {
                const float rate = 1.0f / (float)std::max(interval, 1e-3);
                Raise(peer, p, DetectKind::FireRate, t, rate, w->shotsPerSecond, Ratio(rate, w->shotsPerSecond), "weapon " + s.weapon, true);
            }
        }
    }

    // aim snap: a flick followed at once by a hit, again and again
    if (s.hit && p.snapT > -1e8 && t - p.snapT >= 0.0 && t - p.snapT <= settings.aimSnapShotWindow && !grace) {
        p.snapHits.push_back(t);
        while (!p.snapHits.empty() && t - p.snapHits.front() > settings.aimSnapWithin) p.snapHits.pop_front();
        p.snapT = -1e9;
        if (settings.aimSnapConfirm > 0 && (int)p.snapHits.size() >= settings.aimSnapConfirm)
            Raise(peer, p, DetectKind::AimSnap, t, p.snapSpeed, settings.aimSnapSpeed, Ratio(p.snapSpeed, settings.aimSnapSpeed), "snap then hit x" + std::to_string(p.snapHits.size()), false);
    }

    // accuracy / headshot ratio over a sliding window
    if (settings.accuracyWindow > 0 && !(s.distance > 0.0f && s.distance < settings.accuracyMinRange)) {
        p.shots.push_back(Shot{s.hit, s.hit && s.headshot, s.distance});
        while ((int)p.shots.size() > settings.accuracyWindow) p.shots.pop_front();
        if (!grace) CheckAccuracy(peer, p, t);
    }
}

void CheatDetector::CheckAccuracy(std::uint32_t peer, Player& p, double t) {
    const int n = (int)p.shots.size();
    if (n < settings.accuracyMinShots) return;
    int hits = 0, heads = 0;
    for (const Shot& s : p.shots) { hits += s.hit; heads += s.head; }
    const float acc = (float)hits / (float)n;
    if (settings.maxAccuracy > 0.0f && acc > settings.maxAccuracy)
        Raise(peer, p, DetectKind::Accuracy, t, acc, settings.maxAccuracy, 1.0f + (acc - settings.maxAccuracy) * 20.0f,
              std::to_string(hits) + " hits in " + std::to_string(n) + " shots", false);
    if (hits >= settings.headshotMinHits && settings.maxHeadshotRatio > 0.0f) {
        const float hr = (float)heads / (float)hits;
        if (hr > settings.maxHeadshotRatio)
            Raise(peer, p, DetectKind::Headshot, t, hr, settings.maxHeadshotRatio, 1.0f + (hr - settings.maxHeadshotRatio) * 20.0f,
                  std::to_string(heads) + " headshots in " + std::to_string(hits) + " hits", false);
    }
}

// ---------------------------------------------------------------------------
// economy / state
// ---------------------------------------------------------------------------
void CheatDetector::SetBalance(std::uint32_t peer, const std::string& key, float value, double t) {
    if (!settings.enabled || !std::isfinite(value) || BadName(key)) return;
    Player& p = P(peer, t);
    if (p.expected.size() >= 128 && !p.expected.count(key)) return;
    p.expected[key] = value;
    p.expectedT[key] = t;
}
void CheatDetector::OnGrant(std::uint32_t peer, const std::string& key, float amount, double t) {
    if (!settings.enabled || !std::isfinite(amount) || BadName(key)) return;
    Player& p = P(peer, t);
    if (p.expected.size() >= 128 && !p.expected.count(key)) return;
    p.expected[key] += amount;
    p.expectedT[key] = t;
}
void CheatDetector::OnClaimedValue(std::uint32_t peer, const std::string& key, float value, double t) {
    if (!settings.enabled || BadName(key)) return;
    Player& p = P(peer, t);
    Touch(p, peer, t);
    if (!std::isfinite(value)) { OnMalformed(peer, "claimed value: not finite", t); return; }
    auto e = p.expected.find(key);
    if (e == p.expected.end()) {   // first sight: nothing to compare against yet
        if (p.expected.size() < 128) { p.expected[key] = value; p.expectedT[key] = t; }
        return;
    }
    const double since = std::max(0.0, t - p.expectedT[key]);
    const float excess = value - e->second;
    float allowed = settings.economyTolerance;
    if (const EconomyDef* d = settings.FindEconomy(key); d && d->maxGainPerSecond > 0.0f) allowed += d->maxGainPerSecond * (float)since;
    if (excess > allowed && !InGrace(p, t))
        Raise(peer, p, DetectKind::EconomyDelta, t, excess, std::max(allowed, 1.0f), LogSev(Ratio(excess, std::max(allowed, 1.0f))),
              key + " rose by " + std::to_string((long long)excess) + " with no cause", false);
    e->second = value;          // follow the claim: one jump is one record
    p.expectedT[key] = t;
}

void CheatDetector::OnItemAcquire(std::uint32_t peer, const std::string& uid, const std::string& name, double t) {
    if (!settings.enabled) return;
    Player& p = P(peer, t);
    Touch(p, peer, t);
    if (!uid.empty() && uid.size() <= 128) {
        auto h = m_itemHolder.find(uid);
        if (h != m_itemHolder.end())
            Raise(peer, p, DetectKind::DuplicateItem, t, 2.0f, 1.0f, 2.0f,
                  h->second == peer ? "item held twice: " + name : "item held by two players: " + name, false);
        else if (m_itemHolder.size() < 200000) m_itemHolder[uid] = peer;
    }
    if (settings.maxItemsPerSecond > 0.0f && !name.empty() && name.size() <= 64) {
        if (p.itemTimes.size() >= 64 && !p.itemTimes.count(name)) return;
        auto& dq = p.itemTimes[name];
        dq.push_back(t);
        while (!dq.empty() && t - dq.front() > 1.0) dq.pop_front();
        if ((float)dq.size() > settings.maxItemsPerSecond && t >= p.lagGraceUntil)
            Raise(peer, p, DetectKind::DuplicateItem, t, (float)dq.size(), settings.maxItemsPerSecond, Ratio((float)dq.size(), settings.maxItemsPerSecond),
                  "item gained in a burst: " + name, true);
    }
}
void CheatDetector::OnItemRelease(std::uint32_t peer, const std::string& uid) {
    if (!settings.enabled) return;
    auto h = m_itemHolder.find(uid);
    if (h != m_itemHolder.end() && h->second == peer) m_itemHolder.erase(h);
}

void CheatDetector::OnAbility(std::uint32_t peer, const std::string& name, double t) {
    if (!settings.enabled || BadName(name)) return;
    Player& p = P(peer, t);
    Touch(p, peer, t);
    const AbilityDef* d = settings.FindAbility(name);
    if (!d || d->cooldownSeconds <= 0.0f) return;
    auto last = p.abilityLast.find(name);
    if (last != p.abilityLast.end() && !InGrace(p, t) && t >= p.lagGraceUntil) {
        const float gap = (float)(t - last->second);
        const float need = d->cooldownSeconds * (1.0f - settings.cooldownTolerance) - PingSlack(p);
        if (gap < need)
            Raise(peer, p, DetectKind::CooldownBypass, t, gap, need, Ratio(need, std::max(gap, 0.05f)), name + " used again after " + std::to_string((int)(gap * 1000.0f)) + " ms", true);
    }
    if (p.abilityLast.size() < 128 || last != p.abilityLast.end()) p.abilityLast[name] = t;
}

// ---------------------------------------------------------------------------
// network
// ---------------------------------------------------------------------------
bool CheatDetector::OnMessage(std::uint32_t peer, double t, std::size_t) {
    if (!settings.enabled) return true;
    Player& p = P(peer, t);
    Touch(p, peer, t);
    if (settings.maxMessagesPerSecond <= 0.0f) return true;
    const bool limited = p.action >= DetectAction::RateLimit && p.action < DetectAction::Kick;
    const float rate = settings.maxMessagesPerSecond * (limited ? settings.rateLimitFactor : 1.0f);
    const float cap = rate * (settings.messageBurstSeconds + PingSlack(p));
    Bucket& b = p.msgs;
    if (!b.init) { b.init = true; b.tokens = cap; b.t = t; p.msgWinT = t; p.msgWinN = 0; }
    b.tokens = std::min(cap, b.tokens + rate * (float)std::max(0.0, t - b.t)) - 1.0f;
    b.t = t;
    if (t - p.msgWinT >= 1.0) { p.msgWinT = t; p.msgWinN = 0; }
    ++p.msgWinN;
    if (b.tokens < 0.0f) {
        b.tokens = 0.0f;
        if (t >= p.lagGraceUntil && !InGrace(p, t))
            Raise(peer, p, DetectKind::MessageBurst, t, (float)p.msgWinN, rate, Ratio((float)p.msgWinN, rate), "messages in a second", true);
        return !limited;
    }
    return true;
}

bool CheatDetector::OnSequence(std::uint32_t peer, std::uint64_t seq, double t) {
    if (!settings.enabled) return true;
    Player& p = P(peer, t);
    Touch(p, peer, t);
    const std::uint64_t win = (std::uint64_t)std::max(8.0f, std::min(settings.replayWindow, 4096.0f));
    bool replay = false;
    if (p.haveSeq) {
        if (seq + win <= p.seqMax) replay = true;
        else if (std::find(p.seqs.begin(), p.seqs.end(), seq) != p.seqs.end()) replay = true;
    }
    if (replay) {
        Raise(peer, p, DetectKind::Replay, t, 1.0f, 0.0f, 1.0f, "sequence " + std::to_string(seq) + " again", false);
        return false;
    }
    p.haveSeq = true;
    p.seqMax = std::max(p.seqMax, seq);
    p.seqs.push_back(seq);
    if (p.seqs.size() > win * 2) p.seqs.erase(std::remove_if(p.seqs.begin(), p.seqs.end(), [&](std::uint64_t s) { return s + win <= p.seqMax; }), p.seqs.end());
    return true;
}

void CheatDetector::OnMalformed(std::uint32_t peer, const std::string& what, double t) {
    if (!settings.enabled) return;
    Player& p = P(peer, t);
    Touch(p, peer, t);
    p.malformed.push_back(t);
    while (!p.malformed.empty() && t - p.malformed.front() > 60.0) p.malformed.pop_front();
    if (p.malformed.size() > 4096) p.malformed.pop_front();
    if (settings.maxMalformedPerMinute > 0.0f && (float)p.malformed.size() > settings.maxMalformedPerMinute)
        Raise(peer, p, DetectKind::Malformed, t, (float)p.malformed.size(), settings.maxMalformedPerMinute,
              Ratio((float)p.malformed.size(), settings.maxMalformedPerMinute), what, false);
}

void CheatDetector::OnReplay(std::uint32_t peer, double t) {
    if (!settings.enabled) return;
    Player& p = P(peer, t);
    Touch(p, peer, t);
    Raise(peer, p, DetectKind::Replay, t, 1.0f, 0.0f, 1.0f, "packet replayed", false);
}

void CheatDetector::OnCorrection(std::uint32_t peer, double t) {
    if (!settings.enabled) return;
    Player& p = P(peer, t);
    Touch(p, peer, t);
    p.corrections.push_back(t);
    while (!p.corrections.empty() && t - p.corrections.front() > 60.0) p.corrections.pop_front();
    if (p.corrections.size() > 4096) p.corrections.pop_front();
    if (settings.maxCorrectionsPerMinute > 0.0f && (float)p.corrections.size() > settings.maxCorrectionsPerMinute && !InGrace(p, t))
        Raise(peer, p, DetectKind::DesyncForce, t, (float)p.corrections.size(), settings.maxCorrectionsPerMinute,
              Ratio((float)p.corrections.size(), settings.maxCorrectionsPerMinute), "corrected again and again", false);
}

void CheatDetector::OnClientTick(std::uint32_t peer, std::uint64_t tick, double t) {
    if (!settings.enabled) return;
    Player& p = P(peer, t);
    Touch(p, peer, t);
    if (p.haveTick && tick + 3 < p.lastTick && !InGrace(p, t))
        Raise(peer, p, DetectKind::DesyncForce, t, (float)(p.lastTick - tick), 3.0f, 2.0f, "client tick went backwards", true);
    p.lastTick = tick; p.haveTick = true;
}

void CheatDetector::OnClock(std::uint32_t peer, double clientSeconds, double t) {
    if (!settings.enabled || !std::isfinite(clientSeconds)) return;
    Player& p = P(peer, t);
    Touch(p, peer, t);
    if (!p.haveClock || InGrace(p, t) || t < p.lagGraceUntil) {
        p.haveClock = true; p.clockC0 = clientSeconds; p.clockS0 = t; p.clockBad = 0;
        return;
    }
    const double span = t - p.clockS0;
    if (span < settings.clockWindow) return;
    const double rate = (clientSeconds - p.clockC0) / span;
    const double tol = settings.clockDrift + PingSlack(p) / span;
    if (std::fabs(rate - 1.0) > tol) ++p.clockBad; else p.clockBad = 0;
    p.clockC0 = clientSeconds; p.clockS0 = t;
    if (p.clockBad >= 3)
        Raise(peer, p, DetectKind::TimeManipulation, t, (float)std::fabs(rate - 1.0), settings.clockDrift,
              Ratio((float)std::fabs(rate - 1.0), settings.clockDrift), "game clock runs at " + std::to_string((int)(rate * 100.0)) + "% of real time", false);
}

void CheatDetector::OnViolation(std::uint32_t peer, const std::string& what, const std::string& detail, double t, float scale) {
    if (!settings.enabled) return;
    Player& p = P(peer, t);
    Touch(p, peer, t);
    Raise(peer, p, DetectKind::Violation, t, 1.0f, 1.0f, 1.0f, what + (detail.empty() ? "" : ": " + detail), false, scale);
}

// ---------------------------------------------------------------------------
// queries
// ---------------------------------------------------------------------------
float CheatDetector::Risk(std::uint32_t peer) const {
    auto it = m_players.find(peer);
    if (it == m_players.end()) return 0.0f;
    const Player& p = it->second;
    const double dt = m_now - p.lastDecay;
    return dt > 0.0 && settings.riskHalfLife > 0.0f ? p.risk * (float)std::pow(0.5, dt / settings.riskHalfLife) : p.risk;
}
DetectAction CheatDetector::Action(std::uint32_t peer) const {
    auto it = m_players.find(peer);
    return it == m_players.end() ? DetectAction::None : it->second.action;
}
const std::deque<Evidence>* CheatDetector::EvidenceOf(std::uint32_t peer) const {
    auto it = m_players.find(peer);
    return it == m_players.end() ? nullptr : &it->second.evidence;
}
std::vector<StateSnap> CheatDetector::SnapshotsOf(std::uint32_t peer) const {
    auto it = m_players.find(peer);
    return it == m_players.end() ? std::vector<StateSnap>() : std::vector<StateSnap>(it->second.ring.begin(), it->second.ring.end());
}
int CheatDetector::Count(std::uint32_t peer, DetectKind k) const {
    auto it = m_players.find(peer);
    return it == m_players.end() ? 0 : it->second.scored[(std::size_t)k];
}
int CheatDetector::Suppressed(std::uint32_t peer, DetectKind k) const {
    auto it = m_players.find(peer);
    return it == m_players.end() ? 0 : it->second.suppressed[(std::size_t)k];
}
void CheatDetector::Reset() {
    m_players.clear(); m_itemHolder.clear(); m_now = 0.0; m_migrationUntil = 0.0;
}

EvidenceBundle CheatDetector::Bundle(std::uint32_t peer, const BundleOptions& o, const std::string& account, const std::string& address) const {
    EvidenceBundle b;
    b.peer = peer;
    b.game = EvidenceBundle::Scrub(o.game, 64);
    b.server = EvidenceBundle::Scrub(o.server, 64);
    if (o.includeAccount) b.account = EvidenceBundle::Scrub(account, 64);
    if (o.includeAddress) b.address = EvidenceBundle::Scrub(address, 64);
    b.t = m_now;
    auto it = m_players.find(peer);
    if (it == m_players.end()) { b.action = "none"; return b; }
    const Player& p = it->second;
    b.risk = Risk(peer);
    b.action = DetectActionName(p.action);
    for (std::size_t i = 0; i < kKinds; ++i) if (p.scored[i] > 0) b.counts.emplace_back(kKindNames[i], p.scored[i]);
    auto th = [&](const char* n, float v) { if (v > 0.0f) b.thresholds.emplace_back(n, v); };
    th("maxSpeed", settings.maxSpeed); th("speedTolerance", settings.speedTolerance); th("teleportDistance", settings.teleportDistance);
    th("gravity", settings.gravity); th("maxJumpHeight", settings.maxJumpHeight); th("maxAccuracy", settings.maxAccuracy);
    th("maxHeadshotRatio", settings.maxHeadshotRatio); th("maxMessagesPerSecond", settings.maxMessagesPerSecond);
    th("clockDrift", settings.clockDrift); th("kickAt", settings.kickAt); th("flagAt", settings.flagAt);
    const int ne = std::max(0, o.maxEvidence);
    const std::size_t skip = p.evidence.size() > (std::size_t)ne ? p.evidence.size() - (std::size_t)ne : 0;
    b.evidence.assign(p.evidence.begin() + (std::ptrdiff_t)skip, p.evidence.end());
    const int ns = std::max(0, o.maxSnapshots);
    if (ns > 0 && !p.ring.empty()) {
        const std::size_t n = p.ring.size();
        if ((int)n <= ns) b.snaps.assign(p.ring.begin(), p.ring.end());
        else for (int j = 0; j < ns; ++j) b.snaps.push_back(p.ring[(std::size_t)((double)j * (double)(n - 1) / (double)(ns - 1 > 0 ? ns - 1 : 1))]);
    }
    return b;
}

// ---------------------------------------------------------------------------
// evidence bundle: JSON out and in
// ---------------------------------------------------------------------------
std::string EvidenceBundle::Scrub(const std::string& s, std::size_t maxLen) {
    std::string out;
    out.reserve(std::min(s.size(), maxLen));
    std::size_t i = 0;
    auto tokenChar = [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c == '+' || c == '/' || c == '=' || c == '.'; };
    while (i < s.size() && out.size() < maxLen) {
        const unsigned char c = (unsigned char)s[i];
        if (tokenChar(c)) {
            std::size_t j = i;
            while (j < s.size() && tokenChar((unsigned char)s[j])) ++j;
            if (j - i >= 32) out += "[redacted]";   // a key / token / ticket must never ride along
            else out.append(s, i, j - i);
            i = j;
            continue;
        }
        out += (c < 0x20 || c >= 0x7f) ? ' ' : (char)c;
        ++i;
    }
    if (out.size() > maxLen) out.resize(maxLen);
    return out;
}

namespace {
void PutStr(std::string& o, const std::string& s) {
    o += '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20 || c >= 0x7f) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c < 0x7f ? c : 0x3f); o += b; }
        else o += (char)c;
    }
    o += '"';
}
void PutNum(std::string& o, double v, int digits = 6) {
    char b[48];
    if (!std::isfinite(v)) v = 0.0;
    std::snprintf(b, sizeof b, "%.*g", digits, v);
    o += b;
}
void PutSnap(std::string& o, const StateSnap& s) {
    o += '['; PutNum(o, s.t, 9); o += ','; PutNum(o, s.pos.x, 7); o += ','; PutNum(o, s.pos.y, 7); o += ','; PutNum(o, s.pos.z, 7);
    o += ','; PutNum(o, s.yaw, 5); o += ','; PutNum(o, s.pitch, 5); o += ','; PutNum(o, s.rtt, 4); o += ']';
}
std::string Emit(const EvidenceBundle& b, bool truncated) {
    std::string o = "{\"v\":";
    PutNum(o, b.version, 4);
    o += ",\"peer\":"; PutNum(o, b.peer, 10);
    o += ",\"risk\":"; PutNum(o, b.risk, 5);
    o += ",\"action\":"; PutStr(o, b.action);
    o += ",\"t\":"; PutNum(o, b.t, 12);
    o += ",\"trunc\":"; o += truncated ? "true" : "false";
    if (!b.game.empty()) { o += ",\"game\":"; PutStr(o, b.game); }
    if (!b.server.empty()) { o += ",\"server\":"; PutStr(o, b.server); }
    if (!b.account.empty()) { o += ",\"acct\":"; PutStr(o, b.account); }
    if (!b.address.empty()) { o += ",\"addr\":"; PutStr(o, b.address); }
    o += ",\"counts\":{";
    for (std::size_t i = 0; i < b.counts.size(); ++i) { if (i) o += ','; PutStr(o, b.counts[i].first); o += ':'; PutNum(o, b.counts[i].second, 8); }
    o += "},\"thr\":{";
    for (std::size_t i = 0; i < b.thresholds.size(); ++i) { if (i) o += ','; PutStr(o, b.thresholds[i].first); o += ':'; PutNum(o, b.thresholds[i].second, 6); }
    o += "},\"ev\":[";
    for (std::size_t i = 0; i < b.evidence.size(); ++i) {
        const Evidence& e = b.evidence[i];
        if (i) o += ',';
        o += "{\"k\":"; PutStr(o, DetectKindName(e.kind));
        o += ",\"t\":"; PutNum(o, e.t, 12);
        o += ",\"tick\":"; PutNum(o, (double)e.tick, 12);
        o += ",\"val\":"; PutNum(o, e.value, 5);
        o += ",\"thr\":"; PutNum(o, e.threshold, 5);
        o += ",\"w\":"; PutNum(o, e.weight, 5);
        o += ",\"d\":"; PutStr(o, e.detail);
        o += ",\"s\":[";
        for (std::size_t j = 0; j < e.snaps.size(); ++j) { if (j) o += ','; PutSnap(o, e.snaps[j]); }
        o += "]}";
    }
    o += "],\"snap\":[";
    for (std::size_t i = 0; i < b.snaps.size(); ++i) { if (i) o += ','; PutSnap(o, b.snaps[i]); }
    o += "]}";
    return o;
}

// ---- a bounded JSON reader ----
struct JVal {
    enum T { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false; double n = 0.0; std::string s;
    std::vector<JVal> a;
    std::vector<std::pair<std::string, JVal>> o;
};
struct JReader {
    const std::string& s; std::size_t i = 0; int nodes = 0; std::string err;
    explicit JReader(const std::string& t) : s(t) {}
    static constexpr int kMaxDepth = 8, kMaxNodes = 20000, kMaxArr = 512, kMaxObj = 64, kMaxStr = 512;
    bool Fail(const char* m) { if (err.empty()) err = m; return false; }
    void Ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i; }
    bool Str(std::string& out) {
        if (i >= s.size() || s[i] != '"') return Fail("expected a string");
        ++i;
        while (i < s.size() && s[i] != '"') {
            unsigned char c = (unsigned char)s[i++];
            if (out.size() >= (std::size_t)kMaxStr) return Fail("string too long");
            if (c < 0x20) return Fail("control character in string");
            if (c != '\\') { out += (c >= 0x7f ? '?' : (char)c); continue; }
            if (i >= s.size()) return Fail("bad escape");
            const char e = s[i++];
            switch (e) {
                case '"': out += '"'; break; case '\\': out += '\\'; break; case '/': out += '/'; break;
                case 'n': case 't': case 'r': case 'b': case 'f': out += ' '; break;
                case 'u': {
                    if (i + 4 > s.size()) return Fail("bad escape");
                    unsigned v = 0;
                    for (int k = 0; k < 4; ++k) {
                        const char h = s[i++];
                        v <<= 4;
                        if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
                        else return Fail("bad escape");
                    }
                    out += (v >= 0x20 && v < 0x7f) ? (char)v : (v < 0x20 ? ' ' : '?');
                    break;
                }
                default: return Fail("bad escape");
            }
        }
        if (i >= s.size()) return Fail("unterminated string");
        ++i;
        return true;
    }
    bool Val(JVal& v, int depth) {
        if (depth > kMaxDepth) return Fail("nested too deep");
        if (++nodes > kMaxNodes) return Fail("too many values");
        Ws();
        if (i >= s.size()) return Fail("unexpected end");
        const char c = s[i];
        if (c == '{') {
            ++i; v.type = JVal::Obj; Ws();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            for (;;) {
                if ((int)v.o.size() >= kMaxObj) return Fail("too many keys");
                Ws();
                std::string k;
                if (!Str(k)) return false;
                Ws();
                if (i >= s.size() || s[i++] != ':') return Fail("expected ':'");
                JVal c2;
                if (!Val(c2, depth + 1)) return false;
                v.o.emplace_back(std::move(k), std::move(c2));
                Ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return Fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++i; v.type = JVal::Arr; Ws();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            for (;;) {
                if ((int)v.a.size() >= kMaxArr) return Fail("too many elements");
                JVal e;
                if (!Val(e, depth + 1)) return false;
                v.a.push_back(std::move(e));
                Ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return Fail("expected ',' or ']'");
            }
        }
        if (c == '"') { v.type = JVal::Str; return Str(v.s); }
        if (s.compare(i, 4, "true") == 0) { i += 4; v.type = JVal::Bool; v.b = true; return true; }
        if (s.compare(i, 5, "false") == 0) { i += 5; v.type = JVal::Bool; v.b = false; return true; }
        if (s.compare(i, 4, "null") == 0) { i += 4; v.type = JVal::Null; return true; }
        // a number: -?digits[.digits][e[+-]digits], nothing else
        std::size_t j = i;
        if (j < s.size() && s[j] == '-') ++j;
        const std::size_t d0 = j;
        while (j < s.size() && std::isdigit((unsigned char)s[j])) ++j;
        if (j == d0) return Fail("unexpected character");
        if (j < s.size() && s[j] == '.') { ++j; const std::size_t f0 = j; while (j < s.size() && std::isdigit((unsigned char)s[j])) ++j; if (j == f0) return Fail("bad number"); }
        if (j < s.size() && (s[j] == 'e' || s[j] == 'E')) {
            ++j; if (j < s.size() && (s[j] == '+' || s[j] == '-')) ++j;
            const std::size_t e0 = j; while (j < s.size() && std::isdigit((unsigned char)s[j])) ++j;
            if (j == e0) return Fail("bad number");
        }
        if (j - i > 40) return Fail("number too long");
        const double d = std::strtod(s.substr(i, j - i).c_str(), nullptr);
        if (!std::isfinite(d) || std::fabs(d) > 1e15) return Fail("number out of range");
        v.type = JVal::Num; v.n = d; i = j;
        return true;
    }
};
const JVal* Get(const JVal& o, const char* k) {
    if (o.type != JVal::Obj) return nullptr;
    for (const auto& [name, v] : o.o) if (name == k) return &v;
    return nullptr;
}
bool Snap(const JVal& v, StateSnap& s) {
    if (v.type != JVal::Arr || v.a.size() != 7) return false;
    for (const auto& e : v.a) if (e.type != JVal::Num) return false;
    s.t = v.a[0].n; s.pos = Vec3((float)v.a[1].n, (float)v.a[2].n, (float)v.a[3].n);
    s.yaw = (float)v.a[4].n; s.pitch = (float)v.a[5].n; s.rtt = (float)v.a[6].n;
    return true;
}
} // namespace

std::string EvidenceBundle::ToJson(std::size_t maxBytes) const {
    std::string full = Emit(*this, truncated);
    if (maxBytes == 0 || full.size() <= maxBytes) return full;
    // Too big: halve the top-level snapshots, then drop the per-record ones,
    // then the oldest records, until it fits. `trunc` says it was cut.
    EvidenceBundle c = *this;
    for (int guard = 0; guard < 4096; ++guard) {
        std::string out = Emit(c, true);
        if (out.size() <= maxBytes) return out;
        if (!c.snaps.empty()) { c.snaps.resize(c.snaps.size() / 2); continue; }
        bool had = false;
        for (auto& e : c.evidence) if (!e.snaps.empty()) { e.snaps.clear(); had = true; }
        if (had) continue;
        if (!c.evidence.empty()) { c.evidence.erase(c.evidence.begin()); continue; }
        if (!c.thresholds.empty()) { c.thresholds.clear(); continue; }
        if (!c.counts.empty()) { c.counts.clear(); continue; }
        c.game.clear(); c.server.clear(); c.account.clear(); c.address.clear();
        return Emit(c, true);
    }
    return Emit(c, true);
}

bool EvidenceBundle::Parse(const std::string& text, EvidenceBundle& out, std::string* error) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    if (text.size() > kMaxParseBytes) return fail("bundle too large");
    JReader r(text);
    JVal root;
    if (!r.Val(root, 0)) return fail(r.err);
    r.Ws();
    if (r.i != text.size()) return fail("trailing data");
    if (root.type != JVal::Obj) return fail("a bundle is an object");
    EvidenceBundle b;
    auto num = [&](const JVal* v, double& d) { if (!v || v->type != JVal::Num) return false; d = v->n; return true; };
    auto str = [&](const JVal* v, std::string& s, std::size_t cap) { if (!v) return true; if (v->type != JVal::Str) return false; s = Scrub(v->s, cap); return true; };
    double d = 0;
    if (!num(Get(root, "v"), d) || d != (double)kVersion) return fail("unsupported version");
    if (!num(Get(root, "peer"), d) || d < 0 || d > 4294967295.0) return fail("bad peer");
    b.peer = (std::uint32_t)d;
    if (!num(Get(root, "risk"), d) || d < 0 || d > 1e6) return fail("bad risk");
    b.risk = (float)d;
    if (!num(Get(root, "t"), b.t)) return fail("bad time");
    if (const JVal* a = Get(root, "action")) { if (a->type != JVal::Str) return fail("bad action"); b.action = Scrub(a->s, 32); }
    if (const JVal* tr = Get(root, "trunc")) { if (tr->type != JVal::Bool) return fail("bad trunc"); b.truncated = tr->b; }
    if (!str(Get(root, "game"), b.game, 64) || !str(Get(root, "server"), b.server, 64) ||
        !str(Get(root, "acct"), b.account, 64) || !str(Get(root, "addr"), b.address, 64)) return fail("bad text field");
    if (const JVal* c = Get(root, "counts")) {
        if (c->type != JVal::Obj) return fail("bad counts");
        for (const auto& [k, v] : c->o) { if (v.type != JVal::Num) return fail("bad counts"); b.counts.emplace_back(Scrub(k, 40), (int)std::min(std::max(v.n, 0.0), 1e9)); }
    }
    if (const JVal* c = Get(root, "thr")) {
        if (c->type != JVal::Obj) return fail("bad thresholds");
        for (const auto& [k, v] : c->o) { if (v.type != JVal::Num) return fail("bad thresholds"); b.thresholds.emplace_back(Scrub(k, 40), (float)v.n); }
    }
    if (const JVal* evs = Get(root, "ev")) {
        if (evs->type != JVal::Arr) return fail("bad evidence");
        for (const JVal& ev : evs->a) {
            if (ev.type != JVal::Obj) return fail("bad evidence record");
            const JVal* k = Get(ev, "k");
            Evidence e;
            if (!k || k->type != JVal::Str || !DetectKindFromName(k->s, e.kind)) return fail("unknown detector kind");
            double v = 0;
            if (!num(Get(ev, "t"), e.t) || !num(Get(ev, "val"), v)) return fail("bad evidence record");
            e.value = (float)v;
            if (num(Get(ev, "thr"), v)) e.threshold = (float)v;
            if (num(Get(ev, "w"), v)) e.weight = (float)v;
            if (num(Get(ev, "tick"), v) && v >= 0) e.tick = (std::uint64_t)v;
            if (!str(Get(ev, "d"), e.detail, 120)) return fail("bad evidence detail");
            if (const JVal* ss = Get(ev, "s")) {
                if (ss->type != JVal::Arr) return fail("bad snapshots");
                for (const JVal& sv : ss->a) { StateSnap s; if (!Snap(sv, s)) return fail("bad snapshot"); e.snaps.push_back(s); }
            }
            b.evidence.push_back(std::move(e));
        }
    }
    if (const JVal* ss = Get(root, "snap")) {
        if (ss->type != JVal::Arr) return fail("bad snapshots");
        for (const JVal& sv : ss->a) { StateSnap s; if (!Snap(sv, s)) return fail("bad snapshot"); b.snaps.push_back(s); }
    }
    out = std::move(b);
    return true;
}

} // namespace okay
