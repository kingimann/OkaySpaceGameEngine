// Browser (page engine): layout. Written from CSS 2.1 chapters 8-12 and 16, CSS Box Sizing 3, CSS Overflow 3, CSS Flexible Box
// Layout 1, CSS Grid Layout 1 and CSS Backgrounds 3 (see Layout.hpp for what is done and what is simplified). No code from any
// browser or library.
#include "okay/Browser/Layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <array>
#include <deque>
#include <map>
#include <tuple>

namespace okay::browser {

float FixedMeasure::Width(const std::string& utf8, const FontSpec& font) const {
    std::size_t points = 0;
    for (unsigned char c : utf8) if ((c & 0xC0) != 0x80) ++points;
    return (float)points * m_factor * font.size;
}

namespace {

constexpr float kInf = 1e9f;

// ---- the styled tree ---------------------------------------------------------------------------------------------------

struct SNode {
    const Node* node = nullptr;   // the element, or the text node; null for an anonymous box
    bool text = false;
    ComputedStyle st;
    std::vector<SNode> kids;
};

bool IsEmbedTag(const std::string& t) {
    return t == "canvas" || t == "embed" || t == "object" || t == "iframe" || t == "video" || t == "okay-game";
}
bool IsControlTag(const std::string& t) { return t == "input" || t == "select" || t == "textarea"; }
bool IsReplaced(const SNode& n) { return n.node && !n.text && (n.node->name == "img" || IsEmbedTag(n.node->name) || IsControlTag(n.node->name)); }
// In flow and block-level (floats and positioned boxes are placed by their own rules).
bool IsBlockLevel(const SNode& n) {
    return !n.text && !IsInlineLevel(n.st.display) && n.st.floating == Float::None && !n.st.OutOfFlow();
}
bool IsFlexDisplay(Display d) { return d == Display::Flex || d == Display::InlineFlex; }
bool IsGridDisplay(Display d) { return d == Display::Grid || d == Display::InlineGrid; }
bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
bool OnlySpaceText(const std::string& s) {
    for (char c : s) if (!IsSpace(c)) return false;
    return true;
}

// The style of an anonymous box inside `parent`: the inherited properties, nothing of the box.
ComputedStyle AnonymousStyle(const ComputedStyle& parent, Display d) {
    ComputedStyle s = InheritedStyle(parent);
    s.display = d;
    return s;
}

// A flex or grid item is block-level whatever its display was (CSS Flexbox 4, CSS Grid 6.1: "blockified").
void Blockify(ComputedStyle& s) {
    if (s.display == Display::Inline || s.display == Display::InlineBlock) s.display = Display::Block;
    else if (s.display == Display::InlineFlex) s.display = Display::Flex;
    else if (s.display == Display::InlineGrid) s.display = Display::Grid;
    s.floating = Float::None;
}

// Table parts (CSS 2.1 17.2): a row group, a row, a cell, a caption.
bool IsRowGroup(Display d) { return IsTableSection(d); }

struct Builder {
    const std::vector<const StyleSheet*>& sheets;
    const StyleContext* ctx;
    std::deque<Node> synth;   // the boxes of ::before / ::after and their text
    bool anyPseudo[3] = {false, false, false};
    // the counters in scope (CSS 2.1 12.4.1), innermost last: an element's counter-reset adds one that lives until its parent's
    // children end, so it reaches the element, its descendants and its later siblings
    std::vector<std::pair<std::string, int>> counters;

    void ApplyCounters(const ComputedStyle& st) {
        for (const auto& c : st.counterReset) if (counters.size() < 4096) counters.push_back(c);
        for (const auto& c : st.counterSet) {
            bool found = false;
            for (std::size_t k = counters.size(); k-- > 0;) if (counters[k].first == c.first) { counters[k].second = c.second; found = true; break; }
            if (!found && counters.size() < 4096) counters.push_back(c);
        }
        for (const auto& c : st.counterIncrement) {
            bool found = false;
            for (std::size_t k = counters.size(); k-- > 0;)
                if (counters[k].first == c.first) {
                    const long long v = (long long)counters[k].second + c.second;
                    counters[k].second = (int)std::max(-2000000000LL, std::min(2000000000LL, v));
                    found = true;
                    break;
                }
            if (!found && counters.size() < 4096) counters.push_back(c);   // no counter of that name: one starts here (12.4)
        }
    }
    // Fill the counter() / counters() markers of generated content in with the counters' values now.
    std::string ResolveCounters(const std::string& in) const {
        if (in.find('\x03') == std::string::npos && in.find('\x05') == std::string::npos) return in;
        std::string out;
        for (std::size_t i = 0; i < in.size();) {
            const char c = in[i];
            if (c != '\x03' && c != '\x05') { out += in[i++]; continue; }
            const char endMark = c == '\x03' ? '\x04' : '\x06';
            const std::size_t e = in.find(endMark, i);
            if (e == std::string::npos) break;
            std::vector<std::string> parts;
            std::size_t st = i + 1;
            for (std::size_t k = i + 1; k <= e; ++k) if (k == e || in[k] == '\x1f') { parts.push_back(in.substr(st, k - st)); st = k + 1; }
            ListStyle style = ListStyle::Decimal;
            if (!parts.empty()) ListStyleFromName(parts.back(), style);
            if (c == '\x03' && parts.size() == 2) {
                int v = 0;
                for (std::size_t k = counters.size(); k-- > 0;) if (counters[k].first == parts[0]) { v = counters[k].second; break; }
                out += CounterText(v, style);
            } else if (c == '\x05' && parts.size() == 3) {
                bool firstOne = true;
                for (const auto& cv : counters) {
                    if (cv.first != parts[0]) continue;
                    if (!firstOne) out += parts[1];
                    out += CounterText(cv.second, style);
                    firstOne = false;
                    if (out.size() > 4096) break;
                }
                if (firstOne) out += CounterText(0, style);
            }
            i = e + 1;
        }
        return out;
    }

    void Prepare() {
        for (const StyleSheet* s : sheets) {
            if (!s) continue;
            for (const Rule& r : s->rules) for (const Selector& sel : r.selectors) if (sel.pseudoElement == 1 || sel.pseudoElement == 2) anyPseudo[sel.pseudoElement] = true;
        }
    }

    // Build the styled tree of `el`; false when it is display: none.
    bool Build(const Node& el, const ComputedStyle& parent, SNode& out) {
        out.node = &el;
        out.st = ComputeStyle(el, parent, sheets, ctx);
        if (out.st.display == Display::None) return false;
        ApplyCounters(out.st);
        if (el.name == "img" || el.name == "br" || IsEmbedTag(el.name) || IsControlTag(el.name)) {
            if (IsControlTag(el.name) && out.st.display == Display::Inline) out.st.display = Display::InlineBlock;
            return true;
        }
        const std::size_t counterMark = counters.size();
        if (anyPseudo[1]) AddPseudo(el, out, 1);
        for (const auto& c : el.children) {
            if (c->kind == NodeKind::Text) {
                SNode t;
                t.node = c.get();
                t.text = true;
                t.st = out.st;
                out.kids.push_back(std::move(t));
            } else if (c->kind == NodeKind::Element) {
                SNode k;
                if (Build(*c, out.st, k)) out.kids.push_back(std::move(k));
            }
        }
        if (anyPseudo[2]) AddPseudo(el, out, 2);
        if (counters.size() > counterMark) counters.resize(counterMark);   // the counters the children started end here
        NormalizeTable(out);
        if (IsFlexDisplay(out.st.display) || IsGridDisplay(out.st.display)) {
            // every element child is an item; runs of text become anonymous items; space between them is dropped
            std::vector<SNode> items;
            for (SNode& k : out.kids) {
                if (k.text) {
                    if (OnlySpaceText(k.node->data)) continue;
                    if (!items.empty() && items.back().node == nullptr) { items.back().kids.push_back(std::move(k)); continue; }
                    SNode anon;
                    anon.st = AnonymousStyle(out.st, Display::Block);
                    anon.kids.push_back(std::move(k));
                    items.push_back(std::move(anon));
                    continue;
                }
                if (!k.st.OutOfFlow()) Blockify(k.st);
                items.push_back(std::move(k));
            }
            out.kids.swap(items);
        }
        bool anyBlock = false;
        for (const SNode& k : out.kids) if (IsBlockLevel(k)) anyBlock = true;
        if (anyBlock && out.st.display == Display::Inline) out.st.display = Display::Block;   // an inline that holds a block
        return true;
    }

    // CSS 2.1 17.2.1, anonymous table objects (the parts the pages we meet need): inside a table or a row group, runs of
    // children that are not rows (cells, text, inline boxes) go into an anonymous row; inside a row, runs of children that are not
    // cells go into an anonymous cell; white space between table parts is dropped.
    void NormalizeTable(SNode& out) {
        const Display d = out.st.display;
        const bool table = d == Display::Table, group = IsRowGroup(d), row = d == Display::TableRow;
        if (!table && !group && !row) return;
        std::vector<SNode> kids;
        kids.reserve(out.kids.size());
        auto properChild = [&](const SNode& k) {
            if (k.text) return false;
            if (k.st.OutOfFlow() || k.st.floating != Float::None) return true;   // kept as they are (placed by their own rules)
            if (row) return k.st.display == Display::TableCell;
            if (table) return k.st.display == Display::TableRow || IsRowGroup(k.st.display) || k.st.display == Display::TableCaption;
            return k.st.display == Display::TableRow;
        };
        for (SNode& k : out.kids) {
            if (k.text && OnlySpaceText(k.node->data) && k.st.whiteSpace != WhiteSpace::Pre && k.st.whiteSpace != WhiteSpace::PreWrap) {
                // space between table parts is dropped; inside an anonymous cell it stays
                if (!kids.empty() && kids.back().node == nullptr && !row) {
                    SNode& anonRow = kids.back();
                    if (!anonRow.kids.empty() && anonRow.kids.back().node == nullptr) anonRow.kids.back().kids.push_back(std::move(k));
                } else if (row && !kids.empty() && kids.back().node == nullptr) kids.back().kids.push_back(std::move(k));
                continue;
            }
            if (properChild(k)) { kids.push_back(std::move(k)); continue; }
            if (row) {
                if (kids.empty() || kids.back().node != nullptr) { SNode cell; cell.st = AnonymousStyle(out.st, Display::TableCell); kids.push_back(std::move(cell)); }
                kids.back().kids.push_back(std::move(k));
                continue;
            }
            // table or row group: into an anonymous row (a cell goes in as it is, anything else into an anonymous cell)
            if (kids.empty() || kids.back().node != nullptr) { SNode r; r.st = AnonymousStyle(out.st, Display::TableRow); kids.push_back(std::move(r)); }
            SNode& anonRow = kids.back();
            if (!k.text && k.st.display == Display::TableCell) { anonRow.kids.push_back(std::move(k)); continue; }
            if (anonRow.kids.empty() || anonRow.kids.back().node != nullptr) { SNode cell; cell.st = AnonymousStyle(anonRow.st, Display::TableCell); anonRow.kids.push_back(std::move(cell)); }
            anonRow.kids.back().kids.push_back(std::move(k));
        }
        out.kids.swap(kids);
    }

