// Browser (page engine): layout (engine/include/okay/Browser/Layout.hpp). Box positions and line breaks are checked against values
// worked out by hand from CSS 2.1 with a fixed text measure (every character is half the font size wide, so 8 px at 16 px):
// normal line height is 1.2 x 16 = 19.2 px, the text's em box starts (19.2 - 16) / 2 = 1.6 px below the line top.
#include "test_framework.hpp"
#include "okay/Browser/Layout.hpp"

#include <string>

using namespace okay::browser;

static const FixedMeasure kMeasure(0.5f);

static LayoutResult Lay(const char* html, float width, const ImageSizeFn& img = ImageSizeFn()) {
    const HtmlDocument d = ParseHtml(html);
    return LayoutPage(d, width, kMeasure, img);
}
static bool Dump(const char* html, float width, const std::string& want) {
    const std::string got = DumpLayout(Lay(html, width));
    if (got != want) { std::cerr << "  input: " << html << "\n  --- want ---\n" << want << "  --- got ---\n" << got; return false; }
    return true;
}
static const DrawCmd* FindText(const LayoutResult& r, const std::string& text) {
    for (const DrawCmd& c : r.cmds) if (c.kind == DrawCmd::Text && c.text == text) return &c;
    return nullptr;
}
static const BoxInfo* FindBox(const LayoutResult& r, const std::string& tag, int nth = 0) {
    for (const BoxInfo& b : r.boxes) if (b.tag == tag && nth-- == 0) return &b;
    return nullptr;
}
static int Count(const LayoutResult& r, DrawCmd::Kind k) {
    int n = 0;
    for (const DrawCmd& c : r.cmds) if (c.kind == k) ++n;
    return n;
}

