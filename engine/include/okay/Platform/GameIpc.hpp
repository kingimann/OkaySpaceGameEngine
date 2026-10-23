#pragma once
// ---------------------------------------------------------------------------
// The game <-> launcher link (what Steamworks is to the Steam client). A running
// game and the OkaySpace launcher talk through small files in the per-user
// config folder (<config>/OkaySpace/ipc), keyed by the game's process id:
//
//   game-<pid>.presence  game -> launcher  what I'm doing + my join link
//                                          (the launcher shows it to friends)
//   game-<pid>.invite    game -> launcher  "open the invite-friends window"
//   game-<pid>.join      launcher -> game  "join this session now" (an invite
//                                          accepted while the game is running)
//
// Files beat sockets here: nothing to bind, works when either side starts first,
// and a crashed game leaves at most a stale file the launcher ignores by age.
// ---------------------------------------------------------------------------
#include "okay/Platform/Account/AccountService.hpp"   // DefaultConfigDir

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <process.h>
#else
#  include <unistd.h>
#endif

namespace okay {

/// What a running game reports about itself.
struct GamePresence {
    long pid = 0;
    std::string title;      ///< the game's own title
    std::string status;     ///< "In the menus", "Hosting a match", "Playing on EU 1" ...
    std::string join;       ///< a JoinLink ("" = not joinable right now)
    std::string invite;     ///< the same link with the password, for private invites (stays on this PC)
    int  players = 0, maxPlayers = 0;
    bool hosting = false;
    long updated = 0;       ///< unix seconds when written
};

namespace GameIpc {
namespace fs = std::filesystem;

inline fs::path Dir() {
    if (const char* d = std::getenv("OKAY_IPC_DIR")) if (*d) return fs::path(d);   // tests / portable installs
    return okay::account::DefaultConfigDir(fs::path(".")) / "ipc";
}
inline long CurrentPid() {
#if defined(_WIN32)
    return (long)_getpid();
#else
    return (long)getpid();
#endif
}
inline fs::path FileFor(long pid, const char* ext) { return Dir() / ("game-" + std::to_string(pid) + ext); }

inline bool WriteAtomic(const fs::path& p, const std::string& body) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp";
    { std::ofstream f(tmp, std::ios::trunc | std::ios::binary); if (!f) return false; f << body; }
    fs::rename(tmp, p, ec);
    if (ec) { fs::remove(p, ec); ec.clear(); fs::rename(tmp, p, ec); }
    return !ec;
}
inline std::string OneLine(std::string s) { for (char& c : s) if (c == '\n' || c == '\r') c = ' '; return s; }

// ---- game side -------------------------------------------------------------
inline bool WritePresence(const GamePresence& g) {
    std::string b;
    b += "title=" + OneLine(g.title) + "\n";
    b += "status=" + OneLine(g.status) + "\n";
    b += "join=" + OneLine(g.join) + "\n";
    b += "invite=" + OneLine(g.invite) + "\n";
    b += "players=" + std::to_string(g.players) + "\n";
    b += "max=" + std::to_string(g.maxPlayers) + "\n";
    b += "hosting=" + std::string(g.hosting ? "1" : "0") + "\n";
    b += "updated=" + std::to_string((long)std::time(nullptr)) + "\n";
    return WriteAtomic(FileFor(g.pid ? g.pid : CurrentPid(), ".presence"), b);
}
inline void ClearPresence(long pid = 0) {
    std::error_code ec;
    fs::remove(FileFor(pid ? pid : CurrentPid(), ".presence"), ec);
    fs::remove(FileFor(pid ? pid : CurrentPid(), ".join"), ec);
    fs::remove(FileFor(pid ? pid : CurrentPid(), ".invite"), ec);
}
/// Ask the launcher to open its invite-friends window for this game.
inline bool RequestInvite(long pid = 0) { return WriteAtomic(FileFor(pid ? pid : CurrentPid(), ".invite"), "invite\n"); }
/// A join the launcher asked this game to make (consumes it). False if none.
inline bool TakeJoin(std::string& link, long pid = 0) {
    const fs::path p = FileFor(pid ? pid : CurrentPid(), ".join");
    std::ifstream f(p);
    if (!f) return false;
    std::getline(f, link);
    f.close();
    std::error_code ec; fs::remove(p, ec);
    return !link.empty();
}

// ---- launcher side ---------------------------------------------------------
/// Read a game's presence. False if it has none, or it's older than `maxAgeSec`.
inline bool ReadPresence(long pid, GamePresence& out, long maxAgeSec = 30) {
    std::ifstream f(FileFor(pid, ".presence"));
    if (!f) return false;
    out = GamePresence{}; out.pid = pid;
    std::string line;
    while (std::getline(f, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "title") out.title = v;
        else if (k == "status") out.status = v;
        else if (k == "join") out.join = v;
        else if (k == "invite") out.invite = v;
        else if (k == "players") out.players = std::atoi(v.c_str());
        else if (k == "max") out.maxPlayers = std::atoi(v.c_str());
        else if (k == "hosting") out.hosting = (v == "1");
        else if (k == "updated") out.updated = std::atol(v.c_str());
    }
    return out.updated > 0 && (long)std::time(nullptr) - out.updated <= maxAgeSec;
}
/// Every game asking for the invite window (consumes the requests).
inline std::vector<long> TakeInviteRequests() {
    std::vector<long> out;
    std::error_code ec;
    for (auto it = fs::directory_iterator(Dir(), ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (n.size() > 12 && n.compare(0, 5, "game-") == 0 && n.compare(n.size() - 7, 7, ".invite") == 0) {
            out.push_back(std::atol(n.substr(5, n.size() - 12).c_str()));
            std::error_code rec; fs::remove(it->path(), rec);
        }
    }
    return out;
}
/// Tell a running game to join `link`.
inline bool PostJoin(long pid, const std::string& link) { return WriteAtomic(FileFor(pid, ".join"), OneLine(link) + "\n"); }

} // namespace GameIpc
} // namespace okay
