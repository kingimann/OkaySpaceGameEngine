// Browser (page engine): style sheets. Written from CSS Syntax Level 3, Selectors Level 4, CSS 2.1, CSS Values 4, CSS Custom
// Properties 1, Media Queries 4, CSS Color 4, CSS Fonts 4, CSS Flexible Box Layout 1, CSS Grid Layout 1, CSS Box Alignment 3
// and CSS Backgrounds 3 (see Css.hpp for what is read).
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
int Hex(char c) { return okay::str::HexDigitValue(c); }

// The units em, rem, vw and vh need: set by ComputeStyle while it works (the page engine runs on one thread).
struct Units { float rem = 16.0f, vw = 10.24f, vh = 7.68f; };
Units g_units;

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

// The index of the ')' that closes the '(' at `open`, or npos.
std::size_t CloseParen(const std::string& s, std::size_t open) {
    int depth = 0;
    for (std::size_t i = open; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '"' || c == '\'') { const char q = c; ++i; while (i < s.size() && s[i] != q) { if (s[i] == '\\') ++i; ++i; } continue; }
        if (c == '(') ++depth;
        else if (c == ')' && --depth == 0) return i;
    }
    return std::string::npos;
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
// The words split at top-level commas.
std::vector<std::vector<std::string>> CommaGroups(const std::vector<std::string>& words) {
    std::vector<std::vector<std::string>> out(1);
    for (const std::string& w : words) { if (w == ",") out.emplace_back(); else out.back().push_back(w); }
    return out;
}

std::string Unquote(std::string s) {
    s = Trim(s);
    if (s.size() >= 2 && (s[0] == '"' || s[0] == '\'') && s.back() == s[0]) {
        std::string out;
        for (std::size_t i = 1; i + 1 < s.size(); ++i) {
            if (s[i] == '\\' && i + 2 < s.size()) {
                // a hex escape (\26 or \2026) or the next character as it is
                std::size_t j = i + 1;
                unsigned cp = 0;
                int nd = 0;
                while (j + 1 < s.size() && nd < 6 && Hex(s[j]) >= 0) { cp = cp * 16 + (unsigned)Hex(s[j]); ++j; ++nd; }
                if (nd > 0) {
                    if (j + 1 < s.size() && IsWs(s[j])) ++j;
                    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
                    if (cp < 0x80) out += (char)cp;
                    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 63)); }
                    else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 63)); out += (char)(0x80 | (cp & 63)); }
                    else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 63)); out += (char)(0x80 | ((cp >> 6) & 63)); out += (char)(0x80 | (cp & 63)); }
                    i = j - 1;
                    continue;
                }
                out += s[++i];
                continue;
            }
            out += s[i];
        }
        return out;
    }
    return s;
}

// url(...) or url("...") -> the address; "" when `w` is not a url().
std::string UrlOf(const std::string& w) {
    const std::string lw = LowerAscii(w);
    if (lw.compare(0, 4, "url(") != 0 || w.back() != ')') return std::string();
    return Unquote(w.substr(4, w.size() - 5));
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

bool UnitToPx(float v, const std::string& unit, float em, float& px) {
    if (unit == "px") px = v;
    else if (unit == "em") px = v * em;
    else if (unit == "rem") px = v * g_units.rem;
    else if (unit == "ex" || unit == "ch") px = v * em * 0.5f;
    else if (unit == "vw") px = v * g_units.vw;
    else if (unit == "vh" || unit == "dvh" || unit == "svh" || unit == "lvh") px = v * g_units.vh;
    else if (unit == "vmin") px = v * std::min(g_units.vw, g_units.vh);
    else if (unit == "vmax") px = v * std::max(g_units.vw, g_units.vh);
    else if (unit == "pt") px = v * 96.0f / 72.0f;
    else if (unit == "pc") px = v * 16.0f;
    else if (unit == "in") px = v * 96.0f;
    else if (unit == "cm") px = v * 96.0f / 2.54f;
    else if (unit == "mm") px = v * 96.0f / 25.4f;
    else if (unit == "q") px = v * 96.0f / 101.6f;
    else return false;
    return true;
}

// calc(), min(), max(), clamp(): a value that is px + pct % (+ a plain number), worked out as far as it can be here.
struct Lin { float px = 0, pct = 0, num = 0; bool isNum = false; };

struct CalcReader {
    const std::string& s;
    std::size_t i = 0;
    float em;
    int depth = 0;
    bool ok = true;
    CalcReader(const std::string& text, float e) : s(text), em(e) {}
    void Ws() { while (i < s.size() && IsWs(s[i])) ++i; }
    Lin Expr() {
        Lin a = Term();
        while (ok) {
            Ws();
            if (i >= s.size() || (s[i] != '+' && s[i] != '-')) break;
            const char op = s[i++];
            Lin b = Term();
            const float k = op == '+' ? 1.0f : -1.0f;
            a.px += k * b.px; a.pct += k * b.pct; a.num += k * b.num;
            a.isNum = a.isNum && b.isNum;
        }
        return a;
    }
    Lin Term() {
        Lin a = Factor();
        while (ok) {
            Ws();
            if (i >= s.size() || (s[i] != '*' && s[i] != '/')) break;
            const char op = s[i++];
            Lin b = Factor();
            if (op == '*') {
                if (b.isNum) { a.px *= b.num; a.pct *= b.num; a.num *= b.num; }
                else if (a.isNum) { const float k = a.num; a = b; a.px *= k; a.pct *= k; a.num *= k; }
                else ok = false;
            } else {
                if (!b.isNum || b.num == 0.0f) { ok = false; break; }
                a.px /= b.num; a.pct /= b.num; a.num /= b.num;
            }
        }
        return a;
    }
    Lin Factor() {
        Ws();
        Lin r;
        if (i >= s.size() || ++depth > 32) { ok = false; return r; }
        if (s[i] == '(') {
            ++i;
            r = Expr();
            Ws();
            if (i < s.size() && s[i] == ')') ++i; else ok = false;
            --depth;
            return r;
        }
        // a function: calc( min( max( clamp(
        std::size_t j = i;
        while (j < s.size() && (std::isalpha((unsigned char)s[j]) || s[j] == '-')) ++j;
        if (j < s.size() && s[j] == '(' && j > i) {
            const std::string fn = LowerAscii(s.substr(i, j - i));
            i = j + 1;
            std::vector<Lin> args;
            while (ok) {
                args.push_back(Expr());
                Ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ')') { ++i; break; }
                ok = false;
            }
            --depth;
            if (!ok || args.empty()) { ok = false; return r; }
            if (fn == "calc") return args[0];
            auto pick = [&](bool wantMin) {
                // only comparable when no argument mixes in %; else the first argument stands
                for (const Lin& a : args) if (a.pct != 0.0f) return args[0];
                Lin best = args[0];
                for (const Lin& a : args) if (wantMin ? a.px + a.num < best.px + best.num : a.px + a.num > best.px + best.num) best = a;
                return best;
            };
            if (fn == "min") return pick(true);
            if (fn == "max") return pick(false);
            if (fn == "clamp" && args.size() == 3) {
                for (const Lin& a : args) if (a.pct != 0.0f) return args[1];
                Lin v = args[1];
                if (v.px + v.num < args[0].px + args[0].num) v = args[0];
                if (v.px + v.num > args[2].px + args[2].num) v = args[2];
                return v;
            }
            ok = false;
            return r;
        }
        // a number with its unit
        std::size_t e = i;
        if (e < s.size() && (s[e] == '+' || s[e] == '-')) ++e;
        while (e < s.size() && (std::isdigit((unsigned char)s[e]) || s[e] == '.')) ++e;
        if (e < s.size() && (s[e] == 'e' || s[e] == 'E') && e + 1 < s.size() && (std::isdigit((unsigned char)s[e + 1]) || s[e + 1] == '-' || s[e + 1] == '+')) {
            e += 2;
            while (e < s.size() && std::isdigit((unsigned char)s[e])) ++e;
        }
        while (e < s.size() && (std::isalpha((unsigned char)s[e]) || s[e] == '%')) ++e;
        const std::string tok = s.substr(i, e - i);
        i = e;
        --depth;
        float v = 0;
        std::string unit;
        if (!ParseNumber(tok, v, unit)) { ok = false; return r; }
        if (unit.empty()) { r.isNum = true; r.num = v; return r; }
        if (unit == "%") { r.pct = v; return r; }
        float px;
        if (!UnitToPx(v, unit, em, px)) { ok = false; return r; }
        r.px = px;
        return r;
    }
};

// A length to px: em relative to `em`; % kept (returned as Percent) when allowed; calc() & co. as Calc.
bool ParseLength(const std::string& s, float em, bool allowPercent, Length& out) {
    const std::string ls = LowerAscii(s);
    if (ls.compare(0, 5, "calc(") == 0 || ls.compare(0, 4, "min(") == 0 || ls.compare(0, 4, "max(") == 0 || ls.compare(0, 6, "clamp(") == 0) {
        CalcReader cr(s, em);
        const Lin l = cr.Factor();
        cr.Ws();
        if (!cr.ok || cr.i != s.size()) return false;
        if (l.isNum) { if (l.num != 0.0f) return false; out = Length::Px_(0); return true; }
        if (l.pct != 0.0f && !allowPercent) return false;
        if (!std::isfinite(l.px) || !std::isfinite(l.pct)) return false;
        if (l.pct == 0.0f) out = Length::Px_(std::max(-1e7f, std::min(1e7f, l.px)));
        else if (l.px == 0.0f) { out.unit = Length::Percent; out.value = l.pct; out.pct = 0; }
        else { out.unit = Length::Calc; out.value = l.px; out.pct = l.pct; }
        return true;
    }
    float v = 0;
    std::string unit;
    if (!ParseNumber(s, v, unit)) return false;
    if (unit.empty()) { if (v != 0.0f) return false; out = Length::Px_(0); return true; }
    if (unit == "%") { if (!allowPercent) return false; out.unit = Length::Percent; out.value = v; out.pct = 0; return true; }
    float px;
    if (!UnitToPx(v, unit, em, px)) return false;
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
    {"darkcyan", 0x008B8B}, {"darkmagenta", 0x8B008B}, {"darkviolet", 0x9400D3}, {"darkkhaki", 0xBDB76B}, {"lightcoral", 0xF08080},
    {"lightpink", 0xFFB6C1}, {"lightsalmon", 0xFFA07A}, {"lightseagreen", 0x20B2AA}, {"lightskyblue", 0x87CEFA},
    {"lightsteelblue", 0xB0C4DE}, {"mediumblue", 0x0000CD}, {"mediumseagreen", 0x3CB371}, {"mediumpurple", 0x9370DB},
    {"palegreen", 0x98FB98}, {"paleturquoise", 0xAFEEEE}, {"papayawhip", 0xFFEFD5}, {"peachpuff", 0xFFDAB9},
    {"powderblue", 0xB0E0E6}, {"seashell", 0xFFF5EE}, {"springgreen", 0x00FF7F}, {"thistle", 0xD8BFD8}, {"oldlace", 0xFDF5E6},
    {"cornsilk", 0xFFF8DC}, {"lemonchiffon", 0xFFFACD}, {"mistyrose", 0xFFE4E1}, {"blanchedalmond", 0xFFEBCD},
    {"bisque", 0xFFE4C4}, {"floralwhite", 0xFFFAF0}, {"antiquewhite", 0xFAEBD7}, {"darkslateblue", 0x483D8B},
    {"slateblue", 0x6A5ACD}, {"mediumslateblue", 0x7B68EE}, {"cadetblue", 0x5F9EA0}, {"darkturquoise", 0x00CED1},
    {"deepskyblue", 0x00BFFF}, {"mediumturquoise", 0x48D1CC}, {"aquamarine", 0x7FFFD4}, {"mediumaquamarine", 0x66CDAA},
    {"darkseagreen", 0x8FBC8F}, {"greenyellow", 0xADFF2F}, {"lawngreen", 0x7CFC00}, {"yellowgreen", 0x9ACD32},
    {"darkolivegreen", 0x556B2F}, {"saddlebrown", 0x8B4513}, {"sandybrown", 0xF4A460}, {"rosybrown", 0xBC8F8F},
    {"indianred", 0xCD5C5C}, {"orangered", 0xFF4500}, {"palevioletred", 0xDB7093}, {"mediumvioletred", 0xC71585},
    {"blueviolet", 0x8A2BE2}, {"darkorchid", 0x9932CC}, {"mediumorchid", 0xBA55D3}, {"lightcyan", 0xE0FFFF},
    {"lightgoldenrodyellow", 0xFAFAD2}, {"palegoldenrod", 0xEEE8AA}, {"darkgoldenrod", 0xB8860B}, {"burlywood", 0xDEB887},
    {"lavenderblush", 0xFFF0F5}, {"mediumspringgreen", 0x00FA9A}, {"lightslategrey", 0x778899},
};

std::uint8_t Clamp255(float v) { return (std::uint8_t)std::lround(std::max(0.0f, std::min(255.0f, v))); }

float HueToRgb(float p, float q, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0f / 6) return p + (q - p) * 6 * t;
    if (t < 0.5f) return q;
    if (t < 2.0f / 3) return p + (q - p) * (2.0f / 3 - t) * 6;
    return p;
}

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
    const bool rgb = text.compare(0, 4, "rgb(") == 0 || text.compare(0, 5, "rgba(") == 0;
    const bool hsl = text.compare(0, 4, "hsl(") == 0 || text.compare(0, 5, "hsla(") == 0;
    if (rgb || hsl) {
        const std::size_t open = text.find('('), close = text.rfind(')');
        if (close == std::string::npos || close < open) return false;
        std::vector<std::string> v;
        for (const std::string& w : Words(text.substr(open + 1, close - open - 1))) if (w != "," && w != "/") v.push_back(w);
        if (v.size() != 3 && v.size() != 4) return false;
        float ch[4] = {0, 0, 0, 255};
        for (std::size_t k = 0; k < v.size(); ++k) {
            float num = 0;
            std::string unit;
            if (!ParseNumber(v[k], num, unit)) return false;
            if (k == 3) { ch[3] = unit == "%" ? num * 255.0f / 100.0f : unit.empty() ? num * 255.0f : -1; if (ch[3] < -0.5f) return false; continue; }
            if (rgb) {
                if (unit == "%") ch[k] = num * 255.0f / 100.0f;
                else if (unit.empty()) ch[k] = num;
                else return false;
            } else {
                if (k == 0) { if (!(unit.empty() || unit == "deg")) return false; ch[0] = num; }
                else { if (!(unit == "%" || unit.empty())) return false; ch[k] = num / 100.0f; }
            }
        }
        if (hsl) {
            const float hh = std::fmod(std::fmod(ch[0], 360.0f) + 360.0f, 360.0f) / 360.0f;
            const float s = std::max(0.0f, std::min(1.0f, ch[1])), l = std::max(0.0f, std::min(1.0f, ch[2]));
            const float q = l < 0.5f ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
            ch[0] = HueToRgb(p, q, hh + 1.0f / 3) * 255.0f;
            ch[1] = HueToRgb(p, q, hh) * 255.0f;
            ch[2] = HueToRgb(p, q, hh - 1.0f / 3) * 255.0f;
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

// ---- media queries ---------------------------------------------------------------------------------------------------

MediaQuery ParseMediaQuery(const std::string& textIn) {
    MediaQuery mq;
    const std::string text = LowerAscii(Trim(textIn));
    if (text.empty()) return mq;
    for (const std::vector<std::string>& group : CommaGroups(Words(text))) {
        MediaQuery::Query q;
        std::size_t k = 0;
        if (k < group.size() && group[k] == "not") { q.negate = true; ++k; }
        if (k < group.size() && group[k] == "only") ++k;
        if (k < group.size() && group[k][0] != '(') {
            const std::string& t = group[k++];
            q.typeOk = t == "all" || t == "screen";
            if (!(t == "all" || t == "screen" || t == "print" || t == "speech")) q.never = true;   // unknown type: never
        }
        for (; k < group.size(); ++k) {
            const std::string& w = group[k];
            if (w == "and") continue;
            if (w == "or" || w.empty() || w[0] != '(' || w.back() != ')') { q.never = true; continue; }
            std::string inner = Trim(w.substr(1, w.size() - 2));
            MediaQuery::Feature f;
            const std::size_t colon = inner.find(':');
            auto num = [&](const std::string& v, float& out) {
                Length l;
                if (ParseLength(Trim(v), 16.0f, false, l) && l.unit == Length::Px) { out = l.value; return true; }
                float n; std::string u;
                if (ParseNumber(Trim(v), n, u) && u.empty()) { out = n; return true; }
                return false;
            };
            if (colon != std::string::npos) {
                std::string name = Trim(inner.substr(0, colon));
                const std::string val = Trim(inner.substr(colon + 1));
                if (name.compare(0, 4, "min-") == 0) { f.op = 'g'; name = name.substr(4); }
                else if (name.compare(0, 4, "max-") == 0) { f.op = 'l'; name = name.substr(4); }
                f.name = name;
                if (!num(val, f.value)) { f.word = val; if (f.op != '=') q.never = true; }
            } else {
                // range syntax: (width >= 600px), (400px < width <= 800px), or a boolean feature
                const std::size_t opPos = inner.find_first_of("<>=");
                if (opPos == std::string::npos) { f.name = inner; f.op = '?'; }
                else {
                    std::vector<std::string> parts;
                    std::vector<std::string> ops;
                    std::size_t s = 0;
                    for (std::size_t i = 0; i < inner.size();) {
                        if (inner[i] == '<' || inner[i] == '>' || inner[i] == '=') {
                            parts.push_back(Trim(inner.substr(s, i - s)));
                            std::string op(1, inner[i]);
                            if (i + 1 < inner.size() && inner[i + 1] == '=') { op += '='; ++i; }
                            ops.push_back(op);
                            s = ++i;
                        } else ++i;
                    }
                    parts.push_back(Trim(inner.substr(s)));
                    auto toOp = [](const std::string& o, bool flip) -> char {
                        if (o == "=") return '=';
                        const bool less = (o[0] == '<') != flip;
                        const bool eq = o.size() > 1;
                        return less ? (eq ? 'l' : '<') : (eq ? 'g' : '>');
                    };
                    // name op value | value op name | value op name op value
                    for (std::size_t pi = 0; pi + 1 < parts.size(); ++pi) {
                        MediaQuery::Feature g;
                        float v;
                        if (num(parts[pi + 1], v)) { g.name = parts[pi]; g.value = v; g.op = toOp(ops[pi], false); }
                        else if (num(parts[pi], v)) { g.name = parts[pi + 1]; g.value = v; g.op = toOp(ops[pi], true); }
                        else { q.never = true; continue; }
                        q.features.push_back(g);
                    }
                    continue;
                }
            }
            q.features.push_back(f);
        }
        mq.queries.push_back(q);
    }
    return mq;
}

bool MediaMatches(const MediaQuery& mq, const MediaEnv& env) {
    if (mq.queries.empty()) return true;
    for (const MediaQuery::Query& q : mq.queries) {
        bool ok = q.typeOk && !q.never;
        for (const MediaQuery::Feature& f : q.features) {
            if (!ok) break;
            float have = 0;
            bool numeric = true;
            if (f.name == "width" || f.name == "device-width") have = env.width;
            else if (f.name == "height" || f.name == "device-height") have = env.height;
            else if (f.name == "aspect-ratio") have = env.height > 0 ? env.width / env.height : 1.0f;
            else if (f.name == "resolution" || f.name == "-webkit-device-pixel-ratio") have = 1.0f;
            else if (f.name == "color") have = 8.0f;
            else numeric = false;
            if (numeric) {
                if (f.op == '?') ok = have != 0;
                else if (!f.word.empty()) ok = false;
                else if (f.op == '=') ok = std::fabs(have - f.value) < 0.5f;
                else if (f.op == 'g') ok = have >= f.value;
                else if (f.op == 'l') ok = have <= f.value;
                else if (f.op == '>') ok = have > f.value;
                else if (f.op == '<') ok = have < f.value;
                continue;
            }
            if (f.name == "orientation") ok = f.word == (env.height >= env.width ? "portrait" : "landscape");
            else if (f.name == "prefers-color-scheme") ok = f.word == (env.dark ? "dark" : "light");
            else if (f.name == "prefers-reduced-motion") ok = f.word == "no-preference";
            else if (f.name == "hover") ok = f.op == '?' || f.word == "hover";
            else if (f.name == "pointer" || f.name == "any-pointer") ok = f.op == '?' || f.word == "fine";
            else if (f.name == "any-hover") ok = f.op == '?' || f.word == "hover";
            else if (f.name == "prefers-contrast") ok = f.word == "no-preference";
            else if (f.name == "scripting") ok = f.word == "none";
            else if (f.name == "display-mode") ok = f.word == "browser";
            else ok = false;   // a feature we do not know: false (Media Queries 4, 3.1)
        }
        if (q.negate) ok = !ok && !q.never;
        if (ok) return true;
    }
    return false;
}

// ---- selectors ---------------------------------------------------------------------------------------------------------

namespace {

// An identifier at s[i] with CSS escapes (\: or \31 ) decoded; false when there is none.
bool ReadIdent(const std::string& s, std::size_t& i, std::string& out) {
    out.clear();
    const std::size_t start = i;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            std::size_t j = i + 1;
            unsigned cp = 0;
            int nd = 0;
            while (j < s.size() && nd < 6 && Hex(s[j]) >= 0) { cp = cp * 16 + (unsigned)Hex(s[j]); ++j; ++nd; }
            if (nd > 0) {
                if (j < s.size() && IsWs(s[j])) ++j;
                if (cp < 0x80 && cp > 0) out += (char)cp;
                else if (cp >= 0x80 && cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 63)); }
                else if (cp >= 0x800 && cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 63)); out += (char)(0x80 | (cp & 63)); }
                else out += "\xEF\xBF\xBD";
                i = j;
            } else { out += s[i + 1]; i += 2; }
            continue;
        }
        if (!IsIdentChar(c)) break;
        out += c;
        ++i;
    }
    if (out.empty() || (start < s.size() && s[start] >= '0' && s[start] <= '9')) { i = start; return false; }
    return true;
}

