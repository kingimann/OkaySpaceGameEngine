#pragma once
// Browser (page engine): the HTML reader. Turns the text of a page into a tree of elements and text, written from the WHATWG HTML
// standard, section 13.2 ("Parsing HTML documents"): the tokenizer states for data, tags, attributes, comments, DOCTYPE,
// RAWTEXT (style, script) and RCDATA (title, textarea), character references (13.2.5.72-80, with the numeric-reference
// replacement table), and a reduced tree builder: the html / head / body elements are made when the page leaves them out,
// "has an element in (button / list item / table) scope", "generate implied end tags", a <p> closed by the block start tags
// that close it, <li> / <dd> / <dt> closing their open sibling, void elements, and "any other end tag". Not done (a page
// still reads, just with a slightly different tree than a browser's): the adoption agency algorithm for mis-nested
// formatting tags (an end tag that would cross a block is ignored), foster parenting of stray text in tables, templates,
// frames, and SVG / MathML foreign content (their tags are read as ordinary elements).
//
// Hostile input: every limit in HtmlLimits is checked, so a page cannot make the reader use unbounded memory or stack:
// too-deep nesting is flattened (deeper elements become siblings at the deepest level), text and nodes beyond the caps are
// dropped, long attribute values and names are cut. `truncated` says that something was dropped.
// Standard library only; no drawing, no network.
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace okay::browser {

enum class NodeKind { Document, Doctype, Element, Text, Comment };

struct Node {
    NodeKind kind = NodeKind::Element;
    std::string name;   ///< element: the lower-cased tag name; doctype: its name
    std::string data;   ///< text and comment: their characters (UTF-8, character references decoded)
    std::vector<std::pair<std::string, std::string>> attrs;   ///< in source order; names lower-cased; a repeated name keeps the first
    Node* parent = nullptr;
    std::vector<std::unique_ptr<Node>> children;

    bool IsElement(const char* tag) const { return kind == NodeKind::Element && name == tag; }
    /// The value of attribute `name`, or nullptr.
    const std::string* Attr(const char* attrName) const;
    /// All the text below this node, in order.
    std::string TextContent() const;
};

struct HtmlLimits {
    std::size_t maxInputBytes = 8u << 20;   ///< the rest of a longer page is not read
    std::size_t maxNodes = 200000;          ///< elements + text + comments
    int maxDepth = 256;                     ///< open elements at once; deeper start tags are added but not entered
    std::size_t maxAttrs = 64;              ///< per element
    std::size_t maxNameBytes = 64;          ///< tag and attribute names
    std::size_t maxAttrValueBytes = 64u << 10;
    std::size_t maxTextBytes = 4u << 20;    ///< all text nodes together
};

struct HtmlDocument {
    std::unique_ptr<Node> root;            ///< the Document node
    std::string title;                     ///< the first <title>'s text, spaces collapsed
    std::vector<std::string> styleSheets;  ///< the text of each <style> element, in order
    std::string baseHref;                  ///< the first <base href>, as written
    bool truncated = false;                ///< a limit dropped or cut something
    /// Quirks mode (HTML 13.2.6.4.1, "the initial insertion mode"): the page has no <!DOCTYPE html> before its first tag, or an
    /// old one (a DOCTYPE other than "html", or a legacy HTML 2 / 3.2 / 4.0 / 4.01-transitional-without-system-id public id).
    /// The style reads it (Css.hpp StyleContext::quirks): tables do not inherit fonts and alignment from outside, as in old browsers.
    bool quirks = false;

    const Node* Html() const;   ///< the <html> element (always present after ParseHtml)
    const Node* Body() const;   ///< the <body> element (always present after ParseHtml)
    /// The first element whose id (or, for <a>, name) is `id`; nullptr when there is none.
    const Node* FindAnchor(const std::string& id) const;
};

/// Read a page. Never fails: any text gives a tree with <html>, <head> and <body>.
HtmlDocument ParseHtml(const std::string& text, const HtmlLimits& limits = HtmlLimits());

/// The tree as text, one node per line, two spaces per level (the shape of the html5lib tree-construction test format):
///   <!DOCTYPE html>   <tag>   name="value" (attributes, under their element)   "text"   <!-- comment -->
std::string DumpTree(const Node& node);

/// The children of `node` as HTML text (the standard's "HTML fragment serialization algorithm", 13.3: what innerHTML reads):
/// text escaped (& < > and the no-break space; inside an attribute also "), void elements without an end tag, the text of
/// style / script / xmp / iframe / noembed / noframes / plaintext as it is.
std::string SerializeChildren(const Node& node);
/// `node` itself and its children as HTML text (what outerHTML reads).
std::string SerializeNode(const Node& node);

/// Decode the character references in `text` ("&amp;", "&#169;", "&#x1F600;" ...). In an attribute value a named reference
/// without its ';' that is followed by '=' or a letter or digit is left as written (13.2.5.73).
std::string DecodeCharRefs(const std::string& text, bool inAttribute);

/// Elements that never have content (13.1.2): area base br col embed hr img input link meta source track wbr.
bool IsVoidElement(const std::string& tag);

} // namespace okay::browser
