#pragma once
// Hooks the session-ticket flow (shared/TicketClient.hpp) into a game or the launcher: the account's HTTP call as the Transport and a
// NetworkManager join gate. Include this where okay::NetworkManager and okay::Account are linked (the player, the launcher, tests).
#include "TicketClient.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Platform/Account/Account.hpp"

#include <memory>

namespace okayac {
namespace ticket {

/// Asks the ticket service through the signed-in account (the session's access token goes as the bearer; the service checks it and the
/// account's bans). `timeoutSec` bounds each request so a stalled service can't hang a join.
inline Transport AccountTransport(int timeoutSec = 8) {
    return [timeoutSec](const backend::Request& r) {
        const okay::account::ApiResponse a = okay::Account::ApiTimed(r.path, r.method, r.body, r.header, timeoutSec);
        Reply out;
        out.reached = a.reached;
        out.status = a.status;
        out.body = a.body;
        return out;
    };
}

inline bool LooksLikeTicket(const std::string& token) { return token.rfind("okt1.", 0) == 0; }

/// What a NetworkManager presents when no ticket is used: the account's own token, never a ticket left over from an earlier
/// (bound, now useless) connection.
inline void RestoreAccountToken(okay::NetworkManager& nm) {
    if (LooksLikeTicket(nm.AuthToken())) nm.SetAuthToken(okay::Account::IsLoggedIn() ? okay::Account::Token() : std::string());
}

/// The gate: for every client join, ask for a ticket bound to this connection (TicketBinding) and present it as the auth token;
/// a refusal stops the join before anything is sent. `transport` / `options` are for tests; the defaults use the account.
inline void InstallJoinGate(const Config& cfg, Hooks hooks, Transport transport = {}, Options options = {}) {
    if (!transport) transport = AccountTransport();
    if (!hooks.signedIn) hooks.signedIn = [] { return okay::Account::IsLoggedIn(); };
    auto client = std::make_shared<Client>(std::move(transport), std::move(options));
    okay::NetworkManager::SetJoinGate([cfg, hooks, client](okay::NetworkManager& nm, const std::string& host, std::uint16_t port,
                                                           bool relayed, const std::string& code, std::string& why) {
        if (cfg.mode == Mode::Off) { RestoreAccountToken(nm); return true; }
        JoinTarget t;
        t.host = host; t.port = port; t.relayed = relayed; t.relayCode = code;
        const std::string cnf = nm.TicketBinding();   // "" without encryption: the ticket is then unbound
        const Decision d = Decide(cfg, *client, hooks, t, cnf);
        if (!d.allow) { why = d.message; return false; }
        if (d.useTicket) nm.SetAuthToken(d.ticket);
        else RestoreAccountToken(nm);
        return true;
    });
}

}  // namespace ticket
}  // namespace okayac
