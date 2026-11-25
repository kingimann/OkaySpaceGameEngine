#pragma once
// Text helpers of the OkayScript library, for any value type V: format() templates with format specs, UTF-8, base64,
// hex and URL encoding. Used by the shared builtins (CoreBuiltins.hpp) and the string methods (ValueMethods.ipp), so
// format("{0:.2f}", x) and "{0:.2f}".format(x) are one piece of code.
//
// Written for this project from the specifications only: the format spec follows the shape of the common
// "[[fill]align][sign][0][width][,][.precision][type]" mini-language; base64 is RFC 4648 section 4 (with '='
// padding); percent-encoding is RFC 3986 section 2 (unreserved characters kept); UTF-8 is RFC 3629.
#include "okayscript/detail/Support.hpp"
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace okayscript {
namespace detail {

/// One value formatted with a spec ("" = as text): fill and align (<, >, ^), sign (+, space), 0 padding, width,
/// a thousands comma, precision, and the type: f (fixed), e (exponent), % (times 100, fixed), d (whole number),
/// x / X (hex), b (binary), o (octal), s (text). A spec it cannot read leaves the value as text.
template <class V> std::string FormatOne(const V& v, const std::string& spec) {
    std::string s = v.AsString();
    if (spec.empty()) return s;
    std::size_t i = 0;
    char fill = ' ', align = 0, sign = 0, type = 0;
    bool comma = false;
    int width = 0, prec = -1;
    auto isAlign = [](char c) { return c == '<' || c == '>' || c == '^'; };
    if (spec.size() >= 2 && isAlign(spec[1])) { fill = spec[0]; align = spec[1]; i = 2; }
    else if (isAlign(spec[0])) { align = spec[0]; i = 1; }
    if (i < spec.size() && (spec[i] == '+' || spec[i] == ' ' || spec[i] == '-')) sign = spec[i++];
    if (i < spec.size() && spec[i] == '0' && !align) { fill = '0'; align = '='; ++i; }
    while (i < spec.size() && std::isdigit((unsigned char)spec[i]) && width < 100000) width = width * 10 + (spec[i++] - '0');
    if (i < spec.size() && spec[i] == ',') { comma = true; ++i; }
    if (i < spec.size() && spec[i] == '.') {
        ++i;
        prec = 0;
        while (i < spec.size() && std::isdigit((unsigned char)spec[i]) && prec < 100) prec = prec * 10 + (spec[i++] - '0');
    }
    if (i < spec.size()) type = spec[i++];
    if (i != spec.size()) return s;   // not a spec: the value as it is
    if (width > 4096) width = 4096;
    if (prec > 30) prec = 30;
    const bool number = v.IsNumber() || v.IsBool();
    std::string body;
    bool negative = false;
    if (type == 's' || (!type && !number)) {
        body = s;
        if (prec >= 0 && (std::size_t)prec < body.size()) body.resize((std::size_t)prec);
    } else if (!number && type != 0) {
        body = s;   // a spec for numbers on text: the text
    } else {
        const std::int64_t* iv = IntOf(v);
        const double d = iv ? (double)*iv : (double)v.AsFloat();
        negative = d < 0 || (iv && *iv < 0);
        char buf[512];
        if (type == 'x' || type == 'X' || type == 'b' || type == 'o' || type == 'd') {
            const std::int64_t whole = iv ? *iv : SafeInt64(d);
            std::uint64_t u = whole < 0 ? (std::uint64_t)0 - (std::uint64_t)whole : (std::uint64_t)whole;
            negative = whole < 0;
            if (type == 'd') body = std::to_string(u);
            else {
                const unsigned base = type == 'b' ? 2 : type == 'o' ? 8 : 16;
                const char* digits = type == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
                if (u == 0) body = "0";
                while (u) { body.insert(body.begin(), digits[u % base]); u /= base; }
            }
        } else if (type == 'e' || type == 'E') {
            std::snprintf(buf, sizeof buf, type == 'e' ? "%.*e" : "%.*E", prec < 0 ? 6 : prec, std::fabs(d));
            body = buf;
        } else if (type == '%') {
            std::snprintf(buf, sizeof buf, "%.*f", prec < 0 ? 0 : prec, std::fabs(d) * 100.0);
            body = std::string(buf) + "%";
        } else if (type == 'f' || type == 'F' || prec >= 0) {
            if (!std::isfinite(d)) body = std::isnan(d) ? "nan" : "inf";
            else { std::snprintf(buf, sizeof buf, "%.*f", prec < 0 ? 6 : prec, std::fabs(d)); body = buf; }
        } else {
            body = s;
            if (!body.empty() && body[0] == '-') body.erase(0, 1);
        }
        if (comma) {   // 1234567.5 -> 1,234,567.5
            std::size_t end = body.find_first_not_of("0123456789");
            if (end == std::string::npos) end = body.size();
            for (std::size_t at = end; at > 3; at -= 3) body.insert(at - 3, ",");
        }
        if (negative) body.insert(body.begin(), '-');
        else if (sign == '+') body.insert(body.begin(), '+');
        else if (sign == ' ') body.insert(body.begin(), ' ');
    }
    if ((int)body.size() >= width) return body;
    const std::size_t pad = (std::size_t)width - body.size();
    if (!align) align = number ? '>' : '<';
    if (align == '<') return body + std::string(pad, fill);
    if (align == '^') return std::string(pad / 2, fill) + body + std::string(pad - pad / 2, fill);
    if (align == '=') {   // zero padding goes after the sign
        const bool signed_ = !body.empty() && (body[0] == '-' || body[0] == '+' || body[0] == ' ');
        return signed_ ? body.substr(0, 1) + std::string(pad, fill) + body.substr(1) : std::string(pad, fill) + body;
    }
    return std::string(pad, fill) + body;
}

/// format("hp={} of {}", 7, 9) and format("{0} / {1:.2f}", a, b): `{}` is the next value, `{n}` value n, either may
/// have `:spec` (FormatOne); `{{` and `}}` are braces. Anything else in braces stays as written. `args` are the
/// values after the template. Moved from the engine's format builtin (OkayScriptVM_Math.cpp), which had no specs.
template <class V> std::string FormatTemplate(const std::string& tmpl, const V* args, std::size_t n) {
    std::string out;
    std::size_t seq = 0;
    for (std::size_t i = 0; i < tmpl.size(); ++i) {
        const char ch = tmpl[i];
        if (ch == '{' && i + 1 < tmpl.size() && tmpl[i + 1] == '{') { out += '{'; ++i; continue; }
        if (ch == '}' && i + 1 < tmpl.size() && tmpl[i + 1] == '}') { out += '}'; ++i; continue; }
        if (ch != '{') { out += ch; continue; }
        const std::size_t close = tmpl.find('}', i);
        if (close == std::string::npos) { out += ch; continue; }
        const std::string inside = tmpl.substr(i + 1, close - i - 1);
        const std::size_t colon = inside.find(':');
        const std::string head = inside.substr(0, colon);
        const std::string spec = colon == std::string::npos ? std::string() : inside.substr(colon + 1);
        bool digits = !head.empty();
        for (char c : head) if (!std::isdigit((unsigned char)c)) digits = false;
        if (!head.empty() && !digits) { out += '{'; out += inside; out += '}'; i = close; continue; }   // literal
        std::size_t idx = seq;
        if (digits) {
            idx = 0;
            for (char c : head) { idx = idx * 10 + (std::size_t)(c - '0'); if (idx > (1u << 20)) break; }
        } else {
            ++seq;
        }
        if (idx < n) out += FormatOne<V>(args[idx], spec);   // an index past the values is an empty field
        CheckTextLength(out.size());
        i = close;
    }
    return out;
}

/// The UTF-8 bytes of a code point (U+FFFD for one that is not a character).
inline std::string Utf8Encode(std::uint32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp < 0x80) return std::string(1, (char)cp);
    // n bytes: a lead byte with n high bits set, then n - 1 bytes of 6 bits each, filled from the last one back.
    const int n = cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    std::string s((std::size_t)n, '\0');
    for (int k = n - 1; k > 0; --k) { s[(std::size_t)k] = (char)(0x80 | (cp & 0x3F)); cp >>= 6; }
    s[0] = (char)((0xF00u >> n) | cp);   // 2: 0xC0, 3: 0xE0, 4: 0xF0
    return s;
}
/// The code points of UTF-8 text; a byte that does not start a valid sequence reads as U+FFFD.
inline std::vector<std::uint32_t> Utf8Decode(const std::string& s) {
    std::vector<std::uint32_t> out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        std::uint32_t cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        bool ok = len > 0 && i + (std::size_t)len <= s.size();
        for (int k = 1; ok && k < len; ++k) {
            const unsigned char cc = (unsigned char)s[i + (std::size_t)k];
            if ((cc >> 6) != 2) ok = false;
            else cp = (cp << 6) | (cc & 0x3F);
        }
        if (ok && ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
                   (cp >= 0xD800 && cp <= 0xDFFF)))
            ok = false;   // overlong forms and surrogates are not characters
        if (!ok) { out.push_back(0xFFFD); ++i; continue; }
        out.push_back(cp);
        i += (std::size_t)len;
    }
    return out;
}

