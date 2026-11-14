#pragma once
// Small helpers shared by the own text-mesh readers (OFF, SMD, MD5, ASE): a size-capped file read through a UTF-8 path,
// a text encoding fix (UTF-8 byte order mark dropped, UTF-16 with a byte order mark converted), a line splitter that takes
// \n, \r\n and \r, a whitespace tokenizer and strict, locale independent number parsing (NumParse.hpp). Internal: not
// installed. Nothing here reads past the buffer it is given.
#include "FileOpen.hpp"
#include "NumParse.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace okay {
namespace detail {
namespace txt {

inline bool IsWs(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

inline std::string Lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

// Whole file into `bytes`, never more than `cap` bytes.
inline bool ReadFileCapped(const std::string& path, std::size_t cap, std::string& bytes, std::string* err) {
    bytes.clear();
    std::FILE* fp = OpenRead(path);
    if (!fp) { if (err) *err = "cannot open file"; return false; }
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
inline std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> r;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && IsWs(s[i])) ++i;
        if (i >= s.size()) break;
        if (s[i] == '"') {
            std::size_t a = ++i;
            while (i < s.size() && s[i] != '"') ++i;
            r.push_back(s.substr(a, i - a));
            if (i < s.size()) ++i;
        } else {
            std::size_t a = i;
            while (i < s.size() && !IsWs(s[i])) ++i;
            r.push_back(s.substr(a, i - a));
        }
    }
    return r;
}

// A finite number, the whole token, within +-1e15. No hex, no "nan", no "inf", no trailing text, '.' always the point.
inline bool ToDouble(const std::string& s, double& v) {
    if (s.empty() || s.size() > 64) return false;
    const char c = s[0];
    if (!((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.')) return false;
    for (char ch : s) if (!((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E')) return false;
    char* e = nullptr;
    v = StrToD(s.c_str(), &e);
    return e && *e == '\0' && std::isfinite(v) && std::fabs(v) <= 1e15;
}
inline bool ToFloat(const std::string& s, float& f) { double d; if (!ToDouble(s, d)) return false; f = (float)d; return true; }

// A plain decimal integer (optional sign), the whole token, within +-2^31.
inline bool ToInt(const std::string& s, long long& v) {
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
