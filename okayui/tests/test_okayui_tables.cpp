// OkayUI tables, step 4: sorting (header clicks, the sort specs, multi-key, tristate, defaults), scrolling boxes (rows and columns, frozen
// rows and columns, wheel, bars, clipping, clicks in scrolled cells), and row backgrounds (alternation, full-height bands, TableSetBgColor).
// Headless: synthetic input through the test kit Sim, pixels through the software backend.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {

const unsigned kClear = Rgba(12, 14, 20);
UI::Vec2 V(float x, float y) { return UI::Vec2{x, y}; }
unsigned P(const Bitmap& b, float x, float y) { return PixelAt(b, (int)x, (int)y); }
int CountEq(const Bitmap& b, unsigned c, int x0 = 0, int y0 = 0, int x1 = 100000, int y1 = 100000) {
    int n = 0;
    for (int y = std::max(0, y0); y < std::min(b.h, y1); ++y) for (int x = std::max(0, x0); x < std::min(b.w, x1); ++x) if (PixelAt(b, x, y) == c) ++n;
    return n;
}

struct Rig {
    Sim sim;
    std::function<void()> body;
    explicit Rig(float w = 640, float h = 480) : sim(w, h) { Reset(); }
    void Frame() { sim.Frame([&] { if (UI::Begin("W", 10, 10, w() - 20, h() - 20) && body) body(); UI::End(); }); }
    float w() const { return sim.W; } float h() const { return sim.H; }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void Move(float x, float y) { sim.Move(x, y); Frame(); }
    void Click(float x, float y) { Frames(1); sim.Move(x, y); Frame(); sim.Down(); Frame(); sim.Up(); Frame(); }
    void CtrlClick(float x, float y) { sim.ctrl = true; Click(x, y); sim.ctrl = false; Frame(); }
    void Wheel(float x, float y, float dy, int times = 1) { Move(x, y); for (int i = 0; i < times; ++i) { sim.wheel = dy; Frame(); } Frames(2); }
};

// ---- sorting ------------------------------------------------------------------------------------------------------------

struct Row { int a; const char* b; float c; };

struct SortRig {
    Rig r;
    std::vector<Row> rows{{3, "pear", 2.5f}, {1, "plum", 9.0f}, {2, "apple", 1.5f}, {2, "fig", 7.0f}, {1, "lime", 0.5f}};
    UI::TableFlags flags = UI::Table_Sortable | UI::Table_SortMulti;
    int defaultSort = -1;           // column flagged DefaultSort
    bool noSortOnB = false, preferDescOnC = false;
    int sorts = 0;
    int specCount = -1; int col0 = -1, col1 = -1; bool desc0 = false, desc1 = false; unsigned user0 = 0; bool dirtySeen = false;
    bool nullSpecs = false;
    UI::Rect header;                 // the header row of the table
    float colX[3] = {0, 0, 0}, colW[3] = {0, 0, 0};
    std::vector<int> order;          // the first column's values in the order shown
    SortRig() {
        r.body = [&] {
            if (UI::BeginTable("t", 3, flags)) {
                UI::TableSetupColumn("A", defaultSort == 0 ? UI::Col_DefaultSort : 0, 1.0f, 100);
                UI::TableSetupColumn("B", (noSortOnB ? UI::Col_NoSort : 0) | (defaultSort == 1 ? UI::Col_DefaultSort : 0), 2.0f, 200);
                UI::TableSetupColumn("C", (preferDescOnC ? UI::Col_PreferSortDescending : 0) | (defaultSort == 2 ? UI::Col_DefaultSort : 0), 3.0f, 300);
                UI::TableHeadersRow();
                if (UI::TableSortSpecs* sp = UI::TableGetSortSpecs()) {
                    nullSpecs = false;
                    specCount = sp->specsCount;
                    if (sp->specsCount > 0) { col0 = sp->specs[0].columnIndex; desc0 = sp->specs[0].descending; user0 = sp->specs[0].columnUserId; }
                    if (sp->specsCount > 1) { col1 = sp->specs[1].columnIndex; desc1 = sp->specs[1].descending; }
                    if (sp->specsDirty) {
                        dirtySeen = true; ++sorts;
                        const auto less = [&](const Row& x, const Row& y) {
                            for (int k = 0; k < sp->specsCount; ++k) {
                                const int c = sp->specs[k].columnIndex; int d = 0;
                                if (c == 0) d = x.a < y.a ? -1 : (x.a > y.a ? 1 : 0);
                                else if (c == 1) d = std::strcmp(x.b, y.b);
                                else d = x.c < y.c ? -1 : (x.c > y.c ? 1 : 0);
                                if (d != 0) return sp->specs[k].descending ? d > 0 : d < 0;
                            }
                            return false;
                        };
                        std::stable_sort(rows.begin(), rows.end(), less);
                        sp->specsDirty = false;
                    }
                } else { nullSpecs = true; specCount = -1; }
                order.clear();
                for (const Row& row : rows) {
                    UI::TableNextRow();
                    UI::TableSetColumnIndex(0); char t[16]; std::snprintf(t, sizeof t, "%d", row.a); UI::Text(t); order.push_back(row.a);
                    UI::TableSetColumnIndex(1); UI::Text(row.b);
                    UI::TableSetColumnIndex(2); std::snprintf(t, sizeof t, "%.1f", (double)row.c); UI::Text(t);
                }
                UI::EndTable();
            }
        };
    }
    float HeaderY() { return r.sim.H * 0 + 10 + 28 + 10 + 14; }   // window top + title + padding + half a header row
    float ColCenter(int i) {   // the three columns are 100:200:300 fixed pixels scaled to the table width; measure from the table origin
        const float tw = r.w() - 20 - 20;   // window content width: window width - 2 * padding
        const float f = tw / 600.0f; float x = 20.0f;
        for (int k = 0; k < i; ++k) x += (k == 0 ? 100 : (k == 1 ? 200 : 300)) * f;
        return x + (i == 0 ? 100 : (i == 1 ? 200 : 300)) * f * 0.5f;
    }
    void ClickHeader(int c, bool ctrl = false) { if (ctrl) r.CtrlClick(ColCenter(c), HeaderY()); else r.Click(ColCenter(c), HeaderY()); }
    std::vector<int> Order() const { return order; }
};

