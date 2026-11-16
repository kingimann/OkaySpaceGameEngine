// okay::fbx layer 1: inflate + binary / ASCII FBX element tree. See FbxReader.hpp.
// Written from the public description of the format; no third-party code.
#include "NumParse.hpp"
#include "okay/Core/Inflate.hpp"
#include "okay/Render/FbxReader.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <type_traits>

namespace okay {
namespace fbx {

const Elem* Elem::Find(const char* n) const {
    for (const Elem& c : children) if (c.name == n) return &c;
    return nullptr;
}

// ============================================================================
// Inflate (RFC 1950 wrapper around RFC 1951 deflate)
// ============================================================================
// The decoder is the engine's one inflate (okay/Core/Inflate.hpp). Binary FBX arrays keep the forgiving
// behaviour they always had: the Adler-32 and bytes after the stream are not checked (Options::Lenient).
bool Inflate(const std::uint8_t* data, std::size_t n, std::vector<std::uint8_t>& out, std::size_t maxOut) {
    std::string why;
    // The caller knows the exact decoded size (the array header says it) and passes it as the cap: allocate it once
    // instead of growing the buffer by doubling (fewer copies, and no zero-filled slack).
    return okay::inflate::Zlib(data, n, out, okay::inflate::Options::Lenient(maxOut, maxOut), why);
}

// ============================================================================
// Binary FBX
// ============================================================================
namespace {

// Reads element k of a little-endian array of T stored at an unaligned address, as a double.
template <class T> struct LoadIter {
    using iterator_category = std::random_access_iterator_tag;
    using value_type = double; using difference_type = std::ptrdiff_t; using pointer = const double*; using reference = double;
    const std::uint8_t* p;
    double operator*() const {
        if constexpr (std::is_same_v<T, bool>) return *p != 0 ? 1.0 : 0.0;
        else { T x; std::memcpy(&x, p, sizeof(T)); return (double)x; }
    }
    static constexpr std::size_t kSize = std::is_same_v<T, bool> ? 1 : sizeof(T);
    LoadIter& operator++() { p += kSize; return *this; }
    LoadIter operator++(int) { LoadIter r = *this; p += kSize; return r; }
    LoadIter& operator--() { p -= kSize; return *this; }
    LoadIter operator--(int) { LoadIter r = *this; p -= kSize; return r; }
    LoadIter& operator-=(difference_type n) { p -= n * (difference_type)kSize; return *this; }
    LoadIter operator-(difference_type n) const { LoadIter r = *this; r -= n; return r; }
    LoadIter& operator+=(difference_type n) { p += n * (difference_type)kSize; return *this; }
    LoadIter operator+(difference_type n) const { LoadIter r = *this; r += n; return r; }
    difference_type operator-(const LoadIter& o) const { return (p - o.p) / (difference_type)kSize; }
    double operator[](difference_type n) const { return *(*this + n); }
    bool operator==(const LoadIter& o) const { return p == o.p; }
    bool operator!=(const LoadIter& o) const { return p != o.p; }
    bool operator<(const LoadIter& o) const { return p < o.p; }
    bool operator>(const LoadIter& o) const { return p > o.p; }
    bool operator<=(const LoadIter& o) const { return p <= o.p; }
    bool operator>=(const LoadIter& o) const { return p >= o.p; }
};
template <class T> void Fill(std::vector<double>& out, const std::uint8_t* src, std::uint32_t cnt) {
    const LoadIter<T> b{src};
    out.assign(b, b + (std::ptrdiff_t)cnt);
}

struct Reader {
    const std::uint8_t* p; std::size_t n; const Limits* lim; Document* doc;
    std::size_t arrayBytes = 0; std::string err;
    std::uint32_t version = 0;

    bool Fail(const char* m) { if (err.empty()) err = m; return false; }
    template <class T> bool Get(std::size_t at, T& v) const {
        if (at > n || n - at < sizeof(T)) return false;
        std::memcpy(&v, p + at, sizeof(T));   // little endian host assumed (all targets are)
        return true;
    }

