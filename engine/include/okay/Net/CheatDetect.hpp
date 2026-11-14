#pragma once
// ---------------------------------------------------------------------------
// CheatDetect — the server-side detection engine (opt-in; see docs/anticheat.md).
//
// Client-side checks can always be patched out. What a server can do is look at
// what clients CLAIM (positions, aim, shots, counters, packets) and compare it
// with what the server knows is possible. This engine is that comparison, in
// one deterministic, engine-independent class:
//
//   * detectors for movement, combat, economy / state and the network, each
//     with thresholds in DetectSettings (.okayac "detect.*" keys);
//   * a suspicion model instead of binary checks: every detector adds
//     EVIDENCE (kind, value, threshold, tick, a short state snapshot), the
//     player's RISK score rises with it and decays over time, and graduated
//     responses (log, flag, rate-limit, shadow-restrict, kick, ban request)
//     hang off risk thresholds;
//   * false-positive protection: grace after teleports / respawns / lag
//     spikes / host migration, a ping-scaled tolerance, stale-input
//     tolerance, a warm-up for new players, and soft detectors that need
//     several confirmations before they count;
//   * an evidence bundle (compact JSON, size capped, no secrets, no addresses
//     unless the game opts in) with a serializer and a hostile-input parser.
//
// Time is always passed in (seconds, any origin), never read from a clock, so
// tests drive it with fake time and get the same answer every run. Nothing
// here is on unless DetectSettings::enabled is true, and every game-specific
// detector (speed, flight, weapons, abilities, economy) stays off until its
// threshold is set. NetworkManager feeds it from its receive path when the
// anti-cheat and detect.enabled are both on; the engine can equally be fed by
// game code (OnGrant, OnItemAcquire, NoteRespawn ...).
// ---------------------------------------------------------------------------
#include "okay/Math/Vec3.hpp"
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

enum class DetectKind : std::uint8_t {
    // movement
    SpeedWindow, SpeedBurst, Teleport, Fly, NoClip, WallClimb, InputMismatch, JumpRate,
    // combat
    FireRate, Accuracy, Headshot, AimSnap, ThroughWall, DamageExceeds, HitOutOfReach,
    // economy / state
    EconomyDelta, DuplicateItem, CooldownBypass,
    // network
    MessageBurst, Replay, Malformed, DesyncForce, TimeManipulation,
    // an immediate AntiCheat refusal (forged ownership, forged ticket, bad spawn ...)
    Violation,
    Count
};
const char* DetectKindName(DetectKind k);
bool DetectKindFromName(const std::string& name, DetectKind& out);

/// Graduated responses, in order. Each is reached when risk crosses its
/// threshold and left (except Kick / BanRequest) when risk falls back.
enum class DetectAction : std::uint8_t { None, Log, Flag, RateLimit, ShadowRestrict, Kick, BanRequest };
const char* DetectActionName(DetectAction a);

/// A weapon the server knows: how fast it may fire and how hard it may hit.
struct WeaponDef {
    std::string name;
    float shotsPerSecond = 0.0f;   ///< 0 = no fire-rate check
    float maxDamage = 0.0f;        ///< body damage per hit, 0 = no damage check
    float headMultiplier = 1.0f;   ///< a headshot may do maxDamage x this
    float maxRange = 0.0f;         ///< 0 = any
};
/// An ability with a cooldown the server knows.
struct AbilityDef { std::string name; float cooldownSeconds = 0.0f; };
/// A state counter (currency, ammo, a stack size) and the fastest it may rise.
struct EconomyDef { std::string key; float maxGainPerSecond = 0.0f; };

struct DetectSettings {
    /// Master switch. Off: every method is a no-op and nothing is stored.
    bool enabled = false;

