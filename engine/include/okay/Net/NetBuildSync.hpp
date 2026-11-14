#pragma once
// Build mode over the network (docs/build_mode.md "Multiplayer", docs/multiplayer_audit.md).
//
// The server is the only one that changes what players built. A client's BuildMode sends its place,
// remove, move, link, unlink and undo as requests ("__req/build"); the server checks each one and, if it
// passes, does it and tells every client in order ("__bld"). A client that joins later is sent the whole
// state. Nothing a client claims is trusted: the prefab must be on the allowlist, the pose inside the
// bounds, the player within reach of where it builds, below the object cap and the request rate, and it
// may only change what it placed itself (ownerOnly).
//
// It runs inside NetworkManager (NetworkManager::Build()); a game with a BuildMode in the scene and a
// NetworkManager needs no setup. Nothing is sent while the scene has no BuildMode.
#include "okay/Components/BuildMode.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

class GameObject;
class NetworkManager;
class Scene;

namespace net {

class BuildSync {
public:
    explicit BuildSync(NetworkManager& nm);
    ~BuildSync();

    // ---- Server rules ----
    bool  enabled = true;
    /// Placed objects in the world (0 = no cap).
    int   maxObjects = 250;
    /// Placed by one player at a time (0 = only the world cap).
    int   maxPerPlayer = 0;
    /// Requests a player may send per second (a burst of twice this); more are dropped unanswered.
    int   maxRequestsPerSecond = 10;
    /// Where pieces may go (world space).
    Vec3  boundsMin{-1000.0f, -1000.0f, -1000.0f};
    Vec3  boundsMax{ 1000.0f,  1000.0f,  1000.0f};
    /// How far from the player's avatar it may build or edit, when the server knows where the avatar is
    /// (0 = no check). BuildMode's own reach is 8; this adds the slack of a laggy avatar.
    float maxReach = 16.0f;
    /// Refuse a piece that would overlap something solid on the server.
    bool  checkBlocked = true;
    /// A player may only move, remove or link what it placed itself (the host may change anything).
    bool  ownerOnly = true;
    /// Players may also edit the level's own Buildable objects that carry a build id in the scene.
    bool  levelEditable = false;
    /// Prefabs a client may place besides the ones the scene's palettes list.
    std::vector<std::string> extraPrefabs;
    /// Per player, how many undo steps are kept.
    static constexpr std::size_t kMaxHistory = 64;

    // ---- What happened ----
    int Accepted() const { return m_accepted; }
    int Refused() const { return m_refused; }
    /// Server: why the last request was refused. Client: the server's last refusal text.
    const std::string& LastRefusal() const { return m_lastRefusal; }
    /// Client: called with the server's refusal text (for a toast).
    std::function<void(const std::string&)> onRefused;
    /// Who placed the object with build id `id` (0 = the host; -1 = nobody knows / not placed by a player).
    long long OwnerOf(int id) const;
    /// Messages a client dropped as malformed (client) / requests the server dropped as malformed.
    int Malformed() const { return m_malformed; }
    /// Client: snapshots applied.
    int SnapshotsApplied() const { return m_snapshots; }
    std::size_t HistorySize(std::uint32_t peer) const;

    // ---- Driven by NetworkManager ----
    void Tick(Scene* scene, float dt);
    /// The session ended: let go of every BuildMode.
    void Reset();
    /// The server's ordered "__bld" stream, as a client reads it.
    void OnServerMessage(const std::string& data);

private:
    struct Hist {
        enum class Type { Place, Remove, Pose, Link, Unlink } type = Type::Place;
        int id = 0, other = 0;
        Vec3 prevPos; Quat prevRot;
        std::string snapshot;
    };
    struct PeerState {
        float tokens = 0.0f;
        double last = 0.0;
        std::deque<Hist> history;
    };

    NetworkManager& m_nm;
    Scene* m_scene = nullptr;
    BuildMode* m_exec = nullptr;
    std::uint64_t m_execId = 0;
    std::vector<std::uint64_t> m_hooked;      // BuildModes (by instance id) whose hooks are ours
    double m_time = 0.0, m_scan = 0.0, m_prune = 0.0;
    float m_present = 0.0f;
    bool m_hasBuild = false;
    bool m_handlersOn = false;
    int  m_joinHook = 0;
    int  m_accepted = 0, m_refused = 0, m_malformed = 0, m_snapshots = 0;
    std::string m_lastRefusal;
    std::unordered_map<std::uint32_t, PeerState> m_peers;
    std::unordered_map<int, std::uint32_t> m_owner;   // build id -> who placed it
    std::set<int> m_removedLevel;                     // level objects players removed (for late joiners)
    std::int64_t m_actor = -1;                        // the peer whose request is being carried out (-1 = the host)
    bool m_undoing = false;
    std::set<int> m_stale;                            // client: placed ids not yet confirmed by a snapshot
    bool m_inSnapshot = false;

    void EnsureExecutor(Scene& scene);
    void Rescan(Scene& scene, bool server, bool client);
    void Unhook();
    void ServerOn();
    void ServerOff();

    // Server
    void OnRequest(std::uint32_t peer, const std::string& data);
    void Refuse(std::uint32_t peer, const std::string& why, bool reply = true);
    bool TakeToken(std::uint32_t peer);
    bool MayEdit(std::uint32_t peer, GameObject* go, std::string& why) const;
    bool InReach(std::uint32_t peer, const Vec3& at) const;
    bool InBounds(const Vec3& p) const;
    void SyncPalette(Scene& scene);
    void OnChange(const BuildMode::Op& op);
    void Broadcast(const std::string& line);
    void SendOne(std::uint32_t peer, GameObject* go);   // tell one client the truth about one object
    void SendSnapshot(std::int64_t peer);   // -1 = every client
    std::string PlaceLine(GameObject* go) const;
    void Record(Hist h);
    bool Undo(std::uint32_t peer, std::string& why);
    int  PlacedBy(std::uint32_t peer) const;

    // Client
    void ApplyLine(const std::string& line);
    bool Send(const std::string& line);
};

} // namespace net
} // namespace okay
