#pragma once
// ---------------------------------------------------------------------------
// Site administration over Supabase (docs/sql/admin.sql): roles (admin >
// staff > moderator, stored as profile badges), site-wide bans, the audit
// log and the dashboard numbers. Everything is enforced server-side by RLS;
// this class only issues the requests, so a non-moderator simply gets
// failures / empty lists.
// ---------------------------------------------------------------------------
#include "okay/Platform/Account/AccountService.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace okay {

struct AdminUser {
    std::string id, username, display, avatar;
    bool verified = false, online = false;
    long lastSeen = 0, joined = 0;
    std::vector<std::string> badges;       // admin, staff, moderator, developer, beta, partner
    bool banned = false;                   // an active site ban
    std::string banReason; long banUntil = 0;   // 0 = permanent
};
struct AdminBan { std::string userId, username, reason; long until = 0, when = 0; };
struct AdminLogEntry { std::string actor, action, target, detail; long when = 0; };
struct AdminStats {
    bool ok = false;
    long players = 0, newWeek = 0, onlineNow = 0, banned = 0, staff = 0, threadsWeek = 0, items = 0, pending = 0;
};
struct MyBanInfo { bool banned = false; std::string reason; long until = 0; };

class Admin {
public:
    explicit Admin(okay::account::AccountService& acct) : acct_(acct) {}

    /// 3 admin, 2 staff, 1 moderator, 0 player; -1 when admin.sql hasn't run.
    int MyRank() {
        auto r = acct_.Api("/rest/v1/rpc/my_role_rank", "POST", "{}");
        if (!r.ok) return -1;
        std::string t = r.body; t.erase(std::remove_if(t.begin(), t.end(), [](char c) { return !std::isdigit((unsigned char)c); }), t.end());
        return t.empty() ? 0 : std::atoi(t.c_str());
    }
    static int RankOf(const std::vector<std::string>& badges) {
        int r = 0;
        for (const auto& b : badges) r = std::max(r, b == "admin" ? 3 : b == "staff" ? 2 : b == "moderator" ? 1 : 0);
        return r;
    }
    static const char* RankName(int r) { return r >= 3 ? "Admin" : r == 2 ? "Staff" : r == 1 ? "Moderator" : "Player"; }

    AdminStats Stats() {
        AdminStats s;
        auto r = acct_.Api("/rest/v1/rpc/admin_stats", "POST", "{}");
        if (!r.ok) return s;
        auto o = SplitObjects(r.body);
        if (o.empty()) return s;
        auto n = [&](const char* k) { return std::atol(Field(o[0], k).c_str()); };
        s.ok = true;
        s.players = n("players"); s.newWeek = n("new_week"); s.onlineNow = n("online_now"); s.banned = n("banned");
        s.staff = n("staff"); s.threadsWeek = n("threads_week"); s.items = n("items"); s.pending = n("pending");
        return s;
    }

