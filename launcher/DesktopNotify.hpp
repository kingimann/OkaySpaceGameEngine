#pragma once
// Desktop notifications: the launcher's pop-ups in the bottom-right corner of the screen (owner, 2026-11-25: "we dont
// have notifications for like messages or updates, like notifications that pop up bottom right corner like every
// other application"). Part of the Launcher system (feature map 17.1.12). This header holds only the decisions, with
// no window system, so they are tested on Linux (tests/test_launcher_notify.cpp):
//  - for each event, whether it pops up on the desktop, shows only inside the launcher (the existing in-app toast,
//    when the launcher is in front and focused), only goes to the Notifications tab, or is dropped;
//  - the Settings > Chat & Notifications toggles per kind, the master switch, Do not disturb, and the Invisible / Do
//    not disturb presence;
//  - bursts collapse into one pop-up ("3 new messages from Sam"), pop-ups are rate limited, and only one shows at a
//    time (Windows shows one notify-icon balloon at a time), so the rest wait in a queue;
//  - where a click on the pop-up goes.
// On Windows the pop-up is the tray icon's balloon (Shell_NotifyIconW NIF_INFO, launcher/TrayWin32.cpp), which Windows
// 10 and 11 show as a notification in the corner and keep in the Action Center. The launcher's glue (okl::notify::Post,
// the pump and the click routing) is in launcher/main.cpp. Written from the owner's request and Microsoft's
// NOTIFYICONDATAW documentation (szInfoTitle holds 64 UTF-16 units, szInfo 256); no outside code.
#include "okay/Core/Utf8.hpp"   // okay::utf8::Next
#include "okay/Platform/ChatDelivery.hpp"   // okay::chat::Event: what the chat and invite code report
#include <cstddef>
#include <cstdint>
#include <deque>
#include <set>
#include <string>
#include <vector>

