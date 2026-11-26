// PageLayout.hpp - where the launcher's page pieces go: the page toolbar, wrapping chip rows, card grids and the
// listing card used by the Marketplace and Community pages (and the rows of the Notifications panel).
// Our own code, written for this launcher from plain box-layout arithmetic. Pure maths, no ImGui, so the layouts are
// tested headless (tests/test_launcher_page_layout.cpp): text fits its button, nothing overlaps, nothing leaves its card,
// at the small window size and at UI scale 1.5.
#pragma once
#include "../editor/EditorLayoutMath.hpp"   // okay::edlayout::Rc, Overlaps, Inside: the one rectangle test, shared with the editor (std only)
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace okay::launcherlayout {

/// A box by its top-left corner and size (what ImGui hands out); the tests go through the editor's Rc.
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float R() const { return x + w; }
    float B() const { return y + h; }
    okay::edlayout::Rc Rc() const { return okay::edlayout::MakeRc(x, y, w, h); }
};

/// Do two rectangles share some area (touching edges do not count)?
inline bool Overlaps(const Rect& a, const Rect& b) { return okay::edlayout::Overlaps(a.Rc(), b.Rc()); }
/// Is `in` inside `out` (`out` grown by half a pixel, for sums of fractional widths)?
inline bool Inside(const Rect& in, const Rect& out) {
    return okay::edlayout::Inside(okay::edlayout::MakeRc(out.x - 0.5f, out.y - 0.5f, out.w + 1.0f, out.h + 1.0f), in.Rc());
}

/// The width of a button whose label is `textW` wide: the label plus padding on both sides, never under `minW`.
inline float ButtonW(float textW, float padX, float minW) { return std::max(minW, std::ceil(textW + 2.0f * padX)); }

/// A grid of equal cards: as many columns of at least `minCardW` as fit in `availW` (1..maxCols), the cards share
/// the width exactly (whole pixels).
struct Grid { int cols = 1; float cardW = 0; float gap = 0; };
inline Grid FitGrid(float availW, float minCardW, float gap, int maxCols) {
    Grid g; g.gap = gap;
    g.cols = std::clamp((int)std::floor((availW + gap) / (minCardW + gap)), 1, std::max(1, maxCols));
    g.cardW = std::max(1.0f, std::floor((availW - (g.cols - 1) * gap) / g.cols));
    return g;
}
/// Card `i` of a grid whose top-left is (0, 0) and whose cards are `cardH` high.
inline Rect GridCell(const Grid& g, int i, float cardH) {
    return Rect{(i % g.cols) * (g.cardW + g.gap), (float)(i / g.cols) * (cardH + g.gap), g.cardW, cardH};
}
inline float GridHeight(const Grid& g, int n, float cardH) {
    if (n <= 0) return 0.0f;
    const int rows = (n + g.cols - 1) / g.cols;
    return rows * cardH + (rows - 1) * g.gap;
}

/// Items of the given widths laid left to right from (0, 0), wrapping onto a new row when the next one would pass
/// `availW` (an item wider than the whole row gets a row to itself and is narrowed to fit).
inline std::vector<Rect> Flow(const std::vector<float>& widths, float itemH, float availW, float gapX, float gapY) {
    std::vector<Rect> out;
    float x = 0, y = 0;
    for (float w : widths) {
        w = std::min(w, availW);
        if (x > 0 && x + w > availW + 0.01f) { x = 0; y += itemH + gapY; }
        out.push_back(Rect{x, y, w, itemH});
        x += w + gapX;
    }
    return out;
}
inline float FlowHeight(const std::vector<Rect>& r) {
    float b = 0;
    for (const Rect& q : r) b = std::max(b, q.B());
    return b;
}

