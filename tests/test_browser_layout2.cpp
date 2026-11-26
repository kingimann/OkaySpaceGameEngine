// Browser (page engine): round 1 of "a regular browser" - real layout (engine/include/okay/Browser/Layout.hpp, Css.hpp).
// Every box below is worked out by hand from the specifications (CSS 2.1 9.5 floats and 9.6 / 10.3.7 positioning, CSS Flexible
// Box Layout 1 section 9, CSS Grid Layout 1 sections 7, 8 and 11, CSS Box Sizing 3, CSS Values 4, Media Queries 4, Selectors 4)
// with the fixed text measure of the layout test: every character is half the font size wide (8 px at 16 px), a normal line is
// 1.2 x 16 = 19.2 px and the text's em box starts 1.6 px below the line top.
#include "test_framework.hpp"
#include "okay/Browser/Layout.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <string>

using namespace okay::browser;

static const FixedMeasure kMeasure(0.5f);

static std::deque<HtmlDocument> g_docs;   // kept: the boxes point at their elements
static LayoutResult Lay(const std::string& html, float width, const ImageSizeFn& img = ImageSizeFn(), const LayoutContext* ctx = nullptr) {
    g_docs.push_back(ParseHtml(html));
    return LayoutPage(g_docs.back(), width, kMeasure, img, ctx);
}
// The box of the element with id `id`.
static const BoxInfo* ById(const LayoutResult& r, const std::string& id) {
    for (const BoxInfo& b : r.boxes) if (b.node) if (const std::string* v = b.node->Attr("id")) if (*v == id) return &b;
    return nullptr;
}
static bool Near(float a, float b) { return std::fabs(a - b) < 0.05f; }
static bool BoxIs(const LayoutResult& r, const std::string& id, float x, float y, float w, float h) {
    const BoxInfo* b = ById(r, id);
    if (!b) { std::cerr << "  no box #" << id << "\n"; return false; }
    if (Near(b->x, x) && Near(b->y, y) && Near(b->w, w) && Near(b->h, h)) return true;
    std::cerr << "  #" << id << ": got " << b->x << "," << b->y << " " << b->w << "x" << b->h << "  want " << x << "," << y << " " << w << "x" << h << "\n";
    return false;
}
static const DrawCmd* TextCmd(const LayoutResult& r, const std::string& text) {
    for (const DrawCmd& c : r.cmds) if (c.kind == DrawCmd::Text && c.text == text) return &c;
    return nullptr;
}
static bool TextAt(const LayoutResult& r, const std::string& text, float x, float y) {
    const DrawCmd* c = TextCmd(r, text);
    if (!c) { std::cerr << "  no text '" << text << "'\n"; return false; }
    if (Near(c->x, x) && Near(c->y, y)) return true;
    std::cerr << "  '" << text << "': got " << c->x << "," << c->y << " want " << x << "," << y << "\n";
    return false;
}
static int IndexOfFill(const LayoutResult& r, Color c) {
    for (std::size_t i = 0; i < r.cmds.size(); ++i) if (r.cmds[i].kind == DrawCmd::Fill && r.cmds[i].color == c) return (int)i;
    return -1;
}
static const std::string B0 = "<body style='margin:0'>";