    /// Players whose username or display name contains `q` (empty = newest).
    std::vector<AdminUser> FindUsers(const std::string& q, int limit = 40) {
        std::string path = "/rest/v1/profiles?select=id,username,display_name,avatar_url,verified,last_seen,created_at&limit=" + std::to_string(limit);
        if (!q.empty()) path += "&or=(username.ilike.*" + Url(q) + "*,display_name.ilike.*" + Url(q) + "*)&order=username.asc";
        else path += "&order=created_at.desc";
        auto r = acct_.Api(path, "GET");
        std::vector<AdminUser> out;
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body)) {
            AdminUser u; u.id = Field(o, "id"); u.username = Field(o, "username");
            u.display = Field(o, "display_name"); if (u.display.empty()) u.display = u.username;
            u.avatar = Field(o, "avatar_url"); u.verified = Field(o, "verified") == "true";
            u.lastSeen = ParseIso(Field(o, "last_seen")); u.joined = ParseIso(Field(o, "created_at"));
            u.online = u.lastSeen > 0 && std::difftime(std::time(nullptr), (std::time_t)u.lastSeen) < 180.0;
            if (!u.id.empty()) out.push_back(u);
        }
        FillRolesAndBans(out);
        return out;
    }

    bool SetBadge(const std::string& uid, const std::string& badge, bool on) {
        if (on) return acct_.Api("/rest/v1/profile_badges", "POST", "{\"owner\":" + JStr(uid) + ",\"badge\":" + JStr(badge) + "}",
                                 "Prefer: resolution=ignore-duplicates").ok;
        return acct_.Api("/rest/v1/profile_badges?owner=eq." + Url(uid) + "&badge=eq." + Url(badge), "DELETE").ok;
    }
    bool SetVerified(const std::string& uid, bool on) {
        return acct_.Api("/rest/v1/profiles?id=eq." + Url(uid), "PATCH", on ? "{\"verified\":true}" : "{\"verified\":false}").ok;
    }
    /// Ban for `hours` (0 = permanent). Replaces an existing ban.
    bool Ban(const std::string& uid, const std::string& reason, long hours) {
        std::string until = "null";
        if (hours > 0) until = JStr(IsoAt((long)std::time(nullptr) + hours * 3600));
        return acct_.Api("/rest/v1/site_bans?on_conflict=user_id", "POST",
                         "{\"user_id\":" + JStr(uid) + ",\"reason\":" + JStr(reason) + ",\"until\":" + until + "}",
                         "Prefer: resolution=merge-duplicates").ok;
    }
    bool Unban(const std::string& uid) {
        return acct_.Api("/rest/v1/site_bans?user_id=eq." + Url(uid), "DELETE").ok;
    }
    std::vector<AdminBan> Bans() {
        std::vector<AdminBan> out;
        auto r = acct_.Api("/rest/v1/site_bans?select=user_id,reason,until,created_at&order=created_at.desc&limit=200", "GET");
        if (!r.ok) return out;
        std::vector<std::string> ids;
        const long now = (long)std::time(nullptr);
        for (const auto& o : SplitObjects(r.body)) {
            AdminBan b; b.userId = Field(o, "user_id"); b.reason = Field(o, "reason");
            b.until = ParseIso(Field(o, "until")); b.when = ParseIso(Field(o, "created_at"));
            if (b.until > 0 && b.until <= now) continue;   // expired
            out.push_back(b); ids.push_back(b.userId);
        }
        auto names = Names(ids);
        for (auto& b : out) for (const auto& n : names) if (n.first == b.userId) b.username = n.second;
        return out;
    }
    std::vector<AdminLogEntry> Log(int limit = 100) {
        std::vector<AdminLogEntry> out;
        auto r = acct_.Api("/rest/v1/admin_log?select=actor_name,action,target_name,detail,created_at&order=created_at.desc&limit=" +
                           std::to_string(limit), "GET");
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body)) {
            AdminLogEntry e; e.actor = Field(o, "actor_name"); e.action = Field(o, "action");
            e.target = Field(o, "target_name"); e.detail = Field(o, "detail"); e.when = ParseIso(Field(o, "created_at"));
            out.push_back(e);
        }
        return out;
    }
    /// Am I banned? (RLS lets a player read their own ban.)
    MyBanInfo MyBan() {
        MyBanInfo m;
        const std::string me = MyUid(); if (me.empty()) return m;
        auto r = acct_.Api("/rest/v1/site_bans?select=reason,until&user_id=eq." + Url(me), "GET");
        if (!r.ok) return m;
        auto o = SplitObjects(r.body);
        if (o.empty()) return m;
        m.until = ParseIso(Field(o[0], "until"));
        if (m.until > 0 && m.until <= (long)std::time(nullptr)) return m;   // expired
        m.banned = true; m.reason = Field(o[0], "reason");
        return m;
    }