/// A page toolbar: a search field that takes the spare width (at least `searchMinW`) and buttons after it. When the
/// field and every button fit on one row they share it (buttons on the right); otherwise the search field gets the
/// first row to itself and the buttons wrap below it, left-aligned.
struct Toolbar { Rect search; std::vector<Rect> buttons; float height = 0; bool stacked = false; };
inline Toolbar LayoutToolbar(float availW, float searchMinW, float searchMaxW, const std::vector<float>& buttonW, float h, float gap) {
    Toolbar t;
    float bw = 0;
    for (float w : buttonW) bw += w + gap;
    if (searchMinW + bw <= availW) {
        const float sw = std::min(std::max(searchMinW, availW - bw), searchMaxW > 0 ? searchMaxW : availW);
        t.search = Rect{0, 0, sw, h};
        float x = availW;
        for (std::size_t i = buttonW.size(); i-- > 0;) { x -= buttonW[i]; t.buttons.insert(t.buttons.begin(), Rect{x, 0, buttonW[i], h}); x -= gap; }
        t.height = h;
        return t;
    }
    t.stacked = true;
    t.search = Rect{0, 0, availW, h};
    t.buttons = Flow(buttonW, h, availW, gap, gap);
    t.height = h + gap + FlowHeight(t.buttons);   // (before the buttons move under the search field)
    for (Rect& r : t.buttons) r.y += h + gap;
    return t;
}

/// The listing card (Marketplace, Community library): an icon tile at the top left, the text column beside it and a
/// row of actions along the bottom. `textLinesH` is the height the text column needs.
struct ListCard { Rect card, icon, text, actions; };
inline ListCard LayoutListCard(float cardW, float pad, float icon, float textLinesH, float actionH) {
    ListCard c;
    const float top = std::max(icon, textLinesH);
    c.icon = Rect{pad, pad, icon, icon};
    c.text = Rect{pad + icon + pad * 0.85f, pad, std::max(0.0f, cardW - (pad + icon + pad * 0.85f) - pad), textLinesH};
    c.actions = Rect{pad, pad + top + pad * 0.85f, std::max(0.0f, cardW - 2 * pad), actionH};
    c.card = Rect{0, 0, cardW, c.actions.B() + pad};
    return c;
}

/// Buttons along a card's action row: the first one (the main action) takes the spare width, the rest keep theirs,
/// right-aligned. Returns rects relative to the row.
inline std::vector<Rect> LayoutActionRow(float rowW, float h, float mainMinW, const std::vector<float>& restW, float gap) {
    float rest = 0;
    for (float w : restW) rest += w + gap;
    std::vector<Rect> out;
    out.push_back(Rect{0, 0, std::max(mainMinW, rowW - rest), h});
    float x = out[0].R() + gap;
    for (float w : restW) { out.push_back(Rect{x, 0, w, h}); x += w + gap; }
    return out;
}

/// A flyout panel (Notifications) beside the sidebar: `navW` wide sidebar, the window's work area `areaW` x `areaH`.
/// It keeps a margin on every side, is `prefW` wide and `prefH` high when there is room, and never leaves the window.
inline Rect FlyoutRect(float areaW, float areaH, float navW, float prefW, float prefH, float minW, float minH, float margin) {
    Rect r;
    r.w = std::clamp(areaW - navW - 2 * margin, std::min(minW, areaW - navW - 2 * margin), prefW);
    r.h = std::clamp(areaH - 2 * margin, std::min(minH, areaH - 2 * margin), prefH);
    r.x = navW + margin;
    r.y = areaH - margin - r.h;
    return r;
}

/// A card's header: something on the left that can wrap (at least `leftMinW` wide) and a fixed group on the right
/// (`rightW`). Side by side when both fit, the left part taking the spare width; otherwise the right group goes on its own
/// row under the left part, and the left part gets the whole width. Rects from the card's inner top-left; `leftH` and
/// `rightH` are their heights (the left one after wrapping to the width it is given, so call twice: once for the width).
struct HeaderSplit { bool sameRow = true; float leftW = 0; Rect left, right; float height = 0; };
inline HeaderSplit SplitHeader(float availW, float leftMinW, float rightW, float gap) {
    HeaderSplit h;
    h.sameRow = leftMinW + gap + rightW <= availW;
    h.leftW = h.sameRow ? availW - gap - rightW : availW;
    return h;
}
inline void PlaceHeader(HeaderSplit& h, float availW, float leftH, float rightW, float rightH, float gap) {
    if (h.sameRow) {
        const float rowH = std::max(leftH, rightH);
        h.left = Rect{0, (rowH - leftH) * 0.5f, h.leftW, leftH};
        h.right = Rect{availW - rightW, (rowH - rightH) * 0.5f, rightW, rightH};
        h.height = rowH;
    } else {
        h.left = Rect{0, 0, h.leftW, leftH};
        h.right = Rect{0, leftH + gap, std::min(rightW, availW), rightH};
        h.height = h.right.B();
    }
}

