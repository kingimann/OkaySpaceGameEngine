// The launcher's Downloads page, Notifications panel and Achievements page in OkayUI (okayui/screens/OkayUILauncherPages), and the
// OkayUI pieces they needed (sized buttons, a right-click menu that respects clipping and windows in front). Driven headless the
// way the launcher drives them: every button, chip, row and tile is found by its rectangle in the result and used with synthetic
// clicks, typing and the wheel; golden images (launcher look) pin what is drawn. At the end the per-frame CPU time of each screen
// is measured (median of 300 frames) and printed; it is information, not a pass or fail.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "OkayUILauncherPages.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
static const int kHostBg[3] = {28, 30, 36};
static float CX(const UI::Rect& r) { return r.x + r.w * 0.5f; }
static float CY(const UI::Rect& r) { return r.y + r.h * 0.5f; }
static bool Has(const UI::Rect& r) { return r.w > 0.0f && r.h > 0.0f; }

// A rig per screen: the screen in a window-sized rectangle, launcher look, steady caret.
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
};
template <class Fn> static Rig<Fn> MakeRig(float w, float h, Fn f) { return Rig<Fn>(w, h, f); }

// ---- models ----
static S::DownloadsModel Downloads() {
    S::DownloadsModel m;
    m.items = {
        {1, "Orbit Hopper", "C:/OkaySpace/downloads/orbit_hopper.zip", 44040192, 44040192, 2},
        {2, "Neon Drift", "C:/OkaySpace/downloads/neon_drift.zip", 12582912, 50331648, 1},
        {3, "Puzzle Pack", "C:/OkaySpace/downloads/puzzles.okaypack", 0, 0, 0},
        {4, "Space Miner", "C:/OkaySpace/downloads/space_miner.exe", 2048, 0, 2},
        {5, "Broken Link", "C:/OkaySpace/downloads/broken.zip", 1536, 900000, 3},
        {6, "Not Wanted", "C:/OkaySpace/downloads/x.zip", 512, 1024, 4},
    };
    return m;
}
static S::NotificationsModel Notifs() {
    S::NotificationsModel m;
    auto add = [&](const char* t, const char* b, int kind, unsigned ageMin, bool unread, int day) {
        S::NotifEntry e; e.title = t; e.body = b; e.kind = kind; e.ageSeconds = ageMin * 60u; e.unread = unread; e.day = day; m.items.push_back(e);
    };
    add("New friend request", "alexr wants to be your friend.", 1, 14, true, 0);
    add("maya sent you a message", "Perfect. First playable by tonight? I can share the build link as soon as it's up, and the notes too.", 2, 2, true, 0);
    add("Update installed", "Updated to v3.0 - restart to finish.", 3, 65, true, 0);
    add("Game Jam Crew", "You were mentioned in #game-jam.", 0, 190, false, 0);
    add("priya accepted your friend request", "You can now chat and see when priya is playing.", 1, 26 * 60, false, 1);
    add("Download finished", "Orbit Hopper (42 MB) is ready to play in your Library.", 3, 5 * 1440, false, 2);
    return m;
}
static S::AchievementsModel Achs(long now = 1800000000L) {
    S::AchievementsModel m;
    m.now = now;
    const long day = 86400;
    auto add = [&](const char* id, const char* label, long ago, int pts, bool show, unsigned char r, unsigned char g, unsigned char b) {
        S::AchEntry a; a.id = id; a.label = label; a.when = now - ago; a.points = pts; a.showcased = show;
        a.date = "Jan 02, 2027"; a.rel = ago < day ? "3h ago" : "5d ago"; a.color[0] = r; a.color[1] = g; a.color[2] = b;
        m.items.push_back(a);
    };
    add("first_win", "First Victory", 120 * day, 10, true, 200, 120, 90);
    add("coin_hoarder", "Coin Hoarder", 60 * day, 25, false, 90, 180, 120);
    add("speed_demon", "Speed Demon", 20 * day, 50, false, 100, 140, 220);
    add("night_owl", "Night Owl", 9 * day, 10, false, 180, 110, 210);
    add("perfect_lap", "Perfect Lap", 2 * 3600, 25, true, 220, 190, 80);
    add("untouchable", "", 40 * 60, 50, false, 80, 200, 200);   // no label: shown as "Untouchable"
    m.points = 170; m.level = 3; m.levelProgress = 0.4f; m.showcaseCount = 2;
    return m;
}

static double MedianUs(std::vector<double> v) { std::sort(v.begin(), v.end()); return v.empty() ? 0.0 : v[v.size() / 2]; }