namespace okl {
namespace notify {

/// What happened. Each kind has its own switch in Settings (Test has none: the "Send a test notification" button).
enum class Kind : int { Message = 0, FriendRequest, Invite, Update, Download, System, Test };

/// Where a click on the pop-up (or its row in the Notifications tab) takes the person.
enum class Place : int {
    None = 0,
    Chat,            // the chat with a friend (id = their uid, name = their public name)
    GroupChat,       // a group chat (id = the group id)
    FriendRequests,  // the Friends page, requests
    Invite,          // a game or party invite (id = the sender's uid: the invite's Join is in the chat with them;
                     // "" = a party invite: the Multiplayer page; name = who sent it)
    Update,          // the launcher's own update (the update dialog)
    Versions,        // the Versions page (id = the editor version that was downloaded)
    Downloads,       // the Downloads page
    Notifications,   // the Notifications tab
    Settings,        // Settings > Chat & Notifications
};
struct Target {
    Place place = Place::None;
    std::string id;
    std::string name;
};

/// One thing that may deserve a pop-up.
struct Event {
    Kind kind = Kind::System;
    std::string title, body;   // as shown for one event ("Sam" / "are you there?")
    Target target;             // Place::None = the kind's default (ClickTargetOf)
    std::string fromUid;       // who caused it (messages, requests, invites): checked against the block list
    std::string fromName;      // their public name (okay::account::PublicName: never an email)
    std::string burstKey;      // events with the same key collapse into one pop-up; "" = BurstKeyOf
    std::string onceKey;       // non-empty: only the first event with this key is ever shown ("update:2.1")
    bool failure = false;      // something failed (a download, an update): the in-app toast is the red one
    std::string tabKey;        // its Notifications tab row; "" = "notify:" + the burst key. Give an existing row's key
                               // (an invite's "invite:<id>", an update's "update:<version>") so it is not listed twice
};

/// Settings > Chat & Notifications. Everything is on by default except Do not disturb.
struct Settings {
    bool enabled = true;          // "Desktop notifications" (the master switch)
    bool messages = true;
    bool friendRequests = true;
    bool invites = true;
    bool updates = true;
    bool downloads = true;
    bool doNotDisturb = false;    // nothing pops up; everything still goes to the Notifications tab
    bool sound = true;            // the Windows notification sound (NIIF_NOSOUND when off)
};
/// Whether the kind's own switch is on (System and Test have no switch).
inline bool KindOn(const Settings& s, Kind k) {
    switch (k) {
        case Kind::Message:       return s.messages;
        case Kind::FriendRequest: return s.friendRequests;
        case Kind::Invite:        return s.invites;
        case Kind::Update:        return s.updates;
        case Kind::Download:      return s.downloads;
        default:                  return true;
    }
}

/// The person's own online status, as the launcher keeps it (g_onlineStatus: 0 Online, 1 Away, 2 Invisible). 3 is
/// Do not disturb for when presence gets it.
constexpr int kPresenceOnline = 0, kPresenceAway = 1, kPresenceInvisible = 2, kPresenceDoNotDisturb = 3;
/// Quiet: Do not disturb is on, or the person is Invisible or Do not disturb (owner brief 2026-11-25).
inline bool Quiet(const Settings& s, int presence) {
    return s.doNotDisturb || presence == kPresenceInvisible || presence == kPresenceDoNotDisturb;
}

/// The launcher's state when the event arrives.
struct Context {
    bool appFocused = false;           // the launcher (or its chat window) is in front and has the keyboard
    bool conversationFocused = false;  // the message's conversation is open on screen in a focused window
    bool trayIcon = false;             // the tray icon is there (a balloon can be shown)
    int presence = kPresenceOnline;
    bool senderBlocked = false;        // fromUid is on the block list
    bool conversationMuted = false;    // the conversation is muted
};

enum class Route {
    Drop,      // nothing at all (a blocked sender, the conversation being read, a once-only event already shown)
    TabOnly,   // only the Notifications tab (switched off, quiet, muted)
    InApp,     // the launcher's own toast (it is in front, or there is no tray icon) and the tab
    Desktop,   // a desktop pop-up and the tab
};
inline const char* RouteName(Route r) {
    switch (r) {
        case Route::Drop:    return "drop";
        case Route::TabOnly: return "tab";
        case Route::InApp:   return "in-app";
        case Route::Desktop: return "desktop";
    }
    return "?";
}

/// The decision for one event (no queue, no once-only memory: Centre::Post adds those).
inline Route Decide(const Settings& s, const Event& ev, const Context& c) {
    if (c.senderBlocked) return Route::Drop;   // never show what a blocked person sent, not even in the tab
    if (ev.kind == Kind::Test) return c.trayIcon ? Route::Desktop : Route::InApp;   // the button shows the real thing
    if (ev.kind == Kind::Message && c.conversationFocused && c.appFocused) return Route::Drop;   // already reading it
    if (!s.enabled || !KindOn(s, ev.kind) || Quiet(s, c.presence) || c.conversationMuted) return Route::TabOnly;
    if (c.appFocused || !c.trayIcon) return Route::InApp;
    return Route::Desktop;
}

/// Chat messages that are signals, not text: calls ring on their own, invites have their own pop-up.
inline bool IsSignalMessage(const std::string& body) {
    for (const char* p : {"[Call] ", "[CallEnd] ", "[Invite] ", "[Party] "})
        if (body.rfind(p, 0) == 0) return true;
    return false;
}

// ---- Text limits ----------------------------------------------------------------------------------------------------
/// NOTIFYICONDATAW: szInfoTitle is 64 UTF-16 units and szInfo 256, each with the terminating zero.
constexpr int kMaxTitle = 63, kMaxBody = 255;
/// The length of a UTF-8 string in UTF-16 units (a character above U+FFFF takes two). Decoded with okay::utf8::Next.
inline int Utf16Units(const std::string& s) {
    int n = 0;
    for (size_t i = 0; i < s.size();) n += okay::utf8::Next(s, i) > 0xFFFFu ? 2 : 1;
    return n;
}
/// `s` cut to at most `maxUnits` UTF-16 units, ending with an ellipsis when cut; never inside a character.
inline std::string Clip(const std::string& s, int maxUnits) {
    if (Utf16Units(s) <= maxUnits) return s;
    int n = 0;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = i;
        const int units = okay::utf8::Next(s, j) > 0xFFFFu ? 2 : 1;
        if (n + units > maxUnits - 1) break;   // room for the ellipsis (one unit)
        n += units;
        i = j;
    }
    return s.substr(0, i) + "\xE2\x80\xA6";
}

// ---- Bursts and targets ---------------------------------------------------------------------------------------------
/// The default burst key: one per conversation for messages, one for all friend requests, one per sender for
/// invites, and otherwise one per title (so different updates or downloads never merge).
inline std::string BurstKeyOf(const Event& ev) {
    if (!ev.burstKey.empty()) return ev.burstKey;
    switch (ev.kind) {
        case Kind::Message:       return "msg:" + (ev.target.id.empty() ? ev.fromUid : ev.target.id);
        case Kind::FriendRequest: return "friends";
        case Kind::Invite:        return "invite:" + ev.fromUid;
        default:                  return "k" + std::to_string((int)ev.kind) + ":" + ev.title;
    }
}
/// The title of a pop-up that stands for `count` events like `first`.
inline std::string BurstTitle(const Event& first, int count) {
    const std::string n = std::to_string(count);
    switch (first.kind) {
        case Kind::Message:
            if (count <= 1) return first.title.empty() ? (first.fromName.empty() ? std::string("New message") : first.fromName) : first.title;
            return first.fromName.empty() ? n + " new messages" : n + " new messages from " + first.fromName;
        case Kind::FriendRequest:
            return count <= 1 ? first.title : n + " new friend requests";
        case Kind::Invite:
            if (count <= 1) return first.title;
            return first.fromName.empty() ? n + " new invites" : n + " invites from " + first.fromName;
        default:
            return first.title;
    }
}
/// Where a click goes when the event gave no target.
inline Target ClickTargetOf(const Event& ev) {
    if (ev.target.place != Place::None) return ev.target;
    Target t;
    switch (ev.kind) {
        case Kind::Message:       t.place = Place::Chat; t.id = ev.fromUid; t.name = ev.fromName; break;
        case Kind::FriendRequest: t.place = Place::FriendRequests; break;
        case Kind::Invite:        t.place = Place::Invite; t.name = ev.fromName; break;
        case Kind::Update:        t.place = Place::Update; break;
        case Kind::Download:      t.place = Place::Downloads; break;
        case Kind::Test:          t.place = Place::Settings; break;
        default:                  t.place = Place::Notifications; break;
    }
    return t;
}

// ---- The queue ------------------------------------------------------------------------------------------------------
constexpr uint64_t kMinGapMs = 1500;     // between two pop-ups
constexpr uint64_t kBurstGapMs = 10000;  // between two pop-ups of the same burst key (the rest collapse into one)
constexpr uint64_t kShowMs = 8000;       // a pop-up counts as gone after this, if Windows never said so
constexpr int kMaxPerMinute = 6;         // pop-ups in any 60 s
constexpr int kMaxQueue = 12;            // waiting pop-ups; past this the oldest is dropped (it is in the tab)

/// One desktop pop-up, ready to show.
struct Popup {
    std::string title, body;
    Target target;
    Kind kind = Kind::System;
    std::string burstKey;
    int count = 1;
    bool sound = true;
    uint64_t queuedAt = 0;
};
/// What Post decided, with the texts to put in the toast or the Notifications tab (empty when dropped).
struct PostResult {
    Route route = Route::Drop;
    bool record = false;   // add it to the Notifications tab (every route but Drop)
    std::string title, body;
    Target target;
};

/// The notification centre: one per launcher, used on the main thread only.
class Centre {
public:
    Settings settings;