inline std::string Base64Encode(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";   // dup-allow: okayscript_lang may not depend on the engine (okayscript_no_engine), so it cannot use the engine's copy
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const std::uint32_t n = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8) | (unsigned char)in[i + 2];
        out += t[(n >> 18) & 63]; out += t[(n >> 12) & 63]; out += t[(n >> 6) & 63]; out += t[n & 63];
    }
    if (i + 1 == in.size()) {
        const std::uint32_t n = (unsigned char)in[i] << 16;
        out += t[(n >> 18) & 63]; out += t[(n >> 12) & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        const std::uint32_t n = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8);
        out += t[(n >> 18) & 63]; out += t[(n >> 12) & 63]; out += t[(n >> 6) & 63]; out += '=';
    }
    return out;
}
/// False when `in` is not base64 (spaces and line breaks are skipped).
inline bool Base64Decode(const std::string& in, std::string& out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';   // dup-allow: okayscript_lang may not depend on the engine (okayscript_no_engine), so it cannot use the engine's copy
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;   // '-' and '_': the URL-safe alphabet (RFC 4648 section 5)
        if (c == '/' || c == '_') return 63;
        return -1;
    };
    out.clear();
    std::uint32_t acc = 0;
    int bits = 0, pad = 0;
    for (char c : in) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        if (c == '=') { ++pad; continue; }
        const int v = val(c);
        if (v < 0 || pad) return false;
        acc = (acc << 6) | (std::uint32_t)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out += (char)((acc >> bits) & 0xFF); }
    }
    return pad <= 2;
}

