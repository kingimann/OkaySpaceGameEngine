// The launcher's desktop notifications (launcher/DesktopNotify.hpp): which events pop up in the bottom-right corner,
// which stay in the launcher, which only go to the Notifications tab, bursts, rate limits, blocked senders, the queue
// (Windows shows one balloon at a time) and where a click on the pop-up goes. Also the balloon messages of the tray
// icon (launcher/TrayLogic.hpp ClassifyBalloonMessage) and the TrayController::Notify fallback. Headless; no Win32.
#include "DesktopNotify.hpp"
#include "TrayLogic.hpp"
#include <cstdint>
#include <cstdio>
#include <string>

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

using namespace okl::notify;

static Event Msg(const std::string& uid, const std::string& name, const std::string& text) {
    Event e; e.kind = Kind::Message; e.fromUid = uid; e.fromName = name; e.title = name; e.body = text;
    e.target.place = Place::Chat; e.target.id = uid; e.target.name = name;
    return e;
}
static Context Away() { Context c; c.trayIcon = true; return c; }   // the launcher is in the tray / not focused

static void TestDecide() {
    Settings s;
    const Event m = Msg("u-sam", "Sam", "hi");
    // In the tray (not focused): a desktop pop-up. In front and focused: the in-app toast instead.
    CHECK(Decide(s, m, Away()) == Route::Desktop);
    Context front = Away(); front.appFocused = true;
    CHECK(Decide(s, m, front) == Route::InApp);
    // Reading that very conversation: nothing at all.
    Context reading = front; reading.conversationFocused = true;
    CHECK(Decide(s, m, reading) == Route::Drop);
    // The conversation is "open" but the launcher is in the background: it still pops up.
    Context openBg = Away(); openBg.conversationFocused = false;
    CHECK(Decide(s, m, openBg) == Route::Desktop);
    // No tray icon (both tray options off, or the shell refused it): in-app only.
    Context noTray; noTray.trayIcon = false;
    CHECK(Decide(s, m, noTray) == Route::InApp);
    // Toggles per kind and the master switch: only recorded in the Notifications tab.
    Settings noMsg; noMsg.messages = false;
    CHECK(Decide(noMsg, m, Away()) == Route::TabOnly);
    CHECK(Decide(noMsg, m, front) == Route::TabOnly);
    Settings off; off.enabled = false;
    CHECK(Decide(off, m, Away()) == Route::TabOnly);
    Event fr; fr.kind = Kind::FriendRequest; fr.title = "Friend request"; fr.body = "Sam wants to be friends";
    Settings noFr; noFr.friendRequests = false;
    CHECK(Decide(s, fr, Away()) == Route::Desktop);
    CHECK(Decide(noFr, fr, Away()) == Route::TabOnly);
    CHECK(Decide(noFr, m, Away()) == Route::Desktop);   // the other kinds are untouched
    Event inv; inv.kind = Kind::Invite; Settings noInv; noInv.invites = false;
    CHECK(Decide(noInv, inv, Away()) == Route::TabOnly);
    Event up; up.kind = Kind::Update; Settings noUp; noUp.updates = false;
    CHECK(Decide(noUp, up, Away()) == Route::TabOnly);
    Event dl; dl.kind = Kind::Download; Settings noDl; noDl.downloads = false;
    CHECK(Decide(noDl, dl, Away()) == Route::TabOnly);
    CHECK(Decide(s, dl, Away()) == Route::Desktop);
    // Do not disturb (the setting) and the Invisible / Do not disturb presence: quiet, recorded only.
    Settings dnd; dnd.doNotDisturb = true;
    CHECK(Decide(dnd, m, Away()) == Route::TabOnly);
    CHECK(Decide(dnd, m, front) == Route::TabOnly);
    Context invisible = Away(); invisible.presence = kPresenceInvisible;
    CHECK(Decide(s, m, invisible) == Route::TabOnly);
    Context busy = Away(); busy.presence = kPresenceDoNotDisturb;
    CHECK(Decide(s, m, busy) == Route::TabOnly);
    Context away = Away(); away.presence = kPresenceAway;
    CHECK(Decide(s, m, away) == Route::Desktop);   // Away is not quiet
    CHECK(Quiet(dnd, kPresenceOnline) && Quiet(s, kPresenceInvisible) && !Quiet(s, kPresenceAway) && !Quiet(s, kPresenceOnline));
    // A blocked sender: dropped, never shown, not even in the tab (their messages are hidden in the chat too).
    Context blocked = Away(); blocked.senderBlocked = true;
    CHECK(Decide(s, m, blocked) == Route::Drop);
    Event bfr = fr; bfr.fromUid = "u-x";
    CHECK(Decide(s, bfr, blocked) == Route::Drop);
    // A muted conversation: recorded, no pop-up.
    Context muted = Away(); muted.conversationMuted = true;
    CHECK(Decide(s, m, muted) == Route::TabOnly);
    // The test button ignores the toggles and Do not disturb (so it can be tried), but not a missing tray icon.
    Event t; t.kind = Kind::Test; t.title = "Test"; t.body = "x";
    CHECK(Decide(off, t, Away()) == Route::Desktop);
    CHECK(Decide(dnd, t, invisible) == Route::Desktop);
    CHECK(Decide(s, t, front) == Route::Desktop);   // even in front: the point is to see the real pop-up
    CHECK(Decide(s, t, noTray) == Route::InApp);
    // Route names (for logs).
    CHECK(std::string(RouteName(Route::Desktop)) == "desktop" && std::string(RouteName(Route::Drop)) == "drop");
    // Kind toggles read back.
    CHECK(KindOn(s, Kind::Message) && !KindOn(noMsg, Kind::Message) && KindOn(noMsg, Kind::System));
}

