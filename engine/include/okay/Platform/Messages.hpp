#pragma once
// Direct messages over Supabase (messages table; see docs/sql/social.sql).
// Drives an okay::account::AccountService (same pattern as Friends/Workshop).
// RLS limits every row to the two parties, so a conversation with one friend is
// just "messages where that friend is sender or recipient" — and whichever side
// isn't the friend is me, so we never need our own uuid.
#include "okay/Platform/Account/AccountService.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>
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
    bool        edited = false; // body was edited after sending
    long        likes = 0;      // ❤ reaction count (filled by the UI)
    bool        liked = false;  // I reacted (filled by the UI)
    long        replyTo = 0;    // id of the message this one replies to (0 = none)
};

class Messages {
public:
    explicit Messages(okay::account::AccountService& acct) : acct_(acct) {}

    bool Available() const { return acct_.IsLoggedIn(); }

    // Send a message to a friend (their profile/user uuid). Optionally a reply
    // to an earlier message (Messenger/Telegram-style quoting), by its id.
    bool Send(const std::string& friendUid, const std::string& body, long replyTo = 0) {
        if (!acct_.IsLoggedIn() || friendUid.empty() || body.empty()) return false;
        std::string j = "{\"recipient\":" + JStr(friendUid) + ",\"body\":" + JStr(body);
        if (replyTo > 0) j += ",\"reply_to\":" + std::to_string(replyTo);
        j += "}";
        return acct_.Api("/rest/v1/messages", "POST", j).ok;
    }