// An+B (Selectors 4, 6.6.6 / CSS Syntax 3, 6): "odd", "even", "3", "2n+1", "-n+3", "n".
bool ParseAnB(std::string t, int& a, int& b) {
    t = LowerAscii(Trim(t));
    std::string c;
    for (char ch : t) if (!IsWs(ch)) c += ch;
    if (c == "odd") { a = 2; b = 1; return true; }
    if (c == "even") { a = 2; b = 0; return true; }
    const std::size_t n = c.find('n');
    auto toInt = [](const std::string& s, int& out) {
        if (s.empty()) return false;
        char* e = nullptr;
        const long v = std::strtol(s.c_str(), &e, 10);
        if (*e != 0) return false;
        out = (int)std::max(-100000L, std::min(100000L, v));
        return true;
    };
    if (n == std::string::npos) { a = 0; return toInt(c, b); }
    const std::string as = c.substr(0, n), bs = c.substr(n + 1);
    if (as.empty() || as == "+") a = 1;
    else if (as == "-") a = -1;
    else if (!toInt(as, a)) return false;
    if (bs.empty()) b = 0;
    else if (bs[0] != '+' && bs[0] != '-') return false;
    else if (!toInt(bs, b)) return false;
    return true;
}

} // namespace

// A selector list split at its top-level commas (not inside brackets or parentheses).
static std::vector<std::string> SplitSelectorList(const std::string& text) {   // dup-allow: commas inside () and [] do not split (a selector list, not a plain Split)
    std::vector<std::string> parts;
    std::size_t s = 0;
    int depth = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        const char c = i < text.size() ? text[i] : ',';
        if (c == '(' || c == '[') ++depth;
        else if ((c == ')' || c == ']') && depth > 0) --depth;
        else if (c == ',' && depth == 0) { parts.push_back(text.substr(s, i - s)); s = i + 1; }
    }
    return parts;
}

bool ParseSelectorList(const std::string& text, std::vector<Selector>& out, int& maxSpec) {
    maxSpec = 0;
    for (const std::string& part : SplitSelectorList(text)) {
        Selector sel;
        if (!ParseSelector(part, sel)) return false;
        maxSpec = std::max(maxSpec, sel.specificity);
        out.push_back(std::move(sel));
    }
    return !out.empty();
}

// :has()'s argument: comma-separated selectors, each with an optional leading combinator (' ' when none).
static bool ParseRelativeList(const std::string& text, std::vector<Selector>& out, std::vector<char>& combs, int& maxSpec) {
    maxSpec = 0;
    for (const std::string& raw : SplitSelectorList(text)) {
        std::string part = Trim(raw);
        char comb = ' ';
        if (!part.empty() && (part[0] == '>' || part[0] == '+' || part[0] == '~')) { comb = part[0]; part = Trim(part.substr(1)); }
        Selector sel;
        if (!ParseSelector(part, sel) || sel.pseudoElement) return false;
        for (const Compound& cp : sel.parts) for (const PseudoSel& ps : cp.pseudos) if (ps.kind == PseudoSel::Has) return false;
        maxSpec = std::max(maxSpec, sel.specificity);
        out.push_back(std::move(sel));
        combs.push_back(comb);
        if (out.size() > 64) return false;
    }
    return !out.empty();
}

bool ParseSelector(const std::string& textIn, Selector& out) {
    out = Selector();
    const std::string text = Trim(textIn);
    if (text.empty()) return false;
    const std::size_t n = text.size();
    std::size_t i = 0;
    char pending = 0;   // combinator before the next compound
    int ids = 0, classes = 0, types = 0;
    while (i < n) {
        if (IsWs(text[i]) || text[i] == '>' || text[i] == '+' || text[i] == '~') {
            char comb = ' ';
            while (i < n && (IsWs(text[i]) || text[i] == '>' || text[i] == '+' || text[i] == '~')) {
                if (!IsWs(text[i])) { if (comb != ' ') return false; comb = text[i]; }
                ++i;
            }
            if (i >= n) return comb == ' ' && !out.parts.empty();
            if (out.parts.empty()) return false;
            if (out.pseudoElement) return false;   // nothing may follow a pseudo-element
            pending = comb;
            continue;
        }
        Compound c;
        bool any = false;
        std::string id;
        if (text[i] == '*') { c.tag = "*"; ++i; any = true; }
        else if (ReadIdent(text, i, id)) { c.tag = LowerAscii(id); any = true; ++types; }
        while (i < n && !IsWs(text[i]) && text[i] != '>' && text[i] != '+' && text[i] != '~') {
            const char kind = text[i];
            if (kind == '#' || kind == '.') {
                ++i;
                std::string name;
                if (!ReadIdent(text, i, name)) {
                    // "#123" is a valid id selector only through an escape; plain digits are not an identifier
                    return false;
                }
                if (kind == '#') { if (!c.id.empty() && c.id != name) c.id = "\x01"; else c.id = name; ++ids; }
                else { c.classes.push_back(name); ++classes; }
                any = true;
                continue;
            }
            if (kind == '[') {
                const std::size_t close = text.find(']', i);
                if (close == std::string::npos) return false;
                std::string inner = Trim(text.substr(i + 1, close - i - 1));
                i = close + 1;
                AttrSel a;
                std::size_t k = 0;
                std::string name;
                if (!ReadIdent(inner, k, name)) return false;
                a.name = LowerAscii(name);
                while (k < inner.size() && IsWs(inner[k])) ++k;
                if (k < inner.size()) {
                    if (inner[k] == '=') { a.op = '='; ++k; }
                    else if (k + 1 < inner.size() && inner[k + 1] == '=' && std::strchr("~|^$*", inner[k])) { a.op = inner[k]; k += 2; }
                    else return false;
                    std::string rest = Trim(inner.substr(k));
                    // the i / s flag after the value
                    if (rest.size() >= 2 && IsWs(rest[rest.size() - 2]) && (rest.back() == 'i' || rest.back() == 'I' || rest.back() == 's' || rest.back() == 'S')) {
                        a.ci = rest.back() == 'i' || rest.back() == 'I';
                        rest = Trim(rest.substr(0, rest.size() - 1));
                    }
                    if (!rest.empty() && (rest[0] == '"' || rest[0] == '\'')) a.value = Unquote(rest);
                    else { std::size_t q = 0; std::string v; if (!ReadIdent(rest, q, v) || q != rest.size()) { if (rest.find_first_of(" \t\"'") != std::string::npos || rest.empty()) return false; v = rest; } a.value = v; }
                }
                c.attrs.push_back(a);
                ++classes;
                any = true;
                continue;
            }
            if (kind == ':') {
                const bool element = i + 1 < n && text[i + 1] == ':';
                i += element ? 2 : 1;
                std::string name;
                if (!ReadIdent(text, i, name)) return false;
                name = LowerAscii(name);
                std::string arg;
                bool hasArg = false;
                if (i < n && text[i] == '(') {
                    const std::size_t close = CloseParen(text, i);
                    if (close == std::string::npos) return false;
                    arg = text.substr(i + 1, close - i - 1);
                    hasArg = true;
                    i = close + 1;
                }
                if (element || name == "before" || name == "after" || name == "first-line" || name == "first-letter") {
                    if (out.pseudoElement) return false;
                    if (name == "before") out.pseudoElement = 1;
                    else if (name == "after") out.pseudoElement = 2;
                    else if (name == "first-letter") out.pseudoElement = 4;
                    else if (name == "marker") out.pseudoElement = 5;
                    else if (name == "placeholder" || name == "selection" || name == "first-line" ||
                             name == "backdrop" || name == "file-selector-button") out.pseudoElement = 3;
                    else return false;
                    ++types;
                    any = true;
                    continue;
                }
                PseudoSel p;
                struct K { const char* name; PseudoSel::Kind kind; };
                static const K simple[] = {
                    {"first-child", PseudoSel::FirstChild}, {"last-child", PseudoSel::LastChild}, {"only-child", PseudoSel::OnlyChild},
                    {"first-of-type", PseudoSel::FirstOfType}, {"last-of-type", PseudoSel::LastOfType}, {"only-of-type", PseudoSel::OnlyOfType},
                    {"root", PseudoSel::Root}, {"empty", PseudoSel::Empty}, {"link", PseudoSel::Link}, {"any-link", PseudoSel::Link},
                    {"visited", PseudoSel::Visited}, {"hover", PseudoSel::Hover}, {"focus", PseudoSel::Focus}, {"focus-visible", PseudoSel::Focus},
                    {"focus-within", PseudoSel::FocusWithin}, {"active", PseudoSel::Active}, {"checked", PseudoSel::Checked},
                    {"disabled", PseudoSel::Disabled}, {"enabled", PseudoSel::Enabled}, {"required", PseudoSel::Required},
                    {"optional", PseudoSel::Optional}, {"target", PseudoSel::Never}, {"indeterminate", PseudoSel::Never},
                    {"placeholder-shown", PseudoSel::Never}, {"invalid", PseudoSel::Never}, {"valid", PseudoSel::Never},
                    {"read-only", PseudoSel::Never}, {"read-write", PseudoSel::Never}};
                if (!hasArg && name == "defined") { any = true; ++classes; continue; }   // every element is defined
                bool known = false;
                if (!hasArg) for (const K& k : simple) if (name == k.name) { p.kind = k.kind; known = true; break; }
                if (!known && hasArg) {
                    int spec = 0;
                    if (name == "nth-child" || name == "nth-last-child" || name == "nth-of-type" || name == "nth-last-of-type") {
                        if (!ParseAnB(arg, p.a, p.b)) return false;
                        p.kind = name == "nth-child" ? PseudoSel::NthChild : name == "nth-last-child" ? PseudoSel::NthLastChild
                               : name == "nth-of-type" ? PseudoSel::NthOfType : PseudoSel::NthLastOfType;
                        known = true;
                    } else if (name == "not" || name == "is" || name == "where" || name == "matches" || name == "-webkit-any") {
                        if (!ParseSelectorList(arg, p.list, spec)) return false;
                        for (const Selector& s2 : p.list) if (s2.pseudoElement) return false;
                        p.kind = name == "not" ? PseudoSel::Not : PseudoSel::Is;
                        known = true;
                        if (name != "where") {   // :where adds nothing to the specificity
                            ids += spec / 10000; classes += (spec / 100) % 100; types += spec % 100;
                        }
                        for (const Selector& s2 : p.list) if (s2.dynamic) out.dynamic = true;
                        c.pseudos.push_back(std::move(p));
                        any = true;
                        continue;
                    } else if (name == "has") {
                        // Selectors 4, 4.5: a list of relative selectors ("> img", "+ p", ".note"); no :has inside :has
                        if (!ParseRelativeList(arg, p.list, p.relative, spec)) return false;
                        ids += spec / 10000; classes += (spec / 100) % 100; types += spec % 100;
                        p.kind = PseudoSel::Has;
                        for (const Selector& s2 : p.list) if (s2.dynamic) out.dynamic = true;
                        c.pseudos.push_back(std::move(p));
                        any = true;
                        continue;
                    } else if (name == "lang" || name == "dir") {
                        p.kind = PseudoSel::Never;   // read, never matches (no language data)
                        known = true;
                    }
                }
                if (!known) return false;
                if (p.kind == PseudoSel::Hover || p.kind == PseudoSel::Focus || p.kind == PseudoSel::FocusWithin || p.kind == PseudoSel::Active)
                    out.dynamic = true;
                ++classes;
                c.pseudos.push_back(std::move(p));
                any = true;
                continue;
            }
            return false;   // anything else: not supported
        }
        if (!any) return false;
        if (!out.parts.empty()) out.combinators.push_back(pending ? pending : ' ');
        pending = 0;
        out.parts.push_back(std::move(c));
    }
    out.specificity = std::min(ids, 99) * 10000 + std::min(classes, 99) * 100 + std::min(types, 99);
    return !out.parts.empty();
}

