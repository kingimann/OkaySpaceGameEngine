#pragma once
// ---------------------------------------------------------------------------
// AntiCheat — opt-in server-side checks on what clients send (P1: authority).
//
// The server already trusts nothing about a client's avatar position (the
// maxMoveSpeed check). With anti-cheat on it also refuses, from a client:
//   * __xform / __despawn for a networked object that peer doesn't own
//     (owners come from SpawnOwned ids "sp/<peer>/<n>", or the first peer to
//     drive an id; objects the server drives itself are the server's);
//   * __spawn of a prefab that isn't on the game's allowlist (or a SpawnOwned
//     id claiming another peer);
//   * SyncVar writes to "<netId>.<name>" / "<netId>/<name>" keys of an object it
//     doesn't own, and SyncVars past a per-second rate;
//   * NetworkSync objects moving faster than maxSyncSpeed (speed hacks).
// Each refusal is reported (NetworkManager::SetCheatHandler) and counts as a
// violation; a peer with too many is kicked.
//
// It is a small layer NetworkManager calls from its receive path, so the wire
// format and the behaviour with anti-cheat off are exactly as before.
// ---------------------------------------------------------------------------
#include "okay/Math/Vec3.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

/// The "__spawn" message, "<path>|x|y|z[|<netId>]", read exactly the way a
/// receiving peer reads it (NetworkManager::Deliver uses this too), so what the
/// server checks is always what gets spawned.
struct SpawnMessage { std::string path; Vec3 pos; std::string netId; };
SpawnMessage ParseSpawnMessage(const std::string& data);

/// What the server enforces (set in code or parsed from .okayac text).
struct AntiCheatSettings {
    bool  enabled = false;
    /// Prefab paths clients may __spawn / SpawnOwned. Empty = clients may spawn nothing.
    std::vector<std::string> spawnAllowlist;
    /// Spawns a client may make per second (0 = unlimited).
    int   maxSpawnsPerSecond = 5;
    /// SpawnOwned objects a client may have at once (0 = unlimited).
    int   maxLiveSpawnsPerPeer = 50;
    /// How far from the sender a spawn may be placed (0 = anywhere).
    float spawnRadius = 0.0f;
    /// Fastest a client's NetworkSync object may move, units / second (0 = no check).
    float maxSyncSpeed = 0.0f;
    /// SyncVar writes a client may make per second (0 = unlimited).
    int   maxSyncVarsPerSecond = 0;
    /// Kick a client after this many refused messages within a minute (0 = never).
    int   kickAfterViolations = 0;
    /// "<netId>.<name>" SyncVar keys may only be written by that object's owner.
    bool  ownerOnlySyncVars = true;
    /// SyncVar keys only the server may write ("score", "money", "match."): a
    /// client writing a key that starts with one is refused (forged syncvar)
    /// and told the server's value. "srv." keys are always server-only.
    std::vector<std::string> serverVars;
    /// When any is set, clients may only write SyncVar keys starting with one
    /// of these ("player.", "chat."); every other key is the server's.
    std::vector<std::string> clientVars;
    /// Calls a client may make per second to any one RPC (0 = unlimited). A
    /// NetValidator with its own calls/s for that RPC takes its place.
    int   maxRpcsPerSecond = 0;
    /// Longest RPC payload a client may send (0 = any). A NetValidator with its
    /// own max bytes for that RPC takes its place.
    int   maxRpcBytes = 0;

    // ---- P2: transport ----
    /// Only encrypted clients may join (needs libsodium on both ends; turns the
    /// server's `encryption` on). Every packet then also carries a sequence
    /// number, replays are refused, and plaintext game packets from joined
    /// clients are dropped.
    bool  requireEncryption = false;
    /// A session ticket service's ed25519 PUBLIC key (base64url). When set, a
    /// joining client's token must be a valid ticket (okay/Net/SessionTicket.hpp)
    /// and the peer is bound to its account id. "" = no ticket check.
    std::string ticketKey;
    /// This server's id as tickets name it ("aud"); "" = accept any.
    std::string ticketAudience;
    /// How far a client's game clock may run off the server's, as a fraction
    /// (0.05 = 5%; 0 = no check). The server asks each client for its clock
    /// about once a second; a client that runs too fast or slow for three
    /// 2-second windows in a row (a speed hack) is reported, and so is one that
    /// stops answering. Clients older than this check never answer: turn it
    /// on once every player has the update.
    float maxClockDrift = 0.0f;