static void TestSignalsAndClip() {
    CHECK(IsSignalMessage("[Call] abc key"));
    CHECK(IsSignalMessage("[CallEnd] abc 12"));
    CHECK(IsSignalMessage("[Invite] Star Arena okay://join"));
    CHECK(IsSignalMessage("[Party] p1"));
    CHECK(!IsSignalMessage("[Voice] https://x"));   // a voice note is a message (shown as "Voice message")
    CHECK(!IsSignalMessage("hello [Call] there"));
    CHECK(!IsSignalMessage(""));
    // Clip counts UTF-16 units like NOTIFYICONDATAW (szInfoTitle 64, szInfo 256), never cuts inside a character.
    CHECK(Clip("hello", 10) == "hello");
    CHECK(Clip("hello world", 8) == "hello w\xE2\x80\xA6");
    const std::string e3 = "\xC3\xA9\xC3\xA9\xC3\xA9";   // three e-acute
    CHECK(Clip(e3, 3) == e3);
    CHECK(Clip(e3, 2) == "\xC3\xA9\xE2\x80\xA6");
    const std::string emoji = "\xF0\x9F\x98\x80";        // one emoji = two UTF-16 units
    CHECK(Utf16Units(emoji) == 2 && Utf16Units(e3) == 3 && Utf16Units("abc") == 3);
    CHECK(Clip("a" + emoji + "b", 3) == "a\xE2\x80\xA6");   // the emoji does not fit with the ellipsis
    CHECK(Clip(std::string(300, 'x'), kMaxBody).size() <= (size_t)kMaxBody + 2);
    CHECK(Utf16Units(Clip(std::string(300, 'x'), kMaxBody)) == kMaxBody);
}