void TestSorting() {
    RUN_SUITE("tables: sorting");
    {   // a sortable table starts unsorted: no keys, not dirty; clicks sort
        SortRig s; s.r.Frames(3);
        CHECK(!s.nullSpecs && s.specCount == 0 && !s.dirtySeen);
        CHECK((s.order == std::vector<int>{3, 1, 2, 2, 1}));
        s.ClickHeader(0); s.r.Frames(2);
        CHECK(s.specCount == 1 && s.col0 == 0 && !s.desc0 && s.dirtySeen && s.user0 == 100);
        CHECK((s.order == std::vector<int>{1, 1, 2, 2, 3}));
        CHECK(s.sorts == 1);
        s.r.Frames(3); CHECK(s.sorts == 1);                                           // dirty was cleared: no re-sort every frame
        s.ClickHeader(0); s.r.Frames(2);
        CHECK(s.desc0 && s.sorts == 2 && (s.order == std::vector<int>{3, 2, 2, 1, 1}));  // a second click flips it
        s.ClickHeader(0); s.r.Frames(2);
        CHECK(!s.desc0 && s.specCount == 1);                                          // without tristate it goes back to ascending
        s.ClickHeader(1); s.r.Frames(2);
        CHECK(s.col0 == 1 && s.specCount == 1 && s.user0 == 200);                      // another column replaces it
        CHECK(s.rows[0].b == std::string("apple") && s.rows[4].b == std::string("plum"));
        s.ClickHeader(2); s.r.Frames(2);
        CHECK(s.col0 == 2 && s.rows[0].c == 0.5f && s.rows[4].c == 9.0f);
    }
    {   // the sort arrow is drawn on the sorted column's header only
        SortRig s; s.r.Frames(3);
        const unsigned txt = ThemeRgba(UI::Style().text);
        const float hy = s.HeaderY();
        const Bitmap before = s.r.sim.Draw(12, 14, 20);
        s.ClickHeader(1); s.r.Move(600, 440); s.r.Frames(2);
        const Bitmap after = s.r.sim.Draw(12, 14, 20);
        const float cx = s.ColCenter(1) + 100.0f;                                    // near the right end of column B
        int diffB = 0, diffA = 0;
        for (int y = (int)hy - 8; y < (int)hy + 8; ++y) {
            for (int x = (int)s.ColCenter(1); x < (int)(s.ColCenter(1) + 140); ++x) if (PixelAt(before, x, y) != PixelAt(after, x, y)) ++diffB;
            for (int x = (int)s.ColCenter(0) - 40; x < (int)(s.ColCenter(0) + 40); ++x) if (PixelAt(before, x, y) != PixelAt(after, x, y)) ++diffA;
        }
        CHECK(diffB > 8 && diffA == 0);
        (void)cx; (void)txt;
        // ascending and descending arrows differ
        s.ClickHeader(1); s.r.Move(600, 440); s.r.Frames(2);
        const Bitmap desc = s.r.sim.Draw(12, 14, 20);
        int diffD = 0;
        for (int y = (int)hy - 8; y < (int)hy + 8; ++y) for (int x = (int)s.ColCenter(1); x < (int)(s.ColCenter(1) + 140); ++x) if (PixelAt(after, x, y) != PixelAt(desc, x, y)) ++diffD;
        CHECK(diffD > 4);
    }
    {   // tristate: ascending, descending, then no key (the data keeps its last order)
        SortRig s; s.flags = UI::Table_Sortable | UI::Table_SortTristate; s.r.Frames(3);
        s.ClickHeader(0); s.r.Frames(2); CHECK(s.specCount == 1 && !s.desc0);
        s.ClickHeader(0); s.r.Frames(2); CHECK(s.specCount == 1 && s.desc0);
        s.ClickHeader(0); s.r.Frames(2); CHECK(s.specCount == 0);
        s.ClickHeader(0); s.r.Frames(2); CHECK(s.specCount == 1 && !s.desc0);
    }
    {   // multi: Ctrl+click adds a key, flips it, a plain click replaces them all; without SortMulti Ctrl does nothing special
        SortRig s; s.r.Frames(3);
        s.ClickHeader(0); s.r.Frames(2);
        s.ClickHeader(1, true); s.r.Frames(2);
        CHECK(s.specCount == 2 && s.col0 == 0 && s.col1 == 1 && !s.desc1);
        CHECK((s.order == std::vector<int>{1, 1, 2, 2, 3}) && s.rows[0].b == std::string("lime") && s.rows[2].b == std::string("apple") && s.rows[3].b == std::string("fig"));
        s.ClickHeader(1, true); s.r.Frames(2);
        CHECK(s.specCount == 2 && s.desc1 && s.rows[0].b == std::string("plum") && s.rows[2].b == std::string("fig"));
        s.ClickHeader(2); s.r.Frames(2);
        CHECK(s.specCount == 1 && s.col0 == 2);
        SortRig m; m.flags = UI::Table_Sortable; m.r.Frames(3);
        m.ClickHeader(0); m.r.Frames(2); m.ClickHeader(1, true); m.r.Frames(2);
        CHECK(m.specCount == 1 && m.col0 == 1);
    }
    {   // defaults: DefaultSort starts as a key and the first frame is dirty; PreferSortDescending; NoSort
        SortRig s; s.defaultSort = 2; s.preferDescOnC = true; s.r.Frames(3);
        CHECK(s.specCount == 1 && s.col0 == 2 && s.desc0 && s.sorts == 1 && s.rows[0].c == 9.0f);
        SortRig n; n.noSortOnB = true; n.r.Frames(3);
        n.ClickHeader(1); n.r.Frames(2);
        CHECK(n.specCount == 0 && n.sorts == 0);                                      // a NoSort column does nothing
        SortRig p; p.preferDescOnC = true; p.r.Frames(3);
        p.ClickHeader(2); p.r.Frames(2);
        CHECK(p.specCount == 1 && p.desc0);                                           // the first click goes the preferred way
        p.ClickHeader(2); p.r.Frames(2);
        CHECK(!p.desc0);
    }
    {   // not sortable: no specs, clicks do nothing; a click on the border handle does not sort; dragging the border resizes
        SortRig s; s.flags = UI::Table_RowBg; s.r.Frames(3);
        CHECK(s.nullSpecs);
        s.ClickHeader(0); s.r.Frames(2);
        CHECK(s.nullSpecs && s.sorts == 0 && (s.order == std::vector<int>{3, 1, 2, 2, 1}));
        SortRig t; t.r.Frames(3);
        const float border = (t.ColCenter(0) + t.ColCenter(1)) * 0.5f - 50.0f;           // roughly the A/B border: x = 20 + width of A
        const float tw = t.r.w() - 40, f = tw / 600.0f;
        t.r.Click(20 + 100 * f, t.HeaderY()); t.r.Frames(2);
        CHECK(t.specCount == 0);                                                      // the border belongs to the resize handle
        (void)border;
    }
    {   // the specs belong to the table: outside a table there is nothing; two tables keep their own keys
        Rig r; UI::TableSortSpecs* outside = reinterpret_cast<UI::TableSortSpecs*>(1);
        int c1 = -1, c2 = -1; UI::Rect rh1, rh2;
        r.body = [&] {
            outside = UI::TableGetSortSpecs();
            for (int t = 0; t < 2; ++t) {
                char id[4] = {'t', (char)('0' + t), 0, 0};
                if (UI::BeginTable(id, 2, UI::Table_Sortable)) {
                    UI::TableSetupColumn("x"); UI::TableSetupColumn("y"); UI::TableHeadersRow();
                    if (UI::TableSortSpecs* sp = UI::TableGetSortSpecs()) { (t == 0 ? c1 : c2) = sp->specsCount > 0 ? sp->specs[0].columnIndex : -1; sp->specsDirty = false; }
                    UI::TableNextRow(); UI::TableNextColumn(); UI::Text("r");
                    UI::EndTable();
                    (t == 0 ? rh1 : rh2) = UI::GetItemRect();
                }
            }
        };
        r.Frames(3);
        CHECK(outside == nullptr);
        r.Click(60, 10 + 28 + 10 + 14);                                              // table 0, column x
        r.Frames(2);
        CHECK(c1 == 0 && c2 == -1);
    }
}

