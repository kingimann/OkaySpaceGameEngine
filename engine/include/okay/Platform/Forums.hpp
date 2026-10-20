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
};
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
        auto r = acct_.Api("/rest/v1/forum_threads?select=id,title,author_name,created_at,body,category"
                           "&order=created_at.desc", "GET");
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body)) {
            ForumThread t;
            t.id = std::atol(Field(o, "id").c_str());
            t.title = Field(o, "title");
            t.author = Field(o, "author_name");
            t.when = ParseIso(Field(o, "created_at"));
            t.body = Field(o, "body");
            t.category = Field(o, "category");
            if (!t.title.empty()) out.push_back(t);
        }
        return out;
    }

    bool StartThread(const std::string& title, std::string* err = nullptr) {
        return StartThread(title, "", "", err);
    }
    // Reddit-style: a post has a title, optional self-text body, and a category.
    bool StartThread(const std::string& title, const std::string& body,
                     const std::string& category, std::string* err) {
        if (!acct_.IsLoggedIn()) { if (err) *err = "Sign in to post."; return false; }
        if (title.empty())       { if (err) *err = "Enter a title.";   return false; }
        auto r = acct_.Api("/rest/v1/forum_threads", "POST",
                           "{\"title\":" + JStr(title) +
                           ",\"body\":" + JStr(body) +
                           ",\"category\":" + JStr(category) +
                           ",\"author_name\":" + JStr(acct_.CurrentSession().username) + "}");
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
