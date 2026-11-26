// Browser (page engine): round 3, "pages styled properly" - the cascade (CSS Cascade 4 keywords, CSS Nesting 1, CSS Logical
// Properties 1), text (letter- and word-spacing, justify, text-shadow, small-caps, ::first-letter), boxes (outline, inset shadows,
// border line styles, aspect-ratio, filter), images (srcset, <picture>) and the default sheet (the HTML standard's Rendering
// section: details / summary). Every number is worked out by hand with the fixed text measure (8 px per character at 16 px,
// lines 19.2 px, text 1.6 px below the line top).
#include "BrowserLayoutTestKit.hpp"

#include <string>
#include <vector>

using namespace browsertest;

static const std::string B0 = "<body style='margin:0'>";
static int CountFill(const LayoutResult& r, Color c) {
    int n = 0;
    for (const DrawCmd& k : r.cmds) if (k.kind == DrawCmd::Fill && k.color == c) ++n;
    return n;
}
static bool FillAtColor(const LayoutResult& r, float x, float y, Color c) {
    for (const DrawCmd& k : r.cmds) if (k.kind == DrawCmd::Fill && k.color == c && x >= k.x && x < k.x + k.w && y >= k.y && y < k.y + k.h) return true;
    return false;
}
static const DrawCmd* TextColored(const LayoutResult& r, const std::string& t, Color c) {
    for (const DrawCmd& k : r.cmds) if (k.kind == DrawCmd::Text && k.text == t && k.color == c) return &k;
    return nullptr;
}