// ---- scrolling ----------------------------------------------------------------------------------------------------------

struct ScrollRig {
    Rig r;
    UI::TableFlags flags = UI::Table_ScrollY | UI::Table_RowBg;
    int rows = 40, freezeRows = 1, freezeCols = 0;
    UI::Vec2 outer{360, 200};
    float innerWidth = 0;
    bool fixedCols = false;
    UI::Rect box;                      // the item rect after EndTable
    UI::Rect firstCell, lastCell, btn;
    float sx = 0, sy = 0;
    int clicks = 0;
    UI::Rect below;
    ScrollRig() {
        r.body = [&] {
            if (UI::BeginTable("sc", 4, flags, outer, innerWidth)) {
                const int fx = fixedCols ? UI::Col_WidthFixed : 0;
                UI::TableSetupColumn("ID", fx, fixedCols ? 80 : 0); UI::TableSetupColumn("Name", fx, fixedCols ? 160 : 0);
                UI::TableSetupColumn("Value", fx, fixedCols ? 160 : 0); UI::TableSetupColumn("Act", fx, fixedCols ? 160 : 0);
                UI::TableSetupScrollFreeze(freezeCols, freezeRows);
                UI::TableHeadersRow();
                for (int i = 0; i < rows; ++i) {
                    UI::TableNextRow();
                    UI::TableNextColumn(); char t[16]; std::snprintf(t, sizeof t, "r%d", i); UI::Text(t);
                    if (i == 0) firstCell = UI::GetItemRect();
                    if (i == rows - 1) lastCell = UI::GetItemRect();
                    UI::TableNextColumn(); UI::Text("name");
                    UI::TableNextColumn(); UI::Text("value");
                    UI::TableNextColumn(); if (i == 2) { if (UI::Button("go")) ++clicks; btn = UI::GetItemRect(); } else UI::Text("-");
                }
                sx = UI::TableGetScrollX(); sy = UI::TableGetScrollY();
                UI::EndTable();
                box = UI::GetItemRect();
            }
            UI::Text("after"); below = UI::GetItemRect();
        };
    }
};

