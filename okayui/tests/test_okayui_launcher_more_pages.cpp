// The launcher's Friends, Community and Versions pages in OkayUI (okayui/screens/OkayUILauncherMorePages). Driven headless the way
// the launcher drives them: every chip, row, button and menu entry is found by its rectangle in the result and used with synthetic
// clicks, typing and the wheel; golden images (launcher look) pin what is drawn. At the end the per-frame CPU time of each page is
// measured (median of 300 frames) and printed; it is information, not a pass or fail.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "OkayUILauncherMorePages.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace okayuitest;
namespace UI = OkayUI;
namespace S = okayuiscreens;

static void Check(const char* name, const Bitmap& b) {
    std::string why;
    const bool ok = MatchesGolden(name, b, &why);
    if (!ok) std::cerr << "  golden " << name << ": " << why << " (actual written as " << name << ".actual.png)\n";
    CHECK(ok);
}
static const int kHostBg[3] = {30, 33, 41};   // the launcher's page background
static float CX(const UI::Rect& r) { return r.x + r.w * 0.5f; }
static float CY(const UI::Rect& r) { return r.y + r.h * 0.5f; }
static bool Has(const UI::Rect& r) { return r.w > 0.0f && r.h > 0.0f; }

template <class Fn> struct Rig {
    Sim sim;
    Fn draw;
    Rig(float w, float h, Fn f) : sim(w, h), draw(f) { Reset(); UI::UseLauncherLook(true); UI::Style().caretBlink = false; }
    ~Rig() { UI::UseLauncherLook(false); }
    void Frame() { sim.Frame([&] { draw(); }); }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void Click(const UI::Rect& r) { Click(CX(r), CY(r)); }
    void Click(float x, float y) { sim.Move(x, y); Frame(); sim.Down(); Frame(); sim.Up(); Frame(); }
    void RightClick(const UI::Rect& r) { sim.Move(CX(r), CY(r)); Frame(); sim.rdown = true; Frame(); sim.rdown = false; Frame(); }
    void Type(const std::string& s) { sim.Type(s); Frame(); }
};
template <class Fn> static Rig<Fn> MakeRig(float w, float h, Fn f) { return Rig<Fn>(w, h, f); }

static double MedianUs(std::vector<double> v) { std::sort(v.begin(), v.end()); return v.empty() ? 0.0 : v[v.size() / 2]; }

// ---- models (the launcher's demo data) ----
static S::FriendsModel Friends() {
    S::FriendsModel m;
    auto mk = [&](long row, const char* id, const char* user, const char* name, int kind, bool on, const char* playing, const char* custom,
                  long seen, unsigned char r, unsigned char g, unsigned char b) {
        S::FriendRow f; f.rowId = row; f.id = id; f.username = user; f.name = name; f.kind = kind; f.online = on; f.playing = playing;
        f.customStatus = custom; f.lastSeen = seen; f.lastOnline = seen ? "Last online 12m ago" : ""; f.avatar[0] = r; f.avatar[1] = g; f.avatar[2] = b;
        m.friends.push_back(f);
    };
    mk(1, "u-maya", "maya", "Maya Chen", 0, true, "Playing Neon Drift", "", 0, 56, 112, 64);
    mk(2, "demo-uid", "alexr", "Alex Rivera", 0, true, "", "shipping the jam build", 0, 52, 60, 112);
    mk(3, "u-priya", "priya", "Priya Natarajan", 0, true, "In the editor", "", 0, 92, 108, 50);
    mk(4, "u-jon", "jonas", "Jonas Berg", 0, false, "", "", 1700000000, 100, 92, 40);
    mk(5, "u-leo", "leo", "Leo Martins", 0, false, "", "", 1600000000, 40, 100, 96);
    mk(6, "u-kai", "kai_dev", "Kai", 1, true, "", "", 0, 50, 90, 112);
    mk(7, "u-zoe", "zoe", "Zoe Park", 2, false, "", "", 0, 90, 56, 112);
    m.friends[0].favorite = true; m.friends[0].joinable = true;
    auto fl = [&](std::vector<S::FollowRow>& v, const char* id, const char* u, const char* d, bool on, const char* seen) {
        S::FollowRow e; e.id = id; e.username = u; e.display = d; e.online = on; e.seen = seen; v.push_back(e);
    };
    fl(m.following, "u-maya", "maya", "Maya Chen", true, "");
    fl(m.following, "u-studio", "pixelforge", "PixelForge Studio", false, "seen 3h ago");
    fl(m.followers, "u-maya", "maya", "Maya Chen", true, "");
    fl(m.followers, "u-sam", "samw", "Sam Wilde", true, "");
    fl(m.followers, "u-ola", "ola", "Ola Nordmann", false, "seen 1d ago");
    auto ev = [&](const char* who, const char* text, const char* ago) { S::FeedRow e; e.who = who; e.text = text; e.ago = ago; m.feed.push_back(e); };
    ev("Maya Chen", "Maya Chen is playing Neon Drift", "4m ago");
    ev("Alex Rivera", "Alex Rivera published Coin Collector Deluxe", "1h ago");
    ev("Jonas Berg", "Jonas Berg came online", "1d ago");
    m.code = "OKAY-7K2F";
    m.canInvite = true; m.inviteLabel = "Invite to Neon Drift";
    return m;
}

