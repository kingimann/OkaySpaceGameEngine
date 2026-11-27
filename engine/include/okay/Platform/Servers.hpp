#pragma once
// Discord-style servers over Supabase (servers / server_members / channels /
// channel_messages; see docs/sql/social.sql). Drives an okay::account::
// AccountService. Create/join go through SECURITY DEFINER RPCs (create_server /
// join_server) so a non-member can make or join one without being able to read
// others' servers; everything else is plain PostgREST gated by RLS. No-ops when
// signed out / offline. Every table request and database function call goes
// through OkayBackend's Database (acct_.Db(); round 6), byte for byte what it
// sent before (tests/test_okay_backend_golden_community2.cpp).
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Platform/Privacy.hpp"
#include <string>
#include <vector>
#include <algorithm>
#include <map>
#include <cstdlib>
#include <ctime>
#include <cstdio>

namespace okay {

struct Server  { long id = 0; std::string name; std::string invite; std::string owner; };
struct Channel { long id = 0; std::string name; };
struct ChannelMessage { long id = 0; std::string author; std::string body; long when = 0; bool edited = false; long likes = 0; bool liked = false; };
struct ServerMember { std::string uid, name, avatar, role; bool online = false; long lastSeen = 0; };

class Servers {
public:
    using Query = okay::backend::Query;
    explicit Servers(okay::account::AccountService& acct) : acct_(acct) {}

    bool Available() const { return acct_.IsLoggedIn(); }

