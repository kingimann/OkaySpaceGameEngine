#pragma once
// Kick / ban / unban / promote for community servers and group chats (launcher).
// Pure logic, header-only, no network of its own: who may act on whom, the exact
// PostgREST requests (they call the RPCs in docs/sql/member_moderation.sql) and
// how a server answer becomes honest feedback. The database enforces every rule
// again; these checks only decide which buttons are worth showing and give a
// clear message instead of a bare 4xx.
//
// Roles: owner (3) > admin (2) > member (1). Nobody may act on an equal or
// higher role, nobody on themselves, and an owner can never be removed.
#include "okay/Platform/Servers.hpp"
#include <string>
#include <vector>
#include <cstdio>

namespace okay { namespace modmember {

enum class Role { None = 0, Member = 1, Admin = 2, Owner = 3 };
enum class Scope { Server, Group };

inline int Rank(Role r) { return (int)r; }
inline Role ParseRole(const std::string& s) {
    if (s == "owner") return Role::Owner;
    if (s == "admin") return Role::Admin;
    return Role::Member;   // a member row with no / unknown role is a plain member
}
inline const char* RoleName(Role r) {
    switch (r) { case Role::Owner: return "owner"; case Role::Admin: return "admin"; case Role::Member: return "member"; default: return "none"; }
}
inline const char* ScopeName(Scope s) { return s == Scope::Server ? "server" : "group"; }
inline const char* ScopeNoun(Scope s) { return s == Scope::Server ? "server" : "group"; }

// Kick and ban need admin or owner, strictly above the target, never yourself.
inline bool CanModerate(Role actor, Role target, bool targetIsSelf) {
    return !targetIsSelf && Rank(actor) >= Rank(Role::Admin) && Rank(actor) > Rank(target);
}
// Only the owner changes roles, and only between member and admin.
inline bool CanSetRole(Role actor, Role target, bool targetIsSelf) {
    return !targetIsSelf && actor == Role::Owner && (target == Role::Member || target == Role::Admin);
}
// Admins and the owner see the banned list and may lift bans.
inline bool CanSeeBans(Role actor) { return Rank(actor) >= Rank(Role::Admin); }
inline bool CanUnban(Role actor)   { return CanSeeBans(actor); }

// Why a button is absent / refused, in plain words ("" when allowed).
inline std::string DenyReason(Role actor, Role target, bool targetIsSelf) {
    if (targetIsSelf) return "You can't do this to yourself.";
    if (target == Role::Owner) return "The owner can't be removed or banned.";
    if (Rank(actor) < Rank(Role::Admin)) return "Only the owner or an admin can do this.";
    if (Rank(actor) <= Rank(target)) return "You can only act on members below your role.";
    return std::string();
}

// ---- Requests --------------------------------------------------------------
struct Request { std::string method, path, body; bool valid() const { return !path.empty(); } };

inline bool LooksLikeUuid(const std::string& u) {
    if (u.size() != 36) return false;
    for (std::size_t i = 0; i < u.size(); ++i) {
        const char c = u[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; }
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}
// Trim, drop control characters, cap at 300 bytes without cutting a UTF-8 sequence.
inline std::string CleanReason(const std::string& in) {
    std::string s;
    for (char c : in) if ((unsigned char)c >= 0x20 || c == '\t') s += (c == '\t' ? ' ' : c);
    std::size_t a = 0, b = s.size();
    while (a < b && s[a] == ' ') ++a;
    while (b > a && s[b - 1] == ' ') --b;
    s = s.substr(a, b - a);
    if (s.size() > 300) {
        std::size_t n = 300;
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) --n;
        s.resize(n);
    }
    return s;
}

namespace detail {
inline std::string Common(Scope sc, long cid, const std::string& uid) {
    return "{\"p_scope\":\"" + std::string(ScopeName(sc)) + "\",\"p_container\":" + std::to_string(cid) +
           ",\"p_target\":" + okay::Servers::JStr(uid);
}
inline Request Rpc(const char* fn, const std::string& body) { return Request{"POST", std::string("/rest/v1/rpc/") + fn, body}; }
}

inline Request KickRequest(Scope sc, long cid, const std::string& uid) {
    if (cid <= 0 || !LooksLikeUuid(uid)) return {};
    return detail::Rpc("kick_member", detail::Common(sc, cid, uid) + "}");
}
// seconds <= 0 means permanent. The server computes the end time (no client clock).
inline Request BanRequest(Scope sc, long cid, const std::string& uid, const std::string& reason, long seconds) {
    if (cid <= 0 || !LooksLikeUuid(uid)) return {};
    return detail::Rpc("ban_member", detail::Common(sc, cid, uid) + ",\"p_reason\":" + okay::Servers::JStr(CleanReason(reason)) +
                       ",\"p_seconds\":" + (seconds > 0 ? std::to_string(seconds) : std::string("null")) + "}");
}
inline Request UnbanRequest(Scope sc, long cid, const std::string& uid) {
    if (cid <= 0 || !LooksLikeUuid(uid)) return {};
    return detail::Rpc("unban_member", detail::Common(sc, cid, uid) + "}");
}
inline Request SetRoleRequest(Scope sc, long cid, const std::string& uid, Role newRole) {
    if (cid <= 0 || !LooksLikeUuid(uid) || (newRole != Role::Admin && newRole != Role::Member)) return {};
    return detail::Rpc("set_member_role", detail::Common(sc, cid, uid) + ",\"p_role\":\"" + RoleName(newRole) + "\"}");
}
inline Request BansListRequest(Scope sc, long cid) {
    if (cid <= 0) return {};
    return Request{"GET", "/rest/v1/community_bans?scope=eq." + std::string(ScopeName(sc)) + "&container_id=eq." + std::to_string(cid) +
                          "&select=user_id,reason,until,created_at&order=created_at.desc", ""};
}

// ---- Answers ---------------------------------------------------------------
enum class Status { Accepted, Rejected, NotApplied, Offline, SignedOut, Invalid };
struct Outcome { Status status = Status::Invalid; std::string message; bool ok() const { return status == Status::Accepted; } };

inline bool Contains(const std::string& h, const char* n) { return h.find(n) != std::string::npos; }

// PostgREST error body: {"code":"...","message":"...","details":...}.
inline std::string ErrorMessage(const std::string& body) { return okay::Servers::Field(body, "message"); }

// What an answer means. `ok`/`reached`/`status`/`body` are okay::account::ApiResponse's fields.
inline Outcome MapResponse(bool ok, bool reached, long status, const std::string& body, const std::string& what = "Action") {
    Outcome o;
    if (ok) { o.status = Status::Accepted; o.message = what + " done."; return o; }
    if (!reached) { o.status = Status::Offline; o.message = "Couldn't reach the server. Nothing was changed."; return o; }
    const std::string code = okay::Servers::Field(body, "code");
    const std::string msg = ErrorMessage(body);
    if (status == 401 || Contains(body, "JWT")) { o.status = Status::SignedOut; o.message = "You're signed out. Sign in again and retry."; return o; }
    // The function or table is not in the database: the owner has not applied member_moderation.sql.
    if (code == "PGRST202" || code == "PGRST205" || code == "42883" || code == "42P01" ||
        Contains(body, "Could not find the function") || Contains(body, "Could not find the table") ||
        (status == 404 && msg.empty())) {
        o.status = Status::NotApplied;
        o.message = "Moderation isn't set up on this project yet: docs/sql/member_moderation.sql has not been applied.";
        return o;
    }
    o.status = Status::Rejected;
    if (code == "42501" || status == 403 || Contains(msg, "row-level security"))
        o.message = "Refused by the server: you don't have permission for this.";
    else if (!msg.empty()) o.message = "Refused by the server: " + msg;
    else o.message = "Refused by the server (HTTP " + std::to_string(status) + ").";
    return o;
}

// api(path, method, body) -> something with ok / reached / status / body (AccountService::Api).
template <class ApiFn>
Outcome Execute(ApiFn&& api, const Request& req, const std::string& what = "Action") {
    if (!req.valid()) { Outcome o; o.status = Status::Invalid; o.message = "That request isn't valid."; return o; }
    auto r = api(req.path, req.method, req.body);
    return MapResponse(r.ok, r.reached, r.status, r.body, what);
}

// ---- Banned list -------------------------------------------------------------
struct BanEntry { std::string uid, name, reason; long until = 0; long since = 0; };   // until 0 = permanent

inline long ParseIsoUtc(const std::string& ls) {
    if (ls.size() < 19) return 0;
    std::tm tm{};
    if (std::sscanf(ls.c_str(), "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) return 0;
    tm.tm_year -= 1900; tm.tm_mon -= 1;
#if defined(_WIN32)
    return (long)_mkgmtime(&tm);
#else
    return (long)timegm(&tm);
#endif
}
// Active bans only: a ban whose end time has passed is hidden (the database ignores it too).
inline std::vector<BanEntry> ParseBans(const std::string& body, long now) {
    std::vector<BanEntry> out;
    for (const auto& o : okay::Servers::SplitObjects(body)) {
        BanEntry b; b.uid = okay::Servers::Field(o, "user_id");
        if (b.uid.empty()) continue;
        b.reason = okay::Servers::Field(o, "reason");
        b.until = ParseIsoUtc(okay::Servers::Field(o, "until"));
        b.since = ParseIsoUtc(okay::Servers::Field(o, "created_at"));
        if (b.until != 0 && b.until <= now) continue;
        out.push_back(b);
    }
    return out;
}

// Ban length choices for the dialog (seconds; 0 = permanent).
struct Duration { const char* label; long seconds; };
inline const std::vector<Duration>& Durations() {
    static const std::vector<Duration> d = {{"1 hour", 3600}, {"1 day", 86400}, {"7 days", 7 * 86400}, {"30 days", 30 * 86400}, {"Permanent", 0}};
    return d;
}

}} // namespace okay::modmember