    void AddPseudo(const Node& el, SNode& out, int which) {
        ComputedStyle ps = ComputeStyle(el, out.st, sheets, ctx, which);
        if (!ps.hasContent || ps.display == Display::None) return;
        ApplyCounters(ps);
        ps.content = ResolveCounters(ps.content);
        if (synth.size() > 100000) return;
        synth.emplace_back();
        Node& pe = synth.back();
        pe.kind = NodeKind::Element;
        pe.name = which == 1 ? "::before" : "::after";
        pe.parent = const_cast<Node*>(&el);
        SNode sn;
        sn.node = &pe;
        sn.st = ps;
        if (!ps.content.empty()) {
            synth.emplace_back();
            Node& t = synth.back();
            t.kind = NodeKind::Text;
            t.data = ps.content;
            t.parent = &pe;
            SNode tn;
            tn.node = &t;
            tn.text = true;
            tn.st = ps;
            sn.kids.push_back(std::move(tn));
        }
        if (which == 1) out.kids.insert(out.kids.begin(), std::move(sn));
        else out.kids.push_back(std::move(sn));
    }
};

// ---- helpers -----------------------------------------------------------------------------------------------------------

float Finite(float v) { return std::isfinite(v) ? std::max(-1e9f, std::min(1e9f, v)) : 0.0f; }

FontSpec FontOf(const ComputedStyle& s) {
    FontSpec f;
    f.size = s.fontSize;
    f.bold = s.bold; f.italic = s.italic; f.mono = s.mono;
    f.weight = s.weight;
    f.family = s.family;
    return f;
}


int AttrInt(const Node& el, const char* name, int fallback) {
    const std::string* v = el.Attr(name);
    if (!v) return fallback;
    const char* s = v->c_str();
    while (*s == ' ') ++s;
    char* e = nullptr;
    const long n = std::strtol(s, &e, 10);
    if (e == s) return fallback;
    return (int)std::max(-1000000000L, std::min(1000000000L, n));
}

// A list item's marker text (CSS 2.1 12.5 / 12.6.2): the counter in its style and a full stop; "" for the bullet styles.
std::string MarkerText(ListStyle ls, int n) {
    if (ls == ListStyle::Disc || ls == ListStyle::Circle || ls == ListStyle::Square || ls == ListStyle::None) return std::string();
    return CounterText(n, ls) + ".";
}

// A margin collapsing with others (CSS 2.1 8.3.1): the largest positive and the most negative margin of the set.
struct MarginSet {
    float pos = 0.0f, neg = 0.0f;
    void Add(float m) { if (m >= 0) pos = std::max(pos, m); else neg = std::min(neg, m); }
    void Add(const MarginSet& o) { pos = std::max(pos, o.pos); neg = std::min(neg, o.neg); }
    float Value() const { return pos + neg; }
};

// The next UTF-8 code point's length at s[i].
std::size_t CpLen(const std::string& s, std::size_t i) {
    const unsigned char c = (unsigned char)s[i];
    std::size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    if (i + n > s.size()) n = s.size() - i;
    return n;
}

std::string Transform(const std::string& s, TextTransform t, bool& wordStart) {
    if (t == TextTransform::None) return s;
    std::string o = s;
    for (char& c : o) {
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (t == TextTransform::Upper && c >= 'a' && c <= 'z') c = (char)(c - 32);
        else if (t == TextTransform::Lower && c >= 'A' && c <= 'Z') c = (char)(c + 32);
        else if (t == TextTransform::Capitalize && wordStart && c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (IsSpace(c)) wordStart = true;
        else if (letter || (unsigned char)c >= 0x80 || (c >= '0' && c <= '9')) wordStart = false;
    }
    return o;
}

// ---- inline content ----------------------------------------------------------------------------------------------------

struct Frag {
    enum Kind { Word, Space, Image, Embed, Break, Anchor, Atomic, Float, Abs, Pad } kind = Word;   // Pad: an inline box's margin + border + padding
    std::string text;              // Word / Space: the characters; Image / Embed: src; Anchor: the id
    const ComputedStyle* st = nullptr;
    const SNode* sn = nullptr;     // Image / Embed / Atomic / Float / Abs: the element
    int link = -1;
    bool breakBefore = false;      // a line may end before this fragment
    float w = 0, h = 0;            // measured / sized (Atomic: the margin box)
    float ascent = -1;             // Atomic: the baseline below the margin box's top (< 0: the bottom)
    int atomicMark = -1;           // Atomic: index into Layouter::atomicMarks (where its draw commands are)
};

struct BoxDraw { int bg = -1; int border[4] = {-1, -1, -1, -1}; int ring = -1; int grad = -1; };

struct FloatBox { float x, y, w, h; bool left; };
struct FloatCtx {
    std::vector<FloatBox> list;
    float Bottom(Clear c) const {
        float b = -kInf;
        for (const FloatBox& f : list)
            if (c == Clear::Both || (c == Clear::Left && f.left) || (c == Clear::Right && !f.left)) b = std::max(b, f.y + f.h);
        return b;
    }
};

struct Pending { const SNode* n; int depth; std::size_t staticIdx; };

struct Mark {
    std::size_t cmds = 0, areas = 0, boxes = 0, anchors = 0, links = 0, embeds = 0, clips = 0, scrollers = 0, controls = 0, statics = 0, grads = 0;
};

struct BlockOpt {
    int* boxIndex = nullptr;
    bool cell = false;
    float borderW = -1.0f;   // the border-box width to use (flex / grid items, shrink-to-fit boxes); < 0: from the style
    float borderH = -1.0f;   // the border-box height to use; < 0: from the style and the content
    float cbH = -1.0f;       // the containing block's height when it is definite (for % heights)
    bool noAvoid = false;    // do not move beside floats (floats, inline-blocks, items: they are placed by their parent)
    bool bfc = false;        // a new block formatting context (flex / grid items)
    float contentDy = 0.0f;  // table cells: the content moved down by this much (vertical-align)
    const float* borders = nullptr;   // table cells in the collapsing border model: the border widths drawn (top, right, bottom, left)
};

struct Layouter {
    const TextMeasure& measure;
    const ImageSizeFn& imageSize;
    LayoutResult& r;
    const LayoutContext* ctx;
    std::vector<BoxDraw> draws;   // per entry of r.boxes
    std::vector<int> cmdKey;      // per entry of r.cmds: its paint-order key (index into keys)
    std::vector<int> cmdGrad;     // per entry of r.cmds: a gradient placeholder's index into grads, or -1
    std::vector<std::shared_ptr<const Gradient>> grads;
    long gradientCells = 60000;   // the Fill commands every gradient of a page may make together (a hostile page's budget)
    std::vector<std::vector<std::pair<int, int>>> keys{{{0, 0}}};
    int curKey = 0;
    int curClip = -1;
    bool curFixed = false;
    FloatCtx* fc = nullptr;
    std::vector<std::vector<Pending>*> cbStack;   // the containing blocks of absolutely positioned boxes
    std::vector<Pending> fixedPending;
    std::vector<std::pair<float, float>> statics;   // static positions of out-of-flow boxes (moved with Shift)
    std::map<const SNode*, std::pair<float, float>> intrinsicCache, contentCache;   // with / without the box's own width
    std::map<std::tuple<const SNode*, int, int, int, int>, std::pair<float, float>> heightCache;   // (item, sizes) -> (h, baseline)
    bool canvasFromBody = false;
    const Node* bodyNode = nullptr;
    const Node* htmlNode = nullptr;
    float viewW = 0, viewH = 0;
    long budget = 2000000;   // block layouts a hostile page may cause
    int curLink = -1;        // the link a block-level <a href> (or <label>) gives everything inside it

    Layouter(const TextMeasure& m, const ImageSizeFn& img, LayoutResult& res, const LayoutContext* c, const Node* body, const Node* html)
        : measure(m), imageSize(img), r(res), ctx(c), bodyNode(body), htmlNode(html) {}

    struct Out { float h = 0; float baseline = -1; MarginSet esc; };   // esc: the bottom margins that collapse through the box's bottom

    float Measure(const std::string& s, const ComputedStyle& st) const {
        if (s.empty()) return 0.0f;
        return Finite(measure.Width(s, FontOf(st)));
    }

    // ---- emitting and moving what was laid out ----

    void Emit(DrawCmd c) {
        c.clip = curClip;
        c.fixed = curFixed;
        r.cmds.push_back(std::move(c));
        cmdKey.push_back(curKey);
        cmdGrad.push_back(-1);
    }
    void Area(float x, float y, float w, float h, int link) {
        LinkArea a;
        a.x = x; a.y = y; a.w = w; a.h = h; a.link = link;
        a.clip = curClip; a.fixed = curFixed; a.layer = curKey;
        r.areas.push_back(a);
    }
    Mark Here() const {
        Mark m;
        m.cmds = r.cmds.size(); m.areas = r.areas.size(); m.boxes = r.boxes.size(); m.anchors = r.anchors.size();
        m.links = r.links.size(); m.embeds = r.embeds.size(); m.clips = r.clips.size(); m.scrollers = r.scrollers.size();
        m.controls = r.controls.size(); m.statics = statics.size(); m.grads = grads.size();
        return m;
    }
    void ShiftRange(const Mark& a, const Mark& b, float dx, float dy) {
        if (dx == 0.0f && dy == 0.0f) return;
        for (std::size_t i = a.cmds; i < b.cmds; ++i) { DrawCmd& c = r.cmds[i]; c.x += dx; c.y += dy; c.ix += dx; c.iy += dy; }
        for (std::size_t i = a.areas; i < b.areas; ++i) { r.areas[i].x += dx; r.areas[i].y += dy; }
        for (std::size_t i = a.boxes; i < b.boxes; ++i) { r.boxes[i].x += dx; r.boxes[i].y += dy; }
        for (std::size_t i = a.anchors; i < b.anchors; ++i) r.anchors[i].second += dy;
        for (std::size_t i = a.clips; i < b.clips; ++i) { r.clips[i].x += dx; r.clips[i].y += dy; }
        for (std::size_t i = a.scrollers; i < b.scrollers; ++i) { r.scrollers[i].x += dx; r.scrollers[i].y += dy; }
        for (std::size_t i = a.controls; i < b.controls; ++i) { r.controls[i].x += dx; r.controls[i].y += dy; }
        for (std::size_t i = a.statics; i < b.statics; ++i) { statics[i].first += dx; statics[i].second += dy; }
    }
    void Shift(const Mark& a, float dx, float dy) { ShiftRange(a, Here(), dx, dy); }
    // Undo everything laid out since `m` (a measuring layout).
    void Rollback(const Mark& m) {
        r.cmds.resize(m.cmds); cmdKey.resize(m.cmds); cmdGrad.resize(m.cmds);
        r.areas.resize(m.areas);
        r.boxes.resize(m.boxes); draws.resize(m.boxes);
        r.anchors.resize(m.anchors);
        r.links.resize(m.links);
        r.linkNodes.resize(m.links);
        r.embeds.resize(m.embeds);
        r.clips.resize(m.clips);
        r.scrollers.resize(m.scrollers);
        r.controls.resize(m.controls);
        statics.resize(m.statics);
        grads.resize(m.grads);
        for (std::vector<Pending>* p : cbStack)
            p->erase(std::remove_if(p->begin(), p->end(), [&](const Pending& x) { return x.staticIdx >= m.statics; }), p->end());
        fixedPending.erase(std::remove_if(fixedPending.begin(), fixedPending.end(), [&](const Pending& x) { return x.staticIdx >= m.statics; }), fixedPending.end());
        while (!atomicMarks.empty() && atomicMarks.back().first.cmds > m.cmds) atomicMarks.pop_back();
    }
    void Fade(const Mark& m, float opacity) {
        if (opacity >= 1.0f) return;
        for (std::size_t i = m.cmds; i < r.cmds.size(); ++i) r.cmds[i].color.a = (std::uint8_t)std::lround(r.cmds[i].color.a * std::max(0.0f, opacity));
    }

    // ---- intrinsic widths (CSS Box Sizing 3, 5: min-content and max-content), border boxes without margins ----

    float EdgesH(const ComputedStyle& st, float base) const {
        return st.border[1] + st.border[3] + st.padding[1].Resolve(base) + st.padding[3].Resolve(base);
    }
    float EdgesV(const ComputedStyle& st, float base) const {
        return st.border[0] + st.border[2] + st.padding[0].Resolve(base) + st.padding[2].Resolve(base);
    }
    float MarginsH(const ComputedStyle& st, float base) const { return st.margin[1].Resolve(base) + st.margin[3].Resolve(base); }
    float MarginsV(const ComputedStyle& st, float base) const { return st.margin[0].Resolve(base) + st.margin[2].Resolve(base); }

    std::pair<float, float> Intrinsic(const SNode& n, bool ownWidth = true) {
        if (n.text) return {0, 0};
        auto& cache = ownWidth ? intrinsicCache : contentCache;
        auto it = cache.find(&n);
        if (it != cache.end()) return it->second;
        const ComputedStyle& st = n.st;
        const float edges = EdgesH(st, 0);
        float mn = 0, mx = 0;
        if (IsReplaced(n)) {
            float w, h;
            SizeReplaced(n, 0, w, h);
            mn = mx = w + edges;
        } else if (ownWidth && st.width.unit == Length::Px) {
            mn = mx = st.width.value + (st.borderBox ? 0 : edges);
        } else {
            if (IsFlexDisplay(st.display) && (st.flexDirection == FlexDir::Row || st.flexDirection == FlexDir::RowReverse)) {
                const float gap = st.columnGap.Resolve(0);
                int count = 0;
                for (const SNode& k : n.kids) {
                    if (k.text || k.st.OutOfFlow()) continue;
                    const auto ki = Intrinsic(k);
                    const float m = MarginsH(k.st, 0);
                    mx += ki.second + m + (count ? gap : 0);
                    mn = st.flexWrap == FlexWrap::NoWrap ? mn + ki.first + m : std::max(mn, ki.first + m);
                    ++count;
                }
            } else if (st.display == Display::Table) {
                TableIntrinsic(n, mn, mx);
            } else if (IsGridDisplay(st.display)) {
                const std::size_t cols = std::max<std::size_t>(1, st.gridColumns.tracks.size());
                float fixedSum = 0;
                bool allFixed = !st.gridColumns.tracks.empty() && st.gridColumns.autoRepeat.empty();
                for (const GridTrack& t : st.gridColumns.tracks) { if (t.maxKind == GridTrack::Fixed) fixedSum += t.maxValue; else allFixed = false; }
                const float gap = st.columnGap.Resolve(0);
                if (allFixed) mn = mx = fixedSum + gap * (float)(cols - 1);
                else {
                    std::vector<float> colMax(cols, 0.0f);
                    std::size_t i = 0;
                    for (const SNode& k : n.kids) {
                        if (k.text || k.st.OutOfFlow()) continue;
                        const auto ki = Intrinsic(k);
                        const float m = MarginsH(k.st, 0);
                        colMax[i % cols] = std::max(colMax[i % cols], ki.second + m);
                        mn = std::max(mn, ki.first + m);
                        ++i;
                    }
                    for (float c : colMax) mx += c;
                    mx += gap * (float)(cols - 1);
                }
            } else {
                std::size_t i = 0;
                while (i < n.kids.size()) {
                    if (!IsBlockLevel(n.kids[i])) {
                        std::vector<const SNode*> items;
                        while (i < n.kids.size() && !IsBlockLevel(n.kids[i])) items.push_back(&n.kids[i++]);
                        float a = 0, b = 0;
                        InlineIntrinsic(items, a, b);
                        mn = std::max(mn, a); mx = std::max(mx, b);
                        continue;
                    }
                    const SNode& k = n.kids[i++];
                    const auto ki = Intrinsic(k);
                    const float m = MarginsH(k.st, 0);
                    mn = std::max(mn, ki.first + m);
                    mx = std::max(mx, ki.second + m);
                }
            }
            mn += edges; mx += edges;
        }
        if (ownWidth) {
            if (st.maxWidth.unit == Length::Px) { const float m = st.maxWidth.value + (st.borderBox ? 0 : edges); mn = std::min(mn, m); mx = std::min(mx, m); }
            if (st.minWidth.unit == Length::Px) { const float m = st.minWidth.value + (st.borderBox ? 0 : edges); mn = std::max(mn, m); mx = std::max(mx, m); }
        }
        mx = std::max(mx, mn);
        const std::pair<float, float> res{Finite(mn), Finite(mx)};
        cache[&n] = res;
        return res;
    }
    void InlineIntrinsic(const std::vector<const SNode*>& items, float& mn, float& mx) {
        std::vector<Frag> frags;
        bool lastSpace = true;
        bool ws = true;
        for (const SNode* n : items) Flatten(*n, -1, frags, lastSpace, ws, true);
        float line = 0, unit = 0;
        mn = 0; mx = 0;
        bool lineStart = true;
        for (const Frag& f : frags) {
            float w = 0;
            const bool box = f.kind == Frag::Image || f.kind == Frag::Embed || f.kind == Frag::Atomic || f.kind == Frag::Float;
            if (f.kind == Frag::Word || f.kind == Frag::Space) w = Measure(f.text, *f.st);
            else if (box && f.sn) { const auto ki = Intrinsic(*f.sn); w = (f.kind == Frag::Atomic || f.kind == Frag::Float ? ki.second : ki.first) + MarginsH(f.sn->st, 0); }
            if (f.kind == Frag::Break) { mx = std::max(mx, line); mn = std::max(mn, unit); line = 0; unit = 0; lineStart = true; continue; }
            if (f.kind == Frag::Anchor || f.kind == Frag::Abs) continue;
            if (f.kind == Frag::Pad) { unit += f.w; line += f.w; continue; }
            if (f.kind == Frag::Space) {
                if (lineStart) continue;
                line += w;
                const bool wraps = f.st->whiteSpace != WhiteSpace::Pre && f.st->whiteSpace != WhiteSpace::NoWrap;
                if (wraps) { mn = std::max(mn, unit); unit = 0; } else unit += w;
                continue;
            }
            lineStart = false;
            if (f.breakBefore) { mn = std::max(mn, unit); unit = 0; }
            if (box) {
                const float boxMin = f.sn ? Intrinsic(*f.sn).first + MarginsH(f.sn->st, 0) : w;
                mn = std::max(mn, unit); unit = 0; mn = std::max(mn, boxMin); line += w;
                continue;
            }
            unit += w;
            line += w;
        }
        mx = std::max(mx, line);
        mn = std::max(mn, unit);
        mn = Finite(mn); mx = Finite(mx);
    }
    // min(max(min-content, available), max-content): CSS 2.1 10.3.5, the shrink-to-fit width (border box).
    float ShrinkToFit(const SNode& n, float avail) {
        const auto i = Intrinsic(n);
        return std::max(0.0f, std::min(std::max(i.first, avail), i.second));
    }

    // ---- flattening inline content into fragments ----
    void Flatten(const SNode& n, int link, std::vector<Frag>& out, bool& lastSpace, bool& wordStart, bool dry) {
        if (n.text) { FlattenText(n.node->data, n.st, link, out, lastSpace, wordStart); return; }
        if (!n.node) { for (const SNode& k : n.kids) Flatten(k, link, out, lastSpace, wordStart, dry); return; }
        const Node& el = *n.node;
        if (n.st.floating != Float::None || n.st.OutOfFlow()) {
            Frag f;
            f.kind = n.st.OutOfFlow() ? Frag::Abs : Frag::Float;
            f.st = &n.st; f.sn = &n;
            out.push_back(f);
            return;
        }
        if (el.name == "br") { Frag f; f.kind = Frag::Break; f.st = &n.st; out.push_back(f); lastSpace = true; return; }
        if (el.name == "img" || IsEmbedTag(el.name) || IsControlTag(el.name) || n.st.display != Display::Inline) {
            Frag f;
            f.kind = el.name == "img" ? Frag::Image : IsEmbedTag(el.name) ? Frag::Embed : Frag::Atomic;
            const std::string* src = el.Attr(el.name == "object" ? "data" : "src");
            if (f.kind != Frag::Atomic) f.text = src ? *src : std::string();
            f.st = &n.st; f.sn = &n; f.link = link; f.breakBefore = true;
            out.push_back(f);
            lastSpace = false;
            wordStart = true;
            return;
        }
        if (!dry) if (const std::string* id = el.Attr("id")) { Frag a; a.kind = Frag::Anchor; a.text = *id; a.st = &n.st; out.push_back(a); }
        if (el.name == "a") {
            if (!dry) if (const std::string* nm = el.Attr("name")) { Frag a; a.kind = Frag::Anchor; a.text = *nm; a.st = &n.st; out.push_back(a); }
            if (!dry) if (const std::string* href = el.Attr("href")) { link = (int)r.links.size(); r.links.push_back(*href); r.linkNodes.push_back(&el); }
        } else if (el.name == "label" && !dry && link < 0) {   // a label's text works its control (HTML 4.10.4)
            link = (int)r.links.size();
            r.links.push_back(std::string());
            r.linkNodes.push_back(&el);
        }
        // an inline box's horizontal margin, border and padding take room in the line (CSS 2.1 8.3, 10.3.1)
        const float padL = n.st.margin[3].Resolve(0) + n.st.border[3] + n.st.padding[3].Resolve(0);
        const float padR = n.st.margin[1].Resolve(0) + n.st.border[1] + n.st.padding[1].Resolve(0);
        if (padL != 0.0f) { Frag f; f.kind = Frag::Pad; f.w = padL; f.st = &n.st; f.link = link; out.push_back(f); }
        for (const SNode& k : n.kids) Flatten(k, link, out, lastSpace, wordStart, dry);
        if (padR != 0.0f) { Frag f; f.kind = Frag::Pad; f.w = padR; f.st = &n.st; f.link = link; out.push_back(f); }
    }

    void FlattenText(const std::string& sIn, const ComputedStyle& st, int link, std::vector<Frag>& out, bool& lastSpace, bool& wordStart) {
        const std::string s = Transform(sIn, st.textTransform, wordStart);
        const WhiteSpace ws = st.whiteSpace;
        const bool keepSpaces = ws == WhiteSpace::Pre || ws == WhiteSpace::PreWrap;
        const bool keepNewlines = keepSpaces || ws == WhiteSpace::PreLine;
        const bool wraps = ws != WhiteSpace::Pre && ws != WhiteSpace::NoWrap;
        auto push = [&](Frag::Kind k, std::string t) {
            Frag f;
            f.kind = k; f.text = std::move(t); f.st = &st; f.link = link;
            if (k == Frag::Word) f.breakBefore = wraps && (out.empty() || out.back().kind == Frag::Space);
            out.push_back(std::move(f));
        };
        std::string word;
        auto flushWord = [&]() { if (!word.empty()) { push(Frag::Word, word); word.clear(); lastSpace = false; } };
        for (std::size_t i = 0; i < s.size(); ++i) {
            const char c = s[i];
            if (c == '\n' && keepNewlines) { flushWord(); Frag b; b.kind = Frag::Break; b.st = &st; out.push_back(b); lastSpace = !keepSpaces; continue; }
            const bool space = IsSpace(c);
            if (!space) { word += c; continue; }
            if (keepSpaces) {
                if (ws == WhiteSpace::Pre) { word += (c == '\t') ? std::string("    ") : std::string(1, ' '); continue; }
                flushWord();
                push(Frag::Space, c == '\t' ? "    " : " ");
                continue;
            }
            flushWord();
            if (lastSpace) continue;
            push(Frag::Space, " ");
            lastSpace = true;
        }
        flushWord();
    }

    // The size of a replaced element (an image, an embedded box, a form control) in a containing block `cbW` wide: content box.
    void SizeReplaced(const SNode& n, float cbW, float& wOut, float& hOut) {
        const Node& el = *n.node;
        const ComputedStyle& st = n.st;
        const float eh = EdgesH(st, cbW), ev = EdgesV(st, cbW);
        float w = -1, h = -1;
        if (st.width.IsSet() && !(st.width.HasPercent() && cbW <= 0)) w = st.width.Resolve(cbW) - (st.borderBox ? eh : 0);
        else if (el.Attr("width")) w = (float)AttrInt(el, "width", -1);
        if (st.height.unit == Length::Px) h = st.height.value - (st.borderBox ? ev : 0);
        else if (el.Attr("height")) h = (float)AttrInt(el, "height", -1);
        if (el.name == "img") {
            int iw = 0, ih = 0;
            const std::string* src = el.Attr("src");
            const bool known = imageSize && src && imageSize(*src, iw, ih) && iw > 0 && ih > 0;
            if (w < 0 && h < 0) { if (known) { w = (float)iw; h = (float)ih; } else { w = 0; h = 0; } }
            else if (w < 0) w = known ? h * (float)iw / (float)ih : h;
            else if (h < 0) h = known ? w * (float)ih / (float)iw : w;
        } else if (IsControlTag(el.name)) {
            ControlDefaultSize(n, w, h);
        } else {
            if (w < 0) w = 300;
            if (h < 0) h = 150;
        }
        w = std::max(0.0f, Finite(w)); h = std::max(0.0f, Finite(h));
        if (st.maxWidth.IsSet() && !(st.maxWidth.HasPercent() && cbW <= 0)) {
            const float mw = std::max(0.0f, st.maxWidth.Resolve(cbW) - (st.borderBox ? eh : 0));
            if (w > mw && w > 0) { if (el.name == "img") h = h * mw / w; w = mw; }
        }
        if (st.maxHeight.unit == Length::Px) {
            const float mh = std::max(0.0f, st.maxHeight.value - (st.borderBox ? ev : 0));
            if (h > mh && h > 0) { if (el.name == "img") w = w * mh / h; h = mh; }
        }
        if (st.minWidth.unit == Length::Px) w = std::max(w, st.minWidth.value - (st.borderBox ? eh : 0));
        if (st.minHeight.unit == Length::Px) h = std::max(h, st.minHeight.value - (st.borderBox ? ev : 0));
        wOut = w; hOut = h;
    }
    // A form control's own size when the page gives none (the content box): a text field 20 characters wide, a button its label.
    void ControlDefaultSize(const SNode& n, float& w, float& h) {
        const Node& el = *n.node;
        const float fs = n.st.fontSize;
        const float lh = std::max(n.st.LineHeightPx(), fs * 1.2f);
        std::string type = el.Attr("type") ? *el.Attr("type") : std::string("text");
        for (char& c : type) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (el.name == "textarea") {
            const int cols = std::max(1, AttrInt(el, "cols", 20)), rows = std::max(1, AttrInt(el, "rows", 2));
            if (w < 0) w = Measure(std::string((std::size_t)std::min(cols, 400), 'm'), n.st) * 0.62f;
            if (h < 0) h = lh * (float)std::min(rows, 200);
            return;
        }
        if (el.name == "select") {
            std::string longest;
            for (const auto& c : el.children) if (c->IsElement("option")) { const std::string t = c->TextContent(); if (t.size() > longest.size()) longest = t; }
            if (w < 0) w = Measure(longest.empty() ? std::string("    ") : longest, n.st) + fs * 1.6f;
            if (h < 0) h = lh;
            return;
        }
        if (type == "checkbox" || type == "radio") { if (w < 0) w = 13; if (h < 0) h = 13; return; }
        if (type == "submit" || type == "button" || type == "reset") {
            const std::string label = ControlText(n);
            if (w < 0) w = Measure(label, n.st);
            if (h < 0) h = lh;
            return;
        }
        const int size = std::max(1, AttrInt(el, "size", 20));
        if (w < 0) w = Measure(std::string((std::size_t)std::min(size, 400), 'm'), n.st) * 0.62f;
        if (h < 0) h = lh;
    }
    std::string ControlText(const SNode& n) const {
        if (ctx && ctx->controlText) return ctx->controlText(*n.node);
        const Node& el = *n.node;
        if (el.name == "textarea") return el.TextContent();
        if (el.name == "select") {
            for (const auto& c : el.children) if (c->IsElement("option") && c->Attr("selected")) return c->TextContent();
            for (const auto& c : el.children) if (c->IsElement("option")) return c->TextContent();
            return std::string();
        }
        std::string type = el.Attr("type") ? *el.Attr("type") : std::string();
        for (char& c : type) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (const std::string* v = el.Attr("value")) return *v;
        if (type == "submit") return "Submit";
        if (type == "reset") return "Reset";
        return std::string();
    }

    // ---- floats (CSS 2.1 9.5) ----

    void Band(float y, float h, float cx, float cw, float& l, float& rr) const {
        l = cx; rr = cx + cw;
        if (!fc) return;
        for (const FloatBox& f : fc->list) {
            if (f.y + f.h <= y || f.y >= y + std::max(h, 0.01f)) continue;
            if (f.left) l = std::max(l, f.x + f.w);
            else rr = std::min(rr, f.x);
        }
    }
    // Where a float of outer size w x h goes, at or below y (CSS 2.1 9.5.1, simplified): as high, then as far left (or right) as
    // it fits; below the floats in the way otherwise.
    void PlaceFloat(float w, float h, bool left, float y, float cx, float cw, float& fx, float& fy) {
        float yy = y;
        if (fc) for (const FloatBox& f : fc->list) yy = std::max(yy, f.y);   // rule 5: not higher than an earlier float
        for (int guard = 0; guard < 1000; ++guard) {
            float l, rr;
            Band(yy, h, cx, cw, l, rr);
            if (rr - l >= w - 0.01f || (l <= cx + 0.01f && rr >= cx + cw - 0.01f)) {
                fx = left ? l : rr - w;
                fy = yy;
                return;
            }
            float next = kInf;
            if (fc) for (const FloatBox& f : fc->list) if (f.y + f.h > yy + 0.01f && f.y < yy + std::max(h, 0.01f)) next = std::min(next, f.y + f.h);
            if (next >= kInf) break;
            yy = next;
        }
        fx = left ? cx : cx + cw - w;
        fy = yy;
    }
    // Lay out a float found at y in a container (cx, cw) and add it to the float context.
    void LayFloat(const SNode& k, float cx, float y, float cw, int depth) {
        const ComputedStyle& st = k.st;
        const float ml = st.margin[3].Resolve(cw), mr = st.margin[1].Resolve(cw), mt = st.margin[0].Resolve(cw), mb = st.margin[2].Resolve(cw);
        BlockOpt o;
        o.noAvoid = true;
        o.bfc = true;
        int bi = -1;
        o.boxIndex = &bi;
        if (!st.width.IsSet() && !IsReplaced(k)) o.borderW = ShrinkToFit(k, cw - ml - mr);
        const Mark m = Here();
        const Out fo = Block(k, cx, y, cw, depth, 0, o);
        const float bw = bi >= 0 ? r.boxes[(std::size_t)bi].w : 0.0f;
        const float ow = ml + bw + mr, oh = std::max(0.0f, mt + fo.h + mb);
        float fx, fy;
        PlaceFloat(ow, oh, st.floating == Float::Left, y, cx, cw, fx, fy);
        Shift(m, fx - cx, fy + mt - y);
        if (fc) fc->list.push_back({fx, fy, ow, oh, st.floating == Float::Left});
    }

    // ---- out-of-flow boxes (CSS 2.1 10.3.7, 10.6.4) ----

    void DeferAbs(const SNode& k, float sx, float sy, int depth) {
        statics.push_back({sx, sy});
        const Pending p{&k, depth, statics.size() - 1};
        if (k.st.position == Position::Fixed) fixedPending.push_back(p);
        else if (!cbStack.empty()) cbStack.back()->push_back(p);
    }
    void LayAbs(const Pending& p, float cbX, float cbY, float cbW, float cbH) {
        const SNode& k = *p.n;
        const ComputedStyle& st = k.st;
        const bool lSet = st.inset[3].IsSet(), rSet = st.inset[1].IsSet(), tSet = st.inset[0].IsSet(), bSet = st.inset[2].IsSet();
        const float left = st.inset[3].Resolve(cbW), right = st.inset[1].Resolve(cbW), top = st.inset[0].Resolve(cbH), bottom = st.inset[2].Resolve(cbH);
        const float ml = st.margin[3].Resolve(cbW), mr = st.margin[1].Resolve(cbW), mt = st.margin[0].Resolve(cbW), mb = st.margin[2].Resolve(cbW);
        BlockOpt o;
        o.noAvoid = true;
        o.bfc = true;
        o.cbH = cbH;
        int bi = -1;
        o.boxIndex = &bi;
        const float avail = cbW - (lSet ? left : 0) - (rSet ? right : 0) - ml - mr;
        if (!st.width.IsSet() && !IsReplaced(k)) o.borderW = (lSet && rSet) ? std::max(0.0f, avail) : ShrinkToFit(k, std::max(0.0f, avail));
        if (tSet && bSet && !st.height.IsSet()) o.borderH = std::max(0.0f, cbH - top - bottom - mt - mb);
        const std::pair<float, float> sp = statics[p.staticIdx];
        const bool wasFixed = curFixed;
        if (st.position == Position::Fixed) curFixed = true;
        const Mark m = Here();
        const Out ao = Block(k, 0, 0, cbW, p.depth, 0, o);
        curFixed = wasFixed;
        const float bw = bi >= 0 ? r.boxes[(std::size_t)bi].w : 0.0f;
        float dx, dy;
        if (lSet) dx = cbX + left;
        else if (rSet) dx = cbX + cbW - right - mr - bw - ml;
        else dx = sp.first;
        // auto left and right margins with left, right and width set centre the box (10.3.7)
        if (lSet && rSet && st.width.IsSet() && st.margin[3].unit == Length::Auto && st.margin[1].unit == Length::Auto)
            dx = cbX + left + std::max(0.0f, (cbW - left - right - bw) * 0.5f);
        if (tSet) dy = cbY + top + mt;
        else if (bSet) dy = cbY + cbH - bottom - mb - ao.h;
        else dy = sp.second + mt;
        if (tSet && bSet && st.height.IsSet() && st.margin[0].unit == Length::Auto && st.margin[2].unit == Length::Auto)
            dy = cbY + top + std::max(0.0f, (cbH - top - bottom - ao.h) * 0.5f);
        Shift(m, dx, dy);
    }

    // ---- inline content: lines ----

    struct Placed { const Frag* f; float x; float w; };

    // Lay out inline content in a box `cw` wide at (cx, cy) with `container`'s strut, around the floats. Returns the height.
    float InlineContent(const std::vector<const SNode*>& items, const ComputedStyle& container, float cx, float cy, float cw, float& baseline, int depth) {
        std::vector<Frag> frags;
        bool lastSpace = true, wordStart = true;
        for (const SNode* n : items) Flatten(*n, curLink, frags, lastSpace, wordStart, false);
        // measure; atomic inline boxes are laid out now (at 0, 0) and moved into their line later
        for (Frag& f : frags) {
            if (f.kind == Frag::Word || f.kind == Frag::Space) f.w = Measure(f.text, *f.st);
            else if (f.kind == Frag::Image || f.kind == Frag::Embed) SizeAtomic(f, cw);
            else if (f.kind == Frag::Atomic) LayAtomic(f, cw, depth);
        }
        float y = cy;
        baseline = -1;
        std::vector<Placed> line;
        const float strut = container.LineHeightPx();
        float lineL = cx, lineW = cw;
        auto band = [&]() {
            float l, rr;
            Band(y, strut, cx, cw, l, rr);
            lineL = l;
            lineW = std::max(0.0f, rr - l);
        };
        band();
        float x = container.textIndent.Resolve(cw);
        bool lineHasContent = false;
        std::vector<const Frag*> laterFloats;
        auto finish = [&](bool forced) {
            if (!lineHasContent && !forced) { line.clear(); x = 0; return; }
            // drop trailing collapsible spaces
            while (!line.empty() && line.back().f->kind == Frag::Space && line.back().f->st->whiteSpace != WhiteSpace::PreWrap) {
                x -= line.back().w; line.pop_back();
            }
            y += FinishLine(line, x, container, lineL, y, lineW, baseline);
            line.clear(); x = 0; lineHasContent = false;
            for (const Frag* f : laterFloats) LayFloat(*f->sn, cx, y, cw, depth);
            laterFloats.clear();
            band();
        };
        std::size_t i = 0;
        const std::size_t n = frags.size();
        int guard = 0;
        while (i < n) {
            const Frag& f = frags[i];
            if (f.kind == Frag::Break) { finish(true); ++i; continue; }
            if (f.kind == Frag::Anchor) { line.push_back({&f, x, 0}); ++i; continue; }
            if (f.kind == Frag::Pad) { line.push_back({&f, x, f.w}); x += f.w; ++i; continue; }
            if (f.kind == Frag::Abs) { DeferAbs(*f.sn, lineL + x, y, depth); ++i; continue; }
            if (f.kind == Frag::Float) {
                // at the top of this line when it fits beside what is on it, else after the line
                if (!lineHasContent) { LayFloat(*f.sn, cx, y, cw, depth); band(); }
                else if (x + Intrinsic(*f.sn).first + MarginsH(f.sn->st, cw) <= lineW) { LayFloat(*f.sn, cx, y, cw, depth); band(); }
                else laterFloats.push_back(&f);
                ++i;
                continue;
            }
            if (f.kind == Frag::Space) {
                const bool collapsible = f.st->whiteSpace != WhiteSpace::PreWrap;
                if (!lineHasContent && collapsible) { ++i; continue; }
                line.push_back({&f, x, f.w}); x += f.w; ++i;
                continue;
            }
            // a unit: this fragment and the ones after it up to the next break opportunity
            std::size_t j = i + 1;
            float uw = f.w;
            if (f.kind == Frag::Word) {
                while (j < n && (frags[j].kind == Frag::Word || frags[j].kind == Frag::Anchor || frags[j].kind == Frag::Pad) && !frags[j].breakBefore) { uw += frags[j].w; ++j; }
            }
            const bool wraps = f.st->whiteSpace != WhiteSpace::Pre && f.st->whiteSpace != WhiteSpace::NoWrap;
            // the spaces before the unit count (it follows them); spaces left at a line's end are dropped by finish()
            if (lineHasContent && wraps && x + uw > lineW + 0.01f && x > 0) finish(false);
            // beside floats and too wide for the space left: go below the floats (CSS 2.1 9.5)
            if (!lineHasContent && uw > lineW + 0.01f && lineW < cw - 0.01f && fc && ++guard < 1000) {
                float next = kInf;
                for (const FloatBox& fb : fc->list) if (fb.y + fb.h > y + 0.01f) next = std::min(next, fb.y + fb.h);
                if (next < kInf) { y = next; band(); continue; }
            }
            if (!lineHasContent && wraps && uw > lineW + 0.01f && f.kind == Frag::Word) {
                // too long for a line of its own: break between characters
                for (std::size_t k = i; k < j; ++k) {
                    const Frag& part = frags[k];
                    if (part.kind == Frag::Anchor) { line.push_back({&part, x, 0}); continue; }
                    std::size_t p = 0;
                    while (p < part.text.size()) {
                        std::size_t q = p;
                        float w = 0;
                        while (q < part.text.size()) {
                            const std::size_t len = CpLen(part.text, q);
                            const float cwid = Measure(part.text.substr(q, len), *part.st);
                            if (q > p || lineHasContent) { if (x + w + cwid > lineW + 0.01f) break; }
                            w += cwid; q += len;
                        }
                        if (q == p) { finish(false); continue; }   // nothing fits after what is on the line: new line
                        pieces.push_back(part);
                        pieces.back().text = part.text.substr(p, q - p);
                        pieces.back().w = w;
                        line.push_back({&pieces.back(), x, w});
                        x += w; lineHasContent = true;
                        p = q;
                        if (p < part.text.size()) finish(false);
                    }
                }
                i = j;
                continue;
            }
            for (std::size_t k = i; k < j; ++k) { line.push_back({&frags[k], x, frags[k].w}); x += frags[k].w; }
            lineHasContent = true;
            i = j;
        }
        if (lineHasContent) finish(false);
        else {
            if (!line.empty())   // only anchors (and spaces) left: they still need a position
                for (const Placed& p : line) if (p.f->kind == Frag::Anchor) r.anchors.push_back({p.f->text, y});
            for (const Frag* f : laterFloats) LayFloat(*f->sn, cx, y, cw, depth);
        }
        return y - cy;
    }
    std::deque<Frag> pieces;   // pieces of words broken between characters (a deque: the line keeps pointers to them)
    std::vector<std::pair<Mark, Mark>> atomicMarks;

    // An image or embedded box in a line, for a containing block `cbW` wide.
    void SizeAtomic(Frag& f, float cbW) {
        float w, h;
        SizeReplaced(*f.sn, cbW, w, h);
        const float eh = EdgesH(f.sn->st, cbW), ev = EdgesV(f.sn->st, cbW);
        f.w = w + (f.kind == Frag::Image || f.kind == Frag::Embed ? 0.0f : eh);
        f.h = h + (f.kind == Frag::Image || f.kind == Frag::Embed ? 0.0f : ev);
    }
    // An inline-block (or inline-flex, inline-grid, form control, button): laid out as a block now, at (0, 0), moved later.
    void LayAtomic(Frag& f, float cw, int depth) {
        const SNode& k = *f.sn;
        const ComputedStyle& st = k.st;
        const float ml = st.margin[3].Resolve(cw), mr = st.margin[1].Resolve(cw), mt = st.margin[0].Resolve(cw), mb = st.margin[2].Resolve(cw);
        BlockOpt o;
        o.noAvoid = true;
        o.bfc = true;
        int bi = -1;
        o.boxIndex = &bi;
        if (!st.width.IsSet() && !IsReplaced(k)) o.borderW = ShrinkToFit(k, std::max(0.0f, cw - ml - mr));
        const Mark a = Here();
        const Out ao = Block(k, 0, mt, cw, depth, 0, o);
        const Mark b = Here();
        const float bw = bi >= 0 ? r.boxes[(std::size_t)bi].w : 0.0f;
        f.w = ml + bw + mr;
        f.h = mt + ao.h + mb;
        f.ascent = ao.baseline >= 0 ? ao.baseline : -1.0f;   // the first line's baseline (from the margin box's top)
        if (IsReplaced(k)) f.ascent = -1.0f;
        f.atomicMark = (int)atomicMarks.size();
        atomicMarks.push_back({a, b});
    }

    // Place one line: vertical alignment on the baseline (CSS 2.1 10.8), text-align, draw commands and link areas.
    float FinishLine(const std::vector<Placed>& line, float lineW, const ComputedStyle& container, float cx, float top, float cw, float& firstBaseline) {
        auto metrics = [](const ComputedStyle& st, float& asc, float& desc) {
            const float lh = st.LineHeightPx();
            const float half = (lh - st.fontSize) * 0.5f;
            asc = half + st.fontSize * 0.8f;
            desc = half + st.fontSize * 0.2f;
        };
        auto boxMetrics = [&](const Frag& f, float& a, float& d) {
            const VAlign va = f.sn ? f.sn->st.verticalAlign : VAlign::Baseline;
            const float fs = container.fontSize;
            if (va != VAlign::Baseline) {
                a = f.h * 0.5f + fs * 0.25f;
                d = f.h - a;
                return;
            }
            if (f.kind == Frag::Atomic && f.ascent >= 0) { a = f.ascent; d = f.h - f.ascent; return; }
            a = f.h; d = 0;
        };
        float maxA, maxD;
        metrics(container, maxA, maxD);   // the strut
        for (const Placed& p : line) {
            float a = 0, d = 0;
            if (p.f->kind == Frag::Word || p.f->kind == Frag::Space) metrics(*p.f->st, a, d);
            else if (p.f->kind == Frag::Image || p.f->kind == Frag::Embed || p.f->kind == Frag::Atomic) boxMetrics(*p.f, a, d);
            else continue;
            maxA = std::max(maxA, a);
            maxD = std::max(maxD, d);
        }
        const float lineH = Finite(std::max(0.0f, maxA + maxD));
        const float base = top + maxA;
        if (firstBaseline < 0) firstBaseline = base;
        float dx = 0;
        if (container.textAlign == TextAlign::Center) dx = (cw - lineW) * 0.5f;
        else if (container.textAlign == TextAlign::Right) dx = cw - lineW;
        dx = std::max(0.0f, dx);
        int run = -1;   // the text command being extended
        std::vector<std::pair<int, const ComputedStyle*>> runs;   // this line's text commands and their styles
        for (const Placed& p : line) {
            const Frag& f = *p.f;
            const float x = Finite(cx + dx + p.x);
            if (f.kind == Frag::Anchor) { r.anchors.push_back({f.text, top}); run = -1; continue; }
            if (f.kind == Frag::Pad) { run = -1; if (f.link >= 0) Area(x, top, p.w, 0.0f, f.link); continue; }
            if (f.kind == Frag::Atomic) {
                run = -1;
                float a, d;
                boxMetrics(f, a, d);
                if (f.atomicMark >= 0 && (std::size_t)f.atomicMark < atomicMarks.size())
                    ShiftRange(atomicMarks[(std::size_t)f.atomicMark].first, atomicMarks[(std::size_t)f.atomicMark].second, x, Finite(base - a));
                if (f.link >= 0) Area(x, Finite(base - a), f.w, f.h, f.link);
                continue;
            }
            if (f.kind == Frag::Image || f.kind == Frag::Embed) {
                run = -1;
                if (f.w <= 0 || f.h <= 0 || !f.st->visible) continue;
                float a, d;
                boxMetrics(f, a, d);
                DrawCmd c;
                c.kind = f.kind == Frag::Image ? DrawCmd::Image : DrawCmd::Embed;
                c.x = x; c.y = Finite(base - a); c.w = f.w; c.h = f.h;
                c.text = f.text;
                c.tag = f.sn->node->name;
                c.link = f.link;
                c.color = f.st->color;
                c.color.a = 255;
                if (c.kind == DrawCmd::Embed) { r.embeds.push_back((int)r.cmds.size()); Emit(c); }
                else EmitImage(c, *f.sn);
                if (f.link >= 0) Area(c.x, c.y, c.w, c.h, f.link);
                continue;
            }
            if (!f.st->visible) { run = -1; continue; }
            // Word or Space: extend the run when the style and link are the same
            if (run >= 0 && r.cmds[(std::size_t)run].link == f.link && runStyle == f.st &&
                std::fabs(r.cmds[(std::size_t)run].x + r.cmds[(std::size_t)run].w - x) < 0.01f) {
                DrawCmd& rc = r.cmds[(std::size_t)run];
                rc.text += f.text;
                rc.w += p.w;
                if (f.link >= 0 && !r.areas.empty()) r.areas.back().w = rc.w;
                continue;
            }
            DrawCmd c;
            c.kind = DrawCmd::Text;
            c.x = x; c.y = Finite(base - f.st->fontSize * 0.8f); c.w = p.w; c.h = f.st->fontSize;
            c.text = f.text;
            c.font = FontOf(*f.st);
            c.color = f.st->color;
            c.underline = f.st->underline; c.strike = f.st->strike;
            if (CustomDecoration(*f.st)) c.underline = c.strike = false;   // drawn by Decorations below
            c.link = f.link;
            Emit(c);
            run = (int)r.cmds.size() - 1;
            runStyle = f.st;
            runs.push_back({run, f.st});
            if (f.link >= 0) Area(c.x, top, c.w, lineH, f.link);
        }
        if (container.ellipsis && container.Clips() && lineW > cw + 0.01f && !runs.empty()) Ellipsis(runs, cx + cw);
        for (const auto& rs : runs) if (CustomDecoration(*rs.second) && rs.second->visible) Decorations(r.cmds[(std::size_t)rs.first], *rs.second);
        return lineH;
    }
    const ComputedStyle* runStyle = nullptr;

    // ---- transforms (CSS Transforms 1: the box and everything in it drawn through the matrix about transform-origin) ----

    static void MapRect(const Transform2D& m, float& x, float& y, float& w, float& h) {
        const float xs[4] = {x, x + w, x, x + w}, ys[4] = {y, y, y + h, y + h};
        float x0 = kInf, y0 = kInf, x1 = -kInf, y1 = -kInf;
        for (int k = 0; k < 4; ++k) {
            const float px = m.a * xs[k] + m.c * ys[k] + m.e, py = m.b * xs[k] + m.d * ys[k] + m.f;
            x0 = std::min(x0, px); y0 = std::min(y0, py); x1 = std::max(x1, px); y1 = std::max(y1, py);
        }
        x = Finite(x0); y = Finite(y0); w = Finite(x1 - x0); h = Finite(y1 - y0);
    }
    void ApplyTransform(const Mark& m, const ComputedStyle& st, float bx, float by, float bw, float bh) {
        Transform2D t;
        if (!ParseTransform(st.transform, st.fontSize, bw, bh, t)) return;
        const float ox = bx + st.originX.Resolve(bw), oy = by + st.originY.Resolve(bh);
        Transform2D to, back;
        to.e = -ox; to.f = -oy;
        back.e = ox; back.f = oy;
        const Transform2D mt = to.Then(t).Then(back);
        if (mt.IsIdentity()) return;
        for (std::size_t i = m.cmds; i < r.cmds.size(); ++i) {
            DrawCmd& c = r.cmds[i];
            const Transform2D full = c.xform.Then(mt);
            if (full.b == 0 && full.c == 0 && full.a > 0 && full.d > 0) {
                // translate and scale: folded into the command
                const float sx = full.a, sy = full.d;
                c.x = Finite(sx * c.x + full.e); c.y = Finite(sy * c.y + full.f); c.w *= sx; c.h *= sy;
                c.ix = Finite(sx * c.ix + full.e); c.iy = Finite(sy * c.iy + full.f); c.iw *= sx; c.ih *= sy;
                c.font.size *= sy;
                const float s = std::min(sx, sy);
                c.radius *= s; c.thick *= s; c.blur *= s;
                c.xform = Transform2D();
            } else c.xform = full;
        }
        for (std::size_t i = m.areas; i < r.areas.size(); ++i) MapRect(mt, r.areas[i].x, r.areas[i].y, r.areas[i].w, r.areas[i].h);
        for (std::size_t i = m.boxes; i < r.boxes.size(); ++i) MapRect(mt, r.boxes[i].x, r.boxes[i].y, r.boxes[i].w, r.boxes[i].h);
        for (std::size_t i = m.clips; i < r.clips.size(); ++i) MapRect(mt, r.clips[i].x, r.clips[i].y, r.clips[i].w, r.clips[i].h);
        for (std::size_t i = m.scrollers; i < r.scrollers.size(); ++i) MapRect(mt, r.scrollers[i].x, r.scrollers[i].y, r.scrollers[i].w, r.scrollers[i].h);
        for (std::size_t i = m.controls; i < r.controls.size(); ++i) MapRect(mt, r.controls[i].x, r.controls[i].y, r.controls[i].w, r.controls[i].h);
        for (std::size_t i = m.anchors; i < r.anchors.size(); ++i) r.anchors[i].second = Finite(mt.d * r.anchors[i].second + mt.f);
    }

    // ---- gradients drawn as bands of colour (CSS Images 3) ----

    // The Fill commands that draw gradient `g` over the placeholder `ph`'s box: bands across the gradient line for a gradient
    // along an axis, else a grid of small cells (runs of one colour joined). The whole page may make `gradientCells` of them; a
    // gradient past that is one Fill of its middle colour.
    void GradientCells(const DrawCmd& ph, const Gradient& g, std::vector<DrawCmd>& out, std::size_t& made) {
        made = 0;
        const float w = ph.w, h = ph.h;
        if (!(w > 0.0f) || !(h > 0.0f)) return;
        const float fade = ph.color.a / 255.0f;
        auto colorAt = [&](float px, float py) {
            Color c = GradientColorAt(g, w, h, px, py);
            c.a = (std::uint8_t)std::lround(c.a * fade);
            return c;
        };
        auto push = [&](float x, float y, float cw, float chh, Color col) {
            if (col.a == 0) return;
            DrawCmd c = ph;
            c.x = ph.x + x; c.y = ph.y + y; c.w = cw; c.h = chh;
            c.color = col;
            c.radius = 0.0f;
            out.push_back(c);
            ++made;
        };
        const bool axis = !g.radial && g.cornerX == 0 && g.cornerY == 0 && std::fmod(std::fabs(g.angle), 90.0f) < 1e-4f;
        if (axis) {
            const bool vertical = std::fmod(std::fabs(g.angle), 180.0f) < 1e-4f;   // to top / to bottom: bands one above another
            const float len = vertical ? h : w;
            const int n = std::max(1, std::min(256, (int)std::ceil(len / 2.0f)));
            if (gradientCells < n) { push(0, 0, w, h, colorAt(w * 0.5f, h * 0.5f)); --gradientCells; return; }
            const float step = len / (float)n;
            int start = 0;
            Color cur = vertical ? colorAt(w * 0.5f, step * 0.5f) : colorAt(step * 0.5f, h * 0.5f);
            for (int k = 1; k <= n; ++k) {
                const Color next = k < n ? (vertical ? colorAt(w * 0.5f, step * ((float)k + 0.5f)) : colorAt(step * ((float)k + 0.5f), h * 0.5f)) : Color();
                if (k < n && next == cur) continue;
                const float a = step * (float)start, b = step * (float)k;
                if (vertical) push(0, a, w, b - a, cur); else push(a, 0, b - a, h, cur);
                start = k;
                cur = next;
            }
            gradientCells -= (long)made;
            return;
        }
        const float cell = std::max(2.0f, std::sqrt(w * h / 1024.0f));
        const int nx = std::max(1, std::min(128, (int)std::ceil(w / cell))), ny = std::max(1, std::min(128, (int)std::ceil(h / cell)));
        if (gradientCells < (long)nx * ny) { push(0, 0, w, h, colorAt(w * 0.5f, h * 0.5f)); --gradientCells; return; }
        const float cw = w / (float)nx, chh = h / (float)ny;
        for (int j = 0; j < ny; ++j) {
            int start = 0;
            Color cur = colorAt(cw * 0.5f, chh * ((float)j + 0.5f));
            for (int i = 1; i <= nx; ++i) {
                const Color next = i < nx ? colorAt(cw * ((float)i + 0.5f), chh * ((float)j + 0.5f)) : Color();
                if (i < nx && next == cur) continue;
                push(cw * (float)start, chh * (float)j, cw * (float)(i - start), chh, cur);
                start = i;
                cur = next;
            }
        }
        gradientCells -= (long)made;
    }
    // Replace every gradient placeholder with its bands (after the layout, so the boxes have their final size and place).
    void ExpandGradients() {
        if (grads.empty()) return;
        std::vector<DrawCmd> out;
        std::vector<int> keyOut;
        out.reserve(r.cmds.size());
        std::vector<std::size_t> newIndex(r.cmds.size() + 1, 0);
        for (std::size_t i = 0; i < r.cmds.size(); ++i) {
            newIndex[i] = out.size();
            const int gi = i < cmdGrad.size() ? cmdGrad[i] : -1;
            if (gi < 0 || (std::size_t)gi >= grads.size() || !grads[(std::size_t)gi]) { out.push_back(r.cmds[i]); keyOut.push_back(cmdKey[i]); continue; }
            std::size_t made = 0;
            GradientCells(r.cmds[i], *grads[(std::size_t)gi], out, made);
            for (std::size_t k = 0; k < made; ++k) keyOut.push_back(cmdKey[i]);
        }
        newIndex[r.cmds.size()] = out.size();
        for (BoxInfo& b : r.boxes) b.firstCmd = newIndex[std::min(b.firstCmd, r.cmds.size())];
        for (int& e : r.embeds) if (e >= 0 && (std::size_t)e < r.cmds.size()) e = (int)newIndex[(std::size_t)e];
        r.cmds.swap(out);
        cmdKey.swap(keyOut);
        cmdGrad.assign(r.cmds.size(), -1);
        grads.clear();
    }

    // ---- text decorations other than a plain line in the text's colour (CSS Text Decoration 3): drawn as Fill commands ----

    static bool CustomDecoration(const ComputedStyle& st) {
        return (st.underline || st.strike || st.overline) && (st.decoStyle != DecoStyle::Solid || st.decoColor.a != 0 || st.overline);
    }
    void Decorations(const DrawCmd t, const ComputedStyle& st) {   // a copy: Emit below may move r.cmds
        const float fs = t.h;
        const float thick = std::max(1.0f, fs / 16.0f);
        Color col = st.decoColor.a != 0 ? st.decoColor : t.color;
        if (t.w <= 0) return;
        std::vector<float> lines;
        if (st.underline) lines.push_back(t.y + fs * 0.92f);
        if (st.strike) lines.push_back(t.y + fs * 0.55f);
        if (st.overline) lines.push_back(t.y);
        for (float ly : lines) {
            auto fill = [&](float x, float y, float w, float h) {
                DrawCmd c;
                c.kind = DrawCmd::Fill;
                c.x = x; c.y = y; c.w = w; c.h = h;
                c.color = col;
                c.link = t.link;
                Emit(c);
            };
            DecoStyle ds = st.decoStyle;
            const float period = ds == DecoStyle::Dotted ? thick * 2.0f : ds == DecoStyle::Dashed ? thick * 5.0f : thick;
            if ((ds == DecoStyle::Dotted || ds == DecoStyle::Dashed || ds == DecoStyle::Wavy) && t.w / period > 600.0f) ds = DecoStyle::Solid;
            switch (ds) {
            case DecoStyle::Solid: fill(t.x, ly, t.w, thick); break;
            case DecoStyle::Double: fill(t.x, ly, t.w, thick); fill(t.x, ly + thick * 2.0f, t.w, thick); break;
            case DecoStyle::Dotted:
                for (float x = t.x; x < t.x + t.w; x += period) fill(x, ly, std::min(thick, t.x + t.w - x), thick);
                break;
            case DecoStyle::Dashed:
                for (float x = t.x; x < t.x + t.w; x += period) fill(x, ly, std::min(thick * 3.0f, t.x + t.w - x), thick);
                break;
            case DecoStyle::Wavy: {
                // a sine wave (amplitude 1.5 x the thickness, wavelength 6 x) traced with small squares
                const float amp = thick * 1.5f, wave = thick * 6.0f;
                for (float x = t.x; x < t.x + t.w; x += thick)
                    fill(x, ly - thick * 0.5f + amp * std::sin((x - t.x) * 6.2831853f / wave), std::min(thick, t.x + t.w - x), thick);
                break;
            }
            }
        }
    }

    // text-overflow: ellipsis (CSS Overflow 3, 3.2): the line's text that does not fit in a box that clips is cut and ends in "…".
    void Ellipsis(const std::vector<std::pair<int, const ComputedStyle*>>& runs, float limit) {
        static const std::string kDots = "\xE2\x80\xA6";
        bool cut = false;
        for (const auto& run : runs) {
            DrawCmd& c = r.cmds[(std::size_t)run.first];
            if (cut) { c.text.clear(); c.w = 0; c.underline = c.strike = false; continue; }
            const float dots = Measure(kDots, *run.second);
            if (c.x + c.w <= limit + 0.01f) continue;
            std::size_t keep = 0;
            float w = 0;
            while (keep < c.text.size()) {
                const std::size_t len = CpLen(c.text, keep);
                const float cw = Measure(c.text.substr(keep, len), *run.second);
                if (c.x + w + cw + dots > limit + 0.01f) break;
                w += cw; keep += len;
            }
            c.text = c.text.substr(0, keep) + kDots;
            c.w = w + dots;
            cut = true;
        }
    }

    // An image's drawing command with object-fit and object-position (CSS Images 3, 5.5 / 5.6) inside its content box; one that
    // overflows the box (cover, none) becomes a background image clipped to the box.
    void EmitImage(DrawCmd c, const SNode& sn) {
        const ComputedStyle& st = sn.st;
        int iw = 0, ih = 0;
        if (st.objectFit == ObjectFit::Fill || !imageSize || !imageSize(c.text, iw, ih) || iw <= 0 || ih <= 0 || c.w <= 0 || c.h <= 0) { Emit(c); return; }
        const float sx = c.w / (float)iw, sy = c.h / (float)ih;
        float sc = 1.0f;
        switch (st.objectFit) {
        case ObjectFit::Contain: sc = std::min(sx, sy); break;
        case ObjectFit::Cover: sc = std::max(sx, sy); break;
        case ObjectFit::ScaleDown: sc = std::min(1.0f, std::min(sx, sy)); break;
        default: sc = 1.0f; break;
        }
        const float dw = (float)iw * sc, dh = (float)ih * sc;
        const float dx = c.x + st.objectPosX.Resolve(c.w - dw), dy = c.y + st.objectPosY.Resolve(c.h - dh);
        if (dx >= c.x - 0.01f && dy >= c.y - 0.01f && dx + dw <= c.x + c.w + 0.01f && dy + dh <= c.y + c.h + 0.01f) {
            c.x = dx; c.y = dy; c.w = dw; c.h = dh;
            Emit(c);
            return;
        }
        c.kind = DrawCmd::BgImage;
        c.ix = dx; c.iy = dy; c.iw = dw; c.ih = dh;
        c.repeatX = c.repeatY = false;
        Emit(c);
    }

    // ---- boxes ----

    int AddBox(const SNode& n, int depth, float x, float y, float w) {
        BoxInfo b;
        b.tag = n.node ? n.node->name : std::string("(anonymous)");
        b.node = n.node;
        b.depth = depth;
        b.x = x; b.y = y; b.w = w;
        b.firstCmd = r.cmds.size();
        r.boxes.push_back(b);
        draws.push_back(BoxDraw());
        return (int)r.boxes.size() - 1;
    }

    bool PaintsBackground(const SNode& n) const {
        if (!n.st.visible) return false;
        if (n.st.background.a == 0 && n.st.backgroundImage.empty() && !n.st.bgGradient) return false;
        if (n.node && n.node == htmlNode) return false;                        // the canvas
        if (n.node && n.node == bodyNode && canvasFromBody) return false;      // moved to the canvas (CSS 2.1 14.2)
        return true;
    }

    float Radius(const ComputedStyle& st, float w, float h) const {
        // one radius for the box (the largest corner), a percentage of the smaller side; at most half the smaller side
        float rad = 0;
        for (float v : st.radius) rad = std::max(rad, v < 0 ? -v * std::min(w, h) / 100.0f : v);
        return std::max(0.0f, std::min(rad, std::min(w, h) * 0.5f));
    }

    // Set a box's final height and draw its borders (called once its content is laid out).
    void FinishBox(int bi, const SNode& n, float h, const float* bw) {
        BoxInfo& b = r.boxes[(std::size_t)bi];
        b.h = Finite(std::max(0.0f, h));
        BoxDraw& d = draws[(std::size_t)bi];
        const float x = b.x, y = b.y, w = b.w, hh = b.h;
        const float rad = Radius(n.st, w, hh);
        if (d.bg >= 0) { r.cmds[(std::size_t)d.bg].h = b.h; r.cmds[(std::size_t)d.bg].radius = rad; }
        if (d.grad >= 0) { r.cmds[(std::size_t)d.grad].h = b.h; r.cmds[(std::size_t)d.grad].radius = rad; }
        if (!n.st.visible) return;
        const bool uniform = bw[0] > 0 && bw[0] == bw[1] && bw[0] == bw[2] && bw[0] == bw[3] && n.st.borderColor[0] == n.st.borderColor[1] &&
                             n.st.borderColor[0] == n.st.borderColor[2] && n.st.borderColor[0] == n.st.borderColor[3];
        if (rad > 0 && uniform) {
            if (n.st.borderColor[0].a == 0) return;
            DrawCmd c;
            c.kind = DrawCmd::Border;
            c.x = x; c.y = y; c.w = w; c.h = hh;
            c.thick = bw[0];
            c.radius = rad;
            c.color = n.st.borderColor[0];
            d.ring = (int)r.cmds.size();
            Emit(c);
            return;
        }
        const float rects[4][4] = {{x, y, w, bw[0]}, {x + w - bw[1], y + bw[0], bw[1], hh - bw[0] - bw[2]},
                                   {x, y + hh - bw[2], w, bw[2]}, {x, y + bw[0], bw[3], hh - bw[0] - bw[2]}};
        for (int k = 0; k < 4; ++k) {
            if (bw[k] <= 0 || n.st.borderColor[k].a == 0) continue;
            DrawCmd c;
            c.kind = DrawCmd::Fill;
            c.x = rects[k][0]; c.y = rects[k][1]; c.w = std::max(0.0f, rects[k][2]); c.h = std::max(0.0f, rects[k][3]);
            c.color = n.st.borderColor[k];
            d.border[k] = (int)r.cmds.size();
            Emit(c);
        }
    }

    // Make a finished box taller (table cells stretched to their row).
    void Stretch(int bi, float h) {
        BoxInfo& b = r.boxes[(std::size_t)bi];
        if (h <= b.h) return;
        const float grow = h - b.h;
        b.h = h;
        BoxDraw& d = draws[(std::size_t)bi];
        if (d.bg >= 0) r.cmds[(std::size_t)d.bg].h = h;
        if (d.grad >= 0) r.cmds[(std::size_t)d.grad].h = h;
        if (d.border[1] >= 0) r.cmds[(std::size_t)d.border[1]].h += grow;
        if (d.border[3] >= 0) r.cmds[(std::size_t)d.border[3]].h += grow;
        if (d.border[2] >= 0) r.cmds[(std::size_t)d.border[2]].y += grow;
        if (d.ring >= 0) r.cmds[(std::size_t)d.ring].h = h;
    }

    // The background image of a box (CSS Backgrounds 3: background-size, -position, -repeat) over its padding box.
    bool MakeBackgroundImage(const ComputedStyle& st, float x, float y, float w, float h, DrawCmd& c) {
        if (st.backgroundImage.empty() || w <= 0 || h <= 0) return false;
        if (std::find(r.images.begin(), r.images.end(), st.backgroundImage) == r.images.end() && r.images.size() < 2048) r.images.push_back(st.backgroundImage);
        int iw = 0, ih = 0;
        if (!imageSize || !imageSize(st.backgroundImage, iw, ih) || iw <= 0 || ih <= 0) return false;
        float tw = (float)iw, th = (float)ih;
        if (st.bgSize == ComputedStyle::BgCover || st.bgSize == ComputedStyle::BgContain) {
            const float sx = w / tw, sy = h / th;
            const float s = st.bgSize == ComputedStyle::BgCover ? std::max(sx, sy) : std::min(sx, sy);
            tw *= s; th *= s;
        } else if (st.bgSize == ComputedStyle::BgExplicit) {
            const bool aw = st.bgW.unit == Length::Auto, ah = st.bgH.unit == Length::Auto;
            if (!aw && !ah) { tw = st.bgW.Resolve(w); th = st.bgH.Resolve(h); }
            else if (!aw) { const float nw = st.bgW.Resolve(w); th = th * nw / tw; tw = nw; }
            else if (!ah) { const float nh = st.bgH.Resolve(h); tw = tw * nh / th; th = nh; }
        }
        if (tw < 1 || th < 1) return false;
        c = DrawCmd();
        c.kind = DrawCmd::BgImage;
        c.x = x; c.y = y; c.w = w; c.h = h;
        c.text = st.backgroundImage;
        c.iw = tw; c.ih = th;
        c.ix = x + st.bgPosX.Resolve(w - tw);   // a percentage places the same point of image and box (CSS Backgrounds 3, 3.6)
        c.iy = y + st.bgPosY.Resolve(h - th);
        c.repeatX = st.bgRepeatX; c.repeatY = st.bgRepeatY;
        c.color = Color{255, 255, 255, 255};
        return true;
    }

    bool EstablishesBfc(const ComputedStyle& st, bool root) const {
        return root || st.floating != Float::None || st.OutOfFlow() || st.Clips() || st.display == Display::FlowRoot ||
               st.display == Display::Table || st.display == Display::TableCell || st.display == Display::TableCaption || IsFlexDisplay(st.display) || IsGridDisplay(st.display) ||
               IsInlineLevel(st.display);
    }

    // A block-level box whose border box starts at (x + margin-left, y) in a containing block cbW wide.
    // Returns the border-box height and the first baseline; `o.boxIndex` gets its entry in r.boxes.
    Out Block(const SNode& n, float x, float y, float cbW, int depth, int ordinal, const BlockOpt& o = BlockOpt()) {
        if (--budget < 0) return Out();
        const ComputedStyle& st = n.st;
        const bool cell = o.cell;
        const bool forcedW = o.borderW >= 0;
        const float* bws = o.borders ? o.borders : st.border;
        const float bl = bws[3], br = bws[1], bt = bws[0], bb = bws[2];
        const float pl = st.padding[3].Resolve(cbW), pr = st.padding[1].Resolve(cbW);
        const float pt = st.padding[0].Resolve(cbW), pb = st.padding[2].Resolve(cbW);
        const float edgesH = bl + br + pl + pr, edgesV = bt + bb + pt + pb;
        const bool autoL = st.margin[3].unit == Length::Auto, autoR = st.margin[1].unit == Length::Auto;
        float ml = cell ? 0.0f : st.margin[3].Resolve(cbW), mr = cell ? 0.0f : st.margin[1].Resolve(cbW);
        const bool replaced = IsReplaced(n);
        float repW = 0, repH = 0;
        if (replaced) SizeReplaced(n, cbW, repW, repH);
        float w;   // content width
        const bool hasWidth = !cell && (st.width.IsSet() || replaced);
        const bool hasMax = !cell && st.maxWidth.IsSet();
        const bool hasMin = !cell && st.minWidth.IsSet() && st.minWidth.Resolve(cbW) > 0;
        float avail = cbW;
        float bx0 = x;
        // a box that makes its own formatting context goes beside the floats (CSS 2.1 9.5)
        if (!forcedW && !cell && !o.noAvoid && fc && !fc->list.empty() && EstablishesBfc(st, false)) {
            float l, rr;
            Band(y, 1.0f, x, cbW, l, rr);
            if (l > x + 0.01f || rr < x + cbW - 0.01f) { bx0 = l; avail = std::max(0.0f, rr - l); }
        }
        if (forcedW) w = o.borderW - edgesH;
        else if (replaced) w = repW;
        else if (hasWidth) w = st.width.Resolve(cbW) - (st.borderBox ? edgesH : 0.0f);
        else w = avail - ml - mr - edgesH;
        if (!forcedW && !replaced) {
            if (hasMax) w = std::min(w, st.maxWidth.Resolve(cbW) - (st.borderBox ? edgesH : 0.0f));
            if (hasMin) w = std::max(w, st.minWidth.Resolve(cbW) - (st.borderBox ? edgesH : 0.0f));
        }
        if (!forcedW && st.display == Display::Table && !(st.tableFixed && hasWidth)) {   // a fixed-layout table keeps its width (17.5.2.1)
            // CSS 2.1 17.5.2: an auto-width table is as wide as its columns want, at most the space there is; never narrower than
            // its columns' minimum
            const auto ti = Intrinsic(n, false);
            if (!hasWidth) w = std::min(w, ti.second - edgesH);
            w = std::max(w, ti.first - edgesH);
        }
        w = std::max(0.0f, Finite(w));
        if (!forcedW && (hasWidth || hasMax || hasMin || st.display == Display::Table)) {
            const float rest = avail - w - edgesH;
            if (autoL && autoR) ml = mr = std::max(0.0f, rest * 0.5f);
            else if (autoL) ml = std::max(0.0f, rest - mr);
        }
        const float bx = Finite(bx0 + ml);
        const float bwid = w + edgesH;
        const Mark boxMark = Here();
        // a positioned box is its own layer (z-index), and the containing block of its absolutely positioned descendants
        const int savedKey = curKey;
        const bool positioned = st.IsPositioned();
        if (positioned && keys.size() < 100000) {
            std::vector<std::pair<int, int>> path = keys[(std::size_t)curKey];
            if (!path.empty()) path.pop_back();
            path.push_back({st.zAuto ? 0 : st.zIndex, 1});
            path.push_back({0, 0});
            keys.push_back(path);
            curKey = (int)keys.size() - 1;
        }
        const bool savedFixed = curFixed;
        if (st.position == Position::Fixed) curFixed = true;
        std::vector<Pending> myAbs;
        if (positioned) cbStack.push_back(&myAbs);

        const int bi = AddBox(n, depth, bx, y, bwid);
        if (o.boxIndex) *o.boxIndex = bi;
        // a block-level link (display: block, a flex or grid item) or label: the whole box and everything in it is the link
        const int savedLink = curLink;
        if (n.node && !n.text && ((n.node->name == "a" && n.node->Attr("href")) || n.node->name == "label")) {
            curLink = (int)r.links.size();
            r.links.push_back(n.node->name == "a" ? *n.node->Attr("href") : std::string());
            r.linkNodes.push_back(n.node);
        }
        if (n.node) {
            if (const std::string* id = n.node->Attr("id")) r.anchors.push_back({*id, y});
            if (n.node->name == "a") if (const std::string* nm = n.node->Attr("name")) r.anchors.push_back({*nm, y});
        }
        // shadows, then the background
        std::vector<std::pair<int, float>> shadows;   // cmd, spread
        if (st.visible)
            for (const BoxShadow& s : st.shadows) {
                if (s.inset || s.color.a == 0) continue;
                DrawCmd c;
                c.kind = DrawCmd::Shadow;
                c.x = bx + s.x - s.spread; c.y = y + s.y - s.spread; c.w = bwid + 2 * s.spread;
                c.blur = s.blur;
                c.color = s.color;
                shadows.push_back({(int)r.cmds.size(), s.spread});
                Emit(c);
            }
        const bool paintsBg = PaintsBackground(n);
        if (paintsBg && st.background.a > 0) {
            DrawCmd c;
            c.kind = DrawCmd::Fill;
            c.x = bx; c.y = y; c.w = bwid; c.color = st.background;
            draws[(std::size_t)bi].bg = (int)r.cmds.size();
            Emit(c);
        }
        if (paintsBg && st.bgGradient) {   // a place for the gradient's colours (made once the page is laid out: Gradients())
            DrawCmd c;
            c.kind = DrawCmd::Fill;
            c.x = bx; c.y = y; c.w = bwid; c.color = Color{255, 255, 255, 255};
            draws[(std::size_t)bi].grad = (int)r.cmds.size();
            Emit(c);
            cmdGrad.back() = (int)grads.size();
            grads.push_back(st.bgGradient);
        }
        int bgImageCmd = -1;
        if (paintsBg && !st.backgroundImage.empty()) {   // a place for it now; filled in once the height is known
            DrawCmd c;
            c.kind = DrawCmd::BgImage;
            bgImageCmd = (int)r.cmds.size();
            Emit(c);
        }
        const float cx = bx + bl + pl, cy = y + bt + pt + o.contentDy;
        // the containing block height for children's % heights
        float defH = -1.0f;
        if (o.borderH >= 0) defH = std::max(0.0f, o.borderH - edgesV);
        else if (st.height.unit == Length::Px || (st.height.IsSet() && o.cbH >= 0)) defH = std::max(0.0f, st.height.Resolve(o.cbH) - (st.borderBox ? edgesV : 0.0f));
        // overflow other than visible: a clip region (and a scroller when it scrolls)
        const int savedClip = curClip;
        int myClip = -1, myScroller = -1;
        if (st.Clips() && !replaced) {
            ClipRect cr;
            cr.parent = curClip;
            cr.fixed = curFixed;
            if (st.Scrolls()) {
                Scroller sc;
                sc.fixed = curFixed;
                myScroller = (int)r.scrollers.size();
                cr.scroller = myScroller;
                r.scrollers.push_back(sc);
            }
            myClip = (int)r.clips.size();
            if (myScroller >= 0) r.scrollers[(std::size_t)myScroller].clip = myClip;
            r.clips.push_back(cr);
            curClip = myClip;
        }
        const Mark contentMark = Here();
        FloatCtx own;
        FloatCtx* savedFc = fc;
        const bool bfc = o.bfc || EstablishesBfc(st, n.node != nullptr && n.node == htmlNode);
        if (bfc) fc = &own;
        // margins that collapse through this box's top and bottom (CSS 2.1 8.3.1): the caller placed the box with its first
        // child's top margins already (EffTop); the last child's bottom margins go out to the caller (Out::esc)
        const bool collapseTop = !bfc && !replaced && CollapsesTop(n, cbW, bt, pt);
        const bool collapseBottom = !bfc && !replaced && bb == 0.0f && pb == 0.0f && o.borderH < 0 && defH < 0 && IsCollapsingContainer(st) &&
                                    !st.height.IsSet() && st.minHeight.Resolve(cbW) <= 0.0f;
        Out content;
        if (replaced) {
            content.h = repH;
            EmitReplaced(n, cx, cy, w, repH);
        } else if (st.display == Display::Table) content = Table(n, cx, cy, w, depth + 1);
        else if (IsFlexDisplay(st.display)) content = Flex(n, cx, cy, w, defH, depth + 1);
        else if (IsGridDisplay(st.display)) content = Grid(n, cx, cy, w, defH, depth + 1);
        else content = Contents(n, cx, cy, w, depth + 1, defH, collapseTop, collapseBottom);
        float ch = content.h;
        if (bfc && !own.list.empty()) ch = std::max(ch, own.Bottom(Clear::Both) - cy);
        fc = savedFc;
        if (o.borderH >= 0) ch = std::max(0.0f, o.borderH - edgesV);
        else if (!replaced) {
            if (defH >= 0) ch = cell ? std::max(ch, defH) : defH;   // a cell's height is a minimum (CSS 2.1 17.5.3)
            if (st.maxHeight.unit == Length::Px || (st.maxHeight.IsSet() && o.cbH >= 0)) ch = std::min(ch, st.maxHeight.Resolve(o.cbH) - (st.borderBox ? edgesV : 0.0f));
            if (st.minHeight.unit == Length::Px || (st.minHeight.IsSet() && o.cbH >= 0)) ch = std::max(ch, st.minHeight.Resolve(o.cbH) - (st.borderBox ? edgesV : 0.0f));
        }
        ch = std::max(0.0f, Finite(ch));
        const float h = edgesV + ch;
        if (st.display == Display::ListItem && st.listStyle != ListStyle::None) {
            const float base = content.baseline >= 0 ? content.baseline : cy + (st.LineHeightPx() - st.fontSize) * 0.5f + st.fontSize * 0.8f;
            Marker(st, cx, base, ordinal);
        }
        if (myClip >= 0) {
            ClipRect& cr = r.clips[(std::size_t)myClip];
            cr.x = bx + bl; cr.y = y + bt; cr.w = std::max(0.0f, bwid - bl - br); cr.h = std::max(0.0f, h - bt - bb);
            if (myScroller >= 0) {
                float right = cr.x, bottom = cy + content.h;   // the content's own height, and anything drawn below it
                for (std::size_t i = contentMark.cmds; i < r.cmds.size(); ++i) {
                    right = std::max(right, r.cmds[i].x + r.cmds[i].w);
                    bottom = std::max(bottom, r.cmds[i].y + r.cmds[i].h);
                }
                Scroller& sc = r.scrollers[(std::size_t)myScroller];
                sc.x = cr.x; sc.y = cr.y; sc.w = cr.w; sc.h = cr.h;
                sc.contentW = right - cr.x + pr;
                sc.contentH = bottom - cr.y + pb;
            }
        }
        curClip = savedClip;
        if (bgImageCmd >= 0) {
            DrawCmd c;
            if (MakeBackgroundImage(st, bx + bl, y + bt, bwid - bl - br, h - bt - bb, c)) {
                c.clip = r.cmds[(std::size_t)bgImageCmd].clip;
                c.fixed = r.cmds[(std::size_t)bgImageCmd].fixed;
                c.radius = Radius(st, bwid, h);
                r.cmds[(std::size_t)bgImageCmd] = c;
            } else r.cmds[(std::size_t)bgImageCmd].w = 0;   // no image (yet): draws nothing
        }
        for (const auto& s : shadows) { r.cmds[(std::size_t)s.first].h = h + 2 * s.second; r.cmds[(std::size_t)s.first].radius = Radius(st, bwid, h); }
        FinishBox(bi, n, h, bws);
        if (curLink != savedLink && curLink >= 0) Area(bx, y, bwid, h, curLink);
        curLink = savedLink;
        if (n.node && n.node->name == "button") {   // a <button> takes clicks like a form control
            ControlBox cbx;
            cbx.node = n.node;
            cbx.x = bx; cbx.y = y; cbx.w = bwid; cbx.h = h;
            cbx.clip = curClip; cbx.fixed = curFixed;
            cbx.font = FontOf(st);
            cbx.color = st.color;
            r.controls.push_back(cbx);
        }
        // the absolutely positioned boxes this one contains, now that its padding box is known
        if (positioned) {
            cbStack.pop_back();
            for (const Pending& p : myAbs) LayAbs(p, bx + bl, y + bt, bwid - bl - br, h - bt - bb);
        }
        Fade(boxMark, st.opacity);
        if (!st.transform.empty()) ApplyTransform(boxMark, st, bx, y, bwid, h);
        if (st.position == Position::Relative || st.position == Position::Sticky) {
            float dx = 0, dy = 0;
            if (st.inset[3].IsSet()) dx = st.inset[3].Resolve(cbW); else if (st.inset[1].IsSet()) dx = -st.inset[1].Resolve(cbW);
            if (st.position == Position::Relative) {
                if (st.inset[0].IsSet()) dy = st.inset[0].Resolve(o.cbH >= 0 ? o.cbH : 0);
                else if (st.inset[2].IsSet()) dy = -st.inset[2].Resolve(o.cbH >= 0 ? o.cbH : 0);
            }
            Shift(boxMark, dx, dy);
        }
        curKey = savedKey;
        curFixed = savedFixed;
        Out out;
        out.h = h;
        out.baseline = content.baseline;
        if (collapseBottom) out.esc = content.esc;
        return out;
    }

    // An image, embedded box or form control that is laid out as a block, at its content box.
    void EmitReplaced(const SNode& n, float x, float y, float w, float h) {
        const Node& el = *n.node;
        if (!n.st.visible) return;
        if (IsControlTag(el.name)) { EmitControl(n, x, y, w, h); return; }
        if (w <= 0 || h <= 0) return;
        DrawCmd c;
        c.kind = el.name == "img" ? DrawCmd::Image : DrawCmd::Embed;
        c.x = x; c.y = y; c.w = w; c.h = h;
        const std::string* src = el.Attr(el.name == "object" ? "data" : "src");
        c.text = src ? *src : std::string();
        c.tag = el.name;
        c.color = n.st.color;
        c.color.a = 255;
        if (c.kind == DrawCmd::Embed) { r.embeds.push_back((int)r.cmds.size()); Emit(c); }
        else EmitImage(c, n);
    }
    void EmitControl(const SNode& n, float x, float y, float w, float h) {
        const ComputedStyle& st = n.st;
        ControlBox cb;
        cb.node = n.node;
        cb.x = x - st.padding[3].Resolve(0) - st.border[3];   // the control's border box
        cb.y = y - st.padding[0].Resolve(0) - st.border[0];
        cb.w = w + EdgesH(st, 0);
        cb.h = h + EdgesV(st, 0);
        cb.clip = curClip;
        cb.fixed = curFixed;
        cb.font = FontOf(st);
        cb.color = st.color;
        DrawCmd c;
        c.kind = DrawCmd::Control;
        c.x = x; c.y = y; c.w = w; c.h = h;
        c.control = (int)r.controls.size();
        c.color = st.color;
        c.font = cb.font;
        c.text = ControlText(n);
        r.controls.push_back(cb);
        Emit(c);
    }

    void Marker(const ComputedStyle& st, float cx, float base, int ordinal) {
        if (!st.visible) return;
        const float fs = st.fontSize;
        DrawCmd c;
        c.color = st.color;
        if (st.listStyle == ListStyle::Disc || st.listStyle == ListStyle::Circle || st.listStyle == ListStyle::Square) {
            const float d = fs * 0.375f;
            c.kind = st.listStyle == ListStyle::Disc ? DrawCmd::Disc : st.listStyle == ListStyle::Circle ? DrawCmd::Circle : DrawCmd::Square;
            c.x = cx - fs * 0.5f - d; c.y = base - fs * 0.3f - d * 0.5f; c.w = d; c.h = d;
        } else {
            c.kind = DrawCmd::Text;
            c.text = MarkerText(st.listStyle, ordinal);
            c.font = FontOf(st);
            c.w = Measure(c.text, st);
            c.h = fs;
            c.x = cx - fs * 0.5f - c.w; c.y = base - fs * 0.8f;
        }
        c.x = Finite(c.x); c.y = Finite(c.y);
        Emit(c);
    }

    // ---- margin collapsing (CSS 2.1 8.3.1) ----

    static bool IsCollapsingContainer(const ComputedStyle& st) { return st.display == Display::Block || st.display == Display::ListItem; }
    // A box whose top margin collapses with its first in-flow child's: a block container that is not a new formatting context,
    // with no top border and no top padding.
    bool CollapsesTop(const SNode& n, float cbW, float bt, float pt) const {
        if (!n.node || n.text || n.node == htmlNode) return false;
        if (!IsCollapsingContainer(n.st) || EstablishesBfc(n.st, false)) return false;
        (void)cbW;
        return bt == 0.0f && pt == 0.0f;
    }
    // A box whose own top and bottom margins adjoin (an empty block: no border, padding, height or content): margins collapse
    // through it.
    // (Both are remembered per box: a hostile page with 100 000 empty siblings deep down asks about them from every level.)
    mutable std::map<const SNode*, bool> emptyCache;
    mutable std::map<std::pair<const SNode*, int>, MarginSet> effTopCache;
    bool EmptyThrough(const SNode& c, float cw, int depth) const {
        if (c.text || depth > 64 || IsReplaced(c) || !IsCollapsingContainer(c.st) || EstablishesBfc(c.st, false)) return false;
        const auto hit = emptyCache.find(&c);
        if (hit != emptyCache.end()) return hit->second;
        const bool e = EmptyThroughUncached(c, cw, depth);
        if (emptyCache.size() < 500000) emptyCache[&c] = e;
        return e;
    }
    bool EmptyThroughUncached(const SNode& c, float cw, int depth) const {
        const ComputedStyle& st = c.st;
        if (st.display == Display::ListItem && st.listStyle != ListStyle::None) return false;
        if (st.border[0] != 0 || st.border[2] != 0 || st.padding[0].Resolve(cw) != 0 || st.padding[2].Resolve(cw) != 0) return false;
        if (st.height.IsSet() && st.height.Resolve(0) > 0) return false;
        if (st.minHeight.Resolve(cw) > 0) return false;
        for (const SNode& k : c.kids) {
            if (k.text) { if (k.st.whiteSpace == WhiteSpace::Pre || k.st.whiteSpace == WhiteSpace::PreWrap || !OnlySpaceText(k.node->data)) return false; continue; }
            if (k.st.OutOfFlow()) continue;
            if (!IsBlockLevel(k) || !EmptyThrough(k, cw, depth + 1)) return false;
        }
        return true;
    }
    // The margins that collapse into a block child's top margin before it is laid out: its own, and, when its top collapses with
    // its first child's, that child's (and so on down; empty boxes on the way add their bottom margins too).
    MarginSet EffTop(const SNode& k, float cw, int depth) const {
        const auto key = std::make_pair(&k, (int)std::lround(Finite(cw) * 4));
        const auto hit = effTopCache.find(key);
        if (hit != effTopCache.end()) return hit->second;
        const MarginSet m = EffTopUncached(k, cw, depth);
        if (effTopCache.size() < 500000) effTopCache[key] = m;
        return m;
    }
    MarginSet EffTopUncached(const SNode& k, float cw, int depth) const {
        MarginSet m;
        m.Add(k.st.margin[0].Resolve(cw));
        if (depth > 64 || IsReplaced(k) || !CollapsesTop(k, cw, k.st.border[0], k.st.padding[0].Resolve(cw))) return m;
        float inner = cw - MarginsH(k.st, cw) - EdgesH(k.st, cw);
        if (k.st.width.IsSet()) inner = k.st.width.Resolve(cw) - (k.st.borderBox ? EdgesH(k.st, cw) : 0.0f);
        inner = std::max(0.0f, inner);
        for (const SNode& c : k.kids) {
            if (c.text) {
                if (c.st.whiteSpace != WhiteSpace::Pre && c.st.whiteSpace != WhiteSpace::PreWrap && OnlySpaceText(c.node->data)) continue;
                return m;
            }
            if (c.st.OutOfFlow() || c.st.floating != Float::None) continue;
            if (!IsBlockLevel(c)) return m;   // a line box ends it
            if (c.st.clear != Clear::None) return m;
            m.Add(EffTop(c, inner, depth + 1));
            if (!EmptyThrough(c, inner, depth + 1)) return m;
            m.Add(c.st.margin[2].Resolve(inner));
        }
        return m;
    }

    // The content of a block container: block children (with anonymous blocks around inline runs) or inline content.
    // `collapseTop`: the box's top collapses with its first child's (the caller has placed it below those margins already);
    // `collapseBottom`: the last child's bottom margins are not added here but handed out in Out::esc.
    Out Contents(const SNode& n, float cx, float cy, float cw, int depth, float cbH = -1.0f, bool collapseTop = false, bool collapseBottom = false) {
        Out o;
        bool anyBlock = false;
        for (const SNode& k : n.kids) if (IsBlockLevel(k)) { anyBlock = true; break; }
        if (!anyBlock) {
            std::vector<const SNode*> items;
            for (const SNode& k : n.kids) items.push_back(&k);
            float base = -1;
            o.h = InlineContent(items, n.st, cx, cy, cw, base, depth);
            o.baseline = base;
            return o;
        }
        float y = cy;
        MarginSet pending;        // margins collapsing with whatever comes next
        bool atTop = true;        // nothing in flow has been placed yet
        auto pendingNow = [&]() { return (atTop && collapseTop) ? 0.0f : pending.Value(); };   // at the top they went out already
        // list numbering (CSS 2.1 12.4 / the HTML standard's ordinal values)
        int counter = 1;
        if (n.node && n.node->name == "ol") counter = AttrInt(*n.node, "start", 1);
        std::size_t i = 0;
        while (i < n.kids.size()) {
            if (!IsBlockLevel(n.kids[i])) {
                std::vector<const SNode*> items;
                while (i < n.kids.size() && !IsBlockLevel(n.kids[i])) items.push_back(&n.kids[i++]);
                if (OnlyCollapsibleSpace(items)) continue;
                // only floats and positioned boxes (and space): placed without a line box of their own
                bool onlyOut = true;
                for (const SNode* s : items) if (s->text ? !OnlySpaceText(s->node->data) : (s->st.floating == Float::None && !s->st.OutOfFlow())) onlyOut = false;
                if (onlyOut) {
                    for (const SNode* s : items) {
                        if (s->text) continue;
                        if (s->st.OutOfFlow()) DeferAbs(*s, cx, y + pendingNow(), depth);
                        else LayFloat(*s, cx, y + pendingNow(), cw, depth);
                    }
                    continue;
                }
                y += pendingNow(); pending = MarginSet(); atTop = false;
                SNode anon;
                anon.st = n.st;   // inherits the text properties; no box of its own to draw
                const int bi = AddBox(anon, depth, cx, y, cw);
                float base = -1;
                const float h = InlineContent(items, n.st, cx, y, cw, base, depth);
                r.boxes[(std::size_t)bi].h = h;
                if (o.baseline < 0) o.baseline = base;
                y += h;
                continue;
            }
            const SNode& k = n.kids[i++];
            const float mb = k.st.margin[2].Resolve(cw);
            MarginSet m = pending;
            m.Add(EffTop(k, cw, 0));
            const float before = (atTop && collapseTop) ? 0.0f : m.Value();
            y += before;
            if (k.st.clear != Clear::None && fc) {
                const float clearY = fc->Bottom(k.st.clear);
                if (clearY > y) y = clearY;   // clearance (CSS 2.1 9.5.2, simplified: the border edge goes below the floats)
            }
            int ordinal = 0;
            if (k.st.display == Display::ListItem) {
                if (k.node) counter = AttrInt(*k.node, "value", counter);
                ordinal = counter;
                counter = counter < 1000000000 ? counter + 1 : counter;
            }
            BlockOpt bo;
            bo.cbH = cbH;
            const Out ko = Block(k, cx, y, cw, depth, ordinal, bo);
            if (o.baseline < 0) o.baseline = ko.baseline;
            if (ko.h <= 0.01f && k.st.clear == Clear::None && EmptyThrough(k, cw, 0)) {
                // its top and bottom margins adjoin: they collapse with the margins around it (8.3.1)
                y -= before;
                pending = m;
                pending.Add(mb);
                pending.Add(ko.esc);
                continue;
            }
            atTop = false;
            y += ko.h;
            pending = MarginSet();
            pending.Add(mb);
            pending.Add(ko.esc);
        }
        if (collapseBottom) o.esc = pending;
        else y += pendingNow();
        o.h = y - cy;
        return o;
    }

    static bool OnlyCollapsibleSpace(const std::vector<const SNode*>& items) {
        for (const SNode* s : items) {
            if (!s->text) return false;
            if (s->st.whiteSpace == WhiteSpace::Pre || s->st.whiteSpace == WhiteSpace::PreWrap) return false;
            if (!OnlySpaceText(s->node->data)) return false;
        }
        return true;
    }

    // ---- tables (CSS 2.1 chapter 17) ----

    struct TCell { const SNode* sn = nullptr; int row = 0, col = 0, rows = 1, cols = 1; };
    struct TModel {
        std::vector<const SNode*> captions;
        std::vector<const SNode*> rows;        // in drawing order: header groups first, footer groups last (17.2)
        std::vector<const SNode*> rowGroup;    // each row's group (null: directly in the table)
        std::vector<TCell> cells;
        int cols = 0;
        std::vector<float> colWidth;           // from <col> / <colgroup> (< 0: none); px
        std::vector<float> colPct;             // from <col> (< 0: none)
    };
    // The table's grid: rows from the row groups in order, cells in slots with colspan / rowspan (the HTML standard's table model,
    // 4.9.12, the parts layout needs: a cell takes the first free slot of its row; rowspan 0 reaches the end of its group).
    // (remembered per table: a table is measured and laid out more than once)
    std::map<const SNode*, std::shared_ptr<const TModel>> tableCache;
    const TModel& BuildTable(const SNode& n) {
        auto it = tableCache.find(&n);
        if (it != tableCache.end()) return *it->second;
        auto made = std::make_shared<const TModel>(BuildTableUncached(n));
        tableCache[&n] = made;
        return *made;
    }
    TModel BuildTableUncached(const SNode& n) const {
        TModel t;
        std::vector<const SNode*> head, body, foot;
        std::vector<const SNode*> headG, bodyG, footG;
        for (const SNode& k : n.kids) {
            if (k.text || k.st.OutOfFlow() || k.st.floating != Float::None) continue;
            if (k.st.display == Display::TableCaption) { t.captions.push_back(&k); continue; }
            if (k.st.display == Display::TableRow) { body.push_back(&k); bodyG.push_back(nullptr); continue; }
            if (!IsRowGroup(k.st.display)) continue;
            auto& list = k.st.display == Display::TableHeaderGroup && head.empty() ? head : k.st.display == Display::TableFooterGroup && foot.empty() ? foot : body;
            auto& groups = &list == &head ? headG : &list == &foot ? footG : bodyG;
            for (const SNode& g : k.kids) if (!g.text && g.st.display == Display::TableRow) { list.push_back(&g); groups.push_back(&k); }
        }
        for (std::size_t i = 0; i < head.size(); ++i) { t.rows.push_back(head[i]); t.rowGroup.push_back(headG[i]); }
        for (std::size_t i = 0; i < body.size(); ++i) { t.rows.push_back(body[i]); t.rowGroup.push_back(bodyG[i]); }
        for (std::size_t i = 0; i < foot.size(); ++i) { t.rows.push_back(foot[i]); t.rowGroup.push_back(footG[i]); }
        const std::size_t maxCols = 1000, maxSlots = 1000000;
        if (t.rows.size() > maxSlots) { t.rows.resize(maxSlots); t.rowGroup.resize(maxSlots); }
        std::vector<std::vector<unsigned char>> used(t.rows.size());
        for (std::size_t ri = 0; ri < t.rows.size(); ++ri) {
            std::size_t c = 0;
            // where this row's group ends (rowspan 0, and no span past it)
            std::size_t groupEnd = ri + 1;
            while (groupEnd < t.rows.size() && t.rowGroup[groupEnd] == t.rowGroup[ri]) ++groupEnd;   // rows straight in the table: one group
            for (const SNode& cell : t.rows[ri]->kids) {
                if (cell.text || cell.st.display != Display::TableCell) continue;
                while (c < used[ri].size() && used[ri][c]) ++c;
                if (c >= maxCols) break;
                int cs = cell.node ? AttrInt(*cell.node, "colspan", 1) : 1;
                int rs = cell.node ? AttrInt(*cell.node, "rowspan", 1) : 1;
                cs = std::max(1, std::min(cs, 1000));
                if (rs <= 0) rs = (int)(groupEnd - ri);
                rs = std::max(1, std::min(rs, 65534));
                rs = (int)std::min<std::size_t>((std::size_t)rs, std::max<std::size_t>(1, groupEnd - ri));
                if (c + (std::size_t)cs > maxCols) cs = (int)(maxCols - c);
                if ((std::size_t)(t.cols > (int)(c + cs) ? t.cols : (int)(c + cs)) * t.rows.size() > maxSlots) break;
                TCell tc;
                tc.sn = &cell; tc.row = (int)ri; tc.col = (int)c; tc.rows = rs; tc.cols = cs;
                t.cells.push_back(tc);
                for (std::size_t rr = ri; rr < ri + (std::size_t)rs; ++rr) {
                    if (used[rr].size() < c + (std::size_t)cs) used[rr].resize(c + (std::size_t)cs, 0);
                    for (std::size_t cc = c; cc < c + (std::size_t)cs; ++cc) used[rr][cc] = 1;
                }
                t.cols = std::max(t.cols, (int)(c + (std::size_t)cs));
                c += (std::size_t)cs;
            }
        }
        // <col span width> and <colgroup span width> (not in the styled tree: they are display: none here)
        t.colWidth.assign((std::size_t)t.cols, -1.0f);
        t.colPct.assign((std::size_t)t.cols, -1.0f);
        if (n.node) {
            std::size_t c = 0;
            auto colEl = [&](const Node& el) {
                int span = std::max(1, std::min(AttrInt(el, "span", 1), 1000));
                float px = -1, pct = -1;
                if (const std::string* w = el.Attr("width")) {
                    char* e = nullptr;
                    const float v = std::strtof(w->c_str(), &e);
                    if (e != w->c_str() && std::isfinite(v) && v >= 0) { if (*e == '%') pct = std::min(v, 100.0f); else px = std::min(v, 100000.0f); }
                }
                if (const std::string* stl = el.Attr("style"))
                    for (const Declaration& d : ParseDeclarations(*stl)) {
                        if (d.property != "width") continue;
                        const char* b2 = d.value.c_str();
                        char* e = nullptr;
                        const float v = std::strtof(b2, &e);
                        if (e == b2 || !std::isfinite(v) || v < 0) continue;
                        if (*e == '%') pct = std::min(v, 100.0f); else if (std::string(e) == "px" || *e == 0) px = std::min(v, 100000.0f);
                    }
                for (int k = 0; k < span && c < (std::size_t)t.cols; ++k, ++c) { t.colWidth[c] = px; t.colPct[c] = pct; }
            };
            for (const auto& ch : n.node->children) {
                if (ch->IsElement("col")) colEl(*ch);
                else if (ch->IsElement("colgroup")) {
                    bool hasCols = false;
                    for (const auto& g : ch->children) if (g->IsElement("col")) { hasCols = true; colEl(*g); }
                    if (!hasCols) colEl(*ch);
                }
                if (c >= (std::size_t)t.cols) break;
            }
        }
        return t;
    }

    // The columns' minimum and maximum widths (CSS 2.1 17.5.2.2, the automatic table layout; cells' border boxes, spacing not
    // included): from the cells that span one column, then the spanning cells' widths spread over the columns they span.
    void ColumnWidths(const TModel& t, float spacingH, std::vector<float>& mn, std::vector<float>& mx, std::vector<float>& pct) {
        const std::size_t nc = (std::size_t)t.cols;
        mn.assign(nc, 0.0f); mx.assign(nc, 0.0f); pct.assign(nc, -1.0f);
        for (std::size_t c = 0; c < nc; ++c) {
            if (t.colWidth[c] >= 0) { mn[c] = mx[c] = t.colWidth[c]; }
            if (t.colPct[c] >= 0) pct[c] = t.colPct[c];
        }
        auto cellWidths = [&](const TCell& tc, float& a, float& b, float& p) {
            const auto ci = Intrinsic(*tc.sn);
            a = ci.first; b = ci.second; p = -1;
            const Length& w = tc.sn->st.width;
            if (w.unit == Length::Percent) p = w.value;
            else if (w.unit == Length::Px) b = std::max(a, w.value + (tc.sn->st.borderBox ? 0.0f : EdgesH(tc.sn->st, 0)));
        };
        for (const TCell& tc : t.cells) {
            if (tc.cols != 1) continue;
            float a, b, p;
            cellWidths(tc, a, b, p);
            const std::size_t c = (std::size_t)tc.col;
            mn[c] = std::max(mn[c], a);
            mx[c] = std::max(mx[c], b);
            if (p >= 0) pct[c] = std::max(pct[c], p);
        }
        for (const TCell& tc : t.cells) {
            if (tc.cols == 1) continue;
            float a, b, p;
            cellWidths(tc, a, b, p);
            const std::size_t c0 = (std::size_t)tc.col, c1 = std::min(nc, c0 + (std::size_t)tc.cols);
            const float gaps = spacingH * (float)(c1 - c0 - 1);
            float sMin = gaps, sMax = gaps;
            for (std::size_t c = c0; c < c1; ++c) { sMin += mn[c]; sMax += mx[c]; }
            // more than the columns have: the rest spread in proportion to their maximum widths (equally when those are 0)
            auto spread = [&](std::vector<float>& v, float need, float have) {
                if (need <= have) return;
                float weight = 0;
                for (std::size_t c = c0; c < c1; ++c) weight += mx[c];
                for (std::size_t c = c0; c < c1; ++c) v[c] += (need - have) * (weight > 0 ? mx[c] / weight : 1.0f / (float)(c1 - c0));
            };
            spread(mn, a, sMin);
            spread(mx, b, sMax);
            for (std::size_t c = c0; c < c1; ++c) mx[c] = std::max(mx[c], mn[c]);
        }
    }
    float Spacing(const ComputedStyle& st, bool horizontal) const {
        if (st.borderCollapse) return 0.0f;
        return horizontal ? st.borderSpacingH : st.borderSpacingV;
    }
    // A table's min-content and max-content widths (the content box): columns, spacing and captions.
    void TableIntrinsic(const SNode& n, float& mn, float& mx) {
        const TModel& t = BuildTable(n);
        mn = mx = 0;
        if (t.cols > 0) {
            std::vector<float> cmin, cmax, cpct;
            const float hs = Spacing(n.st, true);
            ColumnWidths(t, hs, cmin, cmax, cpct);
            const float gaps = hs * (float)(t.cols + 1);
            mn = gaps; mx = gaps;
            for (int c = 0; c < t.cols; ++c) { mn += cmin[(std::size_t)c]; mx += cmax[(std::size_t)c]; }
        }
        for (const SNode* cap : t.captions) { const auto ci = Intrinsic(*cap); mn = std::max(mn, ci.first + MarginsH(cap->st, 0)); }
        mx = std::max(mx, mn);
    }

    // A table laid out in a content box `cw` wide: captions, then the rows with real column widths (the automatic or the fixed
    // table layout, 17.5.2), row heights from their cells (17.5.3), cell vertical alignment (17.5.4), border-spacing or collapsed
    // borders (17.6, the collapsing model simplified: a cell's left and top borders give way to its neighbour's).
    Out Table(const SNode& n, float cx, float cy, float cw, int depth) {
        Out o;
        const TModel& t = BuildTable(n);
        const ComputedStyle& ts = n.st;
        const float hs = Spacing(ts, true), vs = Spacing(ts, false);
        float y = cy;
        auto captions = [&](bool bottom) {
            for (const SNode* cap : t.captions) {
                if (cap->st.captionBottom != bottom) continue;
                const float mt = cap->st.margin[0].Resolve(cw), mb = cap->st.margin[2].Resolve(cw);
                BlockOpt bo;
                bo.noAvoid = true;
                const Out co = Block(*cap, cx, y + mt, cw, depth, 0, bo);
                y += mt + co.h + mb;
            }
        };
        captions(false);
        const std::size_t nc = (std::size_t)t.cols, nr = t.rows.size();
        if (nc == 0 || nr == 0) { captions(true); o.h = y - cy; return o; }
        // column widths
        std::vector<float> colW(nc, 0.0f);
        const float avail = std::max(0.0f, cw - hs * (float)(nc + 1));
        if (ts.tableFixed && ts.width.IsSet()) {
            // 17.5.2.1: <col> widths, then the first row's cells; the other columns share what is left equally
            std::vector<bool> set(nc, false);
            for (std::size_t c = 0; c < nc; ++c) {
                if (t.colWidth[c] >= 0) { colW[c] = t.colWidth[c]; set[c] = true; }
                else if (t.colPct[c] >= 0) { colW[c] = t.colPct[c] * avail / 100.0f; set[c] = true; }
            }
            for (const TCell& tc : t.cells) {
                if (tc.row != 0) continue;
                const Length& w = tc.sn->st.width;
                if (!w.IsSet()) continue;
                const float bw = w.Resolve(cw) + (tc.sn->st.borderBox ? 0.0f : EdgesH(tc.sn->st, cw)) - hs * (float)(tc.cols - 1);
                for (int k = 0; k < tc.cols; ++k) {
                    const std::size_t c = (std::size_t)(tc.col + k);
                    if (c < nc && !set[c]) { colW[c] = std::max(0.0f, bw / (float)tc.cols); set[c] = true; }
                }
            }
            float used = 0;
            std::size_t unset = 0;
            for (std::size_t c = 0; c < nc; ++c) { if (set[c]) used += colW[c]; else ++unset; }
            if (unset > 0) { const float each = std::max(0.0f, (avail - used) / (float)unset); for (std::size_t c = 0; c < nc; ++c) if (!set[c]) colW[c] = each; }
            else if (used < avail && used > 0) for (std::size_t c = 0; c < nc; ++c) colW[c] += (avail - used) * colW[c] / used;
        } else {
            // 17.5.2.2 (the algorithm is not normative; this is the common one): every column at least its minimum; percentage
            // columns get their share; the rest goes in proportion to the maximum widths, or between minimum and maximum
            std::vector<float> mn, mx, pct;
            ColumnWidths(t, hs, mn, mx, pct);
            float left = avail;
            std::vector<bool> done(nc, false);
            for (std::size_t c = 0; c < nc; ++c)
                if (pct[c] >= 0) { colW[c] = std::max(mn[c], pct[c] * avail / 100.0f); left -= colW[c]; done[c] = true; }
            float sMin = 0, sMax = 0;
            for (std::size_t c = 0; c < nc; ++c) if (!done[c]) { sMin += mn[c]; sMax += mx[c]; }
            left = std::max(0.0f, left);
            for (std::size_t c = 0; c < nc; ++c) {
                if (done[c]) continue;
                if (left <= sMin) colW[c] = mn[c];
                else if (left <= sMax) colW[c] = mn[c] + (mx[c] - mn[c]) * (sMax - sMin > 0 ? (left - sMin) / (sMax - sMin) : 0.0f);
                else {
                    std::size_t open = 0;
                    for (std::size_t k = 0; k < nc; ++k) if (!done[k]) ++open;
                    colW[c] = mx[c] + (left - sMax) * (sMax > 0 ? mx[c] / sMax : 1.0f / (float)std::max<std::size_t>(1, open));
                }
            }
        }
        for (float& w : colW) w = std::max(0.0f, Finite(w));
        std::vector<float> colX(nc + 1, 0.0f);
        colX[0] = cx + hs;
        for (std::size_t c = 0; c < nc; ++c) colX[c + 1] = colX[c] + colW[c] + hs;
        auto cellWidth = [&](const TCell& tc) {
            const std::size_t c1 = std::min(nc, (std::size_t)(tc.col + tc.cols));
            return colX[c1] - hs - colX[(std::size_t)tc.col];
        };
        // the borders a cell draws in the collapsing model: its left and top give way to the cell before it / above it, and to
        // the table's own border at the table's edge
        auto cellBorders = [&](const TCell& tc, float* b) {
            const float* sb = tc.sn->st.border;
            for (int k = 0; k < 4; ++k) b[k] = sb[k];
            if (!ts.borderCollapse) return;
            if (tc.col > 0 || ts.border[3] > 0) b[3] = 0;
            if (tc.row > 0 || ts.border[0] > 0) b[0] = 0;
            if ((std::size_t)(tc.col + tc.cols) >= nc && ts.border[1] > 0) b[1] = 0;
            if ((std::size_t)(tc.row + tc.rows) >= nr && ts.border[2] > 0) b[2] = 0;
        };
        // row heights: each cell measured at its width (17.5.3)
        std::vector<float> rowH(nr, 0.0f), cellH(t.cells.size(), 0.0f), cellBase(t.cells.size(), -1.0f), rowBase(nr, -1.0f);
        std::vector<std::array<float, 4>> borders(t.cells.size());
        for (std::size_t ri = 0; ri < nr; ++ri)
            if (t.rows[ri]->st.height.unit == Length::Px) rowH[ri] = std::max(0.0f, t.rows[ri]->st.height.value);
        for (std::size_t i = 0; i < t.cells.size(); ++i) {
            const TCell& tc = t.cells[i];
            cellBorders(tc, borders[i].data());
            BlockOpt bo;
            bo.cell = true;
            bo.noAvoid = true;
            bo.borderW = cellWidth(tc);
            bo.borders = borders[i].data();
            const auto hb = MeasureBlock(*tc.sn, bo.borderW, bo);
            cellH[i] = hb.first;
            cellBase[i] = hb.second;
            if (tc.sn->st.verticalAlign == VAlign::Baseline && tc.rows == 1 && hb.second >= 0) rowBase[(std::size_t)tc.row] = std::max(rowBase[(std::size_t)tc.row], hb.second);
        }
        for (std::size_t i = 0; i < t.cells.size(); ++i) {   // one-row cells set the heights; baseline cells sit on the row's baseline
            const TCell& tc = t.cells[i];
            if (tc.rows != 1) continue;
            float h = cellH[i];
            if (tc.sn->st.verticalAlign == VAlign::Baseline && cellBase[i] >= 0 && rowBase[(std::size_t)tc.row] >= 0) h += rowBase[(std::size_t)tc.row] - cellBase[i];
            rowH[(std::size_t)tc.row] = std::max(rowH[(std::size_t)tc.row], h);
        }
        for (std::size_t i = 0; i < t.cells.size(); ++i) {   // spanning cells: a taller one makes its last row taller
            const TCell& tc = t.cells[i];
            if (tc.rows == 1) continue;
            const std::size_t r0 = (std::size_t)tc.row, r1 = std::min(nr, r0 + (std::size_t)tc.rows);
            float have = vs * (float)(r1 - r0 - 1);
            for (std::size_t ri = r0; ri < r1; ++ri) have += rowH[ri];
            if (cellH[i] > have) rowH[r1 - 1] += cellH[i] - have;
        }
        std::vector<float> rowY(nr + 1, 0.0f);
        rowY[0] = y + vs;
        for (std::size_t ri = 0; ri < nr; ++ri) rowY[ri + 1] = rowY[ri] + Finite(rowH[ri]) + vs;
        // row group and row backgrounds under the cells (17.5.1, layers 3 and 5)
        const float rowX = colX[0], rowW = std::max(0.0f, colX[nc] - hs - colX[0]);
        const SNode* lastGroup = nullptr;
        std::size_t nextCell = 0;
        for (std::size_t ri = 0; ri < nr; ++ri) {
            const SNode* g = t.rowGroup[ri];
            if (g && g != lastGroup && g->st.background.a > 0 && g->st.visible) {
                std::size_t r1 = ri;
                while (r1 + 1 < nr && t.rowGroup[r1 + 1] == g) ++r1;
                DrawCmd c;
                c.kind = DrawCmd::Fill;
                c.x = rowX; c.y = rowY[ri]; c.w = rowW; c.h = rowY[r1 + 1] - vs - rowY[ri];
                c.color = g->st.background;
                Emit(c);
            }
            lastGroup = g;
            const int rowBox = AddBox(*t.rows[ri], depth, rowX, rowY[ri], rowW);
            r.boxes[(std::size_t)rowBox].h = rowH[ri];
            if (t.rows[ri]->st.background.a > 0 && t.rows[ri]->st.visible) {
                DrawCmd c;
                c.kind = DrawCmd::Fill;
                c.x = rowX; c.y = rowY[ri]; c.w = rowW; c.h = rowH[ri];
                c.color = t.rows[ri]->st.background;
                Emit(c);
            }
            // the cells are in row order (BuildTable): this row's are the next ones
            for (; nextCell < t.cells.size() && (std::size_t)t.cells[nextCell].row == ri; ++nextCell) {
                const std::size_t i = nextCell;
                const TCell& tc = t.cells[i];
                const std::size_t r1 = std::min(nr, ri + (std::size_t)tc.rows);
                const float h = rowY[r1] - vs - rowY[ri];
                float dy = 0;
                switch (tc.sn->st.verticalAlign) {
                case VAlign::Middle: dy = (h - cellH[i]) * 0.5f; break;
                case VAlign::Bottom: case VAlign::TextBottom: dy = h - cellH[i]; break;
                case VAlign::Baseline: if (cellBase[i] >= 0 && rowBase[ri] >= 0 && tc.rows == 1) dy = rowBase[ri] - cellBase[i]; break;
                default: break;
                }
                BlockOpt bo;
                bo.cell = true;
                bo.noAvoid = true;
                bo.borderW = cellWidth(tc);
                bo.borderH = h;
                bo.contentDy = std::max(0.0f, Finite(dy));
                bo.borders = borders[i].data();
                const Out co = Block(*tc.sn, colX[(std::size_t)tc.col], rowY[ri], bo.borderW, depth + 1, 0, bo);
                if (o.baseline < 0) o.baseline = co.baseline;
            }
        }
        y = rowY[nr];
        captions(true);
        o.h = y - cy;
        return o;
    }

    // The height (and first baseline) a box gets at a border-box width (and height): laid out and taken back again (cached).
    std::pair<float, float> MeasureBlock(const SNode& k, float cbW, const BlockOpt& o) {
        const auto key = std::make_tuple(&k, (int)std::lround(o.borderW * 4), (int)std::lround(o.borderH * 4), (int)std::lround(cbW * 4),
                                         (int)std::lround(o.cbH * 4));
        auto it = heightCache.find(key);
        if (it != heightCache.end()) return it->second;
        const Mark m = Here();
        FloatCtx none;
        FloatCtx* savedFc = fc;
        fc = &none;
        BlockOpt mo = o;
        mo.boxIndex = nullptr;
        const Out out = Block(k, 0, 0, cbW, 0, 0, mo);
        fc = savedFc;
        Rollback(m);
        const std::pair<float, float> res{out.h, out.baseline};
        if (heightCache.size() < 200000) heightCache[key] = res;
        return res;
    }

    // ---- flexbox (CSS Flexible Box Layout 1, section 9) ----

    Out Flex(const SNode& n, float cx, float cy, float cw, float ch, int depth) {
        const ComputedStyle& cs = n.st;
        const bool row = cs.flexDirection == FlexDir::Row || cs.flexDirection == FlexDir::RowReverse;
        const bool reverse = cs.flexDirection == FlexDir::RowReverse || cs.flexDirection == FlexDir::ColumnReverse;
        const bool wrap = cs.flexWrap != FlexWrap::NoWrap;
        const float mainGap = row ? cs.columnGap.Resolve(cw) : cs.rowGap.Resolve(cw);
        const float crossGap = row ? cs.rowGap.Resolve(cw) : cs.columnGap.Resolve(cw);
        const float mainSize = row ? cw : (ch >= 0 ? ch : kInf);
        struct Item {
            const SNode* sn = nullptr;
            float base = 0, hypo = 0, minM = 0, maxM = kInf, target = 0;   // main sizes of the border box
            float mStart = 0, mEnd = 0, cStart = 0, cEnd = 0;             // margins (auto = 0)
            bool autoMStart = false, autoMEnd = false, autoCStart = false, autoCEnd = false;
            float crossBorder = -1;   // column items: the border-box width it is laid out with (< 0: from its style)
            float cross = 0;          // its border-box cross size
            float baseline = -1;
            bool frozen = false;
            Align align = Align::Stretch;
        };
        std::vector<Item> items;
        for (const SNode& k : n.kids) {
            if (k.text) continue;
            if (k.st.OutOfFlow()) { DeferAbs(k, cx, cy, depth); continue; }
            Item it;
            it.sn = &k;
            items.push_back(it);
        }
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.sn->st.order < b.sn->st.order; });
        Out out;
        if (items.empty()) { out.h = ch >= 0 ? ch : 0.0f; return out; }
        const float pctBase = row ? cw : std::max(0.0f, ch);
        for (Item& it : items) {
            const ComputedStyle& st = it.sn->st;
            const int ms = row ? 3 : 0, me = row ? 1 : 2, cS = row ? 0 : 3, cE = row ? 2 : 1;
            it.mStart = st.margin[ms].Resolve(cw); it.mEnd = st.margin[me].Resolve(cw);
            it.cStart = st.margin[cS].Resolve(cw); it.cEnd = st.margin[cE].Resolve(cw);
            it.autoMStart = st.margin[ms].unit == Length::Auto; it.autoMEnd = st.margin[me].unit == Length::Auto;
            it.autoCStart = st.margin[cS].unit == Length::Auto; it.autoCEnd = st.margin[cE].unit == Length::Auto;
            Align a = st.alignSelf == Align::Auto ? cs.alignItems : st.alignSelf;
            if (a == Align::Normal || a == Align::Auto) a = Align::Stretch;
            it.align = a;
            const float edgesM = row ? EdgesH(st, cw) : EdgesV(st, cw);
            const Length& sizeM = row ? st.width : st.height;
            const Length& sizeC = row ? st.height : st.width;
            const auto intr = Intrinsic(*it.sn);
            if (!row) {   // the width a column item is laid out at
                if (sizeC.IsSet() || IsReplaced(*it.sn)) it.crossBorder = -1;
                else if (it.align == Align::Stretch && !it.autoCStart && !it.autoCEnd) it.crossBorder = std::max(0.0f, cw - it.cStart - it.cEnd);
                else it.crossBorder = ShrinkToFit(*it.sn, std::max(0.0f, cw - it.cStart - it.cEnd));
            }
            auto contentMain = [&]() -> float {
                if (row) return intr.second;
                BlockOpt mo;
                mo.borderW = it.crossBorder;
                mo.noAvoid = true; mo.bfc = true;
                return MeasureBlock(*it.sn, cw, mo).first;
            };
            const bool definiteMain = row || ch >= 0;
            const Length& basis = st.flexBasis;
            if (basis.IsSet() && (!basis.HasPercent() || definiteMain)) it.base = basis.Resolve(pctBase) + (st.borderBox ? 0.0f : edgesM);
            else if (sizeM.IsSet() && (!sizeM.HasPercent() || definiteMain)) it.base = sizeM.Resolve(pctBase) + (st.borderBox ? 0.0f : edgesM);
            else if (IsReplaced(*it.sn)) { float w, h; SizeReplaced(*it.sn, cw, w, h); it.base = (row ? w : h) + edgesM; }
            else it.base = contentMain();
            it.base = std::max(it.base, edgesM);
            // min and max main sizes (the automatic minimum: the content's min-content size, CSS Flexbox 4.5)
            const bool minAuto = row ? st.minWidthAuto : st.minHeightAuto;
            const Length& minL = row ? st.minWidth : st.minHeight;
            const Length& maxL = row ? st.maxWidth : st.maxHeight;
            if (!minAuto) it.minM = minL.Resolve(pctBase) + (st.borderBox ? 0.0f : edgesM);
            else if ((row ? st.overflowX : st.overflowY) != Overflow::Visible) it.minM = edgesM;
            else {
                float contentMin = row ? Intrinsic(*it.sn, false).first : contentMain();
                if (sizeM.IsSet() && (!sizeM.HasPercent() || definiteMain)) contentMin = std::min(contentMin, sizeM.Resolve(pctBase) + (st.borderBox ? 0.0f : edgesM));
                it.minM = contentMin;
            }
            if (maxL.IsSet() && (!maxL.HasPercent() || definiteMain)) it.maxM = maxL.Resolve(pctBase) + (st.borderBox ? 0.0f : edgesM);
            it.minM = std::max(it.minM, edgesM);
            if (it.maxM < it.minM) it.maxM = it.minM;
            it.hypo = std::max(it.minM, std::min(it.maxM, it.base));
        }
        // lines (9.3)
        std::vector<std::pair<std::size_t, std::size_t>> lines;
        {
            std::size_t start = 0;
            float used = 0;
            for (std::size_t i = 0; i < items.size(); ++i) {
                const float outer = items[i].hypo + items[i].mStart + items[i].mEnd;
                if (wrap && i > start && used + mainGap + outer > mainSize + 0.01f) { lines.push_back({start, i}); start = i; used = 0; }
                used += (i > start ? mainGap : 0.0f) + outer;
            }
            lines.push_back({start, items.size()});
        }
        // resolve the flexible lengths of each line (9.7)
        for (const auto& ln : lines) {
            float sumHypo = mainGap * (float)(ln.second - ln.first - 1);
            for (std::size_t i = ln.first; i < ln.second; ++i) sumHypo += items[i].hypo + items[i].mStart + items[i].mEnd;
            if (mainSize >= kInf) { for (std::size_t i = ln.first; i < ln.second; ++i) items[i].target = items[i].hypo; continue; }
            const bool grow = sumHypo < mainSize;
            for (std::size_t i = ln.first; i < ln.second; ++i) {
                Item& it = items[i];
                const float f = grow ? it.sn->st.flexGrow : it.sn->st.flexShrink;
                it.frozen = f == 0.0f || (grow && it.base > it.hypo) || (!grow && it.base < it.hypo);
                it.target = it.hypo;
            }
            for (int loop = 0; loop < 64; ++loop) {
                bool anyOpen = false;
                float used = mainGap * (float)(ln.second - ln.first - 1), sumFlex = 0, sumScaled = 0;
                for (std::size_t i = ln.first; i < ln.second; ++i) {
                    const Item& it = items[i];
                    used += it.mStart + it.mEnd + (it.frozen ? it.target : it.base);
                    if (!it.frozen) {
                        anyOpen = true;
                        sumFlex += grow ? it.sn->st.flexGrow : it.sn->st.flexShrink;
                        sumScaled += it.sn->st.flexShrink * it.base;
                    }
                }
                if (!anyOpen) break;
                float free = mainSize - used;
                if (sumFlex < 1.0f) { const float alt = free * sumFlex; if (std::fabs(alt) < std::fabs(free)) free = alt; }
                float violation = 0;
                for (std::size_t i = ln.first; i < ln.second; ++i) {
                    Item& it = items[i];
                    if (it.frozen) continue;
                    float t = it.base;
                    if (grow && sumFlex > 0) t = it.base + free * it.sn->st.flexGrow / sumFlex;
                    else if (!grow && sumScaled > 0) t = it.base + free * (it.sn->st.flexShrink * it.base) / sumScaled;
                    const float clamped = std::max(it.minM, std::min(it.maxM, t));
                    violation += clamped - t;
                    it.target = clamped;
                }
                for (std::size_t i = ln.first; i < ln.second; ++i) {
                    Item& it = items[i];
                    if (it.frozen) continue;
                    if (std::fabs(violation) < 0.01f) it.frozen = true;
                    else if (violation > 0 && it.target <= it.minM + 0.001f) it.frozen = true;
                    else if (violation < 0 && it.target >= it.maxM - 0.001f) it.frozen = true;
                }
                if (std::fabs(violation) < 0.01f) break;
            }
        }
        // cross sizes: each item measured at its main size (9.4)
        for (Item& it : items) {
            BlockOpt mo;
            mo.noAvoid = true; mo.bfc = true;
            if (row) {
                mo.borderW = it.target;
                mo.cbH = ch;
                const auto m = MeasureBlock(*it.sn, cw, mo);
                it.cross = m.first;
                it.baseline = m.second;
            } else if (it.crossBorder >= 0) it.cross = it.crossBorder;
            else {
                int bi = -1;
                mo.borderH = it.target;
                mo.boxIndex = &bi;
                const Mark mk = Here();
                FloatCtx none;
                FloatCtx* savedFc = fc;
                fc = &none;
                Block(*it.sn, 0, 0, cw, depth, 0, mo);
                fc = savedFc;
                it.cross = bi >= 0 ? r.boxes[(std::size_t)bi].w : 0.0f;
                Rollback(mk);
            }
        }
        std::vector<float> lineCross(lines.size(), 0.0f);
        for (std::size_t li = 0; li < lines.size(); ++li)
            for (std::size_t i = lines[li].first; i < lines[li].second; ++i) lineCross[li] = std::max(lineCross[li], items[i].cross + items[i].cStart + items[i].cEnd);
        const float crossSize = row ? ch : cw;   // the container's definite cross size (< 0: none)
        if (lines.size() == 1 && crossSize >= 0 && !wrap) lineCross[0] = crossSize;
        // align-content (9.6 step 15): the free cross space between the lines
        float totalCross = crossGap * (float)(lines.size() - 1);
        for (float c : lineCross) totalCross += c;
        const float crossFree = crossSize >= 0 ? crossSize - totalCross : 0.0f;
        float crossStart = 0, crossBetween = 0;
        const Align ac = cs.alignContent;
        if (wrap && crossSize >= 0) {
            const std::size_t nl = lines.size();
            if ((ac == Align::Normal || ac == Align::Stretch) && crossFree > 0) { for (float& c : lineCross) c += crossFree / (float)nl; }
            else if (ac == Align::End) crossStart = crossFree;
            else if (ac == Align::Center) crossStart = crossFree * 0.5f;
            else if (ac == Align::SpaceBetween && crossFree > 0 && nl > 1) crossBetween = crossFree / (float)(nl - 1);
            else if (ac == Align::SpaceAround && crossFree > 0) { crossBetween = crossFree / (float)nl; crossStart = crossBetween * 0.5f; }
            else if (ac == Align::SpaceEvenly && crossFree > 0) { crossBetween = crossFree / (float)(nl + 1); crossStart = crossBetween; }
        }
        float allCross = crossGap * (float)(lines.size() - 1);
        for (float c : lineCross) allCross += c;
        const float crossExtent = crossSize >= 0 ? crossSize : allCross;
        // place the lines and their items, laying each out for real
        float crossPos = crossStart;
        const bool wrapReverse = cs.flexWrap == FlexWrap::WrapReverse;
        float usedMainMax = 0;
        for (std::size_t li = 0; li < lines.size(); ++li) {
            const std::size_t a = lines[li].first, b = lines[li].second;
            const float lc = lineCross[li];
            float used = mainGap * (float)(b - a - 1);
            int autoMargins = 0;
            for (std::size_t i = a; i < b; ++i) { used += items[i].target + items[i].mStart + items[i].mEnd; autoMargins += (int)items[i].autoMStart + (int)items[i].autoMEnd; }
            const float lineMain = mainSize >= kInf ? used : mainSize;
            usedMainMax = std::max(usedMainMax, used);
            const float free = lineMain - used;
            float pos = 0, between = mainGap, autoShare = 0;
            if (free > 0 && autoMargins > 0) autoShare = free / (float)autoMargins;
            else {
                const Align jc = cs.justifyContent;
                const std::size_t cnt = b - a;
                if (jc == Align::End) pos = free;
                else if (jc == Align::Center) pos = free * 0.5f;
                else if (jc == Align::SpaceBetween && cnt > 1 && free > 0) between += free / (float)(cnt - 1);
                else if (jc == Align::SpaceAround && free > 0) { between += free / (float)cnt; pos = free / (float)cnt * 0.5f; }
                else if (jc == Align::SpaceEvenly && free > 0) { between += free / (float)(cnt + 1); pos = free / (float)(cnt + 1); }
            }
            const float lineCrossPos = wrapReverse ? crossExtent - crossPos - lc : crossPos;
            for (std::size_t i = a; i < b; ++i) {
                Item& it = items[i];
                const ComputedStyle& st = it.sn->st;
                const float ms = it.autoMStart ? autoShare : it.mStart, me = it.autoMEnd ? autoShare : it.mEnd;
                const float mainStart = pos + ms;   // the border box's main start within the line
                pos += ms + it.target + me + (i + 1 < b ? between : 0.0f);
                float crossB = it.cross;
                float off = it.cStart;
                const bool crossAuto = row ? !st.height.IsSet() : !st.width.IsSet();
                const float room = lc - it.cStart - it.cEnd;
                bool stretched = false;
                if (it.autoCStart || it.autoCEnd) {
                    const float extra = std::max(0.0f, room - crossB);
                    off = it.cStart + (it.autoCStart && it.autoCEnd ? extra * 0.5f : it.autoCStart ? extra : 0.0f);
                } else if (it.align == Align::Stretch && crossAuto && !IsReplaced(*it.sn)) {
                    crossB = std::max(crossB, room);
                    const Length& maxC = row ? st.maxHeight : st.maxWidth;
                    if (maxC.unit == Length::Px) crossB = std::min(crossB, maxC.value + (st.borderBox ? 0.0f : (row ? EdgesV(st, cw) : EdgesH(st, cw))));
                    stretched = true;
                } else if (it.align == Align::End) off = it.cStart + room - crossB;
                else if (it.align == Align::Center) off = it.cStart + (room - crossB) * 0.5f;
                const float mainPos = reverse ? (lineMain - mainStart - it.target) : mainStart;
                const float crossFinal = lineCrossPos + off;
                BlockOpt fo;
                fo.noAvoid = true; fo.bfc = true;
                if (row) { fo.borderW = it.target; if (stretched) fo.borderH = crossB; fo.cbH = stretched ? crossB : ch; }
                else { fo.borderH = it.target; fo.borderW = (it.crossBorder >= 0 || stretched) ? crossB : -1.0f; }
                const float bxPos = row ? cx + mainPos : cx + crossFinal;
                const float byPos = row ? cy + crossFinal : cy + mainPos;
                // Block adds the left margin itself; the position here is the border box's
                Block(*it.sn, bxPos - st.margin[3].Resolve(cw), byPos, cw, depth, 0, fo);
                if (out.baseline < 0 && it.baseline >= 0) out.baseline = byPos + it.baseline;
            }
            crossPos += lc + crossGap + crossBetween;
        }
        if (row) out.h = ch >= 0 ? ch : allCross;
        else out.h = ch >= 0 ? ch : usedMainMax;
        return out;
    }