static void TestQueueAndBursts() {
    Centre c;
    uint64_t now = 100000;
    Popup p;
    CHECK(!c.Next(now, &p));   // nothing queued
    // One message: queued, then shown at once.
    PostResult r = c.Post(Msg("u-sam", "Sam", "hey"), Away(), now);
    CHECK(r.route == Route::Desktop && r.record);
    CHECK(c.Queued() == 1);
    CHECK(c.Next(now, &p));
    CHECK(p.title == "Sam" && p.body == "hey" && p.target.place == Place::Chat && p.target.id == "u-sam" && p.count == 1);
    CHECK(c.Showing());
    // Only one balloon at a time: a second event waits while the first shows.
    c.Post(Msg("u-ana", "Ana", "yo"), Away(), now + 100);
    CHECK(!c.Next(now + 200, &p));
    // A burst from Sam while Sam's pop-up was just shown: collapsed into one, shown after the burst gap.
    c.Post(Msg("u-sam", "Sam", "are you there"), Away(), now + 300);
    c.Post(Msg("u-sam", "Sam", "hello??"), Away(), now + 400);
    c.Post(Msg("u-sam", "Sam", "ok bye"), Away(), now + 500);
    CHECK(c.Queued() == 2);   // Ana's, and one for Sam's three
    c.OnClosed(now + 1000);
    CHECK(!c.Showing());
    CHECK(!c.Next(now + 1000 + kMinGapMs - 1, &p));   // the gap between two pop-ups
    CHECK(c.Next(now + 1000 + kMinGapMs, &p));
    CHECK(p.title == "Ana" && p.count == 1);   // Ana first: Sam's burst still inside its gap
    c.OnClosed(now + 3000);
    CHECK(!c.Next(now + 3000 + kMinGapMs, &p));   // Sam's burst gap (10 s from Sam's last pop-up) not over yet
    CHECK(c.Next(now + kBurstGapMs, &p));
    CHECK(p.title == "3 new messages from Sam" && p.body == "ok bye" && p.count == 3 && p.target.id == "u-sam");
    CHECK(c.Queued() == 0);
    // Windows does not always say the balloon went away: after kShowMs it counts as gone.
    c.Post(Msg("u-ana", "Ana", "again"), Away(), now + kBurstGapMs + 10);
    CHECK(!c.Next(now + kBurstGapMs + 20, &p));
    CHECK(c.Next(now + kBurstGapMs + kShowMs + kBurstGapMs, &p));
    CHECK(p.title == "Ana");

    // Not desktop: nothing queued; the result carries the texts for the toast / the tab.
    Centre d;
    Context front = Away(); front.appFocused = true;
    PostResult ir = d.Post(Msg("u-sam", "Sam", "hi"), front, 5);
    CHECK(ir.route == Route::InApp && ir.record && ir.title == "Sam" && ir.body == "hi" && d.Queued() == 0);
    Context blocked = Away(); blocked.senderBlocked = true;
    PostResult br = d.Post(Msg("u-bad", "Bad", "secret text"), blocked, 6);
    CHECK(br.route == Route::Drop && !br.record && br.body.empty() && br.title.empty() && d.Queued() == 0);
    d.settings.messages = false;
    PostResult tr = d.Post(Msg("u-sam", "Sam", "hi"), Away(), 7);
    CHECK(tr.route == Route::TabOnly && tr.record && d.Queued() == 0);
    Context reading = front; reading.conversationFocused = true;
    d.settings.messages = true;
    PostResult rr = d.Post(Msg("u-sam", "Sam", "hi"), reading, 8);
    CHECK(rr.route == Route::Drop && !rr.record);
}

