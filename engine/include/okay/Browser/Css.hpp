#pragma once
// Browser (page engine): style sheets. Reading CSS (<style> blocks, linked sheets and style="" attributes), matching selectors and
// the cascade that gives every element its computed style. Written from CSS Syntax Level 3 (comments, strings, blocks,
// at-rules, declarations and !important), Selectors Level 4 (type, universal, #id, .class, attribute selectors [a] [a=v] [a~=v]
// [a|=v] [a^=v] [a$=v] [a*=v] with the i flag, the descendant, child, next-sibling and subsequent-sibling combinators,
// :first-child :last-child :only-child :nth-child() :nth-last-child() :first-of-type :last-of-type :only-of-type
// :nth-of-type() :nth-last-of-type() :root :empty :link :any-link :visited :hover :focus :focus-within :active :checked
// :disabled :enabled :required :optional :not() :is() :where(), ::before and ::after, specificity; a rule whose selector list
// holds anything else is dropped, as the standard says for an invalid selector list), CSS 2.1 section 6 (cascade order,
// inheritance), CSS Values 4 (em, rem, ex, ch, vw, vh, vmin, vmax, px, pt, pc, in, cm, mm, Q, %, calc(), min(), max(),
// clamp()), CSS Custom Properties 1 (--name and var() with a fallback), Media Queries 4 (media types, width / height /
// orientation / prefers-color-scheme features with min- and max- and the range syntax, and, not, only, commas), CSS Color 4
// (#rgb, #rrggbb, #rgba, #rrggbbaa, rgb()/rgba(), hsl()/hsla(), the named colours, transparent, currentColor) and CSS Fonts 4
// (@font-face: font-family, src url() with format(), font-weight, font-style).
//
// Properties read: display (block, inline, inline-block, flow-root, list-item, flex, inline-flex, grid, inline-grid, table parts,
// contents, none), float, clear, position, top / right / bottom / left, z-index, overflow / overflow-x / overflow-y, box-sizing,
// margin and padding (and their sides), border (and every part of it), border-radius, box-shadow, color, background (colour,
// url() image, a linear / radial / repeating gradient), background-color / -image / -size / -repeat / -position, opacity, visibility,
// font-size, font-weight, font-style, font-family, font, text-align, text-indent, text-transform, line-height, width / height,
// min- and max- width / height, text-decoration, white-space, list-style(-type), vertical-align (middle, top, bottom for inline
// boxes), flex-direction, flex-wrap, flex-flow, flex, flex-grow / -shrink / -basis, order, justify-content, align-items,
// align-self, align-content, gap / row-gap / column-gap, grid-template-columns / -rows (px, %, fr, auto, min-content,
// max-content, minmax(), repeat() with a count or auto-fill / auto-fit), grid-template-areas, grid-template, grid-auto-rows /
// -columns, grid-auto-flow, grid-column / grid-row (-start, -end; numbers, negative numbers, span n, area names), grid-area,
// justify-items, justify-self, place-items, place-self, place-content; round 3: border-spacing, border-collapse, table-layout,
// caption-side, text-decoration-line / -style / -color, text-overflow, object-fit, object-position, transform, transform-origin,
// counter-reset / -increment / -set and counter() / counters() in content, the HTML presentational attributes of tables and cells,
// and :has() (Selectors 4). Everything else (transition, animation ...) is read and ignored.
// Standard library only.
#include "okay/Browser/Html.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace okay::browser {

struct Color {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
    bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const Color& o) const { return !(*this == o); }
};
/// A CSS colour value. False (and `out` unchanged) for anything that is not one. "currentColor" is not handled here.
bool ParseColor(const std::string& text, Color& out);

/// A length after the cascade: em, rem, vw ... are already px; % stays a fraction of the containing block; a calc() that mixes
/// both keeps both (value px + pct %).
struct Length {
    enum Unit { Auto, Px, Percent, None, Calc } unit = Auto;   ///< None: "none" (max-width); Calc: value px + pct %
    float value = 0.0f;
    float pct = 0.0f;
    static Length Px_(float v) { Length l; l.unit = Px; l.value = v; return l; }
    bool IsSet() const { return unit == Px || unit == Percent || unit == Calc; }
    bool HasPercent() const { return unit == Percent || (unit == Calc && pct != 0.0f); }
    /// The value in px for a containing block `base` px wide (Auto and None give `autoValue`).
    float Resolve(float base, float autoValue = 0.0f) const {
        return unit == Px ? value : unit == Percent ? value * base / 100.0f : unit == Calc ? value + pct * base / 100.0f : autoValue;
    }
};

