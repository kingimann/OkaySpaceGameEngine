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

    // ---- Typing indicator -------------------------------------------------
    // Tell `peer` I'm typing to them right now (upsert a fresh timestamp). Call
    // this throttled while the composer has text.
    bool SetTyping(const std::string& peerUid) {
        if (!acct_.IsLoggedIn() || peerUid.empty()) return false;
        return acct_.Api("/rest/v1/typing_state?on_conflict=sender", "POST",
                         "{\"peer\":" + JStr(peerUid) + ",\"at\":" + JStr(IsoNow()) + "}",
                         "Prefer: resolution=merge-duplicates").ok;
    }
    // Is `peer` currently typing to me? True when their row is fresh (<6s old).
    bool PeerTyping(const std::string& peerUid) {
        if (!acct_.IsLoggedIn() || peerUid.empty()) return false;
        std::string mine = MyId(); if (mine.empty()) return false;
        auto r = acct_.Api("/rest/v1/typing_state?select=at&sender=eq." + peerUid +
                           "&peer=eq." + mine, "GET");
        if (!r.ok) return false;
        auto o = SplitObjects(r.body);
        if (o.empty()) return false;
        long at = ParseIso(Field(o[0], "at"));
        long now = (long)std::time(nullptr);
        return at > 0 && (now - at) >= -2 && (now - at) < 6;   // fresh (allow small skew)
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

    // ---- Group chats (multi-person DMs; see docs/sql/group_chats.sql) ------
    // A group is a membership-gated conversation (group_convos / group_members /
    // group_messages), modelled on the Servers stack rather than the 1:1
    // `messages` table, so existing DMs are untouched.
    struct GroupConvo {
        long        id = 0;
        std::string title;          // may be empty -> the UI derives one from members
        std::string owner;          // owner uuid
        int         members = 0;    // member count
        std::string lastBody;
        std::string lastSender;     // sender_name of the last message
        long        when = 0;
        long        lastId = 0;
        long        lastRead = 0;   // my last_read marker (unread = lastId > lastRead)
    };
    struct GroupMessage {
        long        id = 0;
        std::string body;
        std::string sender;         // sender uuid
        std::string senderName;     // denormalized display name
        long        when = 0;
        bool        mine = false;
        bool        edited = false;
        long        replyTo = 0;
    };

    // Create a group with an initial member set (uuids, excluding me — the RPC
    // adds me as owner). Returns the new conversation id (0 on failure).
    long CreateGroup(const std::string& title, const std::vector<std::string>& memberUids) {
        if (!acct_.IsLoggedIn()) return 0;
        std::string arr = "[";
        for (std::size_t i = 0; i < memberUids.size(); ++i) { if (i) arr += ","; arr += JStr(memberUids[i]); }
        arr += "]";
        auto r = acct_.Api("/rest/v1/rpc/create_group", "POST",
                           "{\"p_title\":" + JStr(title) + ",\"p_members\":" + arr + "}");
        if (!r.ok) return 0;
        auto o = SplitObjects(r.body);
        return o.empty() ? 0 : std::atol(Field(o[0], "id").c_str());
    }

    // Every group I'm in, with its last message + my unread marker, most-recent
    // first. One RPC round-trip (my_groups).
    std::vector<GroupConvo> MyGroups() {
        std::vector<GroupConvo> out;
        if (!acct_.IsLoggedIn()) return out;
        auto r = acct_.Api("/rest/v1/rpc/my_groups", "POST", "{}");
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body)) {
            GroupConvo g;
            g.id        = std::atol(Field(o, "convo_id").c_str());
            g.title     = Field(o, "title");
            g.owner     = Field(o, "owner");
            g.members   = std::atoi(Field(o, "member_count").c_str());
            g.lastId    = std::atol(Field(o, "last_id").c_str());
            g.lastBody  = Field(o, "last_body");
            g.lastSender= Field(o, "last_sender");
            g.when      = ParseIso(Field(o, "last_when"));
            g.lastRead  = std::atol(Field(o, "last_read").c_str());
            if (g.id) out.push_back(g);
        }
        return out;
    }

    // Member uuids of a group.
    std::vector<std::string> GroupMemberIds(long convoId) {
        std::vector<std::string> out;
        if (!acct_.IsLoggedIn() || convoId <= 0) return out;
        auto r = acct_.Api("/rest/v1/group_members?convo_id=eq." + std::to_string(convoId) +
                           "&select=member", "GET");
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body)) { std::string u = Field(o, "member"); if (!u.empty()) out.push_back(u); }
        return out;
    }

    // The messages of a group, oldest first.
    std::vector<GroupMessage> GroupThread(long convoId) {
        std::vector<GroupMessage> out;
        if (!acct_.IsLoggedIn() || convoId <= 0) return out;
        std::string mine = MyId();
        auto r = acct_.Api("/rest/v1/group_messages?convo_id=eq." + std::to_string(convoId) +
                           "&select=id,sender,sender_name,body,created_at,edited,reply_to"
                           "&order=created_at.asc", "GET");
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body)) {
            GroupMessage m;
            m.id        = std::atol(Field(o, "id").c_str());
            m.sender    = Field(o, "sender");
            m.senderName= Field(o, "sender_name");
            m.body      = Field(o, "body");
            m.when      = ParseIso(Field(o, "created_at"));
            m.edited    = Field(o, "edited") == "true";
            m.replyTo   = std::atol(Field(o, "reply_to").c_str());
            m.mine      = !mine.empty() && m.sender == mine;
            out.push_back(m);
        }
        return out;
    }

    // Send a message to a group (optionally a reply). sender_name is denormalized
    // so the UI can show the author without a profiles join per row.
    bool SendGroup(long convoId, const std::string& body, long replyTo = 0) {
        if (!acct_.IsLoggedIn() || convoId <= 0 || body.empty()) return false;
        std::string j = "{\"convo_id\":" + std::to_string(convoId) + ",\"body\":" + JStr(body) +
                        ",\"sender_name\":" + JStr(acct_.CurrentSession().username);
        if (replyTo > 0) j += ",\"reply_to\":" + std::to_string(replyTo);
        j += "}";
        return acct_.Api("/rest/v1/group_messages", "POST", j).ok;
    }
    bool EditGroup(long messageId, const std::string& body) {
        if (!acct_.IsLoggedIn() || messageId <= 0 || body.empty()) return false;
        return acct_.Api("/rest/v1/group_messages?id=eq." + std::to_string(messageId), "PATCH",
                         "{\"body\":" + JStr(body) + ",\"edited\":true}").ok;
    }
    bool DeleteGroup(long messageId) {
        if (!acct_.IsLoggedIn() || messageId <= 0) return false;
        return acct_.Api("/rest/v1/group_messages?id=eq." + std::to_string(messageId), "DELETE").ok;
    }

    // Membership management: add (owner only, via RPC), leave (self), rename (owner).
    bool AddGroupMember(long convoId, const std::string& uid) {
        if (!acct_.IsLoggedIn() || convoId <= 0 || uid.empty()) return false;
        return acct_.Api("/rest/v1/rpc/add_group_member", "POST",
                         "{\"p_convo\":" + std::to_string(convoId) + ",\"p_member\":" + JStr(uid) + "}").ok;
    }
    bool LeaveGroup(long convoId) {
        if (!acct_.IsLoggedIn() || convoId <= 0) return false;
        std::string mine = MyId(); if (mine.empty()) return false;
        return acct_.Api("/rest/v1/group_members?convo_id=eq." + std::to_string(convoId) +
                         "&member=eq." + mine, "DELETE").ok;
    }
    bool RenameGroup(long convoId, const std::string& title) {
        if (!acct_.IsLoggedIn() || convoId <= 0) return false;
        return acct_.Api("/rest/v1/group_convos?id=eq." + std::to_string(convoId), "PATCH",
                         "{\"title\":" + JStr(title) + "}").ok;
    }

    // Mark the group read up to lastId (updates my last_read row).
    bool MarkGroupRead(long convoId, long lastId) {
        if (!acct_.IsLoggedIn() || convoId <= 0 || lastId <= 0) return false;
        std::string mine = MyId(); if (mine.empty()) return false;
        return acct_.Api("/rest/v1/group_members?convo_id=eq." + std::to_string(convoId) +
                         "&member=eq." + mine, "PATCH",
                         "{\"last_read\":" + std::to_string(lastId) + "}").ok;
    }

    // Group reactions (scope 'group'; shares the message_reactions table). Mirror
    // the DM React/Reactions/MyReactions API.
    bool GroupReact(long messageId, const std::string& emoji, bool on) {
        if (!acct_.IsLoggedIn() || messageId <= 0 || emoji.empty()) return false;
        if (on)
            return acct_.Api("/rest/v1/message_reactions", "POST",
                             "{\"scope\":\"group\",\"target_id\":" + std::to_string(messageId) +
                             ",\"emoji\":" + JStr(emoji) + "}", "Prefer: resolution=ignore-duplicates").ok;
        return acct_.Api("/rest/v1/message_reactions?scope=eq.group&target_id=eq." +
                         std::to_string(messageId) + "&emoji=eq." + emoji, "DELETE").ok;
    }
    std::map<long, std::map<std::string,long>> GroupReactions() {
        std::map<long, std::map<std::string,long>> out;
        auto r = acct_.Api("/rest/v1/message_reactions?select=target_id,emoji&scope=eq.group", "GET");
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body))
            out[std::atol(Field(o,"target_id").c_str())][Field(o,"emoji")] += 1;
        return out;
    }
    std::map<long, std::set<std::string>> GroupMyReactions() {
        std::map<long, std::set<std::string>> out;
        std::string mine = MyId(); if (mine.empty()) return out;
        auto r = acct_.Api("/rest/v1/message_reactions?select=target_id,emoji&scope=eq.group&reactor=eq." + mine, "GET");
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body))
            out[std::atol(Field(o,"target_id").c_str())].insert(Field(o,"emoji"));
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

    // Current UTC time as an ISO-8601 string (for timestamptz columns).
    static std::string IsoNow() {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
#if defined(_WIN32)
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
        return buf;
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