    // ---- movement (units / second, +Y is up) -------------------------------
    /// Fastest a player's object may move horizontally. 0 = the speed checks
    /// are off (AntiCheat falls back to maxSyncSpeed when wiring it).
    float maxSpeed = 0.0f;
    float speedTolerance = 1.25f;   ///< shortest window
    float speedToleranceMid = 1.15f;
    float speedToleranceLong = 1.08f;
    float speedWindowShort = 0.5f, speedWindowMid = 2.0f, speedWindowLong = 6.0f;
    float burstTolerance = 1.5f;     ///< per-sample travel budget: refills at maxSpeed x this
    float burstBankSeconds = 1.5f;   ///< ...and holds up to this many seconds of travel
    float teleportDistance = 12.0f;  ///< a jump this far beyond what maxSpeed allows
    float maxVerticalSpeed = 0.0f;   ///< terminal fall / launch speed; 0 = derived from gravity and jump height
    float gravity = 0.0f;            ///< world gravity (units/s^2); 0 = the vertical checks are off
    float maxJumpHeight = 0.0f;      ///< highest legitimate jump; 0 = the vertical checks are off
    float stepHeight = 0.6f;         ///< stairs / ledges walked up without a jump
    float slopeLimit = 1.5f;         ///< steepest walkable rise / run
    float groundEpsilon = 0.35f;     ///< within this of the ground counts as standing on it
    float hoverWindow = 0.6f;        ///< airborne this long past the jump's apex without falling = flying
    float noclipMinStep = 0.02f;     ///< ignore steps shorter than this for the wall test
    float noclipMaxGap = 0.35f;      ///< ...and gaps longer than this (the path may bend)
    float ghostSpeedFraction = 0.4f; ///< moving faster than this x maxSpeed with no input
    float ghostWindow = 1.0f;        ///< ...for this long
    float staleInputSeconds = 0.6f;  ///< input older than this says nothing
    float maxJumpsPerSecond = 4.0f;  ///< 0 = off

    // ---- combat ------------------------------------------------------------
    std::vector<WeaponDef> weapons;
    float fireRateTolerance = 1.2f;
    float fireBankSeconds = 0.35f;   ///< shots a lag spike may bunch together
    int   accuracyWindow = 40;       ///< sliding window, in shots; 0 = accuracy checks off
    int   accuracyMinShots = 30;
    float maxAccuracy = 0.95f;
    int   headshotMinHits = 20;
    float maxHeadshotRatio = 0.8f;
    float accuracyMinRange = 0.0f;   ///< shots closer than this don't count towards accuracy
    float aimSnapDegrees = 25.0f;    ///< a one-sample turn at least this far...
    float aimSnapSpeed = 1500.0f;    ///< ...and this fast (deg/s), followed by a hit within
    float aimSnapShotWindow = 0.15f; ///< ...this many seconds, is a snap-and-shoot
    int   aimSnapConfirm = 4;        ///< ...and it takes this many in aimSnapWithin seconds
    float aimSnapWithin = 30.0f;

    // ---- economy / state ---------------------------------------------------
    std::vector<AbilityDef> abilities;
    std::vector<EconomyDef> economy;
    float cooldownTolerance = 0.1f;
    float economyTolerance = 0.0f;   ///< a claimed counter may exceed what the server expects by this
    float maxItemsPerSecond = 8.0f;  ///< same item acquired this fast = a duplication pattern (0 = off)

    // ---- network -----------------------------------------------------------
    float maxMessagesPerSecond = 90.0f;  ///< sustained; 0 = off
    float messageBurstSeconds = 0.75f;   ///< ...with this many seconds of bank
    float maxMalformedPerMinute = 12.0f; ///< 0 = off
    float maxCorrectionsPerMinute = 24.0f;
    float clockDrift = 0.10f;            ///< game clock vs the server's, as a fraction
    float clockWindow = 2.0f;
    float replayWindow = 128.0f;         ///< sequence numbers remembered