namespace {

const Node* PrevElement(const Node& el) {
    if (!el.parent) return nullptr;
    const Node* prev = nullptr;
    for (const auto& c : el.parent->children) {
        if (c.get() == &el) return prev;
        if (c->kind == NodeKind::Element) prev = c.get();
    }
    return nullptr;
}

// el's place among its element siblings (1-based) counted from the start or the end, all of them or only its type.
int SiblingIndex(const Node& el, bool fromEnd, bool ofType) {
    if (!el.parent) return 1;
    const auto& kids = el.parent->children;
    int idx = 0;
    if (!fromEnd) {
        for (const auto& c : kids) {
            if (c->kind != NodeKind::Element || (ofType && c->name != el.name)) continue;
            ++idx;
            if (c.get() == &el) return idx;
        }
    } else {
        for (std::size_t k = kids.size(); k-- > 0;) {
            const Node* c = kids[k].get();
            if (c->kind != NodeKind::Element || (ofType && c->name != el.name)) continue;
            ++idx;
            if (c == &el) return idx;
        }
    }
    return idx;
}

bool NthMatches(int a, int b, int index) {
    if (a == 0) return index == b;
    const int d = index - b;
    return d % a == 0 && d / a >= 0;
}

bool AttrMatches(const AttrSel& a, const Node& el) {
    const std::string* v = el.Attr(a.name.c_str());
    if (!v) return false;
    if (a.op == 0) return true;
    std::string have = *v, want = a.value;
    if (a.ci) { have = LowerAscii(have); want = LowerAscii(want); }
    switch (a.op) {
    case '=': return have == want;
    case '~': {
        if (want.empty() || want.find_first_of(" \t\n\r\f") != std::string::npos) return false;
        for (std::size_t k = 0; k < have.size();) {
            while (k < have.size() && IsWs(have[k])) ++k;
            std::size_t e = k;
            while (e < have.size() && !IsWs(have[e])) ++e;
            if (e > k && have.compare(k, e - k, want) == 0 && e - k == want.size()) return true;
            k = e;
        }
        return false;
    }
    case '|': return have == want || (have.size() > want.size() && have.compare(0, want.size(), want) == 0 && have[want.size()] == '-');
    case '^': return !want.empty() && have.compare(0, want.size(), want) == 0;
    case '$': return !want.empty() && have.size() >= want.size() && have.compare(have.size() - want.size(), want.size(), want) == 0;
    case '*': return !want.empty() && have.find(want) != std::string::npos;
    default: return false;
    }
}

bool IsFormControl(const Node& el) {
    return el.name == "input" || el.name == "button" || el.name == "select" || el.name == "textarea" || el.name == "option" ||
           el.name == "optgroup" || el.name == "fieldset";
}
bool IsAncestorOrSelf(const Node& el, const Node* n) {
    for (; n; n = n->parent) if (n == &el) return true;
    return false;
}

bool MatchAt(const Selector& sel, std::size_t idx, const Node& el, const StyleContext* ctx, int& budget);

bool CompoundMatches(const Compound& c, const Node& el, const StyleContext* ctx, int& budget) {
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
    for (const AttrSel& a : c.attrs) if (!AttrMatches(a, el)) return false;
    for (const PseudoSel& p : c.pseudos) {
        bool ok = false;
        switch (p.kind) {
        case PseudoSel::FirstChild: ok = SiblingIndex(el, false, false) == 1; break;
        case PseudoSel::LastChild: ok = SiblingIndex(el, true, false) == 1; break;
        case PseudoSel::OnlyChild: ok = SiblingIndex(el, false, false) == 1 && SiblingIndex(el, true, false) == 1; break;
        case PseudoSel::NthChild: ok = NthMatches(p.a, p.b, SiblingIndex(el, false, false)); break;
        case PseudoSel::NthLastChild: ok = NthMatches(p.a, p.b, SiblingIndex(el, true, false)); break;
        case PseudoSel::FirstOfType: ok = SiblingIndex(el, false, true) == 1; break;
        case PseudoSel::LastOfType: ok = SiblingIndex(el, true, true) == 1; break;
        case PseudoSel::OnlyOfType: ok = SiblingIndex(el, false, true) == 1 && SiblingIndex(el, true, true) == 1; break;
        case PseudoSel::NthOfType: ok = NthMatches(p.a, p.b, SiblingIndex(el, false, true)); break;
        case PseudoSel::NthLastOfType: ok = NthMatches(p.a, p.b, SiblingIndex(el, true, true)); break;
        case PseudoSel::Root: ok = el.parent && el.parent->kind == NodeKind::Document; break;
        case PseudoSel::Empty: {
            ok = true;
            for (const auto& k : el.children) if (k->kind == NodeKind::Element || (k->kind == NodeKind::Text && !k->data.empty())) ok = false;
            break;
        }
        case PseudoSel::Link: ok = (el.name == "a" || el.name == "area") && el.Attr("href"); break;
        case PseudoSel::Visited: ok = false; break;   // never: a page must not learn the person's history
        case PseudoSel::Hover: ok = ctx && IsAncestorOrSelf(el, ctx->hover); break;
        case PseudoSel::Active: ok = ctx && IsAncestorOrSelf(el, ctx->active); break;
        case PseudoSel::Focus: ok = ctx && ctx->focus == &el; break;
        case PseudoSel::FocusWithin: ok = ctx && IsAncestorOrSelf(el, ctx->focus); break;
        case PseudoSel::Checked:
            if (el.name == "input") {
                const std::string* t = el.Attr("type");
                const std::string type = t ? LowerAscii(*t) : std::string();
                if (type == "checkbox" || type == "radio") ok = ctx && ctx->isChecked ? ctx->isChecked(el, ctx->checkedUser) : el.Attr("checked") != nullptr;
            } else if (el.name == "option") ok = el.Attr("selected") != nullptr;
            break;
        case PseudoSel::Disabled: ok = IsFormControl(el) && el.Attr("disabled"); break;
        case PseudoSel::Enabled: ok = IsFormControl(el) && !el.Attr("disabled"); break;
        case PseudoSel::Required: ok = IsFormControl(el) && el.Attr("required"); break;
        case PseudoSel::Optional: ok = IsFormControl(el) && !el.Attr("required"); break;
        case PseudoSel::Not:
            ok = true;
            for (const Selector& s : p.list) if (MatchAt(s, s.parts.size() - 1, el, ctx, budget)) { ok = false; break; }
            break;
        case PseudoSel::Is:
            for (const Selector& s : p.list) if (MatchAt(s, s.parts.size() - 1, el, ctx, budget)) { ok = true; break; }
            break;
        case PseudoSel::Has: {
            // an element related to el as the combinator says matches the selector (a descendant selector is matched in full,
            // so its earlier parts may also match outside el: simplified). At most 4096 elements looked at per :has().
            int seen = 0;
            for (std::size_t k = 0; k < p.list.size() && !ok; ++k) {
                const Selector& s = p.list[k];
                const char comb = k < p.relative.size() ? p.relative[k] : ' ';
                if (comb == '>' || comb == ' ') {
                    std::vector<const Node*> todo;
                    for (const auto& ch : el.children) if (ch->kind == NodeKind::Element) todo.push_back(ch.get());
                    while (!todo.empty() && !ok && ++seen < 4096 && budget > 0) {
                        const Node* d = todo.back();
                        todo.pop_back();
                        if (MatchAt(s, s.parts.size() - 1, *d, ctx, budget)) ok = true;
                        if (comb == ' ') for (const auto& ch : d->children) if (ch->kind == NodeKind::Element) todo.push_back(ch.get());
                    }
                } else if (el.parent) {
                    bool after = false;
                    for (const auto& sib : el.parent->children) {
                        if (sib.get() == &el) { after = true; continue; }
                        if (!after || sib->kind != NodeKind::Element) continue;
                        if (++seen >= 4096 || budget <= 0) break;
                        if (MatchAt(s, s.parts.size() - 1, *sib, ctx, budget)) { ok = true; break; }
                        if (comb == '+') break;
                    }
                }
            }
            break;
        }
        case PseudoSel::Never: ok = false; break;
        }
        if (!ok) return false;
    }
    return true;
}

// parts[0..idx] match with part idx on el. `budget` caps the work a hostile selector can cause.
bool MatchAt(const Selector& sel, std::size_t idx, const Node& el, const StyleContext* ctx, int& budget) {
    if (--budget < 0) return false;
    if (sel.parts.empty() || !CompoundMatches(sel.parts[idx], el, ctx, budget)) return false;
    if (idx == 0) return true;
    const char comb = sel.combinators[idx - 1];
    if (comb == '>') return el.parent && el.parent->kind == NodeKind::Element && MatchAt(sel, idx - 1, *el.parent, ctx, budget);
    if (comb == ' ') {
        for (const Node* a = el.parent; a && a->kind == NodeKind::Element; a = a->parent)
            if (MatchAt(sel, idx - 1, *a, ctx, budget)) return true;
        return false;
    }
    if (comb == '+') { const Node* p = PrevElement(el); return p && MatchAt(sel, idx - 1, *p, ctx, budget); }
    // '~'
    for (const Node* p = PrevElement(el); p; p = PrevElement(*p)) if (MatchAt(sel, idx - 1, *p, ctx, budget)) return true;
    return false;
}

} // namespace

bool Matches(const Selector& sel, const Node& el, const StyleContext* ctx, int pseudo) {
    if (sel.parts.empty() || sel.pseudoElement != pseudo) return false;
    int budget = 20000;
    return MatchAt(sel, sel.parts.size() - 1, el, ctx, budget);
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
        const std::string prop = Trim(d.substr(0, colon));
        decl.property = prop.compare(0, 2, "--") == 0 ? prop : LowerAscii(prop);   // custom properties keep their case
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
        if (v.empty() && decl.property.compare(0, 2, "--") != 0) continue;
        decl.value = v;
        out.push_back(std::move(decl));
    }
    return out;
}

namespace {

// A rule's block split into its own declarations and the rules nested in it (prelude, block) (CSS Nesting 1).
void SplitNested(const std::string& body, std::string& own, std::vector<std::pair<std::string, std::string>>& nested) {   // dup-allow: splits a CSS block into declarations and nested rules (CSS Nesting 1), not a string Split
    std::size_t s = 0;
    int depth = 0;
    for (std::size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (c == '"' || c == '\'') { const char q = c; ++i; while (i < body.size() && body[i] != q) { if (body[i] == '\\') ++i; ++i; } continue; }
        if (c == '(') ++depth;
        else if (c == ')' && depth > 0) --depth;
        else if (depth == 0 && c == ';') { own.append(body, s, i + 1 - s); s = i + 1; }
        else if (depth == 0 && c == '{') {
            const std::size_t end = BlockEnd(body, i + 1);
            if (nested.size() < 256) nested.push_back({body.substr(s, i - s), body.substr(i + 1, std::min(end, body.size()) - i - 1)});
            i = std::min(end, body.size());
            s = i + 1;
        }
    }
    if (s < body.size()) own.append(body, s, std::string::npos);
}

void ParseRules(const std::string& css, std::size_t from, std::size_t to, StyleSheet& sheet, const std::vector<int>& media,
                const CssLimits& limits, int nesting) {
    std::size_t i = from;
    while (i < to && sheet.rules.size() < limits.maxRules) {
        while (i < to && (IsWs(css[i]) || css[i] == ';' || css[i] == '}')) ++i;
        if (i >= to) break;
        if (css[i] == '@') {
            std::size_t j = i + 1;
            while (j < to && (IsIdentChar(css[j]))) ++j;
            const std::string name = LowerAscii(css.substr(i + 1, j - i - 1));
            std::size_t k = j;
            while (k < to && css[k] != ';' && css[k] != '{') {
                if (css[k] == '"' || css[k] == '\'') { const char q = css[k++]; while (k < to && css[k] != q) ++k; }
                if (k < to) ++k;
            }
            const std::string prelude = Trim(css.substr(j, k - j));
            if (k >= to || css[k] == ';') {
                if (name == "import") {
                    const std::vector<std::string> w = Words(prelude);
                    if (!w.empty()) {
                        std::string u = UrlOf(w[0]);
                        if (u.empty()) u = Unquote(w[0]);
                        if (!u.empty() && sheet.imports.size() < 64) sheet.imports.push_back(u);
                    }
                }
                i = k + 1;
                continue;
            }
            const std::size_t close = BlockEnd(css, k + 1);
            const std::size_t inner = k + 1, innerEnd = std::min(close, to);
            if ((name == "media" || name == "supports" || name == "layer" || name == "document" || name == "-moz-document") && nesting < limits.maxNesting) {
                if (name == "media") {
                    std::vector<int> m = media;
                    m.push_back((int)sheet.media.size());
                    sheet.media.push_back(ParseMediaQuery(prelude));
                    ParseRules(css, inner, innerEnd, sheet, m, limits, nesting + 1);
                } else if (name == "supports") {
                    // the features a page checks for are mostly ones we read (display: grid / flex, gap ...): the block applies,
                    // unless it asks for the absence of something ("not (...)")
                    if (LowerAscii(prelude).compare(0, 3, "not") != 0) ParseRules(css, inner, innerEnd, sheet, media, limits, nesting + 1);
                } else ParseRules(css, inner, innerEnd, sheet, media, limits, nesting + 1);
            } else if (name == "font-face") {
                FontFace ff;
                for (const Declaration& d : ParseDeclarations(css.substr(inner, innerEnd - inner), limits)) {
                    const std::string v = LowerAscii(d.value);
                    if (d.property == "font-family") ff.family = LowerAscii(Unquote(d.value));
                    else if (d.property == "font-weight") {
                        if (v == "bold") ff.weight = 700;
                        else { float n; std::string u; if (ParseNumber(Words(v).empty() ? v : Words(v)[0], n, u) && u.empty()) ff.weight = (int)n; }
                    } else if (d.property == "font-style") ff.italic = v.compare(0, 6, "italic") == 0 || v.compare(0, 7, "oblique") == 0;
                    else if (d.property == "src" && ff.url.empty()) {
                        std::string woffUrl;   // a WOFF 1.0 file (WebFont.hpp decodes it) when there is no TrueType / OpenType one
                        for (const std::vector<std::string>& g : CommaGroups(Words(d.value))) {
                            std::string url, fmt;
                            for (const std::string& w : g) {
                                if (url.empty()) url = UrlOf(w);
                                const std::string lw = LowerAscii(w);
                                if (lw.compare(0, 7, "format(") == 0) fmt = LowerAscii(Unquote(w.substr(7, w.size() - 8)));
                            }
                            // TrueType / OpenType first (our rasterizer reads them); WOFF 1.0 next; WOFF2 is not read yet
                            const std::string ext = LowerAscii(url.substr(url.find_last_of('.') == std::string::npos ? url.size() : url.find_last_of('.')));
                            const bool ttf = fmt == "truetype" || fmt == "opentype" || (fmt.empty() && (ext.compare(0, 4, ".ttf") == 0 || ext.compare(0, 4, ".otf") == 0));
                            if (!url.empty() && ttf) { ff.url = url; break; }
                            const bool woff = fmt == "woff" || (fmt.empty() && ext.compare(0, 5, ".woff") == 0 && ext.compare(0, 6, ".woff2") != 0);
                            if (!url.empty() && woff && woffUrl.empty()) woffUrl = url;
                        }
                        if (ff.url.empty()) ff.url = woffUrl;
                    }
                }
                if (!ff.family.empty() && !ff.url.empty() && sheet.fontFaces.size() < 64) sheet.fontFaces.push_back(ff);
            }
            i = close + 1;
            continue;
        }
        // the prelude (selectors) up to '{'
        std::size_t open = i;
        while (open < to && css[open] != '{') {
            if (css[open] == '"' || css[open] == '\'') { const char q = css[open++]; while (open < to && css[open] != q) ++open; }
            if (open < to) ++open;
        }
        if (open >= to) break;
        const std::size_t close = BlockEnd(css, open + 1);
        const std::string prelude = css.substr(i, open - i);
        const std::string body = css.substr(open + 1, (close < to ? close : to) - open - 1);
        i = close + 1;
        Rule rule;
        rule.media = media;
        int spec = 0;
        if (!ParseSelectorList(prelude, rule.selectors, spec)) continue;
        bool tooLong = false;
        for (const Selector& s : rule.selectors) { if (s.parts.size() > limits.maxSelectorParts) tooLong = true; if (s.dynamic) sheet.hasDynamic = true; }
        if (tooLong) continue;
        // CSS Nesting 1: rules inside the block apply relative to this one ("&" is this rule's selector; without "&" they are
        // its descendants); they come after the block's own declarations
        std::string own;
        std::vector<std::pair<std::string, std::string>> nested;
        SplitNested(body, own, nested);
        rule.declarations = ParseDeclarations(own, limits);
        if (!rule.declarations.empty()) sheet.rules.push_back(std::move(rule));
        if (nesting >= limits.maxNesting) continue;
        const std::string parent = ":is(" + Trim(prelude) + ")";
        for (const auto& nr : nested) {
            std::string pre = Trim(nr.first);
            if (!pre.empty() && pre[0] == '@') {   // @media / @supports inside a rule: its declarations belong to this rule
                const std::string inner = parent + "{" + nr.second + "}";
                ParseRules(pre + "{" + inner + "}", 0, pre.size() + inner.size() + 2, sheet, media, limits, nesting + 1);
                continue;
            }
            std::string sel;
            for (const std::string& part0 : SplitSelectorList(pre)) {
                std::string part = Trim(part0);
                if (part.empty()) continue;
                std::string full;
                if (part.find('&') != std::string::npos) {
                    for (char c : part) { if (c == '&') full += parent; else full += c; }
                } else full = parent + " " + part;
                if (!sel.empty()) sel += ", ";
                sel += full;
                if (sel.size() > 8192) break;
            }
            if (sel.empty()) continue;
            const std::string text = sel + "{" + nr.second + "}";
            ParseRules(text, 0, text.size(), sheet, media, limits, nesting + 1);
        }
    }
}

} // namespace

StyleSheet ParseStyleSheet(const std::string& cssIn, const CssLimits& limits) {
    StyleSheet sheet;
    const std::string css = StripComments(cssIn.size() > limits.maxBytes ? cssIn.substr(0, limits.maxBytes) : cssIn);
    ParseRules(css, 0, css.size(), sheet, std::vector<int>(), limits, 0);
    return sheet;
}

// ---- the default sheet -------------------------------------------------------------------------------------------------

const StyleSheet& DefaultStyleSheet() {
    static const StyleSheet sheet = ParseStyleSheet(
        "html, address, blockquote, body, dd, div, dl, dt, fieldset, form, frameset, h1, h2, h3, h4, h5, h6, ol, p, ul, center,"
        " dir, hr, menu, pre, listing, xmp, plaintext, article, aside, footer, header, hgroup, main, nav, section, figure,"
        " figcaption, details, summary, search, legend, optgroup { display: block }\n"
        "head, script, style, title, meta, link, base, template, datalist, param, noembed, noframes, area, map, rp, col,"
        " colgroup, source, track, dialog:not([open]), input[type=hidden], [hidden] { display: none }\n"
        "dialog[open] { display: block; position: absolute; left: 0; right: 0; margin: auto; border: 2px solid; padding: 1em; background: white; color: black }\n"
        "li { display: list-item }\n"
        "table { display: table }\n"
        "tr { display: table-row }\n"
        "tbody { display: table-row-group }\n"
        "thead { display: table-header-group }\n"
        "tfoot { display: table-footer-group }\n"
        "table { border-spacing: 2px; border-collapse: separate; text-indent: 0 }\n"
        "td, th { display: table-cell; padding: 1px }\n"
        "thead, tbody, tfoot, table > tr { vertical-align: middle }\n"
        "tr, td, th { vertical-align: inherit }\n"
        "caption { display: table-caption; text-align: center }\n"
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
        "sub { vertical-align: sub } sup { vertical-align: super }\n"
        "big { font-size: larger }\n"
        "center { text-align: center }\n"
        "hr { margin: 0.5em auto; border-width: 1px; border-style: inset; border-color: gray }\n"
        "img { max-width: 100% }\n"
        "mark { background-color: yellow; color: black }\n"
        "nobr { white-space: nowrap }\n"
        "fieldset { margin: 0 2px; padding: 0.35em 0.75em 0.625em; border: 2px groove #c0c0c0 }\n"
        "legend { padding: 0 2px }\n"
        "input, select, textarea, button { display: inline-block; font-size: 13.33px; margin: 0 }\n"
        "input, textarea, select { border: 2px inset #767676; background-color: white; color: black; padding: 1px 2px }\n"
        "button, input[type=submit], input[type=button], input[type=reset] { border: 2px outset #767676; background-color: #efefef;"
        " color: black; padding: 1px 6px }\n"
        "input[type=checkbox], input[type=radio] { margin: 3px 3px 3px 4px; border: 0; padding: 0; background-color: transparent }\n"
        "textarea { white-space: pre-wrap; font-family: monospace }\n"
        "label { cursor: default }\n"
        // round 3, from the HTML standard's Rendering section (15.3.1-15.3.11, 15.5.17)
        "table { box-sizing: border-box }\n"
        "button { text-align: center }\n"
        "pre, code, kbd, samp, tt, listing, xmp, plaintext { font-size: 0.8125em }\n"   // monospace text at 13 px by default
        "pre code, pre kbd, pre samp, pre tt, code code { font-size: 1em }\n"
        "article h1, aside h1, nav h1, section h1 { font-size: 1.5em; margin: 0.83em 0 }\n"
        "abbr[title], acronym[title] { text-decoration: underline dotted }\n"
        "q::before { content: open-quote } q::after { content: close-quote }\n"
        "summary { display: block }\n"
        "summary::before { content: '\\25B8\\00A0' }\n"
        "details[open] > summary::before { content: '\\25BE\\00A0' }\n"
        "details:not([open]) > :not(summary) { display: none }\n"
        "fieldset { min-width: min-content }\n"
        "legend { display: block }\n"
        "address { font-style: italic }\n");
    return sheet;
}

// ---- the cascade -------------------------------------------------------------------------------------------------------

