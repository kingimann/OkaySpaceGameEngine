#pragma once
// okay::json: the engine's JSON reader and writer (docs/engine/shared_code.md "JSON read", "JSON escape and write"). Header only,
// standard library only. Written from RFC 8259 (the JSON grammar, section 2 to 7) and nothing else; no code from any JSON
// library. First user: the Browser's game pack reader (okay/Browser/GamePack.hpp), which reads the web export's manifest.
// The engine's older private readers (Core/LocTable.cpp, Core/BuildPipeline.cpp, the OkayScript VM) can move onto it one by
// one; until they do they are not changed by this file.
//
// Strict: one value with optional white space around it; no comments, no trailing commas, no single quotes, no NaN or
// Infinity, no leading zeros, no control characters inside strings, \u escapes must be well formed and surrogates must
// pair. Hostile input is normal: nesting is capped (Limits::maxDepth), so a deep "[[[[..." cannot exhaust the stack, and
// the input size is capped (Limits::maxBytes). A repeated object key keeps every pair; Get() returns the first.
//
//     okay::json::Value v; std::string why;
//     if (!okay::json::Parse(text, v, &why)) ... why says where and what;
//     if (const okay::json::Value* files = v.Get("files")) for (const auto& f : files->array) ...
#include "okay/Core/Utf8.hpp"

#include <cerrno>
#include <charconv>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace okay::json {

enum class Type { Null, Bool, Number, String, Array, Object };

struct Value {
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;                                   ///< String: UTF-8, escapes decoded
    std::vector<Value> array;                             ///< Array
    std::vector<std::pair<std::string, Value>> object;    ///< Object: the pairs in source order

    bool IsNull() const { return type == Type::Null; }
    bool IsBool() const { return type == Type::Bool; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    bool IsArray() const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }
    /// Object: the first value under `key`, or nullptr (also for anything that is not an object).
    const Value* Get(std::string_view key) const {
        if (type != Type::Object) return nullptr;
        for (const auto& kv : object) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    /// A whole number from 0 to 2^53 (every such number is exact in a double): true and `out` set; anything else false.
    bool AsIndex(std::uint64_t& out) const {
        if (type != Type::Number || !(number >= 0.0) || number > 9007199254740992.0) return false;
        const std::uint64_t u = (std::uint64_t)number;
        if ((double)u != number) return false;
        out = u;
        return true;
    }

    // ---- Building values (for writers: the automation API's answers, okay/Automation/Automation.hpp) ----
    static Value Str(std::string s) { Value v; v.type = Type::String; v.string = std::move(s); return v; }
    static Value Num(double n) { Value v; v.type = Type::Number; v.number = n; return v; }
    static Value Boolean(bool b) { Value v; v.type = Type::Bool; v.boolean = b; return v; }
    static Value Arr() { Value v; v.type = Type::Array; return v; }
    static Value Obj() { Value v; v.type = Type::Object; return v; }
    /// Object: replace the first value under `key`, or add it at the end. A Null value becomes an object first.
    Value& Set(std::string_view key, Value val) {
        if (type == Type::Null) type = Type::Object;
        for (auto& kv : object) if (kv.first == key) { kv.second = std::move(val); return *this; }
        object.emplace_back(std::string(key), std::move(val));
        return *this;
    }
    /// Array: add `val` at the end. A Null value becomes an array first.
    Value& Push(Value val) {
        if (type == Type::Null) type = Type::Array;
        array.push_back(std::move(val));
        return *this;
    }
};

struct Limits {
    std::size_t maxBytes = 64u << 20;   ///< longer input is refused
    int maxDepth = 128;                 ///< arrays and objects open at once
};

namespace detail {

struct Reader {
    std::string_view t;
    std::size_t i = 0;
    int depth = 0;
    Limits lim;
    std::string err;

