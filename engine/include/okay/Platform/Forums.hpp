#pragma once
// Community forums over Supabase (forum_threads + forum_posts; see
// docs/sql/social.sql). Drives an okay::account::AccountService (same pattern as
// Friends/Workshop). Threads and posts are world-readable; writing needs a
// signed-in account. Gracefully no-ops offline / signed out.
#include "okay/Platform/Account/AccountService.hpp"
#include <string>
#include <vector>
#include <map>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <cstdio>

namespace okay {

struct ForumThread {
    long id = 0; std::string title; std::string author; long when = 0;
    std::string body;        // optional self-text (Reddit-style)
    std::string category;    // optional topic / "subforum"
    long score = 0;          // net votes (filled by the UI from Scores())
    int  comments = 0;       // reply count (filled by the UI)
    int  myVote = 0;         // -1 / 0 / +1 for the signed-in player
    long sectionId = 0;      // forum_sections.id (0 = the general board)
    bool pinned = false, locked = false;   // set by the section's moderators
};
// A Reddit-style community (docs/sql/forum_sections.sql). Its creator is admin.
struct ForumSection {
    long id = 0; std::string slug, name, description, rules, owner;
    int color = 0; long members = 0, threads = 0;
    std::string myRole;      // "" (not joined) | member | moderator | admin
    bool banned = false;     // I'm banned from posting here
};
struct ForumMember { std::string uid, name, role; long since = 0; };
struct ForumBan { std::string uid, name, reason; long when = 0; };
struct ForumPost {
    long id = 0; std::string author; std::string body; long when = 0;
    long score = 0; int myVote = 0;
};

class Forums {
public:
    explicit Forums(okay::account::AccountService& acct) : acct_(acct) {}

    bool Available() const { return acct_.IsLoggedIn(); }