    // ---- suspicion model ---------------------------------------------------
    float riskHalfLife = 90.0f;      ///< risk halves every this many seconds
    float riskCap = 200.0f;
    float evidenceInterval = 0.5f;   ///< one detector scores at most once per this
    float severityCap = 4.0f;        ///< value / threshold counts at most this many times
    int   confirmations = 2;         ///< soft detectors need this many hits within confirmWindow
    float confirmWindow = 3.0f;
    int   evidenceCap = 48;          ///< evidence records kept per player
    float snapshotSeconds = 8.0f;    ///< state ring buffer length
    int   snapshotCap = 160;
    int   evidenceSnapshots = 6;     ///< samples attached to each evidence record
    /// Responses by risk (0 = that rung is off).
    float logAt = 10.0f, flagAt = 25.0f, rateLimitAt = 40.0f, shadowAt = 60.0f, kickAt = 90.0f, banAt = 140.0f;
    float releaseRatio = 0.6f;       ///< a rung is left when risk < threshold x this
    float rateLimitFactor = 0.5f;    ///< RateLimit: a peer's message budget x this
    std::array<float, (std::size_t)DetectKind::Count> weight{};   ///< risk per scored evidence at severity 1 (defaults set in the constructor)

    // ---- false-positive protection ----------------------------------------
    float graceTeleport = 1.0f;
    float graceRespawn = 2.0f;
    float graceMigration = 6.0f;
    float graceJoin = 3.0f;          ///< warm-up for a new player
    float lagSpikeGap = 0.35f;       ///< silence this long = a lag spike (grace follows)
    float graceLag = 1.0f;
    int   maxLagGracesPerMinute = 6; ///< past this a player's lag spikes stop buying grace
    float pingTolerance = 0.5f;      ///< thresholds loosen by this x the round trip (seconds)
    float maxToleratedRtt = 0.4f;    ///< ...counting at most this much ping

    DetectSettings();
    const WeaponDef* FindWeapon(const std::string& name) const;
    const AbilityDef* FindAbility(const std::string& name) const;
    const EconomyDef* FindEconomy(const std::string& key) const;

    /// ".okayac" lines, each "detect.<key> <value>"; the AntiCheat parser hands
    /// them here. False + error on an unknown key or a bad value.
    bool ParseLine(const std::string& key, const std::string& value, std::string* error);
    /// Only what differs from the defaults ("" when nothing does).
    std::string ToText() const;
};

/// One server-side sample of a player, kept in a ring buffer.
struct StateSnap {
    double t = 0.0;
    Vec3 pos{0, 0, 0};
    float yaw = 0.0f, pitch = 0.0f;
    float rtt = 0.0f;
};

struct Evidence {
    DetectKind kind = DetectKind::Violation;
    double t = 0.0;
    std::uint64_t tick = 0;     ///< the detector's own count of events seen from this player
    float value = 0.0f;         ///< what was measured
    float threshold = 0.0f;     ///< what was allowed
    float weight = 0.0f;        ///< risk this record added
    std::string detail;
    std::vector<StateSnap> snaps;
};

struct DetectResponse {
    std::uint32_t peer = 0;
    DetectAction action = DetectAction::None;
    float risk = 0.0f;
    double t = 0.0;
};

/// What a game may hand over about the world (all optional).
struct DetectHooks {
    /// Height of the ground under `pos` (y of the surface); false = unknown.
    std::function<bool(const Vec3& pos, float& groundY)> groundAt;
    /// Is a static solid between the two points (a server raycast)?
    std::function<bool(const Vec3& a, const Vec3& b)> staticBlocked;
};

/// What the server knows about a shot a client claimed.
struct ShotEvent {
    double t = 0.0;
    std::string weapon;
    bool hit = false;           ///< the server accepted it as a hit
    bool headshot = false;
    float damage = 0.0f;        ///< what the client / game says it does
    float distance = 0.0f;      ///< shooter to target
    bool blockedByWall = false; ///< the server's raycast found a solid between
    float offRay = 0.0f;        ///< how far the target was from the shot's ray ...
    float allowedOffRay = 0.0f; ///< ... and what lag compensation allows (0 = not measured)
    bool badOrigin = false;     ///< shot from where the shooter wasn't
    bool outOfRange = false;
    std::string detail;
};

