#pragma once
// ---------------------------------------------------------------------------
// The in-game server browser's engine side: fetch the public server list
// (Supabase game_sessions, when signed in), scan the LAN, and ping every server
// — all on a background thread, so a game's menu never stutters. Results are
// filtered to this game. The player's built-in "Find a server" window and the
// net_browser_* script functions both read the shared instance.
// ---------------------------------------------------------------------------
#include "okay/Net/JoinLink.hpp"
#include "okay/Net/ServerQuery.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay {

class ServerBrowser {
public:
    struct Entry {
        ServerInfo info;          ///< live answer (ping, players, flags); responded=false if it didn't
        std::string game, region;
        bool listed = false;      ///< from the public list (else found on the LAN)
        /// A join link for it (add the password for a locked server).
        JoinLink Link() const {
            JoinLink l; l.game = game; l.addr = info.Address(); l.name = info.name; l.needsPassword = info.password;
            return l;
        }
    };
    enum Sources { kInternet = 1, kLan = 2, kAll = 3 };

    ServerBrowser();
    ~ServerBrowser();
    ServerBrowser(const ServerBrowser&) = delete;
    ServerBrowser& operator=(const ServerBrowser&) = delete;

    /// The one the game uses (scripts + the built-in window).
    static ServerBrowser& Shared();

    /// Only list servers for this game ("" = every game). The player sets it to
    /// the game's title.
    void SetGame(const std::string& title);
    const std::string& Game() const;

    /// Start a refresh in the background (ignored while one is running).
    void Refresh(int sources = kAll);
    /// True while a refresh is running.
    bool Busy() const;
    /// The latest results: servers that answered first (by ping), then listed ones
    /// that didn't. Safe to call every frame.
    std::vector<Entry> Results() const;
    /// Bumped each time new results land (cheap change detection).
    std::uint32_t Version() const;

    /// Built-in window: games ask for it with net_browser_open(); the player draws it.
    bool windowOpen = false;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace okay
