// okay::fbx layer 1: inflate + binary / ASCII FBX element tree. See FbxReader.hpp.
// Written from the public description of the format; no third-party code.
#include "DecimalToDouble.hpp"
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
        const std::uint64_t c1 = (std::uint64_t)child + 1;
        if (lastChild == kNone) { Elem& p = parent == kNone ? root : elems[parent]; p.links_ = (p.links_ & 0xFFFFFFFFull) | (c1 << 32); }
        else { Elem& l = elems[lastChild]; l.links_ = (l.links_ & ~0xFFFFFFFFull) | c1; }
        lastChild = child;
    }
};

// Turns the indices recorded while parsing into the pointers of the public tree. Run once, after the last element:
// before that the flat arrays may still move.
void Fixup(Document& d) {
    Elem* const E = d.elems.data();
    auto at = [&](std::uint64_t i1) -> const Elem* { return i1 == 0 ? nullptr : E + (i1 - 1); };   // index + 1
    for (Elem& e : d.elems) {
        const std::uint64_t l = e.links_;
        e.props.p = e.props.n ? d.props.data() + e.props.begin_ : nullptr;
        e.children.first = at(l >> 32);
        e.next = at(l & 0xFFFFFFFFull);
    }
    d.root.children.first = at(d.root.links_ >> 32);
    d.root.next = nullptr;
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
namespace {
/// Inflate into a ByteBuffer (no zero fill of the output first): the same decoder, options and result.
bool InflateArray(const std::uint8_t* data, std::size_t n, ByteBuffer& out, std::size_t maxOut) {
    std::string why;
    return okay::inflate::ZlibInto(data, n, out, okay::inflate::Options::Lenient(maxOut, maxOut), why);
}
} // namespace

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
                    ByteBuffer tmp;
                    if (!InflateArray(src, clen, tmp, raw) || tmp.size() != raw) return Fail("bad compressed array");
                    src = tmp.data();   // the vector's buffer does not move when the vector itself is moved below
                    doc->inflated.push_back(std::move(tmp));
                    out.own = (std::uint32_t)doc->inflated.size();   // (1 + its index)
                } else {
                    return Fail("unknown array encoding");
                }
                arrayBytes += raw;
                // Kept as the file stores it (no conversion): a view into the file, or into the inflated buffer.
                out.SetArray(t, cnt, src);
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
        doc->elems[idx].props.begin_ = (std::uint32_t)firstProp;
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

// Plain decimal numbers ("12", "-0.5", "1.25e-3", "0.30000000000000004"), converted exactly: the double strtod gives,
// bit for bit, without strtod (DecimalToDouble.hpp: Clinger's fast path for short numbers, a 128-bit product with a
// checked error bound for the rest, any number of digits). Returns false for anything else (more than a plain decimal:
// "inf", hex, a malformed token) and for the rare inputs that method cannot decide; the caller then uses strtod as
// before. No locale is involved: the point is '.'.
bool FastDecimal(const char* p, std::size_t n, double& out) {
    detail::DecimalParts d;
    if (detail::ScanDecimal(p, p + n, d) != p + n) return false;
    return detail::DecimalToDouble(d, out);
}

inline bool IdentChar(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '|' || c == '-' || c == '.' || c == '+';
}
struct IdentTable {
    bool t[256];
    IdentTable() : t{} { for (int c = 0; c < 256; ++c) t[c] = IdentChar((unsigned char)c); }
};
const IdentTable kIdent;
// Between the values of an ASCII array: 1 for a space, comma, tab or line end, 2 for ';' (a comment to the line end).
struct ArraySepTable {
    unsigned char t[256];
    ArraySepTable() : t{} { t[(unsigned char)' '] = t[(unsigned char)','] = t[(unsigned char)'\n'] = t[(unsigned char)'\r'] = t[(unsigned char)'\t'] = 1; t[(unsigned char)';'] = 2; }
};
const ArraySepTable kArraySep;