inline std::string HexEncode(const std::string& in) {
    static const char* d = "0123456789abcdef";
    std::string out;
    for (unsigned char c : in) { out += d[c >> 4]; out += d[c & 15]; }
    return out;
}
inline int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
/// False when `in` is not hex pairs.
inline bool HexDecode(const std::string& in, std::string& out) {
    out.clear();
    if (in.size() % 2) return false;
    for (std::size_t i = 0; i < in.size(); i += 2) {
        const int a = HexDigit(in[i]), b = HexDigit(in[i + 1]);
        if (a < 0 || b < 0) return false;
        out += (char)(a * 16 + b);
    }
    return true;
}
/// Percent-encoding: letters, digits and - . _ ~ stay; every other byte is %XX.
inline std::string UrlEncode(const std::string& in) {
    static const char* d = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : in) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') out += (char)c;
        else { out += '%'; out += d[c >> 4]; out += d[c & 15]; }
    }
    return out;
}
/// %XX back to its byte, and '+' to a space (form encoding); a '%' not followed by two hex digits stays.
inline std::string UrlDecode(const std::string& in) {
    std::string out;
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            const int a = HexDigit(in[i + 1]), b = HexDigit(in[i + 2]);
            if (a >= 0 && b >= 0) { out += (char)(a * 16 + b); i += 2; continue; }
        }
        out += in[i] == '+' ? ' ' : in[i];
    }
    return out;
}

} // namespace detail
} // namespace okayscript