    /// Decide what `ev` does; a desktop one is queued (merged into a waiting one with the same burst key).
    PostResult Post(const Event& ev, const Context& c, uint64_t nowMs) {
        PostResult r;
        if (!ev.onceKey.empty() && m_once.count(ev.onceKey)) return r;   // shown (or recorded) before
        r.route = Decide(settings, ev, c);
        if (r.route == Route::Drop) return r;
        if (!ev.onceKey.empty()) m_once.insert(ev.onceKey);
        r.record = true;
        r.title = BurstTitle(ev, 1);
        r.body = ev.body;
        r.target = ClickTargetOf(ev);
        if (r.route != Route::Desktop) return r;
        const std::string key = BurstKeyOf(ev);
        for (Pending& p : m_queue)
            if (p.popup.burstKey == key) {   // collapse: one pop-up for the burst, with the newest text
                ++p.popup.count;
                p.popup.title = Clip(BurstTitle(p.first, p.popup.count), kMaxTitle);
                p.popup.body = Clip(ev.body, kMaxBody);
                p.popup.target = ClickTargetOf(ev);
                p.popup.sound = settings.sound;
                return r;
            }
        Pending p;
        p.first = ev;
        p.popup.title = Clip(r.title, kMaxTitle);
        p.popup.body = Clip(ev.body, kMaxBody);
        p.popup.target = r.target;
        p.popup.kind = ev.kind;
        p.popup.burstKey = key;
        p.popup.sound = settings.sound;
        p.popup.queuedAt = nowMs;
        m_queue.push_back(p);
        while ((int)m_queue.size() > kMaxQueue) m_queue.pop_front();
        return r;
    }
    /// The pop-up to show now, if any (call every pass of the main loop). Only one shows at a time; there is a gap
    /// between two, a longer one between two of the same burst, and at most kMaxPerMinute a minute.
    bool Next(uint64_t nowMs, Popup* out) {
        if (m_showing && nowMs - m_shownAt >= kShowMs) { m_showing = false; m_endedAt = m_shownAt + kShowMs; }
        if (m_showing || m_queue.empty()) return false;
        if (m_any && nowMs - m_endedAt < kMinGapMs) return false;
        while (!m_recent.empty() && nowMs - m_recent.front() >= 60000) m_recent.pop_front();
        if ((int)m_recent.size() >= kMaxPerMinute) return false;
        for (auto it = m_queue.begin(); it != m_queue.end(); ++it) {
            bool wait = false;
            for (const auto& ks : m_keyShown)
                if (ks.first == it->popup.burstKey && nowMs - ks.second < kBurstGapMs) wait = true;
            if (wait) continue;
            if (out) *out = it->popup;
            m_last = it->popup.target;
            SetKeyShown(it->popup.burstKey, nowMs);
            m_queue.erase(it);
            m_showing = true; m_any = true; m_shownAt = nowMs;
            m_recent.push_back(nowMs);
            return true;
        }
        return false;
    }
    /// Windows said the balloon went away (NIN_BALLOONHIDE / NIN_BALLOONTIMEOUT), or showing it failed.
    void OnClosed(uint64_t nowMs) {
        if (!m_showing) return;
        m_showing = false;
        m_endedAt = nowMs;
    }
    /// The person clicked the pop-up (NIN_BALLOONUSERCLICK): where to go. Windows does not say which balloon, so it
    /// is the last one shown (also for a click in the Action Center after it timed out). Place::None before any.
    Target OnClicked(uint64_t nowMs) {
        OnClosed(nowMs);
        return m_last;
    }
    bool Showing() const { return m_showing; }
    size_t Queued() const { return m_queue.size(); }
    /// Once-only keys ("update:2.1"); the launcher saves the update one so a version pops up once, not every start.
    bool SeenOnce(const std::string& key) const { return m_once.count(key) != 0; }
    void MarkSeenOnce(const std::string& key) { if (!key.empty()) m_once.insert(key); }
    /// Drop everything waiting (signed out).
    void Clear() { m_queue.clear(); }

private:
    struct Pending { Event first; Popup popup; };
    void SetKeyShown(const std::string& key, uint64_t now) {
        for (auto& ks : m_keyShown) if (ks.first == key) { ks.second = now; return; }
        m_keyShown.push_back({key, now});
        if (m_keyShown.size() > 64) m_keyShown.erase(m_keyShown.begin());
    }
    std::deque<Pending> m_queue;
    std::deque<uint64_t> m_recent;                             // when the pop-ups of the last minute were shown
    std::vector<std::pair<std::string, uint64_t>> m_keyShown;  // burst key -> when its last pop-up was shown
    std::set<std::string> m_once;
    Target m_last;
    bool m_showing = false, m_any = false;
    uint64_t m_shownAt = 0, m_endedAt = 0;
};

// ---- Chat and invite events ----------------------------------------------------------------------------------------
/// The Notifications tab row of a game invite: one row per invite record when the card has one (the record poll, the
/// chat message's pop-up and the in-app "X invited you" card all use it), else one per chat message.
inline std::string InviteTabKey(long messageId, long inviteId) {
    return inviteId > 0 ? "invite:r" + std::to_string(inviteId) : "invite:" + std::to_string(messageId);
}
/// The once-only key of an invite record: the record poll and the chat message's pop-up never both show it.
inline std::string InviteOnceKey(long inviteId) { return inviteId > 0 ? "invite:" + std::to_string(inviteId) : std::string(); }
/// An okay::chat::Event as a notification. `preview` is the message as the chat list shows it (a photo says "Photo");
/// `groupTitle` names the group of a GroupMessage ("" = "a group").
inline Event FromChatEvent(const okay::chat::Event& e, const std::string& preview = std::string(), const std::string& groupTitle = std::string()) {
    using K = okay::chat::Event::Kind;
    Event ev;
    const std::string who = e.fromName.empty() ? std::string("Someone") : e.fromName;
    switch (e.kind) {
        case K::Message:
            ev.kind = Kind::Message; ev.title = who; ev.body = preview.empty() ? e.text : preview;
            ev.fromUid = e.fromUid; ev.fromName = e.fromName;
            ev.target = {Place::Chat, e.peerUid.empty() ? e.fromUid : e.peerUid, e.fromName};
            ev.tabKey = "msg:" + ev.target.id;
            break;
        case K::GroupMessage: {
            const std::string g = groupTitle.empty() ? std::string("a group") : groupTitle;
            ev.kind = Kind::Message; ev.title = who + " in " + g; ev.body = preview.empty() ? e.text : preview;
            ev.fromUid = e.fromUid; ev.fromName = g;   // a burst reads "3 new messages from <group>"
            ev.target = {Place::GroupChat, std::to_string(e.groupId), g};
            ev.burstKey = "group:" + std::to_string(e.groupId);
            ev.tabKey = "group:" + std::to_string(e.groupId);
            break;
        }
        case K::Invite:
            ev.kind = Kind::Invite; ev.title = e.text.empty() ? who + " invited you" : e.text;
            ev.body = "Open the chat with " + who + " to accept or decline.";
            ev.fromUid = e.fromUid; ev.fromName = e.fromName;
            ev.target = {Place::Invite, e.peerUid.empty() ? e.fromUid : e.peerUid, e.fromName};
            ev.onceKey = InviteOnceKey(e.inviteId);
            ev.tabKey = InviteTabKey(e.messageId, e.inviteId);
            break;
        case K::InviteAnswered:
            ev.kind = Kind::Invite; ev.title = e.text.empty() ? who + " answered your invite" : e.text;
            ev.fromUid = e.fromUid; ev.fromName = e.fromName;
            ev.target = {Place::Chat, e.peerUid.empty() ? e.fromUid : e.peerUid, e.fromName};
            ev.burstKey = "invans:" + std::to_string(e.inviteId);
            ev.onceKey = "invite-answer:" + std::to_string(e.inviteId);
            ev.tabKey = "invans:" + std::to_string(e.inviteId);
            break;
        case K::JoinRequest:
            ev.kind = Kind::Invite; ev.title = who + " asked to join your game"; ev.body = e.text;
            ev.fromUid = e.fromUid; ev.fromName = e.fromName;
            ev.target = {Place::Chat, e.peerUid.empty() ? e.fromUid : e.peerUid, e.fromName};
            break;
    }
    return ev;
}

// ---- The launcher's entry points (launcher/main.cpp) ----------------------------------------------------------------
/// THE way to raise a notification from anywhere in the launcher (chat, invites, friends, updates, downloads): reads
/// the launcher's state (focus, tray icon, presence, block and mute lists, the open conversation), decides with the
/// Centre, adds it to the Notifications tab, shows the in-app toast or queues the desktop pop-up. Main thread only.
PostResult Post(const Event& ev);
/// The short form: kind, title, body and where a click goes.
PostResult Post(Kind kind, const std::string& title, const std::string& body, const Target& target = Target(),
                const std::string& fromUid = std::string(), const std::string& fromName = std::string());

} // namespace notify
} // namespace okl