// ---- Foldable side panels (the launcher sidebar, Servers' channel list, Forums' sections) ------------------------------------
/// A page's side panel: beside the content when there is room and it is not folded; on a narrow page (`availW` below
/// `narrowBelow`) it is never beside the content and opens over it instead (`overlay`, when the person asked for it).
/// `contentW` is what the content gets.
struct SidePanel { bool shown = true; bool overlay = false; float contentW = 0; };
inline SidePanel LayoutSidePanel(float availW, float panelW, float gap, float narrowBelow, bool folded, bool overlayOpen) {
    SidePanel p;
    const bool narrow = availW < narrowBelow;
    p.shown = !narrow && !folded;
    p.overlay = narrow && overlayOpen;
    p.contentW = std::max(0.0f, availW - (p.shown ? panelW + gap : 0.0f));
    return p;
}

// ---- A folded side panel as an icon rail (Servers' server list, Forums' sections when folded) -----------------------------
/// Where the icons of a rail go: one `d`-pixel icon per item, centred in a rail `railW` wide, from `top`, `gap` apart; an
/// entry marked as a divider takes `divH` instead (its rect is the divider line's band). One rect per entry, in order.
inline std::vector<Rect> LayoutIconRail(const std::vector<bool>& isDivider, float railW, float top, float d, float gap, float divH) {
    std::vector<Rect> out;
    float y = top;
    for (bool div : isDivider) {
        if (div) { out.push_back(Rect{railW * 0.25f, y, railW * 0.5f, divH}); y += divH + gap; }
        else { out.push_back(Rect{(railW - d) * 0.5f, y, d, d}); y += d + gap; }
    }
    return out;
}
/// What the Forums page's folded sections rail holds, top to bottom: All posts, each joined section (its letter badge), a
/// divider, Discover (the other sections), Create a section (when you can), Staff tools (staff only). Never empty: All posts
/// and Discover are always there.
enum class ForumRail { AllPosts, Section, Divider, Discover, Create, Staff };
inline std::vector<ForumRail> ForumRailEntries(int joinedSections, bool canCreate, bool staff) {
    std::vector<ForumRail> v{ForumRail::AllPosts};
    for (int i = 0; i < joinedSections; ++i) v.push_back(ForumRail::Section);
    v.push_back(ForumRail::Divider);
    v.push_back(ForumRail::Discover);
    if (canCreate) v.push_back(ForumRail::Create);
    if (staff) v.push_back(ForumRail::Staff);
    return v;
}

