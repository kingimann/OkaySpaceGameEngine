#pragma once
// Small helpers shared by the own text-mesh readers (OFF, SMD, MD5, ASE): a size-capped file read through a UTF-8 path,
// a text encoding fix (UTF-8 byte order mark dropped, UTF-16 with a byte order mark converted), a line splitter that takes
// \n, \r\n and \r, a whitespace tokenizer and strict, locale independent number parsing (NumParse.hpp). Internal: not
// installed. Nothing here reads past the buffer it is given.
#include "FileOpen.hpp"
#include "NumParse.hpp"
#include <charconv>
#include <clocale>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace okay {
namespace detail {
namespace txt {

inline bool IsWs(char c) { return c == ' ' || (unsigned char)(c - 9) < 5u; }   // ' ' and '\t' '\n' '\v' '\f' '\r' (9 to 13)

inline std::string Lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

// Whole file into `bytes`, never more than `cap` bytes.
inline bool ReadFileCapped(const std::string& path, std::size_t cap, std::string& bytes, std::string* err) {
    bytes.clear();
    std::FILE* fp = OpenRead(path);
    if (!fp) { if (err) *err = "cannot open file"; return false; }
    ReserveForFile(fp, bytes, cap);
    char buf[65536];
    for (;;) {
        const std::size_t k = std::fread(buf, 1, sizeof(buf), fp);
        if (k == 0) break;
        if (bytes.size() + k > cap) { std::fclose(fp); bytes.clear(); if (err) *err = "file too large"; return false; }
        bytes.append(buf, k);
    }
    std::fclose(fp);
    return true;
}

// Drops a UTF-8 byte order mark; converts UTF-16 (little or big endian, announced by its byte order mark) to UTF-8.
// Returns false (with `why`) for UTF-16 with an odd length, a lone surrogate, or more than `cap` bytes of output.
// Sets `converted` when the text was UTF-16. UTF-32 and UTF-16 without a mark are not detected.
inline bool NormalizeEncoding(std::string& s, bool& converted, std::string& why, std::size_t cap) {
    converted = false;
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) { s.erase(0, 3); return true; }
    if (s.size() < 2) return true;
    const unsigned char a = (unsigned char)s[0], b = (unsigned char)s[1];
    const bool le = a == 0xFF && b == 0xFE, be = a == 0xFE && b == 0xFF;
    if (!le && !be) return true;
    if (s.size() % 2) { why = "UTF-16 text with an odd number of bytes"; return false; }
    std::string out;
    out.reserve(s.size());
    auto unit = [&](std::size_t i) -> unsigned {
        const unsigned char x = (unsigned char)s[i], y = (unsigned char)s[i + 1];
        return le ? (unsigned)x | ((unsigned)y << 8) : ((unsigned)x << 8) | (unsigned)y;
    };
    for (std::size_t i = 2; i < s.size(); i += 2) {
        unsigned cp = unit(i);
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (i + 3 >= s.size()) { why = "UTF-16 text ends inside a surrogate pair"; return false; }
            const unsigned lo = unit(i + 2);
            if (lo < 0xDC00 || lo > 0xDFFF) { why = "UTF-16 text with a lone surrogate"; return false; }
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            i += 2;
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) { why = "UTF-16 text with a lone surrogate"; return false; }
        if (cp < 0x80) out.push_back((char)cp);
        else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) { out.push_back((char)(0xE0 | (cp >> 12))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
        else { out.push_back((char)(0xF0 | (cp >> 18))); out.push_back((char)(0x80 | ((cp >> 12) & 0x3F))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
        if (out.size() > cap) { why = "text too large after conversion"; return false; }
    }
    s.swap(out);
    converted = true;
    return true;
}

// Splits text into lines ('\n', '\r' and '\r\n' all end one); the line does not include its break.
struct Lines {
    const std::string* t = nullptr;
    std::size_t pos = 0;
    std::size_t number = 0;   // 1 based number of the line last returned
    explicit Lines(const std::string& text) : t(&text) {}
    // The next line as a view into the text (no copy); valid while the text is.
    bool Next(std::string_view& line) {
        const char* const b = t->data();
        const std::size_t n = t->size();
        if (pos >= n) return false;
        // the line ends at the first '\n' or '\r': find the '\n', then look for a '\r' before it
        const void* nl = std::memchr(b + pos, '\n', n - pos);
        std::size_t e = nl ? (std::size_t)((const char*)nl - b) : n;
        if (const void* cr = std::memchr(b + pos, '\r', e - pos)) e = (std::size_t)((const char*)cr - b);
        line = std::string_view(b + pos, e - pos);
        pos = e;
        if (pos < n && b[pos] == '\r') { ++pos; if (pos < n && b[pos] == '\n') ++pos; }
        else if (pos < n) ++pos;
        ++number;
        return true;
    }
    bool Next(std::string& line) {
        const std::string& s = *t;
        if (pos >= s.size()) return false;
        std::size_t e = pos;
        while (e < s.size() && s[e] != '\n' && s[e] != '\r') ++e;
        line.assign(s, pos, e - pos);
        pos = e;
        if (pos < s.size() && s[pos] == '\r') { ++pos; if (pos < s.size() && s[pos] == '\n') ++pos; }
        else if (pos < s.size()) ++pos;
        ++number;
        return true;
    }
};

// Tokens of one line. Double quotes group a token ("my file.tga" is one token, quotes dropped); '#' and "//" start a
// comment only when the caller asks for it (strip it first with StripComment).
// SplitInto fills `r` and reuses its strings and room, so a reader that splits line after line into the same vector
// does not allocate for every line.
inline void SplitInto(std::string_view s, std::vector<std::string>& r) {
    std::size_t n = 0;
    auto put = [&](std::size_t a, std::size_t len) {
        if (n < r.size()) r[n].assign(s, a, len); else r.emplace_back(s, a, len);
        ++n;
    };
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && IsWs(s[i])) ++i;
        if (i >= s.size()) break;
        if (s[i] == '"') {
            std::size_t a = ++i;
            while (i < s.size() && s[i] != '"') ++i;
            put(a, i - a);
            if (i < s.size()) ++i;
        } else {
            std::size_t a = i;
            while (i < s.size() && !IsWs(s[i])) ++i;
            put(a, i - a);
        }
    }
    r.resize(n);
}
// The same tokens as views into `s` (valid while `s` is).
inline void SplitInto(std::string_view s, std::vector<std::string_view>& r) {
    r.clear();
    // ' ' and '\t' '\n' '\v' '\f' '\r' (9 to 13): the same set as IsWs
    auto ws = [](char c) { return c == ' ' || (unsigned char)(c - 9) < 5u; };
    const char* p = s.data();
    const char* const end = p + s.size();
    while (p < end) {
        while (p < end && ws(*p)) ++p;
        if (p >= end) break;
        if (*p == '"') {
            const char* a = ++p;
            while (p < end && *p != '"') ++p;
            r.emplace_back(a, (std::size_t)(p - a));
            if (p < end) ++p;
        } else {
            const char* a = p;
            while (p < end && !ws(*p)) ++p;
            r.emplace_back(a, (std::size_t)(p - a));
        }
    }
}
inline std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> r;
    SplitInto(s, r);
    return r;
}