    bool ReadProp(std::size_t& pos, std::size_t limit, Prop& out) {
        if (pos >= limit) return Fail("property list overruns its record");
        const char t = (char)p[pos++];
        out.type = t;
        auto need = [&](std::size_t k) { return limit - pos >= k && pos <= limit; };
        switch (t) {
            case 'Y': { std::int16_t v; if (!need(2)) return Fail("short property"); std::memcpy(&v, p + pos, 2); pos += 2; out.i = v; return true; }
            case 'C': { if (!need(1)) return Fail("short property"); out.i = p[pos++] != 0; return true; }
            case 'I': { std::int32_t v; if (!need(4)) return Fail("short property"); std::memcpy(&v, p + pos, 4); pos += 4; out.i = v; return true; }
            case 'F': { float v; if (!need(4)) return Fail("short property"); std::memcpy(&v, p + pos, 4); pos += 4; out.d = v; return true; }
            case 'D': { double v; if (!need(8)) return Fail("short property"); std::memcpy(&v, p + pos, 8); pos += 8; out.d = v; return true; }
            case 'L': { std::int64_t v; if (!need(8)) return Fail("short property"); std::memcpy(&v, p + pos, 8); pos += 8; out.i = v; return true; }
            case 'S': case 'R': {
                std::uint32_t len; if (!need(4)) return Fail("short property");
                std::memcpy(&len, p + pos, 4); pos += 4;
                if (len > lim->maxStringBytes || !need(len)) return Fail("string runs past its record");
                out.s.assign((const char*)p + pos, len); pos += len;
                return true;
            }
            case 'f': case 'd': case 'l': case 'i': case 'b': case 'c': {
                std::uint32_t cnt, enc, clen;
                if (!need(12)) return Fail("short array header");
                std::memcpy(&cnt, p + pos, 4); std::memcpy(&enc, p + pos + 4, 4); std::memcpy(&clen, p + pos + 8, 4);
                pos += 12;
                const std::size_t esz = (t == 'd' || t == 'l') ? 8 : ((t == 'b' || t == 'c') ? 1 : 4);
                const std::size_t raw = (std::size_t)cnt * esz;               // size_t is 64-bit here; cnt < 2^32 so no overflow
                if (sizeof(std::size_t) < 8 && (std::size_t)cnt > (~(std::size_t)0) / esz) return Fail("array too large");
                if (raw > lim->maxArrayBytes || arrayBytes + raw > lim->maxArrayBytes) return Fail("arrays exceed the size limit");
                if (!need(clen)) return Fail("array data runs past its record");
                const std::uint8_t* src = p + pos;
                std::vector<std::uint8_t> tmp;
                if (enc == 0) {
                    if (clen != raw) return Fail("array length mismatch");
                } else if (enc == 1) {
                    if (!Inflate(src, clen, tmp, raw) || tmp.size() != raw) return Fail("bad compressed array");
                    src = tmp.data();
                } else {
                    return Fail("unknown array encoding");
                }
                arrayBytes += raw;
                // One allocation at the final size, each element converted as it is constructed (no zero-filled
                // array that is then overwritten).
                switch (t) {
                    case 'd': Fill<double>(out.arr, src, cnt); break;
                    case 'f': Fill<float>(out.arr, src, cnt); break;
                    case 'l': Fill<std::int64_t>(out.arr, src, cnt); break;
                    case 'i': Fill<std::int32_t>(out.arr, src, cnt); break;
                    case 'c': Fill<std::uint8_t>(out.arr, src, cnt); break;   // byte array (written by newer SDKs, e.g. thumbnails)
                    default:  Fill<bool>(out.arr, src, cnt); break;           // 'b': any non-zero byte is true
                }
                out.type = 'a';
                pos += clen;
                return true;
            }
            default: return Fail("unknown property type");
        }
    }

