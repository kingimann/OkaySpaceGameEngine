#pragma once
// Rich text for UI labels: colour and bold / italic tags inside the text.
//
//   "Found the [color=#ffcc33]golden key[/color] and [b]one[/b] [i]coin[/i]."
//
// Tags: [color=#rgb] [color=#rrggbb] [color=#rrggbbaa] [color=name] ... [/color], [b] ... [/b], [i] ... [/i],
// [size=N] ... [/size] (N is a percent of the text's size, 25 to 400; nested sizes multiply).
// Colour names: white black red green blue yellow orange gold cyan magenta purple pink gray.
// A tag that is not one of these (or has a bad colour) is shown as ordinary text. "[[" shows a "[".
// Tags nest; a closing tag with nothing open is shown as text.
//
// The parser has no drawing or font code, so tools and tests use it directly. The text is laid out
// without the tags (wrapping, alignment, typing effect all see the plain text); each visible
// character (not a space or line break) carries one style.
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

struct RichStyle {
    bool hasColor = false;
    std::uint8_t r = 255, g = 255, b = 255, a = 255;
    bool bold = false, italic = false;
    float size = 1.0f;   ///< scale of the text's size ([size=150] gives 1.5)
    bool operator==(const RichStyle& o) const {
        return hasColor == o.hasColor && (!hasColor || (r == o.r && g == o.g && b == o.b && a == o.a)) &&
               bold == o.bold && italic == o.italic && size == o.size;
    }
};

/// The tag-free text and one style per visible character (characters that are not ' ', '\t' or '\n'),
/// in order.
struct RichText {
    std::string plain;
    std::vector<RichStyle> styles;
};

namespace richtext_detail {
inline int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
inline bool ParseColor(const std::string& v, RichStyle& st) {
    static const struct { const char* n; std::uint8_t r, g, b; } kNames[] = {
        {"white", 255, 255, 255}, {"black", 0, 0, 0}, {"red", 230, 70, 70}, {"green", 90, 210, 120},
        {"blue", 80, 150, 255}, {"yellow", 250, 220, 80}, {"orange", 255, 150, 50}, {"gold", 255, 204, 51},
        {"cyan", 80, 220, 240}, {"magenta", 240, 90, 220}, {"purple", 170, 110, 240}, {"pink", 255, 150, 190},
        {"gray", 150, 154, 165}};
    if (!v.empty() && v[0] == '#') {
        const std::size_t n = v.size() - 1;
        int h[8];
        if (n != 3 && n != 6 && n != 8) return false;
        for (std::size_t i = 0; i < n; ++i) { h[i] = Hex(v[i + 1]); if (h[i] < 0) return false; }
        if (n == 3) { st.r = (std::uint8_t)(h[0] * 17); st.g = (std::uint8_t)(h[1] * 17); st.b = (std::uint8_t)(h[2] * 17); st.a = 255; }
        else {
            st.r = (std::uint8_t)(h[0] * 16 + h[1]); st.g = (std::uint8_t)(h[2] * 16 + h[3]); st.b = (std::uint8_t)(h[4] * 16 + h[5]);
            st.a = n == 8 ? (std::uint8_t)(h[6] * 16 + h[7]) : 255;
        }
        st.hasColor = true;
        return true;
    }
    std::string low;
    for (char c : v) low.push_back((char)((c >= 'A' && c <= 'Z') ? c + 32 : c));
    for (const auto& e : kNames)
        if (low == e.n) { st.r = e.r; st.g = e.g; st.b = e.b; st.a = 255; st.hasColor = true; return true; }
    return false;
}
} // namespace richtext_detail

/// Does the text hold anything that looks like a tag? (A quick check so plain text skips the parser.)
inline bool RichTextHasTags(const std::string& s) { return s.find('[') != std::string::npos; }

