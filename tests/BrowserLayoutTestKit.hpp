#pragma once
// The helpers the browser layout tests share (test_browser_layout2.cpp, test_browser_layout3.cpp): lay a page out with the fixed
// text measure (every character half the font size wide), find a box by its id, compare a box or a text run with numbers worked
// out by hand. Uses only the layout API that existed before round 3, so the round 3 test also builds against the older library.
#include "test_framework.hpp"
#include "okay/Browser/Layout.hpp"

#include <cmath>
#include <deque>
#include <iostream>
#include <string>

namespace browsertest {

using namespace okay::browser;

inline const FixedMeasure& Measure() { static const FixedMeasure m(0.5f); return m; }

/// Lay `html` out `width` px wide (the documents are kept: the boxes point at their elements).
inline LayoutResult Lay(const std::string& html, float width, const ImageSizeFn& img = ImageSizeFn(), const LayoutContext* ctx = nullptr) {
    static std::deque<HtmlDocument> docs;
    docs.push_back(ParseHtml(html));
    return LayoutPage(docs.back(), width, Measure(), img, ctx);
}
/// The box of the element with id `id`.
inline const BoxInfo* ById(const LayoutResult& r, const std::string& id) {
    for (const BoxInfo& b : r.boxes) if (b.node) if (const std::string* v = b.node->Attr("id")) if (*v == id) return &b;
    return nullptr;
}
inline bool Near(float a, float b) { return std::fabs(a - b) < 0.05f; }
inline bool BoxIs(const LayoutResult& r, const std::string& id, float x, float y, float w, float h) {
    const BoxInfo* b = ById(r, id);
    if (!b) { std::cerr << "  no box #" << id << "\n"; return false; }
    if (Near(b->x, x) && Near(b->y, y) && Near(b->w, w) && Near(b->h, h)) return true;
    std::cerr << "  #" << id << ": got " << b->x << "," << b->y << " " << b->w << "x" << b->h << "  want " << x << "," << y << " " << w << "x" << h << "\n";
    return false;
}
inline const DrawCmd* TextCmd(const LayoutResult& r, const std::string& text) {
    for (const DrawCmd& c : r.cmds) if (c.kind == DrawCmd::Text && c.text == text) return &c;
    return nullptr;
}
inline bool TextAt(const LayoutResult& r, const std::string& text, float x, float y) {
    const DrawCmd* c = TextCmd(r, text);
    if (!c) {
        std::cerr << "  no text '" << text << "'; runs:";
        for (const DrawCmd& k : r.cmds) if (k.kind == DrawCmd::Text) std::cerr << " '" << k.text << "'";
        std::cerr << "\n";
        return false;
    }
    if (Near(c->x, x) && Near(c->y, y)) return true;
    std::cerr << "  '" << text << "': got " << c->x << "," << c->y << " want " << x << "," << y << "\n";
    return false;
}

} // namespace browsertest
