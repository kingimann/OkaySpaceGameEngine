#pragma once
// ---------------------------------------------------------------------------
// Matchmaking queues ("Find match"). A game asks for a match in a mode with N
// players; the Supabase queue (docs/sql/matchmaking.sql) groups waiting players
// by skill and region and says where to play:
//   - a listed dedicated server with room (in this match's own room), or
//   - a player who hosts — through the site relay (everyone gets its code), or,
//     with no relay, by posting its own join link for the others.
// The network calls run on a background thread; Pump() (every frame) connects
// the scene's NetworkManager once a match is found: it joins the server / the
// host, or hosts and tells the others how to reach it.
// Scripts: net_queue_*. The player draws a "Finding a match" banner while it runs.
// ---------------------------------------------------------------------------
#include "okay/Net/JoinLink.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace okay {

class NetworkManager;

struct MatchRequest {
    std::string game;            ///< the game's title ("" = the player's game)
    std::string mode;            ///< "duel", "ranked", "squads"... ("" = default)
    std::string region;          ///< preferred region ("" = any)
    int  players = 2;            ///< players per match
    int  minPlayers = 0;         ///< start with this many after a long wait (0 = full matches only)
    bool withParty = true;       ///< a party leader brings the whole party
};

/// Where a found match is played.
struct MatchAssignment {
    std::string id;
    std::string game, mode;
    int  players = 0;
    std::string serverAddr, serverName, room;   ///< a dedicated server ("host:port") + this match's room
    std::string relayAddr, relayCode;           ///< or a relay + code
    bool isHost = false;                        ///< I host it (relay, or posting my link)
    std::string hostLink;                       ///< the host's join link (no-relay player host)
    /// How the others connect ("" fields = not known yet).
    JoinLink Link() const {
        JoinLink l;
        if (!hostLink.empty() && JoinLink::Parse(hostLink, l)) return l;
        l.game = game; l.name = serverName.empty() ? std::string("Match") : serverName;
        if (!serverAddr.empty()) { l.addr = serverAddr; l.room = room; }
        else if (!relayAddr.empty()) { l.relay = relayAddr; l.code = relayCode; }
        return l;
    }
};

class Matchmaker {
public:
    enum class State {
        Idle,
        Searching,     ///< in the queue
        Waiting,       ///< matched; waiting for the hosting player to come up
        Found,         ///< ready: Pump() connects (or already did)
        Failed         ///< see Error()
    };
    struct PollResult {
        std::string status;          ///< "searching" | "matched" | "gone"
        int waited = 0, searching = 0;
        MatchAssignment match;
    };
    /// The queue service (Supabase by default; tests plug in their own).
    struct Backend {
        virtual ~Backend() = default;
        virtual bool Join(const MatchRequest& r, std::string& ticket, std::string& err) = 0;
        virtual bool Poll(const std::string& ticket, PollResult& out, std::string& err) = 0;
        virtual void Leave(const std::string& ticket) = 0;
        virtual bool HostReady(const std::string& assignment, const std::string& link) = 0;
    };

    Matchmaker();
    ~Matchmaker();
    Matchmaker(const Matchmaker&) = delete;
    Matchmaker& operator=(const Matchmaker&) = delete;

    /// The one games use (scripts, the player's banner, the launcher hand-off).
    static Matchmaker& Shared();

    /// Start searching (cancels a search in progress). False if it can't start
    /// (signed out with the default backend, no threads on the web build).
    bool Start(const MatchRequest& req);
    /// Leave the queue.
    void Cancel();

    State GetState() const;
    static const char* StateName(State s);
    /// Seconds since Start().
    float Elapsed() const;
    /// Players searching in the same queue (as of the last poll).
    int Searching() const;
    std::string Error() const;
    MatchRequest Request() const;
    MatchAssignment Assignment() const;

    /// Call every frame with the scene's NetworkManager: once a match is found,
    /// joins it (or hosts it). Does nothing while searching or without autoConnect.
    void Pump(NetworkManager* net);
    /// Connect automatically when a match is found (default). Turn off to show a
    /// "Match found — Accept" step and call Connect() yourself.
    bool autoConnect = true;
    /// Connect `net` to the found match now. True once it's started joining / hosting.
    bool Connect(NetworkManager* net);
    /// Already connected to the found match.
    bool Connected() const;

    /// Poll interval (seconds) and how long to wait for a hosting player.
    float pollInterval = 2.0f;
    float hostWaitSeconds = 40.0f;
    /// UDP port a no-relay player host listens on (it tries the next few if busy).
    std::uint16_t hostPort = 45000;
    /// Address the host advertises when there's no relay ("" = this PC's LAN address).
    std::string hostPublicAddress;

    /// Replace the queue service (tests). nullptr restores the Supabase one.
    void SetBackend(std::shared_ptr<Backend> b);

    struct Impl;
    struct Worker;
private:
    std::shared_ptr<Impl> m;   // shared with a search's worker thread
    std::unique_ptr<Worker> m_worker;   // the current search's thread (joined before the next starts)
};

} // namespace okay