/// Read `marked` into plain text and per-character styles.
inline RichText ParseRichText(const std::string& marked) {
    using namespace richtext_detail;
    RichText out;
    out.plain.reserve(marked.size());
    struct Scope { char kind; RichStyle colour; };   // kind: 'c' colour, 'b' bold, 'i' italic, 's' size (colour.size)
    std::vector<Scope> open;
    RichStyle cur;
    auto refresh = [&]() {
        cur = RichStyle();
        for (const Scope& sc : open) {
            if (sc.kind == 'c') { cur.hasColor = true; cur.r = sc.colour.r; cur.g = sc.colour.g; cur.b = sc.colour.b; cur.a = sc.colour.a; }
            else if (sc.kind == 'b') cur.bold = true;
            else if (sc.kind == 's') cur.size *= sc.colour.size;
            else cur.italic = true;
        }
    };
    for (std::size_t i = 0; i < marked.size();) {
        const char c = marked[i];
        if (c == '[') {
            if (i + 1 < marked.size() && marked[i + 1] == '[') { out.plain.push_back('['); out.styles.push_back(cur); i += 2; continue; }
            const std::size_t close = marked.find(']', i + 1);
            if (close != std::string::npos && close - i <= 24) {
                const std::string tag = marked.substr(i + 1, close - i - 1);
                bool ok = false;
                if (tag.rfind("color=", 0) == 0) {
                    RichStyle col;
                    if (ParseColor(tag.substr(6), col)) { open.push_back({'c', col}); ok = true; }
                } else if (tag.rfind("size=", 0) == 0) {
                    const std::string v = tag.substr(5);
                    int pct = 0;
                    bool digits = !v.empty() && v.size() <= 3;
                    for (char d : v) { if (d < '0' || d > '9') { digits = false; break; } pct = pct * 10 + (d - '0'); }
                    if (digits && pct >= 25 && pct <= 400) {
                        RichStyle s; s.size = (float)pct / 100.0f;
                        open.push_back({'s', s}); ok = true;
                    }
                } else if (tag == "b") { open.push_back({'b', RichStyle()}); ok = true; }
                else if (tag == "i") { open.push_back({'i', RichStyle()}); ok = true; }
                else if (tag == "/color" || tag == "/b" || tag == "/i" || tag == "/size") {
                    const char want = tag[1];
                    for (std::size_t k = open.size(); k-- > 0;)
                        if (open[k].kind == want) { open.erase(open.begin() + (std::ptrdiff_t)k); ok = true; break; }
                }
                if (ok) { refresh(); i = close + 1; continue; }
            }
        }
        out.plain.push_back(c);
        // one style per visible character, set at its first byte
        if (c != ' ' && c != '\t' && c != '\n' && ((unsigned char)c & 0xC0) != 0x80) out.styles.push_back(cur);
        ++i;
    }
    return out;
}

/// The text without its tags.
inline std::string StripRichText(const std::string& marked) {
    if (!RichTextHasTags(marked)) return marked;
    return ParseRichText(marked).plain;
}

/// A piece of one line of rich text drawn in one style. A space joins the run it sits in.
struct RichRun {
    int line = 0;          ///< 0 for the first line
    std::string text;      ///< plain text of the run (no tags, no line break)
    RichStyle style;
    std::string before;    ///< the plain text of the line before this run (for placing it)
};

/// Split rich text into runs of one style, line by line, so a host without its own rich-text drawing (the
/// editor's scene view, world text) can draw each run in its colour, weight and size.
inline std::vector<RichRun> RichTextRuns(const RichText& rt) {
    std::vector<RichRun> out;
    std::size_t glyph = 0, lineStart = 0;
    int line = 0;
    const std::string& disp = rt.plain;
    while (lineStart <= disp.size()) {
        std::size_t lineEnd = disp.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = disp.size();
        const std::string ln = disp.substr(lineStart, lineEnd - lineStart);
        std::size_t runStart = 0;
        RichStyle runStyle;
        bool have = false;
        auto flush = [&](std::size_t end) {
            if (!have || end <= runStart) return;
            out.push_back(RichRun{line, ln.substr(runStart, end - runStart), runStyle, ln.substr(0, runStart)});
        };
        for (std::size_t i = 0; i < ln.size(); ++i) {
            const char c = ln[i];
            if (c == ' ' || c == '\t' || ((unsigned char)c & 0xC0) == 0x80) continue;
            const RichStyle st = glyph < rt.styles.size() ? rt.styles[glyph] : RichStyle();
            ++glyph;
            if (!have) { runStart = i; runStyle = st; have = true; }
            else if (!(st == runStyle)) { flush(i); runStart = i; runStyle = st; }
        }
        flush(ln.size());
        ++line;
        if (lineEnd >= disp.size()) break;
        lineStart = lineEnd + 1;
    }
    return out;
}

} // namespace okay
