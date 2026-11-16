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
    const std::string_view key(n);
    for (const Elem& c : children) if (c.name == key) return &c;
    return nullptr;
}

namespace {
constexpr std::uint32_t kNone = 0xFFFFFFFFu;

// Appends element `child` to the children of `parent` (parent == kNone: the root), keeping file order.
struct Linker {
    std::vector<Elem>& elems; Elem& root;
    void Link(std::uint32_t parent, std::uint32_t& lastChild, std::uint32_t child) {
        if (lastChild == kNone) (parent == kNone ? root : elems[parent]).firstChild_ = child;
        else elems[lastChild].nextSibling_ = child;
        lastChild = child;
    }
};

// Turns the indices recorded while parsing into the pointers of the public tree. Run once, after the last element:
// before that the flat arrays may still move.
void Fixup(Document& d) {
    Elem* const E = d.elems.data();
    auto at = [&](std::uint32_t i) -> const Elem* { return i == kNone ? nullptr : E + i; };
    for (Elem& e : d.elems) {
        e.props.p = e.props.n ? d.props.data() + e.propBegin_ : nullptr;
        e.children.first = at(e.firstChild_);
        e.next = at(e.nextSibling_);
    }
    d.root.children.first = at(d.root.firstChild_);
}
} // namespace

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
                out.s = std::string_view((const char*)p + pos, len); pos += len;   // a view, not a copy
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
                if (enc == 0) {
                    if (clen != raw) return Fail("array length mismatch");
                } else if (enc == 1) {
                    std::vector<std::uint8_t> tmp;
                    if (!Inflate(src, clen, tmp, raw) || tmp.size() != raw) return Fail("bad compressed array");
                    src = tmp.data();   // the vector's buffer does not move when the vector itself is moved below
                    doc->inflated.push_back(std::move(tmp));
                } else {
                    return Fail("unknown array encoding");
                }
                arrayBytes += raw;
                // Kept as the file stores it (no conversion): a view into the file, or into the inflated buffer.
                out.arr.type = t; out.arr.count = cnt; out.arr.data = src;
                out.type = 'a';
                pos += clen;
                return true;
            }
            default: return Fail("unknown property type");
        }
    }

    // Reads one record at `pos` into a new element of doc->elems (its index in `idx`).
    // Returns 1 = element read, 0 = null terminator, -1 = error.
    int ReadElem(std::size_t& pos, std::size_t parentEnd, std::size_t depth, std::uint32_t& idx) {
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
        idx = (std::uint32_t)doc->elems.size();
        doc->elems.emplace_back();
        doc->elems[idx].name = std::string_view((const char*)p + q, nameLen); q += nameLen;
        if (propLen > endOff - q) { Fail("property list longer than the record"); return -1; }
        if (nProps > propLen) { Fail("more properties than bytes"); return -1; }   // each property takes at least 1 byte
        const std::size_t propEnd = q + (std::size_t)propLen;
        const std::size_t firstProp = doc->props.size();
        for (std::uint64_t k = 0; k < nProps; ++k) {
            doc->props.emplace_back();
            if (!ReadProp(q, propEnd, doc->props.back())) return -1;
        }
        if (q != propEnd) { Fail("property list size mismatch"); return -1; }
        doc->elems[idx].propBegin_ = (std::uint32_t)firstProp;
        doc->elems[idx].props.n = (std::uint32_t)nProps;
        q = propEnd;
        std::uint32_t last = kNone;
        Linker link{doc->elems, doc->root};
        while (q < endOff) {
            std::uint32_t c = kNone;
            const int r = ReadElem(q, (std::size_t)endOff, depth + 1, c);
            if (r < 0) return -1;
            if (r == 0) break;
            link.Link(idx, last, c);
        }
        pos = (std::size_t)endOff;
        return 1;
    }
};

