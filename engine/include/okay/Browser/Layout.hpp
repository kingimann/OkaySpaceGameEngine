#pragma once
// Browser (page engine): layout. Places the styled page in a column of a given width and gives back a flat list of things to draw
// (filled and rounded rectangles, borders, shadows, text runs, images, background images, list markers, form controls) plus the
// clickable link areas, the clip regions and the scrollable areas. Written from:
//   * CSS 2.1 chapters 8 (box model), 9 (visual formatting: block and inline formatting contexts, anonymous block boxes, floats
//     and clear (9.5), relative, absolute and fixed positioning (9.3, 9.6), z-index and the painting order (9.9, appendix E,
//     simplified)), 10 (widths and heights, min- and max-, shrink-to-fit widths 10.3.5 / 10.3.7, absolutely positioned
//     widths and heights 10.3.7 / 10.6.4, line height and baselines), 11 (overflow and clipping), 12 (list markers),
//     16 (text-align, text-indent, text-transform, white-space, text-decoration);
//   * CSS Box Sizing 3 (box-sizing, min-content / max-content sizes), CSS Overflow 3 (scroll containers);
//   * CSS Flexible Box Layout 1 section 9 (the flex layout algorithm: base sizes, line breaking, resolving flexible lengths
//     with min / max violations, cross sizes, stretch, justify-content, align-items / align-self, align-content, gaps,
//     order, the reverse directions);
//   * CSS Grid Layout 1 (explicit and implicit tracks, line numbers and names, spans, grid-template-areas, auto-placement
//     (sparse and dense, row and column flow), the track sizing algorithm for fixed, percentage, auto, min-content,
//     max-content, minmax() and fr tracks (simplified, 11.3-11.8), gaps, justify / align items and self);
//   * CSS Backgrounds 3 (background images with size, position and repeat, border-radius, box-shadow).
//   * CSS 2.1 chapter 17 (tables: anonymous table parts, row groups with thead first and tfoot last, captions on top or bottom,
//     colspan and rowspan, the automatic (17.5.2.2) and fixed (17.5.2.1) column widths, row heights, vertical-align in cells,
//     row and row-group backgrounds, border-spacing, border-collapse) and 8.3.1 (margins collapsing between siblings, through a
//     parent's top and bottom, and through empty boxes);
//   * CSS Images 3 (linear, radial and repeating gradients, drawn as bands of Fill commands; object-fit and object-position),
//     CSS Text Decoration 3 (underline / overline / line-through in solid, double, dotted, dashed and wavy, in their own colour),
//     CSS Overflow 3 (text-overflow: ellipsis), CSS Transforms 1 (translate and scale folded into the commands; rotate and skew
//     kept in DrawCmd::xform for the host), CSS 2.1 12.4 (counters in generated content).
//
// What is simplified (said here so nobody is surprised): inline boxes draw no borders, padding or backgrounds of their own; a float
// inside a line starts at the line's top and lines wrap around the floats' bands; z-index orders whole positioned boxes (every
// positioned box is its own layer); justified text is laid out as left-aligned; sticky is relative; a table's caption is inside
// the table's border; in the collapsing border model a cell's left and top borders give way to its neighbour's (no width / style
// contest); gradient bands ignore border-radius; transitions and animations show their final state at once.
//
// The text is measured through TextMeasure, so the same layout runs against the real font in the app and against a fixed
// measure in the tests. Standard library only; no drawing.
#include "okay/Browser/Css.hpp"
#include "okay/Browser/Html.hpp"

#include <functional>
#include <string>
#include <vector>

