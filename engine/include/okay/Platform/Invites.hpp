#pragma once
// Game invites (Community system, feature map 17.3 / 17.15): one record per "join my game / party /
// server / room" invite, with an expiry and an answer, kept by the server in public.game_invites
// (docs/sql/chat_invites.sql). The server lets only friends invite each other, honours blocks both
// ways, limits invites to 20 per 10 minutes and owns the clock and the status, so a modified client
// cannot invite strangers or forge an answer.
//
// The chat card stays the "[Invite] <title>\n<okay://join link>" message every launcher already shows
// (launcher/main.cpp ParseChatInvite); a new launcher adds the record's id to the link (JoinLink::inviteId)
// so the card can show the record's state. Until chat_invites.sql has run, Invites::Send answers
// Support::Missing and the caller sends the chat message alone, as before.
//
// Pure parts (tested in tests/test_chat_invites.cpp): ViewInvite / ViewLegacyInvite (what a card or a
// notification says: pending with the time left, expired, ended, accepted, declined, withdrawn),
// InviteAnswerText (the server's answers in words) and CheckJoinFriend (whether "Join game" on a friend
// works now, and why not: not friends, blocked, offline, not in a joinable game, full).
#include "okay/Platform/Friends.hpp"
#include "okay/Platform/Privacy.hpp"
#include "okay/Platform/ChatDelivery.hpp"
#include "okay/Net/JoinLink.hpp"

#include <string>
#include <vector>