int main() {
    RUN_SUITE("page layout");

    // One paragraph: the paragraph's 16 px margins collapse through body (no border or padding) and end up outside it; html is
    // the root (a new formatting context) and holds them (CSS 2.1 8.3.1).
    CHECK(Dump("<body style='margin:0'><p>Hello world</p>", 800,
               "block html 0,0 800x51\n"
               "block body 0,16 800x19\n"
               "block p 0,16 800x19\n"
               "text 0,18 'Hello world'\n"));

    // The default body margin is 8 px on every side.
    CHECK(Dump("<p style='margin:0'>x</p>", 300,
               "block html 0,0 300x35\n"
               "block body 8,8 284x19\n"
               "block p 8,8 284x19\n"
               "text 8,10 'x'\n"));

    // Line breaking at spaces: "aaa bbb ccc" is 88 px, adding " ddd" would make 120 > 100.
    CHECK(Dump("<body style='margin:0'><p style='margin:0'>aaa  bbb\nccc   ddd</p>", 100,
               "block html 0,0 100x38\n"
               "block body 0,0 100x38\n"
               "block p 0,0 100x38\n"
               "text 0,2 'aaa bbb ccc'\n"
               "text 0,21 'ddd'\n"));

    // A word longer than the line is broken into pieces that fit.
    CHECK(Dump("<body style='margin:0'><div>abcdefghij</div>", 40,
               "block html 0,0 40x38\n"
               "block body 0,0 40x38\n"
               "block div 0,0 40x38\n"
               "text 0,2 'abcde'\n"
               "text 0,21 'fghij'\n"));

    // No break inside "foobar" even though it spans two elements; the bold run is its own text command.
    {
        const LayoutResult r = Lay("<body style='margin:0'>foo<b>bar</b> baz", 48);
        const DrawCmd* foo = FindText(r, "foo");
        const DrawCmd* bar = FindText(r, "bar");
        const DrawCmd* baz = FindText(r, "baz");
        CHECK(foo && bar && baz);
        if (foo && bar && baz) {
            CHECK_NEAR(foo->x, 0.0f, 0.01f); CHECK_NEAR(bar->x, 24.0f, 0.01f); CHECK(bar->font.bold && !foo->font.bold);
            CHECK_NEAR(bar->y, foo->y, 0.01f);
            CHECK_NEAR(baz->x, 0.0f, 0.01f); CHECK_NEAR(baz->y, foo->y + 19.2f, 0.01f);
        }
    }

    // text-align.
    {
        const LayoutResult r = Lay("<body style='margin:0'><p style='text-align:center'>ab</p><p style='text-align:right'>cd</p>", 100);
        CHECK(FindText(r, "ab") && FindText(r, "ab")->x == 42.0f);
        CHECK(FindText(r, "cd") && FindText(r, "cd")->x == 84.0f);
    }

    // Headings: 2em bold, margins .67em.
    {
        const LayoutResult r = Lay("<body style='margin:0'><h1>T</h1>", 400);
        const BoxInfo* h = FindBox(r, "h1");
        CHECK(h != nullptr);
        if (h) { CHECK_NEAR(h->y, 21.44f, 0.01f); CHECK_NEAR(h->h, 38.4f, 0.01f); }
        const DrawCmd* t = FindText(r, "T");
        CHECK(t && t->font.bold && t->font.size == 32.0f);
    }

    // Sibling margins collapse to the larger one.
    {
        const LayoutResult r = Lay("<body style='margin:0'><div style='margin-bottom:20px'>a</div><div style='margin-top:30px'>b</div>", 200);
        const BoxInfo* b = FindBox(r, "div", 1);
        CHECK(b && std::abs(b->y - 49.2f) < 0.01f);
        const LayoutResult n = Lay("<body style='margin:0'><div style='margin-bottom:20px'>a</div><div style='margin-top:-5px'>b</div>", 200);
        const BoxInfo* nb = FindBox(n, "div", 1);
        CHECK(nb && std::abs(nb->y - 34.2f) < 0.01f);   // 19.2 + 20 - 5
    }

    // width, auto margins, padding and border; max-width; borders and backgrounds are drawn.
    {
        const LayoutResult r = Lay("<body style='margin:0'><div style='width:200px;margin:0 auto;padding:10px;border:5px solid red;background:#0f0'>x</div>"
                                   "<div style='max-width:50%;height:30px'>y</div>", 800);
        const BoxInfo* a = FindBox(r, "div", 0);
        const BoxInfo* b = FindBox(r, "div", 1);
        CHECK(a && a->x == 285.0f && a->w == 230.0f);
        if (a) CHECK_NEAR(a->h, 49.2f, 0.01f);
        CHECK(FindText(r, "x") && FindText(r, "x")->x == 300.0f);
        CHECK(b && b->w == 400.0f && b->h == 30.0f);
        int green = 0, red = 0;
        for (const DrawCmd& c : r.cmds) if (c.kind == DrawCmd::Fill) {
            if (c.color == (Color{0, 255, 0, 255}) && c.w == 230.0f) ++green;
            if (c.color == (Color{255, 0, 0, 255})) ++red;
        }
        CHECK(green == 1 && red == 4);
    }

    // Lists: 40 px indent, a disc to the left of the first line, numbers for <ol> (start= and value=).
    {
        const LayoutResult r = Lay("<body style='margin:0'><ul style='margin:0'><li>one<li>two</ul><ol start=3><li>a<li value=9>b<li>c</ol>"
                                   "<ul style='list-style:none'><li>n</ul>", 400);
        const BoxInfo* li = FindBox(r, "li");
        CHECK(li && li->x == 40.0f && li->w == 360.0f);
        CHECK(Count(r, DrawCmd::Disc) == 2);
        for (const DrawCmd& c : r.cmds) if (c.kind == DrawCmd::Disc) { CHECK_NEAR(c.x, 26.0f, 0.01f); CHECK_NEAR(c.w, 6.0f, 0.01f); break; }
        CHECK(FindText(r, "3.") && FindText(r, "3.")->x == 16.0f);
        CHECK(FindText(r, "9.") != nullptr);
        CHECK(FindText(r, "10.") != nullptr);
        CHECK(FindText(r, "one") && FindText(r, "one")->y == FindText(r, "one")->y);
        const LayoutResult nested = Lay("<ul><li>a<ul><li>b<ul><li>c</ul></ul></ul><ol style='list-style-type:lower-alpha'><li>x<li>y</ol>"
                                        "<ol style='list-style-type:upper-roman' start=4><li>z</ol>", 400);
        CHECK(Count(nested, DrawCmd::Disc) == 1 && Count(nested, DrawCmd::Circle) == 1 && Count(nested, DrawCmd::Square) == 1);
        CHECK(FindText(nested, "b.") != nullptr && FindText(nested, "IV.") != nullptr);
    }

    // Images: the decoded size, one given dimension keeps the aspect ratio, the line grows to hold the image.
    {
        const ImageSizeFn img = [](const std::string& src, int& w, int& h) {
            if (src == "a.png") { w = 100; h = 50; return true; }
            return false;
        };
        const LayoutResult r = Lay("<body style='margin:0'><p style='margin:0'><img src=a.png></p><p style='margin:0'><img src=a.png width=50></p>"
                                   "<p style='margin:0'><img src=b.png width=20 height=10><img src=c.png></p>", 800, img);
        int n = 0;
        float ys[4] = {0, 0, 0, 0}, ws[4] = {0, 0, 0, 0}, hs[4] = {0, 0, 0, 0};
        for (const DrawCmd& c : r.cmds) if (c.kind == DrawCmd::Image && n < 4) { ys[n] = c.y; ws[n] = c.w; hs[n] = c.h; ++n; }
        CHECK(n == 2 || n == 3);   // c.png is unknown and has no size: nothing to draw yet
        CHECK(ws[0] == 100.0f && hs[0] == 50.0f && ys[0] == 0.0f);
        CHECK(ws[1] == 50.0f && hs[1] == 25.0f);
        const BoxInfo* p0 = FindBox(r, "p", 0);
        CHECK(p0 && std::abs(p0->h - 54.8f) < 0.01f);   // image ascent 50 + the strut's descent 4.8
        CHECK(r.images.size() == 3 && r.images[0] == "a.png" && r.images[1] == "b.png" && r.images[2] == "c.png");
        const LayoutResult wide = Lay("<body style='margin:0'><img src=a.png>", 60, img);   // max-width: 100% from the default sheet
        for (const DrawCmd& c : wide.cmds) if (c.kind == DrawCmd::Image) { CHECK(c.w == 60.0f && c.h == 30.0f); }
    }

    // <br>, <pre> keeps spaces and line ends and does not wrap.
    CHECK(Dump("<body style='margin:0'><pre style='margin:0'>a  b\nc</pre><div>x<br>y<br><br>z</div>", 20,
               "block html 0,0 20x115\n"
               "block body 0,0 20x115\n"
               "block pre 0,0 20x38\n"
               "text 0,2 'a  b'\n"
               "text 0,21 'c'\n"
               "block div 0,38 20x77\n"
               "text 0,40 'x'\n"
               "text 0,59 'y'\n"
               "text 0,98 'z'\n"));

    // Links: the run's area, LinkAt, links listed once per <a>.
    {
        const LayoutResult r = Lay("<body style='margin:0'><p style='margin:0'><a href='/x'>go</a> on <a href='#t'>t</a></p>", 400);
        CHECK(r.links.size() == 2 && r.links[0] == "/x" && r.links[1] == "#t");
        CHECK(LinkAt(r, 5, 5) == 0);
        CHECK(LinkAt(r, 30, 5) == -1);
        CHECK(LinkAt(r, 50, 5) == 1);
        CHECK(LinkAt(r, 5, 25) == -1);
        const DrawCmd* go = FindText(r, "go");
        CHECK(go && go->link == 0 && go->underline);
    }

    // <hr>: a 2 px tall box (top and bottom border) with .5em margins; hidden elements make no boxes.
    {
        const LayoutResult r = Lay("<body style='margin:0'><hr><p hidden>no</p><div style='display:none'>no</div>", 100);
        const BoxInfo* hr = FindBox(r, "hr");
        CHECK(hr && hr->y == 8.0f && hr->h == 2.0f && hr->w == 100.0f);
        CHECK(FindBox(r, "p") == nullptr && FindBox(r, "div") == nullptr && FindText(r, "no") == nullptr);
    }

    // Tables (CSS 2.1 17.5.2.2, automatic layout): column 1 wants 10 px ("a" + 2 px padding) at most, column 2 at least 10 and
    // at most 218 (27 characters + padding); 200 - 3 x 2 px spacing = 194 lies between the sums (20 and 228), so each column
    // gets its minimum plus its share of the rest in proportion to max - min: 10 and 184. Rows are as tall as their tallest cell.
    {
        const LayoutResult r = Lay("<body style='margin:0'><table><tr><td>a<td>b b b b b b b b b b b b b b<tr><td>c</table>", 200);
        const BoxInfo* t = FindBox(r, "table");
        const BoxInfo* c1 = FindBox(r, "td", 0);
        const BoxInfo* c2 = FindBox(r, "td", 1);
        const BoxInfo* c3 = FindBox(r, "td", 2);
        CHECK(t && t->w == 200.0f);
        CHECK(c1 && c2 && c3);
        if (c1 && c2 && c3) {
            CHECK(c1->x == 2.0f && c1->w == 10.0f && c2->x == 14.0f && c2->w == 184.0f);
            CHECK(c1->y == 2.0f && c2->y == 2.0f);
            CHECK(c1->h == c2->h && c2->h > 40.0f);
            CHECK_NEAR(c3->y, c1->y + c1->h + 2.0f, 0.01f);
        }
    }

    // Embedded things (a future game or script surface) get a box and are not entered: <okay-game> with its size,
    // <canvas> with the standard 300 x 150, fallback content inside them is not laid out.
    {
        const LayoutResult r = Lay("<body style='margin:0'><okay-game src='game.okaypack' width=320 height=200>no game</okay-game>"
                                   "<p style='margin:0'><canvas>fallback</canvas></p>", 800);
        CHECK(r.embeds.size() == 2);
        if (r.embeds.size() == 2) {
            const DrawCmd& g = r.cmds[r.embeds[0]];
            const DrawCmd& c = r.cmds[r.embeds[1]];
            CHECK(g.kind == DrawCmd::Embed && g.tag == "okay-game" && g.text == "game.okaypack" && g.w == 320.0f && g.h == 200.0f);
            CHECK(c.kind == DrawCmd::Embed && c.tag == "canvas" && c.w == 300.0f && c.h == 150.0f);
        }
        CHECK(FindText(r, "no game") == nullptr && FindText(r, "fallback") == nullptr);
    }

    // Anchors, the canvas colour, the page height.
    {
        const LayoutResult r = Lay("<body style='margin:0;background:#123456'><h2 id=top>T</h2><p>x</p>", 300);
        CHECK(r.canvas == (Color{0x12, 0x34, 0x56, 255}));
        bool found = false;
        for (const auto& a : r.anchors) if (a.first == "top") { found = true; CHECK_NEAR(a.second, 19.92f, 0.01f); }
        CHECK(found);
        CHECK(r.height > 60.0f && r.width == 300.0f);
    }

    // A zero or tiny width never loops or crashes.
    {
        const LayoutResult r = Lay("<p>some words here</p><ul><li>x</ul>", 0);
        CHECK(r.height > 0.0f);
    }

    TEST_MAIN_RESULT();
}