    std::vector<ForumThread> Threads() {
        std::vector<ForumThread> out;
        auto r = acct_.Api("/rest/v1/forum_threads?select=id,title,author_name,created_at,body,category,section_id,pinned,locked"
                           "&order=created_at.desc", "GET");
        if (!r.ok) r = acct_.Api("/rest/v1/forum_threads?select=id,title,author_name,created_at,body,category"
                                 "&order=created_at.desc", "GET");   // before forum_sections.sql
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body)) {
            ForumThread t;
            t.id = std::atol(Field(o, "id").c_str());
            t.title = Field(o, "title");
            t.author = Field(o, "author_name");
            t.when = ParseIso(Field(o, "created_at"));
            t.body = Field(o, "body");
            t.category = Field(o, "category");
            t.sectionId = std::atol(Field(o, "section_id").c_str());
            t.pinned = Field(o, "pinned") == "true"; t.locked = Field(o, "locked") == "true";
            if (!t.title.empty()) out.push_back(t);
        }
        return out;
    }

    bool StartThread(const std::string& title, std::string* err = nullptr) {
        return StartThread(title, "", "", err);
    }
    // Reddit-style: a post has a title, optional self-text body, and a category.
    bool StartThread(const std::string& title, const std::string& body,
                     const std::string& category, std::string* err, long sectionId = 0) {
        if (!acct_.IsLoggedIn()) { if (err) *err = "Sign in to post."; return false; }
        if (title.empty())       { if (err) *err = "Enter a title.";   return false; }
        auto r = acct_.Api("/rest/v1/forum_threads", "POST",
                           "{\"title\":" + JStr(title) +
                           ",\"body\":" + JStr(body) +
                           ",\"category\":" + JStr(category) +
                           (sectionId > 0 ? ",\"section_id\":" + std::to_string(sectionId) : std::string()) +
                           ",\"author_name\":" + JStr(acct_.CurrentSession().username) + "}");
        if (!r.ok && err && r.body.find("row-level security") != std::string::npos) { *err = "You can't post here (banned or locked)."; return false; }
        if (!r.ok && err) *err = r.body.empty() ? "Failed to create thread." : r.body;
        return r.ok;
    }

    // ---- Voting (Reddit-style up/down) ------------------------------------
    // value: +1 up, -1 down, 0 clears my vote. Replaces any prior vote of mine.
    bool Vote(const std::string& kind, long targetId, int value) {
        if (!acct_.IsLoggedIn() || targetId <= 0) return false;
        // Remove my existing vote for this target (RLS scopes the delete to me).
        acct_.Api("/rest/v1/forum_votes?target_kind=eq." + kind +
                  "&target_id=eq." + std::to_string(targetId), "DELETE");
        if (value == 0) return true;
        return acct_.Api("/rest/v1/forum_votes", "POST",
                         "{\"target_kind\":" + JStr(kind) +
                         ",\"target_id\":" + std::to_string(targetId) +
                         ",\"value\":" + std::to_string(value > 0 ? 1 : -1) + "}").ok;
    }
    // Net score per target of a kind ('thread'|'post'): id -> sum(value).
    std::map<long, long> Scores(const std::string& kind) {
        std::map<long, long> out;
        auto r = acct_.Api("/rest/v1/forum_votes?select=target_id,value&target_kind=eq." + kind, "GET");
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body))
            out[std::atol(Field(o, "target_id").c_str())] += std::atol(Field(o, "value").c_str());
        return out;
    }
    // My own vote per target of a kind: id -> (-1|+1). Empty when signed out.
    std::map<long, int> MyVotes(const std::string& kind) {
        std::map<long, int> out;
        std::string uid = MyUid();
        if (uid.empty()) return out;
        auto r = acct_.Api("/rest/v1/forum_votes?select=target_id,value&target_kind=eq." + kind +
                           "&voter=eq." + uid, "GET");
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body))
            out[std::atol(Field(o, "target_id").c_str())] = (int)std::atol(Field(o, "value").c_str());
        return out;
    }
    // Reply count per thread (one GET of all post thread_ids, counted here).
    std::map<long, int> CommentCounts() {
        std::map<long, int> out;
        auto r = acct_.Api("/rest/v1/forum_posts?select=thread_id", "GET");
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body)) out[std::atol(Field(o, "thread_id").c_str())] += 1;
        return out;
    }
    // Delete my own thread (RLS blocks deleting others').
    bool DeleteThread(long id) {
        if (!acct_.IsLoggedIn() || id <= 0) return false;
        return acct_.Api("/rest/v1/forum_threads?id=eq." + std::to_string(id), "DELETE").ok;
    }

    std::vector<ForumPost> Posts(long threadId) {
        std::vector<ForumPost> out;
        if (threadId <= 0) return out;
        auto r = acct_.Api("/rest/v1/forum_posts?select=id,author_name,body,created_at"
                           "&thread_id=eq." + std::to_string(threadId) +
                           "&order=created_at.asc", "GET");
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body)) {
            ForumPost p;
            p.id = std::atol(Field(o, "id").c_str());
            p.author = Field(o, "author_name");
            p.body = Field(o, "body");
            p.when = ParseIso(Field(o, "created_at"));
            out.push_back(p);
        }
        return out;
    }

    bool Reply(long threadId, const std::string& body, std::string* err = nullptr) {
        if (!acct_.IsLoggedIn()) { if (err) *err = "Sign in to post."; return false; }
        if (threadId <= 0 || body.empty()) { if (err) *err = "Write a message."; return false; }
        auto r = acct_.Api("/rest/v1/forum_posts", "POST",
                           "{\"thread_id\":" + std::to_string(threadId) +
                           ",\"body\":" + JStr(body) +
                           ",\"author_name\":" + JStr(acct_.CurrentSession().username) + "}");
        if (!r.ok && err) *err = r.body.empty() ? "Failed to post." : r.body;
        return r.ok;
    }

    // ---- Sections (docs/sql/forum_sections.sql) -----------------------------
    // All sections with counts and my role. *supported = false before the SQL ran.
    std::vector<ForumSection> Sections(bool* supported) {
        std::vector<ForumSection> out;
        if (supported) *supported = false;
        auto r = acct_.Api("/rest/v1/forum_sections?select=id,slug,name,description,rules,color,owner&order=created_at.asc", "GET");
        if (!r.ok) return out;
        if (supported) *supported = true;
        for (const auto& o : SplitObjects(r.body)) {
            ForumSection x; x.id = std::atol(Field(o, "id").c_str()); x.slug = Field(o, "slug"); x.name = Field(o, "name");
            x.description = Field(o, "description"); x.rules = Field(o, "rules"); x.owner = Field(o, "owner");
            x.color = (int)std::atol(Field(o, "color").c_str());
            if (x.id > 0) out.push_back(x);
        }
        auto rc = acct_.Api("/rest/v1/rpc/forum_section_counts", "POST", "{}");
        if (rc.ok) for (const auto& o : SplitObjects(rc.body)) {
            const long id = std::atol(Field(o, "section_id").c_str());
            for (auto& x : out) if (x.id == id) { x.members = std::atol(Field(o, "members").c_str()); x.threads = std::atol(Field(o, "threads").c_str()); }
        }
        const std::string me = MyUid();
        if (!me.empty()) {
            auto rm = acct_.Api("/rest/v1/forum_section_members?select=section_id,role&user_id=eq." + me, "GET");
            if (rm.ok) for (const auto& o : SplitObjects(rm.body)) {
                const long id = std::atol(Field(o, "section_id").c_str());
                for (auto& x : out) if (x.id == id) x.myRole = Field(o, "role");
            }
            auto rb = acct_.Api("/rest/v1/forum_section_bans?select=section_id&user_id=eq." + me, "GET");
            if (rb.ok) for (const auto& o : SplitObjects(rb.body)) {
                const long id = std::atol(Field(o, "section_id").c_str());
                for (auto& x : out) if (x.id == id) x.banned = true;
            }
        }
        return out;
    }
    long CreateSection(const std::string& slug, const std::string& name, const std::string& desc, int color, std::string* err) {
        if (!acct_.IsLoggedIn()) { if (err) *err = "Sign in first."; return 0; }
        auto r = acct_.Api("/rest/v1/forum_sections", "POST",
                           "{\"slug\":" + JStr(slug) + ",\"name\":" + JStr(name) + ",\"description\":" + JStr(desc) +
                           ",\"color\":" + std::to_string(color) + "}", "Prefer: return=representation");
        if (!r.ok) {
            if (err) *err = r.body.find("duplicate") != std::string::npos ? "That name is taken." :
                            r.body.find("check") != std::string::npos ? "Use 3-24 lowercase letters, numbers or _ for the link name." : "Couldn't create the section.";
            return 0;
        }
        auto o = SplitObjects(r.body);
        return o.empty() ? 0 : std::atol(Field(o[0], "id").c_str());
    }
    bool UpdateSection(long id, const std::string& desc, const std::string& rules, int color) {
        return acct_.Api("/rest/v1/forum_sections?id=eq." + std::to_string(id), "PATCH",
                         "{\"description\":" + JStr(desc) + ",\"rules\":" + JStr(rules) + ",\"color\":" + std::to_string(color) + "}").ok;
    }
    bool DeleteSection(long id) { return acct_.Api("/rest/v1/forum_sections?id=eq." + std::to_string(id), "DELETE").ok; }
    bool Join(long sectionId) {
        return acct_.Api("/rest/v1/forum_section_members", "POST",
                         "{\"section_id\":" + std::to_string(sectionId) + ",\"user_name\":" + JStr(acct_.CurrentSession().username) + "}").ok;
    }
    bool Leave(long sectionId) {
        const std::string me = MyUid(); if (me.empty()) return false;
        return acct_.Api("/rest/v1/forum_section_members?section_id=eq." + std::to_string(sectionId) + "&user_id=eq." + me, "DELETE").ok;
    }
    std::vector<ForumMember> Members(long sectionId) {
        std::vector<ForumMember> out;
        auto r = acct_.Api("/rest/v1/forum_section_members?select=user_id,user_name,role,joined_at&section_id=eq." +
                           std::to_string(sectionId) + "&order=joined_at.asc", "GET");
        if (r.ok) for (const auto& o : SplitObjects(r.body)) {
            ForumMember m; m.uid = Field(o, "user_id"); m.name = Field(o, "user_name"); m.role = Field(o, "role");
            m.since = ParseIso(Field(o, "joined_at")); out.push_back(m);
        }
        return out;
    }
    // Admin tools: roles, removal, bans (RLS checks the caller's role).
    bool SetRole(long sectionId, const std::string& uid, const std::string& role) {
        return acct_.Api("/rest/v1/forum_section_members?section_id=eq." + std::to_string(sectionId) + "&user_id=eq." + uid,
                         "PATCH", "{\"role\":" + JStr(role) + "}").ok;
    }
    bool AddMember(long sectionId, const std::string& uid, const std::string& name, const std::string& role) {
        return acct_.Api("/rest/v1/forum_section_members?on_conflict=section_id,user_id", "POST",
                         "{\"section_id\":" + std::to_string(sectionId) + ",\"user_id\":" + JStr(uid) +
                         ",\"user_name\":" + JStr(name) + ",\"role\":" + JStr(role) + "}", "Prefer: resolution=merge-duplicates").ok;
    }
    bool RemoveMember(long sectionId, const std::string& uid) {
        return acct_.Api("/rest/v1/forum_section_members?section_id=eq." + std::to_string(sectionId) + "&user_id=eq." + uid, "DELETE").ok;
    }
    std::vector<ForumBan> Bans(long sectionId) {
        std::vector<ForumBan> out;
        auto r = acct_.Api("/rest/v1/forum_section_bans?select=user_id,user_name,reason,created_at&section_id=eq." +
                           std::to_string(sectionId) + "&order=created_at.desc", "GET");
        if (r.ok) for (const auto& o : SplitObjects(r.body)) {
            ForumBan b; b.uid = Field(o, "user_id"); b.name = Field(o, "user_name"); b.reason = Field(o, "reason");
            b.when = ParseIso(Field(o, "created_at")); out.push_back(b);
        }
        return out;
    }
    bool Ban(long sectionId, const std::string& uid, const std::string& name, const std::string& reason) {
        RemoveMember(sectionId, uid);
        return acct_.Api("/rest/v1/forum_section_bans", "POST",
                         "{\"section_id\":" + std::to_string(sectionId) + ",\"user_id\":" + JStr(uid) +
                         ",\"user_name\":" + JStr(name) + ",\"reason\":" + JStr(reason) + "}").ok;
    }
    bool Unban(long sectionId, const std::string& uid) {
        return acct_.Api("/rest/v1/forum_section_bans?section_id=eq." + std::to_string(sectionId) + "&user_id=eq." + uid, "DELETE").ok;
    }
    bool SetPinned(long threadId, bool on) {
        return acct_.Api("/rest/v1/forum_threads?id=eq." + std::to_string(threadId), "PATCH", on ? "{\"pinned\":true}" : "{\"pinned\":false}").ok;
    }
    bool SetLocked(long threadId, bool on) {
        return acct_.Api("/rest/v1/forum_threads?id=eq." + std::to_string(threadId), "PATCH", on ? "{\"locked\":true}" : "{\"locked\":false}").ok;
    }
    bool DeletePost(long postId) { return acct_.Api("/rest/v1/forum_posts?id=eq." + std::to_string(postId), "DELETE").ok; }
    // A user's id from their username ("" if none).
    std::string UidFor(const std::string& username) {
        std::string enc; for (char c : username) {
            if (std::isalnum((unsigned char)c)||c=='-'||c=='_'||c=='.'||c=='~') enc += c;
            else { char b[4]; std::snprintf(b,sizeof(b),"%%%02X",(unsigned char)c); enc += b; } }
        auto r = acct_.Api("/rest/v1/profiles?select=id&username=eq." + enc, "GET");
        if (!r.ok) return "";
        auto o = SplitObjects(r.body);
        return o.empty() ? "" : Field(o[0], "id");
    }
    // ---- Staff tools (profile_badges 'staff') ----------------------------------
    bool AmStaff() {
        const std::string me = MyUid(); if (me.empty()) return false;
        auto r = acct_.Api("/rest/v1/profile_badges?select=badge&badge=eq.staff&owner=eq." + me, "GET");
        return r.ok && !SplitObjects(r.body).empty();
    }
    bool SetVerified(const std::string& uid, bool on) {
        return acct_.Api("/rest/v1/profiles?id=eq." + uid, "PATCH", on ? "{\"verified\":true}" : "{\"verified\":false}").ok;
    }
    bool AwardBadge(const std::string& uid, const std::string& badge, bool on) {
        if (on) return acct_.Api("/rest/v1/profile_badges", "POST", "{\"owner\":" + JStr(uid) + ",\"badge\":" + JStr(badge) + "}",
                                 "Prefer: resolution=ignore-duplicates").ok;
        return acct_.Api("/rest/v1/profile_badges?owner=eq." + uid + "&badge=eq." + badge, "DELETE").ok;
    }

private:
    okay::account::AccountService& acct_;
    std::string myUid_;

    // My own profile/user uuid (looked up by username, cached) — needed to read
    // back which way I voted.
    std::string MyUid() {
        if (myUid_.empty() && acct_.IsLoggedIn()) {
            std::string enc; for (char c : acct_.CurrentSession().username) {
                if (std::isalnum((unsigned char)c)||c=='-'||c=='_'||c=='.'||c=='~') enc += c;
                else { char b[4]; std::snprintf(b,sizeof(b),"%%%02X",(unsigned char)c); enc += b; } }
            auto r = acct_.Api("/rest/v1/profiles?select=id&username=eq." + enc, "GET");
            if (r.ok) { auto o = SplitObjects(r.body); if (!o.empty()) myUid_ = Field(o[0], "id"); }
        }
        return myUid_;
    }

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
    static std::vector<std::string> SplitObjects(const std::string& json) {
        std::vector<std::string> out; int depth = 0; std::size_t start = std::string::npos;
        for (std::size_t i = 0; i < json.size(); ++i) {
            char c = json[i];
            if (c == '{') { if (depth == 0) start = i; ++depth; }
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