void TestScrollY() {
    RUN_SUITE("tables: vertical scroll, frozen header");
    ScrollRig s; s.r.Frames(3);
    CHECK_NEAR(s.box.w, 360, 0.5); CHECK_NEAR(s.box.h, 200, 0.5);
    CHECK(s.below.y >= s.box.y + s.box.h);                                           // the box is one item; the next widget is under it
    const Bitmap b0 = s.r.sim.Draw(12, 14, 20);
    CHECK(s.lastCell.y > s.box.y + s.box.h);                                        // the last row is below the box
    // nothing of the table draws past the box (a strip under it holds only the "after" text, which is further down)
    const unsigned txt = ThemeRgba(UI::Style().text);
    CHECK(CountEq(b0, txt, (int)s.box.x, (int)(s.box.y + s.box.h + 1), (int)(s.box.x + s.box.w), (int)(s.box.y + s.box.h + 5)) == 0);
    // the scroll bar is drawn along the right edge
    const unsigned acc = ThemeRgba(UI::Style().accent);
    CHECK(CountEq(b0, acc, (int)(s.box.x + s.box.w - 10), (int)s.box.y, (int)(s.box.x + s.box.w), (int)(s.box.y + s.box.h)) > 20);
    const float firstY = s.firstCell.y;
    // wheel over the box scrolls it
    s.r.Wheel(s.box.x + 100, s.box.y + 100, -2.0f, 5);
    CHECK(s.sy > 20 && std::fabs(s.firstCell.y - (firstY - s.sy)) < 1.5f);
    const Bitmap b1 = s.r.sim.Draw(12, 14, 20);
    // the header row (frozen) is where it was; the body moved
    int headerDiff = 0, bodyDiff = 0;
    for (int y = (int)s.box.y; y < (int)s.box.y + 26; ++y) for (int x = (int)s.box.x; x < (int)(s.box.x + 340); ++x) if (PixelAt(b0, x, y) != PixelAt(b1, x, y)) ++headerDiff;
    for (int y = (int)s.box.y + 40; y < (int)(s.box.y + s.box.h) - 4; ++y) for (int x = (int)s.box.x; x < (int)(s.box.x + 340); ++x) if (PixelAt(b0, x, y) != PixelAt(b1, x, y)) ++bodyDiff;
    CHECK(headerDiff == 0 && bodyDiff > 200);
    // and scrolled rows never show above or below the box
    CHECK(CountEq(b1, txt, (int)s.box.x, (int)(s.box.y + s.box.h + 1), (int)(s.box.x + s.box.w), (int)(s.box.y + s.box.h + 5)) == 0);
    // the wheel stops at the end of the content: the last row ends at the bottom of the box
    s.r.Wheel(s.box.x + 100, s.box.y + 100, -5.0f, 60);
    CHECK(s.lastCell.y + s.lastCell.h <= s.box.y + s.box.h + 1.0f && s.lastCell.y + s.lastCell.h > s.box.y + s.box.h - 40.0f);
    // the wheel outside the box does nothing
    const float at = s.sy;
    s.r.Wheel(s.box.x + 100, s.box.y + s.box.h + 60, 5.0f, 3);
    CHECK(s.sy == at);
    // back to the top by the wheel; the thumb drags
    s.r.Wheel(s.box.x + 100, s.box.y + 100, 5.0f, 80);
    CHECK(s.sy == 0);
    s.r.Move(s.box.x + s.box.w - 5, s.box.y + 40); s.r.sim.Down(); s.r.Frame(); s.r.Move(s.box.x + s.box.w - 5, s.box.y + 110); s.r.sim.Up(); s.r.Frames(3);
    CHECK(s.sy > 30);
    // a button in a scrolled row: clickable where it is, not where it is hidden under the header or outside the box
    s.r.Wheel(s.box.x + 100, s.box.y + 100, 5.0f, 80);                                // top
    CHECK(s.btn.y > s.box.y && s.btn.y + s.btn.h < s.box.y + s.box.h + 1);
    s.r.Click(CenterX(s.btn), CenterY(s.btn)); CHECK(s.clicks == 1);
    // scrolled so the button sits half under the frozen header: a click on its covered part does nothing, on its visible part it clicks
    s.r.Wheel(s.box.x + 100, s.box.y + 100, -1.0f, 3);
    CHECK(s.btn.y < s.box.y + 28 && s.btn.y + s.btn.h > s.box.y + 28 + 8);
    s.r.Click(CenterX(s.btn), s.box.y + 14);
    CHECK(s.clicks == 1);
    s.r.Click(CenterX(s.btn), s.box.y + 28 + 4);
    CHECK(s.clicks == 2);
}

