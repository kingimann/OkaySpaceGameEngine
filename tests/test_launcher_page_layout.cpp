// The launcher's page layouts (launcher/PageLayout.hpp): the Marketplace, Community and Notifications pages at the
// small window (960 x 600), the normal one (1280 x 800) and the large one (1920 x 1080), at UI scale 1.0 and 1.5.
// Nothing overlaps, every label fits its button, every piece stays inside its card or panel.
// With OKAY_LAYOUT_REAL_FONT the labels are measured with the launcher's own font as OkayUI draws it (Roboto Medium, 17 px, no
// window); without it an average glyph width stands in.
#include "test_framework.hpp"
#include "PageLayout.hpp"

#include <cmath>
#include <string>
#include <vector>

#ifdef OKAY_LAYOUT_REAL_FONT
#include "imgui.h"   // OkayUI's (okayui/probe)
#endif

namespace L = okay::launcherlayout;

namespace {

float g_scale = 1.0f;
float Px(float v) { return v * g_scale; }

#ifdef OKAY_LAYOUT_REAL_FONT
float TextW(const std::string& s) { return ImGui::CalcTextSize(s.c_str()).x; }
float LineH() { return ImGui::GetTextLineHeight(); }
#else
float TextW(const std::string& s) { return (float)s.size() * 8.6f * g_scale; }
float LineH() { return 17.0f * g_scale; }
#endif

// The content area a page gets in a window of w x h: the window less the sidebar (232, or 64 folded below 1000 px
// wide is NOT done by the launcher, so always 232), the content padding (28 a side) and a scrollbar.
float PageW(float winW) { return winW - 232.0f - 2 * 28.0f - 14.0f; }

bool NoOverlaps(const std::vector<L::Rect>& r) {
    for (std::size_t i = 0; i < r.size(); ++i)
        for (std::size_t j = i + 1; j < r.size(); ++j)
            if (L::Overlaps(r[i], r[j])) return false;
    return true;
}

// The Marketplace / Community toolbar: search, then the page's buttons, each as wide as its label.
void CheckToolbar(float availW, const std::vector<std::string>& labels) {
    std::vector<float> bw;
    for (const auto& l : labels) bw.push_back(L::ButtonW(TextW(l), Px(14.0f), 0.0f));
    const L::Toolbar t = L::LayoutToolbar(availW, Px(220.0f), Px(420.0f), bw, LineH() + Px(14.0f), Px(8.0f));
    std::vector<L::Rect> all = t.buttons;
    all.push_back(t.search);
    CHECK(NoOverlaps(all));
    const L::Rect area{0, 0, availW, t.height};
    float bottom = 0; for (const auto& r : all) bottom = std::max(bottom, r.B());
    CHECK_NEAR(t.height, bottom, 0.5);   // no empty band under the toolbar (the stacked layout counted its first row twice)
    for (const auto& r : all) CHECK(L::Inside(r, area));
    for (std::size_t i = 0; i < labels.size(); ++i) CHECK(t.buttons[i].w >= TextW(labels[i]) + Px(8.0f));   // the label fits with padding
    CHECK(t.search.w >= Px(220.0f) - 0.5f);
}

// The listing card: icon, text column and the action row (main button + the round buttons), inside the card.
void CheckListCard(float pageW, float minCardW, int maxCols, const std::vector<std::string>& mainLabels, int roundButtons) {
    const L::Grid g = L::FitGrid(pageW, Px(minCardW), Px(12.0f), maxCols);
    CHECK(g.cols >= 1);
    CHECK(g.cardW * g.cols + g.gap * (g.cols - 1) <= pageW + 0.5f);
    const float lh = LineH();
    const float textH = lh * 4 + Px(10.0f);   // title, kind + author, rating, a description line
    const float actH = lh + Px(14.0f);
    const L::ListCard c = L::LayoutListCard(g.cardW, Px(14.0f), Px(56.0f), textH, actH);
    CHECK(L::Inside(c.icon, c.card)); CHECK(L::Inside(c.text, c.card)); CHECK(L::Inside(c.actions, c.card));
    CHECK(!L::Overlaps(c.icon, c.text)); CHECK(!L::Overlaps(c.text, c.actions)); CHECK(!L::Overlaps(c.icon, c.actions));
    CHECK(c.text.w >= Px(120.0f));   // a title has room
    for (const auto& lbl : mainLabels) {
        std::vector<float> rest((std::size_t)roundButtons, actH);
        const auto row = L::LayoutActionRow(c.actions.w, actH, L::ButtonW(TextW(lbl), Px(14.0f), Px(64.0f)), rest, Px(8.0f));
        CHECK(NoOverlaps(row));
        const L::Rect rowArea{0, 0, c.actions.w, actH};
        for (const auto& r : row) CHECK(L::Inside(r, rowArea));
        CHECK(row[0].w >= TextW(lbl) + Px(8.0f));
    }
    // Two cards side by side do not touch.
    if (g.cols > 1) CHECK(!L::Overlaps(L::GridCell(g, 0, c.card.h), L::GridCell(g, 1, c.card.h)));
    CHECK(!L::Overlaps(L::GridCell(g, 0, c.card.h), L::GridCell(g, g.cols, c.card.h)));
}

void CheckChips(float availW, const std::vector<std::string>& labels) {
    std::vector<float> w;
    for (const auto& l : labels) w.push_back(TextW(l) + 2 * Px(11.0f) + Px(12.0f) + Px(7.0f) + TextW("88"));
    const auto rs = L::Flow(w, LineH() + Px(10.0f), availW, Px(6.0f), Px(6.0f));
    CHECK(NoOverlaps(rs));
    const L::Rect area{0, 0, availW, L::FlowHeight(rs)};
    for (std::size_t i = 0; i < rs.size(); ++i) { CHECK(L::Inside(rs[i], area)); CHECK(rs[i].w >= w[i] - 0.5f || w[i] > availW); }
}

void CheckNotifications(float winW, float winH) {
    const L::Rect p = L::FlyoutRect(winW, winH, 232.0f, Px(480.0f), Px(640.0f), Px(320.0f), Px(300.0f), 12.0f);
    const L::Rect win{0, 0, winW, winH};
    CHECK(L::Inside(p, win));
    CHECK(p.x >= 232.0f);   // never over the sidebar
    CHECK(p.w >= 300.0f);
    // The header: the title on the left, "Mark all read", "Clear" and the close button on the right, inside the panel.
    const float inner = p.w - 2 * Px(16.0f);
    const float title = TextW("Notifications") * 1.18f + Px(10.0f) + TextW("88 new") + Px(14.0f);
    const float acts = L::ButtonW(TextW("Mark all read"), Px(8.0f), 0) + L::ButtonW(TextW("Clear"), Px(8.0f), 0) + Px(26.0f) + Px(8.0f);
    // When they do not fit side by side the panel puts the actions on their own row: either way nothing overlaps.
    if (title + acts > inner) {
        const std::vector<float> bw{L::ButtonW(TextW("Mark all read"), Px(8.0f), 0), L::ButtonW(TextW("Clear"), Px(8.0f), 0)};
        const auto rs = L::Flow(bw, Px(26.0f), inner, Px(4.0f), Px(4.0f));
        CHECK(NoOverlaps(rs));
        for (const auto& r : rs) CHECK(r.R() <= inner + 0.5f);
    }
    CheckChips(inner, {"All", "Unread", "Friends", "Messages", "Updates", "System"});
}

#ifdef OKAY_LAYOUT_REAL_FONT
float WrappedH(const std::string& s, float w) { return ImGui::CalcTextSize(s.c_str(), nullptr, false, w).y; }
#else
float WrappedH(const std::string& s, float w) { const float perLine = std::max(1.0f, std::floor(w / (8.6f * g_scale))); return std::ceil((float)s.size() / perLine) * LineH(); }
#endif

// The What's New notes: a long test-build list (60 lines of up to 400 characters) and an old six-line notes file.
void CheckNotes(float winW, float winH) {
    const float w = PageW(winW) - 2 * Px(16.0f) - Px(20.0f);   // card padding and the bullet's indent
    const float viewH = winH - Px(260.0f);                   // under the page header and the version card
    std::vector<float> longH, shortH;
    for (int i = 0; i < L::kNotesMaxLines; ++i) {
        std::string t = "PR " + std::to_string(1200 + i) + ": ";
        while (t.size() < L::kNotesMaxLineBytes) t += "a change described in plain words, ";
        longH.push_back(WrappedH(t.substr(0, L::kNotesMaxLineBytes), w));
    }
    for (int i = 0; i < 6; ++i) shortH.push_back(WrappedH("Short note about one fix.", w));
    for (bool expanded : {false, true}) {
        const L::NotesLayout n = L::LayoutNotes(longH, w, Px(4.0f), LineH() + Px(8.0f), expanded, viewH);
        CHECK((int)n.bullets.size() == (expanded ? L::kNotesMaxLines : L::kNotesCollapsed));
        std::vector<L::Rect> all = n.bullets; all.push_back(n.toggle);
        CHECK(NoOverlaps(all));
        const L::Rect col{0, 0, w, n.height};
        for (const auto& r : all) CHECK(L::Inside(r, col));
        for (const auto& r : n.bullets) CHECK(r.h >= LineH() - 0.5f);   // every note gets at least a line (wrapped: more)
        if (expanded) CHECK(n.scrolls);   // sixty long notes never fit a window: the area scrolls
    }
    if (winW <= 960) {   // a 400-character note at 960 x 600 wraps inside the card's content rectangle (it ran off both edges, cut)
        const float cardW = PageW(winW), pad = Px(16.0f), bulletIndent = Px(20.0f);
        const L::Rect content{pad, 0, cardW - 2 * pad, 10000.0f};
        const std::string note(L::kNotesMaxLineBytes, 'x');
        std::string words; while (words.size() < L::kNotesMaxLineBytes) words += "wrapped words ";
        for (const std::string& t : {note, words}) {
            const float tw = content.w - bulletIndent;
            const float h = WrappedH(t, tw);
            const L::Rect text{content.x + bulletIndent, 0, tw, h};
            CHECK(L::Inside(text, content));
            CHECK(h >= 2 * LineH() - 0.5f);   // it wrapped (it does not fit one line), so nothing was cut at the edge
        }
    }
    const L::NotesLayout s = L::LayoutNotes(shortH, w, Px(4.0f), LineH() + Px(8.0f), false, viewH);
    CHECK((int)s.bullets.size() == 6); CHECK(s.toggle.h == 0.0f);   // an old short file: every line, no button
    if (winH >= 800) CHECK(!s.scrolls);
}

// The What's New "Test builds" section: which state shows, and several build rows that fit and do not overlap.
void CheckTestBuilds(float winW) {
    using V = L::TestBuildsView;
    CHECK(L::TestBuildsViewOf(false, 1, 5) == V::Off);    // switched off: the hint, even with builds known
    CHECK(L::TestBuildsViewOf(true, 0, 0) == V::Loading);
    CHECK(L::TestBuildsViewOf(true, 2, 0) == V::Failed);
    CHECK(L::TestBuildsViewOf(true, 1, 0) == V::Empty);
    CHECK(L::TestBuildsViewOf(true, 1, 4) == V::List);
    const float w = PageW(winW);
    const struct { const char* ver; const char* date; const char* note; bool installed; } builds[] = {
        {"v2.904.0.3", "2026-11-25", "Marketplace, Community and Notifications redone", false},
        {"v2.904.0.2", "2026-11-24", "Ragdoll joints no longer stretch when a character falls down a slope at speed and hits a wall", true},
        {"v2.904.0.1", "2026-11-24", "No note was given for this build.", false},
        {"v2.903.9.12", "2026-11-23", "A note that is long on purpose so it wraps onto several lines at the small window size and at UI scale 1.5", false},
    };
    float y = 0;
    std::vector<L::Rect> rows;
    for (const auto& b : builds) {
        const float pad = Px(14.0f), noteW = w - 2 * pad;
        const float pillW = b.installed ? TextW("Installed") + Px(14.0f) : 0.0f;
        const L::BuildRow r = L::LayoutBuildRow(w, pad, LineH(), TextW(b.ver), pillW, TextW(b.date), WrappedH(b.note, noteW));
        std::vector<L::Rect> parts{r.version, r.date, r.note, r.branch};
        if (b.installed) parts.push_back(r.pill);
        CHECK(NoOverlaps(parts));
        for (const auto& q : parts) CHECK(L::Inside(q, r.row));
        CHECK(r.version.w >= TextW(b.ver) - 0.5f);   // the version is never cut at these sizes
        rows.push_back(L::Rect{0, y, r.row.w, r.row.h});
        y += r.row.h + Px(8.0f);
    }
    CHECK(NoOverlaps(rows));
}

// The Achievements header card (owner's v2.903.8.7 screenshot: the level ring sat on the subtitle): medals + count + a wrapped
// line on the left, the level ring, POINTS, THIS MONTH and "View my profile" on the right; side by side or stacked, never on
// top of each other, and the wrapped line keeps at least 200 px.
void CheckAchievementsHeader(float winW) {
    const float W = PageW(winW) - 2 * Px(22.0f), gap = Px(24.0f);
    const float artW = Px(30.0f) + 3 * Px(26.0f) + Px(28.0f);
    const float ringR = Px(22.0f), statGap = Px(22.0f);
    const float statW1 = std::max(TextW("POINTS") * 0.8f, Px(40.0f)), statW2 = std::max(TextW("THIS MONTH") * 0.8f, Px(40.0f));
    const float bw = L::ButtonW(TextW("View my profile"), Px(14.0f), Px(140.0f));
    const float rightW = 2 * ringR + statGap + statW1 + statGap + statW2 + statGap + bw;
    const float rightH = std::max(2 * ringR + Px(4.0f), LineH() + Px(22.0f));
    for (const char* sub : {"Play games to start earning achievements.", "Latest: Jam Survivor  \xC2\xB7  26 min ago"}) {
        L::HeaderSplit hs = L::SplitHeader(W, artW + Px(200.0f), rightW, gap);
        const float textW = hs.leftW - artW;
        CHECK(textW >= Px(200.0f) - 0.5f);
        const float leftH = std::max(Px(64.0f), LineH() * 1.45f + Px(4.0f) + WrappedH(sub, textW));
        L::PlaceHeader(hs, W, leftH, rightW, rightH, Px(16.0f));
        CHECK(!L::Overlaps(hs.left, hs.right));
        const L::Rect card{0, 0, W, hs.height};
        CHECK(L::Inside(hs.left, card)); CHECK(L::Inside(hs.right, card));
        // Inside the right group: ring, stats and button one after the other.
        float x = hs.right.x;
        const L::Rect ring{x, hs.right.y, 2 * ringR, 2 * ringR}; x += 2 * ringR + statGap;
        const L::Rect s1{x, hs.right.y, statW1, rightH}; x += statW1 + statGap;
        const L::Rect s2{x, hs.right.y, statW2, rightH}; x += statW2 + statGap;
        const L::Rect btn{x, hs.right.y, bw, rightH};
        CHECK(NoOverlaps({ring, s1, s2, btn}));
        CHECK(btn.R() <= W + 0.5f);
        CHECK(!L::Overlaps(ring, hs.left)); CHECK(!L::Overlaps(btn, hs.left));
    }
    // The case of the screenshot (a 1154 px window, scale 1): side by side, and the subtitle stops before the ring.
    if (winW >= 1280) CHECK(L::SplitHeader(W, artW + Px(200.0f), rightW, gap).sameRow || g_scale > 1.0f);
}

// The Marketplace as a shop: the filter rail beside the grid (folded into "Filters" on a narrow page), 3 to 6 columns of tiles,
// tiles that do not overlap, the price, two lines of title and "seller  ·  1.5k downloads" inside each tile, and the item page.
void CheckMarketShop(float winW) {
    const float W = PageW(winW);
    const L::MarketPage mp = L::LayoutMarketPage(W, Px(236.0f), Px(24.0f), Px(176.0f), Px(16.0f), Px(760.0f));
    if (mp.railShown) { CHECK(!L::Overlaps(L::Rect{0, 0, mp.rail.w, 100}, L::Rect{mp.grid.x, 0, mp.grid.w, 100})); CHECK(mp.grid.R() <= W + 0.5f); }
    const L::Grid g = L::FitGrid(mp.railShown ? mp.grid.w : W, Px(176.0f), Px(16.0f), 6);
    CHECK(g.cols >= 1 && g.cols <= 6);
    if (g_scale == 1.0f && winW >= 1280) CHECK(g.cols >= 3);   // a real grid at the normal and large sizes
    CHECK(g.cardW >= Px(176.0f) - 0.5f || g.cols == 1);
    const float lh = LineH(), priceH = LineH() * 1.2f;
    const L::MarketTile t = L::LayoutMarketTile(g.cardW, lh, priceH, Px(12.0f), Px(32.0f));
    std::vector<L::Rect> parts{t.price, t.title, t.meta};
    CHECK(NoOverlaps(parts));
    for (const auto& r : parts) CHECK(L::Inside(r, t.tile));
    CHECK(L::Inside(t.heart, t.picture));
    CHECK(std::abs(t.picture.h - std::floor(t.tile.w * 0.75f)) < 1.0f);   // 4:3
    CHECK(TextW("Free") * 1.2f <= t.price.w);
    // "maya  ·  4.2k downloads" (the seller shortened when it must, the count never)
    CHECK(TextW("4.2k downloads") + Px(30.0f) + Px(10.0f) <= t.meta.w + 0.5f);
    CHECK(TextW("Free") * 1.2f + Px(10.0f) + TextW("4.6 stars") <= t.price.w + 0.5f);   // the rating sits right of the price
    CHECK(TextW("12k downloads") + Px(30.0f) + Px(10.0f) <= t.meta.w + 0.5f);
    std::vector<L::Rect> tiles;
    for (int i = 0; i < g.cols * 3; ++i) tiles.push_back(L::GridCell(g, i, t.tile.h));
    CHECK(NoOverlaps(tiles));
    for (const auto& r : tiles) CHECK(r.R() <= (mp.railShown ? mp.grid.w : W) + 0.5f);
    // The item page.
    const L::MarketDetail d = L::LayoutMarketDetail(W, Px(28.0f), Px(320.0f));
    CHECK(!L::Overlaps(d.picture, L::Rect{d.info.x, d.info.y, d.info.w, 400.0f}));
    CHECK(d.info.w >= Px(320.0f) - 0.5f || !d.twoColumns);
    CHECK(d.info.R() <= W + 0.5f && d.picture.R() <= W + 0.5f);
}

// The Community showcase: the feed of big cards and the rail (top creators, friends playing) only at the wide sizes; the rail
// never covers the feed, the feed keeps two cards a row beside it, and the tabs and the "Share your creation" box fit.
void CheckShowcase(float winW) {
    const float W = PageW(winW);
    const L::Showcase sc = L::LayoutShowcase(W, Px(300.0f), Px(20.0f), Px(280.0f), Px(28.0f), Px(1240.0f));
    CHECK(sc.feed.R() <= W + 0.5f);
    if (sc.railShown) {
        CHECK(!L::Overlaps(L::Rect{0, 0, sc.feed.w, 100}, L::Rect{sc.rail.x, 0, sc.rail.w, 100}));
        CHECK(sc.rail.R() <= W + 0.5f);
        CHECK(L::FitGrid(sc.feed.w, Px(300.0f), Px(20.0f), 3).cols >= 2);
    }
    if (winW >= 1920 && g_scale == 1.0f) CHECK(sc.railShown);   // the rail at the wide size
    if (winW <= 960) CHECK(!sc.railShown);                       // never at the small one
    const L::Grid g = L::FitGrid(sc.feed.w, Px(300.0f), Px(20.0f), 3);
    CHECK(g.cols >= 1 && g.cols <= 3);
    CHECK(g.cardW >= Px(300.0f) - 0.5f || g.cols == 1);
    if (winW >= 1280 && g_scale == 1.0f) CHECK(g.cols >= 2);
    for (int i = 0; i + 1 < 6; ++i) CHECK(!L::Overlaps(L::GridCell(g, i, Px(360.0f)), L::GridCell(g, i + 1, Px(360.0f))));
    CheckChips(sc.feed.w, {"Featured", "New", "Popular", "Following", "Your creations"});
    // The share box: the text on the left, the library pick and Share on the right (or under the text when narrow).
    const float shareW = L::ButtonW(TextW("Share"), Px(14.0f), Px(100.0f)), btnH = LineH() + Px(14.0f);
    const L::ShareBox b = L::LayoutShareBox(sc.feed.w, Px(16.0f), TextW("Showcase your game or level") * 1.15f, LineH() * 1.15f, LineH(),
                                            Px(260.0f), shareW, btnH, Px(10.0f));
    const L::Rect box{0, 0, sc.feed.w, b.h};
    CHECK(NoOverlaps({b.title, b.sub, b.combo, b.share}));
    for (const auto& r : {b.title, b.sub, b.combo, b.share}) CHECK(L::Inside(r, box));
    CHECK(b.combo.w >= Px(120.0f));
    if (winW >= 1280 && g_scale == 1.0f) CHECK(!b.stacked);
}

// A folded side panel is an icon rail: Forums' folded sections give one icon per item (All posts, each joined section,
// Discover, Create, Staff tools) and never an empty rail; the icons sit inside the rail, one under another, never overlapping.
void CheckIconRail(float winW, float winH) {
    for (int joined : {0, 1, 4}) for (bool create : {false, true}) for (bool staff : {false, true}) {
        const auto ent = L::ForumRailEntries(joined, create, staff);
        int icons = 0, sections = 0;
        bool sawDiv = false;
        std::vector<bool> div;
        for (auto e : ent) {
            div.push_back(e == L::ForumRail::Divider);
            if (e == L::ForumRail::Divider) sawDiv = true; else ++icons;
            if (e == L::ForumRail::Section) ++sections;
        }
        CHECK(icons == 1 + joined + 1 + (create ? 1 : 0) + (staff ? 1 : 0));   // one icon per item
        CHECK(icons >= 2 && sawDiv);                                          // never an empty rail
        CHECK(sections == joined);
        CHECK(ent.front() == L::ForumRail::AllPosts);
        const float railW = Px(60.0f), d = Px(40.0f);
        const auto rs = L::LayoutIconRail(div, railW, Px(48.0f), d, Px(8.0f), Px(6.0f));
        CHECK(rs.size() == ent.size());
        for (std::size_t i = 0; i < rs.size(); ++i) {
            CHECK(rs[i].x >= 0 && rs[i].R() <= railW + 0.5f);
            if (i) CHECK(rs[i].y >= rs[i - 1].B());
        }
        if (joined <= 4) CHECK(rs.back().B() <= winH - Px(200.0f) || g_scale > 1.0f || winH < 700);   // fits the page at the normal sizes
    }
    (void)winW;
}

// The Marketplace: the filter rail folds to an icon rail (one icon per entry: search, all, each category, rating, sort; the grid
// gets the room), Today's picks and Featured show approved listings only, never just repeat a short grid, and the seller's name
// gets the whole line before it is shortened.
void CheckMarketFold(float winW) {
    const float W = PageW(winW);
    const L::MarketPage open = L::LayoutMarketPage(W, Px(236.0f), Px(24.0f), Px(176.0f), Px(16.0f), Px(760.0f), false, Px(60.0f));
    const L::MarketPage fold = L::LayoutMarketPage(W, Px(236.0f), Px(24.0f), Px(176.0f), Px(16.0f), Px(760.0f), true, Px(60.0f));
    if (open.railShown) {
        CHECK(fold.iconRail && !fold.railShown);
        CHECK(fold.grid.w > open.grid.w);                    // folded: a wider grid
        CHECK(fold.tiles.cols >= open.tiles.cols);
        CHECK(fold.grid.x >= fold.rail.R());                  // the rail never covers the grid
    } else CHECK(!fold.iconRail);                            // narrow pages: the Filters overlay, no rail
    const auto ent = L::MarketRailEntries(9);
    int icons = 0, cats = 0; std::vector<bool> div;
    for (auto e : ent) { div.push_back(e == L::MarketRail::Divider); if (e != L::MarketRail::Divider) ++icons; if (e == L::MarketRail::Category) ++cats; }
    CHECK(cats == 9 && icons == 9 + 4);
    const auto rs = L::LayoutIconRail(div, Px(60.0f), Px(46.0f), Px(38.0f), Px(6.0f), Px(6.0f));
    CHECK(rs.size() == ent.size());
    for (std::size_t i = 1; i < rs.size(); ++i) CHECK(rs[i].y >= rs[i - 1].B());
    // Approved only in the picks; unapproved listings are the seller's and staff's.
    CHECK(L::MarketFeaturable("approved") && L::MarketFeaturable(""));
    CHECK(!L::MarketFeaturable("pending") && !L::MarketFeaturable("rejected"));
    CHECK(L::MarketListingVisible("approved", false, false));
    CHECK(!L::MarketListingVisible("rejected", false, false) && L::MarketListingVisible("rejected", true, false) && L::MarketListingVisible("pending", false, true));
    // No Today's picks with one item (it would show the same tile twice), nor when the grid is one row.
    CHECK(!L::ShowTodaysPicks(1, 1, 4));
    CHECK(!L::ShowTodaysPicks(5, 20, 4));
    CHECK(!L::ShowTodaysPicks(8, 4, 4));
    CHECK(L::ShowTodaysPicks(8, 20, 4));
    // The seller's name: the whole line when the downloads do not fit beside it; shortened only past that.
    const float lineW = Px(176.0f) - 2 * Px(12.0f);
    const float nameW = TextW("imanfakhargomi"), dlW = TextW("0 downloads");
    const L::TileMeta tm = L::FitTileMeta(nameW, dlW, lineW, Px(10.0f));
    CHECK(tm.sellerW >= std::min(nameW, lineW) - 0.5f);   // never cut while the line has room for it
    if (nameW + Px(10.0f) + dlW > lineW) CHECK(!tm.showDownloads);
    CHECK(L::FitTileMeta(TextW("maya"), dlW, lineW, Px(10.0f)).showDownloads);
    // A few listings: bigger tiles.
    CHECK(L::MarketTileMinW(1, 176.0f) > L::MarketTileMinW(12, 176.0f));
}

// The top of a chat conversation: the header and the friend's "Playing ... / Join game" bar (launcher/main.cpp,
// DrawChatGameBar). Owner bug 2026-11-25: at UI scale 1.5 the bar's second line ran over the first messages.
void CheckChatTop(float winW) {
    const std::vector<std::string> labels = {"Join game", "Ask to join", "Joined", "Asked"};
    float bw = 116.0f;   // the old fixed width is the least
    for (const auto& l : labels) bw = std::max(bw, L::ButtonW(TextW(l), Px(12.0f), 0.0f));
    for (float convW : {320.0f, winW * 0.45f, winW - 300.0f}) {
        const L::ChatHeaderLayout t = L::LayoutChatTop(convW, LineH(), LineH() * 1.12f, bw, true);
        CHECK(t.headerH >= LineH() * 1.12f);                                    // the name fits the header
        CHECK(t.barRect.y >= t.headerH);                                        // the bar starts under the header
        for (const L::Rect& r : {t.icon, t.title, t.detail, t.button}) CHECK(L::Inside(r, t.barRect));   // nothing leaves the bar
        CHECK(NoOverlaps({t.icon, t.title, t.button}) && NoOverlaps({t.icon, t.detail, t.button}));
        CHECK(!L::Overlaps(t.title, t.detail));
        CHECK(t.listTop >= t.barRect.B());                                      // the messages start below the bar
        CHECK(t.button.h >= LineH() + 4.0f);                                    // the button label fits its height
        for (const auto& l : labels) CHECK(t.button.w >= TextW(l) + Px(8.0f));  // and its width
        const L::ChatHeaderLayout none = L::LayoutChatTop(convW, LineH(), LineH() * 1.12f, bw, false);
        CHECK(!none.bar && none.listTop >= none.headerH && none.listTop < t.listTop);
    }
}


// The Play page (launcher/main.cpp, tab 1): the header toolbar, the "Continue playing" banner, the tiles and the list
// columns at every window size, including the launcher's default 1600 x 1135 and a narrow window.
void CheckPlayPage(float winW) {
    const float pw = PageW(winW);
    // Header: search, sort, the List / Grid toggle, Folder, Refresh.
    CheckToolbar(pw, {"Favourites first", "List  Grid", "Folder", "Refresh"});
    // Shelves.
    std::vector<std::string> shelves;
    for (int i = 0; i < (int)L::PlayShelf::Count; ++i) shelves.push_back(std::string(L::PlayShelfName((L::PlayShelf)i)) + " 12");
    CheckChips(pw, shelves);
    const float btnH = LineH() + Px(18.0f), pad = Px(22.0f), gap = Px(10.0f);
    const float playW = L::ButtonW(TextW("Play"), Px(30.0f), Px(150.0f)), moreW = L::ButtonW(TextW("Details"), Px(16.0f), Px(96.0f));
    for (float heroW : {pw, pw * 0.6f, 300.0f}) {   // the whole page, beside the details sheet, and a narrow page
        const L::PlayHero h = L::LayoutPlayHero(heroW, Px(190.0f), pad, LineH(), LineH() * 1.7f, playW, moreW, btnH, gap, Px(560.0f));
        for (const L::Rect& r : {h.kicker, h.title, h.meta, h.play}) CHECK(L::Inside(r, h.hero));
        CHECK(NoOverlaps({h.kicker, h.title, h.meta, h.play}));
        CHECK(h.play.w >= TextW("Play") + Px(8.0f));
        if (h.more.w > 0) { CHECK(L::Inside(h.more, h.hero)); CHECK(!L::Overlaps(h.more, h.play)); CHECK(!L::Overlaps(h.more, h.meta)); }
        if (!h.artFull) { CHECK(h.title.R() <= h.art.x + 0.5f); CHECK(h.play.R() <= h.art.x + 0.5f || h.more.w == 0); }
        CHECK(h.artFull == (heroW < Px(560.0f)));
    }
    // Tiles: 2 to 6 a row; everything inside its tile.
    const L::Grid g = L::FitGrid(pw, Px(200.0f), Px(14.0f), 6);
    CHECK(g.cols >= 1 && g.cols <= 6);
    CHECK(g.cols * g.cardW + (g.cols - 1) * g.gap <= pw + 0.5f);
    const L::PlayTile t = L::LayoutPlayTile(g.cardW, LineH(), Px(12.0f), Px(54.0f), Px(28.0f));
    for (const L::Rect& r : {t.cover, t.play, t.star, t.title, t.meta}) CHECK(L::Inside(r, t.tile));
    CHECK(L::Inside(t.play, t.cover) && L::Inside(t.star, t.cover));
    CHECK(!L::Overlaps(t.play, t.star));
    CHECK(NoOverlaps({t.cover, t.title, t.meta}));
    // List columns: the name keeps its room, the columns never overlap, a narrow list drops columns.
    // The list gets the whole page, or what the details sheet leaves (launcher/main.cpp: the sheet is 27% of the page, 290..360 px,
    // beside the list only from 900 px).
    const float sheetW = std::clamp(pw * 0.27f, Px(290.0f), Px(360.0f));
    const L::SidePanel side = L::LayoutSidePanel(pw, sheetW, Px(16.0f), Px(900.0f), false, false);
    CHECK(side.contentW > 0.0f && (side.shown || side.contentW == pw));
    for (float lw : {pw, side.contentW}) {
        const float rowH = Px(60.0f);
        const float lastW = L::ButtonW(TextW("Last played"), 0, Px(96.0f)), timeW = L::ButtonW(TextW("Play time"), 0, Px(92.0f));
        const float sizeW = L::ButtonW(TextW("999.9 MB"), 0, Px(76.0f)), actW = Px(40.0f) + gap + playW * 0.6f;
        const L::PlayColumns c = L::LayoutPlayColumns(lw, rowH, Px(12.0f), Px(42.0f), lastW, timeW, sizeW, actW, Px(150.0f), gap, playW * 0.6f);
        const L::Rect row{0, 0, lw, rowH};
        std::vector<L::Rect> cols{c.icon, c.name, c.actions};
        if (c.showLast) cols.push_back(c.last);
        if (c.showTime) cols.push_back(c.time);
        if (c.showSize) cols.push_back(c.size);
        CHECK(NoOverlaps(cols));
        for (const L::Rect& r : cols) CHECK(L::Inside(r, row) || r.w == 0);
        CHECK(c.name.w >= Px(150.0f) || (!c.showLast && c.narrowActions && c.name.w >= Px(60.0f)));   // a very narrow row: Play only, the name keeps some room
        CHECK(!c.showSize || c.showTime); CHECK(!c.showTime || c.showLast);   // dropped from the right: Size, Play time, Last played
        CHECK(!c.narrowActions || !c.showLast);                                // the buttons shrink only once every column is gone
        if (lw >= Px(1000.0f)) CHECK(c.showSize && !c.narrowActions);
    }
}

void RunAll() {
    const float sizes[3][2] = {{960, 600}, {1280, 800}, {1920, 1080}};
    for (const auto& s : sizes) {
        const float pw = PageW(s[0]);
        // Marketplace: toolbar (search, sort, wishlist, refresh, publish), chips, cards.
        CheckToolbar(pw, {"Newest", "Wishlist", "Refresh", "Publish an item"});
        CheckChips(pw, {"All", "Games", "Models", "Textures", "Sprites", "Audio", "Scripts", "Templates", "Tools"});
        CheckListCard(pw, 300.0f, 3, {"Get", "Download"}, 2);
        // Community: toolbar (search, refresh, open folder, publish), library cards, template cards.
        CheckToolbar(pw, {"Refresh", "Open folder", "Publish to Marketplace"});
        CheckListCard(pw, 280.0f, 4, {"Play"}, 2);
        // Notifications flyout.
        CheckNotifications(s[0], s[1]);
        CheckNotes(s[0], s[1]);
        CheckTestBuilds(s[0]);
        CheckAchievementsHeader(s[0]);
        CheckMarketShop(s[0]);
        CheckShowcase(s[0]);
        CheckIconRail(s[0], s[1]);
        CheckMarketFold(s[0]);
        CheckChatTop(s[0]);
    }
    for (float w : {720.0f, 960.0f, 1280.0f, 1600.0f, 1920.0f}) CheckPlayPage(w);   // 1600: the launcher's default window; 720: a narrow one
}

}  // namespace

