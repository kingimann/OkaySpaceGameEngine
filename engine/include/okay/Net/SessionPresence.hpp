#pragma once
// ---------------------------------------------------------------------------
// Rich presence for a running game: turn a NetworkManager's live session into
// what friends see in the launcher ("Hosting a match · 3/8") and the join link
// they use to get in. The player writes it to the launcher through GameIpc.
// ---------------------------------------------------------------------------
#include "okay/Net/JoinLink.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/ServerQuery.hpp"
#include "okay/Platform/GameIpc.hpp"

#include <string>

namespace okay {

/// Describe `nm`'s session (nullptr / offline = "In the menus"). `lanAddr` is
/// this machine's LAN address (LocalNetworkAddress()), used for a game hosted
/// directly. `inviteLink`, if given, receives the link with the password in it
/// (for private invites; the public link only says one is needed) — also in
/// the returned presence's `invite`.
inline GamePresence DescribeSession(const NetworkManager* nm, const std::string& title,
                                    const std::string& lanAddr, std::string* inviteLink = nullptr) {
    GamePresence g;
    g.title = title;
    g.status = "In the menus";
    if (inviteLink) inviteLink->clear();
    if (!nm || nm->mode() == NetworkManager::Mode::Offline) return g;

    JoinLink l;
    l.game = title;
    l.room = nm->Room();
    if (nm->IsServer()) {
        g.hosting = true;
        g.players = nm->PlayerCount();
        g.maxPlayers = nm->maxPlayers + (nm->dedicated ? 0 : 1);
        l.name = nm->serverName;
        if (nm->Relayed()) {
            if (!nm->RelayReady()) { g.status = "Starting a match"; return g; }
            l.relay = nm->RelayAddress(); l.code = nm->RelayCode();
        } else {
            l.addr = (lanAddr.empty() ? std::string("127.0.0.1") : lanAddr) + ":" + std::to_string(nm->ServerPort());
        }
        g.status = nm->serverName.empty() ? "Hosting a match" : "Hosting " + nm->serverName;
    } else {
        if (!nm->Joined()) { g.status = "Joining a match"; return g; }
        g.players = nm->PlayerCount();
        l.name = nm->ServerName();
        if (nm->Relayed()) { l.relay = nm->RelayAddress(); l.code = nm->RelayCode(); }
        else {
            l.addr = nm->ServerAddress();
            // A server on this machine: friends reach it at our LAN address.
            if (l.addr.compare(0, 4, "127.") == 0 && !lanAddr.empty())
                l.addr = lanAddr + l.addr.substr(l.addr.find(':'));
        }
        g.status = nm->ServerName().empty() ? "In a match" : "Playing on " + nm->ServerName();
    }
    const std::string pass = nm->password;
    l.needsPassword = !pass.empty();
    g.join = l.Format();                         // public: never carries the password
    JoinLink inv = l; inv.password = pass;
    g.invite = inv.Format();
    if (inviteLink) *inviteLink = g.invite;
    return g;
}

} // namespace okay
