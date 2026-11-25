#pragma once
// Browser (page engine): style sheets. Reading CSS (<style> blocks and style="" attributes), matching selectors and the cascade that
// gives every element its computed style. Written from CSS Syntax Level 3 (comments, strings, blocks, at-rules skipped as a
// whole, declarations and !important), Selectors Level 3 (type, universal, #id, .class, descendant and child combinators,
// specificity; a rule whose selector list holds anything else is dropped, as the standard says for an invalid selector list),
// CSS 2.1 section 6 (cascade order, inheritance, em and % values) and CSS Color Level 3 (#rgb, #rrggbb, #rgba, #rrggbbaa,
// rgb()/rgba(), the basic and common extended colour names, transparent, currentColor).
//
// Properties read: display (block, inline, inline-block (laid out as inline), list-item, table parts, none), margin and
// padding (and their sides), border, border-width / -style / -color (and the four sides), color, background-color and the
// colour of `background`, font-size, font-weight, font-style, font-family (only "monospace" matters), font (size and family),
// text-align, line-height, width, max-width, height, text-decoration (underline, line-through, none), white-space (normal,
// pre, nowrap, pre-wrap, pre-line), list-style-type and list-style. Everything else is read and ignored.
// Standard library only.
#include "okay/Browser/Html.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace okay::browser {

struct Color {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
    bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const Color& o) const { return !(*this == o); }
};
/// A CSS colour value. False (and `out` unchanged) for anything that is not one. "currentColor" is not handled here.
bool ParseColor(const std::string& text, Color& out);

/// A length after the cascade: em is already turned into px; % stays a fraction of the containing block's width.
struct Length {
    enum Unit { Auto, Px, Percent, None } unit = Auto;   ///< None: "none" (max-width)
    float value = 0.0f;
    static Length Px_(float v) { Length l; l.unit = Px; l.value = v; return l; }
    /// The value in px for a containing block `base` px wide (Auto and None give `autoValue`).
    float Resolve(float base, float autoValue = 0.0f) const {
        return unit == Px ? value : unit == Percent ? value * base / 100.0f : autoValue;
    }
};

enum class Display { Inline, Block, ListItem, None, Table, TableRow, TableCell, TableSection };
enum class TextAlign { Left, Right, Center };
enum class WhiteSpace { Normal, Pre, NoWrap, PreWrap, PreLine };
enum class ListStyle { Disc, Circle, Square, Decimal, LowerAlpha, UpperAlpha, LowerRoman, UpperRoman, None };

struct ComputedStyle {
    Display display = Display::Inline;
    Length margin[4], padding[4];   ///< top, right, bottom, left
    float border[4] = {0, 0, 0, 0};   ///< used widths (0 when the side's style is none)
    Color borderColor[4];
    Color color;                      ///< black
    Color background{0, 0, 0, 0};     ///< transparent
    float fontSize = 16.0f;
    bool bold = false, italic = false, mono = false;
    TextAlign textAlign = TextAlign::Left;
    float lineHeight = -1.0f;         ///< px; < 0 means "normal" (LineHeightPx gives 1.2 x font size)
    float lineHeightFactor = -1.0f;   ///< a unitless line-height (inherited as the number, CSS 2.1 10.8.1)
    Length width, maxWidth, height;
    bool underline = false, strike = false;
    WhiteSpace whiteSpace = WhiteSpace::Normal;
    ListStyle listStyle = ListStyle::Disc;

    float LineHeightPx() const {
        if (lineHeightFactor >= 0.0f) return lineHeightFactor * fontSize;
        return lineHeight >= 0.0f ? lineHeight : fontSize * 1.2f;
    }
};

struct Declaration {
    std::string property;   ///< lower-cased
    std::string value;      ///< trimmed, without "!important"
    bool important = false;
};

/// One compound selector: [type or *][#id][.class ...].
struct Compound {
    std::string tag;   ///< "" or "*" = any element
    std::string id;
    std::vector<std::string> classes;
};
struct Selector {
    std::vector<Compound> parts;      ///< left to right
    std::vector<char> combinators;    ///< between parts: ' ' (descendant) or '>' (child); size = parts.size() - 1
    int specificity = 0;              ///< a * 10000 + b * 100 + c (ids, classes, types)
};
struct Rule {
    std::vector<Selector> selectors;
    std::vector<Declaration> declarations;
};
struct StyleSheet {
    std::vector<Rule> rules;
};

struct CssLimits {
    std::size_t maxBytes = 1u << 20;   ///< the rest of a longer sheet is not read
    std::size_t maxRules = 10000;
    std::size_t maxSelectorParts = 32;
    std::size_t maxDeclarations = 256;   ///< per rule
};

StyleSheet ParseStyleSheet(const std::string& css, const CssLimits& limits = CssLimits());
/// The declarations of a style="" attribute (or of the inside of one rule's braces).
std::vector<Declaration> ParseDeclarations(const std::string& text, const CssLimits& limits = CssLimits());
/// Read one selector ("ul li.note > a"); false when it uses anything outside the supported subset.
bool ParseSelector(const std::string& text, Selector& out);
/// Does `el` (an element) match `sel`?
bool Matches(const Selector& sel, const Node& el);

/// The built-in sheet every page starts from (the "user agent" style sheet: block elements, heading sizes, list indents,
/// link colour, <pre>, tables), written for this viewer from the element descriptions in the HTML standard, section 15.
const StyleSheet& DefaultStyleSheet();

/// The computed style of element `el` whose parent's style is `parent` (the root's parent is a default ComputedStyle):
/// the default sheet, then `author` sheets in order, then el's style attribute; inherited properties come from `parent`.
ComputedStyle ComputeStyle(const Node& el, const ComputedStyle& parent, const std::vector<const StyleSheet*>& author);

} // namespace okay::browser