    std::vector<Server> MyServers() {
        std::vector<Server> out;
        if (!acct_.IsLoggedIn()) return out;
        auto r = acct_.Db().From("servers").Select("id,name,invite_code,owner").Order("created_at").Get();
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body)) {
            Server s; s.id = std::atol(Field(o, "id").c_str());
            s.name = Field(o, "name"); s.invite = Field(o, "invite_code"); s.owner = Field(o, "owner");
            if (s.id) out.push_back(s);
        }
        return out;
    }

    // My own profile/user uuid (looked up by username, cached) — lets the UI tell
    // whether I own the selected server (server.owner == MyUid()).
    std::string MyUid() {
        if (myUid_.empty() && acct_.IsLoggedIn()) {
            auto r = acct_.Db().From("profiles").Select("id").Eq("username", acct_.CurrentSession().username).Get();   // the name percent-encoded
            if (r.ok) { auto o = SplitObjects(r.body); if (!o.empty()) myUid_ = Field(o[0], "id"); }
        }
        return myUid_;
    }

    bool CreateServer(const std::string& name, Server* created = nullptr, std::string* err = nullptr) {
        if (!acct_.IsLoggedIn()) { if (err) *err = "Sign in first."; return false; }
        if (name.empty())        { if (err) *err = "Enter a server name."; return false; }
        auto r = acct_.Db().Rpc("create_server", "{\"p_name\":" + JStr(name) + "}");
        if (!r.ok) { if (err) *err = r.body.empty() ? "Couldn't create server." : r.body; return false; }
        if (created) { auto o = SplitObjects(r.body);
            if (!o.empty()) { created->id = std::atol(Field(o[0], "id").c_str());
                              created->name = Field(o[0], "name"); created->invite = Field(o[0], "invite_code"); } }
        return true;
    }

    bool JoinServer(const std::string& code, std::string* err = nullptr) {
        if (!acct_.IsLoggedIn()) { if (err) *err = "Sign in first."; return false; }
        auto r = acct_.Db().Rpc("join_server", "{\"p_code\":" + JStr(code) + "}");
        if (!r.ok) { if (err) *err = r.body.empty() ? "Join failed." : r.body; return false; }
        // A bad code returns JSON null (no object).
        if (SplitObjects(r.body).empty()) { if (err) *err = "No server with code '" + code + "'."; return false; }
        return true;
    }

    std::vector<Channel> Channels(long serverId) {
        std::vector<Channel> out;
        if (serverId <= 0) return out;
        auto r = acct_.Db().From("channels").Eq("server_id", std::to_string(serverId)).Select("id,name").Order("position").Order("id").Get();
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body)) {
            Channel c; c.id = std::atol(Field(o, "id").c_str()); c.name = Field(o, "name");
            if (c.id) out.push_back(c);
        }
        return out;
    }

    // Members of a server (with display name, avatar and online status), owner
    // first. Resolves profiles in one batched query.
    std::vector<ServerMember> Members(long serverId) {
        std::vector<ServerMember> out;
        if (serverId <= 0) return out;
        auto r = acct_.Db().From("server_members").Eq("server_id", std::to_string(serverId)).Select("member,role").Get();
        if (!r.ok) return out;
        std::vector<std::string> ids;
        for (const auto& o : SplitObjects(r.body)) {
            ServerMember m; m.uid = Field(o, "member"); m.role = Field(o, "role");
            if (m.uid.empty()) continue;
            out.push_back(m);
            ids.push_back(m.uid);
        }
        if (ids.empty()) return out;
        auto p = privacy::ProfilesGet(acct_, [&ids](Query& q) {   // the ids as they are, as always
            q.Select("id,username,display_name,avatar_url,last_seen").In("id", ids, kAsIs);
        });
        if (p.ok) {
            for (const auto& o : SplitObjects(p.body)) {
                std::string id = Field(o, "id");
                for (auto& m : out) if (m.uid == id) {
                    std::string disp = okay::account::PublicName(Field(o, "display_name"));
                    m.name = disp.empty() ? okay::account::PublicName(Field(o, "username")) : disp;
                    m.avatar = Field(o, "avatar_url");
                    long seen = ParseIso(Field(o, "last_seen"));
                    m.online = seen > 0 && std::difftime(std::time(nullptr), (std::time_t)seen) < 180.0;
                    m.lastSeen = seen > 978307200 ? seen : 0;   // "Invisible" backdates to 2000
                }
            }
        }
        // Owner/online first for a tidy list.
        std::sort(out.begin(), out.end(), [](const ServerMember& a, const ServerMember& b){
            if ((a.role == "owner") != (b.role == "owner")) return a.role == "owner";
            if (a.online != b.online) return a.online;
            return a.name < b.name;
        });
        return out;
    }

    bool CreateChannel(long serverId, const std::string& name) {
        if (serverId <= 0 || name.empty()) return false;
        return acct_.Db().From("channels").Insert("{\"server_id\":" + std::to_string(serverId) + ",\"name\":" + JStr(name) + "}").ok;
    }

    std::vector<ChannelMessage> Messages(long channelId, int limit = 60) {
        std::vector<ChannelMessage> out;
        if (channelId <= 0) return out;
        auto r = acct_.Db().From("channel_messages").Eq("channel_id", std::to_string(channelId))
                     .Select("id,author_name,body,created_at,edited").Order("created_at").Limit(limit).Get();
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body)) {
            ChannelMessage m; m.id = std::atol(Field(o, "id").c_str());
            m.author = okay::account::PublicName(Field(o, "author_name")); m.body = Field(o, "body");
            m.when = ParseIso(Field(o, "created_at"));
            m.edited = Field(o, "edited") == "true";
            out.push_back(m);
        }
        return out;
    }

    bool Post(long channelId, const std::string& body) {
        if (!acct_.IsLoggedIn() || channelId <= 0 || body.empty()) return false;
        return acct_.Db().From("channel_messages")
            .Insert("{\"channel_id\":" + std::to_string(channelId) + ",\"body\":" + JStr(body) + ",\"author_name\":" + JStr(acct_.CurrentSession().username) + "}").ok;
    }

    // Edit one of my own channel messages (RLS blocks editing others').
    bool EditMessage(long messageId, const std::string& body) {
        if (!acct_.IsLoggedIn() || messageId <= 0 || body.empty()) return false;
        return acct_.Db().From("channel_messages").Eq("id", std::to_string(messageId)).Update("{\"body\":" + JStr(body) + ",\"edited\":true}").ok;
    }
    // ---- Reactions ("likes") on channel messages ---------------------------
    bool Like(long messageId, bool on) {
        if (!acct_.IsLoggedIn() || messageId <= 0) return false;
        if (on)
            return acct_.Db().From("message_reactions").Prefer("resolution=ignore-duplicates")
                .Insert("{\"scope\":\"channel\",\"target_id\":" + std::to_string(messageId) + ",\"emoji\":\"heart\"}").ok;
        return acct_.Db().From("message_reactions").Eq("scope", "channel").Eq("target_id", std::to_string(messageId)).Eq("emoji", "heart").Delete().ok;
    }
    std::map<long,long> Likes() {
        std::map<long,long> out;
        auto r = acct_.Db().From("message_reactions").Select("target_id").Eq("scope", "channel").Eq("emoji", "heart").Get();
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body)) out[std::atol(Field(o,"target_id").c_str())] += 1;
        return out;
    }
    std::map<long,bool> MyLikes() {
        std::map<long,bool> out;
        std::string mine = MyUid();
        if (mine.empty()) return out;
        auto r = acct_.Db().From("message_reactions").Select("target_id").Eq("scope", "channel").Eq("emoji", "heart").Eq("reactor", mine, kAsIs).Get();
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body)) out[std::atol(Field(o,"target_id").c_str())] = true;
        return out;
    }

    // Leave a server: delete MY membership row only. (Without the member filter an
    // owner or admin would also match, and be allowed to remove, everyone below them.)
    bool Leave(long serverId) {
        if (serverId <= 0) return false;
        const std::string me = MyUid(); if (me.empty()) return false;
        return acct_.Db().From("server_members").Eq("server_id", std::to_string(serverId)).Eq("member", me, kAsIs).Delete().ok;
    }

    // ---- owner management (RLS restricts these to the server's owner) ------
    bool RenameChannel(long channelId, const std::string& name) {
        if (channelId <= 0 || name.empty()) return false;
        return acct_.Db().From("channels").Eq("id", std::to_string(channelId)).Update("{\"name\":" + JStr(name) + "}").ok;
    }
    bool DeleteChannel(long channelId) {   // cascades its messages
        if (channelId <= 0) return false;
        return acct_.Db().From("channels").Eq("id", std::to_string(channelId)).Delete().ok;
    }
    bool RenameServer(long serverId, const std::string& name) {
        if (serverId <= 0 || name.empty()) return false;
        return acct_.Db().From("servers").Eq("id", std::to_string(serverId)).Update("{\"name\":" + JStr(name) + "}").ok;
    }
    bool DeleteServer(long serverId) {     // cascades members, channels, messages
        if (serverId <= 0) return false;
        return acct_.Db().From("servers").Eq("id", std::to_string(serverId)).Delete().ok;
    }

