// The chat window's Friends list (launcher/FriendsList.hpp): grouping and sorting, the search box, the line under each
// name, the friend menu with its greyed-out reasons, unread badges, your own status (with Do not disturb), and the
// column's layout with no overlaps at 960x600 and 1280x800, UI scale 1.0 and 1.5. Headless; no window system.
#include "FriendsList.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

using namespace okayfriends;

static Person P(const char* id, const char* name, bool online, const char* playing = "", Kind k = Kind::Friend) {
    Person p; p.id = id; p.name = name; p.username = okay::str::LowerAscii(name); p.online = online; p.playing = playing; p.kind = k; return p;
}

static void TestPresence() {
    CHECK(PresenceOf(true, "Playing Neon Drift") == Presence::InGame);
    CHECK(PresenceOf(true, "Playing Arena \xC2\xB7 Hosting \xC2\xB7 2/8") == Presence::InGame);
    CHECK(PresenceOf(true, "In OkaySpace") == Presence::Online);
    CHECK(PresenceOf(true, "") == Presence::Online);
    CHECK(PresenceOf(true, "In the editor") == Presence::Online);
    CHECK(PresenceOf(true, kAwayText) == Presence::Away);
    CHECK(PresenceOf(true, kBusyText) == Presence::Busy);
    CHECK(PresenceOf(false, "Playing Neon Drift") == Presence::Offline);   // stale text from an offline friend
    CHECK(PresenceOf(true, "Playing X", true) == Presence::Offline);        // blocked: never shown as online
    CHECK(PresenceOf(true, "Displaying") == Presence::Online);              // only the "Playing " prefix counts
}

static void TestGrouping() {
    std::vector<Person> all = {
        P("1", "zed", false), P("2", "Maya Chen", true, "Playing Neon Drift"), P("3", "alex", true),
        P("4", "Bo", true, "Away"), P("5", "kai", true, "", Kind::Incoming), P("6", "Zoe", false, "", Kind::Outgoing),
        P("7", "Ann", true, "Do not disturb"), P("8", "carl", false), P("9", "Ben", true, "Playing Kart Rush"),
    };
    all[0].lastSeen = 100; all[7].lastSeen = 500;      // carl was online more recently than zed
    std::vector<Section> s = GroupFriends(all, "");
    CHECK(s.size() == 6);
    CHECK(s[0].group == Group::Requests && s[0].people.size() == 1 && s[0].people[0].id == "5");
    CHECK(s[1].group == Group::InGame && s[1].people.size() == 2);
    CHECK(s[1].people[0].name == "Ben" && s[1].people[1].name == "Maya Chen");   // A to Z
    CHECK(s[2].group == Group::Online && s[2].people.size() == 2);
    CHECK(s[2].people[0].name == "alex" && s[2].people[1].name == "Ann");       // case ignored; Do not disturb is online
    CHECK(s[3].group == Group::Away && s[3].people[0].name == "Bo");
    CHECK(s[4].group == Group::Offline && s[4].people[0].name == "carl" && s[4].people[1].name == "zed");   // most recent first
    CHECK(s[5].group == Group::Sent && s[5].people[0].name == "Zoe");
    CHECK(std::string(GroupTitle(Group::InGame)) == "In a game" && std::string(GroupTitle(Group::Offline)) == "Offline");
    // Favourites first, then unread, then the name.
    all[2].favorite = true;    // alex stays first
    all[6].unread = 3;         // Ann has unread: before alex? no: favourites win
    s = GroupFriends(all, "");
    CHECK(s[2].people[0].name == "alex" && s[2].people[1].name == "Ann");
    all[2].favorite = false;
    s = GroupFriends(all, "");
    CHECK(s[2].people[0].name == "Ann");   // unread before the name order
    // Search: name or username, any case, part of a word; empty sections are dropped.
    s = GroupFriends(all, "MAY");
    CHECK(s.size() == 1 && s[0].group == Group::InGame && s[0].people[0].id == "2");
    s = GroupFriends(all, "nobody");
    CHECK(s.empty());
    Person u = P("10", "Display Name", true); u.username = "secret_handle";
    CHECK(Matches(u, "handle") && Matches(u, "display") && !Matches(u, "zzz") && Matches(u, ""));
    CHECK(OnlineCount(all) == 5);   // in a game 2, online 2 (one busy), away 1; requests do not count
}

