#pragma once
// The client side of the session-ticket flow (docs/anticheat.md "Joining a server that needs a ticket"): before a client joins a server
// it asks the ticket service (supabase/functions/session-ticket) for a short-lived signed pass, and joins with that pass instead of
// the account's own token. No window, no engine link and no real network here: the HTTP call is an injected Transport, the clock and
// the sleeps are injected too, so tests/test_ticket_client.cpp runs every case (banned, 401, 5xx, timeouts, clock skew, expiry)
// without waiting.
//
// What it guarantees:
//   * a ticket is asked for before the join, never after; with Mode::Required there is NO join without one (Decide says deny);
//   * a banned answer is final (never retried, never cached) and calls onBanned so the launcher refreshes ac_my_bans;
//   * 401 renews the sign-in once and retries; 429, 5xx, timeouts and "not reached" retry with growing waits inside a time budget;
//   * the service's clock (reply "iat") is compared with ours, so an expiry is judged on the service's time: a player whose clock is
//     minutes off still uses a good ticket, and a ticket that is already (nearly) expired is not used;
//   * a ticket bound to one connection (cnf) is never cached or reused; an unbound one is reused until shortly before it expires.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <thread>

#include "AntiCheatBackend.hpp"

namespace okayac {
namespace ticket {

// ---- Transport ----------------------------------------------------------------------------------------------------------------
struct Reply {
    bool reached = false;   // false: no answer at all (offline, DNS, TLS, timeout)
    long status = 0;
    std::string body;
};
using Transport = std::function<Reply(const backend::Request&)>;

inline constexpr const char* kConfigName = "okay_tickets.cfg";   // next to the game (and the launcher): see Config

enum class Mode { Off, Optional, Required };
inline const char* ModeName(Mode m) { return m == Mode::Required ? "required" : m == Mode::Optional ? "optional" : "off"; }
inline bool ParseMode(const std::string& text, Mode& out) {
    const std::string t = og::Lower(og::Trim(text));
    if (t == "off" || t == "0" || t == "false" || t == "no") { out = Mode::Off; return true; }
    if (t == "optional" || t == "try") { out = Mode::Optional; return true; }
    if (t == "required" || t == "1" || t == "true" || t == "yes") { out = Mode::Required; return true; }
    return false;
}

enum class Outcome {
    Ok,             // `ticket` is good to join with
    Banned,         // the account or machine has an anti-cheat ban (final)
    SignedOut,      // not signed in, or the sign-in could not be renewed
    Refused,        // the service said no to this request (bad server id, unusable ticket): retrying the same thing will not help
    NotConfigured,  // the ticket service is not deployed (404) or no service is configured
    Unavailable,    // could not get an answer after the retries (offline, timeouts, 5xx)
};

struct Result {
    Outcome outcome = Outcome::Unavailable;
    std::string ticket;
    long exp = 0;        // when it expires, on the service's clock
    long expLocal = 0;   // the same moment on this machine's clock (what the cache goes by)
    long skew = 0;       // service clock minus this machine's clock when the ticket arrived (0 when the service didn't say)
    int attempts = 0;
    bool signInRenewed = false;
    bool fromCache = false;
    std::string message; // for the player, plain words
};

struct Options {
    int maxAttempts = 3;               // requests per Acquire (a renewed sign-in retry is free)
    double deadlineSeconds = 12.0;     // total time budget (waits included); the transport has its own per-request limit
    double backoffSeconds = 0.4;       // first wait, doubled each retry
    double backoffMaxSeconds = 3.0;
    long minUsefulSeconds = 10;        // a ticket with less than this left is not used, and a cached one is dropped
    long skewWarnSeconds = 120;        // tell the player when their clock is further off than this
    std::function<long()> now;                    // unix seconds
    std::function<double()> mono;                 // monotonic seconds, for the time budget
    std::function<void(double)> sleep;            // wait this many seconds
    std::function<bool()> refreshSignIn;          // after a 401: renew the access token; true = a new one is there
};

namespace detail {
inline long Now(const Options& o) { return o.now ? o.now() : (long)std::time(nullptr); }
inline double Mono(const Options& o) {
    if (o.mono) return o.mono();
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline void Sleep(const Options& o, double s) {
    if (s <= 0) return;
    if (o.sleep) o.sleep(s);
    else std::this_thread::sleep_for(std::chrono::duration<double>(s));
}
inline bool Retryable(const Reply& r) { return !r.reached || r.status == 408 || r.status == 429 || r.status >= 500; }
}  // namespace detail

class Client {
public:
    Client(Transport transport, Options options) : send_(std::move(transport)), opt_(std::move(options)) {}

