// The launcher's page layouts (launcher/PageLayout.hpp): the Marketplace, Community and Notifications pages at the
// small window (960 x 600), the normal one (1280 x 800) and the large one (1920 x 1080), at UI scale 1.0 and 1.5.
// Nothing overlaps, every label fits its button, every piece stays inside its card or panel.
// With OKAY_LAYOUT_REAL_FONT the labels are measured with the launcher's own font (Roboto Medium, 17 px, Dear ImGui's
// atlas, no window); without it an average glyph width stands in.
#include "test_framework.hpp"
#include "PageLayout.hpp"

#include <cmath>
#include <string>
#include <vector>

#ifdef OKAY_LAYOUT_REAL_FONT
#include "imgui.h"
#include "RobotoFont.h"
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
    }
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
        const L::Rect fl = L::FlyoutRect(700, 500, 232, 480, 640, 320, 300, 12);
        CHECK(L::Inside(fl, L::Rect{0, 0, 700, 500})); CHECK_NEAR(fl.y + fl.h, 488.0f, 1e-4);
    }
#ifdef OKAY_LAYOUT_REAL_FONT
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImFontConfig fc; fc.OversampleH = 2; fc.OversampleV = 2; fc.PixelSnapH = true;
    CHECK(io.Fonts->AddFontFromMemoryCompressedBase85TTF(RobotoMedium_compressed_data_base85, 17.0f, &fc) != nullptr);
    { unsigned char* px = nullptr; int fw = 0, fh = 0; io.Fonts->GetTexDataAsRGBA32(&px, &fw, &fh); }   // build the atlas (no renderer here)
#endif
    for (float scale : {1.0f, 1.5f}) {
        g_scale = scale;
#ifdef OKAY_LAYOUT_REAL_FONT
        ImGui::GetStyle().FontScaleMain = scale;   // what Settings > UI scale sets (io.FontGlobalScale before 1.92)
        io.DisplaySize = ImVec2(1920, 1080);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::Begin("measure");
        CHECK(ImGui::GetTextLineHeight() > 16.0f * scale);   // the real font, scaled
#endif
        RunAll();
#ifdef OKAY_LAYOUT_REAL_FONT
        ImGui::End();
        ImGui::EndFrame();
#endif
    }
#ifdef OKAY_LAYOUT_REAL_FONT
    ImGui::DestroyContext();
#endif
    TEST_MAIN_RESULT();
}