static void TestStatusLine() {
    Person p = P("1", "Maya", true, "Playing Neon Drift");
    CHECK(StatusLine(p, "") == "Playing Neon Drift");
    p.playing = ""; CHECK(StatusLine(p, "") == "Online");
    p.customStatus = "Tileset day"; CHECK(StatusLine(p, "") == "Tileset day");
    p.playing = "Away"; CHECK(StatusLine(p, "") == "Away \xC2\xB7 Tileset day");
    p.playing = "Do not disturb"; CHECK(StatusLine(p, "") == "Do not disturb");
    p.online = false; CHECK(StatusLine(p, "") == "Offline");
    CHECK(StatusLine(p, "Last online 3h ago") == "Last online 3h ago");
    CHECK(StatusLine(p, "", true) == "Offline");          // no typing shown for someone offline
    p.online = true; p.playing = ""; p.customStatus.clear();
    CHECK(StatusLine(p, "", true) == "typing\xE2\x80\xA6");
    p.blocked = true; CHECK(StatusLine(p, "") == "Blocked");
    CHECK(StatusLine(P("2", "kai", true, "", Kind::Incoming), "") == "Wants to be your friend");
    CHECK(StatusLine(P("3", "zoe", false, "", Kind::Outgoing), "") == "Request sent");
    CHECK(Initials("Maya Chen") == "MC" && Initials("kai_dev") == "KD" && Initials("alex") == "A");
    CHECK(Initials("") == "?" && Initials("  ") == "?" && Initials("a b c") == "AB");
}

static void TestMenu() {
    Me me;   // not in a game, no party
    Person p = P("1", "Maya", true);
    std::vector<MenuItem> m = FriendMenu(p, me);
    const MenuItem* msg = FindItem(m, Act::Message);
    CHECK(msg && msg->enabled && msg->label == "Send message");
    const MenuItem* inv = FindItem(m, Act::InviteGame);
    CHECK(inv && !inv->enabled && inv->label == "Invite to game" && inv->reason == "Host or join a match in a game first");
    const MenuItem* join = FindItem(m, Act::Join);
    CHECK(join && !join->enabled && join->reason == "They are not in a game you can join");
    const MenuItem* party = FindItem(m, Act::InviteParty);
    CHECK(party && party->enabled && party->label == "Start a party with them");
    CHECK(FindItem(m, Act::Profile) && FindItem(m, Act::Profile)->separatorBefore);
    const MenuItem* rm = FindItem(m, Act::Remove);
    CHECK(rm && rm->danger && rm->confirm);
    const MenuItem* bl = FindItem(m, Act::Block);
    CHECK(bl && bl->danger && bl->confirm && bl->label == "Block" && &m.back() == bl);
    CHECK(!FindItem(m, Act::Accept) && !FindItem(m, Act::Unblock));
    // In a joinable match yourself: invite works; they are hosting: join works.
    me.canInvite = true; me.gameTitle = "Star Arena"; me.inParty = true;
    p.sessionCode = "okay://join?game=Neon%20Drift&code=ABC";
    p.playing = "Playing Neon Drift";
    m = FriendMenu(p, me);
    CHECK(FindItem(m, Act::InviteGame)->enabled && FindItem(m, Act::InviteGame)->label == "Invite to Star Arena");
    CHECK(FindItem(m, Act::InviteGame)->reason.empty());
    CHECK(FindItem(m, Act::Join)->enabled && p.joinable());
    CHECK(FindItem(m, Act::InviteParty)->label == "Invite to party");
    // Offline: neither invite nor join, with the reason.
    p.online = false;
    m = FriendMenu(p, me);
    CHECK(!FindItem(m, Act::InviteGame)->enabled && FindItem(m, Act::InviteGame)->reason == "They are offline");
    CHECK(!FindItem(m, Act::Join)->enabled && FindItem(m, Act::Join)->reason == "They are offline" && !p.joinable());
    CHECK(FindItem(m, Act::Message)->enabled);   // a message waits for them
    // Blocked: no message, no invite; Unblock (no confirm) instead of Block.
    p.online = true; p.blocked = true;
    m = FriendMenu(p, me);
    CHECK(!FindItem(m, Act::Message)->enabled && FindItem(m, Act::Message)->reason == "Unblock them to send a message");
    CHECK(!FindItem(m, Act::InviteGame)->enabled && !FindItem(m, Act::Join)->enabled && !FindItem(m, Act::InviteParty)->enabled);
    CHECK(FindItem(m, Act::Unblock) && !FindItem(m, Act::Unblock)->confirm && !FindItem(m, Act::Block));
    // Requests.
    m = FriendMenu(P("2", "kai", true, "", Kind::Incoming), me);
    CHECK(m.size() == 4 && m[0].act == Act::Accept && m[1].act == Act::Decline && m[2].act == Act::Profile && m[3].act == Act::Block);
    CHECK(m[3].separatorBefore && !FindItem(m, Act::Message) && !FindItem(m, Act::Remove));
    m = FriendMenu(P("3", "zoe", false, "", Kind::Outgoing), me);
    CHECK(m[0].act == Act::Cancel && FindItem(m, Act::Profile) && !FindItem(m, Act::InviteGame));
    // Every greyed-out item says why; no enabled item carries a reason.
    for (const MenuItem& it : FriendMenu(P("4", "x", false), Me())) CHECK(it.enabled == it.reason.empty());
    CHECK(ConfirmText(Act::Remove, "Maya").find("Remove Maya") == 0);
    CHECK(ConfirmText(Act::Block, "Maya").find("Block Maya?") == 0);
    CHECK(ConfirmText(Act::Message, "Maya").empty());
}