    // Reads one record at `pos`. Returns 1 = element read, 0 = null terminator, -1 = error.
    int ReadElem(std::size_t& pos, std::size_t parentEnd, Elem& e, std::size_t depth) {
        if (depth > lim->maxDepth) { Fail("element nesting too deep"); return -1; }
        const bool wide = version >= 7500;
        const std::size_t hdr = wide ? 25 : 13;
        std::uint64_t endOff = 0, nProps = 0, propLen = 0;
        if (parentEnd < pos || parentEnd - pos < hdr) { Fail("truncated record header"); return -1; }
        if (wide) { Get(pos, endOff); Get(pos + 8, nProps); Get(pos + 16, propLen); }
        else { std::uint32_t a = 0, b = 0, c = 0; Get(pos, a); Get(pos + 4, b); Get(pos + 8, c); endOff = a; nProps = b; propLen = c; }
        const std::uint8_t nameLen = p[pos + hdr - 1];
        if (endOff == 0 && nProps == 0 && propLen == 0 && nameLen == 0) { pos += hdr; return 0; }
        if (endOff > parentEnd || endOff < pos + hdr + nameLen) { Fail("record end offset out of range"); return -1; }
        if (++doc->elementCount > lim->maxElements) { Fail("too many elements"); return -1; }
        std::size_t q = pos + hdr;
        e.name.assign((const char*)p + q, nameLen); q += nameLen;
        if (propLen > endOff - q) { Fail("property list longer than the record"); return -1; }
        if (nProps > propLen) { Fail("more properties than bytes"); return -1; }   // each property takes at least 1 byte
        const std::size_t propEnd = q + (std::size_t)propLen;
        e.props.reserve((std::size_t)nProps);
        for (std::uint64_t k = 0; k < nProps; ++k) {
            Prop pr;
            if (!ReadProp(q, propEnd, pr)) return -1;
            e.props.push_back(std::move(pr));
        }
        if (q != propEnd) { Fail("property list size mismatch"); return -1; }
        q = propEnd;
        while (q < endOff) {
            Elem c;
            const int r = ReadElem(q, (std::size_t)endOff, c, depth + 1);
            if (r < 0) return -1;
            if (r == 0) break;
            e.children.push_back(std::move(c));
        }
        pos = (std::size_t)endOff;
        return 1;
    }
};

} // namespace

bool ParseBinary(const std::uint8_t* data, std::size_t size, Document& out, std::string* err, const Limits& lim) {
    out = Document();
    static const char magic[] = "Kaydara FBX Binary  ";
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    if (!data || size < 27 || std::memcmp(data, magic, 20) != 0 || data[20] != 0) return fail("not a binary FBX file");
    Reader r{data, size, &lim, &out, 0, {}, 0};
    std::memcpy(&r.version, data + 23, 4);
    out.version = r.version;
    out.binary = true;
    if (r.version < 7100 || r.version > 9999) return fail("unsupported FBX version " + std::to_string(r.version));
    std::size_t pos = 27;
    while (pos < size) {
        // The file ends with a null record then a footer; stop at the null record.
        Elem e;
        const int rc = r.ReadElem(pos, size, e, 1);
        if (rc < 0) return fail(r.err);
        if (rc == 0) break;
        out.root.children.push_back(std::move(e));
    }
    return true;
}

// ============================================================================
// ASCII FBX
// ============================================================================
namespace {

// Exact fast path for the plain decimal numbers that fill ASCII arrays ("12", "-0.5", "1.25e-3"). When the digits form
// an integer below 2^53 and the power of ten is at most 22, both are exact doubles, so ONE multiplication or division
// gives the correctly rounded result (the classic observation in W. D. Clinger, "How to read floating point numbers
// accurately", 1990), which is the value strtod returns. Anything else (more digits, a larger exponent, "inf", hex,
// a malformed token) returns false and the caller uses strtod as before. No locale is involved: the point is '.'.
bool FastDecimal(const char* p, std::size_t n, double& out) {
    static const double kPow10[23] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
                                      1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    std::size_t i = 0;
    bool neg = false;
    if (i < n && (p[i] == '+' || p[i] == '-')) { neg = p[i] == '-'; ++i; }
    std::uint64_t m = 0; int digits = 0, scale = 0; bool any = false;
    while (i < n && p[i] >= '0' && p[i] <= '9') {
        any = true;
        if (m || p[i] != '0') { if (++digits > 15) return false; m = m * 10 + (std::uint64_t)(p[i] - '0'); }
        ++i;
    }
    if (i < n && p[i] == '.') {
        ++i;
        while (i < n && p[i] >= '0' && p[i] <= '9') {
            any = true;
            if (m || p[i] != '0') { if (++digits > 15) return false; m = m * 10 + (std::uint64_t)(p[i] - '0'); }
            --scale; ++i;
        }
    }
    if (!any) return false;
    if (i < n && (p[i] == 'e' || p[i] == 'E')) {
        ++i;
        bool eneg = false;
        if (i < n && (p[i] == '+' || p[i] == '-')) { eneg = p[i] == '-'; ++i; }
        if (i >= n || p[i] < '0' || p[i] > '9') return false;
        int e = 0;
        while (i < n && p[i] >= '0' && p[i] <= '9') { if (e > 10000) return false; e = e * 10 + (p[i] - '0'); ++i; }
        scale += eneg ? -e : e;
    }
    if (i != n) return false;
    double v = (double)m;   // exact: m < 10^15 < 2^53
    if (m != 0) {
        if (scale < -22 || scale > 22) return false;
        v = scale < 0 ? v / kPow10[-scale] : v * kPow10[scale];
    }
    out = neg ? -v : v;
    return true;
}

struct Text {
    const char* s; std::size_t n, pos = 0; const Limits* lim; Document* doc; std::string err;
    std::size_t arrayBytes = 0;
    bool Fail(const char* m) { if (err.empty()) err = m; return false; }
    bool Eof() const { return pos >= n; }
    char Cur() const { return pos < n ? s[pos] : '\0'; }
    void SkipSpace(bool newlines) {
        while (pos < n) {
            const char c = s[pos];
            if (c == ' ' || c == '\t' || c == '\r') ++pos;
            else if (c == '\n') { if (!newlines) return; ++pos; }
            else if (c == ';') { while (pos < n && s[pos] != '\n') ++pos; }   // comment
            else return;
        }
    }
    static bool IsIdent(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '|' || c == '-' || c == '.' || c == '+'; }

