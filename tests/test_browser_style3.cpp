// Browser (page engine): round 3, "pages styled properly" - the cascade (CSS Cascade 4 keywords, CSS Nesting 1, CSS Logical
// Properties 1), text (letter- and word-spacing, justify, text-shadow, small-caps, ::first-letter), boxes (outline, inset shadows,
// border line styles, aspect-ratio, filter), images (srcset, <picture>) and the default sheet (the HTML standard's Rendering
// section: details / summary). Every number is worked out by hand with the fixed text measure (8 px per character at 16 px,
// lines 19.2 px, text 1.6 px below the line top).
#include "BrowserLayoutTestKit.hpp"

#include <string>

using namespace browsertest;

static const std::string B0 = "<body style='margin:0'>";
static int CountFill(const LayoutResult& r, Color c) {
    int n = 0;
    for (const DrawCmd& k : r.cmds) if (k.kind == DrawCmd::Fill && k.color == c) ++n;
    return n;
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
    TEST_MAIN_RESULT();
}
