#pragma once
// Browser (page engine): layout. Places the styled page in a column of a given width and gives back a flat list of things to draw
// (filled rectangles, text runs, images, list markers) plus the clickable link areas. Written from CSS 2.1 chapters 8 (box
// model: margin, border, padding), 9 (visual formatting: block and inline formatting contexts, anonymous block boxes), 10
// (widths with auto margins, max-width, line height and the strut, baseline alignment), 12 (list markers) and 16 (text-align,
// white-space processing, text-decoration), and a simple table layout (equal column widths, rows as tall as their tallest
// cell, border-spacing 2px).
//
// What is simplified (said here so nobody is surprised): vertical margins collapse between siblings only (not parent with
// first or last child); floats and positioning are not supported; inline boxes draw no borders, padding or backgrounds of
// their own; an inline element that holds a block is laid out as a block; italic text is drawn upright by the default
// drawing code; table columns are equal width.
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
    /// <iframe>, <video>, and <okay-game>, the element an OkaySpace web game will use). Step 1 only lays it out (its
    /// width / height attributes or CSS, else 300 x 150 as the HTML standard gives a canvas) and the host draws a placeholder;
    /// a game runner or a script engine fills it later.
    enum Kind { Fill, Text, Image, Disc, Circle, Square, Embed } kind = Fill;
    float x = 0, y = 0, w = 0, h = 0;   ///< Text: (x, y) is the top-left of the em box, h the font size
    Color color;
    std::string text;    ///< Text: the run; Image and Embed: the src (or data) attribute as written
    std::string tag;     ///< Embed: the element name
    FontSpec font;
    bool underline = false, strike = false;
    int link = -1;       ///< index into LayoutResult::links, or -1
};

struct LinkArea {
    float x = 0, y = 0, w = 0, h = 0;
    int link = -1;
};

/// One block-level box, for tests and tools.
struct BoxInfo {
    std::string tag;   ///< the element name, or "(anonymous)"
    int depth = 0;
    float x = 0, y = 0, w = 0, h = 0;   ///< the border box
    std::size_t firstCmd = 0;           ///< the index in cmds of the first thing drawn at or after this box's start
};

struct LayoutResult {
    std::vector<DrawCmd> cmds;            ///< in painting order
    std::vector<LinkArea> areas;          ///< where each link run is, for clicks
    std::vector<std::string> links;       ///< the href of each link, as written
    std::vector<BoxInfo> boxes;           ///< block boxes in tree order
    std::vector<std::string> images;      ///< the src of every <img>, as written, once each
    std::vector<int> embeds;              ///< indexes into cmds of the Embed boxes, in page order
    std::vector<std::pair<std::string, float>> anchors;   ///< id (or <a name>) and the top of its box
    Color canvas{255, 255, 255, 255};     ///< the page background (from <html> or else <body>, CSS 2.1 14.2)
    float width = 0, height = 0;          ///< the whole page
};

/// Lay out `doc` in a viewport `width` px wide.
LayoutResult LayoutPage(const HtmlDocument& doc, float width, const TextMeasure& measure, const ImageSizeFn& imageSize = ImageSizeFn());

/// The block boxes and text runs as text, one per line: "block <tag> x,y wxh" and "text x,y 'run'" (whole pixels).
std::string DumpLayout(const LayoutResult& r);

/// The link under (x, y) in page coordinates: its index into links, or -1.
int LinkAt(const LayoutResult& r, float x, float y);

} // namespace okay::browser