namespace {

struct Cascaded { int bucket; int specificity; std::size_t order; const Declaration* decl; };

// What the cascade knows while it applies declarations, beyond ComputedStyle itself.
struct Work {
    float borderWidth[4] = {3, 3, 3, 3};   // "medium"
    int borderStyle[4] = {0, 0, 0, 0};   // 0: none / hidden; else the line (BorderStyleWord)
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
// A border style: 0 none / hidden, else the line drawn (ComputedStyle::borderLine: 1 solid (also groove, ridge, inset, outset),
// 2 dashed, 3 dotted, 4 double).
bool BorderStyleWord(const std::string& w, int& line) {
    if (w == "none" || w == "hidden") { line = 0; return true; }
    if (w == "solid" || w == "groove" || w == "ridge" || w == "inset" || w == "outset") { line = 1; return true; }
    if (w == "dashed") { line = 2; return true; }
    if (w == "dotted") { line = 3; return true; }
    if (w == "double") { line = 4; return true; }
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
    if (rest == "-top" || rest == "-block-start") return 0;
    if (rest == "-right" || rest == "-inline-end") return 1;
    if (rest == "-bottom" || rest == "-block-end") return 2;
    if (rest == "-left" || rest == "-inline-start") return 3;
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
    const float px = l.Resolve(parentSize);
    if (px < 0) return false;
    out = std::min(px, 2000.0f);
    return true;
}

bool IsMonoFamily(const std::string& family) {
    // only the first family counts (the rest are fallbacks)
    std::string f = LowerAscii(family);
    const std::size_t comma = f.find(',');
    if (comma != std::string::npos) f = f.substr(0, comma);
    return f.find("monospace") != std::string::npos || f.find("mono") != std::string::npos || f.find("courier") != std::string::npos ||
           f.find("consolas") != std::string::npos || f.find("menlo") != std::string::npos;
}

bool ListStyleWord(const std::string& w, ListStyle& out) {
    struct K { const char* k; ListStyle s; };
    static const K keys[] = {{"disc", ListStyle::Disc}, {"circle", ListStyle::Circle}, {"square", ListStyle::Square},
                             {"decimal", ListStyle::Decimal}, {"decimal-leading-zero", ListStyle::DecimalLeadingZero},
                             {"lower-alpha", ListStyle::LowerAlpha}, {"lower-latin", ListStyle::LowerAlpha},
                             {"upper-alpha", ListStyle::UpperAlpha}, {"upper-latin", ListStyle::UpperAlpha},
                             {"lower-roman", ListStyle::LowerRoman}, {"upper-roman", ListStyle::UpperRoman}, {"none", ListStyle::None},
                             {"lower-greek", ListStyle::LowerGreek}};
    for (const K& k : keys) if (w == k.k) { out = k.s; return true; }
    return false;
}

bool AlignWord(const std::string& wIn, Align& out) {
    std::string w = wIn;
    for (const char* prefix : {"safe ", "unsafe ", "first ", "last "}) if (w.compare(0, std::strlen(prefix), prefix) == 0) w = Trim(w.substr(std::strlen(prefix)));
    struct K { const char* k; Align a; };
    static const K keys[] = {{"auto", Align::Auto}, {"normal", Align::Normal}, {"stretch", Align::Stretch}, {"start", Align::Start},
                             {"flex-start", Align::Start}, {"self-start", Align::Start}, {"left", Align::Start}, {"end", Align::End},
                             {"flex-end", Align::End}, {"self-end", Align::End}, {"right", Align::End}, {"center", Align::Center},
                             {"baseline", Align::Baseline}, {"space-between", Align::SpaceBetween}, {"space-around", Align::SpaceAround},
                             {"space-evenly", Align::SpaceEvenly}};
    for (const K& k : keys) if (w == k.k) { out = k.a; return true; }
    return false;
}

bool TrackSize(const std::string& wIn, float em, GridTrack::Kind& kind, float& value) {
    const std::string w = LowerAscii(wIn);
    if (w == "auto") { kind = GridTrack::Auto; return true; }
    if (w == "min-content") { kind = GridTrack::MinContent; return true; }
    if (w == "max-content") { kind = GridTrack::MaxContent; return true; }
    float n; std::string u;
    if (ParseNumber(w, n, u) && u == "fr") { if (n < 0) return false; kind = GridTrack::Fr; value = n; return true; }
    Length l;
    if (!ParseLength(wIn, em, true, l) || l.value < 0) return false;
    if (l.unit == Length::Percent) { kind = GridTrack::Percent; value = l.value; return true; }
    if (l.unit == Length::Px) { kind = GridTrack::Fixed; value = l.value; return true; }
    kind = GridTrack::Fixed; value = l.value;   // calc() with %: the px part (simplified)
    return true;
}

bool ParseTrack(const std::string& w, float em, GridTrack& t) {
    const std::string lw = LowerAscii(w);
    if (lw.compare(0, 7, "minmax(") == 0 && w.back() == ')') {
        std::vector<std::string> args;
        for (const std::string& a : Words(w.substr(7, w.size() - 8))) if (a != ",") args.push_back(a);
        if (args.size() != 2) return false;
        if (!TrackSize(args[0], em, t.minKind, t.minValue) || !TrackSize(args[1], em, t.maxKind, t.maxValue)) return false;
        if (t.minKind == GridTrack::Fr) { t.minKind = GridTrack::Auto; }   // a flexible minimum is not allowed: auto
        return true;
    }
    if (lw.compare(0, 12, "fit-content(") == 0 && w.back() == ')') {
        t.minKind = GridTrack::Auto;
        return TrackSize(w.substr(12, w.size() - 13), em, t.maxKind, t.maxValue);
    }
    GridTrack::Kind k; float v = 0;
    if (!TrackSize(w, em, k, v)) return false;
    if (k == GridTrack::Fr) { t.minKind = GridTrack::Auto; t.maxKind = GridTrack::Fr; t.maxValue = v; }
    else { t.minKind = t.maxKind = k; t.minValue = t.maxValue = v; }
    return true;
}

bool ParseTemplate(const std::string& v, float em, GridTemplate& out) {
    GridTemplate t;
    if (LowerAscii(Trim(v)) == "none") { out = t; return true; }
    std::vector<std::string> pendingNames;
    t.lineNames.emplace_back();
    for (const std::string& w : Words(v)) {
        if (w.front() == '[' && w.back() == ']') {
            for (const std::string& nm : Words(w.substr(1, w.size() - 2))) t.lineNames.back().push_back(nm);
            continue;
        }
        if (LowerAscii(w).compare(0, 7, "repeat(") == 0 && w.back() == ')') {
            const std::string inner = w.substr(7, w.size() - 8);
            const std::size_t comma = inner.find(',');
            if (comma == std::string::npos) return false;
            const std::string count = LowerAscii(Trim(inner.substr(0, comma)));
            std::vector<GridTrack> rep;
            for (const std::string& tw : Words(inner.substr(comma + 1))) {
                if (tw.front() == '[') continue;   // line names inside repeat(): not kept
                GridTrack g;
                if (!ParseTrack(tw, em, g)) return false;
                rep.push_back(g);
            }
            if (rep.empty()) return false;
            if (count == "auto-fill" || count == "auto-fit") {
                if (!t.autoRepeat.empty()) return false;
                t.autoRepeat = rep;
                t.autoFit = count == "auto-fit";
                t.repeatAt = t.tracks.size();
                continue;
            }
            float n; std::string u;
            if (!ParseNumber(count, n, u) || !u.empty() || n < 1) return false;
            const int times = std::min(1000, (int)n);
            for (int k = 0; k < times; ++k)
                for (const GridTrack& g : rep) { t.tracks.push_back(g); t.lineNames.emplace_back(); }
            continue;
        }
        GridTrack g;
        if (!ParseTrack(w, em, g)) return false;
        t.tracks.push_back(g);
        t.lineNames.emplace_back();
        if (t.tracks.size() > 1000) return false;
    }
    out = t;
    return true;
}

GridLine ParseGridLine(const std::string& v) {
    GridLine g;
    std::vector<std::string> w = Words(v);
    bool span = false;
    for (const std::string& x : w) {
        const std::string lx = LowerAscii(x);
        if (lx == "auto") continue;
        if (lx == "span") { span = true; continue; }
        float n; std::string u;
        if (ParseNumber(x, n, u) && u.empty()) {
            const int k = std::max(-10000, std::min(10000, (int)n));
            if (span) g.span = std::max(1, k); else g.line = k;
        } else g.name = x;
    }
    if (span && g.span == 0) g.span = 1;
    return g;
}

// "a / b" into the start and end of one axis.
void ParseGridPair(const std::string& v, GridLine& start, GridLine& end) {
    const std::size_t slash = v.find('/');
    start = ParseGridLine(slash == std::string::npos ? v : v.substr(0, slash));
    if (slash != std::string::npos) end = ParseGridLine(v.substr(slash + 1));
    else if (!start.name.empty() && start.line == 0 && start.span == 0) end = start;   // an area name: its end line too
    else end = GridLine();
}

bool ShadowList(const std::string& v, float em, std::vector<BoxShadow>& out) {
    out.clear();
    if (LowerAscii(Trim(v)) == "none") return true;
    for (const std::vector<std::string>& g : CommaGroups(Words(v))) {
        BoxShadow s;
        float nums[4];
        int nn = 0;
        for (const std::string& w : g) {
            if (LowerAscii(w) == "inset") { s.inset = true; continue; }
            Length l;
            if (nn < 4 && ParseLength(w, em, false, l) && l.unit == Length::Px) { nums[nn++] = l.value; continue; }
            Color c;
            if (ParseColor(w, c)) { s.color = c; continue; }
            return false;
        }
        if (nn < 2) return false;
        s.x = nums[0]; s.y = nums[1];
        if (nn > 2) s.blur = std::max(0.0f, nums[2]);
        if (nn > 3) s.spread = nums[3];
        if (out.size() < 8) out.push_back(s);
    }
    return true;
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

bool BackgroundPos(const std::vector<std::string>& w, float em, Length& x, Length& y) {
    std::vector<Length> v;
    bool swap = false;
    for (std::size_t k = 0; k < w.size() && k < 2; ++k) {
        const std::string lw = LowerAscii(w[k]);
        Length l;
        if (lw == "left" || lw == "top") { l.unit = Length::Percent; l.value = 0; if (lw == "top" && k == 0) swap = true; }
        else if (lw == "center") { l.unit = Length::Percent; l.value = 50; }
        else if (lw == "right" || lw == "bottom") { l.unit = Length::Percent; l.value = 100; if (lw == "bottom" && k == 0) swap = true; }
        else if (!ParseLength(w[k], em, true, l)) return false;
        v.push_back(l);
    }
    if (v.empty()) return false;
    if (v.size() == 1) { Length c; c.unit = Length::Percent; c.value = 50; v.push_back(c); }
    if (swap) std::swap(v[0], v[1]);
    x = v[0]; y = v[1];
    return true;
}

// CSS Logical Properties 1 for horizontal, left-to-right text (the only writing mode the layout has): the inline axis is
// horizontal (start = left), the block axis vertical (start = top). "" when the property is not a logical one; a pair for the
// two-sided shorthands.
std::vector<std::string> PhysicalOf(const std::string& p) {
    struct M { const char* from; const char* to; };
    static const M exact[] = {{"inline-size", "width"}, {"block-size", "height"}, {"min-inline-size", "min-width"}, {"min-block-size", "min-height"},
                              {"max-inline-size", "max-width"}, {"max-block-size", "max-height"}, {"inset-inline-start", "left"},
                              {"inset-inline-end", "right"}, {"inset-block-start", "top"}, {"inset-block-end", "bottom"}};
    for (const M& m : exact) if (p == m.from) return {m.to};
    if (p == "inset-inline") return {"left", "right"};
    if (p == "inset-block") return {"top", "bottom"};
    static const M prefixes[] = {{"border-inline-start", "border-left"}, {"border-inline-end", "border-right"},
                                 {"border-block-start", "border-top"}, {"border-block-end", "border-bottom"}};
    for (const M& m : prefixes) { const std::size_t n = std::strlen(m.from); if (p.compare(0, n, m.from) == 0) return {m.to + p.substr(n)}; }
    if (p.compare(0, 13, "border-inline") == 0) return {"border-left" + p.substr(13), "border-right" + p.substr(13)};
    if (p.compare(0, 12, "border-block") == 0) return {"border-top" + p.substr(12), "border-bottom" + p.substr(12)};
    return {};
}

void Apply(ComputedStyle& s, Work& wk, const Declaration& d, const ComputedStyle& parent) {
    {
        const std::vector<std::string> phys = PhysicalOf(d.property);
        if (!phys.empty()) {
            // a two-valued shorthand (inset-inline: 1px 2px) gives its first value to the start side, the second to the end
            const std::vector<std::string> ws = Words(d.value);
            for (std::size_t k = 0; k < phys.size(); ++k) {
                Declaration one = d;
                one.property = phys[k];
                if (phys.size() == 2 && ws.size() == 2 && d.property.find("color") == std::string::npos && d.property.find("style") == std::string::npos &&
                    (d.property.compare(0, 5, "inset") == 0 || d.property.find("width") != std::string::npos)) one.value = ws[k];
                Apply(s, wk, one, parent);
            }
            return;
        }
    }
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
        struct K { const char* k; Display d; };
        static const K keys[] = {{"block", Display::Block}, {"flow-root", Display::FlowRoot}, {"flex", Display::Flex}, {"grid", Display::Grid},
                                 {"inline", Display::Inline}, {"inline-block", Display::InlineBlock}, {"inline-flex", Display::InlineFlex},
                                 {"inline-grid", Display::InlineGrid}, {"contents", Display::Inline}, {"list-item", Display::ListItem},
                                 {"none", Display::None}, {"table", Display::Table}, {"inline-table", Display::Table},
                                 {"table-row", Display::TableRow}, {"table-cell", Display::TableCell}, {"table-caption", Display::TableCaption},
                                 {"table-row-group", Display::TableSection}, {"table-header-group", Display::TableHeaderGroup},
                                 {"table-footer-group", Display::TableFooterGroup}, {"block flex", Display::Flex}, {"block grid", Display::Grid},
                                 {"inline flex", Display::InlineFlex}, {"inline grid", Display::InlineGrid}, {"block flow-root", Display::FlowRoot},
                                 {"-webkit-box", Display::Flex}, {"-webkit-flex", Display::Flex}, {"-ms-flexbox", Display::Flex}};
        for (const K& k : keys) if (v == k.k) { s.display = k.d; break; }
    } else if (p == "margin") {
        Length m[4];
        if (FourSides(words, m, lengthOrAuto)) for (int k = 0; k < 4; ++k) s.margin[k] = m[k];
    } else if (SideOf(p, "margin") >= 0) {
        Length l;
        if (words.size() == 1 && lengthOrAuto(words[0], l)) s.margin[SideOf(p, "margin")] = l;
    } else if (p == "margin-inline" || p == "margin-block" || p == "padding-inline" || p == "padding-block") {
        Length l[2];
        const bool margin = p[0] == 'm';
        if (words.size() >= 1 && words.size() <= 2 && (margin ? lengthOrAuto(words[0], l[0]) : nonNegative(words[0], l[0]))) {
            l[1] = l[0];
            if (words.size() == 2 && !(margin ? lengthOrAuto(words[1], l[1]) : nonNegative(words[1], l[1]))) return;
            Length* side = margin ? s.margin : s.padding;
            if (p.find("inline") != std::string::npos) { side[3] = l[0]; side[1] = l[1]; } else { side[0] = l[0]; side[2] = l[1]; }
        }
    } else if (p == "padding") {
        Length m[4];
        if (FourSides(words, m, nonNegative)) for (int k = 0; k < 4; ++k) s.padding[k] = m[k];
    } else if (SideOf(p, "padding") >= 0) {
        Length l;
        if (words.size() == 1 && nonNegative(words[0], l)) s.padding[SideOf(p, "padding")] = l;
    } else if (p == "border-radius") {
        std::vector<std::string> first;
        for (const std::string& w : words) { if (w == "/") break; first.push_back(w); }
        float r[4];
        if (FourSides(first, r, [&](const std::string& x, float& o) {
                Length l;
                if (!ParseLength(x, em, true, l) || l.value < 0) return false;
                o = l.unit == Length::Percent ? -l.value : l.value;   // a percentage is kept negative: the layout works it out
                return true;
            }))
            for (int k = 0; k < 4; ++k) s.radius[k] = r[k];
    } else if (p == "border-top-left-radius" || p == "border-top-right-radius" || p == "border-bottom-right-radius" || p == "border-bottom-left-radius") {
        Length l;
        const int k = p == "border-top-left-radius" ? 0 : p == "border-top-right-radius" ? 1 : p == "border-bottom-right-radius" ? 2 : 3;
        if (!words.empty() && ParseLength(words[0], em, true, l) && l.value >= 0) s.radius[k] = l.unit == Length::Percent ? -l.value : l.value;
    } else if (p == "border" || SideOf(p, "border") >= 0) {
        // shorthand: width, style and colour in any order
        float width = 3; int visible = 0; Color col; bool hasW = false, hasS = false, hasC = false, current = false, ok = !words.empty();
        for (const std::string& w : words) {
            const std::string lw = LowerAscii(w);
            float bw; int vis; Color c;
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
        int st[4];
        if (FourSides(words, st, [&](const std::string& x, int& o) { return BorderStyleWord(LowerAscii(x), o); }))
            for (int k = 0; k < 4; ++k) wk.borderStyle[k] = st[k];
    } else if (p == "border-color") {
        Color c[4];
        if (FourSides(words, c, [&](const std::string& x, Color& o) { return ParseColor(x, o); }))
            for (int k = 0; k < 4; ++k) { s.borderColor[k] = c[k]; wk.borderColorSet[k] = true; }
    } else if (p.compare(0, 7, "border-") == 0 && p != "border-spacing" && p != "border-collapse") {
        // border-top-width / -style / -color
        static const char* const sides[] = {"top", "right", "bottom", "left"};
        for (int k = 0; k < 4; ++k) {
            const std::string base = std::string("border-") + sides[k];
            if (p == base + "-width") { float w; if (BorderWidthWord(v, em, w)) wk.borderWidth[k] = w; }
            else if (p == base + "-style") { int vis; if (BorderStyleWord(v, vis)) wk.borderStyle[k] = vis; }
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
    } else if (p == "background" || p == "background-image") {
        if (p == "background") {
            s.background = Color{0, 0, 0, 0};
            s.backgroundImage.clear();
            s.bgGradient.reset();
            s.bgRepeatX = s.bgRepeatY = true;
            s.bgSize = ComputedStyle::BgAuto;
            s.bgPosX = s.bgPosY = Length::Px_(0);
        }
        if (v == "none") { s.backgroundImage.clear(); s.bgGradient.reset(); return; }
        if (p == "background-image") { s.backgroundImage.clear(); s.bgGradient.reset(); }
        std::vector<std::string> pos;
        bool afterSlash = false;
        for (const std::string& w : words) {
            const std::string lw = LowerAscii(w);
            Color c;
            if (w == ",") break;   // only the first layer
            if (w == "/") { afterSlash = true; continue; }
            if (lw.compare(0, 4, "url(") == 0) { s.backgroundImage = UrlOf(w); continue; }
            if (lw.find("gradient(") != std::string::npos) {
                // a gradient is drawn (Layout); one we cannot read is drawn as its first colour
                Gradient g;
                if (ParseGradient(w, em, g)) { s.bgGradient = std::make_shared<const Gradient>(std::move(g)); continue; }
                const std::size_t open = w.find('(');
                for (const std::string& gw : Words(w.substr(open + 1, w.size() - open - 2))) if (ParseColor(gw, c)) { s.background = c; break; }
                continue;
            }
            if (p == "background" && ParseColor(w, c)) { s.background = c; continue; }
            if (lw == "no-repeat") { s.bgRepeatX = s.bgRepeatY = false; continue; }
            if (lw == "repeat-x") { s.bgRepeatX = true; s.bgRepeatY = false; continue; }
            if (lw == "repeat-y") { s.bgRepeatX = false; s.bgRepeatY = true; continue; }
            if (lw == "repeat") continue;
            if (afterSlash && (lw == "cover" || lw == "contain")) { s.bgSize = lw == "cover" ? ComputedStyle::BgCover : ComputedStyle::BgContain; continue; }
            if (!afterSlash) pos.push_back(w);
        }
        if (!pos.empty()) BackgroundPos(pos, em, s.bgPosX, s.bgPosY);
    } else if (p == "background-size") {
        if (v == "cover") s.bgSize = ComputedStyle::BgCover;
        else if (v == "contain") s.bgSize = ComputedStyle::BgContain;
        else if (v == "auto" || v == "auto auto") s.bgSize = ComputedStyle::BgAuto;
        else if (!words.empty()) {
            Length a, b;
            if (lengthOrAuto(words[0], a) && (words.size() < 2 || lengthOrAuto(words[1], b))) { s.bgSize = ComputedStyle::BgExplicit; s.bgW = a; s.bgH = b; }
        }
    } else if (p == "background-repeat") {
        if (v == "no-repeat") s.bgRepeatX = s.bgRepeatY = false;
        else if (v == "repeat-x") { s.bgRepeatX = true; s.bgRepeatY = false; }
        else if (v == "repeat-y") { s.bgRepeatX = false; s.bgRepeatY = true; }
        else if (v == "repeat") s.bgRepeatX = s.bgRepeatY = true;
    } else if (p == "background-position") {
        BackgroundPos(words, em, s.bgPosX, s.bgPosY);
    } else if (p == "letter-spacing" || p == "word-spacing") {
        Length l;
        float& dst = p == "letter-spacing" ? s.letterSpacing : s.wordSpacing;
        if (v == "normal") dst = 0;
        else if (ParseLength(d.value, em, false, l) && l.unit == Length::Px) dst = std::max(-1000.0f, std::min(1000.0f, l.value));
    } else if (p == "text-shadow") {
        std::vector<BoxShadow> sh;
        if (ShadowList(d.value, em, sh)) { for (BoxShadow& b : sh) if (b.color == (Color{0, 0, 0, 80})) b.color = s.color; s.textShadows = sh; }
    } else if (p == "font-variant" || p == "font-variant-caps") {
        s.smallCaps = v.find("small-caps") != std::string::npos;
    } else if (p == "outline" || p == "outline-width" || p == "outline-style" || p == "outline-color") {
        // CSS UI 4: width, style and colour; drawn outside the border box, taking no room
        if (p == "outline") { s.outlineWidth = 3; s.outlineLine = 0; s.outlineColor = s.color; }
        for (const std::string& w : words) {
            const std::string lw = LowerAscii(w);
            float bw; int line; Color c;
            if ((p == "outline" || p == "outline-width") && BorderWidthWord(lw, em, bw)) s.outlineWidth = bw;
            else if ((p == "outline" || p == "outline-style") && (BorderStyleWord(lw, line) || (lw == "auto" && (line = 1)))) s.outlineLine = (std::uint8_t)line;
            else if ((p == "outline" || p == "outline-color") && ParseColor(w, c)) s.outlineColor = c;
            else if (lw == "currentcolor") s.outlineColor = s.color;
        }
        if (p == "outline" && s.outlineLine == 0) s.outlineWidth = 0;
    } else if (p == "outline-offset") {
        Length l;
        if (ParseLength(d.value, em, false, l) && l.unit == Length::Px) s.outlineOffset = std::max(-1000.0f, std::min(1000.0f, l.value));
    } else if (p == "aspect-ratio") {
        // "auto", "16 / 9", "1.5", "auto 4 / 3" (CSS Box Sizing 4)
        std::vector<float> nums;
        for (const std::string& w : words) { float n; std::string u; if (w != "/" && LowerAscii(w) != "auto" && ParseNumber(w, n, u) && u.empty() && n > 0) nums.push_back(n); }
        if (v == "auto") s.aspectRatio = 0;
        else if (nums.size() == 1) s.aspectRatio = std::min(nums[0], 10000.0f);
        else if (nums.size() == 2) s.aspectRatio = std::max(0.0001f, std::min(nums[0] / nums[1], 10000.0f));
    } else if (p == "filter") {
        // Filter Effects 1: grayscale(), brightness(), blur() (blur is read; the colours of the box are changed, blur is not drawn)
        s.filterGray = 0; s.filterBright = 1; s.filterBlur = 0;
        for (const std::string& w : words) {
            const std::string lw = LowerAscii(w);
            const std::size_t open = lw.find('(');
            if (open == std::string::npos || lw.back() != ')') continue;
            const std::string fn = lw.substr(0, open), arg = Trim(lw.substr(open + 1, lw.size() - open - 2));
            float n = 1; std::string u;
            if (!arg.empty() && !ParseNumber(arg, n, u)) continue;
            if (u == "%") n /= 100.0f;
            if (fn == "grayscale") s.filterGray = std::max(0.0f, std::min(1.0f, arg.empty() ? 1.0f : n));
            else if (fn == "brightness") s.filterBright = std::max(0.0f, std::min(10.0f, arg.empty() ? 1.0f : n));
            else if (fn == "blur") { Length l; if (ParseLength(arg, em, false, l)) s.filterBlur = std::max(0.0f, l.value); }
        }
    } else if (p == "box-shadow") {
        std::vector<BoxShadow> sh;
        if (ShadowList(d.value, em, sh)) s.shadows = sh;
    } else if (p == "mask-image" || p == "-webkit-mask-image" || p == "mask" || p == "-webkit-mask") {
        // CSS Masking 1, 6.1: a mask image that is not (yet) loaded counts as a transparent black layer, so the element is not
        // shown. We do not load mask images: an url() mask (an icon drawn as a coloured box through a picture's shape) hides the
        // element instead of painting a solid box; a gradient mask is not applied (the content is drawn unmasked).
        s.maskUrl = LowerAscii(v).find("url(") != std::string::npos;
    } else if (p == "opacity") {
        float n; std::string u;
        if (ParseNumber(v, n, u)) s.opacity = std::max(0.0f, std::min(1.0f, u == "%" ? n / 100.0f : n));
    } else if (p == "visibility") {
        if (v == "hidden" || v == "collapse") s.visible = false;
        else if (v == "visible") s.visible = true;
        else if (v == "inherit") s.visible = parent.visible;
    } else if (p == "font-weight") {
        if (v == "bold" || v == "bolder") { s.bold = true; s.weight = 700; }
        else if (v == "normal" || v == "lighter") { s.bold = false; s.weight = 400; }
        else if (v == "inherit") { s.bold = parent.bold; s.weight = parent.weight; }
        else { float n; std::string u; if (ParseNumber(v, n, u) && u.empty()) { s.weight = (int)n; s.bold = n >= 600; } }
    } else if (p == "font-style") {
        if (v == "italic" || v.compare(0, 7, "oblique") == 0) s.italic = true;
        else if (v == "normal") s.italic = false;
    } else if (p == "font-family") {
        if (v != "inherit") { s.mono = IsMonoFamily(v); s.family = v; }
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
                else { Length l; if (ParseLength(lw, s.fontSize, true, l)) { s.lineHeight = l.Resolve(s.fontSize); s.lineHeightFactor = -1; } }
                continue;
            }
            if (w == "/") { lhNext = true; continue; }
            if (!seenSize) {
                if (lw == "bold" || lw == "bolder") { s.bold = true; s.weight = 700; }
                else if (lw == "italic" || lw == "oblique") s.italic = true;
                else if (lw == "small-caps") s.smallCaps = true;
                else { float n; std::string u; if (ParseNumber(lw, n, u) && u.empty()) { s.bold = n >= 600; s.weight = (int)n; } else { float px; if (FontSizeValue(lw, parent.fontSize, px)) seenSize = true; } }
            } else family += lw + " ";
        }
        if (seenSize) { s.mono = IsMonoFamily(family); s.family = Trim(family); }
    } else if (p == "text-align") {
        if (v == "left" || v == "start") s.textAlign = TextAlign::Left;
        else if (v == "justify") s.textAlign = TextAlign::Justify;
        else if (v == "right" || v == "end") s.textAlign = TextAlign::Right;
        else if (v == "center" || v == "-webkit-center") s.textAlign = TextAlign::Center;
    } else if (p == "text-transform") {
        s.textTransform = v == "uppercase" ? TextTransform::Upper : v == "lowercase" ? TextTransform::Lower : v == "capitalize" ? TextTransform::Capitalize : TextTransform::None;
    } else if (p == "text-indent") {
        Length l;
        if (!words.empty() && ParseLength(words[0], em, true, l)) s.textIndent = l;
    } else if (p == "line-height") {
        float n; std::string u;
        if (v == "normal") { s.lineHeight = -1; s.lineHeightFactor = -1; }
        else if (ParseNumber(v, n, u) && u.empty() && n >= 0) { s.lineHeightFactor = std::min(n, 100.0f); s.lineHeight = -1; }
        else {
            Length l;
            if (ParseLength(v, em, true, l) && l.value >= 0) {
                s.lineHeight = std::min(l.Resolve(em), 20000.0f);
                s.lineHeightFactor = -1;
            }
        }
    } else if (p == "width" || p == "height") {
        Length l;
        if (v == "fit-content" || v == "max-content" || v == "min-content" || v == "-webkit-fill-available" || v == "stretch") l = Length();
        else if (!lengthOrAuto(d.value, l) || (l.unit != Length::Auto && l.value < 0 && l.unit != Length::Calc)) return;
        (p == "width" ? s.width : s.height) = l;
    } else if (p == "max-width" || p == "max-height") {
        Length l;
        Length& dst = p == "max-width" ? s.maxWidth : s.maxHeight;
        if (v == "none") { dst = Length(); dst.unit = Length::None; }
        else if (ParseLength(d.value, em, true, l) && (l.value >= 0 || l.unit == Length::Calc)) dst = l;
    } else if (p == "min-width" || p == "min-height") {
        Length l;
        const bool w = p == "min-width";
        if (v == "auto") { (w ? s.minWidth : s.minHeight) = Length::Px_(0); (w ? s.minWidthAuto : s.minHeightAuto) = true; }
        else if (ParseLength(d.value, em, true, l) && (l.value >= 0 || l.unit == Length::Calc)) { (w ? s.minWidth : s.minHeight) = l; (w ? s.minWidthAuto : s.minHeightAuto) = false; }
    } else if (p == "box-sizing") {
        s.borderBox = v == "border-box";
    } else if (p == "text-decoration" || p == "text-decoration-line" || p == "text-decoration-style" || p == "text-decoration-color") {
        // CSS Text Decoration 3: the lines, their style and colour, in any order (the shorthand resets what it leaves out)
        if (p == "text-decoration") { s.decoStyle = DecoStyle::Solid; s.decoColor = Color{0, 0, 0, 0}; }
        bool lineSeen = false;
        for (const std::string& wIn : words) {
            const std::string w = LowerAscii(wIn);
            if (p != "text-decoration-style" && p != "text-decoration-color") {
                if (w == "none") { s.underline = s.strike = s.overline = false; lineSeen = true; continue; }
                if (w == "underline" || w == "line-through" || w == "overline" || w == "blink") {
                    if (!lineSeen) { s.underline = s.strike = s.overline = false; lineSeen = true; }
                    if (w == "underline") s.underline = true; else if (w == "line-through") s.strike = true; else if (w == "overline") s.overline = true;
                    continue;
                }
            }
            if (p != "text-decoration-line" && p != "text-decoration-color") {
                if (w == "solid") { s.decoStyle = DecoStyle::Solid; continue; }
                if (w == "double") { s.decoStyle = DecoStyle::Double; continue; }
                if (w == "dotted") { s.decoStyle = DecoStyle::Dotted; continue; }
                if (w == "dashed") { s.decoStyle = DecoStyle::Dashed; continue; }
                if (w == "wavy") { s.decoStyle = DecoStyle::Wavy; continue; }
            }
            if (p != "text-decoration-line" && p != "text-decoration-style") {
                Color c;
                if (w == "currentcolor") s.decoColor = Color{0, 0, 0, 0};
                else if (ParseColor(wIn, c)) s.decoColor = c;
            }
        }
    } else if (p == "text-overflow") {
        s.ellipsis = !words.empty() && LowerAscii(words.back()) == "ellipsis";
    } else if (p == "object-fit") {
        s.objectFit = v == "contain" ? ObjectFit::Contain : v == "cover" ? ObjectFit::Cover : v == "none" ? ObjectFit::None
                    : v == "scale-down" ? ObjectFit::ScaleDown : ObjectFit::Fill;
    } else if (p == "object-position") {
        Length x, y;
        if (BackgroundPos(words, em, x, y)) { s.objectPosX = x; s.objectPosY = y; }
    } else if (p == "transform") {
        s.transform = v == "none" ? std::string() : Trim(d.value).substr(0, 2048);
    } else if (p == "transform-origin") {
        Length x, y;
        if (BackgroundPos(words, em, x, y)) { s.originX = x; s.originY = y; }
    } else if (p == "border-spacing") {
        Length a, b;
        if (!words.empty() && ParseLength(words[0], em, false, a) && a.value >= 0) {
            b = a;
            if (words.size() > 1 && !(ParseLength(words[1], em, false, b) && b.value >= 0)) return;
            s.borderSpacingH = std::min(a.value, 10000.0f); s.borderSpacingV = std::min(b.value, 10000.0f);
        }
    } else if (p == "border-collapse") {
        if (v == "collapse") s.borderCollapse = true; else if (v == "separate") s.borderCollapse = false;
    } else if (p == "table-layout") {
        s.tableFixed = v == "fixed";
    } else if (p == "caption-side") {
        s.captionBottom = v == "bottom";
    } else if (p == "counter-reset" || p == "counter-increment" || p == "counter-set") {
        // CSS 2.1 12.4: name [integer] pairs, or none
        std::vector<std::pair<std::string, int>> list;
        if (v != "none") {
            for (std::size_t k = 0; k < words.size() && list.size() < 32; ++k) {
                float n; std::string u;
                if (ParseNumber(words[k], n, u) && u.empty()) { if (!list.empty()) list.back().second = (int)std::max(-1e9f, std::min(1e9f, n)); continue; }
                list.push_back({words[k], p == "counter-increment" ? 1 : 0});
            }
        }
        (p == "counter-reset" ? s.counterReset : p == "counter-increment" ? s.counterIncrement : s.counterSet) = list;
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
    } else if (p == "vertical-align") {
        if (v == "inherit") { s.verticalAlign = parent.verticalAlign; return; }
        s.verticalAlign = v == "middle" ? VAlign::Middle : v == "top" ? VAlign::Top : v == "bottom" ? VAlign::Bottom
                        : v == "text-top" ? VAlign::TextTop : v == "text-bottom" ? VAlign::TextBottom : VAlign::Baseline;
        // sub / super / a length / a percentage of the line height move the baseline of the text inside (CSS 2.1 10.8.1). The
        // parent's font has no subscript metrics here: a subscript goes down a fifth of the parent's font size, a superscript up
        // a third (what common browsers draw when the font gives none).
        s.baselineShift = parent.baselineShift;
        if (v == "sub") s.baselineShift -= parent.fontSize * 0.2f;
        else if (v == "super") s.baselineShift += parent.fontSize * (1.0f / 3.0f);
        else if (!v.empty() && ((v[0] >= '0' && v[0] <= '9') || v[0] == '-' || v[0] == '.' || v[0] == '+')) {
            Length l;
            if (ParseLength(v, s.fontSize, true, l)) s.baselineShift += std::max(-1000.0f, std::min(1000.0f, l.Resolve(s.LineHeightPx())));
        }
    } else if (p == "float") {
        s.floating = v == "left" || v == "inline-start" ? Float::Left : v == "right" || v == "inline-end" ? Float::Right : Float::None;
    } else if (p == "clear") {
        s.clear = v == "left" ? Clear::Left : v == "right" ? Clear::Right : v == "both" ? Clear::Both : Clear::None;
    } else if (p == "position") {
        s.position = v == "relative" ? Position::Relative : v == "absolute" ? Position::Absolute : v == "fixed" ? Position::Fixed
                   : v == "sticky" || v == "-webkit-sticky" ? Position::Sticky : Position::Static;
    } else if (p == "top" || p == "right" || p == "bottom" || p == "left") {
        Length l;
        const int k = p == "top" ? 0 : p == "right" ? 1 : p == "bottom" ? 2 : 3;
        if (words.size() == 1 && lengthOrAuto(words[0], l)) s.inset[k] = l;
    } else if (p == "inset") {
        Length m[4];
        if (FourSides(words, m, lengthOrAuto)) for (int k = 0; k < 4; ++k) s.inset[k] = m[k];
    } else if (p == "z-index") {
        float n; std::string u;
        if (v == "auto") s.zAuto = true;
        else if (ParseNumber(v, n, u) && u.empty()) { s.zAuto = false; s.zIndex = (int)std::max(-1e6f, std::min(1e6f, n)); }
    } else if (p == "overflow" || p == "overflow-x" || p == "overflow-y") {
        auto word = [](const std::string& w, Overflow& o) {
            if (w == "visible") o = Overflow::Visible;
            else if (w == "hidden") o = Overflow::Hidden;
            else if (w == "clip") o = Overflow::Clip;
            else if (w == "scroll") o = Overflow::Scroll;
            else if (w == "auto" || w == "overlay") o = Overflow::Auto;
            else return false;
            return true;
        };
        Overflow a, b;
        if (p == "overflow") {
            if (words.size() >= 1 && word(LowerAscii(words[0]), a)) {
                b = a;
                if (words.size() == 2 && !word(LowerAscii(words[1]), b)) return;
                s.overflowX = a; s.overflowY = b;
            }
        } else if (word(v, a)) (p == "overflow-x" ? s.overflowX : s.overflowY) = a;
    } else if (p == "flex-direction") {
        s.flexDirection = v == "row-reverse" ? FlexDir::RowReverse : v == "column" ? FlexDir::Column : v == "column-reverse" ? FlexDir::ColumnReverse : FlexDir::Row;
    } else if (p == "flex-wrap") {
        s.flexWrap = v == "wrap" ? FlexWrap::Wrap : v == "wrap-reverse" ? FlexWrap::WrapReverse : FlexWrap::NoWrap;
    } else if (p == "flex-flow") {
        for (const std::string& w : words) {
            const std::string lw = LowerAscii(w);
            if (lw == "row" || lw == "row-reverse" || lw == "column" || lw == "column-reverse") Apply(s, wk, Declaration{"flex-direction", lw, false}, parent);
            else Apply(s, wk, Declaration{"flex-wrap", lw, false}, parent);
        }
    } else if (p == "flex") {
        // CSS Flexbox 7.2: none = 0 0 auto; auto = 1 1 auto; a number = n 1 0; else grow [shrink] [basis]
        if (v == "none") { s.flexGrow = 0; s.flexShrink = 0; s.flexBasis = Length(); }
        else if (v == "auto") { s.flexGrow = 1; s.flexShrink = 1; s.flexBasis = Length(); }
        else if (v == "initial") { s.flexGrow = 0; s.flexShrink = 1; s.flexBasis = Length(); }
        else {
            float nums[2]; int nn = 0;
            Length basis = Length::Px_(0);
            for (const std::string& w : words) {
                float n; std::string u;
                if (nn < 2 && ParseNumber(w, n, u) && u.empty() && n >= 0) { nums[nn++] = n; continue; }
                Length l;
                if (lengthOrAuto(w, l)) { basis = l; continue; }
                if (LowerAscii(w) == "content") { basis = Length(); continue; }
                return;
            }
            s.flexGrow = nn > 0 ? nums[0] : 1;
            s.flexShrink = nn > 1 ? nums[1] : 1;
            s.flexBasis = basis;
        }
    } else if (p == "flex-grow" || p == "flex-shrink") {
        float n; std::string u;
        if (ParseNumber(v, n, u) && u.empty() && n >= 0) (p == "flex-grow" ? s.flexGrow : s.flexShrink) = n;
    } else if (p == "flex-basis") {
        Length l;
        if (v == "content") s.flexBasis = Length();
        else if (lengthOrAuto(d.value, l)) s.flexBasis = l;
    } else if (p == "order") {
        float n; std::string u;
        if (ParseNumber(v, n, u) && u.empty()) s.order = (int)std::max(-1e6f, std::min(1e6f, n));
    } else if (p == "justify-content") { AlignWord(v, s.justifyContent); }
    else if (p == "align-items") { AlignWord(v, s.alignItems); }
    else if (p == "align-self") { AlignWord(v, s.alignSelf); }
    else if (p == "align-content") { AlignWord(v, s.alignContent); }
    else if (p == "justify-items") { AlignWord(v, s.justifyItems); }
    else if (p == "justify-self") { AlignWord(v, s.justifySelf); }
    else if (p == "place-items" || p == "place-self" || p == "place-content") {
        std::vector<std::string> w;
        for (const std::string& x : words) w.push_back(LowerAscii(x));
        if (w.empty()) return;
        Align a, b;
        if (!AlignWord(w[0], a)) return;
        b = a;
        if (w.size() > 1 && !AlignWord(w[1], b)) return;
        if (p == "place-items") { s.alignItems = a; s.justifyItems = b; }
        else if (p == "place-self") { s.alignSelf = a; s.justifySelf = b; }
        else { s.alignContent = a; s.justifyContent = b; }
    } else if (p == "gap" || p == "grid-gap" || p == "row-gap" || p == "column-gap" || p == "grid-row-gap" || p == "grid-column-gap") {
        auto gapLen = [&](const std::string& w, Length& out) {
            if (LowerAscii(w) == "normal") { out = Length::Px_(0); return true; }
            return ParseLength(w, em, true, out) && out.value >= 0;
        };
        Length a, b;
        if (words.empty() || !gapLen(words[0], a)) return;
        if (p == "gap" || p == "grid-gap") { b = a; if (words.size() > 1 && !gapLen(words[1], b)) return; s.rowGap = a; s.columnGap = b; }
        else if (p == "row-gap" || p == "grid-row-gap") s.rowGap = a;
        else s.columnGap = a;
    } else if (p == "grid-template-columns" || p == "grid-template-rows") {
        GridTemplate t;
        if (ParseTemplate(d.value, em, t)) (p == "grid-template-columns" ? s.gridColumns : s.gridRows) = t;
    } else if (p == "grid-template-areas") {
        s.gridAreas.clear();
        if (v == "none") return;
        for (const std::string& w : words) if (w.size() >= 2 && (w[0] == '"' || w[0] == '\'')) s.gridAreas.push_back(Unquote(w));
    } else if (p == "grid-template" || p == "grid") {
        // "rows / columns", or area strings with row sizes then "/ columns"
        const std::size_t slash = d.value.find('/');
        if (slash == std::string::npos) return;
        std::string rows = d.value.substr(0, slash);
        const std::string cols = d.value.substr(slash + 1);
        std::vector<std::string> areas;
        std::string rowSizes;
        for (const std::string& w : Words(rows)) {
            if (w.size() >= 2 && (w[0] == '"' || w[0] == '\'')) areas.push_back(Unquote(w));
            else rowSizes += w + " ";
        }
        GridTemplate r, c;
        if (!areas.empty()) {
            s.gridAreas = areas;
            if (Trim(rowSizes).empty()) { for (std::size_t k = 0; k < areas.size(); ++k) r.tracks.push_back(GridTrack()); r.lineNames.resize(r.tracks.size() + 1); }
            else if (!ParseTemplate(rowSizes, em, r)) return;
        } else if (!ParseTemplate(rows, em, r)) return;
        if (!ParseTemplate(cols, em, c)) return;
        s.gridRows = r; s.gridColumns = c;
    } else if (p == "grid-auto-rows" || p == "grid-auto-columns") {
        GridTrack t;
        if (!words.empty() && ParseTrack(words[0], em, t)) (p == "grid-auto-rows" ? s.gridAutoRows : s.gridAutoColumns) = t;
    } else if (p == "grid-auto-flow") {
        s.gridAutoFlowColumn = v.find("column") != std::string::npos;
        s.gridDense = v.find("dense") != std::string::npos;
    } else if (p == "grid-column") { ParseGridPair(d.value, s.gridColumnStart, s.gridColumnEnd); }
    else if (p == "grid-row") { ParseGridPair(d.value, s.gridRowStart, s.gridRowEnd); }
    else if (p == "grid-column-start") s.gridColumnStart = ParseGridLine(d.value);
    else if (p == "grid-column-end") s.gridColumnEnd = ParseGridLine(d.value);
    else if (p == "grid-row-start") s.gridRowStart = ParseGridLine(d.value);
    else if (p == "grid-row-end") s.gridRowEnd = ParseGridLine(d.value);
    else if (p == "grid-area") {
        std::vector<std::string> parts;
        std::size_t st = 0;
        for (std::size_t k = 0; k <= d.value.size(); ++k)
            if (k == d.value.size() || d.value[k] == '/') { parts.push_back(Trim(d.value.substr(st, k - st))); st = k + 1; }
        if (parts.size() == 1) {
            const GridLine g = ParseGridLine(parts[0]);
            if (!g.name.empty()) { s.gridRowStart = s.gridColumnStart = s.gridRowEnd = s.gridColumnEnd = g; }
            else { s.gridRowStart = g; }
            return;
        }
        if (parts.size() >= 1) s.gridRowStart = ParseGridLine(parts[0]);
        if (parts.size() >= 2) s.gridColumnStart = ParseGridLine(parts[1]);
        if (parts.size() >= 3) s.gridRowEnd = ParseGridLine(parts[2]);
        if (parts.size() >= 4) s.gridColumnEnd = ParseGridLine(parts[3]);
    } else if (p == "content") {
        // ::before / ::after: strings, attr(name), and the quote words; "none" / "normal" mean no box
        if (v == "none" || v == "normal") { s.hasContent = false; return; }
        std::string out;
        for (const std::string& w : words) {
            if (w.size() >= 2 && (w[0] == '"' || w[0] == '\'')) out += Unquote(w);
            else if (LowerAscii(w) == "open-quote") out += "\xE2\x80\x9C";
            else if (LowerAscii(w) == "close-quote") out += "\xE2\x80\x9D";
            else if (LowerAscii(w).compare(0, 5, "attr(") == 0) out += "\x01" + LowerAscii(Trim(w.substr(5, w.size() - 6))) + "\x02";
            else if (LowerAscii(w).compare(0, 4, "url(") == 0) {}   // an image as content: not drawn
            else if (LowerAscii(w).compare(0, 8, "counter(") == 0 || LowerAscii(w).compare(0, 9, "counters(") == 0) {
                // kept as markers; the layout fills in the values (CSS 2.1 12.2, 12.4)
                const bool many = LowerAscii(w).compare(0, 9, "counters(") == 0;
                const std::size_t open = w.find('(');
                const std::vector<std::vector<std::string>> args = CommaGroups(Words(w.substr(open + 1, w.size() - open - 2)));
                if (args.empty() || args[0].size() != 1) continue;
                std::string sep, style = "decimal";
                std::size_t k = 1;
                if (many) { if (args.size() < 2 || args[1].size() != 1) continue; sep = Unquote(args[1][0]); k = 2; }
                if (args.size() > k && args[k].size() == 1) style = LowerAscii(args[k][0]);
                if (many) out += "\x05" + args[0][0] + "\x1f" + sep + "\x1f" + style + "\x06";
                else out += "\x03" + args[0][0] + "\x1f" + style + "\x04";
            }
        }
        s.hasContent = true;
        s.content = out;
    }
}

// A dimension attribute ("120", "50%", "120px") as CSS text, or "" (the HTML standard, 2.3.4.4 "rules for parsing dimension values").
std::string DimensionAttr(const Node& el, const char* name) {
    const std::string* v = el.Attr(name);
    if (!v) return std::string();
    std::size_t i = 0;
    while (i < v->size() && IsWs((*v)[i])) ++i;
    std::size_t j = i;
    while (j < v->size() && (*v)[j] >= '0' && (*v)[j] <= '9' && j - i < 7) ++j;
    if (j == i) return std::string();
    std::string num = v->substr(i, j - i);
    if (j < v->size() && (*v)[j] == '.') { ++j; while (j < v->size() && (*v)[j] >= '0' && (*v)[j] <= '9') ++j; }
    const bool pct = j < v->size() && (*v)[j] == '%';
    return num + (pct ? "%" : "px");
}
bool SafeHintWord(const std::string& v) {
    if (v.empty() || v.size() > 40) return false;
    for (char c : v) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '#')) return false;
    return true;
}
// The HTML standard's "rules for parsing a legacy font size" (15.3.4, the <font size> attribute): "3", "+1", "-2" ... -> the font-size
// keyword it stands for, or "" when the value has no digits.
std::string LegacyFontSize(const std::string& v) {
    std::size_t i = 0;
    while (i < v.size() && IsWs(v[i])) ++i;
    int mode = 0;   // 0 absolute, 1 relative-plus, -1 relative-minus
    if (i < v.size() && (v[i] == '+' || v[i] == '-')) { mode = v[i] == '+' ? 1 : -1; ++i; }
    std::size_t j = i;
    int value = 0;
    while (j < v.size() && v[j] >= '0' && v[j] <= '9') { if (value < 100) value = value * 10 + (v[j] - '0'); ++j; }
    if (j == i) return std::string();
    if (mode != 0) value = 3 + mode * value;
    if (value < 1) value = 1;
    if (value > 7) value = 7;
    static const char* const kSizes[] = {"x-small", "small", "medium", "large", "x-large", "xx-large", "xxx-large"};
    return kSizes[value - 1];
}
// A <font face> list kept to the characters a family list needs (letters, digits, space, comma, hyphen, underscore), so the
// attribute cannot add other declarations.
std::string SafeFaceList(const std::string& v) {
    if (v.empty() || v.size() > 200) return std::string();
    for (char c : v)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == ',' || c == '-' || c == '_'))
            return std::string();
    return v;
}