// ---- The Marketplace (a browse-and-shop page: a filter rail, a grid of item tiles, an item page) ----------------------------
/// Where the filter rail and the grid go. Below `railMinPageW` the rail folds into a "Filters" button (it then opens over the
/// grid). The grid has 3 to 6 columns of tiles at least `tileMinW` wide (fewer than 3 only when even 3 do not fit).
/// Folded (`folded`, on a page wide enough for the rail) the rail is an icon rail `iconRailW` wide (`iconRail`) and the grid
/// takes the rest.
struct MarketPage { bool railShown = true, iconRail = false; Rect rail, grid; Grid tiles; };
inline MarketPage LayoutMarketPage(float availW, float railW, float gap, float tileMinW, float tileGap, float railMinPageW,
                                   bool folded = false, float iconRailW = 0.0f) {
    MarketPage m;
    const bool wide = availW >= railMinPageW;
    m.railShown = wide && !folded;
    m.iconRail = wide && folded;
    const float rw = m.railShown ? railW : m.iconRail ? iconRailW : 0.0f;
    m.rail = rw > 0 ? Rect{0, 0, rw, 0} : Rect{};
    const float gx = rw > 0 ? rw + gap : 0.0f;
    m.grid = Rect{gx, 0, std::max(1.0f, availW - gx), 0};
    m.tiles = FitGrid(m.grid.w, tileMinW, tileGap, 6);
    return m;
}
/// One item tile: a 4:3 picture, the price in bold, the title on up to two lines, then "seller  ·  1.5k downloads"; the heart
/// sits in the picture's top-right corner.
struct MarketTile { Rect tile, picture, heart, price, title, meta; };
inline MarketTile LayoutMarketTile(float tileW, float lineH, float priceH, float pad, float heartD) {
    MarketTile t;
    t.picture = Rect{0, 0, tileW, std::floor(tileW * 0.75f)};
    t.heart = Rect{tileW - pad * 0.6f - heartD, pad * 0.6f, heartD, heartD};
    t.price = Rect{pad, t.picture.B() + pad * 0.7f, std::max(0.0f, tileW - 2 * pad), priceH};
    t.title = Rect{pad, t.price.B() + pad * 0.25f, std::max(0.0f, tileW - 2 * pad), 2 * lineH};
    t.meta = Rect{pad, t.title.B() + pad * 0.3f, std::max(0.0f, tileW - 2 * pad), lineH};
    t.tile = Rect{0, 0, tileW, t.meta.B() + pad};
    return t;
}
/// The item page: the picture and the details side by side on a wide page, the picture over the details on a narrow one.
struct MarketDetail { bool twoColumns = true; Rect picture, info; };
inline MarketDetail LayoutMarketDetail(float availW, float gap, float minInfoW, float narrowMaxPicH = 1e9f) {
    MarketDetail d;
    const float picW = std::floor(availW * 0.55f);
    d.twoColumns = availW - picW - gap >= minInfoW;
    if (d.twoColumns) { d.picture = Rect{0, 0, picW, std::floor(picW * 0.75f)}; d.info = Rect{picW + gap, 0, availW - picW - gap, 0}; }
    else { d.picture = Rect{0, 0, availW, std::floor(std::min(availW * 0.5f, narrowMaxPicH))}; d.info = Rect{0, d.picture.B() + gap, availW, 0}; }   // (a short banner: the details stay in view)
    return d;
}
/// What the Marketplace shows: which view (0 Browse, 1 Your listings, 2 Saved), a category (-1 all), a minimum average
/// rating, the search text (lower case), and the item page that is open (0 none; Back closes it, the list is kept).
struct MarketQuery {
    int view = 0, kind = -1;
    float minRating = 0.0f;
    std::string needle;
    long detail = 0;
    void Open(long id) { detail = id; }
    void Back() { detail = 0; }
};
/// Does one listing pass the query? `haystack` is its title, description and seller in lower case.
inline bool MarketMatches(const MarketQuery& q, int kind, float avgRating, bool wished, bool mine, const std::string& haystack) {
    if (q.view == 1 && !mine) return false;
    if (q.view == 2 && !wished) return false;
    if (q.kind >= 0 && kind != q.kind) return false;
    if (q.minRating > 0.0f && avgRating < q.minRating) return false;
    if (!q.needle.empty() && haystack.find(q.needle) == std::string::npos) return false;
    return true;
}
/// Who sees a listing: an approved one everyone; one in review or not approved only its seller (with the badge) and staff.
inline bool MarketListingVisible(const std::string& status, bool mine, bool staff) {
    return status.empty() || status == "approved" || mine || staff;
}
/// Today's picks, Featured and Popular (Marketplace and Community) show approved listings only, the seller's own included.
inline bool MarketFeaturable(const std::string& status) { return status.empty() || status == "approved"; }
/// Today's picks only when there are enough approved, rated listings to pick from (`minItems`) and the grid under it holds more
/// than one row (else it would only repeat the grid).
inline bool ShowTodaysPicks(int approvedRated, int browseCount, int cols, int minItems = 6) {
    return approvedRated >= minItems && browseCount > cols;
}
/// A tile's last line: the seller and "N downloads" side by side when both fit; else the seller alone on the whole line (the
/// downloads are on the item page). The seller is shortened only when even the whole line is too narrow.
struct TileMeta { bool showDownloads = true; float sellerW = 0; };
inline TileMeta FitTileMeta(float sellerTextW, float downloadsW, float lineW, float gap) {
    TileMeta t;
    t.showDownloads = sellerTextW + gap + downloadsW <= lineW;
    t.sellerW = t.showDownloads ? lineW - gap - downloadsW : lineW;
    return t;
}
/// The smallest tile width: bigger tiles when there are only a few listings, so a near-empty grid is not a row of stamps.
inline float MarketTileMinW(int items, float base) { return items <= 2 ? base * 1.45f : items <= 4 ? base * 1.2f : base; }
/// What the Marketplace's folded filter rail holds: Search, a divider, All, one icon per category, a divider, Rating, Sort.
enum class MarketRail { Search, Divider, All, Category, Rating, Sort };
inline std::vector<MarketRail> MarketRailEntries(int categories) {
    std::vector<MarketRail> v{MarketRail::Search, MarketRail::Divider, MarketRail::All};
    for (int i = 0; i < categories; ++i) v.push_back(MarketRail::Category);
    v.push_back(MarketRail::Divider);
    v.push_back(MarketRail::Rating);
    v.push_back(MarketRail::Sort);
    return v;
}