enum class Display {
    Inline, Block, ListItem, None, Table, TableRow, TableCell, TableSection,
    InlineBlock, FlowRoot, Flex, InlineFlex, Grid, InlineGrid,
    TableCaption, TableHeaderGroup, TableFooterGroup   ///< a caption; thead / tfoot (moved to the top / bottom of the table)
};
inline bool IsTableSection(Display d) { return d == Display::TableSection || d == Display::TableHeaderGroup || d == Display::TableFooterGroup; }
/// Laid out inside a line like a word (inline, and the inline-* boxes that are atomic in a line).
inline bool IsInlineLevel(Display d) { return d == Display::Inline || d == Display::InlineBlock || d == Display::InlineFlex || d == Display::InlineGrid; }
enum class TextAlign { Left, Right, Center, Justify };
enum class WhiteSpace { Normal, Pre, NoWrap, PreWrap, PreLine };
enum class ListStyle { Disc, Circle, Square, Decimal, LowerAlpha, UpperAlpha, LowerRoman, UpperRoman, None, DecimalLeadingZero, LowerGreek };
/// A counter value written in a list style (CSS Counter Styles 3 / CSS 2.1 12.6.2): "4", "04", "d", "D", "iv", "IV", "δ"; a
/// marker style (disc ...) gives the bullet character; none gives "".
std::string CounterText(int value, ListStyle style);
/// A list-style-type name ("lower-roman" ...): true and the style, false for a name we do not know.
bool ListStyleFromName(const std::string& name, ListStyle& out);
enum class Float { None, Left, Right };
enum class Clear { None, Left, Right, Both };
enum class Position { Static, Relative, Absolute, Fixed, Sticky };
enum class Overflow { Visible, Hidden, Clip, Scroll, Auto };
enum class TextTransform { None, Upper, Lower, Capitalize };
enum class VAlign { Baseline, Middle, Top, Bottom, TextTop, TextBottom };
enum class FlexDir { Row, RowReverse, Column, ColumnReverse };
enum class FlexWrap { NoWrap, Wrap, WrapReverse };
/// Box alignment values used by justify-content / align-* / justify-* (CSS Box Alignment 3), reduced to what layout does.
enum class Align { Auto, Normal, Stretch, Start, End, Center, Baseline, SpaceBetween, SpaceAround, SpaceEvenly };

/// One grid track size (CSS Grid 1, 7.2): a min and a max sizing function. "100px" is {Fixed 100, Fixed 100}; "1fr" is
/// {Auto, Fr 1}; "auto" is {Auto, Auto}; minmax(a, b) is {a, b}.
struct GridTrack {
    enum Kind { Fixed, Percent, Fr, Auto, MinContent, MaxContent } minKind = Auto, maxKind = Auto;
    float minValue = 0.0f, maxValue = 0.0f;
};
/// A track list: the tracks, and an auto-fill / auto-fit repeat (its tracks repeated as often as they fit) at `repeatAt`.
struct GridTemplate {
    std::vector<GridTrack> tracks;
    std::vector<GridTrack> autoRepeat;   ///< empty: none
    std::size_t repeatAt = 0;            ///< where the repeated tracks go among `tracks`
    bool autoFit = false;                ///< auto-fit: empty repeated tracks collapse
    std::vector<std::vector<std::string>> lineNames;   ///< names of each line (tracks.size() + 1 entries, before repeat expansion)
    bool IsNone() const { return tracks.empty() && autoRepeat.empty(); }
};
/// grid-column / grid-row: start and end lines (CSS Grid 1, 8.3). line 0 = auto; negative counts from the end; span > 0.
struct GridLine {
    int line = 0;
    int span = 0;
    std::string name;   ///< an area or line name ("" = none)
};

/// A CSS gradient (CSS Images 3, section 3): linear-gradient(), radial-gradient() and their repeating- forms.
struct GradientStop {
    Color color;
    Length pos;   ///< Auto: no position given (spread evenly between its neighbours); Px or Percent of the gradient line
};
struct Gradient {
    bool radial = false, repeating = false;
    // linear: the angle in degrees (0 = to top, 90 = to right, 180 = to bottom); or toward a corner
    float angle = 180.0f;
    int cornerX = 0, cornerY = 0;   ///< "to left top" ...: -1 / +1 per axis (0, 0: use `angle`)
    // radial
    bool circle = false;
    enum Extent { ClosestSide, FarthestSide, ClosestCorner, FarthestCorner, Explicit } extent = FarthestCorner;
    Length rx, ry;                  ///< Explicit sizes
    Length posX, posY;              ///< the centre ("at ..."; 50% 50% by default)
    std::vector<GradientStop> stops;
};
/// Read a gradient function ("linear-gradient(to right, red, blue 80%)"); false for anything else or a broken one.
bool ParseGradient(const std::string& text, float em, Gradient& out);
/// The colour of `g` drawn in a w x h box at (px, py) (box coordinates), premultiplied interpolation in sRGB.
Color GradientColorAt(const Gradient& g, float w, float h, float px, float py);