namespace okay {

struct GameInvite {
    long id = 0;
    std::string senderId, recipientId;
    std::string senderName;      ///< filled by the caller from the friend list (a public name)
    std::string kind = "game";   ///< game | party | server | room
    std::string title;           ///< the game's title (or the party / server name)
    std::string link;            ///< okay://join link, party id, server invite code or room code
    std::string gameVersion;     ///< the inviter's game version ("" = unknown)
    std::string target;          ///< "" | exported | project | dedicated
    std::string status = "pending";   ///< pending | accepted | declined | cancelled
    long created = 0, expires = 0;    ///< unix seconds (server clock)
};

enum class InviteState { Pending, Accepted, Declined, Cancelled, Expired, Ended };
/// Whether the sender's presence was read. Known + an empty link means they are not in a joinable game now.
enum class PresenceSeen { Unknown, Known };

struct InviteView {
    InviteState state = InviteState::Pending;
    bool canAccept = false;   ///< show Accept / Join (and Decline)
    std::string text;         ///< the main line ("maya invited you to play Star Arena")
    std::string sub;          ///< the small line ("Expires in 1 h 20 min", "The match is full (8/8)")
};

namespace invitedetail {
inline std::string Left(long secs) {
    if (secs < 60) return "less than a minute";
    const long m = secs / 60, h = m / 60;
    if (h == 0) return std::to_string(m) + " min";
    return std::to_string(h) + " h" + (m % 60 ? " " + std::to_string(m % 60) + " min" : std::string());
}
inline std::string What(const std::string& kind, const std::string& title) {
    if (kind == "party") return "to their party";
    if (kind == "server") return "to the server " + title;
    if (kind == "room") return "to a room" + (title.empty() ? std::string() : " in " + title);
    return "to play " + (title.empty() ? std::string("a game") : title);
}
}  // namespace invitedetail

/// What the card for `inv` says at `now`. For a game invite, `senderLink` is the sender's current presence
/// join link: a different session (or none, when `seen` is Known) means the match the invite was for is over.
inline InviteView ViewInvite(const GameInvite& inv, long now, PresenceSeen seen, const std::string& senderLink) {
    InviteView v;
    const std::string who = inv.senderName.empty() ? std::string("A friend") : okay::account::PublicName(inv.senderName, inv.senderId);
    v.text = who + " invited you " + invitedetail::What(inv.kind, inv.title);
    if (inv.status == "accepted")  { v.state = InviteState::Accepted;  v.sub = "You accepted this invite."; return v; }
    if (inv.status == "declined")  { v.state = InviteState::Declined;  v.sub = "You declined this invite."; v.text += " (declined)"; return v; }
    if (inv.status == "cancelled") { v.state = InviteState::Cancelled; v.text = "This invite from " + who + " was withdrawn."; return v; }
    if (inv.expires > 0 && now >= inv.expires) { v.state = InviteState::Expired; v.text = "This invite from " + who + " has expired."; return v; }
    JoinLink mine, theirs;
    const bool isJoin = inv.kind == "game" && JoinLink::Parse(inv.link, mine);
    if (isJoin && seen == PresenceSeen::Known) {
        if (!JoinLink::Parse(senderLink, theirs) || !theirs.SameSession(mine)) {
            v.state = InviteState::Ended;
            v.text = who + " is no longer in that match.";
            v.sub = "The invite is out of date.";
            return v;
        }
    }
    v.state = InviteState::Pending; v.canAccept = true;
    if (isJoin && seen == PresenceSeen::Known && theirs.Full())
        v.sub = "The match is full (" + std::to_string(theirs.players) + "/" + std::to_string(theirs.maxPlayers) + ") right now.";
    else if (inv.expires > 0)
        v.sub = "Expires in " + invitedetail::Left(inv.expires - now) + ".";
    return v;
}
/// A chat invite with no record (an older launcher, or before chat_invites.sql ran): only its age is known.
/// Older than 6 hours counts as expired.
inline InviteView ViewLegacyInvite(const std::string& senderName, const std::string& title, long sent, long now) {
    GameInvite g; g.senderName = senderName; g.title = title; g.created = sent; g.expires = sent > 0 ? sent + 6 * 3600 : 0;
    InviteView v = ViewInvite(g, now, PresenceSeen::Unknown, "");
    if (v.state == InviteState::Pending) v.sub = "Sent " + invitedetail::Left(now - sent) + " ago.";
    return v;
}

/// respond_invite / cancel_invite answered this: the change was made.
inline bool InviteAnswerOk(const std::string& a) { return a == "accepted" || a == "declined" || a == "cancelled"; }
/// The server's answer (or an okay_rule reason) in a plain sentence.
inline std::string InviteAnswerText(const std::string& a) {
    if (a == "accepted") return "Invite accepted.";
    if (a == "declined") return "Invite declined.";
    if (a == "cancelled") return "Invite withdrawn.";
    if (a == "expired") return "This invite has expired. Ask for a new one.";
    if (a == "gone") return "This invite is no longer there.";
    if (a.compare(0, 8, "already_") == 0) return "This invite was already " + a.substr(8) + ".";
    const privacy::Rule r = privacy::ParseRule("okay_rule:" + a);
    if (r != privacy::Rule::None && r != privacy::Rule::Unknown) return privacy::RuleText(r);
    return "The invite could not be answered. Try again.";
}

/// The notification event for invites that are new since `seenId` and still worth showing at `now`
/// (pending, not expired), oldest first. The notification module decides how to show them.
inline std::vector<chat::Event> InviteEventsSince(const std::vector<GameInvite>& invites, long seenId, long now) {
    std::vector<chat::Event> out;
    for (auto it = invites.rbegin(); it != invites.rend(); ++it) {
        const GameInvite& g = *it;
        if (g.id <= seenId) continue;
        const InviteView v = ViewInvite(g, now, PresenceSeen::Unknown, "");
        if (!v.canAccept) continue;
        chat::Event e; e.kind = chat::Event::Kind::Invite;
        e.fromUid = g.senderId; e.fromName = okay::account::PublicName(g.senderName, g.senderId);
        e.text = v.text; e.peerUid = g.senderId; e.inviteId = g.id;
        out.push_back(e);
    }
    return out;
}

// ---- "Join game" on a friend --------------------------------------------------------------------

struct JoinCheck {
    bool ok = false;
    JoinLink link;         ///< when ok
    std::string text;      ///< why not, in words ("" when ok)
    std::string bareCode;  ///< an old launcher's bare relay code (can only be copied)
};
/// Whether "Join game" on `f` works now. `blocked` = either of you blocked the other.
inline JoinCheck CheckJoinFriend(const Friend& f, bool blocked) {
    JoinCheck c;
    const std::string name = f.display.empty() ? okay::account::PublicName(f.username, f.id) : f.display;
    if (blocked) { c.text = "You can't join " + name + ": one of you blocked the other."; return c; }
    if (f.status != "accepted") { c.text = "You can only join friends' games. Add " + name + " as a friend first."; return c; }
    if (!f.online) { c.text = name + " is offline."; return c; }
    if (f.sessionCode.empty()) { c.text = name + " isn't in a game you can join right now."; return c; }
    if (!JoinLink::Parse(f.sessionCode, c.link)) {
        c.bareCode = f.sessionCode;
        c.text = name + " uses an older launcher: copy the session code " + f.sessionCode + " and join with it in the game.";
        return c;
    }
    if (c.link.Full()) {
        c.text = name + "'s match is full (" + std::to_string(c.link.players) + "/" + std::to_string(c.link.maxPlayers) + ").";
        return c;
    }
    c.ok = true;
    return c;
}
/// For the friend list's Join button: shown enabled only when CheckJoinFriend would pass (not blocked).
inline bool Joinable(const Friend& f) { return CheckJoinFriend(f, false).ok; }

// ---- the client ------------------------------------------------------------------------------

/// game_invites over Supabase's REST API, through the signed-in AccountService (same pattern as Friends).
class Invites {
public:
    using Support = privacy::Support;
    explicit Invites(okay::account::AccountService& acct) : acct_(acct) {}