/// "980", "1.5k", "12k", "1.2M": short enough for a tile.
inline std::string ShortCount(long n) {
    char b[24];
    if (n >= 1000000) std::snprintf(b, sizeof(b), "%.1fM", n / 1000000.0);
    else if (n >= 10000) std::snprintf(b, sizeof(b), "%ldk", n / 1000);
    else if (n >= 1000) std::snprintf(b, sizeof(b), "%.1fk", n / 1000.0);
    else std::snprintf(b, sizeof(b), "%ld", n);
    return b;
}

// ---- The Community showcase (published games and levels, shown big) --------------------------------------------------------
/// The feed of big cards and, at the wide sizes, a rail on the right (top creators, friends playing now). The rail shows only
/// when the feed keeps at least `feedMinW` beside it (`railW` + `gap`); the feed is never wider than `maxFeedW` (it is
/// centred when the page is wider). Cards: `FitGrid(feed.w, cardMinW, cardGap, 3)`.
struct Showcase { bool railShown = false; Rect feed, rail; };
inline Showcase LayoutShowcase(float availW, float cardMinW, float cardGap, float railW, float gap, float maxFeedW) {
    Showcase s;
    const float feedMinW = 2 * cardMinW + cardGap;   // two cards a row at least beside the rail
    s.railShown = availW - railW - gap >= feedMinW;
    const float fw = std::max(1.0f, std::min(maxFeedW, availW - (s.railShown ? railW + gap : 0.0f)));
    s.feed = Rect{0, 0, fw, 0};
    if (s.railShown) s.rail = Rect{fw + gap, 0, railW, 0};
    return s;
}

/// The "Share your creation" box at the top of the showcase: the title and a line under it on the left, the library pick and
/// Share on the right; when they do not fit beside the title (narrow page, big UI scale) the pick and Share go under the text.
struct ShareBox { bool stacked = false; Rect title, sub, combo, share; float h = 0; };
inline ShareBox LayoutShareBox(float w, float pad, float titleW, float titleH, float lineH, float comboMaxW, float shareW, float btnH, float gap) {
    ShareBox b;
    const float tx = pad + gap * 0.6f;   // (the accent bar on the left edge)
    const float comboW = std::min(comboMaxW, std::max(0.0f, w * 0.35f));
    const float rightW = comboW + gap + shareW;
    b.stacked = tx + titleW + 2 * gap + rightW + pad > w;
    b.title = Rect{tx, pad, std::min(titleW, std::max(0.0f, w - tx - pad)), titleH};
    const float textR = b.stacked ? w - pad : w - pad - rightW - 2 * gap;
    b.sub = Rect{tx, b.title.B() + gap * 0.4f, std::max(0.0f, textR - tx), lineH};
    if (b.stacked) {
        const float y = b.sub.B() + gap;
        const float cw = std::max(0.0f, std::min(comboMaxW, w - tx - pad - gap - shareW));
        b.combo = Rect{tx, y, cw, btnH};
        b.share = Rect{b.combo.R() + gap, y, shareW, btnH};
        b.h = y + btnH + pad;
    } else {
        b.h = std::max(b.sub.B(), btnH + pad) + pad;
        const float y = (b.h - btnH) * 0.5f;
        b.share = Rect{w - pad - shareW, y, shareW, btnH};
        b.combo = Rect{b.share.x - gap - comboW, y, comboW, btnH};
    }
    return b;
}