    bool ReadNumberOrIdent(Prop& out) {
        const std::size_t b = pos;
        while (pos < n && IsIdent(s[pos])) ++pos;
        if (pos == b) return Fail("unexpected character in ASCII FBX");
        std::string tok(s + b, pos - b);
        char* end = nullptr;
        const double d = detail::StrToD(tok.c_str(), &end);
        if (end && *end == '\0' && !tok.empty() && (std::isdigit((unsigned char)tok[0]) || tok[0] == '-' || tok[0] == '+' || tok[0] == '.')) {
            if (tok.find_first_of(".eE") == std::string::npos) {
                out.type = 'L'; out.i = std::strtoll(tok.c_str(), nullptr, 10);
            } else { out.type = 'D'; out.d = d; }
        } else { out.type = 'S'; out.s = tok; }   // bare identifier (T, W, Y ...)
        return true;
    }
    bool ReadString(Prop& out) {
        ++pos;   // opening quote
        const std::size_t b = pos;
        while (pos < n && s[pos] != '"') ++pos;
        if (pos >= n) return Fail("unterminated string");
        if (pos - b > lim->maxStringBytes) return Fail("string too long");
        out.type = 'S'; out.s.assign(s + b, pos - b);
        ++pos;
        return true;
    }
    bool ReadArrayBlock(Prop& out) {   // after '*': "N { a: v,v,v }"
        ++pos;
        std::size_t cnt = 0;
        while (pos < n && s[pos] >= '0' && s[pos] <= '9') { cnt = cnt * 10 + (std::size_t)(s[pos] - '0'); ++pos; if (cnt > (std::size_t)1 << 40) return Fail("array too large"); }
        SkipSpace(true);
        if (Cur() != '{') return Fail("expected '{' after array size");
        ++pos; SkipSpace(true);
        if (Cur() != 'a') return Fail("expected 'a:' in array block");
        ++pos; SkipSpace(true);
        if (Cur() != ':') return Fail("expected 'a:' in array block");
        ++pos;
        if (cnt * 8 > lim->maxArrayBytes || arrayBytes + cnt * 8 > lim->maxArrayBytes) return Fail("arrays exceed the size limit");
        out.type = 'a';
        out.arr.reserve(cnt < (1u << 20) ? cnt : (1u << 20));
        for (;;) {
            SkipSpace(true);
            if (Cur() == '}') { ++pos; break; }
            if (Eof()) return Fail("unterminated array block");
            if (Cur() == ',') { ++pos; continue; }
            const std::size_t b = pos;
            while (pos < n && (IsIdent(s[pos]))) ++pos;
            if (pos == b) return Fail("bad value in array block");
            double d;
            if (!FastDecimal(s + b, pos - b, d)) {
                const std::string tok(s + b, pos - b);
                char* end = nullptr;
                d = detail::StrToD(tok.c_str(), &end);
                if (!end || *end) return Fail("bad number in array block");
            }
            out.arr.push_back(d);
            if (out.arr.size() > lim->maxArrayBytes / 8) return Fail("arrays exceed the size limit");
        }
        arrayBytes += out.arr.size() * 8;
        return true;
    }

