// Browser (page engine): style sheets (engine/include/okay/Browser/Css.hpp): reading sheets and declarations, selectors and their
// specificity, colours, and the cascade (default sheet < page sheets by specificity and order < style attribute,
// !important, inheritance, em and %).
#include "test_framework.hpp"
#include "okay/Browser/Css.hpp"

#include <string>
#include <vector>

using namespace okay::browser;

// The element at a path of child-element indexes under <body>.
static const Node* El(const HtmlDocument& d, std::vector<int> path) {
    const Node* n = d.Body();
    for (int want : path) {
        int k = 0;
        const Node* next = nullptr;
        for (const auto& c : n->children)
            if (c->kind == NodeKind::Element && k++ == want) { next = c.get(); break; }
        if (!next) return nullptr;
        n = next;
    }
    return n;
}

// Computed styles down a path from <html>.
static ComputedStyle StyleOf(const HtmlDocument& d, const Node* target, const std::vector<const StyleSheet*>& sheets) {
    std::vector<const Node*> chain;
    for (const Node* n = target; n && n->kind == NodeKind::Element; n = n->parent) chain.insert(chain.begin(), n);
    ComputedStyle s;
    for (const Node* n : chain) s = ComputeStyle(*n, s, sheets);
    return s;
}

int main() {
    RUN_SUITE("page css");

    // Colours.
    {
        Color c;
        CHECK(ParseColor("#f80", c) && c == (Color{255, 136, 0, 255}));
        CHECK(ParseColor("#FF8800", c) && c == (Color{255, 136, 0, 255}));
        CHECK(ParseColor("#ff880080", c) && c == (Color{255, 136, 0, 128}));
        CHECK(ParseColor("#f808", c) && c == (Color{255, 136, 0, 136}));
        CHECK(ParseColor("rgb(1, 2, 3)", c) && c == (Color{1, 2, 3, 255}));
        CHECK(ParseColor("rgba(10,20,30,0.5)", c) && c == (Color{10, 20, 30, 128}));
        CHECK(ParseColor("rgb(100%, 0%, 50%)", c) && c == (Color{255, 0, 128, 255}));
        CHECK(ParseColor("rgb(1 2 3 / 50%)", c) && c == (Color{1, 2, 3, 128}));
        CHECK(ParseColor("Red", c) && c == (Color{255, 0, 0, 255}));
        CHECK(ParseColor("navy", c) && c == (Color{0, 0, 128, 255}));
        CHECK(ParseColor("transparent", c) && c.a == 0);
        Color keep{9, 9, 9, 9};
        CHECK(!ParseColor("#12", keep) && keep == (Color{9, 9, 9, 9}));
        CHECK(!ParseColor("notacolour", keep));
        CHECK(!ParseColor("rgb(1,2)", keep));
    }

    // Selectors and specificity.
    {
        Selector s;
        CHECK(ParseSelector("p", s) && s.parts.size() == 1 && s.specificity == 1);
        CHECK(ParseSelector("ul li.note > a", s) && s.parts.size() == 3 && s.combinators.size() == 2 &&
              s.combinators[0] == ' ' && s.combinators[1] == '>' && s.specificity == 103);
        CHECK(ParseSelector("#main .a.b", s) && s.specificity == 10200);
        CHECK(ParseSelector("*", s) && s.specificity == 0);
        CHECK(ParseSelector("div>p", s) && s.parts.size() == 2 && s.combinators[0] == '>');
        // round 1 of the browser program reads these (more in test_browser_layout2.cpp); still refused: unknown pseudo-classes
        // and pseudo-elements, and two combinators in a row
        CHECK(ParseSelector("a:hover", s) && s.dynamic && s.specificity == 101);
        CHECK(ParseSelector("input[type=text]", s) && s.parts[0].attrs.size() == 1 && s.specificity == 101);
        CHECK(ParseSelector("a + b", s) && s.combinators[0] == '+');
        CHECK(!ParseSelector("a:frobnicate", s));
        CHECK(!ParseSelector("p::frob", s));
        CHECK(!ParseSelector("a + + b", s));
        CHECK(!ParseSelector("", s));
        CHECK(!ParseSelector("p >", s));
    }

    // Sheets: comments, @media kept with its condition, @import listed, an invalid selector list drops the whole rule, bad
    // declarations are skipped, !important, strings with braces.
    {
        const StyleSheet sh = ParseStyleSheet(
            "/* c { } */ @media screen { p { color: red } } @import 'x.css';\n"
            "h1, h2 { color: blue; margin: 0 }\n"
            "a, a:frobnicate { color: green }\n"
            "p { color: red !important; ; bogus ; content: \"}\"; font-size: 12px }\n"
            "div { color: \n");
        CHECK(sh.rules.size() == 3 && sh.imports.size() == 1 && sh.imports[0] == "x.css");
        if (sh.rules.size() == 3) {
            CHECK(sh.rules[0].media.size() == 1 && sh.media.size() == 1);   // p inside @media screen
            CHECK(sh.rules[1].selectors.size() == 2 && sh.rules[1].declarations.size() == 2 && sh.rules[1].media.empty());
            const auto& d = sh.rules[2].declarations;
            CHECK(d.size() == 3);
            if (d.size() == 3) {
                CHECK(d[0].property == "color" && d[0].value == "red" && d[0].important);
                CHECK(d[1].property == "content" && d[1].value == "\"}\"");
                CHECK(d[2].property == "font-size" && d[2].value == "12px" && !d[2].important);
            }
        }
        const auto decls = ParseDeclarations("COLOR: Red; margin:1px 2px");
        CHECK(decls.size() == 2 && decls[0].property == "color" && decls[0].value == "Red");
    }

    // Matching.
    {
        const HtmlDocument d = ParseHtml("<div id=main class='x y'><ul><li class=note><a href=#>t</a></li></ul></div><p class=x>q</p>");
        const Node* a = El(d, {0, 0, 0, 0});
        CHECK(a && a->name == "a");
        Selector s;
        if (a) {
            CHECK(ParseSelector("a", s) && Matches(s, *a));
            CHECK(ParseSelector("li.note > a", s) && Matches(s, *a));
            CHECK(ParseSelector("#main a", s) && Matches(s, *a));
            CHECK(ParseSelector(".x.y li a", s) && Matches(s, *a));
            CHECK(ParseSelector("div > a", s) && !Matches(s, *a));
            CHECK(ParseSelector(".z a", s) && !Matches(s, *a));
            CHECK(ParseSelector("ul > li > a", s) && Matches(s, *a));
            CHECK(ParseSelector("body ul a", s) && Matches(s, *a));
        }
        const Node* p = El(d, {1});
        CHECK(p && ParseSelector(".x", s) && Matches(s, *p) && ParseSelector("div.x", s) && !Matches(s, *p));
    }

    // The cascade.
    {
        const HtmlDocument d = ParseHtml(
            "<div class=box id=b style='color: rgb(0,128,0); padding: 2em'><p class=lead>text <em>em</em></p><h1>H</h1><a href=x>l</a></div>");
        const StyleSheet sheet = ParseStyleSheet(
            "#b { color: red; font-size: 20px; margin: 10px auto; width: 50%; border: 2px solid #00f }\n"
            ".box { color: blue; background: #ff0 }\n"
            "p { color: purple !important; line-height: 1.5; text-align: center }\n"
            "div p.lead { font-size: 1.5em; text-decoration: underline }\n"
            "p { font-size: 8px }\n"
            "h1 { font-size: 200% }\n");
        const std::vector<const StyleSheet*> sheets = {&sheet};
        const Node* div = El(d, {0});
        const ComputedStyle ds = StyleOf(d, div, sheets);
        CHECK(ds.display == Display::Block);
        CHECK(ds.color == (Color{0, 128, 0, 255}));       // style attribute beats #id
        CHECK(ds.background == (Color{255, 255, 0, 255}));
        CHECK_NEAR(ds.fontSize, 20.0f, 0.01f);
        CHECK(ds.padding[0].unit == Length::Px);
        CHECK_NEAR(ds.padding[0].value, 40.0f, 0.01f);    // 2em of 20px
        CHECK(ds.margin[1].unit == Length::Auto && ds.margin[3].unit == Length::Auto);
        CHECK_NEAR(ds.margin[0].value, 10.0f, 0.01f);
        CHECK(ds.width.unit == Length::Percent && ds.width.value == 50.0f);
        CHECK_NEAR(ds.border[2], 2.0f, 0.01f);
        CHECK(ds.borderColor[0] == (Color{0, 0, 255, 255}));

        const Node* p = El(d, {0, 0});
        const ComputedStyle ps = StyleOf(d, p, sheets);
        CHECK(ps.color == (Color{128, 0, 128, 255}));     // !important beats the style attribute's inherited value and #id
        CHECK_NEAR(ps.fontSize, 30.0f, 0.01f);            // 1.5em of the div's 20px; specificity beats the later "p"
        CHECK_NEAR(ps.LineHeightPx(), 45.0f, 0.01f);      // unitless 1.5
        CHECK(ps.textAlign == TextAlign::Center);
        CHECK(ps.underline);
        CHECK_NEAR(ps.margin[0].value, 30.0f, 0.01f);     // default sheet: p { margin: 1em 0 } at its own 30px

        const Node* em = El(d, {0, 0, 0});
        const ComputedStyle es = StyleOf(d, em, sheets);
        CHECK(es.italic && es.color == ps.color);          // inherited
        CHECK_NEAR(es.fontSize, 30.0f, 0.01f);
        CHECK_NEAR(es.LineHeightPx(), 45.0f, 0.01f);       // the number is inherited, not the px value

        const Node* h1 = El(d, {0, 1});
        const ComputedStyle hs = StyleOf(d, h1, sheets);
        CHECK(hs.bold);
        CHECK_NEAR(hs.fontSize, 40.0f, 0.01f);             // 200% of 20px beats the default 2em (same result here)
        CHECK(hs.color == (Color{0, 128, 0, 255}));

        const Node* a = El(d, {0, 2});
        const ComputedStyle as = StyleOf(d, a, sheets);
        CHECK(as.underline && as.color == (Color{0, 0, 238, 255}));   // the default link look
        CHECK(as.display == Display::Inline);
    }

    // Default sheet bits: hidden things, lists, pre, the hidden attribute, display: none from a page sheet.
    {
        const HtmlDocument d = ParseHtml("<ul><li>a</li></ul><ol><li>1</li></ol><pre>x</pre><p hidden>h</p><code>c</code><span class=gone>g</span>");
        const StyleSheet sheet = ParseStyleSheet(".gone { display: none } ol { list-style: upper-roman inside }");
        const std::vector<const StyleSheet*> sheets = {&sheet};
        CHECK(StyleOf(d, El(d, {0, 0}), sheets).display == Display::ListItem);
        CHECK(StyleOf(d, El(d, {0, 0}), sheets).listStyle == ListStyle::Disc);
        CHECK(StyleOf(d, El(d, {1, 0}), sheets).listStyle == ListStyle::UpperRoman);
        CHECK(StyleOf(d, El(d, {0}), sheets).padding[3].value == 40.0f);
        const ComputedStyle pre = StyleOf(d, El(d, {2}), sheets);
        CHECK(pre.whiteSpace == WhiteSpace::Pre && pre.mono && pre.display == Display::Block);
        CHECK(StyleOf(d, El(d, {3}), sheets).display == Display::None);
        CHECK(StyleOf(d, El(d, {4}), sheets).mono);
        CHECK(StyleOf(d, El(d, {5}), sheets).display == Display::None);
        const Node* head = d.Html()->children[0].get();
        CHECK(head->name == "head" && ComputeStyle(*head, ComputedStyle(), sheets).display == Display::None);
    }

    // Bad values are ignored (the earlier value stays); border styles; keywords.
    {
        const HtmlDocument d = ParseHtml("<div style='color: blue; color: nonsense; margin: -5px; width: banana; font-size: large; "
                                         "font-weight: 700; border-width: 3px; border-top-style: dashed; max-width: none'>x</div>");
        const ComputedStyle s = StyleOf(d, El(d, {0}), {});
        CHECK(s.color == (Color{0, 0, 255, 255}));
        CHECK_NEAR(s.margin[0].value, -5.0f, 0.01f);
        CHECK(s.width.unit == Length::Auto);
        CHECK_NEAR(s.fontSize, 18.0f, 0.01f);
        CHECK(s.bold);
        CHECK_NEAR(s.border[0], 3.0f, 0.01f);   // dashed is drawn as solid
        CHECK_NEAR(s.border[1], 0.0f, 0.01f);   // no style: no border
        CHECK(s.maxWidth.unit == Length::None);
    }

    TEST_MAIN_RESULT();
}