// The CSS that an element's presentational attributes stand for (the HTML standard, 15.3.2 - 15.3.10, the parts the pages we
// meet still use: table border / cellspacing / cellpadding / width / bgcolor / align, cell width / height / nowrap / align /
// valign, body bgcolor / text / margins, <font> color / face / size, align on blocks, <img> align / hspace / vspace / border,
// <hr> width / size / color / noshade / align).
std::string PresentationalHints(const Node& el) {
    const std::string& n = el.name;
    std::string css;
    auto colorAttr = [&](const char* attr, const char* prop) {
        if (const std::string* v = el.Attr(attr)) { Color c; if (SafeHintWord(*v) && ParseColor(*v, c)) css += std::string(prop) + ":" + *v + ";"; }
    };
    auto lowerAttr = [&](const char* attr) { const std::string* a = el.Attr(attr); return a ? LowerAscii(Trim(*a)) : std::string(); };
    if (n == "font") {   // 15.3.4
        colorAttr("color", "color");
        if (const std::string* f = el.Attr("face")) { const std::string safe = SafeFaceList(*f); if (!safe.empty()) css += "font-family:" + safe + ";"; }
        if (const std::string* sz = el.Attr("size")) { const std::string k = LegacyFontSize(*sz); if (!k.empty()) css += "font-size:" + k + ";"; }
    }
    if (n == "div" || n == "p" || n == "h1" || n == "h2" || n == "h3" || n == "h4" || n == "h5" || n == "h6" || n == "caption") {   // 15.3.3
        const std::string la = lowerAttr("align");
        if (la == "left" || la == "right" || la == "center" || la == "middle")
            css += "text-align:-webkit-" + std::string(la == "middle" ? "center" : la) + ";";
        else if (la == "justify") css += "text-align:justify;";
    }
    if (n == "body") {   // 15.3.2: the body's margins
        const std::string top = !DimensionAttr(el, "marginheight").empty() ? DimensionAttr(el, "marginheight") : DimensionAttr(el, "topmargin");
        const std::string bottom = !DimensionAttr(el, "marginheight").empty() ? DimensionAttr(el, "marginheight") : DimensionAttr(el, "bottommargin");
        const std::string left = !DimensionAttr(el, "marginwidth").empty() ? DimensionAttr(el, "marginwidth") : DimensionAttr(el, "leftmargin");
        const std::string right = !DimensionAttr(el, "marginwidth").empty() ? DimensionAttr(el, "marginwidth") : DimensionAttr(el, "rightmargin");
        if (!top.empty()) css += "margin-top:" + top + ";";
        if (!bottom.empty()) css += "margin-bottom:" + bottom + ";";
        if (!left.empty()) css += "margin-left:" + left + ";";
        if (!right.empty()) css += "margin-right:" + right + ";";
    }
    if (n == "img" || n == "object" || n == "embed" || n == "iframe") {   // 15.4.3 (align) and 15.4.2 (hspace / vspace), 15.4.4 (border)
        const std::string la = lowerAttr("align");
        if (la == "left" || la == "right") css += "float:" + la + ";";
        else if (la == "top" || la == "middle" || la == "bottom") css += std::string("vertical-align:") + (la == "bottom" ? "baseline" : la) + ";";
        const std::string hs = DimensionAttr(el, "hspace"), vs = DimensionAttr(el, "vspace");
        if (!hs.empty()) css += "margin-left:" + hs + ";margin-right:" + hs + ";";
        if (!vs.empty()) css += "margin-top:" + vs + ";margin-bottom:" + vs + ";";
        if (n == "img" || n == "object") {
            const std::string b = DimensionAttr(el, "border");
            if (!b.empty() && b.back() != '%') css += "border:" + b + " solid;";
        }
    }
    if (n == "hr") {   // 15.3.10
        const std::string w = DimensionAttr(el, "width");
        if (!w.empty()) css += "width:" + w + ";";
        const std::string la = lowerAttr("align");
        if (la == "left") css += "margin-left:0;margin-right:auto;";
        else if (la == "right") css += "margin-left:auto;margin-right:0;";
        const std::string* cv = el.Attr("color");
        Color c;
        const bool colored = cv && SafeHintWord(*cv) && ParseColor(*cv, c);
        if (colored) css += "border-style:solid;border-color:" + *cv + ";background-color:" + *cv + ";";
        if (el.Attr("noshade")) css += std::string("border-style:solid;") + (colored ? "" : "border-color:gray;background-color:gray;");
        const std::string sz = DimensionAttr(el, "size");
        if (!sz.empty() && sz.back() != '%') {
            const float px = std::strtof(sz.c_str(), nullptr);
            if (px <= 1.0f) css += "border-bottom-width:0;";
            else css += "height:" + std::to_string((int)std::min(px - 2.0f, 1000.0f)) + "px;";
        }
    }
    if (n == "table" || n == "td" || n == "th" || n == "tr" || n == "body" || n == "thead" || n == "tbody" || n == "tfoot") colorAttr("bgcolor", "background-color");
    if (n == "body") colorAttr("text", "color");
    if (n == "table") {
        const std::string w = DimensionAttr(el, "width");
        if (!w.empty()) css += "width:" + w + ";";
        const std::string b = DimensionAttr(el, "border");
        if (el.Attr("border") && (b.empty() || b != "0px")) css += "border:" + (b.empty() || b.back() == '%' ? std::string("1px") : b) + " outset gray;";
        const std::string sp = DimensionAttr(el, "cellspacing");
        if (!sp.empty() && sp.back() != '%') css += "border-spacing:" + sp + ";";
        if (const std::string* a = el.Attr("align")) {
            const std::string la = LowerAscii(Trim(*a));
            if (la == "center") css += "margin-left:auto;margin-right:auto;";
            else if (la == "left" || la == "right") css += "float:" + la + ";";
        }
    }
    if (n == "td" || n == "th") {
        const std::string w = DimensionAttr(el, "width"), h = DimensionAttr(el, "height");
        if (!w.empty() && w != "0px") css += "width:" + w + ";";
        if (!h.empty() && h != "0px") css += "height:" + h + ";";
        if (el.Attr("nowrap")) css += "white-space:nowrap;";
        const Node* table = el.parent;
        for (int guard = 0; table && guard < 4 && !table->IsElement("table"); ++guard) table = table->parent;
        if (table && table->IsElement("table")) {
            const std::string pad = DimensionAttr(*table, "cellpadding");
            if (!pad.empty() && pad.back() != '%') css += "padding:" + pad + ";";
            const std::string b = DimensionAttr(*table, "border");
            if (table->Attr("border") && b != "0px") css += "border:1px inset gray;";
        }
    }
    if (n == "td" || n == "th" || n == "tr") {
        if (const std::string* a = el.Attr("align")) {
            const std::string la = LowerAscii(Trim(*a));
            if (la == "left" || la == "right" || la == "center" || la == "justify") css += "text-align:" + la + ";";
        }
        if (const std::string* a = el.Attr("valign")) {
            const std::string la = LowerAscii(Trim(*a));
            if (la == "top" || la == "middle" || la == "bottom" || la == "baseline") css += "vertical-align:" + la + ";";
        }
    }
    return css;
}