struct EvidenceBundle;
struct BundleOptions;

class CheatDetector {
public:
    DetectSettings settings;
    DetectHooks hooks;

    /// Called when a peer reaches a rung of the response ladder (once per escalation).
    std::function<void(const DetectResponse&)> onResponse;
    /// Called for every scored evidence record.
    std::function<void(std::uint32_t peer, const Evidence&)> onEvidence;

    bool Enabled() const { return settings.enabled; }
    double Now() const { return m_now; }
    /// Move the detector's clock to `now` (never backwards) and decay risk.
    void Advance(double now);

    // ---- who ---------------------------------------------------------------
    void NoteJoin(std::uint32_t peer, double t);
    void ForgetPeer(std::uint32_t peer);
    /// A peer rejoining takes back the risk it left with (so leaving resets nothing).
    void SeedRisk(std::uint32_t peer, float risk, double t);
    void SetPing(std::uint32_t peer, float rttSeconds);

    // ---- false-positive protection (call from game code on legitimate jumps) --
    void NoteTeleport(std::uint32_t peer, double t);
    void NoteRespawn(std::uint32_t peer, double t);
    void NoteHostMigration(double t);                       ///< everyone: the new host's view differs
    void NoteSpeedBoost(std::uint32_t peer, float multiplier, double until);
    void NoteExternalForce(std::uint32_t peer, double until); ///< knockback, vehicles, platforms, ice
    void NoteFlightAllowed(std::uint32_t peer, double until); ///< jetpack, glide, noclip for admins

    // ---- movement ----------------------------------------------------------
    /// A position the client claimed for `subject` (its avatar's net id).
    void OnMove(std::uint32_t peer, const std::string& subject, const Vec3& pos, double t);
    /// What the player is asking to do (the stick / keys) and where it looks.
    void OnInput(std::uint32_t peer, float moveX, float moveY, double t);
    void OnAim(std::uint32_t peer, float yawDeg, float pitchDeg, double t);
    void OnJump(std::uint32_t peer, double t);

    // ---- combat ------------------------------------------------------------
    void OnShot(std::uint32_t peer, const ShotEvent& shot);

    // ---- economy / state ----------------------------------------------------
    /// The server's own record of a counter (on join, load, trade).
    void SetBalance(std::uint32_t peer, const std::string& key, float value, double t);
    /// The server gave / took: the counter's expected value follows.
    void OnGrant(std::uint32_t peer, const std::string& key, float amount, double t);
    /// A client claims a counter now holds `value`. Above what grants explain = evidence.
    void OnClaimedValue(std::uint32_t peer, const std::string& key, float value, double t);
    /// An item (a unique id) now sits in this player's inventory.
    void OnItemAcquire(std::uint32_t peer, const std::string& itemUid, const std::string& itemName, double t);
    void OnItemRelease(std::uint32_t peer, const std::string& itemUid);
    void OnAbility(std::uint32_t peer, const std::string& ability, double t);

    // ---- network -----------------------------------------------------------
    /// A message from the client. Returns false once the peer is over its
    /// (rate-limited) budget: the caller drops it.
    bool OnMessage(std::uint32_t peer, double t, std::size_t bytes = 0);
    /// A sequence number the transport read; false = a replay (seen / too old).
    bool OnSequence(std::uint32_t peer, std::uint64_t seq, double t);
    void OnMalformed(std::uint32_t peer, const std::string& what, double t);
    /// The transport refused a packet as a replay (the secure channel's window).
    void OnReplay(std::uint32_t peer, double t);
    void OnCorrection(std::uint32_t peer, double t);        ///< the server had to correct this peer
    void OnClientTick(std::uint32_t peer, std::uint64_t tick, double t);
    void OnClock(std::uint32_t peer, double clientSeconds, double t);
    /// An immediate AntiCheat refusal (forged ownership, ticket, spawn ...).
    void OnViolation(std::uint32_t peer, const std::string& what, const std::string& detail, double t, float weightScale = 1.0f);

