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
#include <filesystem>
#include <algorithm>

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
    // ---- Voice call signalling ---------------------------------------------
    // A call is announced with an ordinary DM, "[Call] <code> <key>", and closed
    // with "[CallEnd] <code> <seconds>"; the ringer polls the recent ones sent
    // to me. (The audio itself goes over the UDP relay, not through here.)
    struct CallSignal { long id = 0; std::string from, body; long when = 0; };
    std::vector<CallSignal> RecentCallSignals(int maxAgeSecs = 90) {
        std::vector<CallSignal> out;
        if (!acct_.IsLoggedIn()) return out;
        std::string mine = MyId(); if (mine.empty()) return out;
        auto r = acct_.Api("/rest/v1/messages?select=id,sender,body,created_at&recipient=eq." + mine +
                           "&body=like.%5BCall*&created_at=gte." + IsoAt(std::time(nullptr) - maxAgeSecs) +
                           "&order=id.desc&limit=10", "GET");
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body)) {
            CallSignal c; c.id = std::atol(Field(o, "id").c_str()); c.from = Field(o, "sender");
            c.body = Field(o, "body"); c.when = ParseIso(Field(o, "created_at"));
            out.push_back(c);
        }
        return out;
    }

    // Game and party invites sent to me ("[Invite] ..." / "[Party] ..."), newest
    // first: id > sinceId and no older than maxAgeSecs. Drives the launcher's
    // "X invited you to play" pop-up even when the chat isn't open.
    std::vector<CallSignal> InvitesSince(long sinceId, int maxAgeSecs = 900) {
        std::vector<CallSignal> out;
        if (!acct_.IsLoggedIn()) return out;
        std::string mine = MyId(); if (mine.empty()) return out;
        auto r = acct_.Api("/rest/v1/messages?select=id,sender,body,created_at&recipient=eq." + mine +
                           "&or=(body.like.%5BInvite%5D*,body.like.%5BParty%5D*)&id=gt." + std::to_string(sinceId) +
                           "&created_at=gte." + IsoAt(std::time(nullptr) - maxAgeSecs) +
                           "&order=id.desc&limit=10", "GET");
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body)) {
            CallSignal c; c.id = std::atol(Field(o, "id").c_str()); c.from = Field(o, "sender");
            c.body = Field(o, "body"); c.when = ParseIso(Field(o, "created_at"));
            out.push_back(c);
        }
        return out;
    }

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
    // Also stamps updated_at: re-sent every few seconds while the conversation
    // is open and focused, that doubles as "I'm in the chat" presence.
    bool MarkRead(const std::string& peerUid, long lastId) {
        if (!acct_.IsLoggedIn() || peerUid.empty() || lastId <= 0) return false;
        return acct_.Api("/rest/v1/message_reads?on_conflict=reader,peer", "POST",
                         "{\"peer\":" + JStr(peerUid) + ",\"last_read_id\":" + std::to_string(lastId) +
                         ",\"updated_at\":" + JStr(IsoNow()) + "}",
                         "Prefer: resolution=merge-duplicates").ok;
    }
    // Everything the sender side shows about `peer` in one place: how far they
    // have read, when their read row was last refreshed (fresh = they have our
    // chat open right now), and when their launcher was last online (a message
    // older than that has reached them: "Delivered").
    struct PeerState { long readId = 0; long viewingAt = 0; long lastSeen = 0; bool ok = false; };
    PeerState PeerStateOf(const std::string& peerUid, bool withLastSeen) {
        PeerState st;
        if (!acct_.IsLoggedIn() || peerUid.empty()) return st;
        std::string mine = MyId(); if (mine.empty()) return st;
        auto r = acct_.Api("/rest/v1/message_reads?select=last_read_id,updated_at&reader=eq." + peerUid +
                           "&peer=eq." + mine, "GET");
        if (!r.ok) return st;
        st.ok = true;
        auto o = SplitObjects(r.body);
        if (!o.empty()) {
            st.readId = std::atol(Field(o[0], "last_read_id").c_str());
            st.viewingAt = ParseIso(Field(o[0], "updated_at"));
        }
        if (withLastSeen) {
            auto p = acct_.Api("/rest/v1/profiles?select=last_seen&id=eq." + peerUid, "GET");
            if (p.ok) { auto po = SplitObjects(p.body); if (!po.empty()) st.lastSeen = ParseIso(Field(po[0], "last_seen")); }
        }
        return st;
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

    // Everyone in a group with their name, avatar, how far they've read
    // (last_read: drives "Seen by") and when they were last online. Two
    // round-trips: the member rows, then their profiles.
    struct GroupMemberInfo { std::string uid, username, name, avatar; long lastRead = 0; long lastSeen = 0; };
    std::vector<GroupMemberInfo> GroupMembersInfo(long convoId) {
        std::vector<GroupMemberInfo> out;
        if (!acct_.IsLoggedIn() || convoId <= 0) return out;
        auto r = acct_.Api("/rest/v1/group_members?convo_id=eq." + std::to_string(convoId) +
                           "&select=member,last_read", "GET");
        if (!r.ok) return out;
        std::string ids;
        for (const auto& o : SplitObjects(r.body)) {
            GroupMemberInfo m; m.uid = Field(o, "member"); m.lastRead = std::atol(Field(o, "last_read").c_str());
            if (m.uid.empty()) continue;
            if (!ids.empty()) ids += ",";
            ids += m.uid;
            out.push_back(m);
        }
        if (ids.empty()) return out;
        auto p = acct_.Api("/rest/v1/profiles?select=id,username,display_name,avatar_url,last_seen&id=in.(" + ids + ")", "GET");
        if (p.ok)
            for (const auto& o : SplitObjects(p.body)) {
                const std::string id = Field(o, "id");
                for (auto& m : out) if (m.uid == id) {
                    m.username = Field(o, "username");
                    m.name = Field(o, "display_name"); if (m.name.empty()) m.name = m.username;
                    m.avatar = Field(o, "avatar_url");
                    m.lastSeen = ParseIso(Field(o, "last_seen"));
                }
            }
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

    // ---- Search across every chat ------------------------------------------
    // Messages whose text contains `query` (case-insensitive), in my DMs and the
    // groups I'm in, newest first. RLS already limits both tables to what I can
    // see. scope is "dm" (uid = the other person) or "group" (groupId set).
    struct SearchHit {
        std::string scope;          // "dm" | "group"
        long        id = 0;
        std::string uid;            // dm: the other person
        long        groupId = 0;    // group: its conversation id
        std::string senderName;     // group: who wrote it
        std::string body;
        long        when = 0;
        bool        mine = false;
    };
    std::vector<SearchHit> Search(const std::string& query, int limit = 40) {
        std::vector<SearchHit> out;
        if (!acct_.IsLoggedIn()) return out;
        std::string q;   // ilike pattern: *text*, percent-encoded (PostgREST's * is the wildcard)
        for (unsigned char c : query) {
            if (c == '*' || c == '%') continue;
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') q += (char)c;
            else { char b[4]; std::snprintf(b, sizeof(b), "%%%02X", c); q += b; }
        }
        if (q.empty()) return out;
        std::string mine = MyId();
        const std::string lim = std::to_string(limit);
        auto r = acct_.Api("/rest/v1/messages?select=id,sender,recipient,body,created_at&body=ilike.*" + q +
                           "*&order=id.desc&limit=" + lim, "GET");
        if (r.ok)
            for (const auto& o : SplitObjects(r.body)) {
                SearchHit h; h.scope = "dm"; h.id = std::atol(Field(o, "id").c_str());
                const std::string s = Field(o, "sender"), rc = Field(o, "recipient");
                h.mine = !mine.empty() && s == mine; h.uid = h.mine ? rc : s;
                h.body = Field(o, "body"); h.when = ParseIso(Field(o, "created_at"));
                out.push_back(h);
            }
        auto g = acct_.Api("/rest/v1/group_messages?select=id,convo_id,sender,sender_name,body,created_at&body=ilike.*" + q +
                           "*&order=id.desc&limit=" + lim, "GET");
        if (g.ok)
            for (const auto& o : SplitObjects(g.body)) {
                SearchHit h; h.scope = "group"; h.id = std::atol(Field(o, "id").c_str());
                h.groupId = std::atol(Field(o, "convo_id").c_str());
                h.senderName = Field(o, "sender_name");
                h.mine = !mine.empty() && Field(o, "sender") == mine;
                h.body = Field(o, "body"); h.when = ParseIso(Field(o, "created_at"));
                out.push_back(h);
            }
        std::sort(out.begin(), out.end(), [](const SearchHit& a, const SearchHit& b) { return a.when > b.when; });
        if ((int)out.size() > limit) out.resize((std::size_t)limit);
        return out;
    }

    // ---- Edit history (message_edits; docs/sql/chat_extras.sql) ------------
    // Before an edit is saved, the text it replaces is recorded, so anyone in
    // the chat can see earlier versions. scope: "dm" or "group".
    bool RecordEdit(const std::string& scope, long messageId, const std::string& oldBody) {
        if (!acct_.IsLoggedIn() || messageId <= 0) return false;
        return acct_.Api("/rest/v1/message_edits", "POST",
                         "{\"scope\":" + JStr(scope) + ",\"target_id\":" + std::to_string(messageId) +
                         ",\"old_body\":" + JStr(oldBody) + "}").ok;
    }
    struct EditVersion { std::string body; long when = 0; };
    // Earlier versions of a message, oldest first. `supported` is false when the
    // server doesn't have the table yet.
    std::vector<EditVersion> EditHistory(const std::string& scope, long messageId, bool* supported = nullptr) {
        std::vector<EditVersion> out;
        if (supported) *supported = true;
        if (!acct_.IsLoggedIn() || messageId <= 0) return out;
        auto r = acct_.Api("/rest/v1/message_edits?select=old_body,edited_at&scope=eq." + scope +
                           "&target_id=eq." + std::to_string(messageId) + "&order=id.asc", "GET");
        if (!r.ok) { if (supported) *supported = r.status != 404 && r.status != 400; return out; }
        for (const auto& o : SplitObjects(r.body)) out.push_back({Field(o, "old_body"), ParseIso(Field(o, "edited_at"))});
        return out;
    }

    // ---- Pinned messages (message_pins; docs/sql/chat_extras.sql) ----------
    // Shared pins: everyone in the chat sees them. A pin is keyed by (scope,
    // message id); Pins() returns every pin I can see (RLS: messages I can read).
    struct PinInfo { long id = 0; long when = 0; std::string by; };
    bool Pin(const std::string& scope, long messageId, bool on) {
        if (!acct_.IsLoggedIn() || messageId <= 0) return false;
        if (on)
            return acct_.Api("/rest/v1/message_pins", "POST",
                             "{\"scope\":" + JStr(scope) + ",\"target_id\":" + std::to_string(messageId) + "}",
                             "Prefer: resolution=ignore-duplicates").ok;
        return acct_.Api("/rest/v1/message_pins?scope=eq." + scope + "&target_id=eq." + std::to_string(messageId), "DELETE").ok;
    }
    std::vector<PinInfo> Pins(const std::string& scope, bool* supported = nullptr) {
        std::vector<PinInfo> out;
        if (supported) *supported = true;
        if (!acct_.IsLoggedIn()) return out;
        auto r = acct_.Api("/rest/v1/message_pins?select=target_id,pinned_by,created_at&scope=eq." + scope +
                           "&order=created_at.desc&limit=500", "GET");
        if (!r.ok) { if (supported) *supported = r.status != 404 && r.status != 400; return out; }
        for (const auto& o : SplitObjects(r.body))
            out.push_back({std::atol(Field(o, "target_id").c_str()), ParseIso(Field(o, "created_at")), Field(o, "pinned_by")});
        return out;
    }

    // ---- Attachments (Supabase Storage bucket "chat"; docs/sql/chat_media.sql) --
    // Upload a local file to chat/<my uuid>/<random>-<name> and return its
    // public URL ("" on failure; `err` gets the reason). Images are then sent as
    // the bare URL (rendered inline); other files via FileMessageBody().
    std::string UploadChatFile(const std::string& localPath, std::string* err = nullptr) {
        if (!acct_.IsLoggedIn()) { if (err) *err = "Sign in first."; return ""; }
        std::string mine = MyId();
        if (mine.empty()) { if (err) *err = "Couldn't resolve your account."; return ""; }
        std::string name = std::filesystem::path(localPath).filename().string();
        std::string safe;
        for (char c : name) safe += (std::isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_') ? c : '_';
        if (safe.empty()) safe = "file";
        char rnd[24]; std::snprintf(rnd, sizeof(rnd), "%08x%08x", (unsigned)std::rand(), (unsigned)std::time(nullptr));
        std::string obj = mine + "/" + rnd + "-" + safe;
        auto r = acct_.ApiUpload("/storage/v1/object/chat/" + obj, localPath, MimeFor(safe), "x-upsert: true");
        if (!r.ok) {
            if (err) *err = r.status == 404 || r.body.find("Bucket not found") != std::string::npos
                ? "Attachments aren't set up on the server yet (run docs/sql/chat_media.sql)."
                : (r.body.empty() ? std::string("Upload failed.") : r.body);
            return "";
        }
        std::string base = acct_.ServerUrl();
        if (!base.empty() && base.back() == '/') base.pop_back();
        return base + "/storage/v1/object/public/chat/" + obj;
    }

    // ---- Personal media library (custom emoji + stickers) --------------------
    // Lives in the same public "chat" bucket under chat/<me>/<folder>/<name>.<ext>
    // (folder = "emoji" or "stickers"), so no extra table is needed: the storage
    // policies already let you write only inside your own folder, and anyone can
    // read by URL. Messages carry the URL inline, so recipients need nothing else.
    struct LibraryItem { std::string name, file, url; };

    // Upload (or replace) an item; returns its public URL ("" on failure).
    std::string UploadToLibrary(const std::string& localPath, const std::string& folder,
                                const std::string& name, std::string* err = nullptr) {
        if (!acct_.IsLoggedIn()) { if (err) *err = "Sign in first."; return ""; }
        std::string mine = MyId();
        if (mine.empty()) { if (err) *err = "Couldn't resolve your account."; return ""; }
        std::string safe;
        for (char c : name) if (std::isalnum((unsigned char)c) || c == '_' || c == '-') safe += (char)std::tolower((unsigned char)c);
        if (safe.empty()) { if (err) *err = "Pick a name (letters, numbers, _ or -)."; return ""; }
        std::string ext = std::filesystem::path(localPath).extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext != ".png" && ext != ".gif" && ext != ".jpg" && ext != ".jpeg" && ext != ".webp") {
            if (err) *err = "Use a PNG, GIF, JPG or WebP image.";
            return "";
        }
        std::string obj = mine + "/" + folder + "/" + safe + ext;
        auto r = acct_.ApiUpload("/storage/v1/object/chat/" + obj, localPath, MimeFor(obj), "x-upsert: true");
        if (!r.ok) {
            if (err) *err = r.status == 404 || r.body.find("Bucket not found") != std::string::npos
                ? "Media isn't set up on the server yet (run docs/sql/chat_media.sql)."
                : (r.body.empty() ? std::string("Upload failed.") : r.body);
            return "";
        }
        return PublicChatUrl(obj);
    }

    // My items in a folder, by name.
    std::vector<LibraryItem> ListLibrary(const std::string& folder) {
        std::vector<LibraryItem> out;
        std::string mine = MyId();
        if (mine.empty()) return out;
        std::string body = "{\"prefix\":" + JStr(mine + "/" + folder + "/") +
                           ",\"limit\":500,\"offset\":0,\"sortBy\":{\"column\":\"name\",\"order\":\"asc\"}}";
        auto r = acct_.Api("/storage/v1/object/list/chat", "POST", body);
        if (!r.ok) return out;
        for (const auto& o : SplitObjects(r.body)) {
            std::string file = Field(o, "name");
            if (file.empty() || file[0] == '.') continue;   // ".emptyFolderPlaceholder"
            LibraryItem it;
            it.file = file;
            it.name = std::filesystem::path(file).stem().string();
            it.url = PublicChatUrl(mine + "/" + folder + "/" + file);
            out.push_back(it);
        }
        return out;
    }

    bool DeleteFromLibrary(const std::string& folder, const std::string& file) {
        std::string mine = MyId();
        if (mine.empty() || file.empty()) return false;
        return acct_.Api("/storage/v1/object/chat/" + mine + "/" + folder + "/" + file, "DELETE").ok;
    }

    std::string PublicChatUrl(const std::string& obj) const {
        std::string base = acct_.ServerUrl();
        if (!base.empty() && base.back() == '/') base.pop_back();
        return base + "/storage/v1/object/public/chat/" + obj;
    }

    // A file message: "[File] <name> (<size>) <url>". Reads as plain text + a
    // link on older clients; newer ones render it as a file card.
    static std::string FileMessageBody(const std::string& name, unsigned long long bytes, const std::string& url) {
        return "[File] " + name + " (" + HumanSize(bytes) + ") " + url;
    }
    static std::string HumanSize(unsigned long long b) {
        char buf[32];
        if (b < 1024ULL) std::snprintf(buf, sizeof(buf), "%llu B", b);
        else if (b < 1024ULL * 1024ULL) std::snprintf(buf, sizeof(buf), "%.1f KB", b / 1024.0);
        else if (b < 1024ULL * 1024ULL * 1024ULL) std::snprintf(buf, sizeof(buf), "%.1f MB", b / (1024.0 * 1024.0));
        else std::snprintf(buf, sizeof(buf), "%.2f GB", b / (1024.0 * 1024.0 * 1024.0));
        return buf;
    }
    static std::string MimeFor(const std::string& name) {
        std::string ext = std::filesystem::path(name).extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext == ".png")  return "image/png";
        if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
        if (ext == ".gif")  return "image/gif";
        if (ext == ".webp") return "image/webp";
        if (ext == ".bmp")  return "image/bmp";
        if (ext == ".pdf")  return "application/pdf";
        if (ext == ".txt" || ext == ".log" || ext == ".md") return "text/plain";
        if (ext == ".json") return "application/json";
        if (ext == ".zip")  return "application/zip";
        if (ext == ".wav")  return "audio/wav";
        if (ext == ".mp3")  return "audio/mpeg";
        if (ext == ".ogg")  return "audio/ogg";
        return "application/octet-stream";
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
    static std::string IsoNow() { return IsoAt(std::time(nullptr)); }
    static std::string IsoAt(std::time_t t) {
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
public:
    // Tiny JSON helpers (also used by the launcher's emoji/GIF code).
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