// The CSS-wide keywords (CSS Cascade 4, 7.3): inherit, initial, unset and revert (revert is taken as unset: the default sheet's
// value is not kept apart). The property's value is copied from the parent (inherit) or from the initial style (initial). True
// when the declaration was one of them (handled), false otherwise.
bool InheritedProperty(const std::string& p) {
    static const char* const inh[] = {"color", "font", "font-size", "font-weight", "font-style", "font-family", "line-height", "text-align",
                                      "text-indent", "text-transform", "white-space", "list-style", "list-style-type", "visibility",
                                      "border-spacing", "border-collapse", "caption-side", "letter-spacing", "word-spacing",
                                      "text-shadow", "font-variant"};
    for (const char* k : inh) if (p == k) return true;
    return false;
}
void CopyProperty(ComputedStyle& s, Work& wk, const std::string& p, const ComputedStyle& from) {
    auto sides = [&](const std::string& base) {   // which sides a margin / padding / border property names
        if (p == base) return 15;
        const int k = SideOf(p, base);
        if (k >= 0) return 1 << k;
        if (p == base + "-inline") return (1 << 1) | (1 << 3);
        if (p == base + "-block") return (1 << 0) | (1 << 2);
        return 0;
    };
    if (int m = sides("margin")) { for (int k = 0; k < 4; ++k) if (m & (1 << k)) s.margin[k] = from.margin[k]; return; }
    if (int m = sides("padding")) { for (int k = 0; k < 4; ++k) if (m & (1 << k)) s.padding[k] = from.padding[k]; return; }
    if (p.compare(0, 6, "border") == 0 && p.find("radius") == std::string::npos && p != "border-spacing" && p != "border-collapse") {
        int m = sides("border");
        if (!m) for (int k = 0; k < 4; ++k) {
            static const char* const nm[] = {"top", "right", "bottom", "left"};
            if (p.compare(0, 7 + std::strlen(nm[k]), std::string("border-") + nm[k]) == 0) m |= 1 << k;
        }
        if (!m && (p == "border-width" || p == "border-style" || p == "border-color")) m = 15;
        for (int k = 0; k < 4; ++k) if (m & (1 << k)) {
            if (p.find("color") == std::string::npos) { wk.borderWidth[k] = from.border[k]; wk.borderStyle[k] = from.border[k] > 0 ? std::max(1, (int)from.borderLine[k]) : 0; }
            if (p.find("width") == std::string::npos && p.find("style") == std::string::npos) { s.borderColor[k] = from.borderColor[k]; wk.borderColorSet[k] = true; }
        }
        return;
    }
    if (p.find("radius") != std::string::npos) { for (int k = 0; k < 4; ++k) s.radius[k] = from.radius[k]; return; }
    if (p.compare(0, 10, "background") == 0) {
        s.background = from.background; s.backgroundImage = from.backgroundImage; s.bgGradient = from.bgGradient;
        s.bgSize = from.bgSize; s.bgW = from.bgW; s.bgH = from.bgH; s.bgRepeatX = from.bgRepeatX; s.bgRepeatY = from.bgRepeatY;
        s.bgPosX = from.bgPosX; s.bgPosY = from.bgPosY;
        return;
    }
    if (p == "color") { s.color = from.color; return; }
    if (p == "font" || p.compare(0, 5, "font-") == 0) {
        if (p == "font" || p == "font-weight") { s.bold = from.bold; s.weight = from.weight; }
        if (p == "font" || p == "font-style") s.italic = from.italic;
        if (p == "font" || p == "font-family") { s.family = from.family; s.mono = from.mono; }
        if (p == "font" || p == "font-variant") s.smallCaps = from.smallCaps;
        if (p == "font") { s.lineHeight = from.lineHeight; s.lineHeightFactor = from.lineHeightFactor; }
        return;
    }
    if (p == "line-height") { s.lineHeight = from.lineHeight; s.lineHeightFactor = from.lineHeightFactor; return; }
    if (p == "display") { s.display = from.display; return; }
    if (p == "width" || p == "inline-size") { s.width = from.width; return; }
    if (p == "height" || p == "block-size") { s.height = from.height; return; }
    if (p == "max-width") { s.maxWidth = from.maxWidth; return; }
    if (p == "max-height") { s.maxHeight = from.maxHeight; return; }
    if (p == "min-width") { s.minWidth = from.minWidth; s.minWidthAuto = from.minWidthAuto; return; }
    if (p == "min-height") { s.minHeight = from.minHeight; s.minHeightAuto = from.minHeightAuto; return; }
    if (p == "text-align") { s.textAlign = from.textAlign; return; }
    if (p == "text-indent") { s.textIndent = from.textIndent; return; }
    if (p == "text-transform") { s.textTransform = from.textTransform; return; }
    if (p.compare(0, 15, "text-decoration") == 0) { s.underline = from.underline; s.strike = from.strike; s.overline = from.overline; s.decoStyle = from.decoStyle; s.decoColor = from.decoColor; return; }
    if (p == "white-space") { s.whiteSpace = from.whiteSpace; return; }
    if (p == "list-style" || p == "list-style-type") { s.listStyle = from.listStyle; return; }
    if (p == "visibility") { s.visible = from.visible; return; }
    if (p == "opacity") { s.opacity = from.opacity; return; }
    if (p == "mask-image" || p == "-webkit-mask-image" || p == "mask" || p == "-webkit-mask") { s.maskUrl = from.maskUrl; return; }
    if (p == "position") { s.position = from.position; return; }
    if (p == "float") { s.floating = from.floating; return; }
    if (p == "clear") { s.clear = from.clear; return; }
    if (p == "top" || p == "right" || p == "bottom" || p == "left" || p == "inset") {
        const int k = p == "top" ? 0 : p == "right" ? 1 : p == "bottom" ? 2 : p == "left" ? 3 : -1;
        for (int j = 0; j < 4; ++j) if (k < 0 || k == j) s.inset[j] = from.inset[j];
        return;
    }
    if (p == "z-index") { s.zAuto = from.zAuto; s.zIndex = from.zIndex; return; }
    if (p.compare(0, 8, "overflow") == 0) { s.overflowX = from.overflowX; s.overflowY = from.overflowY; return; }
    if (p == "box-shadow") { s.shadows = from.shadows; return; }
    if (p == "text-shadow") { s.textShadows = from.textShadows; return; }
    if (p == "letter-spacing") { s.letterSpacing = from.letterSpacing; return; }
    if (p == "word-spacing") { s.wordSpacing = from.wordSpacing; return; }
    if (p == "vertical-align") { s.verticalAlign = from.verticalAlign; return; }
    if (p == "box-sizing") { s.borderBox = from.borderBox; return; }
    if (p == "border-spacing") { s.borderSpacingH = from.borderSpacingH; s.borderSpacingV = from.borderSpacingV; return; }
    if (p == "border-collapse") { s.borderCollapse = from.borderCollapse; return; }
    if (p == "outline") { s.outlineWidth = from.outlineWidth; s.outlineColor = from.outlineColor; s.outlineOffset = from.outlineOffset; return; }
    if (p == "transform") { s.transform = from.transform; return; }
}
bool ApplyKeyword(ComputedStyle& s, Work& wk, const Declaration& d, const ComputedStyle& parent) {
    const std::string v = LowerAscii(Trim(d.value));
    if (v != "inherit" && v != "initial" && v != "unset" && v != "revert" && v != "revert-layer") return false;
    const bool inherit = v == "inherit" || ((v == "unset" || v == "revert" || v == "revert-layer") && InheritedProperty(d.property));
    if (inherit) { CopyProperty(s, wk, d.property, parent); return true; }
    static const ComputedStyle initial = InheritedStyle(ComputedStyle());
    if (d.property == "color") { s.color = Color{0, 0, 0, 255}; return true; }
    if (d.property.compare(0, 6, "border") == 0 && d.property.find("radius") == std::string::npos && d.property.find("color") == std::string::npos &&
        d.property != "border-spacing" && d.property != "border-collapse") {
        // initial border: medium none (no line)
        ComputedStyle none = initial;
        for (int k = 0; k < 4; ++k) none.border[k] = 0;
        CopyProperty(s, wk, d.property, none);
        for (int k = 0; k < 4; ++k) if (wk.borderWidth[k] == 0 && wk.borderStyle[k] == 0) wk.borderWidth[k] = 3;
        return true;
    }
    CopyProperty(s, wk, d.property, initial);
    if (d.property == "display") s.display = Display::Inline;
    return true;
}