    // ---- grid (CSS Grid Layout 1) ----

    Out Grid(const SNode& n, float cx, float cy, float cw, float ch, int depth) {
        const ComputedStyle& cs = n.st;
        const float colGap = cs.columnGap.Resolve(cw), rowGap = cs.rowGap.Resolve(cw);
        // the explicit tracks, with an auto-fill / auto-fit repeat expanded to as many as fit (7.2.3.2)
        auto expand = [&](const GridTemplate& t, float avail, float gap) {
            std::vector<GridTrack> outT = t.tracks;
            if (!t.autoRepeat.empty()) {
                auto fixedSize = [&](const GridTrack& g) {
                    if (g.minKind == GridTrack::Fixed && g.maxKind == GridTrack::Fixed) return std::max(g.minValue, g.maxValue);
                    if (g.maxKind == GridTrack::Fixed) return g.maxValue;
                    if (g.minKind == GridTrack::Fixed) return g.minValue;
                    if (g.maxKind == GridTrack::Percent && avail < kInf) return g.maxValue * avail / 100.0f;
                    if (g.minKind == GridTrack::Percent && avail < kInf) return g.minValue * avail / 100.0f;
                    return 0.0f;
                };
                float fixed = 0, repSize = 0;
                for (const GridTrack& g : t.tracks) fixed += fixedSize(g) + gap;
                for (const GridTrack& g : t.autoRepeat) repSize += fixedSize(g) + gap;
                int count = 1;
                if (repSize > gap * (float)t.autoRepeat.size() && avail < kInf) count = std::max(1, (int)std::floor((avail - fixed + gap) / repSize));
                count = std::min(count, 1000);
                std::vector<GridTrack> rep;
                for (int k = 0; k < count; ++k) for (const GridTrack& g : t.autoRepeat) rep.push_back(g);
                outT.insert(outT.begin() + (std::ptrdiff_t)std::min(t.repeatAt, outT.size()), rep.begin(), rep.end());
            }
            return outT;
        };
        std::vector<GridTrack> cols = expand(cs.gridColumns, cw, colGap);
        std::vector<GridTrack> rows = expand(cs.gridRows, ch >= 0 ? ch : kInf, rowGap);
        // named areas (7.3)
        struct AreaRect { int r0, c0, r1, c1; };
        std::map<std::string, AreaRect> areas;
        for (std::size_t ri = 0; ri < cs.gridAreas.size() && ri < 1000; ++ri) {
            std::vector<std::string> cells;
            std::string cur;
            for (char c : cs.gridAreas[ri] + " ") { if (IsSpace(c)) { if (!cur.empty()) cells.push_back(cur); cur.clear(); } else cur += c; }
            for (std::size_t ci = 0; ci < cells.size() && ci < 1000; ++ci) {
                if (cells[ci].find_first_not_of('.') == std::string::npos) continue;
                auto it = areas.find(cells[ci]);
                if (it == areas.end()) areas[cells[ci]] = AreaRect{(int)ri, (int)ci, (int)ri + 1, (int)ci + 1};
                else { it->second.r1 = std::max(it->second.r1, (int)ri + 1); it->second.c1 = std::max(it->second.c1, (int)ci + 1); }
            }
            if (cols.size() < cells.size()) cols.resize(cells.size(), cs.gridAutoColumns);
        }
        if (rows.size() < cs.gridAreas.size()) rows.resize(cs.gridAreas.size(), cs.gridAutoRows);
        struct GItem { const SNode* sn = nullptr; int r0 = -1, c0 = -1, rs = 1, cs = 1; float w = 0, h = 0, baseline = -1; };
        std::vector<GItem> items;
        for (const SNode& k : n.kids) {
            if (k.text) continue;
            if (k.st.OutOfFlow()) { DeferAbs(k, cx, cy, depth); continue; }
            GItem g;
            g.sn = &k;
            items.push_back(g);
        }
        std::stable_sort(items.begin(), items.end(), [](const GItem& a, const GItem& b) { return a.sn->st.order < b.sn->st.order; });
        Out out;
        const int nExplicitCols = (int)cols.size(), nExplicitRows = (int)rows.size();
        // a start / end pair on one axis to (start index, span); start -1 = placed automatically (8.3)
        auto lineIndex = [&](const GridLine& g, bool isRow, bool isEnd, int explicitCount, int& outLine) -> bool {
            if (!g.name.empty()) {
                auto it = areas.find(g.name);
                if (it != areas.end()) { outLine = isRow ? (isEnd ? it->second.r1 : it->second.r0) : (isEnd ? it->second.c1 : it->second.c0); return true; }
                const std::string suffix = isEnd ? "-end" : "-start";
                if (g.name.size() > suffix.size() && g.name.compare(g.name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    auto it2 = areas.find(g.name.substr(0, g.name.size() - suffix.size()));
                    if (it2 != areas.end()) { outLine = isRow ? (isEnd ? it2->second.r1 : it2->second.r0) : (isEnd ? it2->second.c1 : it2->second.c0); return true; }
                }
                const GridTemplate& t = isRow ? cs.gridRows : cs.gridColumns;
                for (std::size_t li = 0; li < t.lineNames.size(); ++li)
                    for (const std::string& ln : t.lineNames[li]) if (ln == g.name) { outLine = (int)li; return true; }
                return false;
            }
            if (g.line > 0) { outLine = g.line - 1; return true; }
            if (g.line < 0) { outLine = std::max(0, explicitCount + 1 + g.line); return true; }
            return false;
        };
        auto resolve = [&](const GridLine& s, const GridLine& e, bool isRow, int explicitCount, int& start, int& span) {
            start = -1; span = 1;
            int a = 0, b = 0;
            const bool hasA = s.span == 0 && lineIndex(s, isRow, false, explicitCount, a);
            const bool hasB = e.span == 0 && lineIndex(e, isRow, true, explicitCount, b);
            if (hasA && hasB) { if (b < a) std::swap(a, b); start = a; span = std::max(1, b - a); }
            else if (hasA) { start = a; span = e.span > 0 ? e.span : 1; }
            else if (hasB) { span = s.span > 0 ? s.span : 1; start = std::max(0, b - span); }
            else span = std::max(1, s.span > 0 ? s.span : e.span > 0 ? e.span : 1);
            span = std::min(span, 1000);
            start = std::min(start, 10000);
        };
        for (GItem& g : items) {
            const ComputedStyle& st = g.sn->st;
            resolve(st.gridRowStart, st.gridRowEnd, true, nExplicitRows, g.r0, g.rs);
            resolve(st.gridColumnStart, st.gridColumnEnd, false, nExplicitCols, g.c0, g.cs);
        }
        // auto-placement (8.5): an occupancy grid, row by row (or column by column), sparse unless dense
        int nCols = std::max(1, nExplicitCols);
        for (const GItem& g : items) nCols = std::max(nCols, g.c0 >= 0 ? g.c0 + g.cs : g.cs);
        nCols = std::min(nCols, 1000);
        int nRowsFlow = std::max(1, nExplicitRows);
        for (const GItem& g : items) nRowsFlow = std::max(nRowsFlow, g.r0 >= 0 ? g.r0 + g.rs : g.rs);
        std::vector<std::vector<char>> occ;
        auto ensureRows = [&](int nr) { while ((int)occ.size() < nr && occ.size() < 20000) occ.push_back(std::vector<char>((std::size_t)nCols, 0)); };
        auto fits = [&](int r0, int c0, int rs, int cs2) {
            if (c0 < 0 || r0 < 0 || c0 + cs2 > nCols) return false;
            ensureRows(r0 + rs);
            if ((int)occ.size() < r0 + rs) return false;
            for (int rr = r0; rr < r0 + rs; ++rr) for (int cc = c0; cc < c0 + cs2; ++cc) if (occ[(std::size_t)rr][(std::size_t)cc]) return false;
            return true;
        };
        auto markCells = [&](const GItem& g) {
            ensureRows(g.r0 + g.rs);
            for (int rr = g.r0; rr < g.r0 + g.rs && rr < (int)occ.size(); ++rr)
                for (int cc = g.c0; cc < g.c0 + g.cs && cc < nCols; ++cc) occ[(std::size_t)rr][(std::size_t)cc] = 1;
        };
        for (GItem& g : items) if (g.r0 >= 0 && g.c0 >= 0) { g.c0 = std::min(g.c0, nCols - 1); g.cs = std::min(g.cs, nCols - g.c0); markCells(g); }
        int curR = 0, curC = 0;
        for (GItem& g : items) {
            if (g.r0 >= 0 && g.c0 >= 0) continue;
            g.cs = std::min(g.cs, nCols);
            if (cs.gridDense) { curR = 0; curC = 0; }
            if (g.r0 >= 0) {   // a row given: the first column that fits in it
                int c = 0;
                while (c + g.cs <= nCols && !fits(g.r0, c, g.rs, g.cs)) ++c;
                g.c0 = c + g.cs <= nCols ? c : 0;
                markCells(g);
                continue;
            }
            if (g.c0 >= 0 && !cs.gridAutoFlowColumn) {   // a column given: the first row from the cursor where it fits
                g.c0 = std::min(g.c0, nCols - g.cs);
                int rr = curR;
                if (g.c0 < curC) ++rr;
                for (int guard = 0; guard < 20000 && !fits(rr, g.c0, g.rs, g.cs); ++guard) ++rr;
                g.r0 = rr;
                curR = rr; curC = g.c0 + g.cs;
                markCells(g);
                continue;
            }
            if (cs.gridAutoFlowColumn) {
                int c = g.c0 >= 0 ? g.c0 : curC, rr = g.c0 >= 0 ? 0 : curR;
                for (int guard = 0; guard < 200000; ++guard) {
                    if (rr + g.rs > nRowsFlow) { rr = 0; ++c; }
                    if (c + g.cs > nCols) {
                        if (nCols >= 1000) break;
                        nCols = std::min(1000, c + g.cs);
                        for (auto& rowv : occ) rowv.resize((std::size_t)nCols, 0);
                    }
                    if (fits(rr, c, g.rs, g.cs)) break;
                    ++rr;
                }
                g.r0 = rr; g.c0 = c;
                curR = rr + g.rs; curC = c;
                markCells(g);
                continue;
            }
            int rr = curR, c = curC;
            for (int guard = 0; guard < 2000000; ++guard) {
                if (c + g.cs > nCols) { c = 0; ++rr; }
                if (rr >= 20000 || fits(rr, c, g.rs, g.cs)) break;
                ++c;
            }
            g.r0 = rr; g.c0 = c;
            curR = rr; curC = c + g.cs;
            markCells(g);
        }
        int nRows = std::max((int)rows.size(), (int)occ.size());
        for (const GItem& g : items) nRows = std::max(nRows, g.r0 + g.rs);
        nRows = std::min(nRows, 20000);
        for (GItem& g : items) { g.r0 = std::min(g.r0, nRows - 1); g.rs = std::max(1, std::min(g.rs, nRows - g.r0)); }
        while ((int)cols.size() < nCols) cols.push_back(cs.gridAutoColumns);
        while ((int)rows.size() < nRows) rows.push_back(cs.gridAutoRows);
        if (cols.empty()) { out.h = ch >= 0 ? ch : 0.0f; return out; }
        // auto-fit: repeated columns with no item collapse (7.2.3.2)
        std::vector<char> colEmpty(cols.size(), 1);
        for (const GItem& g : items) for (int c = g.c0; c < g.c0 + g.cs && c < (int)cols.size(); ++c) colEmpty[(std::size_t)c] = 0;
        // ---- the track sizing algorithm (11, simplified) ----
        auto sizeTracks = [&](const std::vector<GridTrack>& tracks, float avail, float gap, bool isCol, const std::vector<float>& itemMins,
                              const std::vector<float>& itemMaxs, std::vector<float>& sizes) {
            const std::size_t nt = tracks.size();
            sizes.assign(nt, 0.0f);
            std::vector<float> base(nt, 0.0f), limit(nt, kInf), cMin(nt, 0.0f), cMax(nt, 0.0f);
            for (std::size_t ii = 0; ii < items.size(); ++ii) {   // the items that span one track give its content sizes
                const GItem& g = items[ii];
                const int s = isCol ? g.c0 : g.r0, span = isCol ? g.cs : g.rs;
                if (span != 1 || s < 0 || s >= (int)nt) continue;
                cMin[(std::size_t)s] = std::max(cMin[(std::size_t)s], itemMins[ii]);
                cMax[(std::size_t)s] = std::max(cMax[(std::size_t)s], itemMaxs[ii]);
            }
            float sumFr = 0;
            for (std::size_t t = 0; t < nt; ++t) {
                const GridTrack& g = tracks[t];
                auto fixedOf = [&](GridTrack::Kind k, float v) { return k == GridTrack::Fixed ? v : (k == GridTrack::Percent && avail < kInf) ? v * avail / 100.0f : -1.0f; };
                const float fMin = fixedOf(g.minKind, g.minValue), fMax = fixedOf(g.maxKind, g.maxValue);
                base[t] = fMin >= 0 ? fMin : g.minKind == GridTrack::MaxContent ? cMax[t] : (g.minKind == GridTrack::MinContent || g.minKind == GridTrack::Auto) ? cMin[t] : 0.0f;
                if (fMax >= 0) limit[t] = std::max(base[t], fMax);
                else if (g.maxKind == GridTrack::MinContent) limit[t] = std::max(base[t], cMin[t]);
                else if (g.maxKind == GridTrack::MaxContent || g.maxKind == GridTrack::Auto) limit[t] = std::max(base[t], cMax[t]);
                else limit[t] = kInf;   // fr
                if (g.maxKind == GridTrack::Fr) sumFr += g.maxValue;
                if (isCol && cs.gridColumns.autoFit && !cs.gridColumns.autoRepeat.empty() && colEmpty[t] && t >= cs.gridColumns.repeatAt) { base[t] = 0; limit[t] = 0; }
            }
            // items that span several tracks: the size they need beyond the tracks goes to the last flexible-content track
            for (std::size_t ii = 0; ii < items.size(); ++ii) {
                const GItem& g = items[ii];
                const int s = isCol ? g.c0 : g.r0, span = isCol ? g.cs : g.rs;
                if (span <= 1 || s < 0) continue;
                float have = gap * (float)(span - 1);
                int last = -1;
                for (int t = s; t < s + span && t < (int)nt; ++t) {
                    have += base[(std::size_t)t];
                    if (tracks[(std::size_t)t].maxKind != GridTrack::Fixed && tracks[(std::size_t)t].maxKind != GridTrack::Fr) last = t;
                }
                if (last < 0) continue;
                if (itemMins[ii] > have) { base[(std::size_t)last] += itemMins[ii] - have; limit[(std::size_t)last] = std::max(limit[(std::size_t)last], base[(std::size_t)last]); }
                if (limit[(std::size_t)last] < kInf) limit[(std::size_t)last] = std::max(limit[(std::size_t)last], itemMaxs[ii] - have + base[(std::size_t)last]);
            }
            float used = gap * (float)(nt > 0 ? nt - 1 : 0);
            for (std::size_t t = 0; t < nt; ++t) { sizes[t] = base[t]; used += base[t]; }
            if (avail < kInf) {   // grow the tracks toward their limits (11.6)
                float free = avail - used;
                for (int pass = 0; pass < 8 && free > 0.01f; ++pass) {
                    int growable = 0;
                    for (std::size_t t = 0; t < nt; ++t) if (tracks[t].maxKind != GridTrack::Fr && sizes[t] < limit[t] - 0.01f) ++growable;
                    if (!growable) break;
                    const float share = free / (float)growable;
                    for (std::size_t t = 0; t < nt; ++t) {
                        if (tracks[t].maxKind == GridTrack::Fr || sizes[t] >= limit[t] - 0.01f) continue;
                        const float add = std::min(share, limit[t] - sizes[t]);
                        sizes[t] += add; free -= add;
                    }
                }
            } else {
                for (std::size_t t = 0; t < nt; ++t) if (tracks[t].maxKind != GridTrack::Fr && limit[t] < kInf) sizes[t] = limit[t];
            }
            if (sumFr > 0) {   // fr tracks (11.7): the space left by their factors; a track whose minimum is larger keeps it
                std::vector<char> inflexible(nt, 0);
                float frSize = 0;
                if (avail >= kInf) {
                    for (std::size_t t = 0; t < nt; ++t)
                        if (tracks[t].maxKind == GridTrack::Fr && tracks[t].maxValue > 0) frSize = std::max(frSize, std::max(base[t], cMax[t]) / std::max(tracks[t].maxValue, 1.0f));
                } else {
                    float leftover = avail - gap * (float)(nt > 0 ? nt - 1 : 0);
                    for (std::size_t t = 0; t < nt; ++t) if (tracks[t].maxKind != GridTrack::Fr) leftover -= sizes[t];
                    for (int loop = 0; loop < 32; ++loop) {
                        float sf = 0, rest = leftover;
                        for (std::size_t t = 0; t < nt; ++t) {
                            if (tracks[t].maxKind != GridTrack::Fr) continue;
                            if (inflexible[t]) rest -= base[t]; else sf += tracks[t].maxValue;
                        }
                        frSize = sf > 0 ? std::max(0.0f, rest) / std::max(sf, 1.0f) : 0.0f;
                        bool changed = false;
                        for (std::size_t t = 0; t < nt; ++t)
                            if (tracks[t].maxKind == GridTrack::Fr && !inflexible[t] && frSize * tracks[t].maxValue < base[t]) { inflexible[t] = 1; changed = true; }
                        if (!changed) break;
                    }
                }
                for (std::size_t t = 0; t < nt; ++t)
                    if (tracks[t].maxKind == GridTrack::Fr) sizes[t] = inflexible[t] ? base[t] : std::max(base[t], frSize * tracks[t].maxValue);
            }
            for (float& s : sizes) s = std::max(0.0f, Finite(s));
        };
        // columns from the items' min- and max-content widths
        std::vector<float> iMin(items.size()), iMax(items.size());
        for (std::size_t ii = 0; ii < items.size(); ++ii) {
            const auto in = Intrinsic(*items[ii].sn);
            const float m = MarginsH(items[ii].sn->st, cw);
            iMin[ii] = in.first + m; iMax[ii] = in.second + m;
        }
        std::vector<float> colSize;
        sizeTracks(cols, cw, colGap, true, iMin, iMax, colSize);
        std::vector<float> colX(cols.size() + 1, 0.0f);
        for (std::size_t c = 0; c < cols.size(); ++c) colX[c + 1] = colX[c] + colSize[c] + (c + 1 < cols.size() ? colGap : 0.0f);
        auto spanSize = [](const std::vector<float>& at, std::size_t count, int s, int span, float gap) {
            const int e = std::min((int)count, s + span);
            return std::max(0.0f, at[(std::size_t)e] - at[(std::size_t)s] - (e < (int)count ? gap : 0.0f));
        };
        // each item's width in its area, and its height at that width
        for (GItem& g : items) {
            const ComputedStyle& st = g.sn->st;
            const float areaW = spanSize(colX, cols.size(), g.c0, g.cs, colGap);
            const float ml = st.margin[3].Resolve(cw), mr = st.margin[1].Resolve(cw);
            Align js = st.justifySelf == Align::Auto ? cs.justifyItems : st.justifySelf;
            if (js == Align::Normal || js == Align::Auto) js = Align::Stretch;
            if (st.width.IsSet() || IsReplaced(*g.sn)) g.w = -1;
            else if (js == Align::Stretch && st.margin[3].unit != Length::Auto && st.margin[1].unit != Length::Auto) g.w = std::max(0.0f, areaW - ml - mr);
            else g.w = ShrinkToFit(*g.sn, std::max(0.0f, areaW - ml - mr));
            BlockOpt mo;
            mo.borderW = g.w;
            mo.noAvoid = true; mo.bfc = true;
            const auto m = MeasureBlock(*g.sn, areaW, mo);
            g.h = m.first + MarginsV(st, cw);
            g.baseline = m.second;
        }
        std::vector<float> hs(items.size());
        for (std::size_t ii = 0; ii < items.size(); ++ii) hs[ii] = items[ii].h;
        std::vector<float> rowSize;
        sizeTracks(rows, ch >= 0 ? ch : kInf, rowGap, false, hs, hs, rowSize);
        std::vector<float> rowY(rows.size() + 1, 0.0f);
        for (std::size_t rr = 0; rr < rows.size(); ++rr) rowY[rr + 1] = rowY[rr] + rowSize[rr] + (rr + 1 < rows.size() ? rowGap : 0.0f);
        // justify-content / align-content for the whole grid when its tracks do not fill the box
        float offX = 0, offY = 0;
        const float gridW = colX.back(), gridH = rowY.back();
        if (gridW < cw) {
            if (cs.justifyContent == Align::Center) offX = (cw - gridW) * 0.5f;
            else if (cs.justifyContent == Align::End) offX = cw - gridW;
        }
        if (ch >= 0 && gridH < ch) {
            if (cs.alignContent == Align::Center) offY = (ch - gridH) * 0.5f;
            else if (cs.alignContent == Align::End) offY = ch - gridH;
        }
        // lay the items out in their areas
        for (GItem& g : items) {
            const ComputedStyle& st = g.sn->st;
            const float ax = cx + offX + colX[(std::size_t)g.c0], ay = cy + offY + rowY[(std::size_t)g.r0];
            const float aw = spanSize(colX, cols.size(), g.c0, g.cs, colGap);
            const float ah = spanSize(rowY, rows.size(), g.r0, g.rs, rowGap);
            const float ml = st.margin[3].Resolve(cw), mr = st.margin[1].Resolve(cw), mt = st.margin[0].Resolve(cw), mb = st.margin[2].Resolve(cw);
            Align js = st.justifySelf == Align::Auto ? cs.justifyItems : st.justifySelf;
            if (js == Align::Normal || js == Align::Auto) js = Align::Stretch;
            Align as = st.alignSelf == Align::Auto ? cs.alignItems : st.alignSelf;
            if (as == Align::Normal || as == Align::Auto) as = Align::Stretch;
            BlockOpt fo;
            fo.noAvoid = true; fo.bfc = true;
            fo.borderW = g.w;
            fo.cbH = ah;
            float y = ay + mt;
            if (as == Align::Stretch && !st.height.IsSet() && !IsReplaced(*g.sn)) fo.borderH = std::max(g.h - mt - mb, ah - mt - mb);
            else if (as == Align::Center) y = ay + mt + (ah - g.h) * 0.5f;
            else if (as == Align::End) y = ay + ah - g.h + mt;
            int bi = -1;
            fo.boxIndex = &bi;
            const Mark m = Here();
            Block(*g.sn, ax, y, aw, depth, 0, fo);
            if (bi >= 0 && js != Align::Stretch) {
                const float bw = r.boxes[(std::size_t)bi].w;
                float dx = 0;
                if (js == Align::Center) dx = (aw - ml - mr - bw) * 0.5f;
                else if (js == Align::End) dx = aw - ml - mr - bw;
                if (dx != 0) Shift(m, dx, 0);
            }
            if (out.baseline < 0 && g.baseline >= 0) out.baseline = y + g.baseline;
        }
        out.h = ch >= 0 ? ch : gridH;
        return out;
    }

    // ---- paint order: the layers of positioned boxes (CSS 2.1 appendix E, simplified) ----
    std::vector<int> LayerRanks() const {
        std::vector<int> order(keys.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return keys[(std::size_t)a] < keys[(std::size_t)b]; });
        std::vector<int> rank(keys.size(), 0);
        for (std::size_t i = 0; i < order.size(); ++i) rank[(std::size_t)order[i]] = (int)i;
        return rank;
    }
};

} // namespace