/// A 2D affine transform (CSS Transforms 1): x' = a x + c y + e, y' = b x + d y + f.
struct Transform2D {
    float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    bool IsIdentity() const { return a == 1 && b == 0 && c == 0 && d == 1 && e == 0 && f == 0; }
    Transform2D Then(const Transform2D& o) const;   ///< this first, then o
};
/// Read a transform list (translate(), translateX/Y(), scale(), scaleX/Y(), rotate(), skew(), skewX/Y(), matrix(); % of a
/// translate is of the box: `boxW` x `boxH`). False for none or anything broken.
bool ParseTransform(const std::string& text, float em, float boxW, float boxH, Transform2D& out);

enum class DecoStyle { Solid, Double, Dotted, Dashed, Wavy };
enum class ObjectFit { Fill, Contain, Cover, None, ScaleDown };

struct BoxShadow {
    float x = 0, y = 0, blur = 0, spread = 0;
    Color color{0, 0, 0, 80};
    bool inset = false;
};

/// The custom properties (--name: value) in effect for an element (inherited, shared between elements until one changes).
using CustomProps = std::vector<std::pair<std::string, std::string>>;

struct ComputedStyle {
    Display display = Display::Inline;
    Length margin[4], padding[4];   ///< top, right, bottom, left
    float border[4] = {0, 0, 0, 0};   ///< used widths (0 when the side's style is none)
    Color borderColor[4];
    float radius[4] = {0, 0, 0, 0};   ///< border-radius: top-left, top-right, bottom-right, bottom-left (px)
    Color color;                      ///< black
    Color background{0, 0, 0, 0};     ///< transparent
    std::string backgroundImage;      ///< url() as written ("" = none)
    std::shared_ptr<const Gradient> bgGradient;   ///< a gradient background image (null = none)
    enum BgSize { BgAuto, BgCover, BgContain, BgExplicit } bgSize = BgAuto;
    Length bgW, bgH;                  ///< BgExplicit
    bool bgRepeatX = true, bgRepeatY = true;
    Length bgPosX, bgPosY;            ///< background-position (default 0% 0%)
    std::vector<BoxShadow> shadows;
    float opacity = 1.0f;
    bool visible = true;              ///< visibility (inherited)
    float fontSize = 16.0f;
    bool bold = false, italic = false, mono = false;
    int weight = 400;
    std::string family;               ///< the font-family list as written, lower-cased (for @font-face)
    TextAlign textAlign = TextAlign::Left;
    TextTransform textTransform = TextTransform::None;
    Length textIndent;
    float lineHeight = -1.0f;         ///< px; < 0 means "normal" (LineHeightPx gives 1.2 x font size)
    float lineHeightFactor = -1.0f;   ///< a unitless line-height (inherited as the number, CSS 2.1 10.8.1)
    Length width, maxWidth, height, minWidth, minHeight, maxHeight;
    bool minWidthAuto = true, minHeightAuto = true;   ///< min-width / min-height: auto (flex items use their content)
    bool borderBox = false;           ///< box-sizing: border-box
    bool underline = false, strike = false, overline = false;
    DecoStyle decoStyle = DecoStyle::Solid;
    Color decoColor{0, 0, 0, 0};      ///< text-decoration-color (alpha 0: the text colour)
    bool ellipsis = false;            ///< text-overflow: ellipsis
    ObjectFit objectFit = ObjectFit::Fill;
    Length objectPosX, objectPosY;    ///< object-position (50% 50%)
    std::string transform;            ///< the transform list as written ("" = none; read at layout, when the box size is known)
    Length originX, originY;          ///< transform-origin (50% 50%)
    // tables (CSS 2.1 chapter 17)
    float borderSpacingH = 0.0f, borderSpacingV = 0.0f;   ///< border-spacing (inherited)
    bool borderCollapse = false;      ///< border-collapse: collapse (inherited)
    bool tableFixed = false;          ///< table-layout: fixed
    bool captionBottom = false;       ///< caption-side: bottom (inherited)
    // counters (CSS 2.1 12.4)
    std::vector<std::pair<std::string, int>> counterReset, counterIncrement, counterSet;
    WhiteSpace whiteSpace = WhiteSpace::Normal;
    ListStyle listStyle = ListStyle::Disc;
    VAlign verticalAlign = VAlign::Baseline;
    // positioning (CSS 2.1 chapter 9, CSS Positioned Layout 3)
    Float floating = Float::None;
    Clear clear = Clear::None;
    Position position = Position::Static;
    Length inset[4];                  ///< top, right, bottom, left (auto by default)
    bool zAuto = true;
    int zIndex = 0;
    Overflow overflowX = Overflow::Visible, overflowY = Overflow::Visible;
    // flexbox (CSS Flexible Box Layout 1)
    FlexDir flexDirection = FlexDir::Row;
    FlexWrap flexWrap = FlexWrap::NoWrap;
    float flexGrow = 0.0f, flexShrink = 1.0f;
    Length flexBasis;                 ///< auto
    int order = 0;
    Align justifyContent = Align::Normal, alignItems = Align::Normal, alignSelf = Align::Auto, alignContent = Align::Normal;
    Align justifyItems = Align::Normal, justifySelf = Align::Auto;
    Length rowGap, columnGap;         ///< "normal" = auto = 0
    // grid (CSS Grid Layout 1)
    GridTemplate gridColumns, gridRows;
    std::vector<std::string> gridAreas;   ///< grid-template-areas, one string per row (cells split by spaces)
    GridTrack gridAutoRows, gridAutoColumns;
    bool gridAutoFlowColumn = false, gridDense = false;
    GridLine gridColumnStart, gridColumnEnd, gridRowStart, gridRowEnd;
    // generated content (::before / ::after); counter() / counters() are kept as \x03name\x1fstyle\x04 and
    // \x05name\x1fseparator\x1fstyle\x06 until the layout fills them in (the counters depend on the tree order)
    bool hasContent = false;
    std::string content;
    std::shared_ptr<const CustomProps> vars;