// var(--name, fallback) replaced by the custom property's value (CSS Custom Properties 1, 3). `budget` caps the text a value may
// grow to, so a page cannot make one value explode (each var() may refer to others).
bool SubstituteVars(const std::string& in, const CustomProps* vars, std::string& out, int depth, std::size_t& budget) {
    out.clear();
    std::size_t i = 0;
    while (i < in.size()) {
        const std::size_t at = in.find("var(", i);
        if (at == std::string::npos || (at > 0 && IsIdentChar(in[at - 1]))) {
            if (at == std::string::npos) { out.append(in, i, std::string::npos); break; }
            out.append(in, i, at + 4 - i);
            i = at + 4;
            continue;
        }
        out.append(in, i, at - i);
        const std::size_t close = CloseParen(in, at + 3);
        if (close == std::string::npos) return false;
        const std::string inner = in.substr(at + 4, close - at - 4);
        const std::size_t comma = inner.find(',');
        const std::string name = Trim(inner.substr(0, comma));
        const std::string fallback = comma == std::string::npos ? std::string() : Trim(inner.substr(comma + 1));
        const std::string* found = nullptr;
        if (vars) for (std::size_t k = vars->size(); k-- > 0;) if ((*vars)[k].first == name) { found = &(*vars)[k].second; break; }
        std::string repl;
        if (found && !found->empty()) repl = *found;
        else if (comma != std::string::npos) repl = fallback;
        else return false;   // no value and no fallback: the declaration is invalid
        if (repl.find("var(") != std::string::npos) {
            if (depth >= 16) return false;
            std::string sub;
            if (!SubstituteVars(repl, vars, sub, depth + 1, budget)) return false;
            repl.swap(sub);
        }
        if (repl.size() > budget) return false;
        budget -= repl.size();
        out += repl;
        i = close + 1;
    }
    return true;
}

} // namespace

ComputedStyle InheritedStyle(const ComputedStyle& parent) {
    ComputedStyle s;
    // inherited properties
    s.color = parent.color;
    s.fontSize = parent.fontSize;
    s.bold = parent.bold; s.italic = parent.italic; s.mono = parent.mono; s.weight = parent.weight;
    s.family = parent.family;
    s.textAlign = parent.textAlign;
    s.textTransform = parent.textTransform;
    s.textIndent = parent.textIndent;
    s.lineHeight = parent.lineHeight; s.lineHeightFactor = parent.lineHeightFactor;
    s.whiteSpace = parent.whiteSpace;
    s.listStyle = parent.listStyle;
    s.visible = parent.visible;
    s.underline = parent.underline; s.strike = parent.strike; s.overline = parent.overline;   // decorations reach the text inside (simplified)
    s.decoStyle = parent.decoStyle; s.decoColor = parent.decoColor;
    s.smallCaps = parent.smallCaps;
    s.letterSpacing = parent.letterSpacing; s.wordSpacing = parent.wordSpacing;
    s.textShadows = parent.textShadows;
    s.borderSpacingH = parent.borderSpacingH; s.borderSpacingV = parent.borderSpacingV;
    s.borderCollapse = parent.borderCollapse;
    s.captionBottom = parent.captionBottom;
    s.baselineShift = parent.baselineShift;   // reset below for anything that is not inline (ComputeStyle)
    s.vars = parent.vars;
    // the rest start at their initial values
    for (int k = 0; k < 4; ++k) { s.margin[k] = Length::Px_(0); s.padding[k] = Length::Px_(0); }
    s.maxWidth.unit = Length::None;
    s.maxHeight.unit = Length::None;
    s.minWidth = Length::Px_(0); s.minHeight = Length::Px_(0);
    s.bgPosX = s.bgPosY = Length::Px_(0);
    s.rowGap = s.columnGap = Length::Px_(0);
    s.objectPosX.unit = s.objectPosY.unit = s.originX.unit = s.originY.unit = Length::Percent;
    s.objectPosX.value = s.objectPosY.value = s.originX.value = s.originY.value = 50.0f;
    return s;
}

ComputedStyle ComputeStyle(const Node& el, const ComputedStyle& parent, const std::vector<const StyleSheet*>& author,
                           const StyleContext* ctx, int pseudo) {
    ComputedStyle s = InheritedStyle(parent);
    if (el.kind != NodeKind::Element) return s;

    // the units of this element's lengths
    const Units saved = g_units;
    if (ctx) {
        g_units.rem = ctx->rootFontSize;
        g_units.vw = ctx->media.width / 100.0f;
        g_units.vh = ctx->media.height / 100.0f;
    }
    struct Restore { Units u; ~Restore() { g_units = u; } } restore{saved};

    // Gather the declarations that apply: (bucket, specificity, order).
    std::vector<Cascaded> list;
    std::size_t order = 0;
    auto collect = [&](const StyleSheet& sheet, int normalBucket, int importantBucket) {
        for (const Rule& r : sheet.rules) {
            bool mediaOk = true;
            for (int mi : r.media) if (mi >= 0 && (std::size_t)mi < sheet.media.size() && !MediaMatches(sheet.media[(std::size_t)mi], ctx ? ctx->media : MediaEnv())) mediaOk = false;
            if (!mediaOk) { order += r.declarations.size(); continue; }
            int best = -1;
            for (const Selector& sel : r.selectors) if (sel.specificity > best && Matches(sel, el, ctx, pseudo)) best = sel.specificity;
            if (best < 0) { order += r.declarations.size(); continue; }
            for (const Declaration& d : r.declarations) list.push_back({d.important ? importantBucket : normalBucket, best, order++, &d});
        }
    };
    collect(DefaultStyleSheet(), 0, 0);
    // Rules the default sheet does not say with selectors: the link look a[href] (pseudo-elements excepted).
    static const std::vector<Declaration> linkDecls = ParseDeclarations("color: #0000ee; text-decoration: underline");
    if (pseudo == 0 && el.name == "a" && el.Attr("href")) for (const Declaration& d : linkDecls) list.push_back({0, 11, order++, &d});
    // HTML presentational hints (the HTML standard, 15.3: tables, cells, bgcolor ...): author level, before every author rule
    std::vector<Declaration> hintDecls;
    if (pseudo == 0) {
        const std::string hints = PresentationalHints(el);
        if (!hints.empty()) {
            hintDecls = ParseDeclarations(hints);
            for (const Declaration& d : hintDecls) list.push_back({1, -1, order++, &d});
        }
    }
    for (const StyleSheet* sh : author) if (sh) collect(*sh, 1, 3);
    std::vector<Declaration> inlineDecls;
    if (pseudo == 0) if (const std::string* st = el.Attr("style")) {
        inlineDecls = ParseDeclarations(*st);
        for (const Declaration& d : inlineDecls) list.push_back({d.important ? 4 : 2, 0, order++, &d});
    }
    std::stable_sort(list.begin(), list.end(), [](const Cascaded& a, const Cascaded& b) {
        if (a.bucket != b.bucket) return a.bucket < b.bucket;
        if (a.specificity != b.specificity) return a.specificity < b.specificity;
        return a.order < b.order;
    });

    // Custom properties first (they are inherited and other values refer to them).
    std::shared_ptr<CustomProps> vars;
    for (const Cascaded& c : list) {
        if (c.decl->property.compare(0, 2, "--") != 0) continue;
        if (!vars) vars = std::make_shared<CustomProps>(parent.vars ? *parent.vars : CustomProps());
        if (vars->size() >= 2000) break;
        std::string value = c.decl->value;
        if (value.size() > 4096) value.resize(4096);
        bool replaced = false;
        for (auto& kv : *vars) if (kv.first == c.decl->property) { kv.second = value; replaced = true; break; }
        if (!replaced) vars->push_back({c.decl->property, value});
    }
    if (vars) {
        // a custom property that uses var() is worked out now, against the ones before it
        for (auto& kv : *vars) {
            if (kv.second.find("var(") == std::string::npos) continue;
            std::string out;
            std::size_t budget = 8192;
            if (SubstituteVars(kv.second, vars.get(), out, 1, budget)) kv.second = out; else kv.second.clear();
        }
        s.vars = vars;
    }
    // Declarations that use var(): their substituted copies.
    std::vector<Declaration> substituted;
    substituted.reserve(list.size());
    for (Cascaded& c : list) {
        if (c.decl->property.compare(0, 2, "--") == 0 || c.decl->value.find("var(") == std::string::npos) continue;
        std::string out;
        std::size_t budget = 16384;
        Declaration d = *c.decl;
        if (SubstituteVars(d.value, s.vars.get(), out, 0, budget)) d.value = Trim(out);
        else d.value.clear();   // invalid at computed-value time: as if not set
        substituted.push_back(d);
    }
    if (!substituted.empty()) {
        std::size_t k = 0;
        for (Cascaded& c : list)
            if (c.decl->property.compare(0, 2, "--") != 0 && c.decl->value.find("var(") != std::string::npos) c.decl = &substituted[k++];
    }

    // Pass 1: the font size (em values of everything else depend on it). Pass 2: the rest.
    for (const Cascaded& c : list) if (!c.decl->value.empty()) ApplyFontSize(s, *c.decl, parent);
    Work wk;
    for (const Cascaded& c : list) {
        if (c.decl->property == "font-size" || c.decl->value.empty() || c.decl->property.compare(0, 2, "--") == 0) continue;
        if (ApplyKeyword(s, wk, *c.decl, parent)) continue;
        Apply(s, wk, *c.decl, parent);
    }
    for (int k = 0; k < 4; ++k) {
        s.border[k] = wk.borderStyle[k] ? std::min(wk.borderWidth[k], 1000.0f) : 0.0f;
        s.borderLine[k] = (std::uint8_t)wk.borderStyle[k];
        if (!wk.borderColorSet[k]) s.borderColor[k] = s.color;   // currentColor
    }
    // CSS 2.1 9.7: a float or an absolutely positioned box is block-level
    if (s.floating != Float::None || s.OutOfFlow()) {
        if (s.display == Display::Inline || s.display == Display::InlineBlock) s.display = Display::Block;
        else if (s.display == Display::InlineFlex) s.display = Display::Flex;
        else if (s.display == Display::InlineGrid) s.display = Display::Grid;
        else if (s.display == Display::TableRow || s.display == Display::TableCell || IsTableSection(s.display) || s.display == Display::TableCaption) s.display = Display::Block;
    }
    if (s.OutOfFlow()) s.floating = Float::None;
    if (s.display != Display::Inline) s.baselineShift = 0.0f;   // a new line box: the text inside starts on its own baseline
    // generated content: attr() filled in from the element
    if (pseudo != 0 && s.hasContent) {
        std::string out;
        for (std::size_t i = 0; i < s.content.size();) {
            if (s.content[i] == '\x01') {
                const std::size_t e = s.content.find('\x02', i);
                if (e == std::string::npos) break;
                if (const std::string* a = el.Attr(s.content.substr(i + 1, e - i - 1).c_str())) out += *a;
                i = e + 1;
            } else out += s.content[i++];
        }
        s.content = out;
    }
    return s;
}

// ---- gradients (CSS Images 3, section 3) ---------------------------------------------------------------------------------