private:
    okay::account::AccountService& acct_;
    std::string myUid_;
    /// Values sent unchanged in the address (ids from the server, my own id), as these requests always sent them.
    static constexpr okay::backend::ValueText kAsIs = okay::backend::ValueText::AsIs;

    static long ParseIso(const std::string& ls) {
        if (ls.size() < 19) return 0;
        std::tm tm{};
        if (sscanf(ls.c_str(), "%d-%d-%dT%d:%d:%d",
                   &tm.tm_year,&tm.tm_mon,&tm.tm_mday,&tm.tm_hour,&tm.tm_min,&tm.tm_sec) != 6) return 0;
        tm.tm_year -= 1900; tm.tm_mon -= 1;
#if defined(_WIN32)
        return (long)_mkgmtime(&tm);
#else
        return (long)timegm(&tm);
#endif
    }
public:
    // Tiny JSON helpers (public so the tests can reach them).
    // Top-level JSON objects. Braces inside strings ("a { in a message") don't count.
    static std::vector<std::string> SplitObjects(const std::string& json) {
        std::vector<std::string> out; int depth = 0; std::size_t start = std::string::npos;
        bool inStr = false;
        for (std::size_t i = 0; i < json.size(); ++i) {
            char c = json[i];
            if (inStr) { if (c == '\\') ++i; else if (c == '"') inStr = false; continue; }
            if (c == '"') inStr = true;
            else if (c == '{') { if (depth == 0) start = i; ++depth; }
            else if (c == '}' && depth > 0) { if (--depth == 0 && start != std::string::npos) { out.push_back(json.substr(start, i - start + 1)); start = std::string::npos; } }
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
                if (obj[p] == '\\' && p + 1 < obj.size()) { ++p; char e = obj[p]; v += (e=='n'?'\n':e=='t'?'\t':e); }
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
        std::string o = "\""; for (char c : s) {
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if (c == '\n') o += "\\n"; else if (c == '\t') o += "\\t";
            else if (c == '\r') {} else o += c;
        } o += "\""; return o;
    }
};

} // namespace okay