static void TestUnread() {
    CHECK(BadgeText(0).empty() && BadgeText(-2).empty() && BadgeText(1) == "1" && BadgeText(99) == "99" && BadgeText(100) == "99+");
    std::vector<Person> all = {P("1", "a", true), P("2", "b", false), P("3", "c", true)};
    all[0].unread = 2; all[1].unread = 1; all[2].unread = 5; all[2].blocked = true;
    CHECK(TotalUnread(all) == 3);   // blocked people do not count
}

static void TestMyStatus() {
    CHECK(kMyStatusCount == 4);
    CHECK(std::string(MyStatusLabel(0)) == "Online" && std::string(MyStatusLabel(1)) == "Away");
    CHECK(std::string(MyStatusLabel(2)) == "Invisible" && std::string(MyStatusLabel(3)) == "Do not disturb");
    CHECK(std::string(MyStatusLabel(9)) == "Online" && ClampStatus(-1) == 0 && ClampStatus(3) == 3);
    for (int s = 0; s < kMyStatusCount; ++s) CHECK(MyStatusHint(s)[0] != '\0');
    // The combo string holds the four labels in order.
    const char* p = kMyStatusCombo; int n = 0;
    while (*p) { CHECK(std::string(p) == MyStatusLabel(n)); p += std::string(p).size() + 1; ++n; }
    CHECK(n == kMyStatusCount);
    // What friends get, and that it reads back as the same presence.
    CHECK(MyPresenceText(kOnline, "") == "In OkaySpace" && PresenceOf(true, MyPresenceText(kOnline, "")) == Presence::Online);
    CHECK(PresenceOf(true, MyPresenceText(kAway, "")) == Presence::Away);
    CHECK(PresenceOf(true, MyPresenceText(kDoNotDisturb, "")) == Presence::Busy);
    CHECK(MyPresenceText(kInvisible, "Playing X").empty());
    CHECK(MyPresenceText(kDoNotDisturb, "Playing X") == "Playing X");   // a running game's presence wins, as before
    CHECK(QuietAlerts(kDoNotDisturb) && !QuietAlerts(kOnline) && !QuietAlerts(kAway) && !QuietAlerts(kInvisible));
}