void TestFrozenAndX() {
    RUN_SUITE("tables: sideways scroll, frozen columns");
    ScrollRig s; s.flags = UI::Table_ScrollX | UI::Table_ScrollY; s.fixedCols = true; s.freezeCols = 1; s.outer = V(300, 180); s.rows = 20; s.r.Frames(3);
    CHECK_NEAR(s.box.w, 300, 0.5);
    const float fx = s.firstCell.x;
    const Bitmap b0 = s.r.sim.Draw(12, 14, 20);
    const unsigned acc = ThemeRgba(UI::Style().accent);
    CHECK(CountEq(b0, acc, (int)s.box.x, (int)(s.box.y + s.box.h - 10), (int)(s.box.x + s.box.w), (int)(s.box.y + s.box.h)) > 20);   // the horizontal bar
    // shift + wheel scrolls sideways; the frozen first column stays
    s.r.sim.shift = true; s.r.Wheel(s.box.x + 150, s.box.y + 80, -2.0f, 4); s.r.sim.shift = false; s.r.Frames(2);
    CHECK(s.sx > 20);
    CHECK(std::fabs(s.firstCell.x - fx) < 0.5f);                                    // the frozen column did not move
    const Bitmap b1 = s.r.sim.Draw(12, 14, 20);
    int frozenDiff = 0, scrolledDiff = 0;
    for (int y = (int)s.box.y; y < (int)(s.box.y + s.box.h) - 12; ++y) {
        for (int x = (int)s.box.x; x < (int)s.box.x + 78; ++x) if (PixelAt(b0, x, y) != PixelAt(b1, x, y)) ++frozenDiff;
        for (int x = (int)s.box.x + 90; x < (int)(s.box.x + s.box.w) - 12; ++x) if (PixelAt(b0, x, y) != PixelAt(b1, x, y)) ++scrolledDiff;
    }
    CHECK(frozenDiff == 0 && scrolledDiff > 100);
    // scrolled columns do not draw over the frozen one or past the box
    const unsigned txt = ThemeRgba(UI::Style().text);
    CHECK(CountEq(b1, txt, (int)(s.box.x + s.box.w + 1), (int)s.box.y, (int)(s.box.x + s.box.w + 40), (int)(s.box.y + s.box.h)) == 0);
    // programmatic scrolling and its limits
    ScrollRig t; t.flags = UI::Table_ScrollX; t.fixedCols = true; t.freezeRows = 0; t.outer = V(300, 160); t.rows = 6; t.r.Frames(3);
    t.r.Wheel(t.box.x + 100, t.box.y + 60, -3.0f, 200);                              // wheel alone scrolls a sideways-only box
    CHECK(t.sx > 100);
    const float maxX = t.sx;
    t.r.Wheel(t.box.x + 100, t.box.y + 60, -3.0f, 20);
    CHECK(t.sx == maxX);                                                              // the end: 80+160*3 = 560 columns in 288 px view
    CHECK(std::fabs(maxX - (560.0f - 300.0f)) < 14.0f);
    t.r.Wheel(t.box.x + 100, t.box.y + 60, 3.0f, 200);
    CHECK(t.sx == 0);
    // a click lands on the right cell after a sideways scroll
    ScrollRig u; u.flags = UI::Table_ScrollX; u.fixedCols = true; u.freezeRows = 0; u.outer = V(300, 160); u.rows = 8; u.r.Frames(3);
    u.r.Wheel(u.box.x + 100, u.box.y + 60, -3.0f, 200);
    u.r.Frames(2);
    if (u.btn.x + u.btn.w < u.box.x + u.box.w) { u.r.Click(CenterX(u.btn), CenterY(u.btn)); CHECK(u.clicks == 1); }
    else CHECK(false);
}