    /// Send an invite to a friend. Returns the new record's id; 0 on failure with `err` in words.
    /// Support() turns Missing when chat_invites.sql has not been run: send the chat message alone then.
    long Send(const std::string& recipientUid, const GameInvite& inv, std::string* err = nullptr) {
        if (!acct_.IsLoggedIn()) { if (err) *err = "Sign in first."; return 0; }
        if (recipientUid.empty() || inv.link.empty() || !ValidKind(inv.kind)) { if (err) *err = "That invite is not complete."; return 0; }
        auto r = acct_.Api("/rest/v1/game_invites", "POST", SendBody(recipientUid, inv), "Prefer: return=representation");
        Note(r);
        if (!r.ok) { if (err) *err = support_ == Support::Missing ? std::string() : privacy::FailureText(r, "The invite could not be sent."); return 0; }
        const auto rows = Friends::SplitObjects(r.body);
        return rows.empty() ? 0 : std::atol(Friends::Field(rows[0], "id").c_str());
    }
    /// Invites sent to me with an id above `sinceId`, newest first (pending and answered).
    std::vector<GameInvite> Incoming(long sinceId = 0, int limit = 30) {
        return List("&recipient=eq." + Friends::Url(acct_.UserId()) + "&id=gt." + std::to_string(sinceId), limit);
    }
    /// Invites I sent, newest first.
    std::vector<GameInvite> Sent(int limit = 30) { return List("&sender=eq." + Friends::Url(acct_.UserId()), limit); }
    /// One invite (an invite card looks up the record its link names). id 0 when not found or not mine.
    GameInvite Get(long id) {
        const auto v = List("&id=eq." + std::to_string(id), 1);
        return v.empty() ? GameInvite{} : v[0];
    }
    /// Accept or decline (recipient). Returns the server's answer word ("accepted", "expired", ...; see
    /// InviteAnswerText), or "" when the server was not reached / the SQL has not been run.
    std::string Respond(long id, bool accept) {
        return Rpc("respond_invite", "{\"p_id\":" + std::to_string(id) + ",\"p_accept\":" + (accept ? "true" : "false") + "}");
    }
    /// Withdraw an invite I sent.
    std::string Cancel(long id) { return Rpc("cancel_invite", "{\"p_id\":" + std::to_string(id) + "}"); }
    Support SupportState() const { return support_; }
    static const char* SetupNote() { return "Not set up on the server yet: the server owner must run docs/sql/chat_invites.sql once."; }