int main() {
    RUN_SUITE("page layout: floats, positioning, overflow, flexbox, grid");
    const float L = 19.2f;   // a normal line

    // ---- floats and clear (CSS 2.1 9.5) ----
    {
        // a left float: the paragraph's box is not narrowed, its line is (9.5: "line boxes ... are shortened")
        const LayoutResult r = Lay(B0 + "<div id=f style='float:left;width:100px;height:50px'></div><p id=p style='margin:0'>aaa bbb</p>", 300);
        CHECK(BoxIs(r, "f", 0, 0, 100, 50));
        CHECK(BoxIs(r, "p", 0, 0, 300, L));
        CHECK(TextAt(r, "aaa bbb", 100, 1.6f));
        // the root is a block formatting context: it holds the float (10.6.7)
        CHECK(Near(r.boxes[0].h, 50));
    }
    {
        // text wraps beside the float while it lasts, then uses the full width
        const LayoutResult r = Lay(B0 + "<div style='width:200px'><div style='float:left;width:50px;height:40px'></div>"
                                   "aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii</div>", 400);
        CHECK(TextAt(r, "aaaa bbbb cccc", 50, 1.6f));            // 150 px beside the float: 112 px fits, +40 does not
        CHECK(TextAt(r, "dddd eeee ffff", 50, L + 1.6f));        // the second line still beside it (19.2 < 40)
        CHECK(TextAt(r, "gggg hhhh iiii", 50, 2 * L + 1.6f));    // the third line starts at 38.4, still beside it (40)
    }
    {
        // two left floats side by side, a right float, and clear: both below them
        const LayoutResult r = Lay(B0 + "<div id=a style='float:left;width:100px;height:30px'></div>"
                                   "<div id=b style='float:left;width:80px;height:50px'></div>"
                                   "<div id=c style='float:right;width:60px;height:20px'></div>"
                                   "<div id=d style='clear:both;height:10px'></div>", 400);
        CHECK(BoxIs(r, "a", 0, 0, 100, 30));
        CHECK(BoxIs(r, "b", 100, 0, 80, 50));
        CHECK(BoxIs(r, "c", 340, 0, 60, 20));
        CHECK(BoxIs(r, "d", 0, 50, 400, 10));
        // clear: left only clears the left floats
        const LayoutResult r2 = Lay(B0 + "<div style='float:left;width:10px;height:30px'></div><div style='float:right;width:10px;height:70px'></div>"
                                    "<div id=e style='clear:left'>x</div>", 200);
        CHECK(BoxIs(r2, "e", 0, 30, 200, L));
    }
    {
        // a float too wide for the space left goes below the floats in the way (9.5.1 rule 7)
        const LayoutResult r = Lay(B0 + "<div style='width:300px'><div id=a style='float:left;width:200px;height:30px'></div>"
                                   "<div id=b style='float:left;width:150px;height:20px'></div></div>", 500);
        CHECK(BoxIs(r, "a", 0, 0, 200, 30));
        CHECK(BoxIs(r, "b", 0, 30, 150, 20));
        // a float with no width shrinks to fit its content (10.3.5): "hello" is 40 px
        const LayoutResult r2 = Lay(B0 + "<div id=f style='float:right'>hello</div>", 300);
        CHECK(BoxIs(r2, "f", 260, 0, 40, L));
        // a box with overflow other than visible is a new formatting context: it holds its floats and goes beside others
        const LayoutResult r3 = Lay(B0 + "<div id=o style='overflow:hidden'><div style='float:left;width:50px;height:60px'></div>x</div>", 300);
        CHECK(BoxIs(r3, "o", 0, 0, 300, 60));
        const LayoutResult r4 = Lay(B0 + "<div style='float:left;width:50px;height:60px'></div><div id=o style='overflow:hidden'>x</div>", 300);
        CHECK(BoxIs(r4, "o", 50, 0, 250, L));
    }

    // ---- positioning (CSS 2.1 9.3, 10.3.7, 10.6.4) and paint order ----
    {
        const LayoutResult r = Lay(B0 + "<div id=rel style='position:relative;left:20px;top:10px;height:30px'></div>", 300);
        CHECK(BoxIs(r, "rel", 20, 10, 300, 30));
        const LayoutResult r2 = Lay(B0 + "<div style='height:40px'></div><div id=cb style='position:relative;width:200px;height:100px;border:5px solid red'>"
                                    "<div id=br style='position:absolute;right:10px;bottom:20px;width:50px;height:30px'></div>"
                                    "<div id=lr style='position:absolute;left:10px;right:30px;top:0'>x</div>"
                                    "<div id=sh style='position:absolute;left:0;top:0'>hello</div></div>", 400);
        // the containing block is the padding box: (5, 45) 200 x 100
        CHECK(BoxIs(r2, "br", 5 + 200 - 10 - 50, 45 + 100 - 20 - 30, 50, 30));
        CHECK(BoxIs(r2, "lr", 15, 45, 160, L));                 // left and right set: the width is what is left
        CHECK(BoxIs(r2, "sh", 5, 45, 40, L));                   // only left: shrink-to-fit
        // static position: an absolutely positioned box with no offsets stays where it would have been
        const LayoutResult r3 = Lay(B0 + "<div style='height:25px'></div><div id=s style='position:absolute'>a</div>", 300);
        CHECK(BoxIs(r3, "s", 0, 25, 8, L));
        // position: fixed is drawn on the view, not moved by the page's scroll
        const LayoutResult r4 = Lay(B0 + "<div style='position:fixed;bottom:0;left:0;right:0;height:20px;background:#00f'></div>", 400);
        const int fi = IndexOfFill(r4, Color{0, 0, 255, 255});
        CHECK(fi >= 0 && r4.cmds[(std::size_t)fi].fixed && Near(r4.cmds[(std::size_t)fi].y, 300 - 20));   // the view is 400 x 300
        // z-index: the higher layer is drawn later, whatever the order in the page
        const LayoutResult r5 = Lay(B0 + "<div style='position:absolute;z-index:2;width:10px;height:10px;background:#f00'></div>"
                                    "<div style='position:absolute;z-index:1;width:10px;height:10px;background:#0f0'></div>"
                                    "<div style='width:10px;height:10px;background:#00f'></div>", 100);
        const int red = IndexOfFill(r5, Color{255, 0, 0, 255}), green = IndexOfFill(r5, Color{0, 255, 0, 255}), blue = IndexOfFill(r5, Color{0, 0, 255, 255});
        CHECK(blue >= 0 && green > blue && red > green);
        const LayoutResult r6 = Lay(B0 + "<div style='position:relative;z-index:-1;height:10px;background:#f00'></div><div style='height:10px;background:#00f'></div>", 100);
        CHECK(IndexOfFill(r6, Color{255, 0, 0, 255}) < IndexOfFill(r6, Color{0, 0, 255, 255}));   // a negative z-index goes below
    }

    // ---- overflow: clips and scrollable areas (CSS Overflow 3) ----
    {
        const LayoutResult r = Lay(B0 + "<div style='height:50px;overflow:auto;padding:5px'><div style='height:200px'></div><a href='/x'>x</a></div>", 300);
        CHECK(r.scrollers.size() == 1 && r.clips.size() == 1);
        if (r.scrollers.size() == 1) {
            const Scroller& s = r.scrollers[0];
            CHECK(Near(s.x, 0) && Near(s.y, 0) && Near(s.w, 300) && Near(s.h, 60));   // the padding box
            CHECK(Near(s.contentH, 5 + 200 + L + 5));
            // the link is at y = 205 inside the scroller: hidden until it is scrolled to
            CHECK(LinkAt(r, 10, 210) == -1);
            ScrollOffsets off{{0.0f, 180.0f}};
            CHECK(LinkAt(r, 10, 210 - 180, &off) == 0);
            CHECK(ScrollerAt(r, 20, 20) == 0 && ScrollerAt(r, 20, 100) == -1);
        }
        const LayoutResult r2 = Lay(B0 + "<div style='height:20px;overflow:hidden'>a<br>b<br>c</div>", 300);
        CHECK(r2.clips.size() == 1 && r2.scrollers.empty());
        for (const DrawCmd& c : r2.cmds) if (c.kind == DrawCmd::Text) CHECK(c.clip == 0);
    }

    // ---- flexbox (CSS Flexible Box Layout 1, section 9) ----
    {
        // flex: 1 and 2 share 300 px by their factors (the basis is 0)
        const LayoutResult r = Lay(B0 + "<div style='display:flex;width:300px'><div id=a style='flex:1'>a</div><div id=b style='flex:2'>b</div></div>", 400);
        CHECK(BoxIs(r, "a", 0, 0, 100, L));
        CHECK(BoxIs(r, "b", 100, 0, 200, L));
        // a min-width violation freezes the item at its minimum, the others share the rest (9.7 step 4)
        const LayoutResult r2 = Lay(B0 + "<div style='display:flex;width:300px'><div id=a style='flex:1;min-width:150px'></div>"
                                    "<div id=b style='flex:1'></div><div id=c style='flex:1'></div></div>", 400);
        CHECK(BoxIs(r2, "a", 0, 0, 150, 0));
        CHECK(BoxIs(r2, "b", 150, 0, 75, 0));
        CHECK(BoxIs(r2, "c", 225, 0, 75, 0));
        // shrinking is weighted by flex-shrink x the base size (9.7 step 4c): 200 + 200 in 300 with shrink 1 and 3
        const LayoutResult r3 = Lay(B0 + "<div style='display:flex;width:300px'><div id=a style='width:200px'>a</div>"
                                    "<div id=b style='width:200px;flex-shrink:3'>b</div></div>", 400);
        CHECK(BoxIs(r3, "a", 0, 0, 175, L));
        CHECK(BoxIs(r3, "b", 175, 0, 125, L));
        // justify-content
        const std::string three = "<div id=a style='width:50px'>a</div><div id=b style='width:50px'>b</div><div id=c style='width:50px'>c</div></div>";
        const LayoutResult r4 = Lay(B0 + "<div style='display:flex;width:300px;justify-content:space-between'>" + three, 400);
        CHECK(BoxIs(r4, "a", 0, 0, 50, L) && BoxIs(r4, "b", 125, 0, 50, L) && BoxIs(r4, "c", 250, 0, 50, L));
        const LayoutResult r5 = Lay(B0 + "<div style='display:flex;width:300px;justify-content:center'>" + three, 400);
        CHECK(BoxIs(r5, "a", 75, 0, 50, L) && BoxIs(r5, "c", 175, 0, 50, L));
        const LayoutResult r6 = Lay(B0 + "<div style='display:flex;width:300px;justify-content:space-around'>" + three, 400);
        CHECK(BoxIs(r6, "a", 25, 0, 50, L) && BoxIs(r6, "b", 125, 0, 50, L) && BoxIs(r6, "c", 225, 0, 50, L));
        const LayoutResult r7 = Lay(B0 + "<div style='display:flex;width:300px;justify-content:space-evenly'>" + three, 400);
        CHECK(BoxIs(r7, "a", 37.5f, 0, 50, L) && BoxIs(r7, "c", 212.5f, 0, 50, L));
        const LayoutResult r8 = Lay(B0 + "<div style='display:flex;width:300px;justify-content:flex-end'>" + three, 400);
        CHECK(BoxIs(r8, "a", 150, 0, 50, L));
        // an auto margin takes the free space first (8.1)
        const LayoutResult r9 = Lay(B0 + "<div style='display:flex;width:300px'><div id=a style='width:50px'>a</div><div id=b style='width:50px;margin-left:auto'>b</div></div>", 400);
        CHECK(BoxIs(r9, "b", 250, 0, 50, L));
        // align-items: stretch (the default) fills the line; center and flex-end place the item
        const LayoutResult r10 = Lay(B0 + "<div style='display:flex;height:100px'><div id=s>s</div><div id=c style='align-self:center'>c</div>"
                                     "<div id=e style='align-self:flex-end'>e</div></div>", 400);
        CHECK(BoxIs(r10, "s", 0, 0, 8, 100));
        CHECK(BoxIs(r10, "c", 8, 50 - L / 2, 8, L));
        CHECK(BoxIs(r10, "e", 16, 100 - L, 8, L));
        // gap: (300 - 2 x 10) / 3 each
        const LayoutResult r11 = Lay(B0 + "<div style='display:flex;width:300px;gap:10px'><div id=a style='flex:1'></div><div id=b style='flex:1'></div><div id=c style='flex:1'></div></div>", 400);
        CHECK(BoxIs(r11, "a", 0, 0, 280.0f / 3, 0) && BoxIs(r11, "b", 280.0f / 3 + 10, 0, 280.0f / 3, 0) && BoxIs(r11, "c", 2 * 280.0f / 3 + 20, 0, 280.0f / 3, 0));
        // wrap: two 120 px items per 300 px line
        std::string items5;
        for (int i = 0; i < 5; ++i) items5 += "<div id=w" + std::to_string(i) + " style='width:120px'>x</div>";
        const LayoutResult r12 = Lay(B0 + "<div id=box style='display:flex;flex-wrap:wrap;width:300px'>" + items5 + "</div>", 400);
        CHECK(BoxIs(r12, "w0", 0, 0, 120, L) && BoxIs(r12, "w1", 120, 0, 120, L) && BoxIs(r12, "w2", 0, L, 120, L) && BoxIs(r12, "w4", 0, 2 * L, 120, L));
        CHECK(BoxIs(r12, "box", 0, 0, 300, 3 * L));
        // order, and row-reverse
        const LayoutResult r13 = Lay(B0 + "<div style='display:flex;width:300px'><div id=a style='order:2;width:50px'></div><div id=b style='order:1;width:50px'></div></div>", 400);
        CHECK(BoxIs(r13, "b", 0, 0, 50, 0) && BoxIs(r13, "a", 50, 0, 50, 0));
        const LayoutResult r14 = Lay(B0 + "<div style='display:flex;flex-direction:row-reverse;width:300px'><div id=a style='width:50px'></div><div id=b style='width:50px'></div></div>", 400);
        CHECK(BoxIs(r14, "a", 250, 0, 50, 0) && BoxIs(r14, "b", 200, 0, 50, 0));
        // a column: a header, a body that takes the rest, a footer
        const LayoutResult r15 = Lay(B0 + "<div style='display:flex;flex-direction:column;height:200px;width:100px'><div id=h style='height:30px'></div>"
                                     "<div id=m style='flex:1'></div><div id=f style='height:30px'></div></div>", 400);
        CHECK(BoxIs(r15, "h", 0, 0, 100, 30) && BoxIs(r15, "m", 0, 30, 100, 140) && BoxIs(r15, "f", 0, 170, 100, 30));
        // a column with no height: as tall as its items; align-items:center centres each one
        const LayoutResult r16 = Lay(B0 + "<div id=col style='display:flex;flex-direction:column;align-items:center;width:100px'><div id=a>abc</div><div id=b>a</div></div>", 400);
        CHECK(BoxIs(r16, "col", 0, 0, 100, 2 * L) && BoxIs(r16, "a", 38, 0, 24, L) && BoxIs(r16, "b", 46, L, 8, L));
        // text directly in a flex container is an anonymous item
        const LayoutResult r17 = Lay(B0 + "<div style='display:flex'>hello<b id=b>x</b></div>", 400);
        CHECK(TextAt(r17, "hello", 0, 1.6f) && BoxIs(r17, "b", 40, 0, 8, L));
    }

    // ---- grid (CSS Grid Layout 1) ----
    {
        // fixed and fr columns: 400 - 100 = 300 shared 1:2
        const LayoutResult r = Lay(B0 + "<div style='display:grid;grid-template-columns:100px 1fr 2fr;width:400px'>"
                                   "<div id=a>a</div><div id=b>b</div><div id=c>c</div><div id=d>d</div></div>", 500);
        CHECK(BoxIs(r, "a", 0, 0, 100, L) && BoxIs(r, "b", 100, 0, 100, L) && BoxIs(r, "c", 200, 0, 200, L));
        CHECK(BoxIs(r, "d", 0, L, 100, L));   // auto-placement: the next row
        // gaps
        const LayoutResult r2 = Lay(B0 + "<div style='display:grid;grid-template-columns:100px 1fr 1fr;gap:10px 20px;width:400px'>"
                                    "<div id=a>a</div><div id=b>b</div><div id=c>c</div><div id=d>d</div></div>", 500);
        CHECK(BoxIs(r2, "b", 120, 0, 130, L) && BoxIs(r2, "c", 270, 0, 130, L) && BoxIs(r2, "d", 0, L + 10, 100, L));
        // explicit placement, spans, negative lines
        const LayoutResult r3 = Lay(B0 + "<div style='display:grid;grid-template-columns:repeat(3,100px);width:300px'>"
                                    "<div id=a style='grid-column:2 / 4'>a</div><div id=b style='grid-column:span 2'>b</div>"
                                    "<div id=c style='grid-row:3;grid-column:-2'>c</div><div id=d>d</div></div>", 500);
        CHECK(BoxIs(r3, "a", 100, 0, 200, L));      // lines 2 to 4
        CHECK(BoxIs(r3, "b", 0, L, 200, L));        // two columns: does not fit beside a in row 1, so row 2
        CHECK(BoxIs(r3, "c", 200, 2 * L, 100, L));  // line -2 is the last column's start
        CHECK(BoxIs(r3, "d", 200, L, 100, L));      // sparse: after b in row 2
        // template areas
        const LayoutResult r4 = Lay(B0 + "<div style='display:grid;grid-template-columns:100px 1fr;grid-template-areas:\"head head\" \"side main\";width:400px'>"
                                    "<div id=m style='grid-area:main'>m</div><div id=s style='grid-area:side'>s</div><div id=h style='grid-area:head'>h</div></div>", 500);
        CHECK(BoxIs(r4, "h", 0, 0, 400, L) && BoxIs(r4, "s", 0, L, 100, L) && BoxIs(r4, "m", 100, L, 300, L));
        // repeat(auto-fill, minmax(100px, 1fr)) in 350: three columns of 350 / 3
        const LayoutResult r5 = Lay(B0 + "<div style='display:grid;grid-template-columns:repeat(auto-fill,minmax(100px,1fr));width:350px'>"
                                    "<div id=a>a</div><div id=b>b</div><div id=c>c</div><div id=d>d</div></div>", 500);
        CHECK(BoxIs(r5, "a", 0, 0, 350.0f / 3, L) && BoxIs(r5, "c", 700.0f / 3, 0, 350.0f / 3, L) && BoxIs(r5, "d", 0, L, 350.0f / 3, L));
        // rows: a fixed row and a fr row in a box 200 high; an auto column as wide as its content
        const LayoutResult r6 = Lay(B0 + "<div style='display:grid;grid-template-rows:50px 1fr;grid-template-columns:auto 1fr;height:200px;width:300px'>"
                                    "<div id=a>abcd</div><div id=b>b</div><div id=c>c</div></div>", 500);
        CHECK(BoxIs(r6, "a", 0, 0, 32, 50) && BoxIs(r6, "b", 32, 0, 268, 50) && BoxIs(r6, "c", 0, 50, 32, 150));
        // align-items / justify-items: center in the area
        const LayoutResult r7 = Lay(B0 + "<div style='display:grid;grid-template-columns:100px;grid-template-rows:60px;place-items:center'><div id=a>ab</div></div>", 300);
        CHECK(BoxIs(r7, "a", 42, 30 - L / 2, 16, L));
    }

    // ---- inline-block, replaced boxes, box sizes ----
    {
        const LayoutResult r = Lay(B0 + "<span id=a style='display:inline-block;width:50px;height:20px'></span><span id=b style='display:inline-block'>abc</span>", 300);
        // shrink-to-fit: "abc" is 24 px, after the first box. The empty box's baseline is its bottom (CSS 2.1 10.8.1), so the
        // line's baseline is 20 px down and the text box, whose baseline is 14.4 px below its top, starts at 5.6
        CHECK(BoxIs(r, "b", 50, 5.6f, 24, L));
        CHECK(TextAt(r, "abc", 50, 7.2f));
        auto img = [](const std::string& src, int& w, int& h) { if (src == "a.png") { w = 40; h = 20; return true; } return false; };
        const LayoutResult r2 = Lay(B0 + "<img id=i src=a.png style='display:block;width:80px'>", 300, img);
        CHECK(BoxIs(r2, "i", 0, 0, 80, 40));   // the ratio of the image is kept
        // box-sizing
        const LayoutResult r3 = Lay(B0 + "<div id=c style='width:100px;padding:10px;border:5px solid'></div><div id=b style='width:100px;padding:10px;border:5px solid;box-sizing:border-box'></div>", 300);
        CHECK(BoxIs(r3, "c", 0, 0, 130, 30) && BoxIs(r3, "b", 0, 30, 100, 30));
        // min- and max- sizes, percentages
        const LayoutResult r4 = Lay(B0 + "<div id=a style='width:50px;min-width:80px'></div><div id=b style='max-width:50%'>x</div><div id=c style='min-height:100px'>x</div>", 300);
        CHECK(BoxIs(r4, "a", 0, 0, 80, 0) && BoxIs(r4, "b", 0, 0, 150, L) && BoxIs(r4, "c", 0, L, 300, 100));
        // a height in % of a definite height
        const LayoutResult r5 = Lay(B0 + "<div style='height:200px'><div id=h style='height:25%'></div></div>", 300);
        CHECK(BoxIs(r5, "h", 0, 0, 300, 50));
    }

    // ---- units, calc(), var(), @media ----
    {
        const LayoutResult r = Lay("<html style='font-size:20px'><body style='margin:0'><div id=a style='width:10vw'></div><div id=b style='width:2rem'></div>"
                                   "<div id=c style='width:calc(100% - 20px)'></div><div id=d style='width:calc(50% + 2em)'></div>"
                                   "<div id=e style='width:max(30px, 5vw)'></div></body></html>", 400);
        CHECK(BoxIs(r, "a", 0, 0, 40, 0));
        CHECK(BoxIs(r, "b", 0, 0, 40, 0));    // 2 x the root's 20 px
        CHECK(BoxIs(r, "c", 0, 0, 380, 0));
        CHECK(BoxIs(r, "d", 0, 0, 240, 0));   // 200 + 2 x 20
        CHECK(BoxIs(r, "e", 0, 0, 30, 0));
        const std::string vars = "<style>:root{--w:120px;--pad:calc(var(--w) / 4)} .x{width:var(--w);padding-left:var(--pad)} .y{width:var(--nope, 77px)} .z{--w:10px}</style>";
        const LayoutResult r2 = Lay(vars + B0 + "<div id=a class=x></div><div id=b class=y></div><div class=z><div id=c class=x></div></div>", 400);
        CHECK(BoxIs(r2, "a", 0, 0, 150, 0));   // 120 + 30 padding
        CHECK(BoxIs(r2, "b", 0, 0, 77, 0));
        CHECK(BoxIs(r2, "c", 0, 0, 40, 0));    // --w is 10px inside .z; --pad was worked out at the root (30px)
        const std::string media = "<style>#a{width:100px} @media (max-width:500px){#a{width:30px}} @media screen and (min-width:600px){#a{width:60px}}"
                                  " @media print{#a{width:5px}} @media (400px <= width < 450px){#a{height:7px}}</style>";
        CHECK(BoxIs(Lay(media + B0 + "<div id=a></div>", 420), "a", 0, 0, 30, 7));
        CHECK(BoxIs(Lay(media + B0 + "<div id=a></div>", 550), "a", 0, 0, 100, 0));
        CHECK(BoxIs(Lay(media + B0 + "<div id=a></div>", 800), "a", 0, 0, 60, 0));
    }

    // ---- selectors, ::before / ::after, :hover ----
    {
        const std::string css = "<style>li:first-child{width:10px} li:nth-child(2n+3){width:30px} li:last-child{width:40px}"
                                " li + li{height:1px} .a ~ .b{height:2px} [data-k^=ab]{width:50px} li:not(.a){padding-top:3px}"
                                " p::before{content:'X '} p::after{content:\"!\"} a:hover{width:60px;display:block}</style>";
        const std::string body = "<ul style='margin:0;padding:0'><li id=l1 class=a>1</li><li id=l2>2</li><li id=l3 class=b>3</li><li id=l4 data-k=abc>4</li><li id=l5 class=b>5</li></ul>"
                                 "<p id=p>hi</p><a id=h href=#>h</a>";
        const HtmlDocument d = ParseHtml(css + B0 + body);
        const LayoutResult r = LayoutPage(d, 400, kMeasure);
        const BoxInfo* l1 = ById(r, "l1"); const BoxInfo* l2 = ById(r, "l2"); const BoxInfo* l3 = ById(r, "l3"); const BoxInfo* l4 = ById(r, "l4"); const BoxInfo* l5 = ById(r, "l5");
        CHECK(l1 && l2 && l3 && l4 && l5);
        if (l1 && l2 && l3 && l4 && l5) {
            CHECK(Near(l1->w, 10) && Near(l2->w, 400) && Near(l3->w, 30) && Near(l4->w, 50) && Near(l5->w, 40));
            CHECK(Near(l1->h, L));                     // the first: no "li + li", no :not(.a) padding
            CHECK(Near(l2->h, 1 + 3));                 // height 1px, padding-top 3 (the text overflows)
            CHECK(Near(l3->h, 2 + 3));                 // .a ~ .b is later than li + li
        }
        // the list is 19.2 + 4 + 5 + 4 + 5 = 37.2 high; the paragraph 16 px below it
        const float py = 37.2f + 16 + 1.6f;
        CHECK(TextAt(r, "X ", 0, py) && TextAt(r, "hi", 16, py) && TextAt(r, "!", 32, py));
        // :hover: the element under the pointer (and its ancestors) match, through the layout context
        const Node* link = nullptr;
        std::vector<const Node*> todo{d.root.get()};
        while (!todo.empty()) { const Node* n = todo.back(); todo.pop_back(); if (n->IsElement("a")) link = n; for (const auto& k : n->children) todo.push_back(k.get()); }
        CHECK(r.dynamic);
        LayoutContext ctx;
        ctx.style.hover = link;
        const LayoutResult rh = LayoutPage(d, 400, kMeasure, ImageSizeFn(), &ctx);
        CHECK(ById(rh, "h") && Near(ById(rh, "h")->w, 60));
        CHECK(!ById(r, "h"));   // not hovered: inline, no box of its own
    }

    // ---- drawing: radius, shadow, background image, opacity, visibility ----
    {
        auto img = [](const std::string& src, int& w, int& h) { if (src == "a.png") { w = 10; h = 10; return true; } return false; };
        const LayoutResult r = Lay(B0 + "<div style='width:100px;height:50px;background:#f00 url(a.png) no-repeat center;border-radius:10px;box-shadow:2px 3px 4px #000'></div>", 300, img);
        const DrawCmd* shadow = nullptr; const DrawCmd* fill = nullptr; const DrawCmd* bg = nullptr;
        for (const DrawCmd& c : r.cmds) {
            if (c.kind == DrawCmd::Shadow) shadow = &c;
            if (c.kind == DrawCmd::Fill && c.color == Color{255, 0, 0, 255}) fill = &c;
            if (c.kind == DrawCmd::BgImage) bg = &c;
        }
        CHECK(shadow && Near(shadow->x, 2) && Near(shadow->y, 3) && Near(shadow->w, 100) && Near(shadow->h, 50) && Near(shadow->blur, 4) && Near(shadow->radius, 10));
        CHECK(fill && Near(fill->radius, 10) && Near(fill->h, 50));
        CHECK(bg && Near(bg->ix, 45) && Near(bg->iy, 20) && Near(bg->iw, 10) && !bg->repeatX && !bg->repeatY);
        CHECK(shadow < fill && fill < bg);   // shadow, colour, image
        CHECK(std::find(r.images.begin(), r.images.end(), "a.png") != r.images.end());
        const LayoutResult r2 = Lay(B0 + "<div style='opacity:0.5;background:#00f;height:10px'></div><div style='visibility:hidden;background:#0f0;height:10px'>x</div>", 300);
        CHECK(IndexOfFill(r2, Color{0, 0, 255, 128}) >= 0);
        CHECK(IndexOfFill(r2, Color{0, 255, 0, 255}) < 0 && !TextCmd(r2, "x"));
    }
    TEST_MAIN_RESULT();
}
