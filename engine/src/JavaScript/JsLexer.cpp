// okay::js lexer. Written from ECMA-262 chapter 12 (ECMAScript Language: Lexical
// Grammar): 12.2 white space, 12.3 line terminators, 12.4 comments, 12.7 names and
// keywords, 12.8 punctuators, 12.9.3 numeric literals (with separators), 12.9.4
// string literals and escapes, 12.9.6 template literal lexical components. The
// whole source is tokenized first (the parser looks ahead to find arrow functions);
// template substitutions are tracked with a stack of open braces.
#include "JsInternal.hpp"
#include <cmath>
#include <cstdlib>

namespace okay::js {

// The reserved words of 12.7.2 (strict mode code included) come first, then the contextual words the
// editor colours too; the parser treats only the first kReservedCount as reserved.
const char* const kKeywords[] = {
    "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do", "else", "enum",
    "export", "extends", "false", "finally", "for", "function", "if", "import", "in", "instanceof", "new", "null",
    "return", "super", "switch", "this", "throw", "true", "try", "typeof", "var", "void", "while", "with", "yield",
    "let", "static", "implements", "interface", "package", "private", "protected", "public",
    // contextual
    "await", "async", "of", "get", "set", "undefined", "NaN", "Infinity",
};
const int kReservedCount = 45;
const int kKeywordCount = (int)(sizeof(kKeywords) / sizeof(kKeywords[0]));

std::u16string AsciiTo16(const char* s) {
    std::u16string o;
    while (*s) o += (char16_t)(unsigned char)*s++;
    return o;
}

std::u16string Utf8To16(std::string_view s) {
    std::u16string o;
    o.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        std::uint32_t cp;
        int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 4; }
        else { o += (char16_t)0xFFFD; ++i; continue; }
        if (i + (std::size_t)n > s.size()) { o += (char16_t)0xFFFD; break; }
        bool bad = false;
        for (int k = 1; k < n; ++k) {
            unsigned char d = (unsigned char)s[i + (std::size_t)k];
            if ((d & 0xC0) != 0x80) { bad = true; break; }
            cp = (cp << 6) | (d & 0x3F);
        }
        if (bad) { o += (char16_t)0xFFFD; ++i; continue; }
        i += (std::size_t)n;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            o += (char16_t)(0xD800 + (cp >> 10));
            o += (char16_t)(0xDC00 + (cp & 0x3FF));
        } else o += (char16_t)cp;
    }
    return o;
}

std::string Utf16To8(const std::u16string& s) {
    std::string o;
    o.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        std::uint32_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            ++i;
        } else if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
        else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    }
    return o;
}

namespace {

struct PunctText { const char* text; P id; };
// Every punctuator of 12.8, longest first, so the first match is the longest one.
const PunctText kPuncts[] = {
    {">>>=", P_ShrEq}, {"...", P_Ellipsis}, {"===", P_Eq3}, {"!==", P_Ne2}, {"**=", P_Star2Eq}, {"<<=", P_ShlEq},
    {">>=", P_SarEq}, {">>>", P_Shr}, {"&&=", P_AndEq}, {"||=", P_OrEq}, {"?" "?=", P_NullishEq},
    {"=>", P_Arrow}, {"<=", P_Le}, {">=", P_Ge}, {"==", P_Eq2}, {"!=", P_Ne}, {"**", P_Star2}, {"++", P_Inc},
    {"--", P_Dec}, {"<<", P_Shl}, {">>", P_Sar}, {"&&", P_And}, {"||", P_Or}, {"?" "?", P_Nullish}, {"?.", P_QDot},
    {"+=", P_PlusEq}, {"-=", P_MinusEq}, {"*=", P_StarEq}, {"/=", P_SlashEq}, {"%=", P_PercentEq}, {"&=", P_AmpEq},
    {"|=", P_BarEq}, {"^=", P_CaretEq},
    {"{", P_LBrace}, {"}", P_RBrace}, {"(", P_LParen}, {")", P_RParen}, {"[", P_LBrack}, {"]", P_RBrack},
    {".", P_Dot}, {";", P_Semi}, {",", P_Comma}, {"<", P_Lt}, {">", P_Gt}, {"+", P_Plus}, {"-", P_Minus},
    {"*", P_Star}, {"/", P_Slash}, {"%", P_Percent}, {"&", P_Amp}, {"|", P_Bar}, {"^", P_Caret}, {"!", P_Bang},
    {"~", P_Tilde}, {"?", P_Quest}, {":", P_Colon}, {"=", P_Assign},
};

struct Lexer {
    const std::string& s;
    std::size_t i = 0;
    int line = 1;
    std::size_t lineStart = 0;
    SyntaxError& err;
    std::vector<Token>& out;
    std::vector<int> braces;   // 0 = a plain {, 1 = a template's ${
    bool sawNl = false;