/// The Notifications page's filter chips: 0 All, 1 Unread, 2 Friends, 3 Messages, 4 Updates, 5 System. `kind` is the
/// notification's kind (0 system, 1 friend, 2 message, 3 update).
inline bool NotifMatches(int filter, int kind, bool unread) {
    static const int kFilterKind[] = {-1, -1, 1, 2, 3, 0};
    if (filter == 1) return unread;
    if (filter >= 2 && filter <= 5) return kind == kFilterKind[filter];
    return true;
}
/// The empty state's title: signed out with nothing at all, "all caught up" (nothing, or nothing unread), or a filter
/// that matches nothing.
inline const char* NotifEmptyTitle(bool signedOut, int total, int filter) {
    if (signedOut && total == 0) return "You're signed out";
    if (total == 0 || filter == 1) return "You're all caught up";
    return "Nothing here";
}

/// Release notes (the What's New page): how many lines one release keeps (FetchSupabaseNews), how long one line may be,
/// and how many a long list shows before "Show all".
inline constexpr int kNotesMaxLines = 60;
inline constexpr std::size_t kNotesMaxLineBytes = 400;
inline constexpr int kNotesCollapsed = 15;
struct NotesView { int shown = 0; bool toggle = false; };
inline NotesView NotesShown(int lines, bool expanded) {
    NotesView v;
    v.toggle = lines > kNotesCollapsed + 2;   // two more lines are not worth a button
    v.shown = (!v.toggle || expanded) ? lines : kNotesCollapsed;
    return v;
}
/// The bullets of a notes list stacked from (0, 0) in a column `width` wide: `lineH[i]` is note i's wrapped height.
/// `viewH` is the scroll area's height; the result says whether it scrolls.
struct NotesLayout { std::vector<Rect> bullets; Rect toggle; float height = 0; bool scrolls = false; };
inline NotesLayout LayoutNotes(const std::vector<float>& lineH, float width, float gap, float toggleH, bool expanded, float viewH) {
    NotesLayout n;
    const NotesView v = NotesShown((int)lineH.size(), expanded);
    float y = 0;
    for (int i = 0; i < v.shown; ++i) { n.bullets.push_back(Rect{0, y, width, lineH[(std::size_t)i]}); y += lineH[(std::size_t)i] + gap; }
    if (v.toggle) { n.toggle = Rect{0, y, std::min(width, 160.0f), toggleH}; y += toggleH + gap; }
    n.height = y;
    n.scrolls = n.height > viewH;
    return n;
}

/// Pages with an OkayUI port (12 What's New, 13 Tips): which ONE surface draws page `tab` this frame. Only the UI backend decides
/// (`okayFrame`: this frame has an OkayUI frame open). Nothing Dear ImGui draws over the page (a tooltip, a popup, another window)
/// switches it to its Dear ImGui copy: the OkayUI page is hosted in Dear ImGui's draw order (okayuihost::HostPanelArea), so those
/// are drawn over it like over any ImGui page. Before, a covered page was drawn by its Dear ImGui copy instead, so hovering the
/// sidebar's profile row (its tooltip reaches into the page) swapped the whole What's New page to the other version and back
/// (owner's recording, 2026-11-26). Tested in tests/test_okayui_panels_imgui.cpp (LauncherPage*).
inline bool OkayUIDrawsPage(int tab, bool okayFrame) { return okayFrame && (tab == 12 || tab == 13); }

