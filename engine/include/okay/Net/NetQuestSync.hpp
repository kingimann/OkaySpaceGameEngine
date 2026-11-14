#pragma once
// Quest log and story variables over the network (docs/multiplayer_audit.md, docs/quests.md).
//
// The server is the only one that changes quest state and story variables. Its QuestLog and StoryState
// are the truth; every change is sent to the clients (ordered, "__qst"), and a client that joins later is
// sent the whole state. A client's QuestLog is a read-only copy (QuestLog::remoteAuthority: starting,
// progress and the kill / reach / dialogue / collect sources change nothing there), and a write to its
// StoryState (a dialogue's Set node, a script) is not applied but sent to the server as a request
// ("__req/quest"), which decides.
//
// Two kinds of state:
//   shared     one copy for everybody: a quest the whole group is on, a door the story opened. This is
//              the server's own QuestLog / StoryState content.
//   per player each player has its own: a personal quest (perPlayerQuests), a dialogue choice or a flag
//              under perPlayerVars. The server keeps each player's copy and sends it to that player only.
//              The host's own per-player state is what its QuestLog / StoryState hold.
// Quests are shared unless listed; story variables are shared unless they match a per-player prefix.
//
// What a client may ask: to set or erase a variable or flag. A per-player name always lands in its own
// copy. A shared name only if it matches `clientWritableVars` (empty by default: refused). Nothing else:
// no client can start, complete or reset a quest, or give itself progress. Per-player quests advance
// through this class on the server (StartQuest(peer, id), AddProgress(peer, ...)), with the same rules
// as the QuestLog's own and no toasts, rewards or events (onPlayerQuest tells the game what happened).
//
// Runs inside NetworkManager (NetworkManager::Quests()); idle while the scene has no QuestLog or
// StoryState. A saved game is the server's business: the clients are copies.
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/StoryState.hpp"
#include "okay/VisualScript/VsValue.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

class Scene;
class NetworkManager;

namespace net {

class QuestSync {
public:
    explicit QuestSync(NetworkManager& nm);
    ~QuestSync();

    // ---- Rules ----
    bool enabled = true;
    /// Quest ids each player has its own copy of (the rest are shared).
    std::vector<std::string> perPlayerQuests;
    /// Story variable and flag name prefixes each player has its own copy of (the rest are shared).
    std::vector<std::string> perPlayerVars;
    /// Shared names (by prefix) a client may ask the server to change. Empty: none.
    std::vector<std::string> clientWritableVars;
    /// Requests per second a client may send (a burst of twice this); more are dropped.
    int   maxRequestsPerSecond = 20;
    /// Most variables a player may hold in its own copy, and the most the shared story may hold.
    int   maxPlayerVars = 256;
    int   maxSharedVars = 4096;
    /// Seconds between looking for changes to send.
    float syncInterval = 0.1f;
    /// Seconds a player's copy is kept after it left (it comes back if the player rejoins as itself).
    float keepSeconds = 600.0f;

    // ---- Server: per-player quests (peer 0 = the host, whose log is the QuestLog itself) ----
    bool StartQuest(std::uint32_t peer, const std::string& id);
    bool CompleteQuest(std::uint32_t peer, const std::string& id);
    bool FailQuest(std::uint32_t peer, const std::string& id);
    bool ResetQuest(std::uint32_t peer, const std::string& id);
    bool CompleteObjective(std::uint32_t peer, const std::string& id, int index);
    bool AddProgress(std::uint32_t peer, const std::string& id, int index, int amount);
    QuestStatus Status(std::uint32_t peer, const std::string& id) const;
    int  Progress(std::uint32_t peer, const std::string& id, int index) const;
    /// Server: one player's own variable / flag (a per-player name; a shared name is the StoryState's).
    bool SetPlayerVar(std::uint32_t peer, const std::string& name, const vs::VsValue& v);
    vs::VsValue PlayerVar(std::uint32_t peer, const std::string& name) const;
    bool SetPlayerFlag(std::uint32_t peer, const std::string& name, bool on);
    bool PlayerFlag(std::uint32_t peer, const std::string& name) const;
    /// Server: a per-player quest changed state (peer, quest id, new status). Give rewards here.
    std::function<void(std::uint32_t, const std::string&, QuestStatus)> onPlayerQuest;

    // ---- Scope ----
    bool QuestIsPerPlayer(const std::string& id) const;
    bool VarIsPerPlayer(const std::string& name) const;

    // ---- What happened ----
    int RequestsApplied() const { return m_applied; }
    int RequestsRefused() const { return m_refused; }
    int Malformed() const { return m_malformed; }
    int SnapshotsApplied() const { return m_snapshots; }
    const std::string& LastRefusal() const { return m_lastRefusal; }

    // ---- Driven by NetworkManager ----
    void Tick(Scene* scene, float dt);
    void Reset();
    /// The server's "__qst" stream, as a client reads it.
    void OnServerMessage(const std::string& data);

private:
    struct PeerState {
        std::vector<QuestRecord> records;
        std::map<std::string, vs::VsValue> vars;
        std::set<std::string> flags;
        std::map<std::string, std::string> sent;   // what this player has been told (key -> line)
        float tokens = 0.0f;
        double last = 0.0;
        double seen = 0.0;                          // last time it was connected
    };
    using Lines = std::map<std::string, std::string>;   // "q:id" / "v:name" / "f:name" -> the line that sets it

    NetworkManager& m_nm;
    Scene* m_scene = nullptr;
    double m_time = 0.0;
    float m_acc = 0.0f, m_scan = 0.0f, m_present = 0.0f;
    bool m_have = false;
    bool m_handlersOn = false;
    int  m_joinHook = 0;
    int  m_applied = 0, m_refused = 0, m_malformed = 0, m_snapshots = 0;
    std::string m_lastRefusal;
    Lines m_shared;                                 // server: what everybody has been told
    std::unordered_map<std::uint32_t, PeerState> m_peers;
    // client
    std::set<std::string> m_staleQ, m_staleV, m_staleF;
    bool m_inSnapshot = false, m_synced = false;
    std::uint64_t m_hookedStory = 0, m_hookedLog = 0;

    PeerState* Peer(std::uint32_t peer, bool create);
    bool PeerConnected(std::uint32_t peer) const;
    void ServerOn();
    void ServerOff();
    void Flush();
    void SendSnapshot(std::uint32_t peer);
    Lines SharedNow() const;
    Lines PlayerNow(const PeerState& ps) const;
    void SendDiff(const Lines& now, Lines& was, std::int64_t only);
    void OnRequest(std::uint32_t peer, const std::string& data);
    void Refuse(const std::string& why) { ++m_refused; m_lastRefusal = why; }
    bool TakeToken(PeerState& ps);
    bool MayWriteShared(const std::string& name) const;
    bool RunQuestOp(std::uint32_t peer, const std::string& id, const std::function<bool(QuestLog&)>& op);

    // client
    void HookClient(Scene& scene);
    void UnhookClient();
    void ApplyLine(const std::string& line);
    bool ClientWrite(const StoryState::Change& c);
};

} // namespace net
} // namespace okay
