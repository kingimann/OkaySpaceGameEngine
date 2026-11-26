// Browser (page engine): round 3 of the rendering side (engine/include/okay/Browser/Layout.hpp, Css.hpp). Every number below is
// worked out by hand from the specifications: CSS 2.1 chapter 17 (tables: automatic and fixed layout, colspan / rowspan,
// captions, row groups, vertical-align in cells, border-spacing and border-collapse), 8.3.1 (margins collapsing through
// parents and through empty boxes), 12.4 (counters) and 12.6.2 (list styles); CSS Images 3 (gradients, object-fit); Selectors 4
// (:has()); CSS Overflow 3 (text-overflow); CSS Text Decoration 3; CSS Transforms 1; the HTML standard's presentational hints
// for tables, <font>, align, body margins; sub / sup; opacity on controls; mask images; quirks-mode tables. The fixed text measure: every character is half the font size wide (8 px at 16 px), a normal line is
// 1.2 x 16 = 19.2 px and the text's em box starts 1.6 px below the line top.
// Only the layout API that existed before round 3 is used here, so this file also builds against the old library (and fails).
#include "BrowserLayoutTestKit.hpp"

#include <cmath>
#include <deque>
#include <string>

using namespace okay::browser;

using namespace browsertest;

static int CountFill(const LayoutResult& r, Color c) {
    int n = 0;
    for (const DrawCmd& k : r.cmds) if (k.kind == DrawCmd::Fill && k.color == c) ++n;
    return n;
}
static int CountKind(const LayoutResult& r, DrawCmd::Kind kind) {
    int n = 0;
    for (const DrawCmd& k : r.cmds) if (k.kind == kind) ++n;
    return n;
}
// The colour of the Fill drawn at (x, y) last (the one on top), or transparent.
static Color FillAt(const LayoutResult& r, float x, float y) {
    Color out{0, 0, 0, 0};
    for (const DrawCmd& k : r.cmds) if (k.kind == DrawCmd::Fill && x >= k.x && x < k.x + k.w && y >= k.y && y < k.y + k.h) out = k.color;
    return out;
}
static const std::string B0 = "<body style='margin:0'>";