// Counts the records (and the properties they declare) by walking the record headers only, so the flat arrays can be
// allocated once at their final size: growing them by doubling copied everything and touched about twice the memory
// (fresh memory costs time). Damaged or hostile headers just stop the count early: the real parse below reports them.
void CountRecords(const std::uint8_t* p, std::size_t n, std::uint32_t version, std::size_t pos, std::size_t end, std::size_t depth,
                  const Limits& lim, std::size_t& elems, std::size_t& props) {
    const bool wide = version >= 7500;
    const std::size_t hdr = wide ? 25 : 13;
    while (pos < end && end - pos >= hdr && depth <= lim.maxDepth && elems <= lim.maxElements) {
        std::uint64_t endOff = 0, nProps = 0, propLen = 0;
        if (wide) { std::memcpy(&endOff, p + pos, 8); std::memcpy(&nProps, p + pos + 8, 8); std::memcpy(&propLen, p + pos + 16, 8); }
        else { std::uint32_t a, b, c; std::memcpy(&a, p + pos, 4); std::memcpy(&b, p + pos + 4, 4); std::memcpy(&c, p + pos + 8, 4); endOff = a; nProps = b; propLen = c; }
        const std::uint8_t nameLen = p[pos + hdr - 1];
        if (endOff == 0 && nProps == 0 && propLen == 0 && nameLen == 0) return;   // null record: end of this level
        if (endOff > end || endOff < pos + hdr + nameLen || propLen > endOff - (pos + hdr + nameLen) || nProps > propLen) return;
        ++elems; props += (std::size_t)nProps;
        const std::size_t children = pos + hdr + nameLen + (std::size_t)propLen;
        if (children < endOff) CountRecords(p, n, version, children, (std::size_t)endOff, depth + 1, lim, elems, props);
        pos = (std::size_t)endOff;
    }
}

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
    {
        std::size_t ne = 0, np = 0;
        CountRecords(data, size, r.version, 27, size, 1, lim, ne, np);
        out.elems.reserve(std::min(ne, lim.maxElements + 1));
        out.props.reserve(std::min(np, size / 2));   // a property takes at least 2 bytes
    }
    std::uint32_t last = kNone;
    Linker link{out.elems, out.root};
    bool ok = true;
    while (pos < size) {
        // The file ends with a null record then a footer; stop at the null record.
        std::uint32_t idx = kNone;
        const int rc = r.ReadElem(pos, size, 1, idx);
        if (rc < 0) { ok = false; break; }
        if (rc == 0) break;
        link.Link(kNone, last, idx);
    }
    Fixup(out);
    if (!ok) return fail(r.err);
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

// The same conversion as FastDecimal, but it finds the end of the token itself (the first character that cannot
// be part of a token), so an array value is read in one pass instead of a scan for its end and then a parse.
// Returns the end of the token, or nullptr when FastDecimal would not take it (the caller then uses the general
// path, which gives the same result as before for everything).
inline bool IdentChar(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '|' || c == '-' || c == '.' || c == '+';
}
struct IdentTable {
    bool t[256];
    IdentTable() : t{} { for (int c = 0; c < 256; ++c) t[c] = IdentChar((unsigned char)c); }
};
const IdentTable kIdent;

// Eight ASCII bytes at once (SWAR: plain 64-bit integer arithmetic). EightDigits tells whether all eight are '0'..'9'
// (each byte is checked to be >= '0' by a subtraction and <= '9' by an addition whose high bit would show otherwise);
// EightValue turns them into their value, first character most significant, by combining neighbours in three
// multiply-and-shift steps (pairs, then fours, then the eight). Worked out here; FastDecimalScan was checked against
// FastDecimal + strtod on millions of random tokens, and the digest of every ASCII file is unchanged.
inline std::uint64_t Load8(const char* p) { std::uint64_t w; std::memcpy(&w, p, 8); return w; }   // little endian
inline bool EightDigits(std::uint64_t w) {
    return (((w - 0x3030303030303030ull) | (w + 0x4646464646464646ull)) & 0x8080808080808080ull) == 0;
}
inline std::uint64_t EightValue(std::uint64_t w) {
    w -= 0x3030303030303030ull;                                    // bytes 0..9
    w = ((w * (10 * 256 + 1)) >> 8) & 0x00FF00FF00FF00FFull;        // 4 numbers 0..99
    w = ((w * (100 * 65536 + 1)) >> 16) & 0x0000FFFF0000FFFFull;    // 2 numbers 0..9999
    return (w * (10000ull * 4294967296ull + 1)) >> 32;             // 0..99999999
}