void TestPicture() {
    RUN_SUITE("tables: a picture");
    // a sortable, scrolling box with a frozen header and first column, scrolled down and sideways, drawn in both looks (golden)
    for (int look = 0; look < 2; ++look) {
        Rig r(420, 300);                       // (its constructor resets OkayUI: switch the look after it)
        if (look) { UI::UseLauncherLook(true); UI::Style().caretBlink = false; }
        r.body = [&] {
            if (UI::BeginTable("pic", 4, UI::Table_ScrollX | UI::Table_ScrollY | UI::Table_RowBg | UI::Table_Sortable | UI::Table_BordersInnerH, V(360, 200))) {
                UI::TableSetupColumn("Name", UI::Col_WidthFixed | UI::Col_DefaultSort, 90); UI::TableSetupColumn("Kind", UI::Col_WidthFixed, 120);
                UI::TableSetupColumn("Size", UI::Col_WidthFixed, 120); UI::TableSetupColumn("Date", UI::Col_WidthFixed, 140);
                UI::TableSetupScrollFreeze(1, 1);
                UI::TableHeadersRow();
                if (UI::TableSortSpecs* sp = UI::TableGetSortSpecs()) sp->specsDirty = false;
                for (int i = 0; i < 30; ++i) {
                    UI::TableNextRow();
                    char t[24];
                    UI::TableNextColumn(); std::snprintf(t, sizeof t, "item %02d", i); UI::Text(t);
                    UI::TableNextColumn(); UI::Text(i % 2 ? "folder" : "document");
                    UI::TableNextColumn(); std::snprintf(t, sizeof t, "%d KB", 100 + i * 37); UI::Text(t);
                    UI::TableNextColumn(); UI::Text("2026-01-01");
                }
                UI::EndTable();
            }
        };
        r.Frames(3);
        r.Wheel(100, 150, -2.0f, 3);
        r.sim.shift = true; r.Wheel(100, 150, -2.0f, 2); r.sim.shift = false;
        r.Move(410, 290); r.Frames(2);
        std::string why;
        const std::string name = look ? "table_scroll_launcher" : "table_scroll";
        const bool ok = MatchesGolden(name, r.sim.Draw(12, 14, 20), &why);
        if (!ok) std::cerr << "  golden " << name << ": " << why << "\n";
        CHECK(ok);
        if (look) UI::UseLauncherLook(false);
    }
}