    // Delete one of my own sent messages (RLS blocks deleting others').
    bool Delete(long messageId) {
        if (!acct_.IsLoggedIn() || messageId <= 0) return false;
        return acct_.Api("/rest/v1/messages?id=eq." + std::to_string(messageId), "DELETE").ok;
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

    // A per-conversation preview (newest message with each other party) for the
    // Messenger-style conversation list: who, the last line, when, its id, and
    // whether I sent it. Most-recent conversation first. One query.
    struct ConversationPreview {
        std::string uid;
        std::string lastBody;
        long        when = 0;
        long        lastId = 0;
        bool        lastMine = false;
    };
    std::vector<ConversationPreview> Conversations() {
        std::vector<ConversationPreview> out;
        if (!acct_.IsLoggedIn()) return out;
        std::string mine = MyId();
        if (mine.empty()) return out;
        auto r = acct_.Api("/rest/v1/messages?select=id,sender,recipient,body,created_at"
                           "&order=id.desc&limit=300", "GET");
        if (!r.ok) return out;
        std::set<std::string> seen;
        for (const auto& o : SplitObjects(r.body)) {
            std::string s = Field(o, "sender"), rc = Field(o, "recipient");
            std::string other = (s == mine) ? rc : s;
            if (other.empty() || seen.count(other)) continue;   // first seen = newest
            seen.insert(other);
            ConversationPreview c;
            c.uid = other;
            c.lastBody = Field(o, "body");
            c.when = ParseIso(Field(o, "created_at"));
            c.lastId = std::atol(Field(o, "id").c_str());
            c.lastMine = (s == mine);
            out.push_back(c);
        }
        return out;
    }

    // The conversation with one friend, oldest first.
    std::vector<DirectMessage> With(const std::string& friendUid) {
        std::vector<DirectMessage> out;
        if (!acct_.IsLoggedIn() || friendUid.empty()) return out;
        auto r = acct_.Api("/rest/v1/messages?select=id,sender,body,created_at,edited,reply_to"
                           "&or=(sender.eq." + friendUid + ",recipient.eq." + friendUid + ")"
                           "&order=created_at.asc", "GET");
        if (!r.ok) return out;
        for (const auto& obj : SplitObjects(r.body)) {
            DirectMessage m;
            m.id   = std::atol(Field(obj, "id").c_str());
            m.body = Field(obj, "body");
            m.when = ParseIso(Field(obj, "created_at"));
            m.mine = Field(obj, "sender") != friendUid;   // the other party is the friend
            m.edited = Field(obj, "edited") == "true";
            m.replyTo = std::atol(Field(obj, "reply_to").c_str());
            out.push_back(m);
        }
        return out;
    }

    // Edit one of my own sent messages (RLS blocks editing others').
    bool Edit(long messageId, const std::string& body) {
        if (!acct_.IsLoggedIn() || messageId <= 0 || body.empty()) return false;
        return acct_.Api("/rest/v1/messages?id=eq." + std::to_string(messageId), "PATCH",
                         "{\"body\":" + JStr(body) + ",\"edited\":true}").ok;
    }

    // ---- Reactions ("likes") ----------------------------------------------
    // Toggle my ❤ on a message. Insert when `on`, delete when off.
    bool Like(long messageId, bool on) {
        if (!acct_.IsLoggedIn() || messageId <= 0) return false;
        if (on)
            return acct_.Api("/rest/v1/message_reactions", "POST",
                             "{\"scope\":\"dm\",\"target_id\":" + std::to_string(messageId) +
                             ",\"emoji\":\"heart\"}", "Prefer: resolution=ignore-duplicates").ok;
        return acct_.Api("/rest/v1/message_reactions?scope=eq.dm&target_id=eq." +
                         std::to_string(messageId) + "&emoji=eq.heart", "DELETE").ok;
    }
    // Like count per message id across this conversation.
    std::map<long,long> Likes() {
        std::map<long,long> out;
        auto r = acct_.Api("/rest/v1/message_reactions?select=target_id&scope=eq.dm&emoji=eq.heart", "GET");
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body)) out[std::atol(Field(o,"target_id").c_str())] += 1;
        return out;
    }
    // Message ids I've liked.
    std::map<long,bool> MyLikes() {
        std::map<long,bool> out;
        std::string mine = MyId();
        if (mine.empty()) return out;
        auto r = acct_.Api("/rest/v1/message_reactions?select=target_id&scope=eq.dm&emoji=eq.heart&reactor=eq." + mine, "GET");
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body)) out[std::atol(Field(o,"target_id").c_str())] = true;
        return out;
    }

    // ---- Reactions (any emoji, Messenger/Telegram-style) ------------------
    // Toggle my `emoji` reaction (a short key like "heart","laugh","fire") on a
    // message. The table keys on (reactor,scope,target_id,emoji), so each person
    // may leave several different reactions on the same message.
    bool React(long messageId, const std::string& emoji, bool on) {
        if (!acct_.IsLoggedIn() || messageId <= 0 || emoji.empty()) return false;
        if (on)
            return acct_.Api("/rest/v1/message_reactions", "POST",
                             "{\"scope\":\"dm\",\"target_id\":" + std::to_string(messageId) +
                             ",\"emoji\":" + JStr(emoji) + "}", "Prefer: resolution=ignore-duplicates").ok;
        return acct_.Api("/rest/v1/message_reactions?scope=eq.dm&target_id=eq." +
                         std::to_string(messageId) + "&emoji=eq." + emoji, "DELETE").ok;
    }
    // Reaction counts per message: id -> (emoji -> count).
    std::map<long, std::map<std::string,long>> Reactions() {
        std::map<long, std::map<std::string,long>> out;
        auto r = acct_.Api("/rest/v1/message_reactions?select=target_id,emoji&scope=eq.dm", "GET");
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body))
            out[std::atol(Field(o,"target_id").c_str())][Field(o,"emoji")] += 1;
        return out;
    }
    // The emojis I've reacted with, per message: id -> set<emoji>.
    std::map<long, std::set<std::string>> MyReactions() {
        std::map<long, std::set<std::string>> out;
        std::string mine = MyId(); if (mine.empty()) return out;
        auto r = acct_.Api("/rest/v1/message_reactions?select=target_id,emoji&scope=eq.dm&reactor=eq." + mine, "GET");
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body))
            out[std::atol(Field(o,"target_id").c_str())].insert(Field(o,"emoji"));
        return out;
    }

    // ---- Read receipts ----------------------------------------------------
    // Record that I've read up to message `lastId` in my conversation with
    // `peer` (upsert on (reader, peer)); drives the other side's "Seen" marker.
    bool MarkRead(const std::string& peerUid, long lastId) {
        if (!acct_.IsLoggedIn() || peerUid.empty() || lastId <= 0) return false;
        return acct_.Api("/rest/v1/message_reads?on_conflict=reader,peer", "POST",
                         "{\"peer\":" + JStr(peerUid) + ",\"last_read_id\":" + std::to_string(lastId) + "}",
                         "Prefer: resolution=merge-duplicates").ok;
    }
    // The id up to which `peer` has read OUR conversation (0 = not read yet).
    long PeerReadId(const std::string& peerUid) {
        if (!acct_.IsLoggedIn() || peerUid.empty()) return 0;
        std::string mine = MyId(); if (mine.empty()) return 0;
        auto r = acct_.Api("/rest/v1/message_reads?select=last_read_id&reader=eq." + peerUid +
                           "&peer=eq." + mine, "GET");
        if (!r.ok) return 0;
        auto o = SplitObjects(r.body);
        return o.empty() ? 0 : std::atol(Field(o[0], "last_read_id").c_str());
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