private:
    okay::account::AccountService& acct_;
    std::string myUid_;

    void FillRolesAndBans(std::vector<AdminUser>& us) {
        if (us.empty()) return;
        std::string in;
        for (const auto& u : us) { if (!in.empty()) in += ","; in += Url(u.id); }
        auto rb = acct_.Api("/rest/v1/profile_badges?select=owner,badge&owner=in.(" + in + ")", "GET");
        if (rb.ok) for (const auto& o : SplitObjects(rb.body)) {
            const std::string id = Field(o, "owner"), b = Field(o, "badge");
            for (auto& u : us) if (u.id == id) u.badges.push_back(b);
        }
        auto rs = acct_.Api("/rest/v1/site_bans?select=user_id,reason,until&user_id=in.(" + in + ")", "GET");
        const long now = (long)std::time(nullptr);
        if (rs.ok) for (const auto& o : SplitObjects(rs.body)) {
            const std::string id = Field(o, "user_id");
            const long until = ParseIso(Field(o, "until"));
            if (until > 0 && until <= now) continue;
            for (auto& u : us) if (u.id == id) { u.banned = true; u.banReason = Field(o, "reason"); u.banUntil = until; }
        }
    }
    std::vector<std::pair<std::string, std::string>> Names(const std::vector<std::string>& ids) {
        std::vector<std::pair<std::string, std::string>> out;
        if (ids.empty()) return out;
        std::string in;
        for (const auto& id : ids) { if (!in.empty()) in += ","; in += Url(id); }
        auto r = acct_.Api("/rest/v1/profiles?select=id,username&id=in.(" + in + ")", "GET");
        if (r.ok) for (const auto& o : SplitObjects(r.body)) out.emplace_back(Field(o, "id"), Field(o, "username"));
        return out;
    }
    std::string MyUid() {
        if (myUid_.empty() && acct_.IsLoggedIn()) {
            auto r = acct_.Api("/rest/v1/profiles?select=id&username=eq." + Url(acct_.CurrentSession().username), "GET");
            if (r.ok) { auto o = SplitObjects(r.body); if (!o.empty()) myUid_ = Field(o[0], "id"); }
        }
        return myUid_;
    }

    static std::string Url(const std::string& v) {
        std::string o;
        for (unsigned char c : v) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
            else { char b[4]; std::snprintf(b, sizeof(b), "%%%02X", c); o += b; }
        }
        return o;
    }
    static std::string IsoAt(long t) {
        std::time_t tt = (std::time_t)t; std::tm g{};
#if defined(_WIN32)
        gmtime_s(&g, &tt);
#else
        gmtime_r(&tt, &g);
#endif
        char b[32]; std::strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", &g);
        return b;
    }
    static long ParseIso(const std::string& ls) {
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
    static std::vector<std::string> SplitObjects(const std::string& json) {
        std::vector<std::string> out; int depth = 0; std::size_t start = std::string::npos;
        bool inStr = false;
        for (std::size_t i = 0; i < json.size(); ++i) {
            char c = json[i];
            if (inStr) { if (c == '\\') ++i; else if (c == '"') inStr = false; continue; }
            if (c == '"') inStr = true;
            else if (c == '{') { if (depth == 0) start = i; ++depth; }
            else if (c == '}') { if (--depth == 0 && start != std::string::npos) { out.push_back(json.substr(start, i - start + 1)); start = std::string::npos; } }
        }
        return out;
    }
    static std::string Field(const std::string& obj, const std::string& key) {
        std::string k = "\"" + key + "\"";
        std::size_t p = obj.find(k); if (p == std::string::npos) return "";
        p = obj.find(':', p + k.size()); if (p == std::string::npos) return "";
        ++p; while (p < obj.size() && obj[p] == ' ') ++p;
        if (p >= obj.size()) return "";
        if (obj[p] == '"') {
            std::string v; ++p;
            while (p < obj.size() && obj[p] != '"') {
                if (obj[p] == '\\' && p + 1 < obj.size()) { ++p; char e = obj[p]; v += (e == 'n' ? '\n' : e == 't' ? '\t' : e); }
                else v += obj[p];
                ++p;
            }
            return v;
        }
        std::size_t e = p; while (e < obj.size() && obj[e] != ',' && obj[e] != '}') ++e;
        std::string v = obj.substr(p, e - p);
        while (!v.empty() && v.back() == ' ') v.pop_back();
        return v == "null" ? "" : v;
    }
    static std::string JStr(const std::string& s) {
        std::string o = "\"";
        for (char c : s) {
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if (c == '\n') o += "\\n"; else if (c == '\t') o += "\\t";
            else if (c == '\r') {} else o += c;
        }
        return o + "\"";
    }
};

} // namespace okay