static void TestOnceAndRateLimit() {
    Centre c;
    uint64_t t = 1000;
    Event up; up.kind = Kind::Update; up.title = "Update available"; up.body = "OkaySpace v2.1 is ready.";
    up.onceKey = "update:2.1"; up.target.place = Place::Update; up.target.id = "2.1";
    CHECK(c.Post(up, Away(), t).route == Route::Desktop);
    CHECK(c.Post(up, Away(), t + 1).route == Route::Drop);   // once per version
    CHECK(c.SeenOnce("update:2.1") && !c.SeenOnce("update:2.2"));
    Centre c2; c2.MarkSeenOnce("update:2.1");   // remembered across launches (the launcher saves it)
    CHECK(c2.Post(up, Away(), t).route == Route::Drop);
    Event up2 = up; up2.onceKey = "update:2.2";
    CHECK(c2.Post(up2, Away(), t).route == Route::Desktop);
    // A once-only event that was quiet (tab only) is still used up: it does not pop later.
    Centre c3; c3.settings.updates = false;
    CHECK(c3.Post(up, Away(), t).route == Route::TabOnly);
    c3.settings.updates = true;
    CHECK(c3.Post(up, Away(), t + 5).route == Route::Drop);

    // Rate limit: at most kMaxPerMinute pop-ups a minute, even when each one closes at once.
    Centre r;
    uint64_t now = 50000;
    int shown = 0;
    for (int i = 0; i < 20; ++i) {
        Event e; e.kind = Kind::System; e.title = "Item " + std::to_string(i); e.body = "b";
        e.burstKey = "sys:" + std::to_string(i);
        r.Post(e, Away(), now);
    }
    CHECK(r.Queued() == (size_t)kMaxQueue);   // the oldest are dropped past the queue size
    Popup p;
    for (int step = 0; step < 60; ++step) {   // 30 s, one look every 500 ms, each pop-up closed at once
        if (r.Next(now, &p)) { ++shown; r.OnClosed(now); }
        now += 500;
    }
    CHECK(shown == kMaxPerMinute);   // the rest wait for the minute to pass
    CHECK(r.Queued() == (size_t)(kMaxQueue - kMaxPerMinute));
    // The newest items are the ones kept (the oldest dropped).
    Centre q;
    for (int i = 0; i < kMaxQueue + 3; ++i) {
        Event e; e.kind = Kind::System; e.title = "N" + std::to_string(i); e.burstKey = "k" + std::to_string(i);
        q.Post(e, Away(), 10);
    }
    CHECK(q.Next(10, &p) && p.title == "N3");
}

static void TestClickTarget() {
    Centre c;
    Popup p;
    // Nothing shown yet: a click goes nowhere.
    CHECK(c.OnClicked(10).place == Place::None);
    Event inv; inv.kind = Kind::Invite; inv.title = "Maya invited you to play Star Arena"; inv.body = "Click to open the chat.";
    inv.fromUid = "u-maya"; inv.target.place = Place::Invite; inv.target.id = "77"; inv.target.name = "Maya";
    c.Post(inv, Away(), 100);
    CHECK(c.Next(100, &p));
    const Target tg = c.OnClicked(900);
    CHECK(tg.place == Place::Invite && tg.id == "77" && tg.name == "Maya");
    CHECK(!c.Showing());
    // The Action Center keeps old ones; a click there (after the balloon timed out) still goes to the last one shown.
    c.OnClosed(1000);
    CHECK(c.OnClicked(60000).place == Place::Invite);
    // A new pop-up replaces the remembered target.
    Event dl; dl.kind = Kind::Download; dl.title = "Download finished"; dl.target.place = Place::Versions; dl.target.id = "2.0";
    c.Post(dl, Away(), 70000);
    CHECK(c.Next(70000, &p) && p.target.place == Place::Versions);
    CHECK(c.OnClicked(70500).place == Place::Versions && c.OnClicked(70600).id == "2.0");
    // Default targets per kind when the caller gives none.
    Event fr; fr.kind = Kind::FriendRequest; fr.title = "Friend request";
    CHECK(ClickTargetOf(fr).place == Place::FriendRequests);
    Event up; up.kind = Kind::Update;
    CHECK(ClickTargetOf(up).place == Place::Update);
    Event m = Msg("u-sam", "Sam", "x"); m.target = Target();
    CHECK(ClickTargetOf(m).place == Place::Chat && ClickTargetOf(m).id == "u-sam");
    Event sys; sys.kind = Kind::System;
    CHECK(ClickTargetOf(sys).place == Place::Notifications);
    // Sound follows the setting (NIIF_NOSOUND when off).
    Centre s; s.settings.sound = false;
    s.Post(Msg("u-sam", "Sam", "x"), Away(), 5);
    CHECK(s.Next(5, &p) && !p.sound);
    Centre s2;
    s2.Post(Msg("u-sam", "Sam", "x"), Away(), 5);
    CHECK(s2.Next(5, &p) && p.sound);
    // Burst titles per kind.
    Event f1; f1.kind = Kind::FriendRequest; f1.title = "Friend request";
    CHECK(BurstTitle(f1, 1) == "Friend request" && BurstTitle(f1, 4) == "4 new friend requests");
    Event i1; i1.kind = Kind::Invite; i1.fromName = "Maya"; i1.title = "Maya invited you";
    CHECK(BurstTitle(i1, 2) == "2 invites from Maya");
    CHECK(BurstTitle(Msg("u", "Sam", "x"), 2) == "2 new messages from Sam");
    // A message with no name never shows an empty title.
    CHECK(BurstTitle(Msg("u", "", "x"), 1) == "New message" && BurstTitle(Msg("u", "", "x"), 3) == "3 new messages");
}