    // ---- Hit claims (NetworkManager::ClaimHit / "__hit") ----
    // The server checks every claim against its own view, whether or not the
    // anti-cheat is enabled; these tune it.
    /// How far a shot may start from the shooter's position as the server knows it.
    float hitOriginTolerance = 2.0f;
    /// How far off the ray the target may be, plus its top speed x the shooter's ping.
    float hitTolerance = 1.0f;
    /// The longest shot (0 = any range).
    float maxHitRange = 0.0f;
    /// Damage a hit does, unless the game's Validate Hit says otherwise.
    float hitDamage = 10.0f;
    /// A server raycast from the shot's origin must reach the target (no shooting through walls).
    bool  hitLineOfSight = true;
    /// Weapons clients may claim hits with, besides those an enabled Net
    /// Validator covers ("hit/<weapon>"). With the anti-cheat on, a claim with
    /// any other weapon does no damage.
    std::vector<std::string> weapons;
    /// Refuse client hit claims when the server can't check where the shooter
    /// is: no maxMoveSpeed, and the server doesn't move the shooter's pawn.
    bool  hitsNeedMoveCheck = true;

    /// .okayac text: one "key value" per line, '#' comments:
    ///   enabled true | spawn <prefab path> (repeatable) | maxSyncSpeed 12
    ///   maxSyncVarsPerSecond 20 | kickAfterViolations 5 | ownerOnlySyncVars true
    ///   requireEncryption true | ticketKey <base64url> | ticketAudience <server id>
    ///   maxRpcsPerSecond 30 | maxRpcBytes 4096 | maxClockDrift 0.05
    ///   serverVar <key prefix> (repeatable) | clientVar <key prefix> (repeatable)
    ///   maxSpawnsPerSecond 5 | maxLiveSpawnsPerPeer 50 | spawnRadius 0
    ///   hitOriginTolerance 2 | hitTolerance 1 | maxHitRange 50 | hitDamage 10 | hitLineOfSight true
    ///   weapon <name> (repeatable)
    ///   hitsNeedMoveCheck true
    static bool Parse(const std::string& text, AntiCheatSettings& out, std::string* error = nullptr);
    bool LoadFile(const std::string& path, std::string* error = nullptr);
    std::string ToText() const;
};

enum class CheatKind {
    ForgedTransform,   ///< __xform for an object the peer doesn't own
    ForgedDespawn,     ///< __despawn for an object the peer doesn't own
    ForgedSpawnId,     ///< SpawnOwned id claiming another peer ("sp/<other>/...")
    SpawnNotAllowed,   ///< __spawn of a prefab that isn't on the allowlist
    ForgedSyncVar,     ///< SyncVar on another peer's object
    SyncVarFlood,      ///< SyncVars past maxSyncVarsPerSecond
    SpeedHack,         ///< a NetworkSync object moving faster than maxSyncSpeed
    RpcRejected,       ///< a NetValidator / OnValidateRpc refused the RPC
    RpcFlood,          ///< one RPC called more often than maxRpcsPerSecond
    ClockDrift,        ///< the client's game clock runs off the server's (a speed hack), or it won't say
    HitRejected,       ///< a __hit claim the server's own view doesn't back (detail says why)
};
const char* CheatKindName(CheatKind k);

/// A client's claim that its shot hit a networked object (NetworkManager::ClaimHit),
/// as the server's Validate Hit hook sees it. Only `damage` may be changed.
struct HitClaim {
    std::uint32_t shooter = 0;      ///< peer id (0 = the host)
    std::string   target;           ///< the target's NetworkSync id
    std::string   weapon;
    std::uint32_t clientTick = 0;   ///< the shooter's frame count when it fired
    Vec3 origin{0, 0, 0};
    Vec3 direction{0, 0, 1};        ///< unit length
    float damage = 0.0f;            ///< what the hit does; a hook may change it
};

struct CheatReport {
    std::uint32_t peer = 0;
    CheatKind kind = CheatKind::ForgedTransform;
    std::string detail;   ///< the object id / prefab / key involved
};

class AntiCheat {
public:
    AntiCheatSettings settings;

    /// A message a client sent on `channel` (before the server applies or relays
    /// it). False = refuse it (it's reported and counted).
    bool ValidateMessage(std::uint32_t peer, const std::string& channel, const std::string& data);
    /// A SyncVar write from a client. False = refuse it.
    bool ValidateSyncVar(std::uint32_t peer, const std::string& key);
    /// One call of RPC `name` against maxRpcsPerSecond (unless a NetValidator
    /// rules on its rate). False = over the limit (reported as an RPC flood).
    bool CountRpc(std::uint32_t peer, const std::string& name);
    /// May a client claim hits with this weapon (anti-cheat on)?
    bool KnownWeapon(const std::string& weapon) const;