    // ---- suspicion model ----------------------------------------------------
    float Risk(std::uint32_t peer) const;
    DetectAction Action(std::uint32_t peer) const;
    bool RateLimited(std::uint32_t peer) const { return Action(peer) >= DetectAction::RateLimit; }
    bool ShadowRestricted(std::uint32_t peer) const { return Action(peer) >= DetectAction::ShadowRestrict; }
    /// A kick / ban request is pending (the caller kicks, then ForgetPeer).
    bool KickRequested(std::uint32_t peer) const { return Action(peer) >= DetectAction::Kick; }
    bool BanRequested(std::uint32_t peer) const { return Action(peer) >= DetectAction::BanRequest; }
    const std::deque<Evidence>* EvidenceOf(std::uint32_t peer) const;
    std::vector<StateSnap> SnapshotsOf(std::uint32_t peer) const;
    /// Scored evidence records per kind for a peer.
    int Count(std::uint32_t peer, DetectKind k) const;
    std::size_t Peers() const { return m_players.size(); }
    /// Raw detections that did not score (grace, unconfirmed, throttled): for tuning.
    int Suppressed(std::uint32_t peer, DetectKind k) const;

    /// A bundle for a report. `account` / `address` are only copied in when opts allow.
    EvidenceBundle Bundle(std::uint32_t peer, const BundleOptions& opts,
                          const std::string& account = std::string(), const std::string& address = std::string()) const;

    void Reset();

private:
    struct Sample { double t; Vec3 pos; };
    struct Track {
        bool have = false;
        Vec3 pos{0, 0, 0};
        double t = 0.0;
        float budget = 0.0f;
        std::deque<Sample> window;
        // vertical
        double airSince = -1.0;       // continuous airborne since
        float airStartY = 0.0f;
        float lastGroundY = 0.0f;      // y at the last sample that stood on the ground
        bool hasGroundY = false;
        float apexY = 0.0f;
        float ascentRun = 0.0f;
        float lastY = 0.0f;
        double ascentSince = -1.0;     // an ascent run (no ground hook)
        float ascentY = 0.0f;
        // ghost movement
        double ghostSince = -1.0;
    };
    struct Shot { bool hit; bool head; float dist; };
    struct Bucket { double t = 0.0; float tokens = 0.0f; bool init = false; };
    struct Player {
        double joinedAt = 0.0, lastEvent = -1.0, lastDecay = 0.0;
        double graceUntil = 0.0, lagGraceUntil = 0.0, forceUntil = 0.0, flightUntil = 0.0, boostUntil = 0.0;
        float boost = 1.0f, rtt = 0.0f, risk = 0.0f;
        DetectAction action = DetectAction::None;
        std::uint64_t events = 0;
        std::vector<double> lagGraces;
        std::unordered_map<std::string, Track> tracks;
        std::string primary;
        std::deque<StateSnap> ring;
        double lastSnapT = -1e9;
        std::deque<Evidence> evidence;
        std::array<int, (std::size_t)DetectKind::Count> scored{}, suppressed{};
        std::array<double, (std::size_t)DetectKind::Count> lastScore{};
        std::array<std::deque<double>, (std::size_t)DetectKind::Count> recent;   // confirmation window
        // input / aim
        float inMag = 0.0f; double inT = -1e9; double inMovingSince = -1.0, inIdleSince = -1.0;
        float yaw = 0.0f, pitch = 0.0f; double aimT = -1e9; bool haveAim = false;
        double snapT = -1e9; float snapSpeed = 0.0f;
        std::deque<double> snapHits;
        std::deque<double> jumps;
        // combat
        std::deque<Shot> shots;
        std::unordered_map<std::string, Bucket> fire;
        std::unordered_map<std::string, double> abilityLast;
        // economy
        std::unordered_map<std::string, float> expected;
        std::unordered_map<std::string, double> expectedT;
        std::unordered_map<std::string, std::deque<double>> itemTimes;
        // network
        Bucket msgs; double msgWinT = 0.0; int msgWinN = 0;
        std::deque<double> malformed, corrections;
        std::vector<std::uint64_t> seqs; std::uint64_t seqMax = 0; bool haveSeq = false;
        std::uint64_t lastTick = 0; bool haveTick = false;
        bool haveClock = false; double clockC0 = 0.0, clockS0 = 0.0; int clockBad = 0;
    };
    std::unordered_map<std::uint32_t, Player> m_players;
    std::unordered_map<std::string, std::uint32_t> m_itemHolder;   // item uid -> peer
    double m_now = 0.0;
    double m_migrationUntil = 0.0;
    std::size_t m_evictCursor = 0;