static void TestTrayBalloonMessages() {
    using namespace okaytray;
    // NIN_BALLOONSHOW 0x402, NIN_BALLOONHIDE 0x403, NIN_BALLOONTIMEOUT 0x404, NIN_BALLOONUSERCLICK 0x405 (WM_USER + 2..5).
    const auto ev = [](unsigned code) { return DecodeIconMessage(kIconVersion4, 0, (int64_t)(code | (1u << 16))).event; };
    CHECK(ClassifyBalloonMessage(ev(0x405)) == BalloonEvent::Clicked);
    CHECK(ClassifyBalloonMessage(ev(0x402)) == BalloonEvent::Shown);
    CHECK(ClassifyBalloonMessage(ev(0x403)) == BalloonEvent::Closed);
    CHECK(ClassifyBalloonMessage(ev(0x404)) == BalloonEvent::Closed);
    CHECK(ClassifyBalloonMessage(ev(kNinSelect)) == BalloonEvent::None);
    CHECK(ClassifyBalloonMessage(kWmContextMenu) == BalloonEvent::None);
    // The balloon messages never count as an icon click (no restore, no menu).
    CHECK(ClassifyIconMessage(0x405, kIconVersion4) == IconEvent::None);
    CHECK(ClassifyIconMessage(0x404, kIconVersion4) == IconEvent::None);

    struct Shell : TrayShell {
        bool icon = false; int notes = 0; bool lastSound = true; bool refuse = false; std::string title;
        bool AddIcon(const std::string&) override { icon = true; return true; }
        void RemoveIcon() override { icon = false; }
        bool IconAlive() override { return icon; }
        void HideWindow() override {}
        void ShowWindow() override {}
        int RunMenu(const std::vector<MenuItem>&) override { return kIdNone; }
        void Balloon(const std::string&, const std::string&) override {}
        bool Notify(const std::string& t, const std::string&, bool sound) override {
            if (refuse) return false;
            ++notes; lastSound = sound; title = t; return true;
        }
    } sh;
    TrayController off(&sh, true);
    off.settings.closeToTray = false; off.settings.minimizeToTray = false;
    off.Sync();
    CHECK(!off.IconShown() && !off.Notify("a", "b", true) && sh.notes == 0);   // no icon: the caller falls back to in-app
    TrayController on(&sh, true);
    on.Sync();
    CHECK(on.IconShown() && on.Notify("Sam", "hey", false) && sh.notes == 1 && !sh.lastSound && sh.title == "Sam");
    sh.refuse = true;
    CHECK(!on.Notify("Sam", "hey", true));
    TrayController unsupported(nullptr, false);
    CHECK(!unsupported.Notify("a", "b", true));
}

int main() {
    TestDecide();
    TestSignalsAndClip();
    TestQueueAndBursts();
    TestOnceAndRateLimit();
    TestClickTarget();
    TestTrayBalloonMessages();
    std::printf("launcher_notify: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
