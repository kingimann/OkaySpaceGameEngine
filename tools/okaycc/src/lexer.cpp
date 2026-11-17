#include "lexer.hpp"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace okaycc {
namespace {

bool isDigit(unsigned char c) { return c >= '0' && c <= '9'; }
bool isHex(unsigned char c) {
    return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
bool isIdentStart(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
bool isIdentCont(unsigned char c) { return isIdentStart(c) || isDigit(c); }

int hexVal(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Longest match first (C17 6.4: the longest punctuator wins).
const char* const kPunct[] = {
    "%:%:", "<<=", ">>=", "...", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "::",
    "++",   "--",  "->",  "+=",  "-=", "*=", "/=", "%=", "&=", "^=", "|=", "##",
    "<:",   ":>",  "<%",  "%>",  "%:", "[",  "]",  "(",  ")",  "{",  "}",  ".",
    "&",    "*",   "+",   "-",   "~",  "!",  "/",  "%",  "<",  ">",  "^",  "|",
    "?",    ":",   ";",   "=",   ",",  "#",
};

}  // namespace

Lexer::Lexer(std::string file, std::string source, Diagnostics& diags)
    : file_(std::move(file)), src_(std::move(source)), diags_(diags) {
    // A leading UTF-8 BOM is not a C character. Skip it so the first column stays 1.
    if (src_.size() >= 3 && static_cast<unsigned char>(src_[0]) == 0xEF &&
        static_cast<unsigned char>(src_[1]) == 0xBB &&
        static_cast<unsigned char>(src_[2]) == 0xBF) {
        i_ = 3;
    }
}

void Lexer::errAt(int line, int col, const std::string& msg) { diags_.error(file_, line, col, msg); }

Token Lexer::make(Tok kind, int line, int col, bool lead) const {
    Token t;
    t.kind = kind;
    t.line = line;
    t.column = col;
    t.leadSpace = lead;
    t.file = file_;
    return t;
}

char Lexer::peekPhys() const { return eof() ? '\0' : src_[i_]; }

bool Lexer::isNlAt(std::size_t pos) const {
    if (pos >= src_.size()) return false;
    char c = src_[pos];
    return c == '\n' || c == '\r';
}

char Lexer::getPhys() {
    if (eof()) return '\0';
    unsigned char c = static_cast<unsigned char>(src_[i_++]);
    if (c == '\r') {
        if (!eof() && src_[i_] == '\n') ++i_;
        ++line_;
        col_ = 1;
        return '\n';
    }
    if (c == '\n') {
        ++line_;
        col_ = 1;
        return '\n';
    }
    if (c == '\t') {
        // Tab stops are every 8 columns, counted from column 1.
        col_ = ((col_ - 1) / 8 + 1) * 8 + 1;
        return '\t';
    }
    ++col_;
    return static_cast<char>(c);
}

void Lexer::skipSplices() {
    while (!eof() && src_[i_] == '\\' && isNlAt(i_ + 1)) {
        getPhys();
        getPhys();
    }
}

char Lexer::get() {
    skipSplices();
    if (eof()) return '\0';
    return getPhys();
}

char Lexer::peekLogical() {
    std::size_t si = i_;
    int sl = line_;
    int sc = col_;
    char c = get();
    i_ = si;
    line_ = sl;
    col_ = sc;
    return c;
}

bool Lexer::startsWith(const char* s) {
    std::size_t si = i_;
    int sl = line_;
    int sc = col_;
    for (std::size_t k = 0; s[k] != '\0'; ++k) {
        if (get() != s[k]) {
            i_ = si;
            line_ = sl;
            col_ = sc;
            return false;
        }
    }
    i_ = si;
    line_ = sl;
    col_ = sc;
    return true;
}

bool Lexer::scanBlockComment(int startLine, int startCol) {
    for (;;) {
        char c = get();
        if (c == '\0') {
            errAt(startLine, startCol, "unterminated block comment");
            return false;
        }
        if (c == '*' && peekLogical() == '/') {
            get();
            return true;
        }
    }
}

void Lexer::appendUtf8(std::string& out, uint32_t cp) const {
    if (cp <= 0x7Fu) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

Lexer::Esc Lexer::readEscape() {
    Esc e;
    int el = line_;
    int ec = col_;
    e.raw.push_back(get());
    char k = peekLogical();
    if (k == '\0' || k == '\n') {
        errAt(el, ec, "unterminated escape sequence");
        return e;
    }
    k = get();
    e.raw.push_back(k);
    auto finishByte = [&](uint32_t cp) {
        e.ok = true;
        e.hasCp = true;
        e.cp = cp;
        if (cp <= 0xFFu) e.bytes.push_back(static_cast<char>(cp));
        return e;
    };
    switch (k) {
    case '\'':
    case '"':
    case '?':
    case '\\':
        return finishByte(static_cast<unsigned char>(k));
    case 'a': return finishByte(7);
    case 'b': return finishByte(8);
    case 'f': return finishByte(12);
    case 'n': return finishByte(10);
    case 'r': return finishByte(13);
    case 't': return finishByte(9);
    case 'v': return finishByte(11);
    case 'x': {
        if (!isHex(static_cast<unsigned char>(peekLogical()))) {
            errAt(el, ec, "hexadecimal escape has no digits");
            return e;
        }
        uint64_t v = 0;
        bool big = false;
        while (isHex(static_cast<unsigned char>(peekLogical()))) {
            int d = hexVal(static_cast<unsigned char>(peekLogical()));
            e.raw.push_back(get());
            if (v > (UINT64_MAX - static_cast<uint64_t>(d)) / 16u) big = true;
            else v = v * 16u + static_cast<uint64_t>(d);
        }
        if (big || v > 0xFFFFFFFFu) {
            errAt(el, ec, "hexadecimal escape is too large");
            return e;
        }
        return finishByte(static_cast<uint32_t>(v));
    }
    case 'u':
    case 'U': {
        int n = k == 'u' ? 4 : 8;
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) {
            if (!isHex(static_cast<unsigned char>(peekLogical()))) {
                errAt(el, ec, "universal character name is incomplete");
                return e;
            }
            v = static_cast<uint32_t>(v * 16u + static_cast<uint32_t>(hexVal(
                                                    static_cast<unsigned char>(peekLogical()))));
            e.raw.push_back(get());
        }
        if (v > 0x10FFFFu || (v >= 0xD800u && v <= 0xDFFFu)) {
            errAt(el, ec, "universal character name is not a valid code point");
            return e;
        }
        e.ok = true;
        e.hasCp = true;
        e.isUcn = true;
        e.cp = v;
        appendUtf8(e.bytes, v);
        return e;
    }
    default:
        break;
    }
    if (k >= '0' && k <= '7') {
        uint32_t v = static_cast<uint32_t>(k - '0');
        for (int i = 0; i < 2; ++i) {
            char n = peekLogical();
            if (n < '0' || n > '7') break;
            v = v * 8u + static_cast<uint32_t>(n - '0');
            e.raw.push_back(get());
        }
        return finishByte(v);
    }
    errAt(el, ec, std::string("unknown escape '\\") + k + "'");
    return e;
}

uint32_t Lexer::packBytes(const std::string& bytes, int line, int col) {
    if (bytes.size() > 4) {
        errAt(line, col, "character constant is too long");
        return 0;
    }
    uint32_t v = 0;
    for (unsigned char b : bytes) v = (v << 8) | b;
    return v;
}

void Lexer::lexChar(Token& t, const std::string& prefix, int width) {
    std::string spelling = prefix;
    int sl = t.line;
    int sc = t.column;
    spelling.push_back(get());
    std::string bytes;
    std::string raw;
    int pieces = 0;
    uint32_t wideCp = 0;
    if (peekLogical() == '\'' || peekLogical() == '\n' || peekLogical() == '\0') {
        errAt(sl, sc, "empty character constant");
    }
    while (peekLogical() != '\'' && peekLogical() != '\0' && peekLogical() != '\n') {
        ++pieces;
        if (peekLogical() == '\\') {
            Esc e = readEscape();
            spelling += e.raw;
            raw += e.raw;
            if (!e.ok) continue;
            if (width == 1) {
                if (!e.isUcn && e.cp > 255u) {
                    errAt(sl, sc, "escape does not fit in a character");
                } else {
                    bytes += e.bytes;
                }
            } else {
                wideCp = e.cp;
            }
        } else {
            char ch = get();
            spelling.push_back(ch);
            raw.push_back(ch);
            bytes.push_back(ch);
            wideCp = static_cast<unsigned char>(ch);
        }
    }
    if (peekLogical() == '\'') spelling.push_back(get());
    else errAt(sl, sc, "unterminated character constant");

    t.kind = Tok::CharLit;
    t.text = spelling;
    t.rawInside = raw;
    t.decoded = bytes;
    t.litWidth = width;
    if (width == 1) t.charValue = packBytes(bytes, sl, sc);
    else {
        if (pieces != 1) errAt(sl, sc, "wide character constant has more than one character");
        t.charValue = wideCp;
    }
}

void Lexer::lexString(Token& t, const std::string& prefix, int width) {
    std::string spelling = prefix;
    int sl = t.line;
    int sc = t.column;
    spelling.push_back(get());
    std::string decoded;
    std::string raw;
    while (true) {
        char c = peekLogical();
        if (c == '\0' || c == '\n') {
            errAt(sl, sc, "unterminated string literal");
            break;
        }
        if (c == '"') {
            spelling.push_back(get());
            break;
        }
        if (c == '\\') {
            Esc e = readEscape();
            spelling += e.raw;
            raw += e.raw;
            if (!e.ok) continue;
            if (width == 1 && !e.isUcn && e.hasCp && e.cp > 255u) {
                errAt(sl, sc, "escape does not fit in a character");
                continue;
            }
            if (width != 1 && e.hasCp && e.bytes.empty()) appendUtf8(decoded, e.cp);
            else decoded += e.bytes;
            continue;
        }
        char ch = get();
        spelling.push_back(ch);
        raw.push_back(ch);
        decoded.push_back(ch);
    }
    t.kind = Tok::StringLit;
    t.text = std::move(spelling);
    t.decoded = std::move(decoded);
    t.rawInside = std::move(raw);
    t.litWidth = width;
}

void Lexer::lexIdent(Token& t) {
    std::string s;
    while (true) {
        unsigned char c = static_cast<unsigned char>(peekLogical());
        if (isIdentCont(c)) {
            s.push_back(get());
            continue;
        }
        if (c == '\\') {
            int el = line_;
            int ec = col_;
            get();
            char kind = peekLogical();
            int n = 0;
            if (kind == 'u') n = 4;
            else if (kind == 'U') n = 8;
            else {
                errAt(el, ec, "expected a universal character name in an identifier");
                s.push_back('\\');
                break;
            }
            s.push_back('\\');
            s.push_back(get());
            for (int i = 0; i < n; ++i) {
                if (!isHex(static_cast<unsigned char>(peekLogical()))) {
                    errAt(el, ec, "universal character name is incomplete");
                    break;
                }
                s.push_back(get());
            }
            continue;
        }
        break;
    }
    t.text = std::move(s);
    t.kind = isKeywordSpelling(t.text) ? Tok::Keyword : Tok::Ident;
}

void Lexer::readIntSuffix(std::string& spelling, Token& t) {
    auto pk = [&]() { return peekLogical(); };
    auto isL = [](char c) { return c == 'l' || c == 'L'; };
    auto isU = [](char c) { return c == 'u' || c == 'U'; };
    char c = pk();
    if (!isL(c) && !isU(c)) {
        if (isIdentStart(static_cast<unsigned char>(c))) {
            errAt(line_, col_, "invalid suffix on integer constant");
            while (isIdentCont(static_cast<unsigned char>(pk()))) spelling.push_back(get());
        }
        return;
    }
    if (isU(c)) {
        spelling.push_back(get());
        t.isUnsigned = true;
        if (isL(pk())) {
            char l1 = get();
            spelling.push_back(l1);
            if (pk() == l1) {
                spelling.push_back(get());
                t.longRank = 2;
            } else if (isL(pk())) {
                errAt(line_, col_, "mixed case in long long suffix");
                spelling.push_back(get());
                t.longRank = 2;
            } else {
                t.longRank = 1;
            }
        }
    } else {
        char l1 = get();
        spelling.push_back(l1);
        if (pk() == l1) {
            spelling.push_back(get());
            t.longRank = 2;
        } else if (isL(pk())) {
            errAt(line_, col_, "mixed case in long long suffix");
            spelling.push_back(get());
            t.longRank = 2;
        } else {
            t.longRank = 1;
        }
        if (isU(pk())) {
            spelling.push_back(get());
            t.isUnsigned = true;
        }
    }
    if (isIdentCont(static_cast<unsigned char>(pk()))) {
        errAt(line_, col_, "invalid suffix on integer constant");
        while (isIdentCont(static_cast<unsigned char>(pk()))) spelling.push_back(get());
    }
}

void Lexer::readFloatSuffix(std::string& spelling, Token& t) {
    char c = peekLogical();
    if (c == 'f' || c == 'F') {
        spelling.push_back(get());
        t.floatRank = 1;
    } else if (c == 'l' || c == 'L') {
        spelling.push_back(get());
        t.floatRank = 2;
    }
    if (isIdentCont(static_cast<unsigned char>(peekLogical()))) {
        errAt(line_, col_, "invalid suffix on floating constant");
        while (isIdentCont(static_cast<unsigned char>(peekLogical()))) spelling.push_back(get());
    }
}

bool Lexer::readDecExponent(std::string& spelling) {
    spelling.push_back(get());
    if (peekLogical() == '+' || peekLogical() == '-') spelling.push_back(get());
    if (!isDigit(static_cast<unsigned char>(peekLogical()))) {
        errAt(line_, col_, "exponent has no digits");
        return false;
    }
    while (isDigit(static_cast<unsigned char>(peekLogical()))) spelling.push_back(get());
    return true;
}

bool Lexer::readBinExponent(std::string& spelling) {
    spelling.push_back(get());
    if (peekLogical() == '+' || peekLogical() == '-') spelling.push_back(get());
    if (!isDigit(static_cast<unsigned char>(peekLogical()))) {
        errAt(line_, col_, "binary exponent has no digits");
        return false;
    }
    while (isDigit(static_cast<unsigned char>(peekLogical()))) spelling.push_back(get());
    return true;
}

void Lexer::finishInt(Token& t, int base) {
    const std::string& s = t.text;
    std::size_t i = 0;
    if (base == 16) i = 2;
    uint64_t v = 0;
    bool ov = false;
    for (; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 'u' || c == 'U' || c == 'l' || c == 'L') break;
        int d = hexVal(c);
        if (d < 0 || d >= base) break;
        if (v > (UINT64_MAX - static_cast<uint64_t>(d)) / static_cast<uint64_t>(base)) ov = true;
        else v = v * static_cast<uint64_t>(base) + static_cast<uint64_t>(d);
    }
    t.uval = v;
    t.base = base;
    if (ov) errAt(t.line, t.column, "integer constant is too large");
}

void Lexer::finishFloat(Token& t) {
    std::string s = t.text;
    if (t.floatRank != 0 && !s.empty()) s.pop_back();
    errno = 0;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size() || !std::isfinite(v)) {
        errAt(t.line, t.column, "floating constant is not representable");
        t.fval = 0;
        return;
    }
    t.fval = v;
}

void Lexer::lexHex(std::string& spelling, Token& t) {
    bool any = false;
    while (isHex(static_cast<unsigned char>(peekLogical()))) {
        spelling.push_back(get());
        any = true;
    }
    bool hexFloat = false;
    if (peekLogical() == '.') {
        std::size_t si = i_;
        int sl = line_;
        int sc = col_;
        std::string extra;
        extra.push_back(get());
        bool frac = false;
        while (isHex(static_cast<unsigned char>(peekLogical()))) {
            extra.push_back(get());
            frac = true;
        }
        char p = peekLogical();
        if ((p == 'p' || p == 'P') && (any || frac)) {
            spelling += extra;
            hexFloat = true;
            any = any || frac;
        } else {
            i_ = si;
            line_ = sl;
            col_ = sc;
        }
    }
    if (!hexFloat && (peekLogical() == 'p' || peekLogical() == 'P') && any) hexFloat = true;
    if (hexFloat) {
        if (!any) errAt(t.line, t.column, "hexadecimal floating constant has no digits");
        if (peekLogical() == 'p' || peekLogical() == 'P') readBinExponent(spelling);
        else errAt(t.line, t.column, "hexadecimal floating constant requires a binary exponent");
        readFloatSuffix(spelling, t);
        t.text = spelling;
        t.kind = Tok::Float;
        finishFloat(t);
        return;
    }
    if (!any) errAt(t.line, t.column, "hexadecimal constant has no digits");
    readIntSuffix(spelling, t);
    t.text = spelling;
    t.kind = Tok::Int;
    finishInt(t, 16);
}

void Lexer::lexNumber(Token& t) {
    std::string s;
    if (peekLogical() == '.') {
        s.push_back(get());
        if (!isDigit(static_cast<unsigned char>(peekLogical()))) {
            errAt(t.line, t.column, "digit required after decimal point");
        }
        while (isDigit(static_cast<unsigned char>(peekLogical()))) s.push_back(get());
        if (peekLogical() == 'e' || peekLogical() == 'E') readDecExponent(s);
        readFloatSuffix(s, t);
        t.text = s;
        t.kind = Tok::Float;
        finishFloat(t);
        return;
    }
    if (peekLogical() == '0') {
        s.push_back(get());
        if (peekLogical() == 'x' || peekLogical() == 'X') {
            s.push_back(get());
            lexHex(s, t);
            return;
        }
    }
    while (isDigit(static_cast<unsigned char>(peekLogical()))) s.push_back(get());
    bool isFloat = false;
    if (peekLogical() == '.') {
        isFloat = true;
        s.push_back(get());
        while (isDigit(static_cast<unsigned char>(peekLogical()))) s.push_back(get());
    }
    if (peekLogical() == 'e' || peekLogical() == 'E') {
        isFloat = true;
        readDecExponent(s);
    }
    if (isFloat) {
        readFloatSuffix(s, t);
        t.text = std::move(s);
        t.kind = Tok::Float;
        finishFloat(t);
        return;
    }
    int base = 10;
    if (!s.empty() && s[0] == '0') {
        base = 8;
        for (char c : s) {
            if (c == '8' || c == '9') {
                errAt(t.line, t.column, "invalid digit in octal constant");
                break;
            }
        }
    }
    readIntSuffix(s, t);
    t.text = std::move(s);
    t.kind = Tok::Int;
    finishInt(t, base);
}

bool Lexer::tryPunct(Token& t) {
    for (const char* p : kPunct) {
        if (!startsWith(p)) continue;
        t.kind = Tok::Punct;
        t.text = p;
        for (std::size_t k = 0; p[k] != '\0'; ++k) get();
        return true;
    }
    return false;
}

bool Lexer::skipSpaceAndComments(bool& lead) {
    while (true) {
        skipSplices();
        if (eof()) return false;
        unsigned char c = static_cast<unsigned char>(peekPhys());
        if (c == '\0') {
            errAt(line_, col_, "null character in source");
            ++i_;
            ++col_;
            lead = true;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\v' || c == '\f') {
            getPhys();
            lead = true;
            continue;
        }
        if (c == '\n' || c == '\r') return true;
        if (c == '/') {
            std::size_t si = i_;
            int sl = line_;
            int sc = col_;
            get();
            char n = peekLogical();
            if (n == '/' || n == '*') {
                get();
                lead = true;
                if (n == '/') {
                    while (true) {
                        char ch = peekLogical();
                        if (ch == '\0' || ch == '\n') break;
                        get();
                    }
                } else {
                    scanBlockComment(sl, sc);
                }
                continue;
            }
            i_ = si;
            line_ = sl;
            col_ = sc;
        }
        return false;
    }
}

Token Lexer::next() {
    bool lead = false;
    while (true) {
        bool atNl = skipSpaceAndComments(lead);
        skipSplices();
        if (eof()) return make(Tok::End, line_, col_, false);
        unsigned char c = static_cast<unsigned char>(peekPhys());
        if (c == '\n' || c == '\r' || atNl) {
            if (c == '\n' || c == '\r') {
                int sl = line_;
                int sc = col_;
                getPhys();
                Token t = make(Tok::Newline, sl, sc, lead);
                t.text = "\n";
                return t;
            }
        }
        int sl = line_;
        int sc = col_;
        char logical = peekLogical();
        Token t = make(Tok::End, sl, sc, lead);
        unsigned char u = static_cast<unsigned char>(logical);
        if (isIdentStart(u) || u == '\\') {
            lexIdent(t);
            if ((t.text == "u" || t.text == "U" || t.text == "L" || t.text == "u8") &&
                (peekLogical() == '"' || peekLogical() == '\'')) {
                int width = 1;
                if (t.text == "u") width = 2;
                else if (t.text == "U" || t.text == "L") width = 4;
                std::string prefix = t.text;
                if (peekLogical() == '"') lexString(t, prefix, width);
                else lexChar(t, prefix, width);
            }
            return t;
        }
        if (u == '.') {
            std::size_t si = i_;
            int l2 = line_;
            int c2 = col_;
            get();
            char n = peekLogical();
            i_ = si;
            line_ = l2;
            col_ = c2;
            if (isDigit(static_cast<unsigned char>(n))) {
                lexNumber(t);
                return t;
            }
        }
        if (isDigit(u)) {
            lexNumber(t);
            return t;
        }
        if (u == '\'') {
            lexChar(t, "", 1);
            return t;
        }
        if (u == '"') {
            lexString(t, "", 1);
            return t;
        }
        if (tryPunct(t)) return t;
        unsigned char bad = static_cast<unsigned char>(get());
        std::string msg = "stray character ";
        if (bad >= 32 && bad < 127) {
            msg += "'";
            msg.push_back(static_cast<char>(bad));
            msg += "'";
        } else {
            char buf[16];
            std::snprintf(buf, sizeof buf, "U+%02X", bad);
            msg += buf;
        }
        errAt(sl, sc, msg);
        lead = true;
    }
}

bool Lexer::readHeaderName(std::string& name, bool& angle, int& line, int& column) {
    bool lead = false;
    skipSpaceAndComments(lead);
    skipSplices();
    char c = peekLogical();
    line = line_;
    column = col_;
    if (c != '"' && c != '<') return false;
    char end = c == '"' ? '"' : '>';
    angle = c == '<';
    get();
    std::string n;
    while (true) {
        char ch = peekLogical();
        if (ch == '\0' || ch == '\n') {
            errAt(line, column, "unterminated header name");
            name = std::move(n);
            return false;
        }
        if (ch == end) {
            get();
            break;
        }
        n.push_back(get());
    }
    name = std::move(n);
    return true;
}

}  // namespace okaycc
