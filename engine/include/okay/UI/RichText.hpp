#pragma once
// Rich text for UI labels: colour and bold / italic tags inside the text.
//
//   "Found the [color=#ffcc33]golden key[/color] and [b]one[/b] [i]coin[/i]."
//
// Tags: [color=#rgb] [color=#rrggbb] [color=#rrggbbaa] [color=name] ... [/color], [b] ... [/b], [i] ... [/i].
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
    bool operator==(const RichStyle& o) const {
        return hasColor == o.hasColor && (!hasColor || (r == o.r && g == o.g && b == o.b && a == o.a)) &&
               bold == o.bold && italic == o.italic;
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
    struct Scope { char kind; RichStyle colour; };   // kind: 'c' colour, 'b' bold, 'i' italic
    std::vector<Scope> open;
    RichStyle cur;
    auto refresh = [&]() {
        cur = RichStyle();
        for (const Scope& sc : open) {
            if (sc.kind == 'c') { cur.hasColor = true; cur.r = sc.colour.r; cur.g = sc.colour.g; cur.b = sc.colour.b; cur.a = sc.colour.a; }
            else if (sc.kind == 'b') cur.bold = true;
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
                } else if (tag == "b") { open.push_back({'b', RichStyle()}); ok = true; }
                else if (tag == "i") { open.push_back({'i', RichStyle()}); ok = true; }
                else if (tag == "/color" || tag == "/b" || tag == "/i") {
                    const char want = tag[1] == 'c' ? 'c' : tag[1];
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

} // namespace okay