namespace okay::browser {

struct FontSpec {
    float size = 16.0f;
    bool bold = false, italic = false, mono = false;
    int weight = 400;
    std::string family;   ///< the font-family list, lower-cased ("" = the default font); a host with web fonts picks by it
};

/// Width of UTF-8 text in px.
class TextMeasure {
public:
    virtual ~TextMeasure() = default;
    virtual float Width(const std::string& utf8, const FontSpec& font) const = 0;
};

/// A fixed measure for tests and tools: every code point is `factor` x the font size wide (0.5 by default).
class FixedMeasure : public TextMeasure {
public:
    explicit FixedMeasure(float factor = 0.5f) : m_factor(factor) {}
    float Width(const std::string& utf8, const FontSpec& font) const override;
private:
    float m_factor;
};

/// The size of an image the host has decoded: true and the size in px, or false while it is not (yet) known.
using ImageSizeFn = std::function<bool(const std::string& src, int& w, int& h)>;

struct DrawCmd {
    /// Embed: the box of an embedded thing that the page engine does not run itself (<canvas>, <embed>, <object>,
    /// <iframe>, <video>, and <okay-game>, the element an OkaySpace web game will use): its width / height attributes or CSS,
    /// else 300 x 150 as the HTML standard gives a canvas; the host draws it (a Play button for a game).
    /// Border: a rounded border line `thick` px wide (border-radius with one width and colour on every side).
    /// Shadow: a box shadow (x, y, w, h is the shadow's box before blur; `blur` px soft edge; `radius` its corners).
    /// BgImage: a background image (`text` = its address as written) tiled from (ix, iy) in tiles iw x ih, inside the area
    /// (x, y, w, h); repeatX / repeatY say whether it repeats.
    /// Control: a form control's box (`control` = index into LayoutResult::controls); the host draws and runs it.
    enum Kind { Fill, Text, Image, Disc, Circle, Square, Embed, Border, Shadow, BgImage, Control } kind = Fill;
    float x = 0, y = 0, w = 0, h = 0;   ///< Text: (x, y) is the top-left of the em box, h the font size
    Color color;
    std::string text;    ///< Text: the run; Image, BgImage and Embed: the src (or data) attribute as written
    std::string tag;     ///< Embed: the element name
    FontSpec font;
    bool underline = false, strike = false;
    int link = -1;       ///< index into LayoutResult::links, or -1
    float radius = 0.0f; ///< Fill, Border, Shadow: corner radius
    float thick = 0.0f;  ///< Border: line width
    float blur = 0.0f;   ///< Shadow
    float ix = 0, iy = 0, iw = 0, ih = 0;   ///< BgImage: the first tile
    bool repeatX = false, repeatY = false;  ///< BgImage
    int control = -1;    ///< Control
    int clip = -1;       ///< index into LayoutResult::clips: drawn only inside it (and its parents), moved by their scrolling
    bool fixed = false;  ///< position: fixed: drawn at its place in the view, not moved by the page's scroll
    /// A CSS transform the layout could not fold into the position and size (rotate, skew, a mirror): the host draws the command
    /// through it (page coordinates, before scrolling and zoom: a point p of the command is drawn at xform(p)). Identity for
    /// everything else (translate and positive scale are applied to x, y, w, h and the font size already).
    Transform2D xform;
    /// The node it was made from: Text, the text node; Image and Embed, the element (null for everything else). The page's
    /// scripts read element boxes through it (getBoundingClientRect of an inline element: the union of its runs).
    const Node* node = nullptr;
};

struct LinkArea {
    float x = 0, y = 0, w = 0, h = 0;
    int link = -1;
    int clip = -1;
    bool fixed = false;
    int layer = 0;       ///< paint order: a higher layer is on top (LinkAt takes the topmost)
};

/// A clip region: a box whose overflow is not visible (its padding box). Things inside are clipped to it and to its parents.
struct ClipRect {
    float x = 0, y = 0, w = 0, h = 0;
    int parent = -1;
    int scroller = -1;   ///< index into LayoutResult::scrollers when this box scrolls (overflow: scroll / auto)
    bool fixed = false;
};
/// A scroll container (overflow: scroll / auto): its view (the padding box) and how big its content is.
struct Scroller {
    float x = 0, y = 0, w = 0, h = 0;
    float contentW = 0, contentH = 0;   ///< the content's extent from the box's top-left (scrollable when larger than w / h)
    int clip = -1;
    bool fixed = false;
};
/// A form control on the page (Forms.hpp keeps its value), or a <button>: the element and where its box is.
struct ControlBox {
    const Node* node = nullptr;
    float x = 0, y = 0, w = 0, h = 0;   ///< the border box
    int clip = -1;
    bool fixed = false;
    FontSpec font;
    Color color;
};

/// One block-level box, for tests and tools.
struct BoxInfo {
    std::string tag;   ///< the element name, or "(anonymous)"
    int depth = 0;
    float x = 0, y = 0, w = 0, h = 0;   ///< the border box
    std::size_t firstCmd = 0;           ///< the index in cmds of the first thing drawn at or after this box's start
    const Node* node = nullptr;         ///< the element (null for an anonymous box)
    bool fixed = false;                 ///< position: fixed (or inside such a box): its place is in the view, not the page
    float border[4] = {0, 0, 0, 0};     ///< the used border widths: top, right, bottom, left
};

struct LayoutResult {
    std::vector<DrawCmd> cmds;            ///< in painting order
    std::vector<LinkArea> areas;          ///< where each link run is, for clicks
    std::vector<std::string> links;       ///< the href of each link, as written ("" for a <label>)
    std::vector<const Node*> linkNodes;   ///< the element of each link: its <a>, or a <label> (clicking it works its control)
    std::vector<BoxInfo> boxes;           ///< block boxes in tree order
    std::vector<std::string> images;      ///< the src of every <img> and background image, as written, once each
    std::vector<int> embeds;              ///< indexes into cmds of the Embed boxes, in page order
    std::vector<std::pair<std::string, float>> anchors;   ///< id (or <a name>) and the top of its box
    std::vector<ClipRect> clips;
    std::vector<Scroller> scrollers;
    std::vector<ControlBox> controls;
    Color canvas{255, 255, 255, 255};     ///< the page background (from <html> or else <body>, CSS 2.1 14.2)
    float width = 0, height = 0;          ///< the whole page
    bool dynamic = false;                 ///< the page's styles use :hover / :focus (the host lays out again when they change)
};

/// What the layout needs beyond the document: the view's height (vh, fixed boxes), the parsed style sheets (a tab keeps them,
/// so they are not read again for every layout), and the state :hover / :focus / :checked match against.
struct LayoutContext {
    float viewportHeight = 0.0f;                       ///< 0: three quarters of the width
    const std::vector<const StyleSheet*>* sheets = nullptr;   ///< null: the document's <style> elements are read now
    StyleContext style;                                ///< hover, focus, checked (its media size is filled in here)
    /// The text a form control shows (its value), when the host keeps one (Forms.hpp); null: the element's own value.
    std::function<std::string(const Node& control)> controlText;
};

/// Lay out `doc` in a viewport `width` px wide.
LayoutResult LayoutPage(const HtmlDocument& doc, float width, const TextMeasure& measure, const ImageSizeFn& imageSize = ImageSizeFn(),
                        const LayoutContext* context = nullptr);

/// The block boxes and text runs as text, one per line: "block <tag> x,y wxh" and "text x,y 'run'" (whole pixels).
std::string DumpLayout(const LayoutResult& r);

/// Scroll offsets of the scrollable areas (index = LayoutResult::scrollers), as the view keeps them.
using ScrollOffsets = std::vector<std::pair<float, float>>;
/// For something inside clip region `clip`: how far the scroll containers around it move it (dx, dy) and the visible rectangle
/// (x0, y0)-(x1, y1) in unscrolled page coordinates of the view. False when nothing of it can be seen.
bool ClipAndScroll(const LayoutResult& r, int clip, const ScrollOffsets* offsets, float& dx, float& dy, float& cx0, float& cy0,
                   float& cx1, float& cy1);

/// The link under (x, y) in page coordinates: its index into links, or -1. With scrolling areas, `offsets` are theirs and
/// `pageScrollY` the page's (fixed links are at view positions).
int LinkAt(const LayoutResult& r, float x, float y, const ScrollOffsets* offsets = nullptr, float pageScrollY = 0.0f);
/// The innermost scroll container under (x, y) (page coordinates), or -1.
int ScrollerAt(const LayoutResult& r, float x, float y, const ScrollOffsets* offsets = nullptr, float pageScrollY = 0.0f);
/// The form control under (x, y), or -1.
int ControlAt(const LayoutResult& r, float x, float y, const ScrollOffsets* offsets = nullptr, float pageScrollY = 0.0f);

/// One place where find-on-page found the text: the text run (index into cmds) and the found part's box in page coordinates.
struct FindHit {
    int cmd = -1;
    float x = 0, y = 0, w = 0, h = 0;
};
/// Every place `query` is in the page's text, in painting order, without regard to the case of ASCII letters (Ctrl+F). Found
/// inside one text run at a time (a run is a line's piece of text with one style). At most `maxHits`.
std::vector<FindHit> FindInPage(const LayoutResult& r, const std::string& query, const TextMeasure& measure, std::size_t maxHits = 2000);

} // namespace okay::browser
