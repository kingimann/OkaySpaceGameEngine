#pragma once
// Direct messages over Supabase (messages table; see docs/sql/social.sql).
// Drives an okay::account::AccountService (same pattern as Friends/Workshop).
// RLS limits every row to the two parties, so a conversation with one friend is
// just "messages where that friend is sender or recipient" — and whichever side
// isn't the friend is me, so we never need our own uuid.
#include "okay/Platform/Account/AccountService.hpp"
#include <string>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <cctype>
#include <cstdio>

namespace okay {

struct DirectMessage {
    long        id = 0;
    std::string body;
    long        when = 0;
    bool        mine = false;   // true when I sent it
};

class Messages {
public:
    explicit Messages(okay::account::AccountService& acct) : acct_(acct) {}

    bool Available() const { return acct_.IsLoggedIn(); }

    // Send a message to a friend (their profile/user uuid).
    bool Send(const std::string& friendUid, const std::string& body) {
        if (!acct_.IsLoggedIn() || friendUid.empty() || body.empty()) return false;
        return acct_.Api("/rest/v1/messages", "POST",
                         "{\"recipient\":" + JStr(friendUid) + ",\"body\":" + JStr(body) + "}").ok;
    }

    // Count incoming messages (addressed to me) newer than sinceId, and report
    // the newest incoming id via newestOut. Used for an unread-DM nav badge.
    int UnreadSince(long sinceId, long& newestOut) {
        newestOut = sinceId;
        if (!acct_.IsLoggedIn()) return 0;
        std::string mine = MyId();
        if (mine.empty()) return 0;
        auto r = acct_.Api("/rest/v1/messages?select=id,recipient&order=id.desc&limit=60", "GET");
        if (!r.ok) return 0;
        int n = 0;
        for (const auto& obj : SplitObjects(r.body)) {
            if (Field(obj, "recipient") != mine) continue;   // only messages sent TO me
            long id = std::atol(Field(obj, "id").c_str());
            if (id > newestOut) newestOut = id;
            if (id > sinceId) ++n;
        }
        return n;
    }

    // The conversation with one friend, oldest first.
    std::vector<DirectMessage> With(const std::string& friendUid) {
        std::vector<DirectMessage> out;
        if (!acct_.IsLoggedIn() || friendUid.empty()) return out;
        auto r = acct_.Api("/rest/v1/messages?select=id,sender,body,created_at"
                           "&or=(sender.eq." + friendUid + ",recipient.eq." + friendUid + ")"
                           "&order=created_at.asc", "GET");
        if (!r.ok) return out;
        for (const auto& obj : SplitObjects(r.body)) {
            DirectMessage m;
            m.id   = std::atol(Field(obj, "id").c_str());
            m.body = Field(obj, "body");
            m.when = ParseIso(Field(obj, "created_at"));
            m.mine = Field(obj, "sender") != friendUid;   // the other party is the friend
            out.push_back(m);
        }
        return out;
    }

private:
    okay::account::AccountService& acct_;
    std::string myId_;

    // My own profile/user uuid (looked up by username, cached).
    std::string MyId() {
        if (myId_.empty()) {
            std::string enc; for (char c : acct_.CurrentSession().username) {
                if (std::isalnum((unsigned char)c)||c=='-'||c=='_'||c=='.'||c=='~') enc += c;
                else { char b[4]; std::snprintf(b,sizeof(b),"%%%02X",(unsigned char)c); enc += b; } }
            auto r = acct_.Api("/rest/v1/profiles?select=id&username=eq." + enc, "GET");
            if (r.ok) { auto o = SplitObjects(r.body); if (!o.empty()) myId_ = Field(o[0], "id"); }
        }
        return myId_;
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