// The column at the window sizes the owner uses, with the fonts' real heights at each scale (Roboto 17 px: line height
// about 17.5 px, a text field about 32 px at scale 1.0).
static void TestLayout() {
    struct Size { float w, h; } sizes[] = {{960, 600}, {1280, 800}, {760, 560}};
    for (Size sz : sizes) for (float scale : {1.0f, 1.5f}) for (int mode = 0; mode < 2; ++mode) {
        if (sz.w / scale < 600.0f) continue;   // folds (checked below)
        const float lineH = 17.5f * scale, frameH = 32.0f * scale;
        const ColumnLayout L = LayoutColumn(sz.w, sz.h, scale, lineH, frameH, mode == 1, false);
        const Rect col{0, 0, L.listW, sz.h};
        CHECK(!L.folded);   // all of these are wide enough for the list
        CHECK(sz.w - L.listW >= kMinThreadW * scale - 0.01f);   // the conversation keeps its room
        std::vector<Rect> parts = {L.header, L.modeSwitch, L.search, L.list};
        if (mode == 1) parts.push_back(L.myStatus);
        for (size_t i = 0; i < parts.size(); ++i) {
            CHECK(Inside(parts[i], col));
            CHECK(parts[i].w > 0 && parts[i].h > 0);
            for (size_t j = i + 1; j < parts.size(); ++j) CHECK(!Overlap(parts[i], parts[j]));
        }
        CHECK(L.modeSwitch.y >= L.header.bottom() && L.search.y >= L.modeSwitch.bottom() && L.list.y >= L.search.bottom());
        if (mode == 1) CHECK(L.myStatus.y >= L.modeSwitch.bottom() && L.search.y >= L.myStatus.bottom());
        CHECK(L.myStatus.h == 0 || L.myStatus.h >= 2 * lineH);    // name and status both fit
        CHECK(L.rowH >= 2 * lineH + 4 && L.rowH >= L.avatar);      // a row holds two lines and the avatar
        CHECK(L.list.h >= 3 * L.rowH);                             // at least three friends show
        CHECK(L.header.h >= 30.0f * scale - 0.01f);                // the round header buttons fit
        if (mode == 1) CHECK(L.listW >= 280.0f * std::min(scale, 1.0f));   // name + status line readable
    }
    // Narrow windows fold to the avatar strip (the chat's rule, in unscaled pixels).
    CHECK(LayoutColumn(560, 500, 1.0f, 17.5f, 32, true, false).folded);
    CHECK(LayoutColumn(760, 560, 1.5f, 26, 48, true, false).folded);     // 760 / 1.5 < 600
    CHECK(LayoutColumn(1280, 800, 1.0f, 17.5f, 32, true, true).folded);  // the person folded it
    const ColumnLayout f = LayoutColumn(1280, 800, 1.0f, 17.5f, 32, true, true);
    CHECK(f.listW == 76.0f && f.modeSwitch.h == 0 && f.search.h == 0 && f.list.y >= f.header.bottom());
    // The chats keep their old widths at scale 1.0 (214 narrow, 280 wide).
    CHECK(LayoutColumn(620, 500, 1.0f, 17.5f, 32, false, false).listW == 214.0f);
    CHECK(LayoutColumn(1280, 800, 1.0f, 17.5f, 32, false, false).listW == 280.0f);
    CHECK(LayoutColumn(1280, 800, 1.0f, 17.5f, 32, true, false).listW == 300.0f);
}

// launcher/main.cpp wires it: one chat view (the Friends list is part of it, not a second window), the tray item opens it,
// Do not disturb keeps the chime quiet, and the status labels come from one place.
static void TestWiring() {
    std::ifstream f(OKAY_LAUNCHER_MAIN);
    CHECK(f.good());
    if (!f.good()) return;
    std::stringstream ss; ss << f.rdbuf();
    const std::string src = ss.str();
    CHECK(src.find("DrawChatFriendsColumn(fc);") != std::string::npos);
    CHECK(src.find("if (okayfriends::QuietAlerts(g_onlineStatus)) return;") != std::string::npos);
    CHECK(src.find("if (fx.openFriends)") != std::string::npos);
    CHECK(src.find("\"Online\\0Away\\0Invisible\\0\"") == std::string::npos);   // the old three-status combos are gone
    CHECK(src.find("k == \"chat_friends\"") != std::string::npos);          // the Friends side is remembered
    CHECK(src.find("okaychat::ListFolded(") == std::string::npos);           // the fold goes through LayoutColumn
}

int main() {
    TestPresence();
    TestGrouping();
    TestStatusLine();
    TestMenu();
    TestUnread();
    TestMyStatus();
    TestLayout();
    TestWiring();
    std::printf("launcher_friends: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