// A finite number, the whole token, within +-1e15. No hex, no "nan", no "inf", no trailing text, '.' always the point.
inline bool ToDouble(std::string_view s, double& v) {
    if (s.empty() || s.size() > 64) return false;
    // the common case first: a plain decimal the exact fast path reads (it accepts only characters the check below allows)
    if (FastDecimal(s.data(), s.data() + s.size(), v)) return std::isfinite(v) && std::fabs(v) <= 1e15;
    const char c = s[0];
    if (!((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.')) return false;
    for (char ch : s) if (!((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E')) return false;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    // std::from_chars reads '.' as the point and rounds correctly, as strtod does in a '.' locale: the same value. Only
    // used in such a locale and for a token it reads whole; anything else (a leading '+', out of range) goes the old way.
    if (c != '+') {
        const char* dp = std::localeconv()->decimal_point;
        if (!dp || (dp[0] == '.' && dp[1] == 0)) {
            double d = 0;
            const auto r = std::from_chars(s.data(), s.data() + s.size(), d);
            if (r.ec == std::errc() && r.ptr == s.data() + s.size()) { v = d; return std::isfinite(v) && std::fabs(v) <= 1e15; }
        }
    }
#endif
    char buf[72];   // the token is at most 64 characters and holds no NUL (checked above)
    std::memcpy(buf, s.data(), s.size());
    buf[s.size()] = '\0';
    char* e = nullptr;
    v = StrToD(buf, &e);
    return e && *e == '\0' && std::isfinite(v) && std::fabs(v) <= 1e15;
}
inline bool ToFloat(std::string_view s, float& f) { double d; if (!ToDouble(s, d)) return false; f = (float)d; return true; }

// Reads the token that starts at `p` (not white space, not a quote: the caller checks) up to the next white space or `e`,
// leaves `p` just after it, and returns exactly what ToDouble(token) returns, in one pass for the usual plain decimal: the
// FastDecimal grammar is followed with white space taken as the end of the token (so "1e " is refused here as FastDecimal
// refuses "1e"); any token it does not settle (more than 19 digits, a large exponent, other characters, more than 64
// characters) is handed to ToDouble whole.
inline bool ScanToDouble(const char*& p, const char* e, double& v) {
    const char* const a = p;
#if !(defined(FLT_EVAL_METHOD) && FLT_EVAL_METHOD != 0)
    {
        static const double kPow10[23] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                                          1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
        const char* q = a;
        const bool neg = q < e && *q == '-';
        if (neg) ++q;
        unsigned long long m = 0;
        const char* const d0 = q;
        for (unsigned d; q < e && (d = (unsigned)(unsigned char)*q - '0') <= 9; ++q) m = m * 10 + d;
        std::ptrdiff_t digits = q - d0;
        int scale = 0;
        bool ok = true;
        if (q < e && *q == '.') {
            const char* const f0 = ++q;
            for (unsigned d; q < e && (d = (unsigned)(unsigned char)*q - '0') <= 9; ++q) m = m * 10 + d;
            scale = -(int)std::min<std::ptrdiff_t>(q - f0, 1000);
            digits += q - f0;
        }
        if (digits == 0 || digits > 19) ok = false;
        if (ok && q < e && (*q == 'e' || *q == 'E')) {
            ++q;
            bool eneg = false;
            if (q < e && (*q == '-' || *q == '+')) { eneg = *q == '-'; ++q; }
            if (q >= e || IsWs(*q)) ok = false;
            int x = 0;
            for (; ok && q < e && *q >= '0' && *q <= '9'; ++q) { x = x * 10 + (*q - '0'); if (x > 1000) ok = false; }
            scale += eneg ? -x : x;
        }
        if (ok && (q >= e || IsWs(*q)) && q - a <= 64 && (m == 0 || (m <= (1ull << 53) && scale >= -22 && scale <= 22))) {
            double r = 0;
            if (m != 0) { r = (double)m; r = scale < 0 ? r / kPow10[-scale] : r * kPow10[scale]; }
            v = neg ? -r : r;
            p = q;
            return std::isfinite(v) && std::fabs(v) <= 1e15;   // as ToDouble after FastDecimal
        }
    }
#endif
    const char* q = a;
    while (q < e && !IsWs(*q)) ++q;
    p = q;
    return ToDouble(std::string_view(a, (std::size_t)(q - a)), v);
}

// A plain decimal integer (optional sign), the whole token, within +-2^31.
inline bool ToInt(std::string_view s, long long& v) {
    if (s.empty() || s.size() > 12) return false;
    std::size_t i = 0;
    bool neg = false;
    if (s[0] == '-' || s[0] == '+') { neg = s[0] == '-'; i = 1; }
    if (i >= s.size()) return false;
    long long r = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        r = r * 10 + (s[i] - '0');
    }
    if (neg) r = -r;
    if (r > 2147483647ll || r < -2147483647ll) return false;
    v = r;
    return true;
}

// Reads the tokens of one line straight from the text, for a reader's fast path: the tokens are the ones SplitInto gives
// (white space as SplitInto takes it), except that a quote ends the scan (a quoted token is the general path's job), and
// numbers are ToDouble's (ScanToDouble) and ToInt's. A reader commits nothing until the whole line has read.
struct LineScan {
    const char* p; const char* e;
    static bool Ws(char c) { return IsWs(c); }
    // the next token, or false at the end of the line or at a quote
    bool Tok(std::string_view& t) {
        while (p < e && IsWs(*p)) ++p;
        if (p >= e || *p == '"') return false;
        const char* a = p;
        while (p < e && !IsWs(*p)) ++p;
        t = std::string_view(a, (std::size_t)(p - a));
        return true;
    }
    bool End() { while (p < e && IsWs(*p)) ++p; return p >= e; }
    bool Num(double& v) { while (p < e && IsWs(*p)) ++p; return p < e && *p != '"' && ScanToDouble(p, e, v); }
    bool Flt(float& f) { double d; if (!Num(d)) return false; f = (float)d; return true; }   // as ToFloat
    bool Int(long long& v) { std::string_view t; return Tok(t) && ToInt(t, v); }
    // an integer that may end in ':' (ASE's "12:"), as ASE's IntAt reads it
    bool Index(long long& v) { std::string_view t; if (!Tok(t)) return false; if (!t.empty() && t.back() == ':') t.remove_suffix(1); return ToInt(t, v); }
};

// A vertex weld: a corner key of N 32-bit words (floats as their bits, compared bit for bit) -> the vertex made for it.
// Open addressing in one array: no allocation per vertex and no string per key (the readers used an unordered_map keyed
// by the key's bytes as a std::string; the vertices and their order do not depend on the lookup).
template <int N>
struct WeldMap {
    struct E { std::uint32_t k[N]; int v; };
    std::vector<E> t;
    std::size_t n = 0;
    static std::size_t Hash(const std::uint32_t* k) {
        std::uint64_t h = 0x9E3779B97F4A7C15ull;
        for (int i = 0; i < N; ++i) { h = (h ^ k[i]) * 0xBF58476D1CE4E5B9ull; h ^= h >> 31; }
        return (std::size_t)h;
    }
    // The vertex of `k`, or `fresh` (stored for it) when the key is new; `added` says which.
    int Get(const std::uint32_t* k, int fresh, bool& added) {
        if ((n + 1) * 2 > t.size()) Grow();
        const std::size_t mask = t.size() - 1;
        for (std::size_t i = Hash(k) & mask;; i = (i + 1) & mask) {
            E& e = t[i];
            if (e.v < 0) { std::memcpy(e.k, k, sizeof e.k); e.v = fresh; ++n; added = true; return fresh; }
            if (std::memcmp(e.k, k, sizeof e.k) == 0) { added = false; return e.v; }
        }
    }
    void Grow() {
        std::vector<E> old;
        old.swap(t);
        E empty{};
        empty.v = -1;
        t.assign(old.size() < 32 ? 64 : old.size() * 2, empty);
        const std::size_t mask = t.size() - 1;
        for (const E& e : old) if (e.v >= 0) for (std::size_t i = Hash(e.k) & mask;; i = (i + 1) & mask) if (t[i].v < 0) { t[i] = e; break; }
    }
};

// Lists bones so that every parent comes before its children. `par[i]` is the parent index or -1; a parent loop is cut at
// the bone that closes it (that bone becomes a root). `order` gets every index once. Returns the number of loops cut.
// Iterative: a 100,000-deep chain does not recurse.
inline int OrderParentsFirst(std::vector<int>& par, std::vector<int>& order) {
    const std::size_t n = par.size();
    order.clear();
    order.reserve(n);
    std::vector<char> state(n, 0);   // 0 new, 1 on the current path, 2 placed
    std::vector<int> path;
    int cut = 0;
    for (std::size_t i = 0; i < n; ++i) {
        path.clear();
        int c = (int)i;
        while (c >= 0 && (std::size_t)c < n && state[(std::size_t)c] == 0) { state[(std::size_t)c] = 1; path.push_back(c); c = par[(std::size_t)c]; }
        if (c >= 0 && (std::size_t)c < n && state[(std::size_t)c] == 1) { par[(std::size_t)path.back()] = -1; ++cut; }
        for (std::size_t k = path.size(); k-- > 0;) { state[(std::size_t)path[k]] = 2; order.push_back(path[k]); }
    }
    return cut;
}

} // namespace txt
} // namespace detail
} // namespace okay