/// What the What's New page's "Test builds" section shows.
enum class TestBuildsView { Off, Loading, Failed, Empty, List };
/// `on`: "Get test builds" is switched on; `devState`: 0 not read yet, 1 read, 2 could not be read; `n`: test builds listed.
inline TestBuildsView TestBuildsViewOf(bool on, int devState, std::size_t n) {
    if (!on) return TestBuildsView::Off;
    if (devState == 0) return TestBuildsView::Loading;
    if (devState == 2) return TestBuildsView::Failed;
    return n == 0 ? TestBuildsView::Empty : TestBuildsView::List;
}
/// One test build's row: the version (and an "Installed" pill) on the left and the date on the right of the first line,
/// the note (wrapped, `noteH` high) under it, then the branch. Rects from the row's top-left.
struct BuildRow { Rect row, version, pill, date, note, branch; };
inline BuildRow LayoutBuildRow(float width, float pad, float lineH, float versionW, float pillW, float dateW, float noteH) {
    BuildRow b;
    b.version = Rect{pad, pad, std::min(versionW, std::max(0.0f, width - 2 * pad - dateW - pillW - 2 * pad)), lineH};
    b.pill = Rect{b.version.R() + (pillW > 0 ? pad * 0.6f : 0.0f), pad, pillW, lineH};
    b.date = Rect{std::max(b.pill.R() + pad * 0.6f, width - pad - dateW), pad, dateW, lineH};
    b.note = Rect{pad, b.version.B() + pad * 0.4f, std::max(0.0f, width - 2 * pad), noteH};
    b.branch = Rect{pad, b.note.B() + pad * 0.4f, std::max(0.0f, width - 2 * pad), lineH};
    b.row = Rect{0, 0, width, b.branch.B() + pad};
    return b;
}

/// The top of a direct-message conversation (DrawChatWindow in launcher/main.cpp): the header (avatar, name, presence,
/// icon buttons) and, when the friend is in a game, the "Playing ..." bar under it (gamepad icon, two text lines, the
/// Join / Ask to join button on the right). Everything grows with the text: `lineH` is the text line height and `nameH`
/// the name's (it is drawn a little larger), both at the current UI scale; `buttonW` is the widest button label plus its
/// padding. `listTop` is where what follows (search, pinned bar, the message list) may start. Rects from the header's
/// top-left. Owner bug 2026-11-25: at UI scale 1.5 the bar's fixed 40 px height left its second line over the first
/// messages.
struct ChatHeaderLayout { float headerH = 0; bool bar = false; Rect barRect, icon, title, detail, button; float listTop = 0; };
inline ChatHeaderLayout LayoutChatTop(float width, float lineH, float nameH, float buttonW, bool hasBar) {
    ChatHeaderLayout t;
    t.headerH = std::max(34.0f, std::ceil(std::max(28.0f, nameH) + 6.0f));   // 34 at scale 1 (the 28 px avatar + room)
    const float gap = 6.0f;
    const float barY = t.headerH + gap;
    t.bar = hasBar;
    if (!hasBar) { t.listTop = barY; return t; }
    const float pad = 4.0f;
    const float iconD = std::max(22.0f, std::round(lineH * 1.3f));
    const float textX = iconD + 20.0f;
    const float btnH = std::max(28.0f, std::ceil(lineH + 8.0f));
    const float h = std::max({40.0f, std::ceil(2.0f * pad + 2.0f * lineH), btnH + 12.0f});
    const float bw = std::min(buttonW, std::max(0.0f, width * 0.5f));
    t.barRect = Rect{0, barY, width, h};
    t.icon = Rect{22.0f - iconD * 0.5f, barY + (h - iconD) * 0.5f, iconD, iconD};
    t.button = Rect{width - bw - 6.0f, barY + (h - btnH) * 0.5f, bw, btnH};
    const float textW = std::max(0.0f, t.button.x - 10.0f - textX);
    const float textTop = barY + (h - 2.0f * lineH) * 0.5f;
    t.title = Rect{textX, textTop, textW, lineH};
    t.detail = Rect{textX, textTop + lineH, textW, lineH};
    t.listTop = t.barRect.B() + gap;
    return t;
}

}  // namespace okay::launcherlayout
