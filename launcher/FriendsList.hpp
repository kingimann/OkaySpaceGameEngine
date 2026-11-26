#pragma once
// The friends list inside the launcher's chat window (the "Friends" side of its left column, in the launcher or popped
// out in its own window, launcher/ChatPlacement.hpp), with no window system: how friends are grouped and sorted, the
// line under each name, what the friend menu offers and why an item is greyed out, the unread badges, your own status
// (Online / Away / Invisible / Do not disturb) and the column's layout. DrawChatWindow in launcher/main.cpp draws it;
// tests/test_launcher_friends.cpp checks it on Linux. Written from the owner's request (2026-11-25: "make the friends pop
// out ... where they can chat, invite, etc"), in our own words; "like Steam" here means the kind of features only.
#include "ChatPlacement.hpp"   // the chat's fold rule (okaychat::ListFolded)
#include "okay/Core/StringUtil.hpp"   // okay::str::LowerAscii
#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>

namespace okayfriends {

// ---- Your own status (the launcher's g_onlineStatus, saved as online_status) ------------------------------------
/// 0 Online, 1 Away, 2 Invisible (friends see you as offline), 3 Do not disturb (online, no chat sounds).
constexpr int kMyStatusCount = 4;
constexpr int kOnline = 0, kAway = 1, kInvisible = 2, kDoNotDisturb = 3;
inline int ClampStatus(int s) { return s < 0 || s >= kMyStatusCount ? kOnline : s; }
inline const char* MyStatusLabel(int s) {
    switch (ClampStatus(s)) {
        case kAway: return "Away";
        case kInvisible: return "Invisible";
        case kDoNotDisturb: return "Do not disturb";
        default: return "Online";
    }
}
inline const char* MyStatusHint(int s) {
    switch (ClampStatus(s)) {
        case kAway: return "Shown as away";
        case kInvisible: return "Appear offline to everyone";
        case kDoNotDisturb: return "Shown as busy; no chat sounds";
        default: return "Friends see you're around";
    }
}
/// The labels for a Dear ImGui combo ("Online\0Away\0...\0").
constexpr const char kMyStatusCombo[] = "Online\0Away\0Invisible\0Do not disturb\0";
/// The presence text friends get for a status. `gameText` is a running game's rich presence ("" = none). Invisible
/// sends nothing (the caller goes offline instead). The Away and Do not disturb words are what PresenceOf reads back.
constexpr const char kAwayText[] = "Away", kBusyText[] = "Do not disturb", kIdleText[] = "In OkaySpace";
inline std::string MyPresenceText(int status, const std::string& gameText) {
    status = ClampStatus(status);
    if (status == kInvisible) return std::string();
    if (!gameText.empty()) return gameText;
    return status == kAway ? kAwayText : status == kDoNotDisturb ? kBusyText : kIdleText;
}
/// Do not disturb keeps the chat quiet (no chime for a new message).
inline bool QuietAlerts(int status) { return ClampStatus(status) == kDoNotDisturb; }

// ---- A friend's presence ----------------------------------------------------------------------------------------
enum class Presence { InGame, Online, Busy, Away, Offline };
/// From what the friend publishes (okay::Friend::online and ::playing). A blocked person always reads as offline.
inline Presence PresenceOf(bool online, const std::string& playing, bool blocked = false) {
    if (blocked || !online) return Presence::Offline;
    if (playing.rfind("Playing ", 0) == 0) return Presence::InGame;
    if (playing == kAwayText) return Presence::Away;
    if (playing == kBusyText) return Presence::Busy;
    return Presence::Online;
}

/// One person in the list (the launcher fills it from okay::Friend; `name` is already the public name or nickname).
enum class Kind { Friend, Incoming, Outgoing };
struct Person {
    std::string id, name, username, playing, sessionCode, customStatus, avatar;
    long rowId = 0, lastSeen = 0;
    bool online = false, blocked = false, favorite = false;
    int unread = 0;       // unread messages from them (0 = none; 1 when only "there is something new" is known)
    Kind kind = Kind::Friend;
    Presence presence() const { return PresenceOf(online, playing, blocked); }
    bool joinable() const { return kind == Kind::Friend && presence() != Presence::Offline && !sessionCode.empty(); }
};

/// The list's sections, top to bottom.
enum class Group { Requests, InGame, Online, Away, Offline, Sent };
inline const char* GroupTitle(Group g) {
    switch (g) {
        case Group::Requests: return "Friend requests";
        case Group::InGame: return "In a game";
        case Group::Online: return "Online";
        case Group::Away: return "Away";
        case Group::Offline: return "Offline";
        case Group::Sent: return "Sent requests";
    }
    return "";
}
inline Group GroupOf(const Person& p) {
    if (p.kind == Kind::Incoming) return Group::Requests;
    if (p.kind == Kind::Outgoing) return Group::Sent;
    switch (p.presence()) {
        case Presence::InGame: return Group::InGame;
        case Presence::Away: return Group::Away;
        case Presence::Offline: return Group::Offline;
        default: return Group::Online;   // Online and Do not disturb
    }
}
struct Section { Group group; std::vector<Person> people; };

/// The search box: a case-insensitive part of the name or the username ("" = everyone).
inline bool Matches(const Person& p, const std::string& query) {
    if (query.empty()) return true;
    const std::string q = okay::str::LowerAscii(query);
    return okay::str::LowerAscii(p.name).find(q) != std::string::npos || okay::str::LowerAscii(p.username).find(q) != std::string::npos;
}
/// The grouped, sorted list: empty sections are left out. In each section favourites come first, then people with
/// unread messages, then by name (A to Z, ignoring case); offline friends are ordered by when they were last online
/// (most recent first) after the favourites.
inline std::vector<Section> GroupFriends(const std::vector<Person>& all, const std::string& query) {
    const Group order[] = {Group::Requests, Group::InGame, Group::Online, Group::Away, Group::Offline, Group::Sent};
    std::vector<Section> out;
    for (Group g : order) {
        Section s{g, {}};
        for (const Person& p : all) if (GroupOf(p) == g && Matches(p, query)) s.people.push_back(p);
        if (s.people.empty()) continue;
        std::stable_sort(s.people.begin(), s.people.end(), [g](const Person& a, const Person& b) {
            if (a.favorite != b.favorite) return a.favorite;
            if ((a.unread > 0) != (b.unread > 0)) return a.unread > 0;
            if (g == Group::Offline && a.lastSeen != b.lastSeen) return a.lastSeen > b.lastSeen;
            const std::string la = okay::str::LowerAscii(a.name), lb = okay::str::LowerAscii(b.name);
            if (la != lb) return la < lb;
            return a.id < b.id;
        });
        out.push_back(std::move(s));
    }
    return out;
}
/// How many accepted friends are online (any presence but offline).
inline int OnlineCount(const std::vector<Person>& all) {
    int n = 0;
    for (const Person& p : all) if (p.kind == Kind::Friend && p.presence() != Presence::Offline) ++n;
    return n;
}

/// The line under a name. `offlineText` is the launcher's "Last online 3h ago" (or "" when unknown); `typing` = they
/// are typing to you right now.
inline std::string StatusLine(const Person& p, const std::string& offlineText, bool typing = false) {
    if (p.kind == Kind::Incoming) return "Wants to be your friend";
    if (p.kind == Kind::Outgoing) return "Request sent";
    if (p.blocked) return "Blocked";
    if (typing && p.presence() != Presence::Offline) return "typing\xE2\x80\xA6";
    switch (p.presence()) {
        case Presence::InGame: return p.playing;
        case Presence::Away: return p.customStatus.empty() ? "Away" : "Away \xC2\xB7 " + p.customStatus;
        case Presence::Busy: return "Do not disturb";
        case Presence::Online: return !p.customStatus.empty() ? p.customStatus : p.playing.empty() ? "Online" : p.playing;
        case Presence::Offline: break;
    }
    return offlineText.empty() ? "Offline" : offlineText;
}

/// Initials for a person with no avatar picture: the first letters of the first two words ("Maya Chen" -> "MC").
inline std::string Initials(const std::string& name) {
    std::string out;
    bool start = true;
    for (char c : name) {
        if (std::isspace((unsigned char)c) || c == '_' || c == '-' || c == '.') { start = true; continue; }
        if (start && std::isalnum((unsigned char)c)) { out += (char)std::toupper((unsigned char)c); if (out.size() == 2) break; }
        start = false;
    }
    return out.empty() ? "?" : out;
}

// ---- Unread badges ----------------------------------------------------------------------------------------------
/// The text in a badge: "" for none, the number, "99+" above 99.
inline std::string BadgeText(int n) { return n <= 0 ? std::string() : n > 99 ? std::string("99+") : std::to_string(n); }
/// A conversation's unread number. `somethingNew` is the conversation list's own rule (their newest message is newer
/// than what you saw, and the chat is not open); then the real count (okay::chat::CountUnread's perPeer), or 1 when the
/// message is older than the counted window. Not new: 0, whatever the count says.
inline int UnreadFor(const std::map<std::string, int>& perPeer, const std::string& key, bool somethingNew) {
    if (!somethingNew) return 0;
    const auto it = perPeer.find(key);
    return it != perPeer.end() && it->second > 0 ? it->second : 1;
}
/// The number on the Chat nav item and the tray's Friends item: the counted total when the rows have been read
/// (`known`), else the older "new since you last looked" count.
inline int NavUnread(bool known, int countedTotal, int fallback) { return known ? countedTotal : fallback; }
/// The total on the Friends switch: unread messages from friends (blocked people do not count).
inline int TotalUnread(const std::vector<Person>& all) {
    int n = 0;
    for (const Person& p : all) if (!p.blocked && p.unread > 0) n += p.unread;
    return n;
}

// ---- The friend menu (right-click, or the row's "..." button) ---------------------------------------------------
enum class Act { Message, InviteGame, InviteParty, Join, Profile, Accept, Decline, Cancel, Remove, Block, Unblock };
struct MenuItem {
    Act act = Act::Message;
    std::string label;
    bool enabled = true;
    std::string reason;          // why it is greyed out (shown as its tooltip)
    bool danger = false;         // drawn in the danger colour
    bool confirm = false;        // asks "are you sure?" first
    bool separatorBefore = false;
};
/// What the launcher knows about you when the menu opens.
struct Me {
    bool canInvite = false;      // you are in a joinable match (MpCanInvite)
    std::string gameTitle;       // its title (MpGameTitle)
    bool inParty = false;        // you have a party (MpPartyId non-empty)
};
inline MenuItem Item(Act a, const std::string& label, bool enabled = true, const std::string& reason = std::string()) {
    MenuItem m; m.act = a; m.label = label; m.enabled = enabled; if (!enabled) m.reason = reason; return m;
}
inline std::vector<MenuItem> FriendMenu(const Person& p, const Me& me) {
    std::vector<MenuItem> m;
    const bool offline = p.presence() == Presence::Offline;
    if (p.kind == Kind::Incoming) {
        m.push_back(Item(Act::Accept, "Accept friend request"));
        m.push_back(Item(Act::Decline, "Decline"));
        m.push_back(Item(Act::Profile, "View profile"));
    } else if (p.kind == Kind::Outgoing) {
        m.push_back(Item(Act::Cancel, "Cancel request"));
        m.push_back(Item(Act::Profile, "View profile"));
    } else {
        m.push_back(Item(Act::Message, "Send message", !p.blocked, "Unblock them to send a message"));
        {
            const std::string label = me.canInvite && !me.gameTitle.empty() ? "Invite to " + me.gameTitle : std::string("Invite to game");
            const char* why = p.blocked ? "They are blocked"
                            : !me.canInvite ? "Host or join a match in a game first"
                            : offline ? "They are offline" : "";
            m.push_back(Item(Act::InviteGame, label, *why == '\0', why));
        }
        m.push_back(Item(Act::InviteParty, me.inParty ? "Invite to party" : "Start a party with them", !p.blocked, "They are blocked"));
        {
            const char* why = p.blocked ? "They are blocked" : offline ? "They are offline"
                            : p.sessionCode.empty() ? "They are not in a game you can join" : "";
            m.push_back(Item(Act::Join, "Join game", *why == '\0', why));
        }
        MenuItem prof = Item(Act::Profile, "View profile"); prof.separatorBefore = true;
        m.push_back(prof);
        MenuItem rm = Item(Act::Remove, "Remove friend"); rm.danger = true; rm.confirm = true; rm.separatorBefore = true;
        m.push_back(rm);
    }
    MenuItem bl = Item(p.blocked ? Act::Unblock : Act::Block, p.blocked ? "Unblock" : "Block");
    bl.danger = !p.blocked; bl.confirm = !p.blocked;
    if (p.kind != Kind::Friend) bl.separatorBefore = true;
    m.push_back(bl);
    return m;
}
inline const MenuItem* FindItem(const std::vector<MenuItem>& m, Act a) {
    for (const MenuItem& it : m) if (it.act == a) return &it;
    return nullptr;
}
/// The question a confirmed item asks.
inline std::string ConfirmText(Act a, const std::string& name) {
    if (a == Act::Remove) return "Remove " + name + " from your friends? You can add them again later.";
    if (a == Act::Block) return "Block " + name + "? They can't message you, call you, send you friend requests or see your "
                                "status. You can unblock them in Settings > Privacy.";
    return std::string();
}

// ---- The column's layout ----------------------------------------------------------------------------------------
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
};
inline bool Overlap(const Rect& a, const Rect& b) {
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}
inline bool Inside(const Rect& inner, const Rect& outer) {
    return inner.x >= outer.x && inner.y >= outer.y && inner.right() <= outer.right() + 0.01f && inner.bottom() <= outer.bottom() + 0.01f;
}
/// The chat window's left column, in its own coordinates (0,0 = the column's top-left corner). `lineH` is the font's
/// line height and `frameH` a text field's height at the current UI scale (Dear ImGui's GetTextLineHeight and
/// GetFrameHeight); `scale` is the UI scale (1.0, 1.5 ...).
struct ColumnLayout {
    bool folded = false;       // only the avatar strip
    float listW = 0;           // the column's width; the conversation gets the rest of the window
    Rect header, modeSwitch, myStatus, search, list;
    float rowH = 0, sectionH = 0, avatar = 0;
};
constexpr float kMinThreadW = 300.0f;    // the conversation keeps at least this much (unscaled)
inline ColumnLayout LayoutColumn(float winW, float winH, float scale, float lineH, float frameH, bool friendsMode, bool userFolded) {
    ColumnLayout L;
    if (scale < 0.5f) scale = 0.5f;
    L.folded = okaychat::ListFolded(userFolded, winW / scale);   // the chat's fold rule, in unscaled pixels
    const float s = scale, pad = 10.0f * s;
    if (L.folded) L.listW = 76.0f * s;
    else if (friendsMode) L.listW = 300.0f * s;
    else L.listW = (winW / s < 640.0f ? 214.0f : 280.0f) * s;
    if (!L.folded) L.listW = std::min(L.listW, std::max(214.0f * s, winW - kMinThreadW * s));   // the conversation keeps its room
    L.listW = std::min(L.listW, winW);
    const float inner = L.listW - 2 * pad;
    float y = pad;
    L.header = {pad, y, inner, std::max(30.0f * s, lineH * 1.3f)};
    y = L.header.bottom() + 8.0f * s;
    if (!L.folded) {
        L.modeSwitch = {pad, y, inner, lineH + 12.0f * s};
        y = L.modeSwitch.bottom() + 8.0f * s;
        if (friendsMode) {
            L.myStatus = {pad, y, inner, lineH * 2 + 12.0f * s};
            y = L.myStatus.bottom() + 8.0f * s;
        }
        L.search = {pad, y, inner, frameH};
        y = L.search.bottom() + 8.0f * s;
    }
    L.list = {pad, y, inner, std::max(0.0f, winH - pad - y)};
    L.avatar = (L.folded ? 44.0f : 36.0f) * s;
    L.rowH = std::max(L.avatar + 12.0f * s, 2 * lineH + 16.0f * s);
    L.sectionH = lineH + 12.0f * s;
    return L;
}

} // namespace okayfriends