int main() {
    RUN_SUITE("okayui launcher more pages");

    // ================= text wrapping =================
    {
        UI::BeginFrame(UI::Input{});
        const float w = UI::CalcTextSize("aaaa bbbb").x + 0.5f;
        std::vector<std::string> l = S::WrapLines("aaaa bbbb cccc dddd", w);
        CHECK(l.size() == 2 && l[0] == "aaaa bbbb" && l[1] == "cccc dddd");
        l = S::WrapLines("one\ntwo", 1000.0f);
        CHECK(l.size() == 2 && l[0] == "one" && l[1] == "two");
        l = S::WrapLines(std::string(60, 'x'), UI::CalcTextSize("xxxxxxxxxx").x + 0.5f);   // a word longer than the line is cut
        CHECK(l.size() == 6);
        for (const std::string& s : l) CHECK(UI::CalcTextSize(s.c_str()).x <= UI::CalcTextSize("xxxxxxxxxx").x + 0.5f);
        l = S::WrapLines("", 100.0f);
        CHECK(l.size() == 1 && l[0].empty());
        l = S::WrapLines("a  b", 0.0f);   // no room: no crash, every word on its own line
        CHECK(!l.empty());
        UI::EndFrameData();
    }

    // ================= OkayUI additions =================
    {   // a popup is as wide as its widest sized item, and stays inside the display near the right and bottom edges
        UI::Rect item, small;
        bool open = false;
        auto rig = MakeRig(400, 300, [&] {
            UI::BareNextWindow();
            if (UI::Begin("page", 0, 0, 400, 300)) {
                UI::SetCursorScreenPos(UI::Vec2{340.0f, 260.0f});
                if (UI::InvisibleButton("##opener", 50.0f, 30.0f)) UI::OpenPopup("##wide");
                open = UI::BeginPopup("##wide");
                if (open) {
                    UI::Selectable("##row", false, UI::Selectable_None, UI::Vec2{230.0f, 38.0f}); item = UI::GetItemRect();
                    UI::Selectable("short", false); small = UI::GetItemRect();
                    UI::EndPopup();
                }
            }
            UI::End();
        });
        rig.Frames(2);
        rig.Click(385.0f, 285.0f);
        rig.Frames(3);
        CHECK(open && item.w >= 229.5f);
        CHECK(item.x >= 0.0f && item.x + item.w <= 400.0f && small.y + small.h <= 300.0f);
        CHECK(small.w >= 229.5f);   // a full-width row fills the wider popup
    }

    {   // a Selectable labelled "##id" shows no text (its caller draws the row); "Text##id" shows "Text"
        UI::Rect a, b;
        auto rig = MakeRig(300, 120, [&] {
            UI::BareNextWindow();
            if (UI::Begin("sel", 0, 0, 300, 120)) {
                UI::Selectable("##hiddenrow", false); a = UI::GetItemRect();
                UI::Selectable("Shown##x", false); b = UI::GetItemRect();
            }
            UI::End();
        });
        rig.sim.Move(290, 115);
        rig.Frames(2);
        const Bitmap& bm = rig.sim.Draw(0, 0, 0);
        CHECK(CountNot(bm, (int)a.x, (int)a.y, (int)(a.x + a.w), (int)(a.y + a.h), Rgba(0, 0, 0)) == 0);
        CHECK(CountNot(bm, (int)b.x, (int)b.y, (int)(b.x + b.w), (int)(b.y + b.h), Rgba(0, 0, 0)) > 20);
        const int shownW = CountNot(bm, (int)b.x + 60, (int)b.y, (int)(b.x + b.w), (int)(b.y + b.h), Rgba(0, 0, 0));
        CHECK(shownW == 0);   // only "Shown": nothing drawn past it (no "##x")
    }

    // ================= Friends =================
    {   // the sections and the status line
        const S::FriendsModel m = Friends();
        std::vector<S::FriendSection> s = S::FriendSections(m, 0, "");
        CHECK(s.size() == 1 && std::string(s[0].title) == "ONLINE" && s[0].rows.size() == 3 && s[0].rows[0] == 0);   // the favourite first
        CHECK(s[0].rows[1] == 1 && s[0].rows[2] == 2);   // then by name
        s = S::FriendSections(m, 1, "");
        CHECK(s.size() == 3 && std::string(s[0].title) == "FAVORITES" && std::string(s[2].title) == "OFFLINE");
        CHECK(s[2].rows.size() == 2 && s[2].rows[0] == 3);   // offline: most recently online first
        s = S::FriendSections(m, 2, "");
        CHECK(s.size() == 2 && s[0].kind == 1 && s[0].rows[0] == 5 && s[1].kind == 2 && s[1].rows[0] == 6);
        s = S::FriendSections(m, 1, "@PRI");   // the search matches the name and the @username, any case
        CHECK(s.size() == 1 && s[0].rows.size() == 1 && s[0].rows[0] == 2);
        CHECK(S::FriendSections(m, 1, "nobody").empty());
        int c = -1;
        CHECK(S::FriendStatusLine(m.friends[0], &c) == "Playing Neon Drift" && c == 1);
        CHECK(S::FriendStatusLine(m.friends[1], &c) == "shipping the jam build" && c == 0);
        CHECK(S::FriendStatusLine(m.friends[3], &c) == "Last online 12m ago" && c == 0);
        CHECK(S::FriendStatusLine(m.friends[5], &c) == "Incoming friend request" && c == 1);
        CHECK(S::FriendStatusLine(m.friends[6], &c) == "Request sent");
        S::FriendRow plain; plain.online = true;
        CHECK(S::FriendStatusLine(plain, &c) == "Online" && c == 2);
    }
    {   // the Online view: chips, rows, buttons, menus, presence, code, the nudge
        S::FriendsModel m = Friends();
        S::FriendsState st;
        S::FriendsResult r;
        auto rig = MakeRig(1000, 640, [&] { r = S::DrawFriends(m, st, 20, 20, 960, 560); });
        rig.Frames(3);
        CHECK(r.chips.size() == 6 && r.shown.size() == 3 && r.rows.size() == 3);
        CHECK(Has(r.presenceButton) && Has(r.searchField) && Has(r.copyCodeButton) && Has(r.nudge));
        CHECK(Has(r.joinButtons[0]) && Has(r.messageButtons[0]) && Has(r.moreButtons[0]) && !Has(r.joinButtons[1]));
        CHECK(r.messageButtons[0].x < r.joinButtons[0].x && r.joinButtons[0].x < r.moreButtons[0].x);
        CHECK(UI::DroppedGeometry() == 0);
        Check("friends_online", rig.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]));
        // a row click opens the profile; a button on the row does its own thing only
        int profile = -1, msg = -1, join = -1;
        auto take = [&] { if (r.openProfile >= 0) profile = r.openProfile; if (r.message >= 0) msg = r.message; if (r.join >= 0) join = r.join; };
        auto click = [&](const UI::Rect& b) { rig.sim.Move(CX(b), CY(b)); rig.Frame(); take(); rig.sim.Down(); rig.Frame(); take(); rig.sim.Up(); rig.Frame(); take(); };
        click(UI::Rect{r.rows[1].x + 100.0f, r.rows[1].y + 5.0f, 10.0f, 10.0f});
        CHECK(profile == 1 && msg == -1);
        profile = -1;
        click(r.messageButtons[2]); CHECK(msg == 2 && profile == -1);
        click(r.joinButtons[0]); CHECK(join == 0 && profile == -1);
        // More opens the menu with the friend's entries; Favorite reports the toggle
        click(r.moreButtons[0]);
        rig.Frames(2);
        CHECK(!r.menuLabels.empty() && r.menuLabels[0] == "Message");
        CHECK(std::find(r.menuLabels.begin(), r.menuLabels.end(), "Invite to Neon Drift") != r.menuLabels.end());
        CHECK(std::find(r.menuLabels.begin(), r.menuLabels.end(), "Unfavorite") != r.menuLabels.end());
        CHECK(std::find(r.menuLabels.begin(), r.menuLabels.end(), "Remove friend") != r.menuLabels.end());
        CHECK(std::find(r.menuLabels.begin(), r.menuLabels.end(), "Clear nickname") == r.menuLabels.end());
        int fav = -1;
        {
            const auto it = std::find(r.menuLabels.begin(), r.menuLabels.end(), "Unfavorite");
            const UI::Rect e = r.menuItems[(std::size_t)(it - r.menuLabels.begin())];
            rig.sim.Move(CX(e), CY(e)); rig.Frame(); rig.sim.Down(); rig.Frame(); rig.sim.Up(); rig.Frame();
            fav = r.toggleFavorite;
            for (int i = 0; i < 3 && fav < 0; ++i) { rig.Frame(); fav = r.toggleFavorite; }
        }
        rig.Frames(2);
        CHECK(r.menuItems.empty());   // chosen: the menu closed
        CHECK(fav == 0 && profile == -1);   // the menu entry's action, and not also a row click
        // a right click opens the same menu on the row
        rig.RightClick(UI::Rect{r.rows[1].x + 200.0f, r.rows[1].y + 10.0f, 4.0f, 4.0f});
        rig.Frames(1);
        CHECK(!r.menuLabels.empty() && r.menuLabels[0] == "Message");
        rig.Click(990, 630);   // a click outside closes it (and does nothing else: the page behind a menu takes no clicks)
        rig.Frames(1);
        CHECK(r.menuItems.empty());
        // the presence menu
        rig.Click(r.presenceButton);
        rig.Frames(1);
        CHECK(r.presenceItems.size() == 3);
        int pres = -1;
        if (r.presenceItems.size() == 3) {
            const UI::Rect away = r.presenceItems[1];
            rig.sim.Move(CX(away), CY(away)); rig.Frame(); rig.sim.Down(); rig.Frame(); rig.sim.Up(); rig.Frame();
            pres = r.presence;
            if (pres < 0) { rig.Frame(); pres = r.presence; }
        }
        CHECK(pres == 1);
        // Copy code; with no code yet it is disabled
        bool copied = false;
        rig.sim.Move(CX(r.copyCodeButton), CY(r.copyCodeButton)); rig.Frame(); rig.sim.Down(); rig.Frame(); rig.sim.Up(); rig.Frame(); copied = r.copyCode;
        CHECK(copied);
        m.code.clear();
        rig.Frames(2);
        rig.sim.Move(CX(r.copyCodeButton), CY(r.copyCodeButton)); rig.Frame(); rig.sim.Down(); rig.Frame(); rig.sim.Up(); rig.Frame();
        CHECK(!r.copyCode);
        // the nudge goes to Pending
        rig.Click(r.nudge);
        CHECK(st.view == 2);
        rig.Frames(2);
        CHECK(r.shown.size() == 2 && !Has(r.nudge) && Has(r.acceptButtons[0]) && Has(r.declineButtons[0]) && Has(r.declineButtons[1]));
        int acc = -1, rem = -1;
        auto clickAR = [&](const UI::Rect& b) { rig.sim.Move(CX(b), CY(b)); rig.Frame(); rig.sim.Down(); rig.Frame(); rig.sim.Up(); rig.Frame();
                                                if (r.accept >= 0) { acc = r.accept; } if (r.remove >= 0) { rem = r.remove; } };
        clickAR(r.acceptButtons[0]); CHECK(acc == 5 && rem == -1);
        clickAR(r.declineButtons[1]); CHECK(rem == 6);
        // chips: All shows the three sections; typing in the search filters
        rig.Click(r.chips[1]);
        CHECK(st.view == 1);
        rig.Frames(2);
        CHECK(r.shown.size() == 5);
        rig.Click(r.searchField);
        rig.Type("leo");
        rig.Frames(2);
        CHECK(r.shown.size() == 1 && r.shown[0] == 4 && std::string(st.filter) == "leo");
        rig.Type("zzz");
        rig.Frames(2);
        CHECK(r.shown.empty() && r.empty && r.emptyTitle == "No matches");
    }
    {   // Add friend: type and Send, Enter sends too; the messages show
        S::FriendsModel m = Friends();
        S::FriendsState st; st.view = 3;
        S::FriendsResult r;
        std::string sent;
        auto rig = MakeRig(1000, 640, [&] { r = S::DrawFriends(m, st, 20, 20, 960, 560); if (!r.addRequest.empty()) sent = r.addRequest; });
        rig.Frames(3);
        CHECK(Has(r.addField) && Has(r.sendButton));
        rig.Click(r.sendButton);   // empty: disabled
        CHECK(sent.empty());
        rig.Click(r.addField);
        rig.Type("maya");
        rig.Click(r.sendButton);
        CHECK(sent == "maya");
        sent.clear();
        rig.Click(r.addField);
        rig.sim.Press(Key_Enter); rig.Frame(); rig.Frame();
        CHECK(sent == "maya");
        m.addError = "Hmm, no player with that username or code. Check the spelling?";
        rig.Frames(2);
        Check("friends_add", rig.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]));
    }
    {   // Following and Followers
        S::FriendsModel m = Friends();
        S::FriendsState st; st.view = 4;
        S::FriendsResult r;
        std::string follow, unfollow, removeF, back; int prof = -1;
        auto rig = MakeRig(1000, 640, [&] {
            r = S::DrawFriends(m, st, 20, 20, 960, 560);
            if (!r.follow.empty()) follow = r.follow;
            if (!r.unfollow.empty()) unfollow = r.unfollow;
            if (!r.removeFollower.empty()) removeF = r.removeFollower;
            if (!r.followBack.empty()) back = r.followBack;
            if (r.openFollowProfile >= 0) prof = r.openFollowProfile;
        });
        rig.Frames(3);
        CHECK(r.followRows.size() == 2 && Has(r.followField) && Has(r.followButton));
        rig.Click(r.followField);
        rig.Type("@ren");
        rig.Click(r.followButton);
        CHECK(follow == "ren" && st.followBuf[0] == '\0');   // the @ is dropped and the field cleared
        rig.Click(r.followButtons[1]);
        CHECK(unfollow == "u-studio" && prof == -1);
        rig.Click(UI::Rect{r.followRows[0].x + 300.0f, r.followRows[0].y + 20.0f, 4.0f, 4.0f});
        CHECK(prof == 0);
        rig.Click(r.chips[4]);   // Followers
        CHECK(st.view == 5);
        rig.Frames(2);
        CHECK(r.followRows.size() == 3 && !Has(r.followField));
        CHECK(!Has(r.followBackButtons[0]) && Has(r.followBackButtons[1]));   // Maya is followed back already
        rig.Click(r.followBackButtons[1]); CHECK(back == "u-sam");
        rig.Click(r.followButtons[2]); CHECK(removeF == "u-ola");
        Check("friends_followers", rig.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]));
        m.followBusy = true;   // a follow job is running: the buttons do nothing
        back.clear();
        rig.Frames(2);
        rig.Click(r.followBackButtons[1]); CHECK(back.empty());
        m.followSupported = false;
        rig.Frames(2);
        CHECK(r.empty && r.emptyTitle == "Follows aren't switched on yet");
    }
    {   // empty states and a narrow page (no side panel: the code and activity cards go under the list)
        S::FriendsModel m;
        S::FriendsState st;
        S::FriendsResult r;
        auto rig = MakeRig(700, 700, [&] { r = S::DrawFriends(m, st, 10, 10, 680, 680); });
        rig.Frames(3);
        CHECK(r.empty && r.emptyTitle == "No friends yet" && Has(r.copyCodeButton) == false);
        st.view = 2; rig.Frames(2);
        CHECK(r.empty && r.emptyTitle == "No friends yet");   // no friends at all says so first, as the Dear ImGui page does
        m.friends = Friends().friends; m.friends.resize(5); rig.Frames(2);
        CHECK(r.empty && r.emptyTitle == "No pending requests");
        m.friends.clear();
        st.view = 3; rig.Frames(2);
        CHECK(Has(r.copyCodeButton) && r.copyCodeButton.y > r.sendButton.y);   // the code card under the Add card
        m = Friends(); st.view = 0; rig.Frames(2);
        CHECK(r.shown.size() == 3 && r.rows[0].w > 600.0f);
        Check("friends_narrow", rig.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]));
    }
    {   // many friends: the list scrolls with the wheel to the last one
        S::FriendsModel m;
        for (int i = 0; i < 80; ++i) {
            S::FriendRow f; f.rowId = 100 + i; f.id = "u" + std::to_string(i); f.username = "user" + std::to_string(i);
            f.name = "Friend " + std::to_string(1000 + i); f.online = true; m.friends.push_back(f);
        }
        S::FriendsState st;
        S::FriendsResult r;
        auto rig = MakeRig(1000, 640, [&] { r = S::DrawFriends(m, st, 20, 20, 960, 560); });
        rig.Frames(3);
        CHECK(r.shown.size() == 80);
        const float y0 = r.rows[79].y;
        rig.sim.Move(300, 400);
        for (int i = 0; i < 80; ++i) { rig.sim.wheel = -3.0f; rig.Frame(); }
        CHECK(r.rows[79].y < y0 - 4000.0f && r.rows[79].y + r.rows[79].h < 600.0f);
        CHECK(UI::DroppedGeometry() == 0);
    }
    {   // hostile text: no crash, nothing dropped
        S::FriendsModel m = Friends();
        m.friends[0].name = std::string(3000, 'W'); m.friends[1].username = std::string(500, '@') + "\xff\xfe";
        m.friends[2].playing = std::string(2000, 'p'); m.feed[0].text = std::string(5000, 'x') + " " + std::string(300, ' ');
        m.code = std::string(400, 'C'); m.presence = 77;
        S::FriendsState st; st.view = 99;
        auto rig = MakeRig(800, 600, [&] { S::DrawFriends(m, st, 0, 0, 800, 600); });
        rig.Frames(3);
        CHECK(st.view == 0 && UI::DroppedGeometry() == 0);
    }

    // ================= Speed: per-frame CPU time of each page (headless; information, not a check) =================
    {
        auto bench = [](const char* name, float W, float H, const std::function<void()>& ui) {
            Reset(); UI::UseLauncherLook(true);
            Sim sim(W, H);
            sim.Move(W * 0.5f, H * 0.5f);
            std::vector<double> build;
            for (int i = 0; i < 330; ++i) {
                const auto t0 = std::chrono::steady_clock::now();
                sim.Frame(ui);
                if (i >= 30) build.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
            }
            const UI::DrawData dd = UI::GetDrawData();
            std::printf("  [speed] %-10s OkayUI build median %.1f us over 300 frames; %d vertices, %d indices, %d draw commands\n",
                        name, MedianUs(build), dd.vertexCount, dd.indexCount, dd.cmdCount);
            UI::UseLauncherLook(false);
        };
        const S::FriendsModel fm = Friends(); S::FriendsState fs;
        bench("Friends", 1280, 720, [&] { S::DrawFriends(fm, fs, 248, 60, 1016, 600); });
    }
    TEST_MAIN_RESULT();
}