    float LineHeightPx() const {
        if (lineHeightFactor >= 0.0f) return lineHeightFactor * fontSize;
        return lineHeight >= 0.0f ? lineHeight : fontSize * 1.2f;
    }
    bool IsPositioned() const { return position != Position::Static; }
    bool OutOfFlow() const { return position == Position::Absolute || position == Position::Fixed; }
    bool Scrolls() const { return overflowX == Overflow::Scroll || overflowX == Overflow::Auto || overflowY == Overflow::Scroll || overflowY == Overflow::Auto; }
    bool Clips() const { return overflowX != Overflow::Visible || overflowY != Overflow::Visible; }
};

struct Declaration {
    std::string property;   ///< lower-cased (a custom property keeps its case)
    std::string value;      ///< trimmed, without "!important"
    bool important = false;
};

/// One attribute selector: [name], [name=value], [name~=value] ... (op is 0 for presence, else '=', '~', '|', '^', '$', '*').
struct AttrSel {
    std::string name, value;
    char op = 0;
    bool ci = false;   ///< the "i" flag: ASCII case-insensitive
};
struct Selector;
/// A pseudo-class with its argument.
struct PseudoSel {
    enum Kind {
        FirstChild, LastChild, OnlyChild, NthChild, NthLastChild, FirstOfType, LastOfType, OnlyOfType, NthOfType, NthLastOfType,
        Root, Empty, Link, Visited, Hover, Focus, FocusWithin, Active, Checked, Disabled, Enabled, Required, Optional, Not, Is,
        Never, Has
    } kind = Never;
    int a = 0, b = 0;                         ///< An+B
    std::vector<Selector> list;               ///< :not() / :is() / :where(); :has(): relative selectors
    std::vector<char> relative;               ///< :has(): each relative selector's leading combinator (' ', '>', '+', '~')
};

/// One compound selector: [type or *][#id][.class ...][attribute ...][:pseudo-class ...].
struct Compound {
    std::string tag;   ///< "" or "*" = any element
    std::string id;
    std::vector<std::string> classes;
    std::vector<AttrSel> attrs;
    std::vector<PseudoSel> pseudos;
};
struct Selector {
    std::vector<Compound> parts;      ///< left to right
    std::vector<char> combinators;    ///< between parts: ' ' (descendant), '>' (child), '+' (next sibling), '~' (later sibling)
    int specificity = 0;              ///< a * 10000 + b * 100 + c (ids, classes / attributes / pseudo-classes, types)
    int pseudoElement = 0;            ///< 0 = the element itself, 1 = ::before, 2 = ::after, 3 = another (never matches)
    bool dynamic = false;             ///< uses :hover / :focus / :active / :focus-within (the layout is redone when they change)
};

