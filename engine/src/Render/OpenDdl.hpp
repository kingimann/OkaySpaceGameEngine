#pragma once
// A bounded, iterative OpenDDL parser (the text data language OpenGEX is written in), written from the public OpenDDL grammar
// description: a structure is `Identifier [name] [(property = value, ...)] { children }`, or, for the primitive data types,
// `type [ [n] ] [name] { value, value, ... }` (with `{...}, {...}` sub-arrays when the type has an `[n]`). Names start with `$`
// (global) or `%` (local). Comments are `//` and `/* */`. Literals: bool, integers (decimal, 0x, 0o, 0b, 'c', `_` separators),
// floats (decimal; a hex, octal or binary literal is the IEEE bit pattern), strings (adjacent strings join, `\` escapes),
// references (`$a`, `%b`, `$a%b`, `null`) and type names.
//
// No recursion: open structures sit on an explicit stack whose depth is bounded. Every count that grows with the file has a
// budget (structures, nesting depth, data values, string bytes, properties): a file over any of them is refused with a message
// naming it, before the memory is allocated. Internal: not installed.
#include "ReaderCommon.hpp"
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {
namespace detail {
namespace ddl {

enum class Prim : std::uint8_t { None = 0, Bool, I8, I16, I32, I64, U8, U16, U32, U64, Half, Float, Double, String, Ref, Type };

constexpr std::uint32_t kNone = 0xFFFFFFFFu;

struct Limits {
    std::size_t maxStructs = 1000000;        // structures of all kinds
    std::size_t maxDepth = 256;              // nesting of custom structures
    std::size_t maxValues = 16000000;        // numbers over all data structures
    std::size_t maxStringBytes = 64u * 1024u * 1024u;   // string, reference and type-name bytes over all data structures
    std::size_t maxStrings = 4000000;        // string / reference values
    std::size_t maxSubArray = 4096;          // the n of `type[n]`
    std::size_t maxProps = 2000000;          // properties over all structures
};

struct Prop {
    std::uint32_t name = 0;      // index into Doc::texts
    Prim kind = Prim::Bool;      // Bool, Float (any number), String, Ref or Type
    double num = 1;              // a bare property name is the bool true
    std::uint32_t str = kNone;   // index into Doc::texts for String / Ref / Type
};

struct Node {
    std::uint32_t ident = 0;                 // Doc::texts: the identifier (the type keyword for a data structure)
    std::uint32_t name = kNone;              // Doc::texts: "$x" or "%x", or none
    Prim prim = Prim::None;                  // None for a custom structure
    std::uint32_t sub = 0;                   // the n of type[n], 0 when none
    std::uint32_t parent = kNone, firstChild = kNone, lastChild = kNone, next = kNone;
    std::uint32_t propBegin = 0, propCount = 0;
    std::uint64_t dataBegin = 0, dataCount = 0;   // numbers: Doc::nums; String / Ref / Type: Doc::strs (values, not groups)
};

struct Doc {
    std::vector<Node> nodes;
    std::vector<std::string> texts;      // identifiers, names, property names and values (interned)
    std::vector<Prop> props;
    std::vector<double> nums;
    std::vector<std::string> strs;
    std::unordered_map<std::string, std::uint32_t> intern;

    const std::string& Ident(const Node& n) const { return texts[n.ident]; }
    bool Is(const Node& n, const char* ident) const { return texts[n.ident] == ident; }
    // The first child with this identifier, or kNone.
    std::uint32_t Child(std::uint32_t at, const char* ident) const {
        for (std::uint32_t c = nodes[at].firstChild; c != kNone; c = nodes[c].next) if (Is(nodes[c], ident)) return c;
        return kNone;
    }
    const Prop* FindProp(const Node& n, const char* name) const {
        for (std::uint32_t i = 0; i < n.propCount; ++i) { const Prop& p = props[n.propBegin + i]; if (texts[p.name] == name) return &p; }
        return nullptr;
    }
    // A property as text ("" when absent or not a string), or as a number (`def` when absent or not a number).
    const std::string& PropStr(const Node& n, const char* name) const {
        static const std::string empty;
        const Prop* p = FindProp(n, name);
        return p && p->str != kNone && (p->kind == Prim::String || p->kind == Prim::Type || p->kind == Prim::Ref) ? texts[p->str] : empty;
    }
    double PropNum(const Node& n, const char* name, double def) const {
        const Prop* p = FindProp(n, name);
        return p && (p->kind == Prim::Float || p->kind == Prim::Bool) ? p->num : def;
    }
    bool HasProp(const Node& n, const char* name) const { return FindProp(n, name) != nullptr; }
};

inline Prim PrimOf(const std::string& s) {
    static const struct { const char* n; Prim p; } t[] = {
        {"bool", Prim::Bool}, {"b", Prim::Bool},
        {"int8", Prim::I8}, {"i8", Prim::I8}, {"int16", Prim::I16}, {"i16", Prim::I16}, {"int32", Prim::I32}, {"i32", Prim::I32}, {"int64", Prim::I64}, {"i64", Prim::I64},
        {"unsigned_int8", Prim::U8}, {"uint8", Prim::U8}, {"u8", Prim::U8}, {"unsigned_int16", Prim::U16}, {"uint16", Prim::U16}, {"u16", Prim::U16},
        {"unsigned_int32", Prim::U32}, {"uint32", Prim::U32}, {"u32", Prim::U32}, {"unsigned_int64", Prim::U64}, {"uint64", Prim::U64}, {"u64", Prim::U64},
        {"half", Prim::Half}, {"h", Prim::Half}, {"float", Prim::Float}, {"f", Prim::Float}, {"double", Prim::Double}, {"d", Prim::Double},
        {"string", Prim::String}, {"s", Prim::String}, {"ref", Prim::Ref}, {"r", Prim::Ref}, {"type", Prim::Type}, {"t", Prim::Type}};
    for (const auto& e : t) if (s == e.n) return e.p;
    return Prim::None;
}
inline bool IsIntPrim(Prim p) { return p >= Prim::I8 && p <= Prim::U64; }
inline bool IsFloatPrim(Prim p) { return p == Prim::Half || p == Prim::Float || p == Prim::Double; }
inline bool IsNumPrim(Prim p) { return p == Prim::Bool || IsIntPrim(p) || IsFloatPrim(p); }

// IEEE half to double.
inline double HalfToDouble(unsigned h) {
    const int e = (int)((h >> 10) & 31), m = (int)(h & 1023);
    double v;
    if (e == 0) v = std::ldexp((double)m, -24);
    else if (e == 31) v = 65504.0;   // infinity and NaN: not meaningful in a model, kept finite
    else v = std::ldexp((double)(m + 1024), e - 25);
    return (h & 0x8000u) ? -v : v;
}

class Parser {
public:
    Parser(const std::string& text, Doc& doc, const Limits& lim) : p_(text.data()), e_(text.data() + text.size()), d_(doc), lim_(lim) {}
    // False with `err` (a short reason, with the line) on any error or budget.
    bool Run(std::string& err) {
        const bool ok = Go();
        if (!ok) err = err_ + " (line " + std::to_string(Line()) + ")";
        return ok;
    }

private:
    const char* p_;
    const char* e_;
    Doc& d_;
    Limits lim_;
    std::string err_;
    std::size_t valueBytes_ = 0;

    std::size_t Line() const { return 1 + lines_; }
    mutable std::size_t lines_ = 0;

    bool Err(const std::string& m) { if (err_.empty()) err_ = m; return false; }
    static bool IsIdStart(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
    static bool IsIdChar(char c) { return IsIdStart(c) || (c >= '0' && c <= '9'); }

    // white space and comments; false on an unterminated block comment
    bool Skip() {
        for (;;) {
            while (p_ < e_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\r' || *p_ == '\n' || *p_ == '\f' || *p_ == '\v')) { if (*p_ == '\n') ++lines_; ++p_; }
            if (p_ + 1 < e_ && p_[0] == '/' && p_[1] == '/') { while (p_ < e_ && *p_ != '\n') ++p_; continue; }
            if (p_ + 1 < e_ && p_[0] == '/' && p_[1] == '*') {
                p_ += 2;
                while (p_ + 1 < e_ && !(p_[0] == '*' && p_[1] == '/')) { if (*p_ == '\n') ++lines_; ++p_; }
                if (p_ + 1 >= e_) return Err("a comment that is never closed");
                p_ += 2;
                continue;
            }
            return true;
        }
    }

    std::uint32_t Intern(const std::string& s) {
        auto it = d_.intern.find(s);
        if (it != d_.intern.end()) return it->second;
        const std::uint32_t i = (std::uint32_t)d_.texts.size();
        d_.texts.push_back(s);
        d_.intern.emplace(s, i);
        return i;
    }
    bool CountBytes(std::size_t n) {
        valueBytes_ += n + 16;
        if (valueBytes_ > lim_.maxStringBytes) return Err("too much string data (budget " + std::to_string(lim_.maxStringBytes >> 20) + " MB)");
        return true;
    }

    bool Ident(std::string& s) {
        if (p_ >= e_ || !IsIdStart(*p_)) return Err("an identifier was expected");
        const char* a = p_;
        while (p_ < e_ && IsIdChar(*p_)) ++p_;
        if (p_ - a > 255) return Err("an identifier longer than 255 characters");
        s.assign(a, (std::size_t)(p_ - a));
        return true;
    }
    // `$name` or `%name`
    bool Name(std::string& s) {
        const char* a = p_;
        ++p_;
        while (p_ < e_ && IsIdChar(*p_)) ++p_;
        if (p_ - a < 2) return Err("a name needs at least one character after the $ or %");
        if (p_ - a > 256) return Err("a name longer than 255 characters");
        s.assign(a, (std::size_t)(p_ - a));
        return true;
    }
    // A reference: null, or $a, %a, $a%b%c
    bool RefText(std::string& s) {
        if (p_ < e_ && IsIdStart(*p_)) {
            std::string id;
            if (!Ident(id)) return false;
            if (id != "null") return Err("a reference must be null or start with $ or %");
            s = "null";
            return true;
        }
        if (p_ >= e_ || (*p_ != '$' && *p_ != '%')) return Err("a reference must be null or start with $ or %");
        const char* a = p_;
        for (;;) {
            ++p_;
            while (p_ < e_ && IsIdChar(*p_)) ++p_;
            if (!(p_ < e_ && *p_ == '%')) break;
        }
        if (p_ - a > 1024) return Err("a reference longer than 1024 characters");
        s.assign(a, (std::size_t)(p_ - a));
        if (s.size() < 2 || s.back() == '%' || s.back() == '$') return Err("a malformed reference");
        return true;
    }

    // One escaped character of a string or character literal, after the backslash.
    bool Escape(std::string& out) {
        if (p_ >= e_) return Err("a string that ends after a backslash");
        const char c = *p_++;
        switch (c) {
        case '"': case '\'': case '?': case '\\': out.push_back(c); return true;
        case 'a': out.push_back('\a'); return true;
        case 'b': out.push_back('\b'); return true;
        case 'f': out.push_back('\f'); return true;
        case 'n': out.push_back('\n'); return true;
        case 'r': out.push_back('\r'); return true;
        case 't': out.push_back('\t'); return true;
        case 'v': out.push_back('\v'); return true;
        case 'x': {
            int v = 0, n = 0;
            while (n < 2 && p_ < e_ && std::isxdigit((unsigned char)*p_)) { const char h = *p_++; v = v * 16 + (h <= '9' ? h - '0' : (h | 32) - 'a' + 10); ++n; }
            if (n != 2) return Err("a \\x escape needs two hexadecimal digits");
            out.push_back((char)v);
            return true;
        }
        default: return Err("an unknown escape sequence in a string");
        }
    }
    // One or more adjacent "..." literals.
    bool StringLit(std::string& s) {
        s.clear();
        for (;;) {
            if (p_ >= e_ || *p_ != '"') return Err("a string was expected");
            ++p_;
            for (;;) {
                if (p_ >= e_) return Err("a string that is never closed");
                const char c = *p_++;
                if (c == '"') break;
                if (c == '\n') ++lines_;
                if (c == '\\') { if (!Escape(s)) return false; }
                else s.push_back(c);
                if (s.size() > (1u << 22)) return Err("a string longer than 4 MB");
            }
            if (!Skip()) return false;
            if (!(p_ < e_ && *p_ == '"')) return true;
        }
    }

    // A number token: sign, digits, letters for 0x / 0b / 0o, '.', '_', exponent sign. Stops at ',', '}', white space, '{'.
    bool NumberToken(std::string& t) {
        const char* a = p_;
        while (p_ < e_ && ((*p_ >= '0' && *p_ <= '9') || (*p_ >= 'a' && *p_ <= 'z') || (*p_ >= 'A' && *p_ <= 'Z') || *p_ == '_' || *p_ == '.' || *p_ == '+' || *p_ == '-')) ++p_;
        if (p_ == a) return Err("a value was expected");
        if (p_ - a > 80) return Err("a number token longer than 80 characters");
        t.assign(a, (std::size_t)(p_ - a));
        return true;
    }
    // An integer literal in `t` (sign and base prefix allowed) as an unsigned magnitude plus sign. False when it is not one.
    static bool IntLiteral(const std::string& t, bool& neg, std::uint64_t& mag, bool& isInt) {
        isInt = false;
        std::size_t i = 0;
        neg = false;
        if (i < t.size() && (t[i] == '+' || t[i] == '-')) { neg = t[i] == '-'; ++i; }
        if (i >= t.size()) return false;
        int base = 10;
        if (t[i] == '0' && i + 1 < t.size() && (t[i + 1] == 'x' || t[i + 1] == 'X')) { base = 16; i += 2; }
        else if (t[i] == '0' && i + 1 < t.size() && (t[i + 1] == 'o' || t[i + 1] == 'O')) { base = 8; i += 2; }
        else if (t[i] == '0' && i + 1 < t.size() && (t[i + 1] == 'b' || t[i + 1] == 'B')) { base = 2; i += 2; }
        std::uint64_t v = 0;
        int digits = 0;
        for (; i < t.size(); ++i) {
            const char c = t[i];
            if (c == '_') { if (digits == 0) return false; continue; }
            int dv;
            if (c >= '0' && c <= '9') dv = c - '0';
            else if (base == 16 && ((c | 32) >= 'a' && (c | 32) <= 'f')) dv = (c | 32) - 'a' + 10;
            else return false;   // '.', 'e', and so on: not an integer
            if (dv >= base) return false;
            if (v > (0xFFFFFFFFFFFFFFFFull - (std::uint64_t)dv) / (std::uint64_t)base) return false;   // overflow past 64 bits
            v = v * (std::uint64_t)base + (std::uint64_t)dv;
            ++digits;
        }
        if (digits == 0) return false;
        mag = v;
        isInt = true;
        return true;
    }
    // A character literal 'a' .. up to 8 characters, big-endian packed.
    bool CharLit(std::uint64_t& v) {
        ++p_;
        std::string s;
        for (;;) {
            if (p_ >= e_) return Err("a character literal that is never closed");
            const char c = *p_++;
            if (c == '\'') break;
            if (c == '\\') { if (!Escape(s)) return false; } else s.push_back(c);
            if (s.size() > 8) return Err("a character literal longer than 8 characters");
        }
        if (s.empty()) return Err("an empty character literal");
        v = 0;
        for (unsigned char c : s) v = (v << 8) | c;
        return true;
    }
    static bool InRange(Prim p, bool neg, std::uint64_t mag) {
        switch (p) {
        case Prim::I8: return neg ? mag <= 128 : mag <= 127;
        case Prim::I16: return neg ? mag <= 32768 : mag <= 32767;
        case Prim::I32: return neg ? mag <= 2147483648ull : mag <= 2147483647ull;
        case Prim::I64: return neg ? mag <= 9223372036854775808ull : mag <= 9223372036854775807ull;
        case Prim::U8: return !neg && mag <= 255;
        case Prim::U16: return !neg && mag <= 65535;
        case Prim::U32: return !neg && mag <= 4294967295ull;
        case Prim::U64: return !neg;
        default: return false;
        }
    }

    // One numeric value of type `prim` (Bool, integer or float types), appended to nums.
    bool NumberValue(Prim prim, double& out) {
        if (p_ < e_ && *p_ == '\'' && (IsIntPrim(prim) || prim == Prim::Bool)) {
            std::uint64_t v;
            if (!CharLit(v)) return false;
            if (!InRange(prim == Prim::Bool ? Prim::U8 : prim, false, v)) return Err("a character literal out of range for its type");
            out = (double)v;
            return true;
        }
        if (prim == Prim::Bool && p_ < e_ && IsIdStart(*p_)) {
            std::string id;
            if (!Ident(id)) return false;
            if (id == "true") { out = 1; return true; }
            if (id == "false") { out = 0; return true; }
            return Err("a bool must be true or false");
        }
        std::string t;
        if (!NumberToken(t)) return false;
        bool neg = false, isInt = false;
        std::uint64_t mag = 0;
        IntLiteral(t, neg, mag, isInt);
        if (prim == Prim::Bool) {
            if (!isInt || neg || mag > 1) return Err("a bool must be true, false, 0 or 1");
            out = (double)mag;
            return true;
        }
        if (IsIntPrim(prim)) {
            if (!isInt) return Err("an integer was expected, found '" + t.substr(0, 20) + "'");
            if (!InRange(prim, neg, mag)) return Err("an integer out of range for its type");
            out = neg ? -(double)mag : (double)mag;
            return true;
        }
        // float types
        const bool prefixed = isInt && t.find_first_of("xXoObB") != std::string::npos && t.find('.') == std::string::npos;
        if (prefixed) {   // the IEEE bit pattern
            if (neg) return Err("a bit-pattern float literal cannot have a sign");
            if (prim == Prim::Float) {
                if (mag > 0xFFFFFFFFull) return Err("a float bit pattern wider than 32 bits");
                const std::uint32_t u = (std::uint32_t)mag;
                float f;
                std::memcpy(&f, &u, 4);
                if (!std::isfinite(f)) return Err("a float bit pattern that is not a finite number");
                out = f;
            } else if (prim == Prim::Double) {
                double f;
                std::memcpy(&f, &mag, 8);
                if (!std::isfinite(f) || std::fabs(f) > rc::kMaxCoord) return Err("a double bit pattern that is not a finite number in range");
                out = f;
            } else {
                if (mag > 0xFFFFull) return Err("a half bit pattern wider than 16 bits");
                out = HalfToDouble((unsigned)mag);
            }
            return true;
        }
        std::string clean;
        clean.reserve(t.size());
        for (char c : t) if (c != '_') clean.push_back(c);
        double v;
        if (!rc::ParseNumber(clean, v)) return Err("a number that is malformed or outside +-1e15: '" + t.substr(0, 20) + "'");
        out = v;
        return true;
    }

    bool PushNum(double v) {
        if (d_.nums.size() >= lim_.maxValues) return Err("too many data values (budget " + std::to_string(lim_.maxValues) + ")");
        d_.nums.push_back(v);
        return true;
    }
    bool PushStr(std::string&& s) {
        if (d_.strs.size() >= lim_.maxStrings) return Err("too many string values (budget " + std::to_string(lim_.maxStrings) + ")");
        if (!CountBytes(s.size())) return false;
        d_.strs.push_back(std::move(s));
        return true;
    }
    // One value of a data structure, any type.
    bool Value(Prim prim, Node& n) {
        if (prim == Prim::String) { std::string s; if (!StringLit(s) || !PushStr(std::move(s))) return false; ++n.dataCount; return true; }
        if (prim == Prim::Ref) { std::string s; if (!RefText(s) || !PushStr(std::move(s))) return false; ++n.dataCount; return true; }
        if (prim == Prim::Type) { std::string s; if (p_ >= e_ || !IsIdStart(*p_) || !Ident(s) || !PushStr(std::move(s))) return Err("a type name was expected"); ++n.dataCount; return true; }
        double v;
        if (!NumberValue(prim, v) || !PushNum(v)) return false;
        ++n.dataCount;
        return true;
    }

    bool Properties(Node& n) {
        // at '('
        ++p_;
        n.propBegin = (std::uint32_t)d_.props.size();
        if (!Skip()) return false;
        if (p_ < e_ && *p_ == ')') { ++p_; return true; }
        for (;;) {
            std::string id;
            if (!Skip() || !Ident(id)) return false;
            if (d_.props.size() >= lim_.maxProps || n.propCount >= 64) return Err("too many properties (budget)");
            Prop pr;
            pr.name = Intern(id);
            if (!Skip()) return false;
            if (p_ < e_ && *p_ == '=') {
                ++p_;
                if (!Skip()) return false;
                if (p_ >= e_) return Err("a property value was expected");
                if (*p_ == '"') { std::string s; if (!StringLit(s) || !CountBytes(s.size())) return false; pr.kind = Prim::String; pr.str = Intern(s); }
                else if (*p_ == '$' || *p_ == '%') { std::string s; if (!RefText(s)) return false; pr.kind = Prim::Ref; pr.str = Intern(s); }
                else if (IsIdStart(*p_)) {
                    std::string s;
                    if (!Ident(s)) return false;
                    if (s == "true") { pr.kind = Prim::Bool; pr.num = 1; }
                    else if (s == "false") { pr.kind = Prim::Bool; pr.num = 0; }
                    else if (s == "null") { pr.kind = Prim::Ref; pr.str = Intern(s); }
                    else { pr.kind = Prim::Type; pr.str = Intern(s); }
                } else {
                    double v;
                    if (!NumberValue(*p_ == '\'' ? Prim::I64 : Prim::Float, v)) return false;
                    pr.kind = Prim::Float;
                    pr.num = v;
                }
            }
            d_.props.push_back(pr);
            ++n.propCount;
            if (!Skip()) return false;
            if (p_ < e_ && *p_ == ',') { ++p_; continue; }
            if (p_ < e_ && *p_ == ')') { ++p_; return true; }
            return Err("a property list must end with )");
        }
    }

    bool Data(Node& n) {
        // after '{' of a data structure; reads up to and including '}'
        const Prim prim = n.prim;
        const bool strLike = prim == Prim::String || prim == Prim::Ref || prim == Prim::Type;
        n.dataBegin = strLike ? d_.strs.size() : d_.nums.size();
        n.dataCount = 0;
        if (!Skip()) return false;
        if (p_ < e_ && *p_ == '}') { ++p_; return true; }
        if (n.sub == 0) {
            for (;;) {
                if (!Skip() || !Value(prim, n) || !Skip()) return false;
                if (p_ < e_ && *p_ == ',') { ++p_; continue; }
                if (p_ < e_ && *p_ == '}') { ++p_; return true; }
                return Err("a data list must end with }");
            }
        }
        for (;;) {
            if (!Skip()) return false;
            if (p_ >= e_ || *p_ != '{') return Err("a sub-array { ... } was expected");
            ++p_;
            std::uint64_t k = 0;
            if (!Skip()) return false;
            if (!(p_ < e_ && *p_ == '}')) {
                for (;;) {
                    if (!Skip() || !Value(prim, n) || !Skip()) return false;
                    ++k;
                    if (p_ < e_ && *p_ == ',') { ++p_; continue; }
                    if (p_ < e_ && *p_ == '}') break;
                    return Err("a sub-array must end with }");
                }
            }
            ++p_;   // the } of the sub-array
            if (k != n.sub) return Err("a sub-array with " + std::to_string(k) + " values where " + std::to_string(n.sub) + " were declared");
            if (!Skip()) return false;
            if (p_ < e_ && *p_ == ',') { ++p_; continue; }
            if (p_ < e_ && *p_ == '}') { ++p_; return true; }
            return Err("a data array list must end with }");
        }
    }

    bool Go() {
        std::vector<std::uint32_t> stack;
        for (;;) {
            if (!Skip()) return false;
            if (p_ >= e_) {
                if (!stack.empty()) return Err("a structure that is never closed (missing })");
                return true;
            }
            if (*p_ == '}') {
                if (stack.empty()) return Err("a } without an open structure");
                stack.pop_back();
                ++p_;
                continue;
            }
            if (d_.nodes.size() >= lim_.maxStructs) return Err("too many structures (budget " + std::to_string(lim_.maxStructs) + ")");
            Node n;
            std::string id;
            if (!Ident(id)) return false;
            n.ident = Intern(id);
            n.prim = PrimOf(id);
            n.parent = stack.empty() ? kNone : stack.back();
            if (!Skip()) return false;
            if (n.prim != Prim::None) {
                if (p_ < e_ && *p_ == '[') {
                    ++p_;
                    if (!Skip()) return false;
                    std::string t;
                    if (!NumberToken(t)) return false;
                    bool neg, isInt;
                    std::uint64_t mag;
                    if (!IntLiteral(t, neg, mag, isInt) || !isInt || neg || mag == 0 || mag > lim_.maxSubArray) return Err("an array size that is not a whole number from 1 to " + std::to_string(lim_.maxSubArray));
                    n.sub = (std::uint32_t)mag;
                    if (!Skip()) return false;
                    if (p_ >= e_ || *p_ != ']') return Err("an array size must be followed by ]");
                    ++p_;
                    if (!Skip()) return false;
                }
                if (p_ < e_ && (*p_ == '$' || *p_ == '%')) { std::string nm; if (!Name(nm)) return false; n.name = Intern(nm); if (!Skip()) return false; }
                if (p_ >= e_ || *p_ != '{') return Err("a { was expected after the data type");
                ++p_;
                if (!Data(n)) return false;
                Attach(n, stack);
                continue;
            }
            if (p_ < e_ && (*p_ == '$' || *p_ == '%')) { std::string nm; if (!Name(nm)) return false; n.name = Intern(nm); if (!Skip()) return false; }
            if (p_ < e_ && *p_ == '(') { if (!Properties(n) || !Skip()) return false; }
            if (p_ >= e_ || *p_ != '{') return Err("a { was expected after the structure '" + id + "'");
            ++p_;
            if (stack.size() >= lim_.maxDepth) return Err("structures nested deeper than " + std::to_string(lim_.maxDepth) + " (budget)");
            const std::uint32_t at = Attach(n, stack);
            stack.push_back(at);
        }
    }

    std::uint32_t Attach(const Node& n, const std::vector<std::uint32_t>&) {
        const std::uint32_t at = (std::uint32_t)d_.nodes.size();
        d_.nodes.push_back(n);
        if (n.parent != kNone) {
            Node& par = d_.nodes[n.parent];
            if (par.lastChild == kNone) par.firstChild = par.lastChild = at;
            else { d_.nodes[par.lastChild].next = at; par.lastChild = at; }
        }
        return at;
    }
};

// Parses `text` into `doc`. `err` gets the reason and the line number on failure.
inline bool Parse(const std::string& text, Doc& doc, std::string& err, const Limits& lim = Limits()) {
    doc = Doc{};
    Parser p(text, doc, lim);
    return p.Run(err);
}

} // namespace ddl
} // namespace detail
} // namespace okay