void TestSpans() {
    RUN_SUITE("tables: many rows do not flood the draw list");
    ScrollRig s; s.flags = UI::Table_ScrollX | UI::Table_ScrollY; s.fixedCols = true; s.freezeCols = 1; s.freezeRows = 1; s.rows = 300; s.outer = V(320, 200);
    s.r.Frames(3);
    UI::DrawData d = UI::GetDrawData();
    CHECK(d.cmdCount < 120 && d.dropped == 0 && d.indexCount % 3 == 0);
    s.r.Wheel(s.box.x + 150, s.box.y + 100, -3.0f, 100);
    d = UI::GetDrawData();
    CHECK(d.cmdCount < 120 && d.dropped == 0);
    int covered = 0; for (int i = 0; i < d.cmdCount; ++i) { CHECK(d.cmds[i].idxOffset == covered); covered += d.cmds[i].idxCount; }
    CHECK(covered == d.indexCount);
}

// ---- row backgrounds ----------------------------------------------------------------------------------------------------

void TestRowBg() {
    RUN_SUITE("tables: row backgrounds");
    Rig r; UI::Rect rr[4]; UI::Rect tall, cellB;
    r.body = [&] {
        if (UI::BeginTable("bg", 2, UI::Table_RowBg)) {
            UI::TableSetupColumn("a"); UI::TableSetupColumn("b"); UI::TableHeadersRow();
            for (int i = 0; i < 4; ++i) {
                UI::TableNextRow();
                if (i == 3) UI::TableSetBgColor(UI::TableBg_RowBg0, UI::RGBA(10, 200, 10));
                UI::TableNextColumn(); UI::Text("cell"); rr[i] = UI::GetItemRect();
                UI::TableNextColumn();
                if (i == 1) { UI::Dummy(10, 80); tall = UI::GetItemRect(); } else UI::Text("x");
                if (i == 2) { UI::TableSetBgColor(UI::TableBg_CellBg, UI::RGBA(200, 10, 200), 1); cellB = UI::GetItemRect(); }
            }
            UI::EndTable();
        }
    };
    UI::SetStyleColor(UI::Col_TableRowBg, 201, 11, 12); UI::SetStyleColor(UI::Col_TableRowBgAlt, 12, 11, 201);
    r.Frames(4);
    const Bitmap b = r.sim.Draw(12, 14, 20);
    const float xr = rr[0].x + 120;                                                   // inside column a, right of the text
    CHECK(P(b, xr, CenterY(rr[0])) == Rgba(201, 11, 12));                             // even rows: Col_TableRowBg
    CHECK(P(b, xr, CenterY(rr[1])) == Rgba(12, 11, 201));                             // odd rows: Col_TableRowBgAlt
    CHECK(P(b, xr, CenterY(rr[3])) == Rgba(10, 200, 10));                             // RowBg0 replaces it
    // the band covers the whole row: row 1 is 80 px tall, so a point near its bottom is still band-coloured
    CHECK(P(b, xr, rr[1].y + 70) == Rgba(12, 11, 201));
    // and the row after it starts below
    CHECK(P(b, xr, rr[2].y + 4) == Rgba(201, 11, 12));
    // a cell colour paints just that cell
    CHECK(P(b, cellB.x + 60, CenterY(cellB)) == Rgba(200, 10, 200));                  // the coloured cell
    CHECK(P(b, rr[2].x + 60, CenterY(rr[2])) == Rgba(201, 11, 12));                   // its neighbour in the row is untouched
    // outside a table, or before the first row, it does nothing
    UI::TableSetBgColor(UI::TableBg_RowBg0, UI::RGBA(1, 2, 3));
    UI::TableSetBgColor((UI::TableBgTarget)99, UI::RGBA(1, 2, 3), 99);
}

// ---- hostile input and a fuzz --------------------------------------------------------------------------------------------

struct Rng {
    unsigned long long s;
    explicit Rng(unsigned long long seed) : s(seed * 0x9e3779b97f4a7c15ull + 1) {}
    unsigned Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (unsigned)(s >> 16); }
    int Int(int n) { return (int)(Next() % (unsigned)n); }
    float F(float a, float b) { return a + (b - a) * (float)(Next() % 10001) / 10000.0f; }
    float Nasty() {
        switch (Int(7)) {
            case 0: return std::numeric_limits<float>::quiet_NaN();
            case 1: return std::numeric_limits<float>::infinity();
            case 2: return -1e30f;
            case 3: return 1e30f;
            default: return F(-50.0f, 500.0f);
        }
    }
};