LayoutResult LayoutPage(const HtmlDocument& doc, float widthIn, const TextMeasure& measure, const ImageSizeFn& imageSize,
                        const LayoutContext* context) {
    LayoutResult r;
    const float width = std::max(0.0f, Finite(widthIn));
    r.width = width;
    const Node* html = doc.Html();
    if (!html) return r;
    std::vector<StyleSheet> parsed;
    std::vector<const StyleSheet*> sheets;
    if (context && context->sheets) sheets = *context->sheets;
    else {
        parsed.reserve(doc.styleSheets.size());
        for (const std::string& css : doc.styleSheets) parsed.push_back(ParseStyleSheet(css));
        for (const StyleSheet& s : parsed) sheets.push_back(&s);
    }
    for (const StyleSheet* s : sheets) if (s && s->hasDynamic) r.dynamic = true;
    LayoutContext localCtx;
    if (context) { localCtx.style = context->style; localCtx.controlText = context->controlText; localCtx.viewportHeight = context->viewportHeight; }
    const float viewH = localCtx.viewportHeight > 0 ? localCtx.viewportHeight : width * 0.75f;
    localCtx.style.media.width = width;
    localCtx.style.media.height = viewH;
    localCtx.style.rootFontSize = ComputeStyle(*html, ComputedStyle(), sheets, &localCtx.style).fontSize;   // rem
    Builder builder{sheets, &localCtx.style, {}, {false, false, false}, {}};
    builder.Prepare();
    SNode root;
    if (!builder.Build(*html, ComputedStyle(), root)) return r;
    if (root.st.display == Display::Inline) root.st.display = Display::Block;

    Layouter L(measure, imageSize, r, &localCtx, doc.Body(), html);
    L.viewW = width;
    L.viewH = viewH;
    // The canvas: the root's background, or else body's (CSS 2.1 14.2).
    const SNode* bodyS = nullptr;
    for (const SNode& k : root.kids) if (!k.text && k.node == doc.Body()) bodyS = &k;
    if (root.st.background.a > 0) r.canvas = root.st.background;
    else if (bodyS && bodyS->st.background.a > 0) { r.canvas = bodyS->st.background; L.canvasFromBody = true; }
    // images, once each, in page order
    std::vector<const Node*> todo{html};
    while (!todo.empty()) {
        const Node* n = todo.back();
        todo.pop_back();
        if (n->IsElement("img")) if (const std::string* src = n->Attr("src")) {
            if (std::find(r.images.begin(), r.images.end(), *src) == r.images.end()) r.images.push_back(*src);
        }
        for (std::size_t k = n->children.size(); k-- > 0;) todo.push_back(n->children[k].get());
    }
    std::vector<Pending> rootAbs;
    L.cbStack.push_back(&rootAbs);
    const float mt = root.st.margin[0].Resolve(width);
    BlockOpt ro;
    ro.cbH = viewH;
    const Layouter::Out o = L.Block(root, 0, mt, width, 0, 0, ro);
    r.height = Finite(mt + o.h + root.st.margin[2].Resolve(width));
    // absolutely positioned boxes with no positioned ancestor: the initial containing block; fixed ones: the view
    L.cbStack.pop_back();
    for (std::size_t i = 0; i < rootAbs.size(); ++i) L.LayAbs(rootAbs[i], 0, 0, width, viewH);
    for (std::size_t i = 0; i < L.fixedPending.size(); ++i) L.LayAbs(L.fixedPending[i], 0, 0, width, viewH);
    // the page grows to hold positioned content (fixed content is on the view, not the page)
    for (const DrawCmd& c : r.cmds) if (!c.fixed && c.clip < 0 && c.kind != DrawCmd::Shadow) r.height = std::max(r.height, Finite(c.y + c.h));
    L.ExpandGradients();
    // paint order: stable by layer
    const std::vector<int> rank = L.LayerRanks();
    std::vector<std::size_t> perm(r.cmds.size());
    for (std::size_t i = 0; i < perm.size(); ++i) perm[i] = i;
    bool layered = false;
    for (int k : L.cmdKey) if (k != 0) { layered = true; break; }
    if (layered) std::stable_sort(perm.begin(), perm.end(), [&](std::size_t a, std::size_t b) { return rank[(std::size_t)L.cmdKey[a]] < rank[(std::size_t)L.cmdKey[b]]; });
    for (LinkArea& a : r.areas) a.layer = rank[(std::size_t)a.layer];
    // drop text runs that are only spaces and draw nothing, in the new order
    std::vector<DrawCmd> kept;
    kept.reserve(r.cmds.size());
    std::vector<int> remap(r.cmds.size(), -1);
    for (std::size_t pi = 0; pi < perm.size(); ++pi) {
        const std::size_t i = perm[pi];
        const DrawCmd& c = r.cmds[i];
        if (c.kind == DrawCmd::Text && !c.underline && !c.strike && c.text.find_first_not_of(' ') == std::string::npos) continue;
        if (c.kind == DrawCmd::BgImage && c.w <= 0) continue;
        remap[i] = (int)kept.size();
        kept.push_back(c);
    }
    for (BoxInfo& b : r.boxes) {
        std::size_t f = b.firstCmd;
        while (f < remap.size() && remap[f] < 0) ++f;
        b.firstCmd = f < remap.size() ? (std::size_t)remap[f] : kept.size();
    }
    std::vector<int> embeds;
    for (int e : r.embeds) if (e >= 0 && (std::size_t)e < remap.size() && remap[(std::size_t)e] >= 0) embeds.push_back(remap[(std::size_t)e]);
    std::sort(embeds.begin(), embeds.end());
    r.embeds.swap(embeds);
    r.cmds.swap(kept);
    return r;
}