const char* FastDecimalScan(const char* p, const char* end, double& out) {
    static const double kPow10[23] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
                                      1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    // m holds the value of all digits so far (integer and fraction digits joined). FastDecimal refuses more than 15
    // significant digits; m >= 10^15 is the same test (m has exactly as many digits as there are significant ones),
    // and m never grows past 10^15 * 10 + 9 because the test runs after every digit.
    const std::uint64_t kMax = 1000000000000000ull;
    bool neg = false;
    if (p < end && (*p == '+' || *p == '-')) { neg = *p == '-'; ++p; }
    std::uint64_t m = 0; int scale = 0; bool any = false;
    while (end - p >= 8 && EightDigits(Load8(p))) {
        if (m >= 10000000ull) return nullptr;
        m = m * 100000000ull + EightValue(Load8(p)); any = true; p += 8;
        if (m >= kMax) return nullptr;
    }
    while (p < end && (unsigned)(*p - '0') < 10u) {
        any = true;
        m = m * 10 + (std::uint64_t)(*p - '0'); ++p;
        if (m >= kMax) return nullptr;
    }
    if (p < end && *p == '.') {
        ++p;
        while (end - p >= 8 && EightDigits(Load8(p))) {
            if (m >= 10000000ull) return nullptr;
            m = m * 100000000ull + EightValue(Load8(p)); any = true; p += 8; scale -= 8;
            if (m >= kMax) return nullptr;
        }
        while (p < end && (unsigned)(*p - '0') < 10u) {
            any = true;
            m = m * 10 + (std::uint64_t)(*p - '0'); ++p; --scale;
            if (m >= kMax) return nullptr;
        }
    }
    if (!any) return nullptr;
    if (p < end && (*p == 'e' || *p == 'E')) {
        ++p;
        bool eneg = false;
        if (p < end && (*p == '+' || *p == '-')) { eneg = *p == '-'; ++p; }
        if (p >= end || (unsigned)(*p - '0') >= 10u) return nullptr;
        int e = 0;
        while (p < end && (unsigned)(*p - '0') < 10u) { if (e > 10000) return nullptr; e = e * 10 + (*p - '0'); ++p; }
        scale += eneg ? -e : e;
    }
    if (p < end && kIdent.t[(unsigned char)*p]) return nullptr;   // the token goes on: not a plain decimal
    double v = (double)m;
    if (m != 0) {
        if (scale < -22 || scale > 22) return nullptr;
        v = scale < 0 ? v / kPow10[-scale] : v * kPow10[scale];
    }
    out = neg ? -v : v;
    return p;
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
    static bool IsIdent(char c) { return kIdent.t[(unsigned char)c]; }

    bool ReadNumberOrIdent(Prop& out) {
        const std::size_t b = pos;
        while (pos < n && IsIdent(s[pos])) ++pos;
        if (pos == b) return Fail("unexpected character in ASCII FBX");
        const char* t = s + b; const std::size_t len = pos - b;
        // Fast paths, with the same result as the general rule below: a plain integer of up to 18 digits (what strtoll
        // returns), and a plain decimal with a point or an exponent that FastDecimal converts exactly (what strtod
        // returns).
        {
            std::size_t i = 0; bool neg = false;
            if (t[0] == '+' || t[0] == '-') { neg = t[0] == '-'; i = 1; }
            if (i < len && len - i <= 18) {
                std::int64_t v = 0; std::size_t k = i;
                while (k < len && t[k] >= '0' && t[k] <= '9') { v = v * 10 + (t[k] - '0'); ++k; }
                if (k == len) { out.type = 'L'; out.i = neg ? -v : v; return true; }
            }
            bool pointOrExp = false;
            for (std::size_t k = 0; k < len; ++k) if (t[k] == '.' || t[k] == 'e' || t[k] == 'E') { pointOrExp = true; break; }
            double d;
            if (pointOrExp && FastDecimal(t, len, d)) { out.type = 'D'; out.d = d; return true; }
        }
        std::string tok(t, len);
        char* end = nullptr;
        const double d = detail::StrToD(tok.c_str(), &end);
        if (end && *end == '\0' && !tok.empty() && (std::isdigit((unsigned char)tok[0]) || tok[0] == '-' || tok[0] == '+' || tok[0] == '.')) {
            if (tok.find_first_of(".eE") == std::string::npos) {
                out.type = 'L'; out.i = std::strtoll(tok.c_str(), nullptr, 10);
            } else { out.type = 'D'; out.d = d; }
        } else { out.type = 'S'; out.s = std::string_view(t, len); }   // bare identifier (T, W, Y ...)
        return true;
    }
    bool ReadString(Prop& out) {
        ++pos;   // opening quote
        const std::size_t b = pos;
        const void* q = pos < n ? std::memchr(s + pos, '"', n - pos) : nullptr;
        pos = q ? (std::size_t)((const char*)q - s) : n;
        if (pos >= n) return Fail("unterminated string");
        if (pos - b > lim->maxStringBytes) return Fail("string too long");
        out.type = 'S'; out.s = std::string_view(s + b, pos - b);
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
        std::vector<double> vals;
        vals.reserve(cnt < (1u << 20) ? cnt : (1u << 20));
        const std::size_t cap = std::min<std::size_t>(lim->maxArrayBytes / 8, 0xFFFFFFFEu);
        for (;;) {
            // spaces, line ends and commas between values (comments too, as SkipSpace does)
            while (pos < n) {
                const char c = s[pos];
                if (c == ' ' || c == ',' || c == '\n' || c == '\r' || c == '\t') ++pos;
                else if (c == ';') { while (pos < n && s[pos] != '\n') ++pos; }
                else break;
            }
            if (Cur() == '}') { ++pos; break; }
            if (Eof()) return Fail("unterminated array block");
            double d;
            if (const char* e = FastDecimalScan(s + pos, s + n, d)) {   // the usual case: one pass over the number
                pos = (std::size_t)(e - s);
                vals.push_back(d);
                if (vals.size() > cap) return Fail("arrays exceed the size limit");
                continue;
            }
            const std::size_t b = pos;
            while (pos < n && (IsIdent(s[pos]))) ++pos;
            if (pos == b) return Fail("bad value in array block");
            if (!FastDecimal(s + b, pos - b, d)) {
                const std::string tok(s + b, pos - b);
                char* end = nullptr;
                d = detail::StrToD(tok.c_str(), &end);
                if (!end || *end) return Fail("bad number in array block");
            }
            vals.push_back(d);
            if (vals.size() > cap) return Fail("arrays exceed the size limit");
        }
        arrayBytes += vals.size() * 8;
        out.arr.type = 'd'; out.arr.count = (std::uint32_t)vals.size(); out.arr.data = (const std::uint8_t*)vals.data();
        doc->numbers.push_back(std::move(vals));   // the buffer stays where it is
        return true;
    }

    bool ReadElem(std::uint32_t& idx, std::size_t depth) {
        if (depth > lim->maxDepth) return Fail("element nesting too deep");
        if (++doc->elementCount > lim->maxElements) return Fail("too many elements");
        const std::size_t b = pos;
        while (pos < n && IsIdent(s[pos])) ++pos;
        if (pos == b || Cur() != ':') return Fail("expected 'Name:' in ASCII FBX");
        idx = (std::uint32_t)doc->elems.size();
        doc->elems.emplace_back();
        doc->elems[idx].name = std::string_view(s + b, pos - b);
        ++pos;
        const std::size_t firstProp = doc->props.size();
        std::size_t count = 0;
        for (;;) {
            SkipSpace(false);
            const char c = Cur();
            if (Eof() || c == '\n') break;
            if (c == '{') break;
            if (c == '}') break;
            if (c == ',') { ++pos; SkipSpace(true); continue; }   // a comma continues onto the next line
            doc->props.emplace_back();
            Prop& pr = doc->props.back();
            if (c == '"') { if (!ReadString(pr)) return false; }
            else if (c == '*') { if (!ReadArrayBlock(pr)) return false; }
            else if (!ReadNumberOrIdent(pr)) return false;
            if (++count > lim->maxArrayBytes / 8) return Fail("too many properties");
        }
        doc->elems[idx].propBegin_ = (std::uint32_t)firstProp;
        doc->elems[idx].props.n = (std::uint32_t)count;
        SkipSpace(true);
        if (Cur() == '{') {
            ++pos;
            std::uint32_t last = kNone;
            Linker link{doc->elems, doc->root};
            for (;;) {
                SkipSpace(true);
                if (Eof()) return Fail("unterminated '{' block");
                if (Cur() == '}') { ++pos; break; }
                std::uint32_t c = kNone;
                if (!ReadElem(c, depth + 1)) return false;
                link.Link(idx, last, c);
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
    std::uint32_t last = kNone;
    Linker link{out.elems, out.root};
    bool ok = true;
    for (;;) {
        t.SkipSpace(true);
        if (t.Eof()) break;
        std::uint32_t idx = kNone;
        if (!t.ReadElem(idx, 1)) { ok = false; break; }
        link.Link(kNone, last, idx);
    }
    Fixup(out);
    if (!ok) return fail(t.err);
    return true;
}

bool Parse(const std::uint8_t* data, std::size_t size, Document& out, std::string* err, const Limits& lim) {
    if (data && size >= 20 && std::memcmp(data, "Kaydara FBX Binary", 18) == 0) return ParseBinary(data, size, out, err, lim);
    return ParseAscii(data, size, out, err, lim);
}

} // namespace fbx
} // namespace okay
