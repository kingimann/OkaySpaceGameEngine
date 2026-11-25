// Browser (page engine): layout. Written from CSS 2.1 chapters 8, 9, 10, 12 and 16 (see Layout.hpp for what is simplified).
#include "okay/Browser/Layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>

namespace okay::browser {

float FixedMeasure::Width(const std::string& utf8, const FontSpec& font) const {
    std::size_t points = 0;
    for (unsigned char c : utf8) if ((c & 0xC0) != 0x80) ++points;
    return (float)points * m_factor * font.size;
}

namespace {

// ---- the styled tree ---------------------------------------------------------------------------------------------------

struct SNode {
    const Node* node = nullptr;   // the element, or the text node
    bool text = false;
    ComputedStyle st;
    std::vector<SNode> kids;
};

bool IsEmbedTag(const std::string& t) {
    return t == "canvas" || t == "embed" || t == "object" || t == "iframe" || t == "video" || t == "okay-game";
}
bool IsBlockLevel(const SNode& n) { return !n.text && n.st.display != Display::Inline; }

// Build the styled tree of `el`; false when it is display: none.
bool Build(const Node& el, const ComputedStyle& parent, const std::vector<const StyleSheet*>& sheets, SNode& out) {
    out.node = &el;
    out.st = ComputeStyle(el, parent, sheets);
    if (out.st.display == Display::None) return false;
    if (el.name == "img" || el.name == "br" || IsEmbedTag(el.name) || el.name == "input" || el.name == "select" || el.name == "textarea") {
        if (IsEmbedTag(el.name) && out.st.display == Display::Inline) out.st.display = Display::Inline;
        return el.name != "input" && el.name != "select" && el.name != "textarea";   // form controls: not in step 1
    }
    bool anyBlock = false;
    for (const auto& c : el.children) {
        if (c->kind == NodeKind::Text) {
            SNode t;
            t.node = c.get();
            t.text = true;
            t.st = out.st;
            out.kids.push_back(std::move(t));
        } else if (c->kind == NodeKind::Element) {
            SNode k;
            if (Build(*c, out.st, sheets, k)) {
                anyBlock = anyBlock || IsBlockLevel(k);
                out.kids.push_back(std::move(k));
            }
        }
    }
    if (anyBlock && out.st.display == Display::Inline) out.st.display = Display::Block;   // an inline that holds a block
    return true;
}

// ---- helpers -----------------------------------------------------------------------------------------------------------

float Finite(float v) { return std::isfinite(v) ? std::max(-1e9f, std::min(1e9f, v)) : 0.0f; }

FontSpec FontOf(const ComputedStyle& s) {
    FontSpec f;
    f.size = s.fontSize;
    f.bold = s.bold; f.italic = s.italic; f.mono = s.mono;
    return f;
}

float CollapseMargins(float a, float b) {
    if (a >= 0 && b >= 0) return std::max(a, b);
    if (a < 0 && b < 0) return std::min(a, b);
    return a + b;
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

std::string Roman(int n, bool upper) {
    if (n <= 0 || n >= 4000) return std::to_string(n);
    static const int vals[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
    static const char* syms[] = {"m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i"};
    std::string out;
    for (int k = 0; k < 13; ++k) while (n >= vals[k]) { out += syms[k]; n -= vals[k]; }
    if (upper) for (char& c : out) c = (char)(c - 'a' + 'A');
    return out;
}
std::string Alpha(int n, bool upper) {
    if (n <= 0) return std::to_string(n);
    std::string out;
    while (n > 0) { --n; out.insert(out.begin(), (char)((upper ? 'A' : 'a') + n % 26)); n /= 26; }
    return out;
}
std::string MarkerText(ListStyle ls, int n) {
    switch (ls) {
    case ListStyle::Decimal: return std::to_string(n) + ".";
    case ListStyle::LowerAlpha: return Alpha(n, false) + ".";
    case ListStyle::UpperAlpha: return Alpha(n, true) + ".";
    case ListStyle::LowerRoman: return Roman(n, false) + ".";
    case ListStyle::UpperRoman: return Roman(n, true) + ".";
    default: return std::string();
    }
}

// The next UTF-8 code point's length at s[i].
std::size_t CpLen(const std::string& s, std::size_t i) {
    const unsigned char c = (unsigned char)s[i];
    std::size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    if (i + n > s.size()) n = s.size() - i;
    return n;
}

// ---- inline content ----------------------------------------------------------------------------------------------------

struct Frag {
    enum Kind { Word, Space, Image, Embed, Break, Anchor } kind = Word;
    std::string text;              // Word / Space: the characters; Image / Embed: src; Anchor: the id
    const ComputedStyle* st = nullptr;
    const SNode* sn = nullptr;     // Image / Embed: the element
    int link = -1;
    bool breakBefore = false;      // a line may end before this fragment
    float w = 0, h = 0;            // measured / sized
};

struct BoxDraw { int bg = -1; int border[4] = {-1, -1, -1, -1}; };

struct Layouter {
    const TextMeasure& measure;
    const ImageSizeFn& imageSize;
    LayoutResult& r;
    std::vector<BoxDraw> draws;   // per entry of r.boxes
    bool canvasFromBody = false;
    const Node* bodyNode = nullptr;
    const Node* htmlNode = nullptr;

    Layouter(const TextMeasure& m, const ImageSizeFn& img, LayoutResult& res, const Node* body, const Node* html)
        : measure(m), imageSize(img), r(res), bodyNode(body), htmlNode(html) {}

    struct Out { float h = 0; float baseline = -1; };

    float Measure(const std::string& s, const ComputedStyle& st) const {
        if (s.empty()) return 0.0f;
        return Finite(measure.Width(s, FontOf(st)));
    }

    // ---- flattening inline content into fragments ----
    void Flatten(const SNode& n, int link, std::vector<Frag>& out, bool& lastSpace) {
        if (n.text) { FlattenText(n.node->data, n.st, link, out, lastSpace); return; }
        const Node& el = *n.node;
        if (el.name == "br") { Frag f; f.kind = Frag::Break; f.st = &n.st; out.push_back(f); lastSpace = true; return; }
        if (el.name == "img" || IsEmbedTag(el.name)) {
            Frag f;
            f.kind = el.name == "img" ? Frag::Image : Frag::Embed;
            const std::string* src = el.Attr(el.name == "object" ? "data" : "src");
            f.text = src ? *src : std::string();
            f.st = &n.st; f.sn = &n; f.link = link; f.breakBefore = true;
            out.push_back(f);
            lastSpace = false;
            return;
        }
        if (const std::string* id = el.Attr("id")) { Frag a; a.kind = Frag::Anchor; a.text = *id; a.st = &n.st; out.push_back(a); }
        if (el.name == "a") {
            if (const std::string* nm = el.Attr("name")) { Frag a; a.kind = Frag::Anchor; a.text = *nm; a.st = &n.st; out.push_back(a); }
            if (const std::string* href = el.Attr("href")) { link = (int)r.links.size(); r.links.push_back(*href); }
        }
        for (const SNode& k : n.kids) Flatten(k, link, out, lastSpace);
    }

    void FlattenText(const std::string& s, const ComputedStyle& st, int link, std::vector<Frag>& out, bool& lastSpace) {
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
            const bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
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

    // Size an image or embedded box for a containing block `cbW` wide.
    void SizeAtomic(Frag& f, float cbW) {
        const SNode& n = *f.sn;
        const Node& el = *n.node;
        float w = -1, h = -1;
        if (n.st.width.unit == Length::Px || n.st.width.unit == Length::Percent) w = n.st.width.Resolve(cbW);
        else if (el.Attr("width")) w = (float)AttrInt(el, "width", -1);
        if (n.st.height.unit == Length::Px) h = n.st.height.value;
        else if (el.Attr("height")) h = (float)AttrInt(el, "height", -1);
        if (f.kind == Frag::Image) {
            int iw = 0, ih = 0;
            const bool known = imageSize && imageSize(f.text, iw, ih) && iw > 0 && ih > 0;
            if (w < 0 && h < 0) { if (known) { w = (float)iw; h = (float)ih; } else { w = 0; h = 0; } }
            else if (w < 0) w = known ? h * (float)iw / (float)ih : h;
            else if (h < 0) h = known ? w * (float)ih / (float)iw : w;
        } else {
            if (w < 0) w = 300;
            if (h < 0) h = 150;
        }
        w = std::max(0.0f, Finite(w)); h = std::max(0.0f, Finite(h));
        if (n.st.maxWidth.unit != Length::None && n.st.maxWidth.unit != Length::Auto) {
            const float mw = std::max(0.0f, n.st.maxWidth.Resolve(cbW));
            if (w > mw && w > 0) { h = h * mw / w; w = mw; }
        }
        f.w = w; f.h = h;
    }

    struct Placed { const Frag* f; float x; float w; };

    // Lay out inline content in a box `cw` wide at (cx, cy) with `container`'s strut. Returns the height.
    float InlineContent(const std::vector<const SNode*>& items, const ComputedStyle& container, float cx, float cy, float cw, float& baseline) {
        std::vector<Frag> frags;
        bool lastSpace = true;
        for (const SNode* n : items) Flatten(*n, -1, frags, lastSpace);
        // measure
        for (Frag& f : frags) {
            if (f.kind == Frag::Word || f.kind == Frag::Space) f.w = Measure(f.text, *f.st);
            else if (f.kind == Frag::Image || f.kind == Frag::Embed) SizeAtomic(f, cw);
        }
        float y = cy;
        baseline = -1;
        std::vector<Placed> line;
        float x = 0;
        bool lineHasContent = false;
        bool anyLine = false;
        auto finish = [&](bool forced) {
            if (!lineHasContent && !forced) { line.clear(); x = 0; return; }
            // drop trailing collapsible spaces
            while (!line.empty() && line.back().f->kind == Frag::Space && line.back().f->st->whiteSpace != WhiteSpace::PreWrap) {
                x -= line.back().w; line.pop_back();
            }
            y += FinishLine(line, x, container, cx, y, cw, baseline);
            anyLine = true;
            line.clear(); x = 0; lineHasContent = false;
        };
        std::size_t i = 0;
        const std::size_t n = frags.size();
        while (i < n) {
            const Frag& f = frags[i];
            if (f.kind == Frag::Break) { finish(true); ++i; continue; }
            if (f.kind == Frag::Anchor) { line.push_back({&f, x, 0}); ++i; continue; }
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
                while (j < n && (frags[j].kind == Frag::Word || frags[j].kind == Frag::Anchor) && !frags[j].breakBefore) { uw += frags[j].w; ++j; }
            }
            const bool wraps = f.st->whiteSpace != WhiteSpace::Pre && f.st->whiteSpace != WhiteSpace::NoWrap;
            // the spaces before the unit count (it follows them); spaces left at a line's end are dropped by finish()
            if (lineHasContent && wraps && x + uw > cw + 0.01f && x > 0) finish(false);
            if (!lineHasContent && wraps && uw > cw + 0.01f && f.kind == Frag::Word) {
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
                            if (q > p || lineHasContent) { if (x + w + cwid > cw + 0.01f) break; }
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
        else if (!line.empty()) {   // only anchors (and spaces) left: they still need a position
            for (const Placed& p : line) if (p.f->kind == Frag::Anchor) r.anchors.push_back({p.f->text, y});
        }
        (void)anyLine;
        return y - cy;
    }
    std::deque<Frag> pieces;   // pieces of words broken between characters (a deque: the line keeps pointers to them)

    // Place one line: vertical alignment on the baseline (CSS 2.1 10.8), text-align, draw commands and link areas.
    float FinishLine(const std::vector<Placed>& line, float lineW, const ComputedStyle& container, float cx, float top, float cw, float& firstBaseline) {
        auto metrics = [](const ComputedStyle& st, float& asc, float& desc) {
            const float lh = st.LineHeightPx();
            const float half = (lh - st.fontSize) * 0.5f;
            asc = half + st.fontSize * 0.8f;
            desc = half + st.fontSize * 0.2f;
        };
        float maxA, maxD;
        metrics(container, maxA, maxD);   // the strut
        for (const Placed& p : line) {
            float a = 0, d = 0;
            if (p.f->kind == Frag::Word || p.f->kind == Frag::Space) metrics(*p.f->st, a, d);
            else if (p.f->kind == Frag::Image || p.f->kind == Frag::Embed) { a = p.f->h; d = 0; }
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
        DrawCmd* run = nullptr;   // the text command being extended
        for (const Placed& p : line) {
            const Frag& f = *p.f;
            const float x = Finite(cx + dx + p.x);
            if (f.kind == Frag::Anchor) { r.anchors.push_back({f.text, top}); run = nullptr; continue; }
            if (f.kind == Frag::Image || f.kind == Frag::Embed) {
                run = nullptr;
                if (f.w <= 0 || f.h <= 0) continue;
                DrawCmd c;
                c.kind = f.kind == Frag::Image ? DrawCmd::Image : DrawCmd::Embed;
                c.x = x; c.y = Finite(base - f.h); c.w = f.w; c.h = f.h;
                c.text = f.text;
                c.tag = f.sn->node->name;
                c.link = f.link;
                c.color = f.st->color;
                if (c.kind == DrawCmd::Embed) r.embeds.push_back((int)r.cmds.size());
                r.cmds.push_back(c);
                if (f.link >= 0) r.areas.push_back({c.x, c.y, c.w, c.h, f.link});
                continue;
            }
            // Word or Space: extend the run when the style and link are the same
            if (run && run->link == f.link && runStyle == f.st && std::fabs(run->x + run->w - x) < 0.01f) {
                run->text += f.text;
                run->w += p.w;
                if (f.link >= 0 && !r.areas.empty()) r.areas.back().w = run->w;
                continue;
            }
            if (f.kind == Frag::Space && f.link < 0 && !f.st->underline && !f.st->strike) {
                // a lone space starts no command, but the next word may continue from it
            }
            DrawCmd c;
            c.kind = DrawCmd::Text;
            c.x = x; c.y = Finite(base - f.st->fontSize * 0.8f); c.w = p.w; c.h = f.st->fontSize;
            c.text = f.text;
            c.font = FontOf(*f.st);
            c.color = f.st->color;
            c.underline = f.st->underline; c.strike = f.st->strike;
            c.link = f.link;
            r.cmds.push_back(c);
            run = &r.cmds.back();
            runStyle = f.st;
            if (f.link >= 0) r.areas.push_back({c.x, top, c.w, lineH, f.link});
        }
        // a run that is only spaces draws nothing: drop it
        return lineH;
    }
    const ComputedStyle* runStyle = nullptr;

    // ---- boxes ----

    int AddBox(const SNode& n, int depth, float x, float y, float w) {
        BoxInfo b;
        b.tag = n.node ? n.node->name : std::string("(anonymous)");
        b.depth = depth;
        b.x = x; b.y = y; b.w = w;
        b.firstCmd = r.cmds.size();
        r.boxes.push_back(b);
        draws.push_back(BoxDraw());
        return (int)r.boxes.size() - 1;
    }

    bool PaintsBackground(const SNode& n) const {
        if (n.st.background.a == 0) return false;
        if (n.node == htmlNode) return false;                        // the canvas
        if (n.node == bodyNode && canvasFromBody) return false;      // moved to the canvas (CSS 2.1 14.2)
        return true;
    }

    // Set a box's final height and draw its borders (called once its content is laid out).
    void FinishBox(int bi, const SNode& n, float h) {
        BoxInfo& b = r.boxes[bi];
        b.h = Finite(std::max(0.0f, h));
        BoxDraw& d = draws[bi];
        if (d.bg >= 0) r.cmds[d.bg].h = b.h;
        const float* bw = n.st.border;
        const float x = b.x, y = b.y, w = b.w, hh = b.h;
        const float rects[4][4] = {{x, y, w, bw[0]}, {x + w - bw[1], y + bw[0], bw[1], hh - bw[0] - bw[2]},
                                   {x, y + hh - bw[2], w, bw[2]}, {x, y + bw[0], bw[3], hh - bw[0] - bw[2]}};
        for (int k = 0; k < 4; ++k) {
            if (bw[k] <= 0 || n.st.borderColor[k].a == 0) continue;
            DrawCmd c;
            c.kind = DrawCmd::Fill;
            c.x = rects[k][0]; c.y = rects[k][1]; c.w = std::max(0.0f, rects[k][2]); c.h = std::max(0.0f, rects[k][3]);
            c.color = n.st.borderColor[k];
            d.border[k] = (int)r.cmds.size();
            r.cmds.push_back(c);
        }
    }

    // Make a finished box taller (table cells stretched to their row).
    void Stretch(int bi, const SNode& n, float h) {
        BoxInfo& b = r.boxes[bi];
        if (h <= b.h) return;
        const float grow = h - b.h;
        b.h = h;
        BoxDraw& d = draws[bi];
        if (d.bg >= 0) r.cmds[d.bg].h = h;
        if (d.border[1] >= 0) r.cmds[d.border[1]].h += grow;
        if (d.border[3] >= 0) r.cmds[d.border[3]].h += grow;
        if (d.border[2] >= 0) r.cmds[d.border[2]].y += grow;
        (void)n;
    }

    // A block-level box whose border box starts at (x + margin-left, y) in a containing block cbW wide.
    // Returns the border-box height and the first baseline; `boxIndex` gets its entry in r.boxes.
    Out Block(const SNode& n, float x, float y, float cbW, int depth, int ordinal, int* boxIndex = nullptr, bool cell = false) {
        const ComputedStyle& st = n.st;
        const float bl = st.border[3], br = st.border[1], bt = st.border[0], bb = st.border[2];
        const float pl = st.padding[3].Resolve(cbW), pr = st.padding[1].Resolve(cbW);
        const float pt = st.padding[0].Resolve(cbW), pb = st.padding[2].Resolve(cbW);
        const bool autoL = st.margin[3].unit == Length::Auto, autoR = st.margin[1].unit == Length::Auto;
        float ml = cell ? 0.0f : st.margin[3].Resolve(cbW), mr = cell ? 0.0f : st.margin[1].Resolve(cbW);
        float w;   // content width
        const bool hasWidth = !cell && (st.width.unit == Length::Px || st.width.unit == Length::Percent);
        const bool hasMax = !cell && (st.maxWidth.unit == Length::Px || st.maxWidth.unit == Length::Percent);
        if (hasWidth) w = st.width.Resolve(cbW);
        else w = cbW - ml - mr - bl - br - pl - pr;
        if (hasMax) w = std::min(w, st.maxWidth.Resolve(cbW));
        w = std::max(0.0f, Finite(w));
        if (hasWidth || hasMax) {
            const float rest = cbW - w - bl - br - pl - pr;
            if (autoL && autoR) ml = mr = std::max(0.0f, rest * 0.5f);
            else if (autoL) ml = std::max(0.0f, rest - mr);
        }
        const float bx = Finite(x + ml);
        const float bwid = w + bl + br + pl + pr;
        const int bi = AddBox(n, depth, bx, y, bwid);
        if (boxIndex) *boxIndex = bi;
        if (n.node) {
            if (const std::string* id = n.node->Attr("id")) r.anchors.push_back({*id, y});
            if (n.node->name == "a") if (const std::string* nm = n.node->Attr("name")) r.anchors.push_back({*nm, y});
        }
        if (PaintsBackground(n)) {
            DrawCmd c;
            c.kind = DrawCmd::Fill;
            c.x = bx; c.y = y; c.w = bwid; c.color = st.background;
            draws[bi].bg = (int)r.cmds.size();
            r.cmds.push_back(c);
        }
        const float cx = bx + bl + pl, cy = y + bt + pt;
        Out content;
        if (st.display == Display::Table) content = Table(n, cx, cy, w, depth + 1);
        else content = Contents(n, cx, cy, w, depth + 1);
        float ch = content.h;
        if (st.height.unit == Length::Px) ch = st.height.value;
        const float h = bt + pt + ch + pb + bb;
        if (st.display == Display::ListItem && st.listStyle != ListStyle::None) {
            const float base = content.baseline >= 0 ? content.baseline : cy + (st.LineHeightPx() - st.fontSize) * 0.5f + st.fontSize * 0.8f;
            Marker(st, cx, base, ordinal);
        }
        FinishBox(bi, n, h);
        Out o;
        o.h = h;
        o.baseline = content.baseline;
        return o;
    }

    void Marker(const ComputedStyle& st, float cx, float base, int ordinal) {
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
        r.cmds.push_back(c);
    }

    // The content of a block container: block children (with anonymous blocks around inline runs) or inline content.
    Out Contents(const SNode& n, float cx, float cy, float cw, int depth) {
        Out o;
        bool anyBlock = false;
        for (const SNode& k : n.kids) if (IsBlockLevel(k)) { anyBlock = true; break; }
        if (!anyBlock) {
            std::vector<const SNode*> items;
            for (const SNode& k : n.kids) items.push_back(&k);
            float base = -1;
            o.h = InlineContent(items, n.st, cx, cy, cw, base);
            o.baseline = base;
            return o;
        }
        float y = cy;
        float prevMb = 0;
        bool first = true;
        // list numbering (CSS 2.1 12.4 / the HTML standard's ordinal values)
        int counter = 1;
        if (n.node && n.node->name == "ol") counter = AttrInt(*n.node, "start", 1);
        std::size_t i = 0;
        while (i < n.kids.size()) {
            if (!IsBlockLevel(n.kids[i])) {
                std::vector<const SNode*> items;
                while (i < n.kids.size() && !IsBlockLevel(n.kids[i])) items.push_back(&n.kids[i++]);
                if (OnlyCollapsibleSpace(items)) continue;
                y += prevMb; prevMb = 0; first = false;
                SNode anon;
                anon.st = n.st;   // inherits the text properties; no box of its own to draw
                const int bi = AddBox(anon, depth, cx, y, cw);
                float base = -1;
                const float h = InlineContent(items, n.st, cx, y, cw, base);
                r.boxes[bi].h = h;
                if (o.baseline < 0) o.baseline = base;
                y += h;
                continue;
            }
            const SNode& k = n.kids[i++];
            const float mt = k.st.margin[0].Resolve(cw), mb = k.st.margin[2].Resolve(cw);
            y += first ? mt : CollapseMargins(prevMb, mt);
            first = false;
            int ordinal = 0;
            if (k.st.display == Display::ListItem) {
                if (k.node) counter = AttrInt(*k.node, "value", counter);
                ordinal = counter;
                counter = counter < 1000000000 ? counter + 1 : counter;
            }
            const Out ko = Block(k, cx, y, cw, depth, ordinal);
            if (o.baseline < 0) o.baseline = ko.baseline;
            y += ko.h;
            prevMb = mb;
        }
        y += prevMb;
        o.h = y - cy;
        return o;
    }

    static bool OnlyCollapsibleSpace(const std::vector<const SNode*>& items) {
        for (const SNode* s : items) {
            if (!s->text) return false;
            if (s->st.whiteSpace == WhiteSpace::Pre || s->st.whiteSpace == WhiteSpace::PreWrap) return false;
            for (char c : s->node->data) if (!(c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f')) return false;
        }
        return true;
    }

    // A table: rows (directly or in row groups), cells in equal columns, 2 px spacing.
    Out Table(const SNode& n, float cx, float cy, float cw, int depth) {
        std::vector<const SNode*> rows;
        for (const SNode& k : n.kids) {
            if (k.text) continue;
            if (k.st.display == Display::TableRow) rows.push_back(&k);
            else if (k.st.display == Display::TableSection)
                for (const SNode& g : k.kids) if (!g.text && g.st.display == Display::TableRow) rows.push_back(&g);
        }
        std::size_t cols = 0;
        for (const SNode* row : rows) {
            std::size_t c = 0;
            for (const SNode& cell : row->kids) if (!cell.text) ++c;
            cols = std::max(cols, c);
        }
        cols = std::min<std::size_t>(cols, 1000);
        const float spacing = 2.0f;
        Out o;
        if (rows.empty() || cols == 0) return o;
        const float colW = std::max(0.0f, (cw - spacing * (float)(cols + 1)) / (float)cols);
        float y = cy + spacing;
        for (const SNode* row : rows) {
            const int rowBox = AddBox(*row, depth, cx, y, cw);
            std::vector<std::pair<int, const SNode*>> cells;
            float rowH = 0;
            std::size_t c = 0;
            for (const SNode& cell : row->kids) {
                if (cell.text) continue;
                if (c >= cols) break;
                const float x = cx + spacing + (float)c * (colW + spacing);
                int bi = -1;
                const Out co = Block(cell, x, y, colW, depth + 1, 0, &bi, /*cell=*/true);
                if (o.baseline < 0) o.baseline = co.baseline;
                rowH = std::max(rowH, co.h);
                cells.push_back({bi, &cell});
                ++c;
            }
            for (const auto& cell : cells) Stretch(cell.first, *cell.second, rowH);
            r.boxes[rowBox].h = rowH;
            y += rowH + spacing;
        }
        o.h = y - cy;
        return o;
    }
};

} // namespace

LayoutResult LayoutPage(const HtmlDocument& doc, float widthIn, const TextMeasure& measure, const ImageSizeFn& imageSize) {
    LayoutResult r;
    const float width = std::max(0.0f, Finite(widthIn));
    r.width = width;
    const Node* html = doc.Html();
    if (!html) return r;
    std::vector<StyleSheet> parsed;
    parsed.reserve(doc.styleSheets.size());
    for (const std::string& css : doc.styleSheets) parsed.push_back(ParseStyleSheet(css));
    std::vector<const StyleSheet*> sheets;
    for (const StyleSheet& s : parsed) sheets.push_back(&s);
    SNode root;
    if (!Build(*html, ComputedStyle(), sheets, root)) return r;
    if (root.st.display == Display::Inline) root.st.display = Display::Block;

    Layouter L(measure, imageSize, r, doc.Body(), html);
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
    const float mt = root.st.margin[0].Resolve(width);
    const Layouter::Out o = L.Block(root, 0, mt, width, 0, 0);
    r.height = Finite(mt + o.h + root.st.margin[2].Resolve(width));
    // drop text runs that are only spaces and draw nothing
    std::vector<DrawCmd> kept;
    kept.reserve(r.cmds.size());
    std::vector<int> remap(r.cmds.size(), -1);
    for (std::size_t i = 0; i < r.cmds.size(); ++i) {
        const DrawCmd& c = r.cmds[i];
        if (c.kind == DrawCmd::Text && !c.underline && !c.strike && c.text.find_first_not_of(' ') == std::string::npos) continue;
        remap[i] = (int)kept.size();
        kept.push_back(c);
    }
    for (BoxInfo& b : r.boxes) {
        std::size_t f = b.firstCmd;
        while (f < remap.size() && remap[f] < 0) ++f;
        b.firstCmd = f < remap.size() ? (std::size_t)remap[f] : kept.size();
    }
    for (int& e : r.embeds) e = remap[(std::size_t)e];
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

int LinkAt(const LayoutResult& r, float x, float y) {
    for (const LinkArea& a : r.areas) if (x >= a.x && x < a.x + a.w && y >= a.y && y < a.y + a.h) return a.link;
    return -1;
}

} // namespace okay::browser