std::string DumpLayout(const LayoutResult& r) {
    std::string out;
    auto num = [](float v) { return std::to_string((long)std::lround(v)); };
    std::size_t b = 0;
    for (std::size_t i = 0; i <= r.cmds.size(); ++i) {
        while (b < r.boxes.size() && r.boxes[b].firstCmd <= i) {
            const BoxInfo& x = r.boxes[b++];
            out += "block " + x.tag + " " + num(x.x) + "," + num(x.y) + " " + num(x.w) + "x" + num(x.h) + "\n";
        }
        if (i == r.cmds.size()) break;
        const DrawCmd& c = r.cmds[i];
        if (c.kind == DrawCmd::Text) out += "text " + num(c.x) + "," + num(c.y) + " '" + c.text + "'\n";
    }
    return out;
}

bool ClipAndScroll(const LayoutResult& r, int clip, const ScrollOffsets* offsets, float& dx, float& dy, float& cx0, float& cy0,
                   float& cx1, float& cy1) {
    dx = 0; dy = 0;
    cx0 = -kInf; cy0 = -kInf; cx1 = kInf; cy1 = kInf;
    std::vector<int> chain;
    for (int c = clip, guard = 0; c >= 0 && (std::size_t)c < r.clips.size() && guard < 512; c = r.clips[(std::size_t)c].parent, ++guard) chain.push_back(c);
    // from the outside in: each clip is moved by the scrolling around it; a scroller moves what is inside it
    float ox = 0, oy = 0;
    for (std::size_t k = chain.size(); k-- > 0;) {
        const ClipRect& cr = r.clips[(std::size_t)chain[k]];
        const float x0 = cr.x - ox, y0 = cr.y - oy;
        cx0 = std::max(cx0, x0); cy0 = std::max(cy0, y0);
        cx1 = std::min(cx1, x0 + cr.w); cy1 = std::min(cy1, y0 + cr.h);
        if (cr.scroller >= 0 && offsets && (std::size_t)cr.scroller < offsets->size()) {
            ox += (*offsets)[(std::size_t)cr.scroller].first;
            oy += (*offsets)[(std::size_t)cr.scroller].second;
        }
    }
    dx = -ox; dy = -oy;
    return cx1 > cx0 && cy1 > cy0;
}