int main() {
    RUN_SUITE("page styles round 3: cascade, text, boxes, images, default sheet");
    const float L = 19.2f;
    const Color red{255, 0, 0, 255}, blue{0, 0, 255, 255}, lime{0, 255, 0, 255}, black{0, 0, 0, 255};

    // ---- the cascade ----
    {
        // inherit / initial / unset (CSS Cascade 4, 7.3)
        const LayoutResult r = Lay("<body style='margin:0;color:#ff0000'><div style='color:#0000ff'><span style='color:inherit'>a</span>"
                                   "<i style='color:initial'>b</i><b style='color:unset'>c</b><u style='margin-left:5px;margin:unset'>d</u></div>", 300);
        CHECK(TextColored(r, "a", blue) && TextColored(r, "b", black) && TextColored(r, "c", blue));
        CHECK(TextAt(r, "d", 24, 1.6f));   // margin: unset is margin: 0
        const LayoutResult r2 = Lay(B0 + "<div style='border:3px solid #ff0000'><p id=p style='margin:0;border:inherit'>x</p></div>", 300);
        CHECK(BoxIs(r2, "p", 3, 3, 294, L + 6));
    }
    {
        // nesting (CSS Nesting 1): a nested rule without & is a descendant; & is the parent; @media inside a rule
        const LayoutResult r = Lay("<style>.a { color: #ff0000; .b { color: #0000ff } & > i { color: #00ff00 } @media (min-width: 10px) { background: #010203 } }</style>"
                                   "<body style='margin:0'><div class=a>x<span class=b>y</span><i>z</i></div>", 300);
        CHECK(TextColored(r, "x", red) && TextColored(r, "y", blue) && TextColored(r, "z", lime));
        CHECK(CountFill(r, Color{1, 2, 3, 255}) == 1);
    }
    {
        // logical properties (horizontal left-to-right: inline = left / right, block = top / bottom)
        const LayoutResult r = Lay(B0 + "<div id=d style='padding-inline:10px 20px;inline-size:50px;border-inline-start:2px solid #00ff00;margin-block:4px 6px'>d</div><p id=n style='margin:0'>n</p>", 300);
        CHECK(BoxIs(r, "d", 0, 4, 82, L));
        CHECK(TextAt(r, "d", 12, 4 + 1.6f));
        CHECK(BoxIs(r, "n", 0, 4 + L + 6, 300, L));
        CHECK(CountFill(r, lime) == 1);
        const LayoutResult r2 = Lay(B0 + "<div style='position:relative;height:50px'><i id=a style='position:absolute;inset-inline:5px 7px;inset-block-start:3px;display:block;height:4px'></i></div>", 300);
        CHECK(BoxIs(r2, "a", 5, 3, 288, 4));
    }

    // ---- text ----
    {
        // letter-spacing: 2 px after every character (spaces too), one run per character; word-spacing: after spaces
        const LayoutResult r = Lay(B0 + "<p style='margin:0;letter-spacing:2px'>ab cd</p><p style='margin:0;word-spacing:10px'>ab cd</p>", 300);
        CHECK(TextAt(r, "a", 0, 1.6f) && TextAt(r, "b", 10, 1.6f) && TextAt(r, "c", 30, 1.6f));
        CHECK(TextAt(r, "cd", 34, L + 1.6f));
    }
    {
        // small-caps: lower-case letters as capitals at 0.7 of the size, on the same baseline
        const LayoutResult r = Lay(B0 + "<p style='margin:0;font-variant:small-caps'>Ab</p>", 300);
        const DrawCmd* b = TextCmd(r, "B");
        CHECK(TextAt(r, "A", 0, 1.6f) && b && Near(b->x, 8) && Near(b->h, 11.2f) && Near(b->y + b->h * 0.8f, 1.6f + 12.8f));
    }
    {
        // justify: the 12 px left on the first line go into its three spaces; the last line is not stretched
        const LayoutResult r = Lay(B0 + "<p style='margin:0;width:100px;text-align:justify'>aa bb cc dd ee ff gg</p>", 300);
        CHECK(TextAt(r, "bb ", 28, 1.6f) && TextAt(r, "cc ", 56, 1.6f) && TextAt(r, "dd", 84, 1.6f));
        CHECK(TextAt(r, "ee ff gg", 0, L + 1.6f));
    }
    {
        // text-shadow: a copy under the text, moved
        const LayoutResult r = Lay(B0 + "<p style='margin:0;text-shadow:1px 2px #ff0000'>sh</p>", 300);
        const DrawCmd* sh = TextColored(r, "sh", red);
        const DrawCmd* tx = TextColored(r, "sh", black);
        CHECK(sh && tx && sh < tx && Near(sh->x, 1) && Near(sh->y, 3.6f));
    }
    {
        // ::first-letter as a drop cap and ::marker's colour
        const LayoutResult r = Lay("<style>p::first-letter{color:#00ff00;font-size:32px} li::marker{color:#ff0000}</style>"
                                   "<body style='margin:0'><p style='margin:0'>Hello</p><ol style='margin:0'><li>x</ol>", 300);
        const DrawCmd* h = TextColored(r, "H", lime);
        CHECK(h && Near(h->h, 32) && TextCmd(r, "ello") != nullptr);
        CHECK(TextColored(r, "1.", red) != nullptr);
    }

    // ---- boxes ----
    {
        // outline: outside the border box, takes no room
        const LayoutResult r = Lay(B0 + "<div id=a style='margin:10px;width:50px;height:20px;outline:2px solid #ff0000;outline-offset:1px'></div><div id=b style='height:5px'></div>", 300);
        CHECK(BoxIs(r, "a", 10, 10, 50, 20));
        CHECK(BoxIs(r, "b", 0, 40, 300, 5));
        CHECK(CountFill(r, red) == 4 && FillAtColor(r, 7.5f, 15, red) && FillAtColor(r, 61.5f, 15, red) && !FillAtColor(r, 30, 15, red));
    }
    {
        // dashed and dotted borders are pieces; a double border is two lines
        const LayoutResult r = Lay(B0 + "<div style='width:60px;height:10px;border-top:2px dashed #ff0000'></div>"
                                   "<div style='width:60px;height:10px;border-top:2px dotted #0000ff'></div>"
                                   "<div style='width:60px;height:10px;border-top:6px double #00ff00'></div>", 300);
        CHECK(CountFill(r, red) == 6);    // 60 / (6 + 4)
        CHECK(CountFill(r, blue) == 15);  // 60 / (2 + 2)
        CHECK(CountFill(r, lime) == 2);
    }
    {
        // an inset shadow: bands inside the padding box (here 4 px all round)
        const LayoutResult r = Lay(B0 + "<div style='width:50px;height:30px;box-shadow:inset 0 0 0 4px #ff0000'></div>", 300);
        CHECK(FillAtColor(r, 2, 15, red) && FillAtColor(r, 25, 2, red) && !FillAtColor(r, 25, 15, red));
    }
    {
        // aspect-ratio: the height from the width; taller content still makes it taller
        const LayoutResult r = Lay(B0 + "<div id=a style='width:160px;aspect-ratio:16 / 9'></div><div id=b style='width:16px;aspect-ratio:4'>a b c</div>", 300);
        CHECK(BoxIs(r, "a", 0, 0, 160, 90));
        CHECK(BoxIs(r, "b", 0, 90, 16, 3 * L));
    }
    {
        // filter: grayscale(1) and brightness(0.5) change the box's colours
        const LayoutResult r = Lay(B0 + "<div style='height:10px;background:#ff0000;filter:grayscale(1)'></div><div style='height:10px;background:#ff0000;filter:brightness(50%)'></div>", 300);
        CHECK(CountFill(r, Color{54, 54, 54, 255}) == 1);
        CHECK(CountFill(r, Color{128, 0, 0, 255}) == 1);
    }


    // ---- images: srcset and <picture> (the HTML standard, 4.8.4.3) ----
    {
        const LayoutResult r = Lay(B0 + "<img id=a src=small.png srcset='a1.png 1x, a2.png 2x' width=10 height=10>"
                                   "<img src=s.png srcset='w200.png 200w, w800.png 800w, w1600.png 1600w' width=10 height=10>"
                                   "<picture><source media='(min-width: 2000px)' srcset=huge.png><source type=image/avif srcset=x.avif>"
                                   "<source srcset=pic.png><img src=fallback.png width=10 height=10></picture>"
                                   "<picture><source type=image/webp srcset=y.webp><img src=y.jpg width=10 height=10></picture>"
                                   "<picture><source type=image/svg+xml srcset=z.svg><img src=z.png width=10 height=10></picture>", 600);
        std::vector<std::string> srcs;
        for (const DrawCmd& c : r.cmds) if (c.kind == DrawCmd::Image) srcs.push_back(c.text);
        // a type we cannot decode (AVIF) is skipped; WebP and SVG are read by our own decoders (Core/ImageDecode)
        CHECK(srcs.size() == 5 && srcs[0] == "a1.png" && srcs[1] == "w800.png" && srcs[2] == "pic.png" && srcs[3] == "y.webp" && srcs[4] == "z.svg");
        bool listed = false;
        for (const std::string& i : r.images) if (i == "w800.png") listed = true;
        CHECK(listed);   // the host fetches the chosen one
    }

    // ---- the default sheet (the HTML standard, 15.3) ----
    {
        const LayoutResult r = Lay(B0 + "<details><summary>More</summary><p>hidden</p></details><details open><summary>Open</summary><p>shown</p></details><q>x</q><code>c</code>", 300);
        CHECK(TextCmd(r, "\xE2\x96\xB8\xC2\xA0") != nullptr && TextCmd(r, "\xE2\x96\xBE\xC2\xA0") != nullptr);
        CHECK(TextCmd(r, "hidden") == nullptr && TextCmd(r, "shown") != nullptr);
        CHECK(TextCmd(r, "\xE2\x80\x9C") != nullptr && TextCmd(r, "\xE2\x80\x9D") != nullptr);
        const DrawCmd* c = TextCmd(r, "c");
        CHECK(c && Near(c->h, 13));   // monospace at 13 px
    }

    TEST_MAIN_RESULT();
}