int main() {
    RUN_SUITE("page layout round 3: tables, margins, gradients, counters, :has, text, images, transforms");
    const float L = 19.2f;

    // ---- margins collapsing through parents and empty boxes (CSS 2.1 8.3.1) ----
    {
        // the paragraph's 20 px top margin and its parent's 10 px collapse into one 20 px margin above both; the paragraph's bottom
        // margin goes through the parent's bottom and meets the next box's 5 px top margin: 20
        const LayoutResult r = Lay(B0 + "<div id=a style='margin-top:10px'><p id=p style='margin:20px 0'>x</p></div>"
                                   "<div id=b style='margin-top:5px'>y</div>", 300);
        CHECK(BoxIs(r, "a", 0, 20, 300, L));
        CHECK(BoxIs(r, "p", 0, 20, 300, L));
        CHECK(BoxIs(r, "b", 0, 20 + L + 20, 300, L));
    }
    {
        // a top border keeps the top margins apart; the bottom margin still goes out through the bottom (no border there)
        const LayoutResult r = Lay(B0 + "<div id=a style='border-top:1px solid'><p id=p style='margin:20px 0'>x</p></div>", 300);
        CHECK(BoxIs(r, "a", 0, 0, 300, 1 + 20 + L));
        CHECK(BoxIs(r, "p", 0, 21, 300, L));
    }
    {
        // padding too, and a new formatting context (overflow: hidden)
        const LayoutResult r = Lay(B0 + "<div id=a style='padding-top:1px'><p id=p style='margin:20px 0'>x</p></div>"
                                   "<div id=c style='overflow:hidden'><p id=q style='margin:20px 0'>x</p></div>", 300);
        CHECK(BoxIs(r, "p", 0, 21, 300, L));
        const BoxInfo* c = ById(r, "c");
        CHECK(c && Near(c->h, 20 + L + 20));
    }
    {
        // an empty box: its own top and bottom margins collapse with the ones around it (10, 30, 30, 15: 30)
        const LayoutResult r = Lay(B0 + "<p id=p1 style='margin:0 0 10px'>x</p><div style='margin:30px 0'></div><p id=p2 style='margin:15px 0 0'>y</p>", 300);
        CHECK(BoxIs(r, "p2", 0, L + 30, 300, L));
    }
    {
        // a negative margin: 20 + (-5)
        const LayoutResult r = Lay(B0 + "<p style='margin:0 0 20px'>x</p><p id=p2 style='margin:-5px 0 0'>y</p>", 300);
        CHECK(BoxIs(r, "p2", 0, L + 15, 300, L));
    }

    // ---- tables (CSS 2.1 chapter 17) ----
    {
        // colspan: the spanning cell's 80 px are spread over its two columns in proportion to their maximum widths (8 and 8)
        const LayoutResult r = Lay(B0 + "<table style='border-spacing:0'><tr><td id=s colspan=2 style='padding:0'>aaaaaaaaaa</td></tr>"
                                   "<tr><td id=a style='padding:0'>a</td><td id=b style='padding:0'>b</td></tr></table>", 400);
        CHECK(BoxIs(r, "s", 0, 0, 80, L));
        CHECK(BoxIs(r, "a", 0, L, 40, L));
        CHECK(BoxIs(r, "b", 40, L, 40, L));
    }
    {
        // rowspan: the cell of the second row goes into column 2, beside the spanning cell, which is two rows tall
        const LayoutResult r = Lay(B0 + "<table style='border-spacing:0'><tr><td id=r rowspan=2 style='padding:0'>a</td><td style='padding:0'>b</td></tr>"
                                   "<tr><td id=c style='padding:0'>c</td></tr></table>", 400);
        CHECK(BoxIs(r, "r", 0, 0, 8, 2 * L));
        CHECK(BoxIs(r, "c", 8, L, 8, L));
    }
    {
        // fixed table layout (17.5.2.1): the first row's widths; the other column gets the rest of the 300 px
        const LayoutResult r = Lay(B0 + "<table style='table-layout:fixed;width:300px;border-spacing:0'><tr><td id=a style='width:100px;padding:0'>a</td>"
                                   "<td id=b style='padding:0'>" + std::string(60, 'b') + "</td></tr></table>", 500);
        CHECK(BoxIs(r, "a", 0, 0, 100, 3 * L));   // the 480 px word is broken into 3 lines of the 200 px column, not widened
        const BoxInfo* b = ById(r, "b");
        CHECK(b && Near(b->x, 100) && Near(b->w, 200));
    }
    {
        // a percentage column: 25 % of 400; the other column takes the rest
        const LayoutResult r = Lay(B0 + "<table style='width:400px;border-spacing:0'><tr><td id=a style='width:25%;padding:0'>a</td>"
                                   "<td id=b style='padding:0'>b</td></tr></table>", 500);
        CHECK(BoxIs(r, "a", 0, 0, 100, L));
        CHECK(BoxIs(r, "b", 100, 0, 300, L));
    }
    {
        // an auto-width table is as wide as its columns want (shrink to fit), here 4 characters; the caption is above the rows
        const LayoutResult r = Lay(B0 + "<table id=t style='border-spacing:0'><caption id=cap>Cap</caption><tr><td id=a style='padding:0'>aaaa</td></tr></table>", 500);
        const BoxInfo* t = ById(r, "t");
        CHECK(t && Near(t->w, 32));
        CHECK(BoxIs(r, "cap", 0, 0, 32, L));
        CHECK(BoxIs(r, "a", 0, L, 32, L));
        // caption-side: bottom
        const LayoutResult r2 = Lay(B0 + "<table style='border-spacing:0'><caption id=cap style='caption-side:bottom'>Cap</caption><tr><td id=a style='padding:0'>aaaa</td></tr></table>", 500);
        CHECK(BoxIs(r2, "a", 0, 0, 32, L));
        CHECK(BoxIs(r2, "cap", 0, L, 32, L));
    }
    {
        // vertical-align: middle (the default for cells): "b" sits in the middle of the 100 px row
        const LayoutResult r = Lay(B0 + "<table style='border-spacing:0'><tr><td style='padding:0;height:100px'>a</td><td style='padding:0'>b</td>"
                                   "<td style='padding:0;vertical-align:bottom'>c</td><td style='padding:0;vertical-align:top'>d</td></tr></table>", 500);
        CHECK(TextAt(r, "b", 8, (100 - L) / 2 + 1.6f));
        CHECK(TextAt(r, "c", 16, 100 - L + 1.6f));
        CHECK(TextAt(r, "d", 24, 1.6f));
    }
    {
        // border-collapse: no spacing, and the second cell's left border gives way to the first cell's right border: 7 red lines
        const LayoutResult r = Lay(B0 + "<table style='border-collapse:collapse'><tr><td style='border:2px solid #ff0000;padding:0'>a</td>"
                                   "<td style='border:2px solid #ff0000;padding:0'>b</td></tr></table>", 500);
        CHECK(TextAt(r, "a", 2, 3.6f));
        CHECK(TextAt(r, "b", 12, 3.6f));
        CHECK(CountFill(r, Color{255, 0, 0, 255}) == 7);
    }
    {
        // presentational attributes (the HTML standard 15.3.8): border=1 cellspacing=0 cellpadding=5 bgcolor
        const LayoutResult r = Lay(B0 + "<table border=1 cellspacing=0 cellpadding=5 bgcolor='#00ff00'><tr><td>a</td></tr></table>", 500);
        CHECK(TextAt(r, "a", 1 + 1 + 5, 1 + 1 + 5 + 1.6f));
        CHECK(CountFill(r, Color{0, 255, 0, 255}) == 1);
    }
    {
        // row groups: thead first, tfoot last, whatever their order in the source
        const LayoutResult r = Lay(B0 + "<table><tfoot><tr><td>F</td></tr></tfoot><tbody><tr><td>B</td></tr></tbody><thead><tr><td>H</td></tr></thead></table>", 500);
        const DrawCmd *h = TextCmd(r, "H"), *b = TextCmd(r, "B"), *f = TextCmd(r, "F");
        CHECK(h && b && f && h->y < b->y && b->y < f->y);
        // a row's background is drawn under its cells
        const LayoutResult r2 = Lay(B0 + "<table style='border-spacing:0'><tr style='background:#0000ff'><td style='padding:0'>a</td></tr></table>", 500);
        CHECK(CountFill(r2, Color{0, 0, 255, 255}) == 1);
    }
    {
        // cells straight in a table (no row) get an anonymous row (17.2.1)
        const LayoutResult r = Lay(B0 + "<div style='display:table;border-spacing:0'><div style='display:table-cell' id=a>a</div><div style='display:table-cell' id=b>b</div></div>", 500);
        CHECK(BoxIs(r, "a", 0, 0, 8, L));
        CHECK(BoxIs(r, "b", 8, 0, 8, L));
    }

    // ---- gradients (CSS Images 3) ----
    {
        const LayoutResult r = Lay(B0 + "<div style='width:100px;height:50px;background:linear-gradient(to right, #ff0000, #0000ff)'></div>", 300);
        const Color left = FillAt(r, 1, 25), right = FillAt(r, 99, 25), mid = FillAt(r, 50, 25);
        CHECK(left.r > 240 && left.b < 15);
        CHECK(right.b > 240 && right.r < 15);
        CHECK(mid.r > 110 && mid.r < 145 && mid.b > 110 && mid.b < 145);
        CHECK(CountKind(r, DrawCmd::Fill) >= 20);
        for (const DrawCmd& c : r.cmds) if (c.kind == DrawCmd::Fill) CHECK(c.x >= -0.01f && c.x + c.w <= 100.01f && c.y >= -0.01f && c.y + c.h <= 50.01f);
    }
    {
        // the default direction is to bottom; stops with positions; a hard stop
        const LayoutResult r = Lay(B0 + "<div style='width:100px;height:100px;background-image:linear-gradient(#ff0000 50%, #00ff00 50%)'></div>", 300);
        CHECK(FillAt(r, 50, 10) == (Color{255, 0, 0, 255}));
        CHECK(FillAt(r, 50, 90) == (Color{0, 255, 0, 255}));
        CHECK(CountKind(r, DrawCmd::Fill) == 2);   // equal bands joined
    }
    {
        // repeating: 10 px red, 10 px blue, again
        const LayoutResult r = Lay(B0 + "<div style='width:100px;height:20px;background:repeating-linear-gradient(90deg, #ff0000 0 10px, #0000ff 10px 20px)'></div>", 300);
        CHECK(FillAt(r, 5, 5) == (Color{255, 0, 0, 255}));
        CHECK(FillAt(r, 15, 5) == (Color{0, 0, 255, 255}));
        CHECK(FillAt(r, 25, 5) == (Color{255, 0, 0, 255}));
        CHECK(FillAt(r, 95, 5) == (Color{0, 0, 255, 255}));
    }
    {
        // radial: the first colour in the middle, the last at the corners
        const LayoutResult r = Lay(B0 + "<div style='width:100px;height:100px;background:radial-gradient(circle, #ffffff, #000000)'></div>", 300);
        const Color c = FillAt(r, 50, 50), k = FillAt(r, 1, 1);
        CHECK(c.r > 235);
        CHECK(k.r < 20);
        // a diagonal: colours along the line, not across it
        const LayoutResult r2 = Lay(B0 + "<div style='width:100px;height:100px;background:linear-gradient(45deg, #000000, #ffffff)'></div>", 300);
        CHECK(FillAt(r2, 2, 98).r < 30 && FillAt(r2, 98, 2).r > 225);
        const Color a = FillAt(r2, 80, 80), b = FillAt(r2, 20, 20);
        CHECK(std::abs((int)a.r - (int)b.r) < 20);
    }
    {
        // hostile: many gradient boxes stay inside the page's budget of commands
        std::string page = B0;
        for (int i = 0; i < 3000; ++i) page += "<div style='height:20px;background:radial-gradient(#ff0000, #0000ff)'></div>";
        const LayoutResult r = Lay(page, 800);
        CHECK(CountKind(r, DrawCmd::Fill) <= 60000 + 3000);
        CHECK(CountKind(r, DrawCmd::Fill) >= 3000);
    }

    // ---- counters and list styles (CSS 2.1 12.4, 12.6.2) ----
    {
        const LayoutResult r = Lay(B0 + "<style>body{counter-reset:s} h2::before{counter-increment:s;content:counter(s) '. '}"
                                   "h3::before{content:counter(s, upper-roman)}</style><h2>A</h2><h2>B</h2><h3>C</h3>", 400);
        CHECK(TextCmd(r, "1. ") != nullptr);
        CHECK(TextCmd(r, "2. ") != nullptr);
        CHECK(TextCmd(r, "II") != nullptr);
    }
    {
        // counters() joins the nested ones; each ol starts its own
        const LayoutResult r = Lay(B0 + "<style>ol{counter-reset:i;list-style:none} li::before{counter-increment:i;content:counters(i, '.') ' '}</style>"
                                   "<ol><li>a<ol><li>b</li><li>c</li></ol></li><li>d</li></ol>", 400);
        CHECK(TextCmd(r, "1 ") != nullptr);
        CHECK(TextCmd(r, "1.1 ") != nullptr);
        CHECK(TextCmd(r, "1.2 ") != nullptr);
        CHECK(TextCmd(r, "2 ") != nullptr);
    }
    {
        const LayoutResult r = Lay(B0 + "<ol style='list-style-type:upper-roman' start=4><li>x</li></ol>"
                                   "<ol style='list-style-type:lower-greek'><li>x</li><li>y</li></ol>"
                                   "<ol style='list-style-type:decimal-leading-zero'><li>x</li></ol>", 400);
        CHECK(TextCmd(r, "IV.") != nullptr);
        CHECK(TextCmd(r, "\xCE\xB2.") != nullptr);   // beta
        CHECK(TextCmd(r, "01.") != nullptr);
    }

    // ---- :has() (Selectors 4, 4.5) ----
    {
        const LayoutResult r = Lay(B0 + "<style>div:has(> img){background:#00ff00} section:has(.x){background:#0000ff} p:has(+ h4){background:#ff0000}</style>"
                                   "<div style='height:10px'><img src=a width=5 height=5></div><div style='height:10px'>no</div>"
                                   "<section style='height:5px'><span><b class=x>x</b></span></section><section style='height:5px'>no</section>"
                                   "<p style='height:5px'>p</p><h4>h</h4><p style='height:5px'>p</p>", 400);
        CHECK(CountFill(r, Color{0, 255, 0, 255}) == 1);
        CHECK(CountFill(r, Color{0, 0, 255, 255}) == 1);
        CHECK(CountFill(r, Color{255, 0, 0, 255}) == 1);
    }

    // ---- white-space and text-overflow ----
    {
        const LayoutResult r = Lay(B0 + "<div style='width:80px;overflow:hidden;white-space:nowrap;text-overflow:ellipsis'>" + std::string(20, 'a') + "</div>", 400);
        CHECK(TextAt(r, std::string(9, 'a') + "\xE2\x80\xA6", 0, 1.6f));
        // without overflow: hidden there is nothing to cut
        const LayoutResult r2 = Lay(B0 + "<div style='width:80px;white-space:nowrap;text-overflow:ellipsis'>" + std::string(20, 'a') + "</div>", 400);
        CHECK(TextCmd(r2, std::string(20, 'a')) != nullptr);
    }
    {
        const LayoutResult r = Lay(B0 + "<div style='width:40px;white-space:nowrap'>aa bb cc</div>", 400);
        CHECK(TextAt(r, "aa bb cc", 0, 1.6f));
        const LayoutResult r2 = Lay(B0 + "<div style='width:40px;white-space:pre-wrap'>aa  bb</div>", 400);
        CHECK(TextAt(r2, "bb", 0, L + 1.6f));
        CHECK(TextCmd(r2, "aa  ") != nullptr);   // the spaces are kept
    }

    // ---- text decoration styles ----
    {
        const LayoutResult r = Lay(B0 + "<p style='margin:0'><span style='text-decoration:underline wavy #ff0000'>abcd</span></p>", 400);
        const DrawCmd* t = TextCmd(r, "abcd");
        CHECK(t && !t->underline);   // drawn by the layout, not by the host's plain line
        CHECK(CountFill(r, Color{255, 0, 0, 255}) >= 20);
        const LayoutResult r2 = Lay(B0 + "<p style='margin:0;text-decoration:overline dotted'>abcd</p>", 400);
        CHECK(CountFill(r2, Color{0, 0, 0, 255}) >= 10);
        const LayoutResult r3 = Lay(B0 + "<p style='margin:0'><u>abcd</u></p>", 400);
        const DrawCmd* t3 = TextCmd(r3, "abcd");
        CHECK(t3 && t3->underline && CountKind(r3, DrawCmd::Fill) == 0);   // a plain underline stays the host's
        const LayoutResult r4 = Lay(B0 + "<p style='margin:0;text-decoration:underline double #0000ff'>ab</p>", 400);
        CHECK(CountFill(r4, Color{0, 0, 255, 255}) == 2);
        // hostile: thousands of wavy runs stay inside the page's budget (then they are plain lines)
        std::string many = B0;
        for (int i = 0; i < 10000; ++i) many += "<p style='margin:0;text-decoration:underline wavy #ff0000'>abcd</p>";
        const LayoutResult r5 = Lay(many, 400);
        const int fills = CountFill(r5, Color{255, 0, 0, 255});
        CHECK(fills <= 200000 + 40 + 10000 && fills >= 10000);
    }

    // ---- object-fit (CSS Images 3, 5.5) ----
    {
        const ImageSizeFn img = [](const std::string&, int& w, int& h) { w = 200; h = 100; return true; };
        const LayoutResult r = Lay(B0 + "<img src=p.png style='display:block;width:100px;height:100px;object-fit:contain'>", 400, img);
        const DrawCmd* c = nullptr;
        for (const DrawCmd& k : r.cmds) if (k.kind == DrawCmd::Image) c = &k;
        CHECK(c && Near(c->x, 0) && Near(c->y, 25) && Near(c->w, 100) && Near(c->h, 50));
        const LayoutResult r2 = Lay(B0 + "<img src=p.png style='width:100px;height:100px;object-fit:cover'>", 400, img);
        const DrawCmd* b = nullptr;
        for (const DrawCmd& k : r2.cmds) if (k.kind == DrawCmd::BgImage) b = &k;
        CHECK(b && Near(b->w, 100) && Near(b->h, 100) && Near(b->iw, 200) && Near(b->ih, 100) && Near(b->ix, -50) && !b->repeatX && !b->repeatY);
        CHECK(CountKind(r2, DrawCmd::Image) == 0);
    }

    // ---- transforms (CSS Transforms 1) and transitions ----
    {
        // scale(2) about the middle, then 10, 20 across: the box's extent
        const LayoutResult r = Lay(B0 + "<div id=t style='width:100px;height:50px;background:#ff0000;transform:translate(10px, 20px) scale(2)'></div>", 400);
        CHECK(BoxIs(r, "t", -40, -5, 200, 100));
        int k = -1;
        for (std::size_t i = 0; i < r.cmds.size(); ++i) if (r.cmds[i].color == (Color{255, 0, 0, 255})) k = (int)i;
        CHECK(k >= 0 && Near(r.cmds[(std::size_t)k].x, -40) && Near(r.cmds[(std::size_t)k].w, 200) && Near(r.cmds[(std::size_t)k].h, 100));
        // a quarter turn: the extent of the turned box (its drawing keeps the turn for the host)
        const LayoutResult r2 = Lay(B0 + "<div id=t style='width:100px;height:50px;transform:rotate(90deg)'></div>", 400);
        CHECK(BoxIs(r2, "t", 25, -25, 50, 100));
        // a transition shows the final value at once
        const LayoutResult r3 = Lay(B0 + "<div id=t style='transition:width 2s ease;width:50px;height:10px'></div>", 400);
        CHECK(BoxIs(r3, "t", 0, 0, 50, 10));
    }

    // ---- hostile tables ----
    {
        const LayoutResult r = Lay(B0 + "<table><tr><td colspan=100000 rowspan=100000>a</td><td colspan=-5>b</td></tr><tr><td>c</td></tr></table>", 800);
        // the spans are cut to 1000 columns and to the rows there are; cells with no column left are dropped
        CHECK(TextCmd(r, "a") != nullptr);
        const BoxInfo* tb = nullptr;
        for (const BoxInfo& b : r.boxes) if (b.tag == "table") tb = &b;
        CHECK(tb && tb->w <= 1000 * 2 + 1001 * 2 + 1);
        std::string many = B0 + "<table><tr>";
        for (int i = 0; i < 3000; ++i) many += "<td>x</td>";
        many += "</tr></table>";
        const LayoutResult r2 = Lay(many, 800);
        int cells = 0;
        for (const BoxInfo& b : r2.boxes) if (b.tag == "td") ++cells;
        CHECK(cells == 1000);   // at most 1000 columns
    }
    // ---- real pages, round 1 (the owner's Google screenshot and the pages checked next to it) ----
    {
        // A "25% | content | 25%" row in a table with no width (a search page's form): the table is made wide enough for the
        // percentages to hold with the middle column at its maximum width (200 / 50% = 400 px), so the right column starts after
        // the middle one instead of over it. Before: the table was 272 wide and the right column started at 268, inside the
        // 200 px box of the middle column (100 + 200 = 300 is where it ends now).
        const std::string page = B0 + "<table cellspacing=0 cellpadding=0><tr><td id=a width='25%'>x</td><td id=b nowrap>"
                                 "<div style='display:inline-block;width:200px;height:10px'></div></td><td id=c width='25%'>"
                                 "<a style='display:block'>Advanced</a></td></tr></table>";
        auto xw = [](const LayoutResult& r, const char* id, float x, float w) {
            const BoxInfo* b = ById(r, id);
            if (b && Near(b->x, x) && Near(b->w, w)) return true;
            std::cerr << "  #" << id << ": got " << (b ? b->x : -1) << " w " << (b ? b->w : -1) << "  want " << x << " w " << w << "\n";
            return false;
        };
        const LayoutResult r = Lay(page, 600);
        CHECK(xw(r, "a", 0, 100));
        CHECK(xw(r, "b", 100, 200));
        CHECK(xw(r, "c", 300, 100));
        // Too narrow for 25% + 200 + 25% (300 px): the percentage columns give back what they have above their minimum
        // (75 - 8 and 75 - 64, in that proportion), so the middle column keeps its 200 px and nothing overlaps.
        const LayoutResult n = Lay(page, 300);
        CHECK(xw(n, "a", 0, 75.0f - 50.0f * 67.0f / 78.0f));
        CHECK(xw(n, "b", 75.0f - 50.0f * 67.0f / 78.0f, 200));
        CHECK(xw(n, "c", 75.0f - 50.0f * 67.0f / 78.0f + 200.0f, 75.0f - 50.0f * 11.0f / 78.0f));
    }
    {
        // <font size color face> (HTML 15.3.4, the legacy font sizes 1-7 and +n / -n from 3): x-small 10, x-large 24, xxx-large
        // 48, small 13; <p align> (15.3.3); <body marginwidth / marginheight> (15.3.2).
        const LayoutResult r = Lay("<body marginwidth=0 marginheight=0><p style='margin:0'><font size=1>aa</font> <font size='+2'>bb</font> "
                                   "<font size=7>cc</font> <font size=-1>dd</font> <font color=red face='Courier New'>ee</font></p>"
                                   "<p align=center style='margin:0;width:100px'>ab</p>", 400);
        const DrawCmd* aa = TextCmd(r, "aa"); const DrawCmd* bb = TextCmd(r, "bb"); const DrawCmd* cc = TextCmd(r, "cc");
        const DrawCmd* dd = TextCmd(r, "dd"); const DrawCmd* ee = TextCmd(r, "ee");
        CHECK(aa && Near(aa->font.size, 10) && Near(aa->x, 0));
        CHECK(bb && Near(bb->font.size, 24));
        CHECK(cc && Near(cc->font.size, 48));
        CHECK(dd && Near(dd->font.size, 13));
        CHECK(ee && ee->color == (Color{255, 0, 0, 255}) && ee->font.family.find("courier new") != std::string::npos);
        const DrawCmd* ab = TextCmd(r, "ab");
        CHECK(ab && Near(ab->x, (100 - 16) * 0.5f));
    }
    {
        // sub / sup (HTML 15.3.4: vertical-align sub / super, font-size smaller): the superscript's baseline is a third of the
        // parent's 20 px font above the line's (6.67 px), the subscript's a fifth below (4 px); a link inside the <sup> moves
        // with it. Text is drawn from its em box top, 0.8 of its font size above its baseline: "x" at base - 16, "2" (16.67 px)
        // at base - 6.67 - 13.33 = base - 20, "3" at base + 4 - 13.33.
        const LayoutResult r = Lay(B0 + "<p style='font-size:20px;line-height:30px'>x<sup>2<a href='#n'>[1]</a></sup>y<sub>3</sub></p>", 400);
        const DrawCmd* x = TextCmd(r, "x"); const DrawCmd* two = TextCmd(r, "2"); const DrawCmd* one = TextCmd(r, "[1]");
        const DrawCmd* three = TextCmd(r, "3");
        CHECK(x && two && one && three);
        if (x && two && one && three) {
            CHECK(Near(two->y - x->y, -4.0f));
            CHECK(Near(one->y, two->y));
            CHECK(Near(three->y - x->y, 16.0f + 4.0f - 20.0f / 1.2f * 0.8f));
        }
    }
    {
        // A control a page hides with opacity: 0 (its own button is drawn over it) is not drawn, but keeps its box for clicks; an
        // element masked by an image (mask-image: url(), an icon) is hidden - the mask image is not loaded, so it counts as
        // transparent (CSS Masking 1, 6.1) - while a gradient mask is not applied (the box is drawn as it is).
        const LayoutResult r = Lay(B0 + "<div style='opacity:0'><input type=checkbox></div><input type=text>"
                                   "<span style='display:inline-block;width:20px;height:20px;background-color:#ff0000;mask-image:url(i.svg)'></span>"
                                   "<div style='width:20px;height:20px;background:#00ff00;-webkit-mask-image:linear-gradient(black,transparent)'></div>", 400);
        CHECK(CountKind(r, DrawCmd::Control) == 1);
        CHECK(r.controls.size() == 2);
        CHECK(CountFill(r, Color{255, 0, 0, 255}) == 0);
        CHECK(CountFill(r, Color{0, 255, 0, 255}) >= 1);
    }
    {
        // Quirks mode (no DOCTYPE, HTML 15.3.8): a table does not inherit the text-align and font-size around it. With
        // <!DOCTYPE html> the cell's "ab" is centred in the 300 px table at 32 px (32 wide: x = 134); without it, it is at the
        // left at 16 px.
        const std::string body = "<body style='margin:0'><div style='text-align:center;font-size:32px'><table style='width:300px' "
                                 "cellspacing=0 cellpadding=0><tr><td>ab</td></tr></table></div>";
        const LayoutResult std_ = Lay("<!DOCTYPE html>" + body, 400);
        const LayoutResult q = Lay(body, 400);
        const DrawCmd* s1 = TextCmd(std_, "ab"); const DrawCmd* q1 = TextCmd(q, "ab");
        CHECK(s1 && Near(s1->font.size, 32) && Near(s1->x, 134));
        CHECK(q1 && Near(q1->font.size, 16) && Near(q1->x, 0));
    }
    TEST_MAIN_RESULT();
}