void TestFuzz() {
    RUN_SUITE("tables: fuzz");
    for (int seed = 1; seed <= 60; ++seed) {
        Reset();
        if (seed & 1) { UI::UseLauncherLook(true); UI::Style().caretBlink = false; }
        Rng r((unsigned long long)seed);
        Sim sim(640, 480);
        std::vector<int> keep;
        auto ui = [&] {
            if (UI::Begin("W", 10, 10, 600, 440)) {
                const int cols = 1 + r.Int(6), rows = r.Int(60);
                const UI::TableFlags fl = (UI::TableFlags)r.Int(256);
                if (UI::BeginTable("t", r.Int(8) == 0 ? cols + 20 : cols, fl, UI::Vec2{r.Nasty(), r.Nasty()}, r.Nasty())) {
                    for (int c = 0; c < cols; ++c) UI::TableSetupColumn(c % 2 ? "x" : nullptr, r.Int(32), r.Nasty(), (unsigned)r.Int(100));
                    if (r.Int(2)) UI::TableSetupScrollFreeze(r.Int(8) - 2, r.Int(8) - 2);
                    if (r.Int(4)) UI::TableHeadersRow();
                    if (UI::TableSortSpecs* sp = UI::TableGetSortSpecs()) {
                        CHECK(sp->specsCount >= 0 && sp->specsCount <= 4);
                        for (int k = 0; k < sp->specsCount; ++k) CHECK(sp->specs[k].columnIndex >= 0 && sp->specs[k].columnIndex < 16 && sp->specs[k].sortOrder == k);
                        if (r.Int(3)) sp->specsDirty = false;
                    }
                    for (int i = 0; i < rows; ++i) {
                        UI::TableNextRow();
                        if (r.Int(9) == 0) UI::TableSetBgColor((UI::TableBgTarget)r.Int(5), (UI::Color32)r.Next(), r.Int(10) - 2);
                        const int n = r.Int(9);
                        for (int c = 0; c < n; ++c) {
                            if (r.Int(2)) UI::TableNextColumn(); else UI::TableSetColumnIndex(r.Int(10) - 2);
                            if (r.Int(3) == 0) UI::Button("b"); else UI::Text("cell");
                            if (r.Int(30) == 0) UI::Selectable("s", false, r.Int(16), UI::Vec2{});
                        }
                    }
                    UI::TableGetScrollX(); UI::TableGetScrollY();
                    if (r.Int(10) == 0) UI::TableSetScroll(r.Nasty(), r.Nasty());
                    if (r.Int(12) != 0) UI::EndTable();            // sometimes the table is left open
                }
                UI::Text("after");
            }
            UI::End();
        };
        for (int f = 0; f < 80; ++f) {
            const int what = r.Int(8);
            if (what < 3) sim.Move(r.F(-10, 650), r.F(-10, 490));
            else if (what == 3) { if (!sim.down) sim.Down(); }
            else if (what == 4) { if (sim.down) sim.Up(); }
            else if (what == 5) sim.wheel = r.F(-4, 4);
            else if (what == 6) sim.ctrl = !sim.ctrl;
            else sim.shift = !sim.shift;
            sim.Frame(ui);
            const UI::DrawData d = UI::GetDrawData();
            CHECK(d.indexCount % 3 == 0 && d.dropped == 0);
            const UI::DrawVertex* vx = (const UI::DrawVertex*)d.vertices; bool ok = true;
            for (int i = 0; i < d.vertexCount; ++i) if (!std::isfinite(vx[i].x) || !std::isfinite(vx[i].y)) ok = false;
            if (!ok) std::fprintf(stderr, "nonfinite vertex: seed %d frame %d\n", seed, f);
            CHECK(ok);
            int covered = 0; for (int i = 0; i < d.cmdCount; ++i) { if (d.cmds[i].idxOffset != covered) ok = false; covered += d.cmds[i].idxCount; }
            if (!ok || covered != d.indexCount) std::fprintf(stderr, "tiling: seed %d frame %d covered %d of %d, cmds %d\n", seed, f, covered, d.indexCount, d.cmdCount);
            CHECK(ok && covered == d.indexCount);
            if (f % 40 == 0) sim.Draw(12, 14, 20);
        }
        if (seed & 1) UI::UseLauncherLook(false);
    }
}

} // namespace

int main() {
    TestSorting();
    TestScrollY();
    TestFrozenAndX();
    TestPicture();
    TestSpans();
    TestRowBg();
    TestFuzz();
    TEST_MAIN_RESULT();
}