int main() {
    RUN_SUITE("launcher_page_layout");
    // Pure maths first.
    {
        const auto g = L::FitGrid(1000, 300, 12, 3);
        CHECK((g.cols) == 3);
        CHECK(g.cardW * 3 + 24 <= 1000.5f);
        CHECK(L::FitGrid(250, 300, 12, 3).cols == 1);   // narrower than one card: one column, card shrinks
        CHECK(L::FitGrid(5000, 300, 12, 3).cols == 3);  // never more than the maximum
        CHECK(L::Overlaps({0, 0, 10, 10}, {5, 5, 10, 10}));
        CHECK(!L::Overlaps({0, 0, 10, 10}, {10, 0, 10, 10}));   // touching is fine
        const auto f = L::Flow({50, 50, 50}, 20, 120, 6, 6);
        CHECK_NEAR(f[1].x, 56.0f, 1e-4); CHECK_NEAR(f[2].y, 26.0f, 1e-4); CHECK_NEAR(f[2].x, 0.0f, 1e-4);
        const auto wide = L::Flow({500}, 20, 120, 6, 6);
        CHECK_NEAR(wide[0].w, 120.0f, 1e-4);   // narrowed, never past the row
        const auto t = L::LayoutToolbar(300, 220, 0, {90, 90}, 30, 8);
        CHECK(t.stacked); CHECK_NEAR(t.search.w, 300.0f, 1e-4); CHECK(t.buttons[0].y > 30.0f);
        const auto t2 = L::LayoutToolbar(900, 220, 420, {90, 90}, 30, 8);
        CHECK(!t2.stacked); CHECK_NEAR(t2.buttons[1].R(), 900.0f, 1e-4); CHECK(t2.search.w <= 420.0f);
        CHECK(L::NotesShown(6, false).shown == 6 && !L::NotesShown(6, false).toggle);
        CHECK(L::NotesShown(60, false).shown == L::kNotesCollapsed && L::NotesShown(60, true).shown == 60);
        // Foldable side panels (Servers' channels: 224 px, narrow below 560; Forums' sections: 220 px, narrow below 820).
        for (const auto& pnl : {std::pair<float, float>{224.0f, 560.0f}, std::pair<float, float>{220.0f, 820.0f}}) {
            const float wide = 1400.0f, narrow = pnl.second - 100.0f;
            const L::SidePanel open = L::LayoutSidePanel(wide, pnl.first, 10.0f, pnl.second, false, false);
            const L::SidePanel folded = L::LayoutSidePanel(wide, pnl.first, 10.0f, pnl.second, true, false);
            CHECK(open.shown && !open.overlay); CHECK(!folded.shown && !folded.overlay);
            CHECK(folded.contentW > open.contentW);                          // folding gives the content the panel's room
            CHECK(std::abs(folded.contentW - open.contentW - (pnl.first + 10.0f)) < 0.01f);
            const L::SidePanel n0 = L::LayoutSidePanel(narrow, pnl.first, 10.0f, pnl.second, false, false);
            const L::SidePanel n1 = L::LayoutSidePanel(narrow, pnl.first, 10.0f, pnl.second, false, true);
            CHECK(!n0.shown && !n0.overlay);                                  // narrow: starts folded
            CHECK(!n1.shown && n1.overlay && n1.contentW == narrow);         // and opens over the content, which keeps its width
        }
        // The Marketplace's filters, views and item page.
        {
            L::MarketQuery q;
            CHECK(L::MarketMatches(q, 2, 0.0f, false, false, "neon drift"));
            q.kind = 2; CHECK(!L::MarketMatches(q, 3, 5.0f, false, false, "x")); CHECK(L::MarketMatches(q, 2, 0.0f, false, false, "x"));
            q.kind = -1; q.minRating = 4.0f; CHECK(!L::MarketMatches(q, 0, 3.9f, false, false, "x")); CHECK(L::MarketMatches(q, 0, 4.5f, false, false, "x"));
            q.minRating = 0.0f; q.needle = "drift"; CHECK(L::MarketMatches(q, 0, 0.0f, false, false, "neon drift by maya")); CHECK(!L::MarketMatches(q, 0, 0.0f, false, false, "pixel dungeon"));
            q.needle.clear(); q.view = 1; CHECK(!L::MarketMatches(q, 0, 0.0f, true, false, "x")); CHECK(L::MarketMatches(q, 0, 0.0f, false, true, "x"));
            q.view = 2; CHECK(L::MarketMatches(q, 0, 0.0f, true, false, "x")); CHECK(!L::MarketMatches(q, 0, 0.0f, false, true, "x"));
            q.Open(42); CHECK(q.detail == 42); q.Back(); CHECK(q.detail == 0); CHECK(q.view == 2);   // Back keeps the view and filters
            CHECK(L::ShortCount(980) == "980"); CHECK(L::ShortCount(1530) == "1.5k"); CHECK(L::ShortCount(42100) == "42k"); CHECK(L::ShortCount(1200000) == "1.2M");
        }
        // The Play page's library: shelves, search and the four persisted sorts.
        {
            std::vector<L::PlayGame> gs(5);
            gs[0].name = "Orbit Hopper"; gs[0].path = "/g/orbit"; gs[0].plays = 2; gs[0].recentRank = 1; gs[0].lastPlayed = 100;
            gs[1].name = "asteroids";    gs[1].path = "/g/ast";   gs[1].favorite = true; gs[1].installed = true;
            gs[2].name = "Neon Drift";   gs[2].path = "/c/neon";  gs[2].plays = 9; gs[2].recentRank = 0; gs[2].lastPlayed = 200; gs[2].installed = true;
            gs[3].name = "Zed";          gs[3].path = "/g/zed";   gs[3].favorite = true; gs[3].plays = 1; gs[3].lastPlayed = 50;
            gs[4].name = "Paper Planes"; gs[4].path = "/w/pp";    gs[4].web = true;
            auto names = [&](const std::vector<std::size_t>& o) { std::string r; for (std::size_t i : o) r += gs[i].name + ","; return r; };
            CHECK(names(L::PlayOrder(gs, 0, L::PlayShelf::All, "")) == "asteroids,Zed,Neon Drift,Orbit Hopper,Paper Planes,");   // favourites first, then A-Z (case ignored)
            CHECK(names(L::PlayOrder(gs, 1, L::PlayShelf::All, "")) == "asteroids,Neon Drift,Orbit Hopper,Paper Planes,Zed,");
            CHECK(names(L::PlayOrder(gs, 2, L::PlayShelf::All, "")) == "Neon Drift,Orbit Hopper,Zed,asteroids,Paper Planes,");   // the recent list, then launch time, then A-Z
            CHECK(names(L::PlayOrder(gs, 3, L::PlayShelf::All, "")) == "Neon Drift,Orbit Hopper,Zed,asteroids,Paper Planes,");
            CHECK(names(L::PlayOrder(gs, 1, L::PlayShelf::Favorites, "")) == "asteroids,Zed,");
            CHECK(names(L::PlayOrder(gs, 1, L::PlayShelf::Recent, "")) == "Neon Drift,Orbit Hopper,Zed,");
            CHECK(names(L::PlayOrder(gs, 1, L::PlayShelf::Installed, "")) == "asteroids,Neon Drift,");
            CHECK(names(L::PlayOrder(gs, 1, L::PlayShelf::Yours, "")) == "Orbit Hopper,Zed,");
            CHECK(names(L::PlayOrder(gs, 1, L::PlayShelf::Web, "")) == "Paper Planes,");
            CHECK(names(L::PlayOrder(gs, 1, L::PlayShelf::All, "o")) == "asteroids,Neon Drift,Orbit Hopper,");   // search: name, any case
            CHECK(names(L::PlayOrder(gs, 1, L::PlayShelf::All, "ORBIT")).empty());   // the needle is lower case already
            CHECK(names(L::PlayOrder(gs, 1, L::PlayShelf::All, "zzz")).empty());
            gs[0].inCollection = false;                                              // a collection filter that leaves it out
            CHECK(names(L::PlayOrder(gs, 1, L::PlayShelf::Recent, "")) == "Neon Drift,Zed,");
            CHECK(L::PlayShelfCount(gs, L::PlayShelf::All) == 4);
            gs[0].inCollection = true;
            CHECK(L::PlayShelfCount(gs, L::PlayShelf::All) == 5 && L::PlayShelfCount(gs, L::PlayShelf::Favorites) == 2);
            CHECK(L::PlayContinueIndex(gs) == 2);                                    // played last
            gs[2].recentRank = L::kPlayNotRecent; gs[0].recentRank = L::kPlayNotRecent;
            CHECK(L::PlayContinueIndex(gs) == 2);                                    // no recent list: the newest launch
            CHECK(L::PlayContinueIndex(std::vector<L::PlayGame>(gs.begin() + 4, gs.end())) == -1);   // a web game is never "continued"
            CHECK(L::PlayContinueIndex({}) == -1);
            // Arrow keys in a grid of 7 with 3 a row, and in the list.
            CHECK(L::PlayKeyMove(-1, 7, 3, 1, 0) == 0);                              // nothing selected: the first
            CHECK(L::PlayKeyMove(0, 7, 3, 1, 0) == 1 && L::PlayKeyMove(0, 7, 3, -1, 0) == 0);
            CHECK(L::PlayKeyMove(1, 7, 3, 0, 1) == 4 && L::PlayKeyMove(4, 7, 3, 0, 1) == 4);   // no tile under 4 in the last row: stays
            CHECK(L::PlayKeyMove(5, 7, 3, 0, -1) == 2 && L::PlayKeyMove(2, 7, 3, 0, -1) == 2);
            CHECK(L::PlayKeyMove(6, 7, 3, 1, 0) == 6);
            CHECK(L::PlayKeyMove(3, 7, 1, 0, 1) == 4 && L::PlayKeyMove(3, 7, 1, 0, -1) == 2);
            CHECK(L::PlayKeyMove(3, 0, 3, 1, 0) == -1);
            CHECK(L::PlaytimeText(0).empty()); CHECK(L::PlaytimeText(30) == "Under a minute");
            CHECK(L::PlaytimeText(45 * 60) == "45 min"); CHECK(L::PlaytimeText(3 * 3600 + 20 * 60 + 5) == "3 h 20 min");
            CHECK(L::PlaytimeText(2 * 3600) == "2 h"); CHECK(L::PlaytimeText(150 * 3600 + 60) == "150 h");
        }
        // The Notifications page's filters and empty state.
        CHECK(L::NotifMatches(0, 2, false));                                   // All: everything
        CHECK(L::NotifMatches(1, 0, true) && !L::NotifMatches(1, 0, false));  // Unread
        CHECK(L::NotifMatches(2, 1, false) && !L::NotifMatches(2, 2, false)); // Friends
        CHECK(L::NotifMatches(3, 2, true) && !L::NotifMatches(3, 3, true));   // Messages
        CHECK(L::NotifMatches(4, 3, false) && !L::NotifMatches(4, 0, false)); // Updates
        CHECK(L::NotifMatches(5, 0, false) && !L::NotifMatches(5, 1, false)); // System
        CHECK(std::string(L::NotifEmptyTitle(true, 0, 0)) == "You're signed out");
        CHECK(std::string(L::NotifEmptyTitle(false, 0, 0)) == "You're all caught up");
        CHECK(std::string(L::NotifEmptyTitle(false, 5, 1)) == "You're all caught up");   // nothing unread
        CHECK(std::string(L::NotifEmptyTitle(false, 5, 3)) == "Nothing here");           // a filter with no match
        // The chat's game bar at every UI scale (0.8 .. 1.6, the Settings range): the old fixed bar (40 px high, second line
        // at +21) left the second line below the bar at 1.5; the layout grows with the text.
        for (float sc : {0.8f, 1.0f, 1.25f, 1.5f, 1.6f}) {
            const float lh = 17.0f * sc;
            const L::ChatHeaderLayout t = L::LayoutChatTop(600, lh, lh * 1.12f, 140, true);
            CHECK(t.detail.B() <= t.barRect.B() && t.title.y >= t.barRect.y && t.listTop >= t.barRect.B());
            CHECK(L::Inside(t.button, t.barRect) && t.button.x >= t.title.R());
            if (sc >= 1.5f) CHECK(21.0f + lh > 40.0f);   // what the old fixed bar did: its second line passed its bottom
        }
        CHECK_NEAR(L::LayoutChatTop(600, 17, 19, 116, true).headerH, 34.0f, 1e-4);   // scale 1 keeps the old header height
        const L::Rect fl = L::FlyoutRect(700, 500, 232, 480, 640, 320, 300, 12);
        CHECK(L::Inside(fl, L::Rect{0, 0, 700, 500})); CHECK_NEAR(fl.y + fl.h, 488.0f, 1e-4);
    }
#ifdef OKAY_LAYOUT_REAL_FONT
    // The launcher's text as OkayUI draws it (the launcher is OkayUI only): its look, Roboto Medium at 17 px (launcher/main.cpp).
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    OkayUI::UseLauncherLook(true);
    OkayUI::Style().textScale = 17.0f / 8.0f;
    OkayUI::UseImGuiCoordinates(true);
#endif
    for (float scale : {1.0f, 1.5f}) {
        g_scale = scale;
#ifdef OKAY_LAYOUT_REAL_FONT
        io.FontGlobalScale = scale;   // what Settings > UI scale sets
        io.DisplaySize = ImVec2(1920, 1080);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::Begin("measure");
        CHECK(ImGui::GetTextLineHeight() > 16.0f * scale);   // the real font, scaled
#endif
        RunAll();
#ifdef OKAY_LAYOUT_REAL_FONT
        ImGui::End();
        ImGui::Render();
#endif
    }
#ifdef OKAY_LAYOUT_REAL_FONT
    ImGui::DestroyContext();
#endif
    TEST_MAIN_RESULT();
}