namespace {
// Is the view point (x, y) inside `clip` (after scrolling)? `px`, `py` get the point in unscrolled page coordinates.
bool InsideClip(const LayoutResult& r, int clip, const ScrollOffsets* offsets, float x, float y, float& px, float& py) {
    float dx, dy, x0, y0, x1, y1;
    if (!ClipAndScroll(r, clip, offsets, dx, dy, x0, y0, x1, y1)) return false;
    if (x < x0 || y < y0 || x >= x1 || y >= y1) return false;
    px = x - dx; py = y - dy;
    return true;
}
} // namespace

int LinkAt(const LayoutResult& r, float x, float y, const ScrollOffsets* offsets, float pageScrollY) {
    int best = -1, bestLayer = -1;
    for (const LinkArea& a : r.areas) {
        float px = x, py = a.fixed ? y - pageScrollY : y;
        if (!InsideClip(r, a.clip, offsets, px, py, px, py)) continue;
        if (px >= a.x && px < a.x + a.w && py >= a.y && py < a.y + a.h && a.layer > bestLayer) { best = a.link; bestLayer = a.layer; }
    }
    return best;
}

int ScrollerAt(const LayoutResult& r, float x, float y, const ScrollOffsets* offsets, float pageScrollY) {
    int best = -1;
    for (std::size_t i = 0; i < r.scrollers.size(); ++i) {
        const Scroller& s = r.scrollers[i];
        if (s.contentH <= s.h + 0.5f && s.contentW <= s.w + 0.5f) continue;
        float px = x, py = s.fixed ? y - pageScrollY : y;
        const int parentClip = s.clip >= 0 ? r.clips[(std::size_t)s.clip].parent : -1;
        if (!InsideClip(r, parentClip, offsets, px, py, px, py)) continue;
        if (px >= s.x && px < s.x + s.w && py >= s.y && py < s.y + s.h) best = (int)i;   // a later one is inside an earlier one
    }
    return best;
}