    bool Fail(const char* what) {
        if (err.empty()) err = std::string(what) + " at byte " + std::to_string(i);
        return false;
    }
    void Ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) ++i; }
    bool Lit(std::string_view word) {
        if (t.substr(i, word.size()) != word) return Fail("unknown word");
        i += word.size();
        return true;
    }
    // The four hex digits of a \u escape (from_chars reads base 16 with no sign or prefix; all four must be digits).
    bool Hex4(std::uint32_t& v) {
        if (i + 4 > t.size()) return Fail("short \\u escape");
        const auto r = std::from_chars(t.data() + i, t.data() + i + 4, v, 16);
        if (r.ec != std::errc() || r.ptr != t.data() + i + 4) return Fail("bad \\u escape");
        i += 4;
        return true;
    }
    bool String(std::string& out) {
        ++i;   // the opening quote
        out.clear();
        while (true) {
            if (i >= t.size()) return Fail("unterminated string");
            const unsigned char c = (unsigned char)t[i];
            if (c == '"') { ++i; return true; }
            if (c < 0x20) return Fail("control character in a string");
            if (c != '\\') { out += (char)c; ++i; continue; }
            if (++i >= t.size()) return Fail("unterminated escape");
            const char e = t[i++];
            switch (e) {
            case '"': out += '"'; break;   case '\\': out += '\\'; break; case '/': out += '/'; break;
            case 'b': out += '\b'; break;  case 'f': out += '\f'; break;  case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;  case 't': out += '\t'; break;
            case 'u': {
                std::uint32_t cp = 0;
                if (!Hex4(cp)) return false;
                if (cp >= 0xDC00 && cp <= 0xDFFF) return Fail("lone low surrogate");
                if (cp >= 0xD800 && cp <= 0xDBFF) {   // a high surrogate must be followed by \u and a low one (RFC 8259 section 7)
                    if (i + 2 > t.size() || t[i] != '\\' || t[i + 1] != 'u') return Fail("lone high surrogate");
                    i += 2;
                    std::uint32_t lo = 0;
                    if (!Hex4(lo)) return false;
                    if (lo < 0xDC00 || lo > 0xDFFF) return Fail("bad surrogate pair");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8::Append(out, cp);
                break;
            }
            default: return Fail("unknown escape");
            }
        }
    }
    // number = [ minus ] int [ frac ] [ exp ]; int = zero / ( digit1-9 *DIGIT ) (RFC 8259 section 6)
    bool Number(double& out) {
        const std::size_t start = i;
        if (i < t.size() && t[i] == '-') ++i;
        if (i >= t.size() || t[i] < '0' || t[i] > '9') return Fail("bad number");
        if (t[i] == '0') { ++i; if (i < t.size() && t[i] >= '0' && t[i] <= '9') return Fail("leading zero"); }
        else while (i < t.size() && t[i] >= '0' && t[i] <= '9') ++i;
        if (i < t.size() && t[i] == '.') {
            ++i;
            if (i >= t.size() || t[i] < '0' || t[i] > '9') return Fail("bad fraction");
            while (i < t.size() && t[i] >= '0' && t[i] <= '9') ++i;
        }
        if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) {
            ++i;
            if (i < t.size() && (t[i] == '+' || t[i] == '-')) ++i;
            if (i >= t.size() || t[i] < '0' || t[i] > '9') return Fail("bad exponent");
            while (i < t.size() && t[i] >= '0' && t[i] <= '9') ++i;
        }
        // The text is checked above, so from_chars (no locale, unlike strtod) reads exactly it; a number too large for a
        // double is refused rather than turned into infinity.
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
        const auto r = std::from_chars(t.data() + start, t.data() + i, out);
        if (r.ec != std::errc() || r.ptr != t.data() + i) return Fail("number out of range");
#else
        // No from_chars for double in this C++ library (Emscripten's): strtod on a copy, with '.' turned into the C
        // library's decimal point, and ERANGE refused as above.
        std::string s(t.data() + start, i - start);
        const char* dp = std::localeconv()->decimal_point;
        if (dp && dp[0] && dp[0] != '.') for (char& ch : s) if (ch == '.') ch = dp[0];
        char* end = nullptr;
        errno = 0;
        out = std::strtod(s.c_str(), &end);
        if (errno == ERANGE || end != s.c_str() + s.size()) return Fail("number out of range");
#endif
        return true;
    }
    bool Val(Value& v) {
        Ws();
        if (i >= t.size()) return Fail("missing value");
        const char c = t[i];
        if (c == '{' || c == '[') {
            if (++depth > lim.maxDepth) return Fail("nested too deeply");
            const bool obj = c == '{';
            v.type = obj ? Type::Object : Type::Array;
            ++i;
            Ws();
            const char close = obj ? '}' : ']';
            if (i < t.size() && t[i] == close) { ++i; --depth; return true; }
            while (true) {
                if (obj) {
                    Ws();
                    if (i >= t.size() || t[i] != '"') return Fail("expected a key");
                    std::string key;
                    if (!String(key)) return false;
                    Ws();
                    if (i >= t.size() || t[i] != ':') return Fail("expected ':'");
                    ++i;
                    v.object.emplace_back(std::move(key), Value());
                    if (!Val(v.object.back().second)) return false;
                } else {
                    v.array.emplace_back();
                    if (!Val(v.array.back())) return false;
                }
                Ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == close) { ++i; --depth; return true; }
                return Fail(obj ? "expected ',' or '}'" : "expected ',' or ']'");
            }
        }
        if (c == '"') { v.type = Type::String; return String(v.string); }
        if (c == 't') { v.type = Type::Bool; v.boolean = true; return Lit("true"); }
        if (c == 'f') { v.type = Type::Bool; v.boolean = false; return Lit("false"); }
        if (c == 'n') { v.type = Type::Null; return Lit("null"); }
        v.type = Type::Number;
        return Number(v.number);
    }
};

} // namespace detail