/// A media query list (Media Queries 4), read once and checked against the view's size each time styles are computed.
struct MediaQuery {
    struct Feature { std::string name; char op = '='; float value = 0; std::string word; };   ///< op: '=' '<' '>' 'l' (<=) 'g' (>=) '?' (boolean)
    struct Query { bool negate = false; bool typeOk = true; std::vector<Feature> features; bool never = false; };
    std::vector<Query> queries;   ///< empty: always true
};
struct MediaEnv {
    float width = 1024.0f, height = 768.0f;
    bool dark = false;   ///< prefers-color-scheme: dark
};
bool MediaMatches(const MediaQuery& q, const MediaEnv& env);
MediaQuery ParseMediaQuery(const std::string& text);

struct Rule {
    std::vector<Selector> selectors;
    std::vector<Declaration> declarations;
    std::vector<int> media;   ///< indexes into StyleSheet::media: the @media blocks the rule is inside (all must match)
};
/// An @font-face rule: a family name and where its font file is.
struct FontFace {
    std::string family;   ///< lower-cased, without quotes
    std::string url;      ///< the first url() whose format the browser reads (TrueType / OpenType), as written
    int weight = 400;
    bool italic = false;
};
struct StyleSheet {
    std::vector<Rule> rules;
    std::vector<MediaQuery> media;
    std::vector<FontFace> fontFaces;
    std::vector<std::string> imports;   ///< @import url() as written, in order
    bool hasDynamic = false;            ///< some selector uses :hover / :focus ...
};

struct CssLimits {
    std::size_t maxBytes = 2u << 20;   ///< the rest of a longer sheet is not read
    std::size_t maxRules = 20000;
    std::size_t maxSelectorParts = 32;
    std::size_t maxDeclarations = 256;   ///< per rule
    int maxNesting = 8;                  ///< @media / @supports inside each other
};

StyleSheet ParseStyleSheet(const std::string& css, const CssLimits& limits = CssLimits());
/// The declarations of a style="" attribute (or of the inside of one rule's braces).
std::vector<Declaration> ParseDeclarations(const std::string& text, const CssLimits& limits = CssLimits());
/// Read one selector ("ul li.note > a"); false when it uses anything outside the supported subset.
bool ParseSelector(const std::string& text, Selector& out);
/// Read a comma-separated selector list ("h1, .note a"); false when any part is outside the subset. `maxSpec`: the highest
/// specificity in it.
bool ParseSelectorList(const std::string& text, std::vector<Selector>& out, int& maxSpec);

/// What matching and the cascade need to know beyond the tree: the element under the pointer and the one with the keyboard
/// (their ancestors match :hover / :focus-within), which form controls are checked (Forms.hpp), and the view's size.
struct StyleContext {
    const Node* hover = nullptr;
    const Node* focus = nullptr;
    const Node* active = nullptr;
    /// Whether a checkbox or radio button is checked now (null: its "checked" attribute).
    bool (*isChecked)(const Node& el, const void* user) = nullptr;
    const void* checkedUser = nullptr;
    MediaEnv media;
    float rootFontSize = 16.0f;
};

/// Does `el` (an element) match `sel` (for its pseudo-element `pseudo`: 0 the element, 1 ::before, 2 ::after)?
bool Matches(const Selector& sel, const Node& el, const StyleContext* ctx = nullptr, int pseudo = 0);

/// The built-in sheet every page starts from (the "user agent" style sheet: block elements, heading sizes, list indents,
/// link colour, <pre>, tables, form controls), written for this viewer from the element descriptions in the HTML standard,
/// section 15.
const StyleSheet& DefaultStyleSheet();

/// A style that has `parent`'s inherited properties (colour, font, text, white-space, list style, visibility, custom
/// properties ...) and every other property at its initial value: the start of ComputeStyle, and the style of an anonymous box.
ComputedStyle InheritedStyle(const ComputedStyle& parent);

/// The computed style of element `el` whose parent's style is `parent` (the root's parent is a default ComputedStyle):
/// the default sheet, then `author` sheets in order, then el's style attribute; inherited properties come from `parent`.
/// `pseudo` 1 / 2: the style of el's ::before / ::after (hasContent tells whether it exists).
ComputedStyle ComputeStyle(const Node& el, const ComputedStyle& parent, const std::vector<const StyleSheet*>& author,
                           const StyleContext* ctx = nullptr, int pseudo = 0);

} // namespace okay::browser