    Player& P(std::uint32_t peer, double t);
    void Touch(Player& p, std::uint32_t peer, double t);
    bool InGrace(const Player& p, double t) const { return t < p.graceUntil || t < m_migrationUntil || t < p.joinedAt + settings.graceJoin; }
    float PingSlack(const Player& p) const;
    /// A detector fired. Returns true if it scored (soft ones need confirmations, and one kind scores at most once per evidenceInterval).
    bool Raise(std::uint32_t peer, Player& p, DetectKind k, double t, float value, float threshold,
               float severity, const std::string& detail, bool soft, float scale = 1.0f);
    void Escalate(std::uint32_t peer, Player& p, double t);
    void DecayTo(Player& p, double t);
    void PushSnap(Player& p, const Vec3& pos, double t);
    void CheckVertical(std::uint32_t peer, Player& p, Track& tr, const Vec3& prev, const Vec3& pos, double dt, double t);
    void CheckAccuracy(std::uint32_t peer, Player& p, double t);
    friend struct EvidenceBundle;
};

// --------------------------------------------------------------------------
// Evidence bundle: what a server attaches to a report.
// --------------------------------------------------------------------------
struct BundleOptions {
    std::size_t maxBytes = 12 * 1024;   ///< the JSON is cut (snapshots first, then oldest evidence) to fit
    int maxEvidence = 32;
    int maxSnapshots = 48;
    bool includeAccount = false;        ///< copy the account id
    bool includeAddress = false;        ///< copy the network address (off: never in the bundle)
    std::string server;                 ///< the server's own id (not a secret) or ""
    std::string game;
};

struct EvidenceBundle {
    static constexpr int kVersion = 1;
    int version = kVersion;
    std::string game, server, account, address;
    std::uint32_t peer = 0;
    float risk = 0.0f;
    std::string action;                 ///< DetectActionName
    double t = 0.0;
    bool truncated = false;
    std::vector<std::pair<std::string, int>> counts;   ///< scored records per kind
    std::vector<std::pair<std::string, float>> thresholds;   ///< the settings that matter to the verdict
    std::vector<Evidence> evidence;
    std::vector<StateSnap> snaps;

    /// Compact JSON, at most maxBytes (0 = no cap); sets `truncated` on the copy it writes.
    std::string ToJson(std::size_t maxBytes = 0) const;
    /// Parse JSON a (possibly hostile) peer sent: bounded size / depth / counts,
    /// finite numbers, strings cleaned. False + error on anything else.
    static bool Parse(const std::string& text, EvidenceBundle& out, std::string* error = nullptr);
    /// Redact long token-like runs and control characters from a free-text field.
    static std::string Scrub(const std::string& s, std::size_t maxLen = 120);
    static constexpr std::size_t kMaxParseBytes = 256 * 1024;
};

} // namespace okay