    Lexer(const std::string& src, std::vector<Token>& o, SyntaxError& e) : s(src), err(e), out(o) {}

    int Col() const { return (int)(i - lineStart) + 1; }
    bool Fail(const std::string& m) {
        err.line = line;
        err.column = Col();
        err.message = m;
        return false;
    }
    // A code point at i (UTF-8), and its byte length.
    std::uint32_t PeekCp(std::size_t at, int* len) const {
        unsigned char c = (unsigned char)s[at];
        if (c < 0x80) { *len = 1; return c; }
        int n = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        if (at + (std::size_t)n > s.size()) { *len = 1; return 0xFFFD; }
        std::uint32_t cp = n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; k < n; ++k) cp = (cp << 6) | ((unsigned char)s[at + (std::size_t)k] & 0x3F);
        *len = n;
        return cp;
    }
    static bool IsLineTerm(std::uint32_t c) { return c == '\n' || c == '\r' || c == 0x2028 || c == 0x2029; }
    static bool IsSpace(std::uint32_t c) {
        return c == ' ' || c == '\t' || c == 0x0B || c == 0x0C || c == 0xA0 || c == 0xFEFF || c == 0x1680 ||
               (c >= 0x2000 && c <= 0x200A) || c == 0x202F || c == 0x205F || c == 0x3000;
    }
    static bool IsIdStart(std::uint32_t c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '$' || c == '_' || (c >= 0x80 && !IsSpace(c) && !IsLineTerm(c));
    }
    static bool IsIdPart(std::uint32_t c) { return IsIdStart(c) || (c >= '0' && c <= '9') || c == 0x200C || c == 0x200D; }
    void NewLine(std::size_t afterBreak) {
        ++line;
        lineStart = afterBreak;
        sawNl = true;
    }
    static void Append(std::u16string& o, std::uint32_t cp) {
        if (cp >= 0x10000) {
            cp -= 0x10000;
            o += (char16_t)(0xD800 + (cp >> 10));
            o += (char16_t)(0xDC00 + (cp & 0x3FF));
        } else o += (char16_t)cp;
    }

