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
    /// Fastest a client's NetworkSync object may move, units / second (0 = no check).
    float maxSyncSpeed = 0.0f;
    /// SyncVar writes a client may make per second (0 = unlimited).
    int   maxSyncVarsPerSecond = 0;
    /// Kick a client after this many refused messages within a minute (0 = never).
    int   kickAfterViolations = 0;
    /// "<netId>.<name>" SyncVar keys may only be written by that object's owner.
    bool  ownerOnlySyncVars = true;
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

    /// .okayac text: one "key value" per line, '#' comments:
    ///   enabled true | spawn <prefab path> (repeatable) | maxSyncSpeed 12
    ///   maxSyncVarsPerSecond 20 | kickAfterViolations 5 | ownerOnlySyncVars true
    ///   requireEncryption true | ticketKey <base64url> | ticketAudience <server id>
    ///   maxRpcsPerSecond 30 | maxRpcBytes 4096
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
};
const char* CheatKindName(CheatKind k);

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

    /// Objects the server itself drives (Authority::Host on the server): clients
    /// may never send their transform / despawn them.
    std::function<bool(const std::string& netId)> serverOwns;
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
    void Reset();

private:
    struct PeerInfo {
        int violations = 0;
        float violationTimer = 0.0f;
        int syncVars = 0;
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