    bool ReadElem(Elem& e, std::size_t depth) {
        if (depth > lim->maxDepth) return Fail("element nesting too deep");
        if (++doc->elementCount > lim->maxElements) return Fail("too many elements");
        const std::size_t b = pos;
        while (pos < n && IsIdent(s[pos])) ++pos;
        if (pos == b || Cur() != ':') return Fail("expected 'Name:' in ASCII FBX");
        e.name.assign(s + b, pos - b);
        ++pos;
        for (;;) {
            SkipSpace(false);
            const char c = Cur();
            if (Eof() || c == '\n') break;
            if (c == '{') break;
            if (c == '}') break;
            if (c == ',') { ++pos; SkipSpace(true); continue; }   // a comma continues onto the next line
            Prop pr;
            if (c == '"') { if (!ReadString(pr)) return false; }
            else if (c == '*') { if (!ReadArrayBlock(pr)) return false; }
            else if (!ReadNumberOrIdent(pr)) return false;
            e.props.push_back(std::move(pr));
            if (e.props.size() > lim->maxArrayBytes / 8) return Fail("too many properties");
        }
        SkipSpace(true);
        if (Cur() == '{') {
            ++pos;
            for (;;) {
                SkipSpace(true);
                if (Eof()) return Fail("unterminated '{' block");
                if (Cur() == '}') { ++pos; break; }
                Elem c;
                if (!ReadElem(c, depth + 1)) return false;
                e.children.push_back(std::move(c));
            }
        }
        return true;
    }
};

} // namespace

bool ParseAscii(const std::uint8_t* data, std::size_t size, Document& out, std::string* err, const Limits& lim) {
    out = Document();
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    if (!data && size) return fail("no data");
    Text t{(const char*)data, size, 0, &lim, &out, {}, 0};
    // "; FBX 7.3.0 project file" in the first comment line gives the version.
    if (size > 6 && std::memcmp(data, "; FBX ", 6) == 0) {
        // The buffer is not NUL-terminated: scan a bounded, terminated copy of the
        // first line's start, never the caller's bytes.
        char hdr[33];
        const std::size_t n = std::min<std::size_t>(size - 6, sizeof(hdr) - 1);
        std::memcpy(hdr, data + 6, n); hdr[n] = '\0';
        int a = 0, b = 0;
        // Range-check before the arithmetic: a long digit run would overflow it.
        if (std::sscanf(hdr, "%d.%d", &a, &b) == 2 && a >= 0 && a <= 99999 && b >= 0 && b <= 9999)
            out.version = (std::uint32_t)(a * 1000 + b * 100);
    }
    for (;;) {
        t.SkipSpace(true);
        if (t.Eof()) break;
        Elem e;
        if (!t.ReadElem(e, 1)) return fail(t.err);
        out.root.children.push_back(std::move(e));
    }
    return true;
}

bool Parse(const std::uint8_t* data, std::size_t size, Document& out, std::string* err, const Limits& lim) {
    if (data && size >= 20 && std::memcmp(data, "Kaydara FBX Binary", 18) == 0) return ParseBinary(data, size, out, err, lim);
    return ParseAscii(data, size, out, err, lim);
}

} // namespace fbx
} // namespace okay