    // Skips white space and comments (12.2-12.4).
    bool Skip() {
        while (i < s.size()) {
            int len;
            std::uint32_t c = PeekCp(i, &len);
            if (c == '\n' || c == 0x2028 || c == 0x2029) { i += (std::size_t)len; NewLine(i); continue; }
            if (c == '\r') { ++i; if (i < s.size() && s[i] == '\n') ++i; NewLine(i); continue; }
            if (IsSpace(c)) { i += (std::size_t)len; continue; }
            if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
                while (i < s.size()) {
                    int l2;
                    std::uint32_t d = PeekCp(i, &l2);
                    if (IsLineTerm(d)) break;
                    i += (std::size_t)l2;
                }
                continue;
            }
            if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
                int startLine = line, startCol = Col();
                i += 2;
                bool closed = false;
                while (i < s.size()) {
                    if (s[i] == '*' && i + 1 < s.size() && s[i + 1] == '/') { i += 2; closed = true; break; }
                    int l2;
                    std::uint32_t d = PeekCp(i, &l2);
                    if (d == '\r') { ++i; if (i < s.size() && s[i] == '\n') ++i; NewLine(i); continue; }
                    i += (std::size_t)l2;
                    if (IsLineTerm(d)) NewLine(i);
                }
                if (!closed) { line = startLine; i = lineStart + (std::size_t)startCol - 1; return Fail("this /* comment is never closed with */"); }
                continue;
            }
            if (c == '#' && i == 0 && i + 1 < s.size() && s[i + 1] == '!') {   // hashbang comment (B.1 / 12.5)
                while (i < s.size() && s[i] != '\n') ++i;
                continue;
            }
            break;
        }
        return true;
    }

    int Hex(char c) const {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    // \u escapes (12.9.4): \uXXXX or \u{X...}. i is just after the 'u'. -1 = bad.
    std::int64_t UnicodeEscape() {
        if (i < s.size() && s[i] == '{') {
            ++i;
            std::int64_t v = 0;
            int digits = 0;
            while (i < s.size() && s[i] != '}') {
                int h = Hex(s[i]);
                if (h < 0) return -1;
                v = v * 16 + h;
                if (v > 0x10FFFF) return -1;
                ++i;
                ++digits;
            }
            if (i >= s.size() || digits == 0) return -1;
            ++i;
            return v;
        }
        if (i + 4 > s.size()) return -1;
        std::int64_t v = 0;
        for (int k = 0; k < 4; ++k) {
            int h = Hex(s[i + (std::size_t)k]);
            if (h < 0) return -1;
            v = v * 16 + h;
        }
        i += 4;
        return v;
    }

    // One escape sequence after a backslash (i at the char after '\'). Returns false on a bad one.
    bool Escape(std::u16string& o, bool inTemplate) {
        char c = s[i];
        switch (c) {
            case 'n': o += u'\n'; ++i; return true;
            case 't': o += u'\t'; ++i; return true;
            case 'r': o += u'\r'; ++i; return true;
            case 'b': o += u'\b'; ++i; return true;
            case 'f': o += u'\f'; ++i; return true;
            case 'v': o += u'\v'; ++i; return true;
            case '0':
                if (i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9') return false;   // legacy octal (not in strict code)
                o += (char16_t)0;
                ++i;
                return true;
            case 'x': {
                if (i + 2 >= s.size() + 0 || Hex(s[i + 1]) < 0 || i + 2 >= s.size() || Hex(s[i + 2]) < 0) return false;
                o += (char16_t)(Hex(s[i + 1]) * 16 + Hex(s[i + 2]));
                i += 3;
                return true;
            }
            case 'u': {
                ++i;
                std::int64_t v = UnicodeEscape();
                if (v < 0) return false;
                Append(o, (std::uint32_t)v);
                return true;
            }
            case '\r':
                ++i;
                if (i < s.size() && s[i] == '\n') ++i;
                NewLine(i);
                if (inTemplate) o += u'\n';   // a line continuation; in templates the raw newline counts
                return true;
            case '\n':
                ++i;
                NewLine(i);
                return true;
            default:
                if (c >= '1' && c <= '9') return false;   // octal and \8 \9 escapes are not allowed in strict code
                {
                    int len;
                    std::uint32_t cp = PeekCp(i, &len);
                    i += (std::size_t)len;
                    if (cp == 0x2028 || cp == 0x2029) { NewLine(i); return true; }
                    Append(o, cp);
                }
                return true;
        }
    }

    Token& Push(TT t, int ln, int col) {
        out.emplace_back();
        Token& k = out.back();
        k.t = t;
        k.line = ln;
        k.col = col;
        k.nl = sawNl;
        sawNl = false;
        return k;
    }

    bool Number(int ln, int col) {
        std::size_t start = i;
        double v = 0;
        auto digitsWithSep = [&](int base, std::string& txt) -> bool {
            bool any = false, lastSep = false;
            while (i < s.size()) {
                char c = s[i];
                if (c == '_') {
                    if (!any || lastSep) return Fail("a numeric separator '_' must sit between digits");
                    lastSep = true;
                    ++i;
                    continue;
                }
                int d = Hex(c);
                if (d < 0 || d >= base) break;
                txt += c;
                any = true;
                lastSep = false;
                ++i;
            }
            if (lastSep) return Fail("a numeric separator '_' must sit between digits");
            return true;
        };
        if (s[i] == '0' && i + 1 < s.size() && (s[i + 1] == 'x' || s[i + 1] == 'X' || s[i + 1] == 'o' || s[i + 1] == 'O' ||
                                               s[i + 1] == 'b' || s[i + 1] == 'B')) {
            char k = (char)(s[i + 1] | 0x20);
            int base = k == 'x' ? 16 : k == 'o' ? 8 : 2;
            i += 2;
            std::string txt;
            if (!digitsWithSep(base, txt)) return false;
            if (txt.empty()) return Fail("a number needs digits after 0" + std::string(1, k));
            for (char c : txt) v = v * base + Hex(c);
        } else {
            std::string txt;
            if (s[i] == '0' && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9')
                return Fail("numbers starting with 0 (old octal) are not allowed; write 0o17 for octal");
            if (s[i] != '.' && !digitsWithSep(10, txt)) return false;
            if (i < s.size() && s[i] == '.') {
                txt += '.';
                ++i;
                if (i < s.size() && s[i] == '_') return Fail("a numeric separator '_' must sit between digits");
                if (!digitsWithSep(10, txt)) return false;
            }
            if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
                std::size_t save = i;
                std::string e = "e";
                ++i;
                if (i < s.size() && (s[i] == '+' || s[i] == '-')) e += s[i++];
                std::string ed;
                if (!digitsWithSep(10, ed)) return false;
                if (ed.empty()) { i = save; return Fail("a number's exponent needs digits"); }
                txt += e + ed;
            }
            v = std::strtod(txt.c_str(), nullptr);
        }
        if (i < s.size() && s[i] == 'n') return Fail("BigInt literals (123n) are not supported");
        if (i < s.size()) {
            int len;
            std::uint32_t c = PeekCp(i, &len);
            if (IsIdStart(c) || (c >= '0' && c <= '9')) return Fail("a name can not start right after a number");
        }
        (void)start;
        Token& t = Push(TT::Num, ln, col);
        t.n = v;
        return true;
    }

    bool StringLit(int ln, int col) {
        char q = s[i++];
        std::u16string v;
        while (true) {
            if (i >= s.size()) { line = ln; return Fail("this string is never closed"); }
            char c = s[i];
            if (c == q) { ++i; break; }
            if (c == '\n' || c == '\r') return Fail("a string can not go over a line end (use \\n or a `template`)");
            if (c == '\\') {
                ++i;
                if (i >= s.size()) return Fail("this string is never closed");
                if (!Escape(v, false)) return Fail("bad escape sequence in a string");
                continue;
            }
            int len;
            std::uint32_t cp = PeekCp(i, &len);
            i += (std::size_t)len;
            Append(v, cp);
        }
        Token& t = Push(TT::Str, ln, col);
        t.s = std::move(v);
        return true;
    }

    // A template piece from just after ` or } up to ` or ${ (12.9.6).
    bool TemplatePiece(int ln, int col, bool head) {
        std::u16string v;
        bool bad = false;
        while (true) {
            if (i >= s.size()) { line = ln; return Fail("this `template` is never closed"); }
            char c = s[i];
            if (c == '`') {
                ++i;
                Token& t = Push(TT::Template, ln, col);
                t.tpl = head ? 0 : 3;
                t.s = std::move(v);
                t.badEscape = bad;
                return true;
            }
            if (c == '$' && i + 1 < s.size() && s[i + 1] == '{') {
                i += 2;
                braces.push_back(1);
                Token& t = Push(TT::Template, ln, col);
                t.tpl = head ? 1 : 2;
                t.s = std::move(v);
                t.badEscape = bad;
                return true;
            }
            if (c == '\\') {
                ++i;
                if (i >= s.size()) return Fail("this `template` is never closed");
                if (!Escape(v, true)) { bad = true; ++i; }
                continue;
            }
            if (c == '\r') {   // CR and CRLF are read as LF (12.9.6 TV)
                ++i;
                if (i < s.size() && s[i] == '\n') ++i;
                NewLine(i);
                v += u'\n';
                continue;
            }
            int len;
            std::uint32_t cp = PeekCp(i, &len);
            i += (std::size_t)len;
            if (cp == '\n' || cp == 0x2028 || cp == 0x2029) NewLine(i);
            Append(v, cp);
        }
    }

    bool Run() {
        while (true) {
            if (!Skip()) return false;
            int ln = line, col = Col();
            if (i >= s.size()) { Push(TT::End, ln, col); return true; }
            int len;
            std::uint32_t c = PeekCp(i, &len);
            if (IsIdStart(c) || c == '\\' || c == '#') {
                bool priv = c == '#';
                if (priv) {
                    ++i;
                    if (i >= s.size()) return Fail("'#' must start a private name");
                    c = PeekCp(i, &len);
                    if (!IsIdStart(c) && c != '\\') return Fail("'#' must start a private name");
                }
                std::u16string name;
                bool escaped = false;
                while (i < s.size()) {
                    std::uint32_t d = PeekCp(i, &len);
                    if (d == '\\') {
                        if (i + 1 >= s.size() || s[i + 1] != 'u') return Fail("bad escape in a name");
                        i += 2;
                        std::int64_t v = UnicodeEscape();
                        if (v < 0 || !(name.empty() ? IsIdStart((std::uint32_t)v) : IsIdPart((std::uint32_t)v)))
                            return Fail("bad escape in a name");
                        Append(name, (std::uint32_t)v);
                        escaped = true;
                        continue;
                    }
                    if (!(name.empty() ? IsIdStart(d) : IsIdPart(d))) break;
                    Append(name, d);
                    i += (std::size_t)len;
                }
                Token& t = Push(priv ? TT::PrivateName : TT::Name, ln, col);
                t.s = std::move(name);
                t.escaped = escaped;
                continue;
            }
            if ((c >= '0' && c <= '9') || (c == '.' && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9')) {
                if (!Number(ln, col)) return false;
                continue;
            }
            if (c == '"' || c == '\'') {
                if (!StringLit(ln, col)) return false;
                continue;
            }
            if (c == '`') {
                ++i;
                if (!TemplatePiece(ln, col, true)) return false;
                continue;
            }
            if (c == '}' && !braces.empty() && braces.back() == 1) {
                braces.pop_back();
                ++i;
                if (!TemplatePiece(ln, col, false)) return false;
                continue;
            }
            // ?. is not a punctuator when a digit follows (a ? .5 : 1)
            bool matched = false;
            for (const PunctText& p : kPuncts) {
                std::size_t n = std::char_traits<char>::length(p.text);
                if (s.compare(i, n, p.text) != 0) continue;
                if (p.id == P_QDot && i + 2 < s.size() && s[i + 2] >= '0' && s[i + 2] <= '9') continue;
                if (p.id == P_LBrace) braces.push_back(0);
                if (p.id == P_RBrace && !braces.empty()) braces.pop_back();
                Token& t = Push(TT::Punct, ln, col);
                t.p = (std::uint16_t)p.id;
                i += n;
                matched = true;
                break;
            }
            if (!matched) {
                std::string ch = s.substr(i, (std::size_t)len);
                return Fail("unexpected character '" + ch + "'");
            }
        }
    }
};

} // namespace

bool Tokenize(const std::string& src, std::vector<Token>& out, SyntaxError& err) {
    out.clear();
    Lexer lx(src, out, err);
    if (!lx.Run()) return false;
    // matching brackets for the parser's look-ahead (arrow functions)
    std::vector<int> st;
    for (int k = 0; k < (int)out.size(); ++k) {
        Token& t = out[(std::size_t)k];
        if (t.t != TT::Punct) continue;
        if (t.p == P_LParen || t.p == P_LBrack || t.p == P_LBrace) st.push_back(k);
        else if (t.p == P_RParen || t.p == P_RBrack || t.p == P_RBrace) {
            if (st.empty()) continue;   // the parser reports it
            out[(std::size_t)st.back()].match = k;
            st.pop_back();
        }
    }
    return true;
}

} // namespace okay::js
