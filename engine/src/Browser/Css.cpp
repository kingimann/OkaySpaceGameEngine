// Browser (page engine): style sheets. Written from CSS Syntax Level 3, Selectors Level 3, CSS 2.1 and CSS Color Level 3
// (see Css.hpp for what is read).
#include "okay/Browser/Css.hpp"

#include "okay/Core/StringUtil.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace okay::browser {

namespace {

bool IsWs(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

using okay::str::Trim;
using okay::str::LowerAscii;

bool IsIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || (unsigned char)c >= 0x80;
}

// The text with /* comments */ removed (strings are kept as they are).
std::string StripComments(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    const std::size_t n = in.size();
    for (std::size_t i = 0; i < n;) {
        const char c = in[i];
        if (c == '"' || c == '\'') {
            std::size_t j = i + 1;
            while (j < n && in[j] != c && in[j] != '\n') { if (in[j] == '\\' && j + 1 < n) ++j; ++j; }
            if (j < n && in[j] == c) ++j;
            out.append(in, i, j - i);
            i = j;
            continue;
        }
        if (c == '/' && i + 1 < n && in[i + 1] == '*') {
            const std::size_t e = in.find("*/", i + 2);
            i = e == std::string::npos ? n : e + 2;
            out += ' ';
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

// From `i` (just after an opening '{'), the index of the matching '}' (or n). Strings and nested blocks are skipped.
std::size_t BlockEnd(const std::string& s, std::size_t i) {
    const std::size_t n = s.size();
    int depth = 1;
    while (i < n) {
        const char c = s[i];
        if (c == '"' || c == '\'') {
            ++i;
            while (i < n && s[i] != c && s[i] != '\n') { if (s[i] == '\\' && i + 1 < n) ++i; ++i; }
            if (i < n) ++i;
            continue;
        }
        if (c == '{') ++depth;
        else if (c == '}' && --depth == 0) return i;
        ++i;
    }
    return n;
}

// Split a value into its top-level words (spaces outside brackets and strings; a ',' or '/' is a word of its own).
std::vector<std::string> Words(const std::string& v) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    char quote = 0;
    for (char c : v) {
        if (quote) { cur += c; if (c == quote) quote = 0; continue; }
        if (c == '"' || c == '\'') { quote = c; cur += c; continue; }
        if (c == '(') ++depth;
        if (c == ')' && depth > 0) --depth;
        if (depth == 0 && (IsWs(c) || c == ',' || c == '/')) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
            if (c == ',' || c == '/') out.push_back(std::string(1, c));
            continue;
        }
        cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// ---- numbers and lengths ----

bool ParseNumber(const std::string& s, float& out, std::string& unit) {
    if (s.empty()) return false;
    const char* b = s.c_str();
    char* e = nullptr;
    const double v = std::strtod(b, &e);
    if (e == b || !std::isfinite(v)) return false;
    // strtod accepts "inf", "nan" and hex: CSS numbers are only [+-]digits[.digits][e[+-]digits]
    const char c0 = (b[0] == '+' || b[0] == '-') ? b[1] : b[0];
    if (!((c0 >= '0' && c0 <= '9') || c0 == '.')) return false;
    for (const char* p = b; p < e; ++p) if (*p == 'x' || *p == 'X') return false;
    out = (float)std::max(-1e7, std::min(1e7, v));
    unit = LowerAscii(std::string(e));
    return true;
}

// A length to px: em relative to `em`; % kept (returned as Percent) when allowed.
bool ParseLength(const std::string& s, float em, bool allowPercent, Length& out) {
    float v = 0;
    std::string unit;
    if (!ParseNumber(s, v, unit)) return false;
    if (unit.empty()) { if (v != 0.0f) return false; out = Length::Px_(0); return true; }
    if (unit == "%") { if (!allowPercent) return false; out.unit = Length::Percent; out.value = v; return true; }
    float px;
    if (unit == "px") px = v;
    else if (unit == "em") px = v * em;
    else if (unit == "rem") px = v * 16.0f;
    else if (unit == "ex" || unit == "ch") px = v * em * 0.5f;
    else if (unit == "pt") px = v * 96.0f / 72.0f;
    else if (unit == "pc") px = v * 16.0f;
    else if (unit == "in") px = v * 96.0f;
    else if (unit == "cm") px = v * 96.0f / 2.54f;
    else if (unit == "mm") px = v * 96.0f / 25.4f;
    else if (unit == "q") px = v * 96.0f / 101.6f;
    else return false;
    out = Length::Px_(px);
    return true;
}

// ---- colours ----

struct NamedColor { const char* name; std::uint32_t rgb; };
const NamedColor kColors[] = {
    {"black", 0x000000}, {"silver", 0xC0C0C0}, {"gray", 0x808080}, {"grey", 0x808080}, {"white", 0xFFFFFF}, {"maroon", 0x800000},
    {"red", 0xFF0000}, {"purple", 0x800080}, {"fuchsia", 0xFF00FF}, {"magenta", 0xFF00FF}, {"green", 0x008000}, {"lime", 0x00FF00},
    {"olive", 0x808000}, {"yellow", 0xFFFF00}, {"navy", 0x000080}, {"blue", 0x0000FF}, {"teal", 0x008080}, {"aqua", 0x00FFFF},
    {"cyan", 0x00FFFF}, {"orange", 0xFFA500}, {"pink", 0xFFC0CB}, {"brown", 0xA52A2A}, {"gold", 0xFFD700}, {"indigo", 0x4B0082},
    {"violet", 0xEE82EE}, {"crimson", 0xDC143C}, {"coral", 0xFF7F50}, {"salmon", 0xFA8072}, {"tomato", 0xFF6347},
    {"orchid", 0xDA70D6}, {"khaki", 0xF0E68C}, {"beige", 0xF5F5DC}, {"ivory", 0xFFFFF0}, {"lavender", 0xE6E6FA},
    {"tan", 0xD2B48C}, {"chocolate", 0xD2691E}, {"turquoise", 0x40E0D0}, {"skyblue", 0x87CEEB}, {"steelblue", 0x4682B4},
    {"royalblue", 0x4169E1}, {"dodgerblue", 0x1E90FF}, {"slategray", 0x708090}, {"slategrey", 0x708090},
    {"darkgray", 0xA9A9A9}, {"darkgrey", 0xA9A9A9}, {"lightgray", 0xD3D3D3}, {"lightgrey", 0xD3D3D3}, {"gainsboro", 0xDCDCDC},
    {"whitesmoke", 0xF5F5F5}, {"dimgray", 0x696969}, {"dimgrey", 0x696969}, {"darkred", 0x8B0000}, {"darkgreen", 0x006400},
    {"darkblue", 0x00008B}, {"darkorange", 0xFF8C00}, {"lightblue", 0xADD8E6}, {"lightgreen", 0x90EE90},
    {"lightyellow", 0xFFFFE0}, {"limegreen", 0x32CD32}, {"forestgreen", 0x228B22}, {"seagreen", 0x2E8B57}, {"midnightblue", 0x191970},
    {"firebrick", 0xB22222}, {"goldenrod", 0xDAA520}, {"plum", 0xDDA0DD}, {"rebeccapurple", 0x663399}, {"aliceblue", 0xF0F8FF},
    {"ghostwhite", 0xF8F8FF}, {"snow", 0xFFFAFA}, {"linen", 0xFAF0E6}, {"mintcream", 0xF5FFFA}, {"honeydew", 0xF0FFF0},
    {"azure", 0xF0FFFF}, {"cornflowerblue", 0x6495ED}, {"darkslategray", 0x2F4F4F}, {"darkslategrey", 0x2F4F4F},
    {"lightslategray", 0x778899}, {"hotpink", 0xFF69B4}, {"deeppink", 0xFF1493}, {"chartreuse", 0x7FFF00}, {"olivedrab", 0x6B8E23},
    {"sienna", 0xA0522D}, {"peru", 0xCD853F}, {"wheat", 0xF5DEB3}, {"moccasin", 0xFFE4B5}, {"navajowhite", 0xFFDEAD},
};

int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::uint8_t Clamp255(float v) { return (std::uint8_t)std::lround(std::max(0.0f, std::min(255.0f, v))); }

} // namespace

bool ParseColor(const std::string& textIn, Color& out) {
    const std::string text = LowerAscii(Trim(textIn));
    if (text.empty()) return false;
    if (text[0] == '#') {
        const std::string h = text.substr(1);
        for (char c : h) if (Hex(c) < 0) return false;
        Color c;
        if (h.size() == 3 || h.size() == 4) {
            c.r = (std::uint8_t)(Hex(h[0]) * 17); c.g = (std::uint8_t)(Hex(h[1]) * 17); c.b = (std::uint8_t)(Hex(h[2]) * 17);
            c.a = h.size() == 4 ? (std::uint8_t)(Hex(h[3]) * 17) : 255;
        } else if (h.size() == 6 || h.size() == 8) {
            c.r = (std::uint8_t)(Hex(h[0]) * 16 + Hex(h[1])); c.g = (std::uint8_t)(Hex(h[2]) * 16 + Hex(h[3]));
            c.b = (std::uint8_t)(Hex(h[4]) * 16 + Hex(h[5]));
            c.a = h.size() == 8 ? (std::uint8_t)(Hex(h[6]) * 16 + Hex(h[7])) : 255;
        } else return false;
        out = c;
        return true;
    }
    if (text.compare(0, 4, "rgb(") == 0 || text.compare(0, 5, "rgba(") == 0) {
        const std::size_t open = text.find('('), close = text.rfind(')');
        if (close == std::string::npos || close < open) return false;
        std::vector<std::string> parts;
        for (const std::string& w : Words(text.substr(open + 1, close - open - 1))) if (w != ",") parts.push_back(w);
        // "r g b / a" or "r, g, b, a": drop the slash
        std::vector<std::string> v;
        for (const std::string& p : parts) if (p != "/") v.push_back(p);
        if (v.size() != 3 && v.size() != 4) return false;
        float ch[4] = {0, 0, 0, 255};
        for (std::size_t k = 0; k < v.size(); ++k) {
            float num = 0;
            std::string unit;
            if (!ParseNumber(v[k], num, unit)) return false;
            if (unit == "%") ch[k] = k < 3 ? num * 255.0f / 100.0f : num * 255.0f / 100.0f;
            else if (unit.empty()) ch[k] = k < 3 ? num : num * 255.0f;
            else return false;
        }
        out = Color{Clamp255(ch[0]), Clamp255(ch[1]), Clamp255(ch[2]), Clamp255(ch[3])};
        return true;
    }
    if (text == "transparent") { out = Color{0, 0, 0, 0}; return true; }
    for (const NamedColor& nc : kColors) {
        if (text == nc.name) {
            out = Color{(std::uint8_t)(nc.rgb >> 16), (std::uint8_t)((nc.rgb >> 8) & 0xFF), (std::uint8_t)(nc.rgb & 0xFF), 255};
            return true;
        }
    }
    return false;
}

// ---- selectors ---------------------------------------------------------------------------------------------------------

bool ParseSelector(const std::string& textIn, Selector& out) {
    out = Selector();
    const std::string text = Trim(textIn);
    if (text.empty()) return false;
    const std::size_t n = text.size();
    std::size_t i = 0;
    char pending = 0;   // combinator before the next compound
    int ids = 0, classes = 0, types = 0;
    while (i < n) {
        if (IsWs(text[i]) || text[i] == '>') {
            char comb = ' ';
            while (i < n && (IsWs(text[i]) || text[i] == '>')) { if (text[i] == '>') { if (comb == '>') return false; comb = '>'; } ++i; }
            if (i >= n) return comb == ' ' && !out.parts.empty() ? true : false;
            if (out.parts.empty()) return false;
            pending = comb;
            continue;
        }
        Compound c;
        bool any = false;
        if (text[i] == '*') { c.tag = "*"; ++i; any = true; }
        else if (IsIdentChar(text[i]) && !(text[i] >= '0' && text[i] <= '9')) {
            std::size_t j = i;
            while (j < n && IsIdentChar(text[j])) ++j;
            c.tag = LowerAscii(text.substr(i, j - i));
            i = j; any = true; ++types;
        }
        while (i < n && (text[i] == '#' || text[i] == '.')) {
            const char kind = text[i++];
            std::size_t j = i;
            while (j < n && IsIdentChar(text[j])) ++j;
            if (j == i) return false;
            const std::string name = text.substr(i, j - i);
            if (kind == '#') { if (!c.id.empty() && c.id != name) c.id = "\x01"; else c.id = name; ++ids; }   // two ids never match
            else { c.classes.push_back(name); ++classes; }
            i = j; any = true;
        }
        if (!any) return false;
        if (i < n && !IsWs(text[i]) && text[i] != '>') return false;   // ':', '[', '+', '~', ... : not supported
        if (!out.parts.empty()) out.combinators.push_back(pending ? pending : ' ');
        pending = 0;
        out.parts.push_back(std::move(c));
    }
    out.specificity = std::min(ids, 99) * 10000 + std::min(classes, 99) * 100 + std::min(types, 99);
    return !out.parts.empty();
}

namespace {

bool CompoundMatches(const Compound& c, const Node& el) {
    if (el.kind != NodeKind::Element) return false;
    if (!c.tag.empty() && c.tag != "*" && c.tag != el.name) return false;
    if (!c.id.empty()) {
        const std::string* id = el.Attr("id");
        if (!id || *id != c.id) return false;
    }
    if (!c.classes.empty()) {
        const std::string* cls = el.Attr("class");
        if (!cls) return false;
        for (const std::string& want : c.classes) {
            bool found = false;
            const std::string& s = *cls;
            for (std::size_t k = 0; k < s.size() && !found;) {
                while (k < s.size() && IsWs(s[k])) ++k;
                std::size_t e = k;
                while (e < s.size() && !IsWs(s[e])) ++e;
                if (e > k && s.compare(k, e - k, want) == 0 && e - k == want.size()) found = true;
                k = e;
            }
            if (!found) return false;
        }
    }
    return true;
}

} // namespace

bool Matches(const Selector& sel, const Node& el) {
    if (sel.parts.empty() || !CompoundMatches(sel.parts.back(), el)) return false;
    if (sel.parts.size() == 1) return true;
    // chain[0] is el, chain[k] its k-th ancestor element. ok[i][j]: parts[0..i] match with part i on chain[j].
    std::vector<const Node*> chain;
    for (const Node* n = &el; n && n->kind == NodeKind::Element; n = n->parent) chain.push_back(n);
    const std::size_t P = sel.parts.size(), C = chain.size();
    // Walk from the leftmost part: can[i][j] = parts 0..i match, with part i on chain[j].
    std::vector<char> can(P * C, 0);
    for (std::size_t j = 0; j < C; ++j) can[j] = CompoundMatches(sel.parts[0], *chain[j]) ? 1 : 0;
    for (std::size_t i = 1; i < P; ++i) {
        const char comb = sel.combinators[i - 1];
        for (std::size_t j = 0; j < C; ++j) {
            if (i == P - 1 && j != 0) continue;              // the last part sits on el itself
            if (!CompoundMatches(sel.parts[i], *chain[j])) continue;
            bool ok = false;
            if (comb == '>') ok = j + 1 < C && can[(i - 1) * C + j + 1];
            else for (std::size_t k = j + 1; k < C && !ok; ++k) ok = can[(i - 1) * C + k] != 0;
            can[i * C + j] = ok ? 1 : 0;
        }
    }
    return can[(P - 1) * C + 0] != 0;
}

// ---- sheets ------------------------------------------------------------------------------------------------------------

std::vector<Declaration> ParseDeclarations(const std::string& textIn, const CssLimits& limits) {
    const std::string text = StripComments(textIn);
    std::vector<Declaration> out;
    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n && out.size() < limits.maxDeclarations) {
        // one declaration: up to ';' at the top level
        std::size_t j = i;
        int depth = 0;
        while (j < n) {
            const char c = text[j];
            if (c == '"' || c == '\'') {
                ++j;
                while (j < n && text[j] != c && text[j] != '\n') { if (text[j] == '\\' && j + 1 < n) ++j; ++j; }
                if (j < n) ++j;
                continue;
            }
            if (c == '(' || c == '[' || c == '{') ++depth;
            else if ((c == ')' || c == ']' || c == '}') && depth > 0) --depth;
            else if (c == ';' && depth == 0) break;
            ++j;
        }
        const std::string d = text.substr(i, j - i);
        i = j + 1;
        const std::size_t colon = d.find(':');
        if (colon == std::string::npos) continue;
        Declaration decl;
        decl.property = LowerAscii(Trim(d.substr(0, colon)));
        if (decl.property.empty()) continue;
        bool identOk = true;
        for (char c : decl.property) if (!IsIdentChar(c)) identOk = false;
        if (!identOk) continue;
        std::string v = Trim(d.substr(colon + 1));
        const std::size_t bang = v.rfind('!');
        if (bang != std::string::npos && LowerAscii(Trim(v.substr(bang + 1))) == "important") {
            decl.important = true;
            v = Trim(v.substr(0, bang));
        }
        if (v.empty()) continue;
        decl.value = v;
        out.push_back(std::move(decl));
    }
    return out;
}

StyleSheet ParseStyleSheet(const std::string& cssIn, const CssLimits& limits) {
    StyleSheet sheet;
    const std::string css = StripComments(cssIn.size() > limits.maxBytes ? cssIn.substr(0, limits.maxBytes) : cssIn);
    const std::size_t n = css.size();
    std::size_t i = 0;
    while (i < n && sheet.rules.size() < limits.maxRules) {
        while (i < n && (IsWs(css[i]) || css[i] == ';' || css[i] == '}')) ++i;
        if (i >= n) break;
        if (css[i] == '@') {   // an at-rule: skipped up to its ';' or past its block
            std::size_t j = i;
            while (j < n && css[j] != ';' && css[j] != '{') ++j;
            i = (j < n && css[j] == '{') ? BlockEnd(css, j + 1) + 1 : j + 1;
            continue;
        }
        // the prelude (selectors) up to '{'
        std::size_t open = i;
        while (open < n && css[open] != '{') {
            if (css[open] == '"' || css[open] == '\'') { const char q = css[open++]; while (open < n && css[open] != q) ++open; }
            if (open < n) ++open;
        }
        if (open >= n) break;
        const std::size_t close = BlockEnd(css, open + 1);
        const std::string prelude = css.substr(i, open - i);
        const std::string body = css.substr(open + 1, (close < n ? close : n) - open - 1);
        i = close + 1;
        Rule rule;
        bool valid = true;
        std::size_t s = 0;
        while (s <= prelude.size()) {
            std::size_t comma = prelude.find(',', s);
            if (comma == std::string::npos) comma = prelude.size();
            Selector sel;
            if (!ParseSelector(prelude.substr(s, comma - s), sel) || sel.parts.size() > limits.maxSelectorParts) { valid = false; break; }
            rule.selectors.push_back(std::move(sel));
            s = comma + 1;
        }
        if (!valid || rule.selectors.empty()) continue;
        rule.declarations = ParseDeclarations(body, limits);
        if (rule.declarations.empty()) continue;
        sheet.rules.push_back(std::move(rule));
    }
    return sheet;
}

// ---- the default sheet -------------------------------------------------------------------------------------------------

const StyleSheet& DefaultStyleSheet() {
    static const StyleSheet sheet = ParseStyleSheet(
        "html, address, blockquote, body, dd, div, dl, dt, fieldset, form, frameset, h1, h2, h3, h4, h5, h6, ol, p, ul, center,"
        " dir, hr, menu, pre, listing, xmp, plaintext, article, aside, footer, header, hgroup, main, nav, section, figure,"
        " figcaption, details, summary, search, legend, dialog, optgroup { display: block }\n"
        "head, script, style, title, meta, link, base, template, datalist, param, noembed, noframes, area, map, rp, col,"
        " colgroup, source, track { display: none }\n"
        "li { display: list-item }\n"
        "table { display: table }\n"
        "tr { display: table-row }\n"
        "thead, tbody, tfoot { display: table-row-group }\n"
        "td, th { display: table-cell; padding: 1px }\n"
        "caption { display: block; text-align: center }\n"
        "th { font-weight: bold; text-align: center }\n"
        "body { margin: 8px }\n"
        "p, dl { margin: 1em 0 }\n"
        "blockquote, figure { margin: 1em 40px }\n"
        "dd { margin-left: 40px }\n"
        "h1 { font-size: 2em; margin: 0.67em 0; font-weight: bold }\n"
        "h2 { font-size: 1.5em; margin: 0.83em 0; font-weight: bold }\n"
        "h3 { font-size: 1.17em; margin: 1em 0; font-weight: bold }\n"
        "h4 { margin: 1.33em 0; font-weight: bold }\n"
        "h5 { font-size: 0.83em; margin: 1.67em 0; font-weight: bold }\n"
        "h6 { font-size: 0.67em; margin: 2.33em 0; font-weight: bold }\n"
        "ul, ol, menu, dir { margin: 1em 0; padding-left: 40px }\n"
        "ol { list-style-type: decimal }\n"
        "ul, menu, dir { list-style-type: disc }\n"
        "ul ul, ol ul, menu ul, ul menu { list-style-type: circle }\n"
        "ul ul ul, ul ol ul, ol ul ul, ol ol ul { list-style-type: square }\n"
        "ul ul, ul ol, ol ul, ol ol { margin-top: 0; margin-bottom: 0 }\n"
        "b, strong { font-weight: bold }\n"
        "i, cite, em, var, dfn, address { font-style: italic }\n"
        "pre, xmp, listing, plaintext { font-family: monospace; white-space: pre; margin: 1em 0 }\n"
        "code, kbd, samp, tt { font-family: monospace }\n"
        "u, ins { text-decoration: underline }\n"
        "s, strike, del { text-decoration: line-through }\n"
        "small, sub, sup { font-size: smaller }\n"
        "big { font-size: larger }\n"
        "center { text-align: center }\n"
        "hr { margin: 0.5em auto; border-width: 1px; border-style: inset; border-color: gray }\n"
        "img { max-width: 100% }\n"
        "mark { background-color: yellow; color: black }\n"
        "nobr { white-space: nowrap }\n");
    return sheet;
}

// ---- the cascade -------------------------------------------------------------------------------------------------------

namespace {

struct Cascaded { int bucket; int specificity; std::size_t order; const Declaration* decl; };

// What the cascade knows while it applies declarations, beyond ComputedStyle itself.
struct Work {
    float borderWidth[4] = {3, 3, 3, 3};   // "medium"
    bool borderStyle[4] = {false, false, false, false};   // a style other than none / hidden
    bool borderColorSet[4] = {false, false, false, false};
};

bool BorderWidthWord(const std::string& w, float em, float& out) {
    if (w == "thin") { out = 1; return true; }
    if (w == "medium") { out = 3; return true; }
    if (w == "thick") { out = 5; return true; }
    Length l;
    if (ParseLength(w, em, false, l) && l.unit == Length::Px && l.value >= 0) { out = l.value; return true; }
    return false;
}
bool BorderStyleWord(const std::string& w, bool& visible) {
    static const char* const styles[] = {"solid", "dashed", "dotted", "double", "groove", "ridge", "inset", "outset", nullptr};
    if (w == "none" || w == "hidden") { visible = false; return true; }
    for (const char* const* s = styles; *s; ++s) if (w == *s) { visible = true; return true; }
    return false;
}

// Apply 1-4 values to the four sides (CSS 2.1 8.3 / 8.4 shorthand order).
template <class T, class F> bool FourSides(const std::vector<std::string>& words, T* out, F parse) {
    if (words.empty() || words.size() > 4) return false;
    T v[4];
    for (std::size_t k = 0; k < words.size(); ++k) if (!parse(words[k], v[k])) return false;
    switch (words.size()) {
    case 1: out[0] = out[1] = out[2] = out[3] = v[0]; break;
    case 2: out[0] = out[2] = v[0]; out[1] = out[3] = v[1]; break;
    case 3: out[0] = v[0]; out[1] = out[3] = v[1]; out[2] = v[2]; break;
    default: for (int k = 0; k < 4; ++k) out[k] = v[k]; break;
    }
    return true;
}

int SideOf(const std::string& prop, const std::string& prefix) {   // "margin-top" -> 0 ... ; -1 when not a side
    if (prop.compare(0, prefix.size(), prefix) != 0) return -1;
    const std::string rest = prop.substr(prefix.size());
    if (rest == "-top") return 0;
    if (rest == "-right") return 1;
    if (rest == "-bottom") return 2;
    if (rest == "-left") return 3;
    return -1;
}

bool FontSizeValue(const std::string& vIn, float parentSize, float& out) {
    const std::string v = LowerAscii(vIn);
    struct K { const char* k; float px; };
    static const K keys[] = {{"xx-small", 9}, {"x-small", 10}, {"small", 13}, {"medium", 16}, {"large", 18}, {"x-large", 24},
                             {"xx-large", 32}, {"xxx-large", 48}};
    for (const K& k : keys) if (v == k.k) { out = k.px; return true; }
    if (v == "smaller") { out = parentSize / 1.2f; return true; }
    if (v == "larger") { out = parentSize * 1.2f; return true; }
    Length l;
    if (!ParseLength(v, parentSize, true, l)) return false;
    const float px = l.unit == Length::Percent ? parentSize * l.value / 100.0f : l.value;
    if (px < 0) return false;
    out = std::min(px, 2000.0f);
    return true;
}

bool IsMonoFamily(const std::string& family) {
    const std::string f = LowerAscii(family);
    return f.find("monospace") != std::string::npos || f.find("mono") != std::string::npos || f.find("courier") != std::string::npos ||
           f.find("consolas") != std::string::npos;
}

bool ListStyleWord(const std::string& w, ListStyle& out) {
    struct K { const char* k; ListStyle s; };
    static const K keys[] = {{"disc", ListStyle::Disc}, {"circle", ListStyle::Circle}, {"square", ListStyle::Square},
                             {"decimal", ListStyle::Decimal}, {"decimal-leading-zero", ListStyle::Decimal},
                             {"lower-alpha", ListStyle::LowerAlpha}, {"lower-latin", ListStyle::LowerAlpha},
                             {"upper-alpha", ListStyle::UpperAlpha}, {"upper-latin", ListStyle::UpperAlpha},
                             {"lower-roman", ListStyle::LowerRoman}, {"upper-roman", ListStyle::UpperRoman}, {"none", ListStyle::None}};
    for (const K& k : keys) if (w == k.k) { out = k.s; return true; }
    return false;
}

void ApplyFontSize(ComputedStyle& s, const Declaration& d, const ComputedStyle& parent) {
    if (d.property == "font-size") {
        float px;
        if (LowerAscii(d.value) == "inherit") s.fontSize = parent.fontSize;
        else if (FontSizeValue(d.value, parent.fontSize, px)) s.fontSize = px;
    } else if (d.property == "font") {
        for (const std::string& wIn : Words(d.value)) {
            const std::string w = LowerAscii(wIn);
            if (w == "/") break;
            if (w == "normal" || w == "italic" || w == "oblique" || w == "bold" || w == "bolder" || w == "lighter" || w == "small-caps") continue;
            float px;
            if (FontSizeValue(w, parent.fontSize, px)) { s.fontSize = px; break; }
        }
    }
}

void Apply(ComputedStyle& s, Work& wk, const Declaration& d, const ComputedStyle& parent) {
    const std::string& p = d.property;
    const std::string v = LowerAscii(d.value);
    const float em = s.fontSize;
    const std::vector<std::string> words = Words(d.value);
    auto lengthOrAuto = [&](const std::string& w, Length& out) {
        if (LowerAscii(w) == "auto") { out = Length(); return true; }
        return ParseLength(w, em, true, out);
    };
    auto nonNegative = [&](const std::string& w, Length& out) { return ParseLength(w, em, true, out) && out.value >= 0; };

    if (p == "display") {
        if (v == "block" || v == "flex" || v == "grid" || v == "flow-root") s.display = Display::Block;
        else if (v == "inline" || v == "inline-block" || v == "inline-flex" || v == "inline-grid" || v == "contents") s.display = Display::Inline;
        else if (v == "list-item") s.display = Display::ListItem;
        else if (v == "none") s.display = Display::None;
        else if (v == "table" || v == "inline-table") s.display = Display::Table;
        else if (v == "table-row") s.display = Display::TableRow;
        else if (v == "table-cell") s.display = Display::TableCell;
        else if (v == "table-row-group" || v == "table-header-group" || v == "table-footer-group") s.display = Display::TableSection;
    } else if (p == "margin") {
        Length m[4];
        if (FourSides(words, m, lengthOrAuto)) for (int k = 0; k < 4; ++k) s.margin[k] = m[k];
    } else if (SideOf(p, "margin") >= 0) {
        Length l;
        if (words.size() == 1 && lengthOrAuto(words[0], l)) s.margin[SideOf(p, "margin")] = l;
    } else if (p == "padding") {
        Length m[4];
        if (FourSides(words, m, nonNegative)) for (int k = 0; k < 4; ++k) s.padding[k] = m[k];
    } else if (SideOf(p, "padding") >= 0) {
        Length l;
        if (words.size() == 1 && nonNegative(words[0], l)) s.padding[SideOf(p, "padding")] = l;
    } else if (p == "border" || SideOf(p, "border") >= 0) {
        // shorthand: width, style and colour in any order
        float width = 3; bool visible = false; Color col; bool hasW = false, hasS = false, hasC = false, current = false, ok = !words.empty();
        for (const std::string& w : words) {
            const std::string lw = LowerAscii(w);
            float bw; bool vis; Color c;
            if (!hasW && BorderWidthWord(lw, em, bw)) { width = bw; hasW = true; }
            else if (!hasS && BorderStyleWord(lw, vis)) { visible = vis; hasS = true; }
            else if (!hasC && ParseColor(w, c)) { col = c; hasC = true; }
            else if (!hasC && lw == "currentcolor") { hasC = true; current = true; }
            else ok = false;
        }
        if (ok) {
            const int side = SideOf(p, "border");
            for (int k = 0; k < 4; ++k) {
                if (side >= 0 && k != side) continue;
                wk.borderWidth[k] = width;
                wk.borderStyle[k] = visible;
                if (hasC && !current) { s.borderColor[k] = col; wk.borderColorSet[k] = true; }
                else wk.borderColorSet[k] = false;
            }
        }
    } else if (p == "border-width") {
        float w[4];
        if (FourSides(words, w, [&](const std::string& x, float& o) { return BorderWidthWord(LowerAscii(x), em, o); }))
            for (int k = 0; k < 4; ++k) wk.borderWidth[k] = w[k];
    } else if (p == "border-style") {
        bool st[4];
        if (FourSides(words, st, [&](const std::string& x, bool& o) { return BorderStyleWord(LowerAscii(x), o); }))
            for (int k = 0; k < 4; ++k) wk.borderStyle[k] = st[k];
    } else if (p == "border-color") {
        Color c[4];
        if (FourSides(words, c, [&](const std::string& x, Color& o) { return ParseColor(x, o); }))
            for (int k = 0; k < 4; ++k) { s.borderColor[k] = c[k]; wk.borderColorSet[k] = true; }
    } else if (p.compare(0, 7, "border-") == 0 && (p.size() > 6)) {
        // border-top-width / -style / -color
        static const char* const sides[] = {"top", "right", "bottom", "left"};
        for (int k = 0; k < 4; ++k) {
            const std::string base = std::string("border-") + sides[k];
            if (p == base + "-width") { float w; if (BorderWidthWord(v, em, w)) wk.borderWidth[k] = w; }
            else if (p == base + "-style") { bool vis; if (BorderStyleWord(v, vis)) wk.borderStyle[k] = vis; }
            else if (p == base + "-color") { Color c; if (ParseColor(d.value, c)) { s.borderColor[k] = c; wk.borderColorSet[k] = true; } }
        }
    } else if (p == "color") {
        Color c;
        if (v == "inherit" || v == "currentcolor") s.color = parent.color;
        else if (v == "initial") s.color = Color{0, 0, 0, 255};
        else if (ParseColor(d.value, c)) s.color = c;
    } else if (p == "background-color") {
        Color c;
        if (ParseColor(d.value, c)) s.background = c;
        else if (v == "currentcolor") s.background = s.color;
    } else if (p == "background") {
        if (v == "none") s.background = Color{0, 0, 0, 0};
        for (const std::string& w : words) { Color c; if (ParseColor(w, c)) { s.background = c; break; } }
    } else if (p == "font-weight") {
        if (v == "bold" || v == "bolder") s.bold = true;
        else if (v == "normal" || v == "lighter") s.bold = false;
        else if (v == "inherit") s.bold = parent.bold;
        else { float n; std::string u; if (ParseNumber(v, n, u) && u.empty()) s.bold = n >= 600; }
    } else if (p == "font-style") {
        if (v == "italic" || v.compare(0, 7, "oblique") == 0) s.italic = true;
        else if (v == "normal") s.italic = false;
    } else if (p == "font-family") {
        s.mono = IsMonoFamily(v);
    } else if (p == "font") {
        bool seenSize = false, lhNext = false;
        std::string family;
        for (const std::string& w : words) {
            const std::string lw = LowerAscii(w);
            if (lhNext) {
                lhNext = false;
                float n; std::string u;
                if (lw == "normal") { s.lineHeight = -1; s.lineHeightFactor = -1; }
                else if (ParseNumber(lw, n, u) && u.empty() && n >= 0) { s.lineHeightFactor = n; s.lineHeight = -1; }
                else { Length l; if (ParseLength(lw, s.fontSize, true, l)) { s.lineHeight = l.unit == Length::Percent ? s.fontSize * l.value / 100 : l.value; s.lineHeightFactor = -1; } }
                continue;
            }
            if (w == "/") { lhNext = true; continue; }
            if (!seenSize) {
                if (lw == "bold" || lw == "bolder") s.bold = true;
                else if (lw == "italic" || lw == "oblique") s.italic = true;
                else { float n; std::string u; if (ParseNumber(lw, n, u) && u.empty()) s.bold = n >= 600; else { float px; if (FontSizeValue(lw, parent.fontSize, px)) seenSize = true; } }
            } else if (w != ",") family += lw + " ";
        }
        if (seenSize) { s.mono = IsMonoFamily(family); if (!lhNext) {} }
    } else if (p == "text-align") {
        if (v == "left" || v == "start" || v == "justify") s.textAlign = TextAlign::Left;
        else if (v == "right" || v == "end") s.textAlign = TextAlign::Right;
        else if (v == "center" || v == "-webkit-center") s.textAlign = TextAlign::Center;
    } else if (p == "line-height") {
        float n; std::string u;
        if (v == "normal") { s.lineHeight = -1; s.lineHeightFactor = -1; }
        else if (ParseNumber(v, n, u) && u.empty() && n >= 0) { s.lineHeightFactor = std::min(n, 100.0f); s.lineHeight = -1; }
        else {
            Length l;
            if (ParseLength(v, em, true, l) && l.value >= 0) {
                s.lineHeight = std::min(l.unit == Length::Percent ? em * l.value / 100.0f : l.value, 20000.0f);
                s.lineHeightFactor = -1;
            }
        }
    } else if (p == "width" || p == "height") {
        Length l;
        if (lengthOrAuto(d.value, l) && (l.unit == Length::Auto || l.value >= 0)) (p == "width" ? s.width : s.height) = l;
    } else if (p == "max-width") {
        Length l;
        if (v == "none") { s.maxWidth = Length(); s.maxWidth.unit = Length::None; }
        else if (ParseLength(d.value, em, true, l) && l.value >= 0) s.maxWidth = l;
    } else if (p == "text-decoration" || p == "text-decoration-line") {
        if (v.find("none") != std::string::npos) { s.underline = false; s.strike = false; }
        if (v.find("underline") != std::string::npos) s.underline = true;
        if (v.find("line-through") != std::string::npos) s.strike = true;
    } else if (p == "white-space") {
        if (v == "normal") s.whiteSpace = WhiteSpace::Normal;
        else if (v == "pre") s.whiteSpace = WhiteSpace::Pre;
        else if (v == "nowrap") s.whiteSpace = WhiteSpace::NoWrap;
        else if (v == "pre-wrap" || v == "break-spaces") s.whiteSpace = WhiteSpace::PreWrap;
        else if (v == "pre-line") s.whiteSpace = WhiteSpace::PreLine;
    } else if (p == "list-style-type") {
        ListStyle ls;
        if (ListStyleWord(v, ls)) s.listStyle = ls;
    } else if (p == "list-style") {
        for (const std::string& w : words) { ListStyle ls; if (ListStyleWord(LowerAscii(w), ls)) { s.listStyle = ls; break; } }
    }
}

} // namespace

ComputedStyle ComputeStyle(const Node& el, const ComputedStyle& parent, const std::vector<const StyleSheet*>& author) {
    ComputedStyle s;
    // inherited properties
    s.color = parent.color;
    s.fontSize = parent.fontSize;
    s.bold = parent.bold; s.italic = parent.italic; s.mono = parent.mono;
    s.textAlign = parent.textAlign;
    s.lineHeight = parent.lineHeight; s.lineHeightFactor = parent.lineHeightFactor;
    s.whiteSpace = parent.whiteSpace;
    s.listStyle = parent.listStyle;
    s.underline = parent.underline; s.strike = parent.strike;   // decorations reach the text inside (simplified)
    // the rest start at their initial values
    for (int k = 0; k < 4; ++k) { s.margin[k] = Length::Px_(0); s.padding[k] = Length::Px_(0); }
    s.maxWidth.unit = Length::None;
    if (el.kind != NodeKind::Element) return s;

    // Gather the declarations that apply: (bucket, specificity, order).
    std::vector<Cascaded> list;
    std::size_t order = 0;
    auto collect = [&](const StyleSheet& sheet, int normalBucket, int importantBucket) {
        for (const Rule& r : sheet.rules) {
            int best = -1;
            for (const Selector& sel : r.selectors) if (sel.specificity > best && Matches(sel, el)) best = sel.specificity;
            if (best < 0) { order += r.declarations.size(); continue; }
            for (const Declaration& d : r.declarations) list.push_back({d.important ? importantBucket : normalBucket, best, order++, &d});
        }
    };
    collect(DefaultStyleSheet(), 0, 0);
    // Rules the default sheet cannot say with the selectors we read: a[href] (the link look) and [hidden].
    static const std::vector<Declaration> linkDecls = ParseDeclarations("color: #0000ee; text-decoration: underline");
    static const std::vector<Declaration> hiddenDecls = ParseDeclarations("display: none");
    if (el.name == "a" && el.Attr("href")) for (const Declaration& d : linkDecls) list.push_back({0, 11, order++, &d});
    if (el.Attr("hidden")) for (const Declaration& d : hiddenDecls) list.push_back({0, 10, order++, &d});
    for (const StyleSheet* sh : author) if (sh) collect(*sh, 1, 3);
    std::vector<Declaration> inlineDecls;
    if (const std::string* st = el.Attr("style")) {
        inlineDecls = ParseDeclarations(*st);
        for (const Declaration& d : inlineDecls) list.push_back({d.important ? 4 : 2, 0, order++, &d});
    }
    std::stable_sort(list.begin(), list.end(), [](const Cascaded& a, const Cascaded& b) {
        if (a.bucket != b.bucket) return a.bucket < b.bucket;
        if (a.specificity != b.specificity) return a.specificity < b.specificity;
        return a.order < b.order;
    });

    // Pass 1: the font size (em values of everything else depend on it). Pass 2: the rest.
    for (const Cascaded& c : list) ApplyFontSize(s, *c.decl, parent);
    Work wk;
    for (const Cascaded& c : list) if (c.decl->property != "font-size") Apply(s, wk, *c.decl, parent);
    for (int k = 0; k < 4; ++k) {
        s.border[k] = wk.borderStyle[k] ? std::min(wk.borderWidth[k], 1000.0f) : 0.0f;
        if (!wk.borderColorSet[k]) s.borderColor[k] = s.color;   // currentColor
    }
    return s;
}

} // namespace okay::browser