namespace {

// An angle in degrees: deg, grad, rad, turn (CSS Values 4, 7.1); a bare 0 is allowed.
bool ParseAngle(const std::string& w, float& deg) {
    float n; std::string u;
    if (!ParseNumber(w, n, u)) return false;
    if (u == "deg") deg = n;
    else if (u == "grad") deg = n * 0.9f;
    else if (u == "rad") deg = n * 57.29577951f;
    else if (u == "turn") deg = n * 360.0f;
    else if (u.empty() && n == 0.0f) deg = 0.0f;
    else return false;
    return std::isfinite(deg);
}

// A position ("center", "left top", "30% 70%", "10px bottom") into x and y (CSS Values 4, <position>, the 1- and 2-value forms).
bool ParsePosition(const std::vector<std::string>& w, float em, Length& x, Length& y) {
    Length lx, ly;
    lx.unit = ly.unit = Length::Percent; lx.value = ly.value = 50.0f;
    if (w.empty() || w.size() > 2) return false;
    int xs = 0, ys = 0;
    for (std::size_t k = 0; k < w.size(); ++k) {
        const std::string lw = LowerAscii(w[k]);
        Length l;
        if (lw == "left" || lw == "right") { l.unit = Length::Percent; l.value = lw == "left" ? 0.0f : 100.0f; lx = l; ++xs; }
        else if (lw == "top" || lw == "bottom") { l.unit = Length::Percent; l.value = lw == "top" ? 0.0f : 100.0f; ly = l; ++ys; }
        else if (lw == "center") { if (k == 0 && w.size() == 2 && LowerAscii(w[1]) != "left" && LowerAscii(w[1]) != "right") ++xs; else if (k == 1) ++ys; }
        else if (ParseLength(w[k], em, true, l)) { if (k == 0) { lx = l; ++xs; } else { ly = l; ++ys; } }
        else return false;
    }
    if (xs > 1 || ys > 1) return false;
    x = lx; y = ly;
    return true;
}

} // namespace

bool ParseGradient(const std::string& text, float em, Gradient& out) {
    const std::string t = Trim(text);
    const std::string lt = LowerAscii(t);
    const std::size_t open = t.find('(');
    if (open == std::string::npos || t.back() != ')' || CloseParen(t, open) != t.size() - 1) return false;
    std::string fn = lt.substr(0, open);
    Gradient g;
    if (fn.compare(0, 10, "repeating-") == 0) { g.repeating = true; fn = fn.substr(10); }
    if (fn.compare(0, 8, "-webkit-") == 0) return false;   // the old prefixed syntax counts angles the other way: not read
    if (fn == "radial-gradient") g.radial = true;
    else if (fn != "linear-gradient") return false;
    g.posX.unit = g.posY.unit = Length::Percent; g.posX.value = g.posY.value = 50.0f;
    const std::vector<std::vector<std::string>> groups = CommaGroups(Words(t.substr(open + 1, t.size() - open - 2)));
    if (groups.empty() || groups.size() > 256) return false;
    std::size_t first = 0;
    // the first group: the direction (linear) or shape, size and centre (radial), unless it is already a colour stop
    const std::vector<std::string>& g0 = groups[0];
    Color probe;
    if (!g0.empty() && !ParseColor(g0[0], probe)) {
        first = 1;
        if (!g.radial) {
            if (LowerAscii(g0[0]) == "to") {
                if (g0.size() < 2 || g0.size() > 3) return false;
                int cx = 0, cy = 0;
                for (std::size_t k = 1; k < g0.size(); ++k) {
                    const std::string lw = LowerAscii(g0[k]);
                    if (lw == "left" && cx == 0) cx = -1;
                    else if (lw == "right" && cx == 0) cx = 1;
                    else if (lw == "top" && cy == 0) cy = -1;
                    else if (lw == "bottom" && cy == 0) cy = 1;
                    else return false;
                }
                if (cx != 0 && cy != 0) { g.cornerX = cx; g.cornerY = cy; }
                else g.angle = cx < 0 ? 270.0f : cx > 0 ? 90.0f : cy < 0 ? 0.0f : 180.0f;
            } else {
                if (g0.size() != 1 || !ParseAngle(g0[0], g.angle)) return false;
            }
        } else {
            std::size_t k = 0;
            bool shapeSet = false, sizeSet = false;
            std::vector<Length> sizes;
            for (; k < g0.size(); ++k) {
                const std::string lw = LowerAscii(g0[k]);
                if (lw == "at") break;
                if (lw == "circle" || lw == "ellipse") { if (shapeSet) return false; shapeSet = true; g.circle = lw == "circle"; continue; }
                struct K { const char* k; Gradient::Extent s; };
                static const K keys[] = {{"closest-side", Gradient::ClosestSide}, {"farthest-side", Gradient::FarthestSide},
                                         {"closest-corner", Gradient::ClosestCorner}, {"farthest-corner", Gradient::FarthestCorner}};
                bool kw = false;
                for (const K& key : keys) if (lw == key.k) { if (sizeSet) return false; g.extent = key.s; sizeSet = kw = true; }
                if (kw) continue;
                Length l;
                if (ParseLength(g0[k], em, true, l) && l.value >= 0 && sizes.size() < 2) { sizes.push_back(l); continue; }
                return false;
            }
            if (!sizes.empty()) {
                if (sizeSet) return false;
                g.extent = Gradient::Explicit;
                if (sizes.size() == 1) {   // one length: a circle (a percentage is not allowed for a circle)
                    if (sizes[0].unit == Length::Percent || (shapeSet && !g.circle)) return false;
                    g.circle = true; g.rx = g.ry = sizes[0];
                } else {
                    if (shapeSet && g.circle) return false;
                    g.circle = false; g.rx = sizes[0]; g.ry = sizes[1];
                }
            } else if (!shapeSet) g.circle = false;
            if (k < g0.size()) {
                const std::vector<std::string> pos(g0.begin() + (std::ptrdiff_t)k + 1, g0.end());
                if (!ParsePosition(pos, em, g.posX, g.posY)) return false;
            }
        }
    }
    // the colour stops: <color> [<length-percentage> [<length-percentage>]]; a hint (a lone length) is skipped (simplified)
    for (std::size_t gi = first; gi < groups.size(); ++gi) {
        const std::vector<std::string>& w = groups[gi];
        if (w.empty() || w.size() > 3) return false;
        GradientStop st;
        std::size_t k = 0;
        Length l;
        if (w.size() == 1 && ParseLength(w[0], em, true, l)) continue;   // a colour hint
        if (!ParseColor(w[0], st.color)) {
            if (LowerAscii(w[0]) == "transparent") st.color = Color{0, 0, 0, 0}; else return false;
        }
        for (k = 1; k < w.size(); ++k) {
            if (!ParseLength(w[k], em, true, l)) return false;
            st.pos = l;
            g.stops.push_back(st);   // a second position: the same colour again
        }
        if (w.size() == 1) g.stops.push_back(st);
    }
    if (g.stops.size() < 2 || g.stops.size() > 256) return false;
    out = std::move(g);
    return true;
}

namespace {

// The stop positions on a gradient line `len` px long, as fractions, fixed up as CSS Images 3, 3.5.3 says: the first and last
// default to 0 and 1, a position before an earlier one moves up to it, the ones in between without a position are spaced evenly.
std::vector<float> StopFractions(const Gradient& g, float len) {
    const std::size_t n = g.stops.size();
    std::vector<float> f(n, 0.0f);
    std::vector<bool> set(n, false);
    for (std::size_t i = 0; i < n; ++i) {
        const Length& p = g.stops[i].pos;
        if (p.IsSet()) { f[i] = len > 0 ? p.Resolve(len) / len : (p.unit == Length::Percent ? p.value / 100.0f : 0.0f); set[i] = true; }
    }
    if (!set[0]) { f[0] = 0.0f; set[0] = true; }
    if (!set[n - 1]) { f[n - 1] = 1.0f; set[n - 1] = true; }
    float hi = f[0];
    for (std::size_t i = 0; i < n; ++i) if (set[i]) { if (f[i] < hi) f[i] = hi; hi = f[i]; }
    for (std::size_t i = 1; i < n;) {
        if (set[i]) { ++i; continue; }
        std::size_t j = i;
        while (!set[j]) ++j;   // the last is set
        const float a = f[i - 1], b = f[j];
        for (std::size_t k = i; k < j; ++k) f[k] = a + (b - a) * (float)(k - i + 1) / (float)(j - i + 1);
        i = j;
    }
    return f;
}

Color Mix(const Color& a, const Color& b, float t) {
    // premultiplied: a transparent stop does not darken its neighbour (CSS Images 3, 3.5.3 "premultiplied")
    const float aa = a.a / 255.0f, ba = b.a / 255.0f;
    const float oa = aa + (ba - aa) * t;
    Color c;
    if (oa <= 0.0f) return Color{0, 0, 0, 0};
    auto ch = [&](std::uint8_t x, std::uint8_t y) {
        const float v = ((float)x * aa + ((float)y * ba - (float)x * aa) * t) / oa;
        return (std::uint8_t)std::lround(std::max(0.0f, std::min(255.0f, v)));
    };
    c.r = ch(a.r, b.r); c.g = ch(a.g, b.g); c.b = ch(a.b, b.b);
    c.a = (std::uint8_t)std::lround(std::max(0.0f, std::min(1.0f, oa)) * 255.0f);
    return c;
}

} // namespace

Color GradientColorAt(const Gradient& g, float w, float h, float px, float py) {
    if (g.stops.empty()) return Color{0, 0, 0, 0};
    float t = 0.0f, len = 1.0f;   // where (px, py) is on the gradient line, in px, and the line's length
    if (!g.radial) {
        float deg = g.angle;
        if (g.cornerX != 0 || g.cornerY != 0) {
            // toward a corner: the angle that makes the 50% line pass through the two other corners (3.1.1)
            // "to top right" in a w x h box: the gradient line is at right angles to the diagonal from the top left to the
            // bottom right corner, so its direction (sin a, -cos a) is (h, -w) / |(w, h)|
            const float a = std::atan2(h, w) * 57.29577951f;
            if (g.cornerY < 0) deg = g.cornerX > 0 ? a : 360.0f - a;
            else deg = g.cornerX > 0 ? 180.0f - a : 180.0f + a;
        }
        const float rad = deg * 0.01745329252f;
        const float dx = std::sin(rad), dy = -std::cos(rad);
        len = std::fabs(w * dx) + std::fabs(h * dy);   // the gradient line's length (3.1.1)
        t = (px - w * 0.5f) * dx + (py - h * 0.5f) * dy + len * 0.5f;
    } else {
        const float cx = g.posX.Resolve(w), cy = g.posY.Resolve(h);
        const float l = cx, rr = w - cx, tp = cy, bt = h - cy;
        float rx = 0, ry = 0;
        const float sx = std::min(std::fabs(l), std::fabs(rr)), sy = std::min(std::fabs(tp), std::fabs(bt));
        const float fx = std::max(std::fabs(l), std::fabs(rr)), fy = std::max(std::fabs(tp), std::fabs(bt));
        switch (g.extent) {
        case Gradient::ClosestSide: if (g.circle) rx = ry = std::min(sx, sy); else { rx = sx; ry = sy; } break;
        case Gradient::FarthestSide: if (g.circle) rx = ry = std::max(fx, fy); else { rx = fx; ry = fy; } break;
        case Gradient::ClosestCorner:
        case Gradient::FarthestCorner: {
            const float cxr = g.extent == Gradient::ClosestCorner ? sx : fx, cyr = g.extent == Gradient::ClosestCorner ? sy : fy;
            if (g.circle) rx = ry = std::sqrt(cxr * cxr + cyr * cyr);
            else { rx = cxr * 1.41421356f; ry = cyr * 1.41421356f; }   // same aspect as the sides, through the corner (3.2.2)
            break;
        }
        case Gradient::Explicit: rx = g.rx.Resolve(w); ry = g.circle ? rx : g.ry.Resolve(h); break;
        }
        len = std::max(rx, 1e-3f);
        const float ex = px - cx, ey = (py - cy) * (ry > 1e-3f ? rx / ry : 1.0f);
        t = std::sqrt(ex * ex + ey * ey);
    }
    if (!std::isfinite(t) || !std::isfinite(len)) return g.stops[0].color;
    const std::vector<float> f = StopFractions(g, len);
    float u = len > 0 ? t / len : 0.0f;
    if (g.repeating) {
        const float a = f.front(), b = f.back();
        if (b - a > 1e-6f) u = a + std::fmod(std::fmod(u - a, b - a) + (b - a), b - a);
        else return g.stops.back().color;
    }
    if (u <= f.front()) return g.stops.front().color;
    if (u >= f.back()) return g.stops.back().color;
    for (std::size_t i = 1; i < f.size(); ++i) {
        if (u <= f[i]) {
            const float span = f[i] - f[i - 1];
            return span <= 1e-6f ? g.stops[i].color : Mix(g.stops[i - 1].color, g.stops[i].color, (u - f[i - 1]) / span);
        }
    }
    return g.stops.back().color;
}

// ---- transforms (CSS Transforms 1) -----------------------------------------------------------------------------------------

Transform2D Transform2D::Then(const Transform2D& o) const {
    Transform2D r;
    r.a = o.a * a + o.c * b;  r.b = o.b * a + o.d * b;
    r.c = o.a * c + o.c * d;  r.d = o.b * c + o.d * d;
    r.e = o.a * e + o.c * f + o.e;  r.f = o.b * e + o.d * f + o.f;
    return r;
}

bool ParseTransform(const std::string& text, float em, float boxW, float boxH, Transform2D& out) {
    const std::vector<std::string> fns = Words(text);
    if (fns.empty() || fns.size() > 32) return false;
    if (fns.size() == 1 && LowerAscii(fns[0]) == "none") return false;
    // the functions apply right to left to the box: "translate(10px) rotate(30deg)" rotates, then moves (CSS Transforms 1, 6)
    Transform2D m;
    for (std::size_t i = fns.size(); i-- > 0;) {
        const std::string& f = fns[i];
        const std::size_t open = f.find('(');
        if (open == std::string::npos || f.back() != ')') return false;
        const std::string name = LowerAscii(f.substr(0, open));
        std::vector<std::string> args;
        for (const std::vector<std::string>& g : CommaGroups(Words(f.substr(open + 1, f.size() - open - 2)))) for (const std::string& a : g) args.push_back(a);
        if (args.empty() || args.size() > 6) return false;
        Transform2D t;
        auto len = [&](const std::string& a, float base, float& v) {
            Length l;
            if (!ParseLength(a, em, true, l)) return false;
            v = l.Resolve(base);
            return std::isfinite(v);
        };
        auto num = [&](const std::string& a, float& v) {
            std::string u;
            if (!ParseNumber(a, v, u)) return false;
            if (u == "%") v /= 100.0f; else if (!u.empty()) return false;
            return true;
        };
        if (name == "translate" || name == "translatex" || name == "translatey") {
            float x = 0, y = 0;
            if (name == "translatey") { if (args.size() != 1 || !len(args[0], boxH, y)) return false; }
            else {
                if (args.size() > 2 || !len(args[0], boxW, x)) return false;
                if (name == "translate" && args.size() == 2 && !len(args[1], boxH, y)) return false;
                if (name == "translatex" && args.size() != 1) return false;
            }
            t.e = x; t.f = y;
        } else if (name == "scale" || name == "scalex" || name == "scaley") {
            float x = 1, y = 1;
            if (args.size() > 2 || !num(args[0], name == "scaley" ? y : x)) return false;
            if (name == "scale") y = args.size() == 2 ? 0.0f : x;
            if (name == "scale" && args.size() == 2 && !num(args[1], y)) return false;
            t.a = x; t.d = y;
        } else if (name == "rotate" || name == "skew" || name == "skewx" || name == "skewy") {
            float ax = 0, ay = 0;
            if (!ParseAngle(args[0], ax)) return false;
            if (name == "skew" && args.size() == 2 && !ParseAngle(args[1], ay)) return false;
            if (args.size() > (name == "skew" ? 2u : 1u)) return false;
            if (name == "rotate") {
                const float r = ax * 0.01745329252f;
                t.a = std::cos(r); t.b = std::sin(r); t.c = -std::sin(r); t.d = std::cos(r);
            } else {
                if (name == "skewy") { ay = ax; ax = 0; }
                t.c = std::tan(ax * 0.01745329252f); t.b = std::tan(ay * 0.01745329252f);
            }
        } else if (name == "matrix") {
            if (args.size() != 6) return false;
            float v[6];
            for (int k = 0; k < 6; ++k) if (!num(args[(std::size_t)k], v[k])) return false;
            t.a = v[0]; t.b = v[1]; t.c = v[2]; t.d = v[3]; t.e = v[4]; t.f = v[5];
        } else return false;
        m = m.Then(t);
    }
    const float vals[6] = {m.a, m.b, m.c, m.d, m.e, m.f};
    for (float v : vals) if (!std::isfinite(v) || std::fabs(v) > 1e6f) return false;
    out = m;
    return true;
}

bool ListStyleFromName(const std::string& name, ListStyle& out) { return ListStyleWord(LowerAscii(Trim(name)), out); }

// ---- counter styles (CSS 2.1 12.6.2, CSS Counter Styles 3 section 6) ------------------------------------------------------

std::string CounterText(int n, ListStyle ls) {
    switch (ls) {
    case ListStyle::None: return std::string();
    case ListStyle::Disc: return "\xE2\x80\xA2";     // U+2022
    case ListStyle::Circle: return "\xE2\x97\xA6";   // U+25E6
    case ListStyle::Square: return "\xE2\x96\xAA";   // U+25AA
    case ListStyle::Decimal: return std::to_string(n);
    case ListStyle::DecimalLeadingZero: return n >= 0 && n < 10 ? "0" + std::to_string(n) : n < 0 && n > -10 ? "-0" + std::to_string(-n) : std::to_string(n);
    case ListStyle::LowerRoman:
    case ListStyle::UpperRoman: {
        // additive: 1 to 3999, else decimal
        if (n <= 0 || n >= 4000) return std::to_string(n);
        static const int vals[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
        static const char* syms[] = {"m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i"};
        std::string out;
        for (int k = 0; k < 13; ++k) while (n >= vals[k]) { out += syms[k]; n -= vals[k]; }
        if (ls == ListStyle::UpperRoman) for (char& c : out) c = (char)(c - 'a' + 'A');
        return out;
    }
    case ListStyle::LowerAlpha:
    case ListStyle::UpperAlpha: {
        // alphabetic: a..z, aa, ab ... (no zero); 0 and below: decimal
        if (n <= 0) return std::to_string(n);
        std::string out;
        while (n > 0) { --n; out.insert(out.begin(), (char)((ls == ListStyle::UpperAlpha ? 'A' : 'a') + n % 26)); n /= 26; }
        return out;
    }
    case ListStyle::LowerGreek: {
        // alphabetic over the 24 lower-case Greek letters (U+03B1 to U+03C9 without final sigma U+03C2)
        if (n <= 0) return std::to_string(n);
        std::vector<unsigned> digits;
        while (n > 0 && digits.size() < 16) { --n; digits.push_back((unsigned)(n % 24)); n /= 24; }
        std::string out;
        for (std::size_t k = digits.size(); k-- > 0;) {
            unsigned cp = 0x03B1u + digits[k];
            if (cp >= 0x03C2u) ++cp;   // skip final sigma
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        }
        return out;
    }
    }
    return std::to_string(n);
}

} // namespace okay::browser