    /// Get a ticket for server `aud`. `cnf` = NetworkManager::TicketBinding() ("" = unbound; only an unbound ticket is cached).
    Result Acquire(const std::string& aud, const std::string& hwHash, const std::string& cnf) {
        Result res;
        backend::Request req;
        std::string err;
        if (!backend::SessionTicket(aud, hwHash, cnf, req, &err)) {
            res.outcome = Outcome::Refused;
            res.message = err;
            return res;
        }
        const long now0 = detail::Now(opt_);
        if (cnf.empty()) {
            const auto it = cache_.find(aud);
            if (it != cache_.end()) {
                if (it->second.expLocal - now0 >= opt_.minUsefulSeconds) {
                    Result c = it->second;
                    c.fromCache = true;
                    c.attempts = 0;
                    return c;
                }
                cache_.erase(it);   // too close to its end: ask for a new one
            }
        }
        if (!send_) { res.outcome = Outcome::NotConfigured; res.message = "No ticket service is configured."; return res; }
        const double t0 = detail::Mono(opt_);
        double wait = opt_.backoffSeconds;
        bool renewed = false;
        std::string last = "The ticket service didn't answer.";
        while (res.attempts < std::max(1, opt_.maxAttempts)) {
            const Reply r = send_(req);
            ++res.attempts;
            const long now = detail::Now(opt_);
            if (r.reached && r.status == 200) {
                backend::TicketReply tr;
                if (!backend::ParseTicket(r.status, r.body, tr) || tr.exp <= 0) {
                    res.outcome = Outcome::Refused;
                    res.message = "The ticket service sent an answer this version can't read. Update the game.";
                    return res;
                }
                res.ticket = tr.ticket;
                res.exp = tr.exp;
                res.skew = tr.iat > 0 ? tr.iat - now : 0;
                res.expLocal = tr.exp - res.skew;
                res.signInRenewed = renewed;
                if (res.expLocal - now < opt_.minUsefulSeconds) {   // already (nearly) over: not worth joining with
                    res.outcome = Outcome::Refused;
                    res.ticket.clear();
                    res.message = tr.iat > 0 ? "The ticket service issued a ticket that is already expiring. Try again."
                                             : "Your computer's clock is wrong, so the session ticket looks expired. Set the date and time, then try again.";
                    return res;
                }
                res.outcome = Outcome::Ok;
                if (std::labs(res.skew) > opt_.skewWarnSeconds)
                    res.message = "Your computer's clock is " + std::to_string(std::labs(res.skew)) + " seconds " + (res.skew > 0 ? "behind" : "ahead") +
                                  "; the ticket is still good, but set the date and time.";
                if (cnf.empty()) cache_[aud] = res;
                return res;
            }
            if (r.reached && backend::IsBannedReply(r.status, r.body)) {
                res.outcome = Outcome::Banned;
                res.message = backend::ErrorText(r.status, r.body);
                cache_.erase(aud);
                return res;
            }
            if (r.reached && r.status == 401) {
                if (!renewed && opt_.refreshSignIn) {
                    renewed = true;
                    if (opt_.refreshSignIn()) { --res.attempts; continue; }   // a new access token: ask again at once, not counted
                }
                res.outcome = Outcome::SignedOut;
                res.message = "Sign in again to join: your session ended.";
                return res;
            }
            if (r.reached && r.status == 404) {
                res.outcome = Outcome::NotConfigured;
                res.message = "The ticket service isn't set up on this server.";
                return res;
            }
            if (!detail::Retryable(r)) {
                res.outcome = Outcome::Refused;
                res.message = backend::ErrorText(r.status, r.body);
                return res;
            }
            last = r.reached ? "The ticket service is having trouble (HTTP " + std::to_string(r.status) + ")." : "Couldn't reach the ticket service.";
            if (res.attempts >= std::max(1, opt_.maxAttempts)) break;
            const double spent = detail::Mono(opt_) - t0;
            if (spent + wait > opt_.deadlineSeconds) break;   // no room for another wait and try
            detail::Sleep(opt_, wait);
            wait = std::min(wait * 2.0, opt_.backoffMaxSeconds);
        }
        res.outcome = Outcome::Unavailable;
        res.message = last;
        return res;
    }