/// Read `text` as one JSON value. False (and `out` left as Null) with a reason in `error` for anything RFC 8259 does not
/// allow or a limit refuses.
inline bool Parse(std::string_view text, Value& out, std::string* error = nullptr, const Limits& limits = Limits()) {
    out = Value();
    if (text.size() > limits.maxBytes) { if (error) *error = "the JSON text is too large"; return false; }
    detail::Reader r;
    r.t = text;
    r.lim = limits;
    Value v;
    bool ok = r.Val(v);
    if (ok) { r.Ws(); if (r.i != text.size()) ok = r.Fail("extra text after the value"); }
    if (!ok) { if (error) *error = r.err; return false; }
    out = std::move(v);
    return true;
}

/// Append `s` (UTF-8) to `out` as a JSON string, with its quotes: '"' and '\\' escaped, \n, \r and \t, other control characters
/// as \u00xx (RFC 8259 section 7). Bytes at or above 0x80 are copied as they are (JSON text is UTF-8). The "JSON escape and
/// write" row of docs/engine/shared_code.md; first user: the Browser's profile (okay/Browser/Profile.hpp).
inline void Escape(std::string_view s, std::string& out) {
    static const char* const hex = "0123456789abcdef";
    out += '"';
    for (const char ch : s) {
        const unsigned char c = (unsigned char)ch;
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
            else out += ch;
        }
    }
    out += '"';
}

/// Append a number as RFC 8259 section 6 writes it: a whole number below 2^53 without a fraction ("3", "-12"), anything
/// else in the shortest form that reads back to the same double. NaN and the infinities have no JSON form: "null".
inline void WriteNumber(double n, std::string& out) {
    if (!(n == n) || n > 1.7976931348623157e308 || n < -1.7976931348623157e308) { out += "null"; return; }
    if (n == (double)(long long)n && n < 9007199254740992.0 && n > -9007199254740992.0) {
        out += std::to_string((long long)n);
        return;
    }
    char buf[40];
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    const auto r = std::to_chars(buf, buf + sizeof buf, n);
    out.append(buf, r.ptr);
#else
    // No to_chars for double here (Emscripten's library): 17 significant digits always read back the same, and the C
    // library's decimal point is turned back into '.'.
    const int len = std::snprintf(buf, sizeof buf, "%.17g", n);
    const char* dp = std::localeconv()->decimal_point;
    for (int k = 0; k < len && k < (int)sizeof buf; ++k) out += (dp && buf[k] == dp[0]) ? '.' : buf[k];
#endif
}

/// Append `v` as compact JSON text (no white space; object keys in their stored order). Parse reads it back to an equal
/// value. Nesting deeper than Limits::maxDepth is written as null, so a value Parse could not read is never produced.
/// With `indent` above 0, each element of an array or object goes on its own line, `indent` spaces deeper than its parent
/// (for files people read, such as an app's settings file); 0 writes one line.
inline void Write(const Value& v, std::string& out, int depth = 0, int indent = 0) {
    auto newline = [&](int level) { if (indent > 0) { out += '\n'; out.append((std::size_t)(level * indent), ' '); } };
    switch (v.type) {
    case Type::Null: out += "null"; return;
    case Type::Bool: out += v.boolean ? "true" : "false"; return;
    case Type::Number: WriteNumber(v.number, out); return;
    case Type::String: Escape(v.string, out); return;
    case Type::Array:
    case Type::Object: {
        if (depth >= Limits().maxDepth) { out += "null"; return; }
        const bool obj = v.type == Type::Object;
        out += obj ? '{' : '[';
        const std::size_t n = obj ? v.object.size() : v.array.size();
        for (std::size_t k = 0; k < n; ++k) {
            if (k) out += ',';
            newline(depth + 1);
            if (obj) { Escape(v.object[k].first, out); out += indent > 0 ? ": " : ":"; Write(v.object[k].second, out, depth + 1, indent); }
            else Write(v.array[k], out, depth + 1, indent);
        }
        if (n) newline(depth);
        out += obj ? '}' : ']';
        return;
    }
    }
}

inline std::string Write(const Value& v, int indent = 0) { std::string s; Write(v, s, 0, indent); return s; }

} // namespace okay::json
