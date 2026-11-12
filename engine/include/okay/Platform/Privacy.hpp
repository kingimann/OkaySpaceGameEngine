#pragma once
// Server-side blocks and enforced privacy (docs/sql/blocks.sql): the pure,
// unit-testable half. Nothing here talks to the network except ProfilesGet.
//
//  * IsMissingTable: "the SQL file was not run" vs "refused" vs "offline".
//  * Rule parsing: the database refuses with an error named "okay_rule:<why>";
//    RuleText turns that into the plain sentence the UI shows.
//  * Settings JSON for the privacy_settings table.
//  * Pending block/unblock changes (kept while offline) and how they are
//    layered on top of the server's list.
//  * ProfilesGet: reads profiles through the profiles_visible view (the only way
//    to read presence / private fields once blocks.sql ran), falling back to the
//    plain table when the view does not exist yet.
#include "okay/Platform/Account/AccountService.hpp"
#include <atomic>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace okay {
namespace privacy {

// 404 (PostgREST PGRST205 "not in the schema cache"), or a 400 / 42P01 naming a
// missing relation. 401/403/5xx and "never reached the server" are NOT missing:
// an RLS refusal or an outage must not be reported as an un-run migration. A
// missing COLUMN (42703) is not a missing table either.
inline bool IsMissingTable(long status, const std::string& body) {
    if (status == 404) return true;
    if (body.find("PGRST205") != std::string::npos || body.find("42P01") != std::string::npos) return status == 400 || status == 404;
    if (status == 400 && body.find("relation") != std::string::npos && body.find("does not exist") != std::string::npos) return true;
    return false;
}

enum class Support { Unknown, Yes, Missing };

inline const char* SetupNote() {
    return "Not set up on the server yet: the server owner must run docs/sql/blocks.sql once.";
}

// ---- Rules the database enforces ------------------------------------------
enum class Rule { None, YouBlocked, DmRefused, DmFriendsOnly, CallsFriendsOnly, CallsRefused, FriendRequestsOff, Unknown };

// Finds "okay_rule:<reason>" anywhere in `text` (a response body or an error).
inline Rule ParseRule(const std::string& text) {
    static const char kTag[] = "okay_rule:";
    const std::size_t p = text.find(kTag);
    if (p == std::string::npos) return Rule::None;
    const std::string r = text.substr(p + sizeof(kTag) - 1, 24);
    auto is = [&](const char* w) { return r.compare(0, std::string(w).size(), w) == 0 &&
                    (r.size() == std::string(w).size() || !(std::isalnum((unsigned char)r[std::string(w).size()]) || r[std::string(w).size()] == '_')); };
    if (is("you_blocked")) return Rule::YouBlocked;
    if (is("dm_nobody")) return Rule::DmRefused;
    if (is("dm_friends")) return Rule::DmFriendsOnly;
    if (is("calls_friends")) return Rule::CallsFriendsOnly;
    if (is("calls_nobody")) return Rule::CallsRefused;
    if (is("friend_requests_off")) return Rule::FriendRequestsOff;
    return Rule::Unknown;
}
inline const char* RuleText(Rule r) {
    switch (r) {
        case Rule::YouBlocked:        return "You blocked this person. Unblock them first.";
        case Rule::DmRefused:         return "This person does not accept messages from you.";
        case Rule::DmFriendsOnly:     return "This person only accepts messages from friends.";
        case Rule::CallsFriendsOnly:  return "This person only accepts calls from friends.";
        case Rule::CallsRefused:      return "This person does not accept calls from you.";
        case Rule::FriendRequestsOff: return "This person is not accepting friend requests from you.";
        case Rule::Unknown:           return "The server refused this because of a privacy rule.";
        default:                      return "";
    }
}
// True when `text` is exactly one of the RuleText sentences (the Friends API hands
// those back as its error string).
inline bool IsRuleText(const std::string& text) {
    for (Rule r : {Rule::YouBlocked, Rule::DmRefused, Rule::DmFriendsOnly, Rule::CallsFriendsOnly, Rule::CallsRefused,
                   Rule::FriendRequestsOff, Rule::Unknown})
        if (text == RuleText(r)) return true;
    return false;
}
// The sentence to show when a call failed: the rule's text if a server rule said
// no, else `fallback`.
inline std::string FailureText(const okay::account::ApiResponse& r, const std::string& fallback) {
    const Rule rule = ParseRule(r.body);
    if (rule != Rule::None) return RuleText(rule);
    if (!r.reached) return fallback.empty() ? std::string("No connection to the server.") : "No connection to the server. " + fallback;
    return fallback;
}

// ---- Settings -----------------------------------------------------------------
// 0 everyone, 1 friends, 2 nobody (friendRequests: 0 everyone or 2 nobody).
struct Settings {
    int dmFrom = 0, profileVis = 0, onlineVis = 0, friendRequests = 0, callsFrom = 1;
    bool operator==(const Settings& o) const {
        return dmFrom == o.dmFrom && profileVis == o.profileVis && onlineVis == o.onlineVis &&
               friendRequests == o.friendRequests && callsFrom == o.callsFrom;
    }
    bool operator!=(const Settings& o) const { return !(*this == o); }
};
inline const char* ReachWord(int v) { return v == 1 ? "friends" : v == 2 ? "nobody" : "everyone"; }
inline int ReachFromWord(const std::string& w, int def) {
    if (w == "everyone") return 0;
    if (w == "friends") return 1;
    if (w == "nobody") return 2;
    return def;
}
inline std::string SettingsJson(const Settings& s) {
    return std::string("{\"dm_from\":\"") + ReachWord(s.dmFrom) + "\",\"profile_visibility\":\"" + ReachWord(s.profileVis) +
           "\",\"online_visibility\":\"" + ReachWord(s.onlineVis) + "\",\"friend_requests\":\"" +
           (s.friendRequests == 2 ? "nobody" : "everyone") + "\",\"calls_from\":\"" + ReachWord(s.callsFrom) + "\"}";
}
// First object of a PostgREST array. false when `body` has no row.
inline bool SettingsFromBody(const std::string& body, Settings& out) {
    auto word = [&](const char* key, int def) {
        const std::string k = std::string("\"") + key + "\"";
        std::size_t p = body.find(k); if (p == std::string::npos) return def;
        p = body.find(':', p + k.size()); if (p == std::string::npos) return def;
        p = body.find('"', p); if (p == std::string::npos) return def;
        const std::size_t e = body.find('"', p + 1); if (e == std::string::npos) return def;
        return ReachFromWord(body.substr(p + 1, e - p - 1), def);
    };
    if (body.find("\"dm_from\"") == std::string::npos) return false;
    Settings s;
    s.dmFrom = word("dm_from", 0); s.profileVis = word("profile_visibility", 0); s.onlineVis = word("online_visibility", 0);
    s.friendRequests = word("friend_requests", 0) == 2 ? 2 : 0; s.callsFrom = word("calls_from", 1);
    out = s;
    return true;
}

// ---- Block list changes kept while offline -----------------------------------
struct PendingOp { std::string uid, name; bool block = true; };
inline bool operator==(const PendingOp& a, const PendingOp& b) { return a.uid == b.uid && a.name == b.name && a.block == b.block; }

// Record "block uid" / "unblock uid". A newer change for the same person
// replaces the older one (block then unblock = just unblock).
inline void QueueOp(std::vector<PendingOp>& ops, const std::string& uid, const std::string& name, bool block) {
    for (auto it = ops.begin(); it != ops.end(); ++it) if (it->uid == uid) { ops.erase(it); break; }
    ops.push_back({uid, name, block});
}
// The list to show: the server's list with unsent changes applied on top.
inline std::set<std::string> ApplyPending(std::set<std::string> server, const std::vector<PendingOp>& ops) {
    for (const auto& o : ops) { if (o.block) server.insert(o.uid); else server.erase(o.uid); }
    return server;
}
// launcher.cfg encoding: "+uid|name" / "-uid|name" (name may be empty).
inline std::string EncodeOp(const PendingOp& o) { return std::string(o.block ? "+" : "-") + o.uid + "|" + o.name; }
inline bool DecodeOp(const std::string& s, PendingOp& o) {
    if (s.size() < 2 || (s[0] != '+' && s[0] != '-')) return false;
    const std::size_t bar = s.find('|');
    o.block = s[0] == '+';
    o.uid = s.substr(1, bar == std::string::npos ? std::string::npos : bar - 1);
    o.name = bar == std::string::npos ? std::string() : s.substr(bar + 1);
    return !o.uid.empty();
}

// How the one-time migration of the old PC-only list works. `marker` is the
// username the local list was last synced for ("" = never synced: it is the
// legacy list and should be uploaded once). If it names a DIFFERENT account the
// local list belongs to someone else and must not be uploaded.
enum class LocalListAction { UploadAsLegacy, Keep, DiscardOtherAccount };
inline LocalListAction ClassifyLocalList(const std::string& marker, const std::string& username) {
    if (marker.empty()) return LocalListAction::UploadAsLegacy;
    if (marker == username) return LocalListAction::Keep;
    return LocalListAction::DiscardOtherAccount;
}

// ---- Reading profiles ---------------------------------------------------------
inline std::atomic<int>& ProfilesViewState() { static std::atomic<int> s{0}; return s; }   // 0 unknown, 1 view works, 2 view missing

// GET /rest/v1/profiles?<query> but through profiles_visible when it exists (so
// blocked people and visibility settings are honoured), else the plain table.
inline okay::account::ApiResponse ProfilesGet(const okay::account::AccountService& acct, const std::string& query) {
    if (ProfilesViewState().load() != 2) {
        okay::account::ApiResponse r = acct.Api("/rest/v1/profiles_visible?" + query, "GET");
        if (r.ok) { ProfilesViewState() = 1; return r; }
        if (!(r.reached && IsMissingTable(r.status, r.body))) return r;
        ProfilesViewState() = 2;
    }
    return acct.Api("/rest/v1/profiles?" + query, "GET");
}

} // namespace privacy
} // namespace okay