    /// Objects the server itself drives (Authority::Host on the server): clients
    /// may never send their transform / despawn them.
    std::function<bool(const std::string& netId)> serverOwns;
    /// Where a peer is on the server (for spawnRadius); false if not known.
    std::function<bool(std::uint32_t peer, Vec3& at)> positionOf;
    /// Networked objects that exist on the server. Transforms for any other id
    /// are dropped quietly (nobody could apply them) and claim nothing.
    std::function<bool(const std::string& netId)> exists;
    /// Extra per-RPC check (e.g. per-RPC rules from game code): false refuses it.
    std::function<bool(std::uint32_t peer, const std::string& name, const std::string& data)> validateRpc;
    /// Called for every refusal.
    std::function<void(const CheatReport&)> onCheat;
    /// Does game code rule on this RPC's rate / size itself (a NetValidator with
    /// its own calls/s or max bytes)? Then maxRpcsPerSecond / maxRpcBytes don't apply.
    std::function<void(const std::string& rpc, bool& ownRate, bool& ownSize)> rpcRulesFor;
    /// Does game code have a rule for this RPC (an enabled NetValidator covering
    /// it)? A hit claim's weapon is known when "hit/<weapon>" is covered.
    std::function<bool(const std::string& rpc)> coversRpc;
    /// Game rules for hit claims, after the server's own checks passed: false
    /// refuses the hit (reported as "hit rejected"); it may change claim.damage.
    std::function<bool(HitClaim& claim)> validateHit;
    /// Called when a peer is forgotten (left, kicked, timed out), so per-peer
    /// state kept elsewhere (NetValidator) can go too.
    std::function<void(std::uint32_t peer)> onForgetPeer;
    /// Who installed the handlers above (an AntiCheatConfig's token; 0 = code).
    /// Lets a component remove only what it put there.
    std::uint64_t installer = 0;
    /// Distinct RPC names counted per peer per second; more share one count.
    static constexpr std::size_t kMaxRpcNamesPerPeer = 64;

    /// A legitimate jump for `netId` (a respawn): its next position isn't
    /// speed-checked against the old one.
    void AllowTeleport(const std::string& netId) { m_track.erase(netId); }
    /// The peer that owns `netId` (0 = the server, or nobody yet).
    std::uint32_t OwnerOf(const std::string& netId) const;
    void SetOwner(const std::string& netId, std::uint32_t peer) { m_owner[netId] = peer; }
    /// Refusals from `peer` in the current minute.
    int Violations(std::uint32_t peer) const;
    /// Total refusals since the server started (all peers).
    int TotalReports() const { return m_total; }
    /// Objects whose movement is being tracked (for tests / stats).
    std::size_t TrackedObjects() const { return m_track.size(); }
    /// Advance the anti-cheat clock (a double: it stays exact for months of
    /// uptime) and the per-second / per-minute windows.
    void Tick(float dt);
    /// A peer left: forget its counters (and its objects' last positions).
    void ForgetPeer(std::uint32_t peer);
    /// Keys that start with this are always server-only (with anti-cheat on).
    static constexpr const char* kServerVarPrefix = "srv.";
    /// Only the server may write `key` (see serverVars).
    bool IsServerVar(const std::string& key) const;
    /// Report and count a refusal found outside the message checks (the
    /// network layer's clock heartbeat). Always returns false.
    bool Report(std::uint32_t peer, CheatKind k, const std::string& detail) { return Refuse(peer, k, detail); }
    void Reset();

private:
    struct PeerInfo {
        int violations = 0;
        float violationTimer = 0.0f;
        int syncVars = 0;
        int spawns = 0;   // this second
        float syncVarTimer = 0.0f;
        std::unordered_map<std::string, int> rpcCalls;   // this second, per RPC name
    };
    /// Refusals logged per peer per minute; the rest are only counted.
    static constexpr int kLoggedPerMinute = 5;
    // A NetworkSync object's last position and its travel budget: it refills at
    // 1.25 x maxSyncSpeed (capped at half a second of travel + a quarter unit),
    // and every move spends from it, so many small fast hops are caught as surely
    // as one big one. `peer` is who drives it (so ForgetPeer drops it).
    struct Track { Vec3 pos; double t = 0.0; float budget = 0.0f; std::uint32_t peer = 0; };
    std::unordered_map<std::uint32_t, PeerInfo> m_peers;
    std::unordered_map<std::string, std::uint32_t> m_owner;   // netId -> owning peer
    std::unordered_map<std::string, Track> m_track;           // netId -> last accepted position
    int m_total = 0;
    double m_clock = 0.0;

    bool Refuse(std::uint32_t peer, CheatKind k, const std::string& detail);
    /// True if `peer` may drive `netId` (claiming it if nobody has).
    bool MayDrive(std::uint32_t peer, const std::string& netId, bool claim);
    bool SpawnAllowed(const std::string& path) const;
};

} // namespace okay