int ControlAt(const LayoutResult& r, float x, float y, const ScrollOffsets* offsets, float pageScrollY) {
    int best = -1;
    for (std::size_t i = 0; i < r.controls.size(); ++i) {
        const ControlBox& c = r.controls[i];
        float px = x, py = c.fixed ? y - pageScrollY : y;
        if (!InsideClip(r, c.clip, offsets, px, py, px, py)) continue;
        if (px >= c.x && px < c.x + c.w && py >= c.y && py < c.y + c.h) best = (int)i;
    }
    return best;
}

std::vector<FindHit> FindInPage(const LayoutResult& r, const std::string& query, const TextMeasure& measure, std::size_t maxHits) {
    std::vector<FindHit> out;
    if (query.empty()) return out;
    auto lower = [](std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); return s; };
    const std::string q = lower(query);
    for (std::size_t i = 0; i < r.cmds.size() && out.size() < maxHits; ++i) {
        const DrawCmd& c = r.cmds[i];
        if (c.kind != DrawCmd::Text || c.text.size() < q.size()) continue;
        const std::string t = lower(c.text);
        for (std::size_t at = t.find(q); at != std::string::npos && out.size() < maxHits; at = t.find(q, at + q.size())) {
            FindHit h;
            h.cmd = (int)i;
            h.x = c.x + Finite(measure.Width(c.text.substr(0, at), c.font));
            h.w = Finite(measure.Width(c.text.substr(at, q.size()), c.font));
            h.y = c.y;
            h.h = c.h;
            out.push_back(h);
        }
    }
    return out;
}

} // namespace okay::browser