    // ---- pure helpers (public for the tests) ----
    static bool ValidKind(const std::string& k) { return k == "game" || k == "party" || k == "server" || k == "room"; }
    static std::string SendBody(const std::string& recipientUid, const GameInvite& inv) {
        std::string j = "{\"recipient\":" + Friends::JStr(recipientUid) + ",\"kind\":" + Friends::JStr(inv.kind) +
                        ",\"title\":" + Friends::JStr(inv.title.substr(0, 120)) + ",\"link\":" + Friends::JStr(inv.link) +
                        ",\"game_version\":" + Friends::JStr(inv.gameVersion.substr(0, 40)) + ",\"target\":" + Friends::JStr(inv.target);
        if (inv.expires > 0 && inv.created > 0 && inv.expires > inv.created)
            j += ",\"expires_at\":" + Friends::JStr(IsoAfter(inv.expires - inv.created));
        return j + "}";
    }
    static GameInvite FromRow(const std::string& o) {
        GameInvite g;   // dup-allow: column-to-field mapping of game_invites, not the reports table
        g.id = std::atol(Friends::Field(o, "id").c_str());
        g.senderId = Friends::Field(o, "sender"); g.recipientId = Friends::Field(o, "recipient");
        g.kind = Friends::Field(o, "kind"); g.title = Friends::Field(o, "title"); g.link = Friends::Field(o, "link");
        g.gameVersion = Friends::Field(o, "game_version"); g.target = Friends::Field(o, "target");
        g.status = Friends::Field(o, "status");
        g.created = Friends::ParseIso(Friends::Field(o, "created_at")); g.expires = Friends::ParseIso(Friends::Field(o, "expires_at"));
        return g;
    }
    /// An RPC returning text comes back as a JSON string ("\"accepted\""); strip the quotes.
    static std::string RpcAnswer(const std::string& body) {
        std::string b = body;
        while (!b.empty() && (b.back() == '\n' || b.back() == ' ')) b.pop_back();
        if (b.size() >= 2 && b.front() == '"' && b.back() == '"') b = b.substr(1, b.size() - 2);
        return b;
    }

private:
    okay::account::AccountService& acct_;
    Support support_ = Support::Unknown;

    void Note(const okay::account::ApiResponse& r) {
        if (r.ok) support_ = Support::Yes;
        else if (r.reached && (privacy::IsMissingTable(r.status, r.body) || r.body.find("PGRST202") != std::string::npos))
            support_ = Support::Missing;
    }
    std::vector<GameInvite> List(const std::string& filter, int limit) {
        std::vector<GameInvite> out;
        if (!acct_.IsLoggedIn()) return out;
        auto r = acct_.Api("/rest/v1/game_invites?select=*" + filter + "&order=id.desc&limit=" + std::to_string(limit), "GET");
        Note(r);
        if (!r.ok) return out;
        for (const auto& o : Friends::SplitObjects(r.body)) out.push_back(FromRow(o));
        return out;
    }
    std::string Rpc(const std::string& fn, const std::string& body) {
        if (!acct_.IsLoggedIn()) return {};
        auto r = acct_.Api("/rest/v1/rpc/" + fn, "POST", body);
        Note(r);
        if (!r.ok) {
            const privacy::Rule rule = privacy::ParseRule(r.body);
            return rule == privacy::Rule::RateLimited ? "rate_limited" : std::string();
        }
        return RpcAnswer(r.body);
    }
    // An expiry `secs` from now on this PC's clock; the server clamps it to 1 minute .. 24 hours of its own clock.
    static std::string IsoAfter(long secs) {
        std::time_t t = std::time(nullptr) + secs;
        std::tm tm{};
#if defined(_WIN32)
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        char buf[32]; std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
        return buf;
    }
};

}  // namespace okay