int main() {
    RUN_SUITE("okayui launcher pages");
    // OKAYUI_BENCH_ONLY=1: only the speed block, without the software raster (for profiling the build with callgrind).
    const bool benchOnly = std::getenv("OKAYUI_BENCH_ONLY") != nullptr;
    if (!benchOnly) {

    // ================= OkayUI additions =================
    {   // Button(label, size) and PrimaryButton(label, size): the size is used, 0 keeps the automatic one, junk falls back
        UI::Rect a, b, c, d, e;
        int clicks = 0;
        auto rig = MakeRig(500, 300, [&] {
            UI::BareNextWindow();
            if (UI::Begin("sized", 0, 0, 500, 300)) {
                if (UI::Button("Wide", UI::Vec2{200.0f, 0.0f})) ++clicks;
                a = UI::GetItemRect();
                UI::Button("Auto", UI::Vec2{0.0f, 0.0f}); b = UI::GetItemRect();
                UI::Button("Auto"); c = UI::GetItemRect();   // same label: same width as the automatic one above
                if (UI::PrimaryButton("Go", UI::Vec2{210.0f, 38.0f})) clicks += 10;
                d = UI::GetItemRect();
                UI::PrimaryButton("Junk", UI::Vec2{std::nanf(""), -5.0f}); e = UI::GetItemRect();
            }
            UI::End();
        });
        rig.Frames(2);
        CHECK(std::fabs(a.w - 200.0f) < 0.5f && std::fabs(a.h - UI::GetFrameHeight()) < 0.5f);
        CHECK(std::fabs(b.w - c.w) < 0.5f && b.w > 20.0f && b.w < 150.0f);
        CHECK(std::fabs(d.w - 210.0f) < 0.5f && std::fabs(d.h - 38.0f) < 0.5f);
        CHECK(e.w > 20.0f && e.w < 200.0f && e.h > 10.0f && e.h < 60.0f);
        rig.Click(a); CHECK(clicks == 1);
        rig.Click(d); CHECK(clicks == 11);
        rig.Click(d.x + d.w - 3.0f, d.y + d.h - 3.0f); CHECK(clicks == 21);   // the whole sized area takes the click
    }
    {   // a right click opens an item's menu only where a left click would reach the item
        int opened = 0;
        UI::Rect inView, clipped, under;
        float scroll = 0.0f;
        bool front = true;
        auto rig = MakeRig(600, 400, [&] {
            UI::BareNextWindow();
            if (UI::Begin("page", 0, 0, 600, 400)) {
                UI::BeginChild("##list", 280.0f, 120.0f, true);
                for (int i = 0; i < 20; ++i) {
                    UI::PushID(i);
                    UI::Button("row");
                    if (i == 0) inView = UI::GetItemRect();
                    if (i == 12) clipped = UI::GetItemRect();
                    if (UI::BeginPopupContextItem("##m")) { if (i == 0 || i == 12) ++opened; UI::EndPopup(); }
                    UI::PopID();
                }
                scroll = UI::GetScrollY();
                UI::EndChild();
                UI::SetCursorScreenPos(UI::Vec2{350.0f, 250.0f});
                UI::Button("under the window");
                under = UI::GetItemRect();
                if (UI::BeginPopupContextItem("##u")) { opened += 100; UI::EndPopup(); }
            }
            UI::End();
            if (front) {   // a floating window in front of the second button
                UI::SetNextWindowPos(UI::Vec2{330.0f, 230.0f});
                UI::SetNextWindowSize(UI::Vec2{250.0f, 120.0f});
                if (UI::Begin("front", nullptr, UI::Window_NoTitleBar | UI::Window_NoMove | UI::Window_NoResize)) UI::Text("in front");
                UI::End();
            }
        });
        rig.Frames(3);
        CHECK(clipped.y > 130.0f && scroll == 0.0f);                 // row 12 is below the child's visible part
        rig.RightClick(clipped); rig.Frames(2);
        CHECK(opened == 0);                                           // scrolled out of view: no menu
        rig.RightClick(under); rig.Frames(2);
        CHECK(opened == 0);                                           // under the window in front: no menu
        front = false; rig.Frames(2);
        rig.RightClick(under); rig.Frames(1);
        CHECK(opened >= 100);                                         // nothing in front any more: it opens
        rig.Click(10, 380); rig.Frames(2);                            // a click elsewhere closes it
        opened = 0; rig.Frames(1);
        CHECK(opened == 0);
        rig.RightClick(inView); rig.Frames(1);
        if (!(opened >= 1 && opened < 100)) std::printf("  ctx: opened %d inView %.0f,%.0f\n", opened, inView.x, inView.y);
        CHECK(opened >= 1 && opened < 100);
    }

    // ---- the gaps closed so the three pages can look like the launcher's Dear ImGui pages (section 8.6) ----
    {   // FramePadding: a frame row is the text height plus twice y; a text field starts its text x in
        float plain = 0.0f, padded = 0.0f;
        UI::Rect field;
        char buf[32] = "";
        auto rig = MakeRig(400, 200, [&] {
            UI::BareNextWindow();
            if (UI::Begin("fp", 0, 0, 400, 200)) {
                plain = UI::GetFrameHeight();
                UI::PushStyleVar(UI::StyleVar_FramePadding, UI::Vec2{30.0f, 7.0f});
                padded = UI::GetFrameHeight();
                UI::InputTextWithHint("##f", "hint", buf, (int)sizeof(buf));
                field = UI::GetItemRect();
                UI::PopStyleVar();
                UI::PushStyleVar(UI::StyleVar_FramePadding, UI::Vec2{std::nanf(""), -4.0f});   // junk: ignored
                CHECK(std::fabs(UI::GetFrameHeight() - plain) < 0.01f);
                UI::PopStyleVar();
            }
            UI::End();
        });
        rig.Frames(2);
        CHECK(std::fabs(padded - (UI::GetTextLineHeight() + 14.0f)) < 0.01f && std::fabs(plain - padded) > 0.5f);
        CHECK(std::fabs(field.h - padded) < 0.01f);
        rig.Click(field.x + 5.0f, field.y + field.h * 0.5f);   // the field still takes focus and typing
        rig.sim.Type("ok"); rig.Frame();
        CHECK(std::strcmp(buf, "ok") == 0);
        const Bitmap& bm = rig.sim.Draw(0, 0, 0);
        // the caret and text start 30 px in: nothing but the field's fill in the first 25 px of its middle row
        const unsigned fill = PixelAt(bm, (int)field.x + 12, (int)(field.y + field.h * 0.5f));
        CHECK(CountNot(bm, (int)field.x + 4, (int)field.y + 6, (int)field.x + 25, (int)(field.y + field.h) - 6, fill) == 0);
        CHECK(CountNot(bm, (int)field.x + 28, (int)field.y + 6, (int)field.x + 50, (int)(field.y + field.h) - 6, fill) > 0);
    }
    {   // ChildPadding and ScrollbarSize: no padding, the full width until the content overflows, then a thin scrollbar's width less
        UI::Rect first; float availNarrow = 0.0f, availWide = 0.0f; int rows = 3;
        auto rig = MakeRig(400, 300, [&] {
            UI::BareNextWindow();
            if (UI::Begin("cp", 0, 0, 400, 300)) {
                UI::SetCursorScreenPos(UI::Vec2{20.0f, 30.0f});
                UI::PushStyleVar(UI::StyleVar_ChildPadding, 0.0f);
                UI::PushStyleVar(UI::StyleVar_ScrollbarSize, 6.0f);
                UI::BeginChild("##c", 300.0f, 200.0f, false);
                const float a = UI::GetContentRegionAvail().x;
                for (int i = 0; i < rows; ++i) { UI::PushID(i); UI::Button("row", UI::Vec2{0.0f, 40.0f}); if (i == 0) first = UI::GetItemRect(); UI::PopID(); }
                (rows > 6 ? availNarrow : availWide) = a;
                UI::EndChild();
                UI::PopStyleVar(2);
            }
            UI::End();
        });
        rig.Frames(2);
        CHECK(std::fabs(first.x - 20.0f) < 0.01f && std::fabs(first.y - 30.0f) < 0.01f && std::fabs(availWide - 300.0f) < 0.01f);
        rows = 20; rig.Frames(2);
        CHECK(std::fabs(availNarrow - 294.0f) < 0.01f);
        const Bitmap& bm = rig.sim.Draw(0, 0, 0);
        CHECK(CountNot(bm, 314, 31, 320, 60, Rgba(0, 0, 0)) > 0);     // the thumb, inside the 6 px bar
        CHECK(CountNot(bm, 320, 31, 330, 229, Rgba(0, 0, 0)) == 0);   // and nothing right of it
    }
    {   // tooltips follow the theme: offset, padding, corner and fill; the default is unchanged
        UI::Rect item; bool custom = false;
        auto rig = MakeRig(500, 300, [&] {
            UI::Theme& t = UI::Style();
            if (custom) { t.tooltipOffsetX = 25.0f; t.tooltipOffsetY = 10.0f; t.tooltipPadX = 14.0f; t.tooltipPadY = 12.0f; t.tooltipRounding = 0.0f;
                          t.tooltipBg[0] = 200; t.tooltipBg[1] = 10; t.tooltipBg[2] = 10; t.tooltipBg[3] = 255; }
            UI::BareNextWindow();
            if (UI::Begin("tt", 0, 0, 500, 300)) {
                UI::SetCursorScreenPos(UI::Vec2{50.0f, 50.0f});
                UI::InvisibleButton("##it", 100.0f, 40.0f);
                item = UI::GetItemRect();
                if (UI::IsItemHovered()) UI::SetTooltip("%s", "tip");
            }
            UI::End();
        });
        rig.sim.Move(100.0f, 70.0f); rig.Frames(2);
        const unsigned dflt = PixelAt(rig.sim.Draw(0, 0, 0), 100 + 14 + 3, 70 + 14 + 3);   // the default box: 14 px right and below
        CHECK(dflt != Rgba(0, 0, 0));
        custom = true; rig.Frames(2);
        const Bitmap& bm = rig.sim.Draw(0, 0, 0);
        CHECK(PixelAt(bm, 100 + 25 + 2, 70 + 10 + 2) == Rgba(200, 10, 10));        // the red box starts 25 px right, 10 px below
        CHECK(PixelAt(bm, 100 + 25 - 2, 70 + 10 + 4) == Rgba(0, 0, 0));
        CHECK(PixelAt(bm, 100 + 25 + 8, 70 + 10 + 8) == Rgba(200, 10, 10));        // 14 x 12 padding: no text this close to the edge
    }
    {   // a polygon with one colour per point is blended across it
        auto rig = MakeRig(200, 200, [&] {
            const UI::DrawList& dl = UI::GetForegroundDrawList();
            const UI::Vec2 p[4] = {UI::Vec2{20, 20}, UI::Vec2{180, 20}, UI::Vec2{180, 180}, UI::Vec2{20, 180}};
            const UI::Color32 c[4] = {UI::RGBA(255, 0, 0, 255), UI::RGBA(255, 0, 0, 255), UI::RGBA(0, 0, 255, 255), UI::RGBA(0, 0, 255, 255)};
            dl.AddConvexPolyFilledMultiColor(p, c, 4);
            dl.AddConvexPolyFilledMultiColor(p, c, 2);         // too few points: nothing
            dl.AddConvexPolyFilledMultiColor(nullptr, c, 4);   // nothing
        });
        rig.Frames(1);
        const Bitmap& bm = rig.sim.Draw(0, 0, 0);
        const unsigned top = PixelAt(bm, 100, 22), mid = PixelAt(bm, 100, 100), bot = PixelAt(bm, 100, 178);
        CHECK((top >> 24) > 240 && ((top >> 8) & 255) < 15);
        CHECK((bot >> 24) < 15 && ((bot >> 8) & 255) > 240);
        CHECK((mid >> 24) > 100 && (mid >> 24) < 155 && ((mid >> 8) & 255) > 100 && ((mid >> 8) & 255) < 155);
    }
    {   // SetInputBlocked: a frame that starts blocked and is unblocked before the widget runs takes the press (the launcher's
        // hosted screens unblock as they are offered, so a click in the same frame as the move onto them is not lost)
        int clicks = 0; bool unblock = true;
        auto rig = MakeRig(300, 200, [&] {
            if (unblock) UI::SetInputBlocked(false);
            UI::BareNextWindow();
            if (UI::Begin("ib", 0, 0, 300, 200)) {
                UI::SetCursorScreenPos(UI::Vec2{50.0f, 50.0f});
                if (UI::InvisibleButton("##b", 100.0f, 40.0f)) ++clicks;
            }
            UI::End();
        });
        rig.Frames(1);
        rig.sim.Move(100.0f, 70.0f); rig.sim.Down(); rig.sim.blocked = true; rig.Frame();   // moved and pressed in one frame, blocked at its start
        rig.sim.Up(); rig.sim.blocked = false; rig.Frame();
        CHECK(clicks == 1);
        unblock = false;
        rig.sim.Move(10.0f, 10.0f); rig.Frame();
        rig.sim.Move(100.0f, 70.0f); rig.sim.Down(); rig.sim.blocked = true; rig.Frame();   // without it the press is lost
        rig.sim.Up(); rig.sim.blocked = false; rig.Frame();
        CHECK(clicks == 1);
        UI::SetInputBlocked(true); CHECK(UI::IsInputBlocked()); UI::SetInputBlocked(false); CHECK(!UI::IsInputBlocked());
    }
    {   // a window begun inside a PushTextScale does not keep that scale into the next frame (it used to grow every frame)
        float first = 0.0f, later = 0.0f; int n = 0;
        auto rig = MakeRig(300, 200, [&] {
            UI::PushTextScale(1.5f);
            UI::BareNextWindow();
            if (UI::Begin("ts", 0, 0, 300, 200)) (n++ == 0 ? first : later) = UI::GetTextLineHeight();
            UI::End();
            UI::PopTextScale();
        });
        rig.Frames(10);
        CHECK(first > 0.0f && std::fabs(first - later) < 0.01f);
    }
    {   // the combo box drawn as the launcher's (Theme::comboArrowBox) still opens and picks
        int cur = 0; UI::Rect box;
        auto rig = MakeRig(400, 300, [&] {
            UI::Style().comboArrowBox = true;
            UI::BareNextWindow();
            if (UI::Begin("cb", 0, 0, 400, 300)) {
                static const char* items[3] = {"One", "Two", "Three"};
                UI::SetNextItemWidth(150.0f);
                UI::Combo("##cb", items, 3, &cur);
                box = UI::GetItemRect();
            }
            UI::End();
            UI::Style().comboArrowBox = false;
        });
        rig.Frames(1);
        const Bitmap& bm = rig.sim.Draw(0, 0, 0);
        const unsigned arrowBox = PixelAt(bm, (int)(box.x + box.w - 3.0f), (int)(box.y + 4.0f)), field = PixelAt(bm, (int)(box.x + box.w * 0.5f), (int)(box.y + 4.0f));
        CHECK(arrowBox != field && arrowBox != Rgba(0, 0, 0));                         // a different square at the right end
        CHECK(PixelAt(bm, (int)(box.x + box.w - box.h * 0.5f), (int)(box.y + box.h * 0.5f)) == ThemeRgba(UI::Style().text));   // its arrow
        rig.Click(box); rig.Frames(1);
        rig.Click(CX(box), box.y + box.h + box.h * 2.5f); rig.Frames(1);   // the third row of the list under the box
        CHECK(cur == 2);
    }

    // ================= Downloads =================
    {   // helpers, as the Dear ImGui page words and computes them
        CHECK(S::HumanBytes(0) == "--" && S::HumanBytes(-5) == "--" && S::HumanBytes(512) == "512 B");
        CHECK(S::HumanBytes(1536) == "1.5 KB" && S::HumanBytes(3355443) == "3.2 MB" && S::HumanBytes(1342177280LL) == "1.25 GB");
        CHECK(std::strcmp(S::DownloadStatusText(0), "Queued") == 0 && std::strcmp(S::DownloadStatusText(2), "Completed") == 0);
        CHECK(std::strcmp(S::DownloadStatusText(4), "Canceled") == 0 && std::strcmp(S::DownloadStatusText(99), "Canceled") == 0);
        CHECK(S::DownloadIsZip("a/b.zip") && S::DownloadIsZip("B.ZIP") && !S::DownloadIsZip(".zip") && !S::DownloadIsZip("a.zip.part"));
        S::DownloadEntry d; d.status = 2; CHECK(S::DownloadFraction(d) == 1.0f);   // completed, size unknown: full
        d.status = 1; CHECK(S::DownloadFraction(d) == 0.0f);
        d.received = 30; d.total = 120; CHECK(std::fabs(S::DownloadFraction(d) - 0.25f) < 1e-6f);
        d.received = 500; CHECK(S::DownloadFraction(d) == 1.0f);                   // more than the size: clamped
        {   // the text in the bar: nothing for a queued download (no "--"), "0 B" before the first byte
            char ov[64];
            S::DownloadEntry q; q.status = 0; S::DownloadOverlay(q, ov, sizeof(ov)); CHECK(ov[0] == '\0');
            q.total = 48 * 1024 * 1024; S::DownloadOverlay(q, ov, sizeof(ov)); CHECK(std::strcmp(ov, "0 B / 48.0 MB") == 0);
            q.received = 12 * 1024 * 1024; S::DownloadOverlay(q, ov, sizeof(ov)); CHECK(std::strcmp(ov, "12.0 MB / 48.0 MB") == 0);
            q.total = 0; q.received = 1536; S::DownloadOverlay(q, ov, sizeof(ov)); CHECK(std::strcmp(ov, "1.5 KB") == 0);
        }
    }
    {   // empty: the friendly message and Browse Marketplace; Clear finished is disabled
        S::DownloadsModel m; S::DownloadsResult r;
        auto rig = MakeRig(900, 560, [&] { r = S::DrawDownloads(m, 16, 12, 868, 536); });
        rig.Frames(3);
        CHECK(r.empty && !r.clearEnabled && Has(r.browseButton));
        CHECK(std::fabs(r.browseButton.w - 210.0f) < 0.5f && std::fabs(r.openFolderButton.w - 200.0f) < 0.5f);
        Check("downloads_empty", rig.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]));
        bool browse = false, clear = false, open = false;
        auto rig2 = MakeRig(900, 560, [&] { r = S::DrawDownloads(m, 16, 12, 868, 536); browse |= r.browseMarketplace; clear |= r.clearFinished; open |= r.openFolder; });
        rig2.Frames(3);
        rig2.Click(r.clearFinishedButton); CHECK(!clear);
        rig2.Click(r.browseButton); CHECK(browse);
        rig2.Click(r.openFolderButton); CHECK(open);
    }
    {   // a list: each row offers the buttons its state has, and each button reports its own download once
        S::DownloadsModel m = Downloads(); S::DownloadsResult r;
        int cancel = -1, openItem = -1, install = -1, remove = -1, hits = 0; bool clear = false;
        auto rig = MakeRig(900, 900, [&] {
            r = S::DrawDownloads(m, 16, 12, 868, 876);
            if (r.cancelId >= 0) { cancel = r.cancelId; ++hits; }
            if (r.openItemFolderId >= 0) { openItem = r.openItemFolderId; ++hits; }
            if (r.installId >= 0) { install = r.installId; ++hits; }
            if (r.removeId >= 0) { remove = r.removeId; ++hits; }
            clear |= r.clearFinished;
        });
        rig.Frames(3);
        CHECK(!r.empty && r.clearEnabled && r.rows.size() == 6 && !Has(r.browseButton));
        // completed zip: Open folder, Install, Remove; downloading and queued: Cancel; completed non-zip: no Install; failed, canceled: Remove
        CHECK(Has(r.openButtons[0]) && Has(r.installButtons[0]) && Has(r.removeButtons[0]) && !Has(r.cancelButtons[0]));
        CHECK(Has(r.cancelButtons[1]) && !Has(r.removeButtons[1]) && Has(r.cancelButtons[2]));
        CHECK(Has(r.openButtons[3]) && !Has(r.installButtons[3]) && Has(r.removeButtons[3]));
        CHECK(!Has(r.openButtons[4]) && Has(r.removeButtons[4]) && Has(r.removeButtons[5]) && !Has(r.cancelButtons[5]));
        for (std::size_t i = 1; i < r.rows.size(); ++i) CHECK(r.rows[i].y >= r.rows[i - 1].y + r.rows[i - 1].h - 0.5f);
        CHECK(UI::DroppedGeometry() == 0);
        Check("downloads_list", rig.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]));
        rig.Click(r.cancelButtons[1]); CHECK(cancel == 2 && hits == 1);
        rig.Click(r.installButtons[0]); CHECK(install == 1 && hits == 2);
        rig.Click(r.openButtons[3]); CHECK(openItem == 4 && hits == 3);
        rig.Click(r.removeButtons[4]); CHECK(remove == 5 && hits == 4);
        rig.Click(r.clearFinishedButton); CHECK(clear);
    }
    {   // many downloads: the list scrolls with the wheel and the last row can be reached and used
        S::DownloadsModel m;
        for (int i = 0; i < 120; ++i) m.items.push_back({100 + i, "Game " + std::to_string(i), "g.zip", 10, 100, i % 2 ? 2 : 1});
        S::DownloadsResult r; int remove = -1;
        auto rig = MakeRig(800, 500, [&] { r = S::DrawDownloads(m, 10, 10, 780, 480); if (r.removeId >= 0) remove = r.removeId; });
        rig.Frames(3);
        const float y0 = r.rows[0].y;
        rig.sim.Move(400, 300);
        for (int i = 0; i < 400; ++i) { rig.sim.wheel = -3.0f; rig.Frame(); }
        rig.Frames(2);
        CHECK(r.rows[0].y < y0 - 1000.0f);
        CHECK(r.rows.back().y < 500.0f && r.rows.back().y > 60.0f);   // the last row is on screen
        rig.Click(r.removeButtons.back()); CHECK(remove == 219);
        CHECK(UI::DroppedGeometry() == 0);
    }

    // ================= Notifications =================
    {   // filter, order, ages and empty texts
        const S::NotificationsModel m = Notifs();
        CHECK(S::NotifPassesFilter(m.items[0], 0) && S::NotifPassesFilter(m.items[0], 1) && S::NotifPassesFilter(m.items[0], 2));
        CHECK(!S::NotifPassesFilter(m.items[3], 1) && S::NotifPassesFilter(m.items[3], 5) && !S::NotifPassesFilter(m.items[3], 2));
        S::NotifEntry odd; odd.kind = 9; CHECK(S::NotifPassesFilter(odd, 5));   // an unknown kind counts as system
        const std::vector<int> all = S::NotifOrder(m, 0);
        CHECK(all.size() == 6 && all[0] == 1 && all[1] == 0 && all[2] == 2 && all.back() == 5);   // newest first
        CHECK(S::NotifOrder(m, 1).size() == 3 && S::NotifOrder(m, 2).size() == 2 && S::NotifOrder(m, 4).size() == 2);
        CHECK(S::NotifAgeText(5) == "now" && S::NotifAgeText(300) == "5m" && S::NotifAgeText(7200) == "2h" && S::NotifAgeText(3 * 86400) == "3d");
        std::string t, h;
        S::NotifEmptyText(0, 0, t, h); CHECK(t == "You're all caught up" && h == "Friend requests, messages and updates show up here.");
        S::NotifEmptyText(4, 1, t, h); CHECK(t == "You're all caught up" && h == "No unread notifications.");
        S::NotifEmptyText(4, 3, t, h); CHECK(t == "Nothing here" && h == "No message notifications.");
    }
    {   // the panel: header counts, chips filter, every control reports once
        S::NotificationsModel m = Notifs(); S::NotificationsState st; S::NotificationsResult r;
        int mark = 0, clear = 0, close = 0, clicked = -1, dismissed = -1;
        auto rig = MakeRig(700, 700, [&] {
            r = S::DrawNotifications(m, st, 60, 20, 460, 640);
            mark += r.markAllRead; clear += r.clearAll; close += r.close;
            if (r.clicked >= 0) clicked = r.clicked;
            if (r.dismissed >= 0) dismissed = r.dismissed;
        });
        rig.Frames(3);
        CHECK(r.total == 6 && r.unread == 3 && r.chips.size() == 6 && r.shown.size() == 6 && r.rows.size() == 6);
        CHECK(Has(r.markAllButton) && Has(r.clearButton) && Has(r.closeButton) && !r.empty);
        CHECK(r.closeButton.x > r.clearButton.x && r.clearButton.x > r.markAllButton.x);
        Check("notifications_feed", rig.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]));
        rig.Click(r.chips[1]); rig.Frames(1);
        CHECK(st.filter == 1 && r.shown.size() == 3);
        rig.Click(r.chips[3]); rig.Frames(1);
        CHECK(st.filter == 3 && r.shown.size() == 1 && r.shown[0] == 1);
        rig.Click(r.chips[0]); rig.Frames(1);
        CHECK(st.filter == 0 && r.shown.size() == 6);
        rig.Click(r.markAllButton); CHECK(mark == 1);
        rig.Click(r.clearButton); CHECK(clear == 1);
        rig.Click(r.closeButton); CHECK(close == 1);
        rig.Click(r.rows[2]); CHECK(clicked == r.shown[2] && dismissed == -1);   // a row: the notification behind it
        // hovering a row shows its dismiss button; clicking it dismisses and is not also a row click
        clicked = -1;
        rig.sim.Move(CX(r.rows[1]), CY(r.rows[1])); rig.Frames(2);
        CHECK(Has(r.dismissButton) && r.dismissButton.x > r.rows[1].x + r.rows[1].w * 0.7f);
        const UI::Rect x = r.dismissButton;
        rig.Click(x); CHECK(dismissed == r.shown[1] && clicked == -1);
        // with nothing unread, Mark all read does nothing; with nothing at all, Clear does nothing either
        for (auto& n : m.items) n.unread = false;
        rig.Frames(2); rig.Click(r.markAllButton); CHECK(mark == 1 && r.unread == 0);
        m.items.clear(); rig.Frames(2); rig.Click(r.clearButton); CHECK(clear == 1);
        CHECK(r.empty && r.emptyTitle == "You're all caught up");
        Check("notifications_empty", rig.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]));
    }
    {   // a narrow panel wraps the chips onto a second row; a filter with nothing in it shows its own empty text
        S::NotificationsModel m = Notifs(); S::NotificationsState st; st.filter = 5; S::NotificationsResult r;
        auto rig = MakeRig(400, 600, [&] { r = S::DrawNotifications(m, st, 10, 10, 300, 560); });
        rig.Frames(3);
        CHECK(r.chips.size() == 6 && r.chips[5].y > r.chips[0].y + 5.0f);
        CHECK(r.shown.size() == 1);
        m.items.erase(m.items.begin() + 3); rig.Frames(2);
        CHECK(r.empty && r.emptyTitle == "Nothing here" && r.emptyHint == "No system notifications.");
    }
    {   // the panel floats in front of a hosted page: clicks on it never reach the page's buttons behind, in either call order
        S::NotificationsModel m = Notifs(); S::NotificationsState st; S::NotificationsResult r;
        int pageClicks = 0; UI::Rect pageBtn; bool panelFirst = false;
        auto page = [&] {
            UI::BareNextWindow();
            if (UI::Begin("page##host", 0, 0, 800, 600)) {
                UI::SetCursorScreenPos(UI::Vec2{150.0f, 300.0f});
                if (UI::Button("page button", UI::Vec2{500.0f, 60.0f})) ++pageClicks;
                pageBtn = UI::GetItemRect();
            }
            UI::End();
        };
        auto rig = MakeRig(800, 600, [&] {
            if (panelFirst) { r = S::DrawNotifications(m, st, 100, 50, 420, 520); page(); }
            else { page(); r = S::DrawNotifications(m, st, 100, 50, 420, 520); }
        });
        rig.Frames(3);
        rig.Click(260, 330); CHECK(pageClicks == 0);       // over the panel
        rig.Click(500, 330); CHECK(pageClicks == 0);
        rig.Click(600, 330); CHECK(pageClicks == 1);       // the part of the button outside the panel (it ends at x 520)
        panelFirst = true; rig.Frames(3);
        rig.Click(260, 330); CHECK(pageClicks == 1);
        // and the panel is drawn on top: its pixels, not the page button's, at a point inside both
        const Bitmap& b = rig.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]);
        const unsigned under = PixelAt(b, 300, 345);
        CHECK(under != ThemeRgba(UI::Style().bg) && under != ThemeRgba(UI::Style().bgHover));
    }
    {   // a long feed scrolls with the wheel
        S::NotificationsModel m;
        for (int i = 0; i < 80; ++i) { S::NotifEntry e; e.title = "Item " + std::to_string(i); e.body = "Body"; e.ageSeconds = (unsigned)i * 600u; e.day = i < 40 ? 0 : 2; m.items.push_back(e); }
        S::NotificationsState st; S::NotificationsResult r;
        auto rig = MakeRig(600, 600, [&] { r = S::DrawNotifications(m, st, 50, 20, 420, 560); });
        rig.Frames(3);
        const float y0 = r.rows[0].y;
        rig.sim.Move(260, 400);
        for (int i = 0; i < 30; ++i) { rig.sim.wheel = -3.0f; rig.Frame(); }
        CHECK(r.rows[0].y < y0 - 300.0f);
        CHECK(UI::DroppedGeometry() == 0);
    }

    // ================= Achievements =================
    {   // helpers
        CHECK(S::AchPrettyId("first_win") == "First Win" && S::AchPrettyId("a-b_c") == "A B C" && S::AchPrettyId("") == "");
        const S::AchievementsModel m = Achs();
        CHECK(S::AchMonthCount(m) == 4);                           // 20, 9 days, 2 hours, 40 minutes ago
        std::vector<int> l = S::AchList(m, 0, 0, "");
        CHECK(l.size() == 6 && l[0] == 5 && l[1] == 4 && l.back() == 0);   // newest first
        l = S::AchList(m, 0, 1, ""); CHECK(l[0] == 0 && l.back() == 5);
        l = S::AchList(m, 0, 2, ""); CHECK(l[0] == 5 && l[1] == 1);         // A to Z by label: "" sorts first, then "Coin Hoarder"
        CHECK(S::AchList(m, 1, 0, "").size() == 4 && S::AchList(m, 2, 0, "").size() == 2);
        CHECK(S::AchList(m, 0, 0, "LAP").size() == 1 && S::AchList(m, 0, 0, "untouch").size() == 1);   // label or id, any case
    }
    {   // the page, wide: header, chips, sort, search, tiles; the controls work
        S::AchievementsModel m = Achs(); S::AchievementsState st; S::AchievementsResult r;
        int profile = 0; std::string add, remove, copy;
        auto rig = MakeRig(1200, 700, [&] {
            r = S::DrawAchievements(m, st, 16, 12, 1168, 676);
            profile += r.viewProfile;
            if (!r.addShowcase.empty()) add = r.addShowcase;
            if (!r.removeShowcase.empty()) remove = r.removeShowcase;
            if (!r.copyId.empty()) copy = r.copyId;
        });
        rig.Frames(3);
        CHECK(r.shown.size() == 6 && r.tiles.size() == 6 && r.chips.size() == 3 && r.monthCount == 4);
        CHECK(r.columns == 4 && Has(r.levelBadge) && Has(r.searchField) && Has(r.sortCombo) && Has(r.profileButton));
        CHECK(r.tiles[1].x > r.tiles[0].x + 200.0f && r.tiles[4].y > r.tiles[0].y + 50.0f);
        CHECK(UI::DroppedGeometry() == 0);
        Check("achievements_grid", rig.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]));
        rig.Click(r.profileButton); CHECK(profile == 1);
        rig.Click(r.chips[1]); rig.Frames(1); CHECK(st.view == 1 && r.shown.size() == 4);
        rig.Click(r.chips[2]); rig.Frames(1); CHECK(st.view == 2 && r.shown.size() == 2);
        rig.Click(r.chips[0]); rig.Frames(1); CHECK(st.view == 0 && r.shown.size() == 6);
        // typing in the search field filters
        rig.Click(r.searchField);
        rig.sim.Type("owl"); rig.Frames(2);
        CHECK(std::strcmp(st.filter, "owl") == 0 && r.shown.size() == 1 && r.shown[0] == 3);
        for (int i = 0; i < 3; ++i) { rig.sim.Press(Key_Backspace); rig.Frame(); }
        rig.Frames(1); CHECK(r.shown.size() == 6);
        rig.sim.Press(Key_Escape); rig.Frames(2);
        // the sort combo: open it, pick "A to Z" (the third entry under the box)
        rig.Click(r.sortCombo); rig.Frames(1);
        rig.Click(CX(r.sortCombo), r.sortCombo.y + r.sortCombo.h + UI::GetFrameHeight() * 2.5f); rig.Frames(1);
        CHECK(st.sort == 2 && r.shown[0] == 5 && r.shown[1] == 1);
        // right-click a tile that is not showcased: Add to showcase; on a showcased one: Remove; Copy id on any
        const int iNight = (int)(std::find(r.shown.begin(), r.shown.end(), 3) - r.shown.begin());
        rig.RightClick(r.tiles[(std::size_t)iNight]); rig.Frames(1);
        CHECK(Has(r.menuShowcase) && Has(r.menuCopyId));
        rig.Click(r.menuShowcase); rig.Frames(1);
        CHECK(add == "night_owl" && !Has(r.menuShowcase));          // and the menu closed
        const int iFirst = (int)(std::find(r.shown.begin(), r.shown.end(), 0) - r.shown.begin());
        rig.RightClick(r.tiles[(std::size_t)iFirst]); rig.Frames(1);
        rig.Click(r.menuShowcase); CHECK(remove == "first_win");
        rig.RightClick(r.tiles[0]); rig.Frames(1);
        rig.Click(r.menuCopyId); CHECK(copy == m.items[(std::size_t)r.shown[0]].id);
        // the showcase is full: Add does nothing
        add.clear(); m.showcaseCount = 6; rig.Frames(1);
        rig.RightClick(r.tiles[(std::size_t)iNight]); rig.Frames(1);
        rig.Click(r.menuShowcase); rig.Frames(1);
        CHECK(add.empty());
    }
    {   // narrow: no level badge or stats, fewer columns; empty states with their words and the Browse games button
        S::AchievementsModel m = Achs(); S::AchievementsState st; S::AchievementsResult r; int browse = 0;
        auto rig = MakeRig(600, 600, [&] { r = S::DrawAchievements(m, st, 10, 10, 580, 580); browse += r.browseGames; });
        rig.Frames(3);
        if (!(!Has(r.levelBadge) && r.columns == 2)) std::printf("  narrow: badge %.0f cols %d\n", r.levelBadge.w, r.columns);
        CHECK(!Has(r.levelBadge) && r.columns == 2);
        std::strcpy(st.filter, "zzz"); rig.Frames(2);
        CHECK(r.empty && r.emptyTitle == "No achievements match your search" && !Has(r.browseButton));
        st.filter[0] = 0; st.view = 2; for (auto& a : m.items) a.showcased = false; rig.Frames(2);
        CHECK(r.empty && r.emptyTitle == "Nothing showcased yet");
        st.view = 0; m.items.clear(); rig.Frames(2);
        CHECK(r.empty && r.emptyTitle == "No achievements yet" && Has(r.browseButton));
        Check("achievements_empty", rig.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]));
        rig.Click(r.browseButton); CHECK(browse == 1);
    }
    {   // many tiles: the list scrolls; tiles out of view are laid out but not drawn; nothing dropped
        S::AchievementsModel m = Achs();
        for (int i = 0; i < 300; ++i) { S::AchEntry a; a.id = "a" + std::to_string(i); a.label = "Achievement " + std::to_string(i); a.when = m.now - i * 3600L; m.items.push_back(a); }
        S::AchievementsState st; S::AchievementsResult r;
        auto rig = MakeRig(1000, 600, [&] { r = S::DrawAchievements(m, st, 10, 10, 980, 580); });
        rig.Frames(3);
        const float y0 = r.tiles[0].y;
        rig.sim.Move(500, 450);
        for (int i = 0; i < 40; ++i) { rig.sim.wheel = -3.0f; rig.Frame(); }
        CHECK(r.tiles[0].y < y0 - 300.0f && r.tiles.size() == 306);
        CHECK(UI::DroppedGeometry() == 0);
    }
    {   // hostile text: no crash, nothing dropped
        S::NotificationsModel nm; S::NotifEntry e; e.title = std::string(3000, 'W'); e.body = std::string(4000, 'x') + " \xff\xfe " + std::string(50, ' '); nm.items.push_back(e);
        S::NotificationsState ns; S::DownloadsModel dm; dm.items.push_back({7, std::string(2000, 'Q'), "", -5, -9, 77});
        S::AchievementsModel am = Achs(); am.items[0].label = std::string(1000, 'L'); am.levelProgress = std::nanf(""); am.items[1].points = -3;
        S::AchievementsState as;
        auto rig = MakeRig(600, 500, [&] {
            S::DrawNotifications(nm, ns, 0, 0, 300, 250);
            S::DrawDownloads(dm, 300, 0, 300, 250);
            S::DrawAchievements(am, as, 0, 250, 600, 250);
        });
        rig.Frames(3);
        CHECK(UI::DroppedGeometry() == 0);
        CHECK(S::FitTextWidth("abc", 0.0f).empty() && S::FitTextWidth("abc", 1000.0f) == "abc");
        UI::BeginFrame(UI::Input{});
        const std::string cut = S::FitTextWidth("Hello wide world \xC3\xA9\xC3\xA9\xC3\xA9", 60.0f);
        UI::EndFrameData();
        CHECK(cut.size() >= 3 && cut.compare(cut.size() - 3, 3, "\xE2\x80\xA6") == 0);
    }

    }   // !benchOnly

    // ================= Speed: per-frame CPU time of each screen (headless; information, not a check) =================
    {
        auto bench = [benchOnly](const char* name, float W, float H, const std::function<void()>& ui) {
            Reset(); UI::UseLauncherLook(true);
            Sim sim(W, H);
            sim.Move(W * 0.5f, H * 0.5f);
            std::vector<double> build, raster;
            for (int i = 0; i < 330; ++i) {
                const auto t0 = std::chrono::steady_clock::now();
                sim.Frame(ui);
                const auto t1 = std::chrono::steady_clock::now();
                if (i >= 30) build.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
                if (!benchOnly && i >= 30 && i % 10 == 0) {   // the software raster is slow and not what the launcher uses: sampled
                    const auto r0 = std::chrono::steady_clock::now();
                    sim.Draw(28, 30, 36);
                    raster.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - r0).count());
                }
            }
            const UI::DrawData dd = UI::GetDrawData();
            std::printf("  [speed] %-14s OkayUI build median %.1f us over 300 frames; %d vertices, %d indices, %d draw commands "
                        "(software raster median %.0f us, not used by the launcher)\n",
                        name, MedianUs(build), dd.vertexCount, dd.indexCount, dd.cmdCount, MedianUs(raster));
            UI::UseLauncherLook(false);
        };
        const S::DownloadsModel dm = Downloads();
        const S::NotificationsModel nm = Notifs(); S::NotificationsState ns;
        const S::AchievementsModel am = Achs(); S::AchievementsState as;
        bench("Downloads", 1280, 720, [&] { S::DrawDownloads(dm, 248, 60, 1016, 600); });
        bench("Notifications", 1280, 720, [&] { S::DrawNotifications(nm, ns, 242, 68, 480, 640); });
        bench("Achievements", 1280, 720, [&] { S::DrawAchievements(am, as, 248, 60, 1016, 600); });
    }
    TEST_MAIN_RESULT();
}