// The same conversion as FastDecimal, but it finds the end of the token itself (the first character that cannot
// be part of a token), so an array value is read in one pass instead of a scan for its end and then a parse.
// Returns the end of the token, or nullptr when FastDecimal would not take it (the caller then uses the general
// path, which gives the same result as before for everything).
const char* FastDecimalScan(const char* p, const char* end, double& out) {
    detail::DecimalParts d;
    const char* e = detail::ScanDecimal(p, end, d);
    if (!e || (e < end && kIdent.t[(unsigned char)*e])) return nullptr;   // the token goes on: not a plain decimal
    if (!detail::DecimalToDouble(d, out)) return nullptr;
    return e;
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
        // returns), and a plain decimal with a point or an exponent, which FastDecimal converts exactly (what strtod
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
        // A token that starts with a letter or '_' is an identifier whatever strtod would make of it ("inf", "nan"):
        // the rule below requires a digit, a sign or a point first.
        if (!(std::isdigit((unsigned char)t[0]) || t[0] == '-' || t[0] == '+' || t[0] == '.')) {
            out.type = 'S'; out.s = std::string_view(t, len);
            return true;
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
        // Written through an index into a vector sized from the declared count (capped: a file can lie), grown by
        // doubling if the file has more values than it declared, trimmed at the end.
        NumberBuffer vals(cnt < (1u << 20) ? cnt : (1u << 20));   // (not zero-filled: every value is written before it is read)
        std::size_t nv = 0;
        const std::size_t cap = std::min<std::size_t>(lim->maxArrayBytes / 8, 0xFFFFFFFEu);
        for (;;) {
            // spaces, line ends and commas between values (comments too, as SkipSpace does)
            while (pos < n) {
                const unsigned char t = kArraySep.t[(unsigned char)s[pos]];
                if (t == 1) ++pos;
                else if (t == 2) { while (pos < n && s[pos] != '\n') ++pos; }
                else break;
            }
            if (Cur() == '}') { ++pos; break; }
            if (Eof()) return Fail("unterminated array block");
            double d;
            if (const char* e = FastDecimalScan(s + pos, s + n, d)) {   // the usual case: one pass over the number
                pos = (std::size_t)(e - s);
                if (nv == vals.size()) vals.resize(vals.size() < 16 ? 16 : vals.size() * 2);
                vals[nv++] = d;
                if (nv > cap) return Fail("arrays exceed the size limit");
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
            if (nv == vals.size()) vals.resize(vals.size() < 16 ? 16 : vals.size() * 2);
            vals[nv++] = d;
            if (nv > cap) return Fail("arrays exceed the size limit");
        }
        vals.resize(nv);
        arrayBytes += nv * 8;   // the limit counts 8 bytes a value, whatever the storage below
        // Smaller storage when it changes no value: an array whose values are all 32-bit integers (index arrays) is
        // kept as int32, one whose values are all exact floats as float32. Array::operator[] gives (double)value,
        // which is the same double bit for bit (checked per value; -0 stays a double), and the readers' type-specific
        // paths (int32 indices, double vectors) give the same numbers as their general paths.
        // Checked in blocks of 256 values without a branch per value (both answers for every value of a block; the
        // loop stops after the block where both became false). A value outside the int32 range (or NaN) is replaced by
        // 0.5 before the conversion and one beyond the largest float by 0.1: neither converts back to itself, so the
        // answer is "no" for them, as it must be, and no conversion is out of range.
        bool allInt = true, allFloat = true;
        for (std::size_t k0 = 0; k0 < nv && (allInt || allFloat); k0 += 256) {
            const std::size_t k1 = std::min(nv, k0 + 256);
            bool okI = true, okF = true;
            for (std::size_t k = k0; k < k1; ++k) {
                const double v = vals[k];
                std::uint64_t bv; std::memcpy(&bv, &v, 8);
                const double ci = (v >= -2147483648.0 && v <= 2147483647.0) ? v : 0.5;
                const double bi = (double)(std::int32_t)ci;
                std::uint64_t ui; std::memcpy(&ui, &bi, 8);
                okI &= ui == bv;
                const double cf = (std::fabs(v) > 3.4028234663852886e38 && !std::isinf(v)) ? 0.1 : v;   // beyond the largest float
                const double bf = (double)(float)cf;
                std::uint64_t uf; std::memcpy(&uf, &bf, 8);
                okF &= uf == bv;
            }
            allInt = allInt && okI; allFloat = allFloat && okF;
        }
        char type = 'd';
        if (nv && (allInt || allFloat)) {
            NumberBuffer packed((nv * 4 + 7) / 8);   // 4 bytes a value, in a buffer of doubles (the storage type)
            if (nv & 1) packed.back() = 0;            // (the 4 bytes after an odd count: never read, set anyway)
            std::uint8_t* q = (std::uint8_t*)packed.data();
            if (allInt) { type = 'i'; for (std::size_t k = 0; k < nv; ++k) { const std::int32_t x = (std::int32_t)vals[k]; std::memcpy(q + k * 4, &x, 4); } }
            else { type = 'f'; for (std::size_t k = 0; k < nv; ++k) { const float x = (float)vals[k]; std::memcpy(q + k * 4, &x, 4); } }
            vals.swap(packed);
        }
        out.SetArray(type, (std::uint32_t)nv, (const std::uint8_t*)vals.data());
        doc->numbers.push_back(std::move(vals));   // the buffer stays where it is
        out.own = (std::uint32_t)doc->numbers.size();   // (1 + its index)
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
        doc->elems[idx].props.begin_ = (std::uint32_t)firstProp;
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
    // The two flat arrays grew by doubling (the text gives no counts up front): give back the slack before the scene
    // is built, which is when memory peaks. They hold indices until Fixup, so moving them is safe here.
    out.elems.shrink_to_fit(); out.props.shrink_to_fit();
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