    /// Drop every cached ticket (sign-out, a ban answer elsewhere).
    void Forget() { cache_.clear(); }
    std::size_t Cached() const { return cache_.size(); }

private:
    Transport send_;
    Options opt_;
    std::map<std::string, Result> cache_;
};

// ---- What a join needs to decide -----------------------------------------------------------------------------------------------
struct JoinTarget {
    std::string host;
    std::uint16_t port = 0;
    bool relayed = false;
    std::string relayCode;
    std::string Where() const { return relayed ? "relay:" + relayCode : host + ":" + std::to_string(port); }
};

/// Client-side ticket settings, one file next to the game / launcher (okay_tickets.cfg), `key value` per line, # comments:
///   mode required            off (default: nothing changes) | optional (try; join without if the service is down) | required (no ticket, no join)
///   audience arena-1         the server id to ask for, for every server without a line of its own
///   server 203.0.113.7:45000 arena-1      this server's id ("off" = this one needs no ticket)
struct Config {
    Mode mode = Mode::Off;
    std::string audience;
    std::map<std::string, std::string> servers;   // "host:port" (lower case) -> id | "off"
    bool badLine = false;                          // a line that was not understood (the rest still applies; Required stays safe)
};

inline Config ParseConfig(const std::string& text) {
    Config c;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        line = og::Trim(line);
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string key, a, b, extra;
        ls >> key >> a >> b >> extra;
        key = og::Lower(key);
        if (!extra.empty()) { c.badLine = true; if (key == "mode") c.mode = Mode::Required; continue; }   // trailing words: not understood
        if (key == "mode") { Mode m; if (b.empty() && ParseMode(a, m)) c.mode = m; else { c.badLine = true; c.mode = Mode::Required; } }   // a typo must not turn the gate off
        else if (key == "audience" && b.empty() && backend::IsTicketAudience(a)) c.audience = a;
        else if (key == "server" && !a.empty() && (b == "off" || backend::IsTicketAudience(b))) c.servers[og::Lower(a)] = b;
        else c.badLine = true;
    }
    return c;
}
inline Config LoadConfig(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return Config();
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string t = ss.str();
    return t.size() > 65536 ? Config() : ParseConfig(t);
}

/// The server id to ask for: the server's own line, else the config's audience, else the address itself when it is a valid id.
/// "" = none known; "off" = this server is exempt.
inline std::string AudienceFor(const Config& c, const JoinTarget& t) {
    const auto it = c.servers.find(og::Lower(t.Where()));
    if (it != c.servers.end()) return it->second;
    if (!c.audience.empty()) return c.audience;
    const std::string w = t.Where();
    return backend::IsTicketAudience(w) ? w : std::string();
}

struct Decision {
    bool allow = false;
    bool useTicket = false;   // join with `ticket` as the auth token
    std::string ticket;
    Outcome outcome = Outcome::Ok;
    std::string message;      // why a join was refused, or a warning to show when it goes ahead
};

struct Hooks {
    std::function<std::string()> hwHash;   // this machine's hash ("" = none)
    std::function<void()> onBanned;        // refresh ac_my_bans / show the ban page
    std::function<bool()> signedIn;        // false = no account to ask a ticket for (unset = assume signed in)
};

/// The one decision: may this join go ahead, and with what token? `cnf` = the connection's key binding ("" = unbound).
inline Decision Decide(const Config& cfg, Client& client, const Hooks& hooks, const JoinTarget& target, const std::string& cnf) {
    Decision d;
    if (cfg.mode == Mode::Off) { d.allow = true; return d; }
    const std::string aud = AudienceFor(cfg, target);
    if (aud == "off") { d.allow = true; return d; }
    if (aud.empty()) {
        if (cfg.mode == Mode::Required) { d.message = "This game needs a session ticket but doesn't know this server's id (set `audience` in okay_tickets.cfg)."; return d; }
        d.allow = true;
        return d;
    }
    if (hooks.signedIn && !hooks.signedIn()) {
        d.outcome = Outcome::SignedOut;
        d.allow = cfg.mode == Mode::Optional;
        if (!d.allow) d.message = "Sign in to join this server.";
        return d;
    }
    const Result r = client.Acquire(aud, hooks.hwHash ? hooks.hwHash() : std::string(), cnf);
    d.outcome = r.outcome;
    d.message = r.message;
    if (r.outcome == Outcome::Ok) { d.allow = d.useTicket = true; d.ticket = r.ticket; return d; }
    if (r.outcome == Outcome::Banned) {
        if (hooks.onBanned) hooks.onBanned();
        return d;   // a ban is never "optional"
    }
    if (cfg.mode == Mode::Optional) { d.allow = true; return d; }
    if (d.message.empty()) d.message = "Couldn't get a session ticket.";
    return d;   // Required: no ticket, no join
}

}  // namespace ticket
}  // namespace okayac
