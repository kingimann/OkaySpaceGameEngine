// okay::fbx layer 1: inflate + binary / ASCII FBX element tree. See FbxReader.hpp.
// Written from the public description of the format; no third-party code.
#include "okay/Render/FbxReader.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace okay {
namespace fbx {

const Elem* Elem::Find(const char* n) const {
    for (const Elem& c : children) if (c.name == n) return &c;
    return nullptr;
}

// ============================================================================
// Inflate (RFC 1950 wrapper around RFC 1951 deflate)
// ============================================================================
namespace {

struct BitIn {
    const std::uint8_t* p; std::size_t n, pos = 0; std::uint32_t buf = 0; int cnt = 0; bool bad = false;
    BitIn(const std::uint8_t* d, std::size_t len) : p(d), n(len) {}
    std::uint32_t Bits(int need) {
        while (cnt < need) {
            if (pos >= n) { bad = true; return 0; }
            buf |= (std::uint32_t)p[pos++] << cnt; cnt += 8;
        }
        const std::uint32_t v = need ? (buf & ((1u << need) - 1u)) : 0u;
        buf >>= need; cnt -= need;
        return v;
    }
};

struct Huff {
    std::uint16_t count[16];
    std::uint16_t sym[320];
    bool Build(const std::uint8_t* len, int n) {
        std::memset(count, 0, sizeof(count));
        for (int i = 0; i < n; ++i) ++count[len[i]];
        if (count[0] == n) return true;      // no codes: legal for an unused distance tree
        int left = 1;
        for (int l = 1; l < 16; ++l) { left <<= 1; left -= count[l]; if (left < 0) return false; }   // over-subscribed
        std::uint16_t offs[16]; offs[1] = 0;
        for (int l = 1; l < 15; ++l) offs[l + 1] = (std::uint16_t)(offs[l] + count[l]);
        for (int i = 0; i < n; ++i) if (len[i]) sym[offs[len[i]]++] = (std::uint16_t)i;
        return true;
    }
    int Decode(BitIn& in) const {
        int code = 0, first = 0, index = 0;
        for (int l = 1; l < 16; ++l) {
            code |= (int)in.Bits(1);
            if (in.bad) return -1;
            const int c = count[l];
            if (code - c < first) return sym[index + (code - first)];
            index += c; first += c; first <<= 1; code <<= 1;
        }
        return -1;
    }
};

const std::uint16_t kLenBase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
const std::uint8_t  kLenExtra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
const std::uint16_t kDistBase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
const std::uint8_t  kDistExtra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

bool InflateBlock(BitIn& in, const Huff& lit, const Huff& dist, std::vector<std::uint8_t>& out, std::size_t maxOut) {
    for (;;) {
        const int s = lit.Decode(in);
        if (s < 0) return false;
        if (s < 256) {
            if (out.size() >= maxOut) return false;
            out.push_back((std::uint8_t)s);
        } else if (s == 256) {
            return true;
        } else {
            const int li = s - 257;
            if (li >= 29) return false;
            const std::size_t len = kLenBase[li] + in.Bits(kLenExtra[li]);
            const int ds = dist.Decode(in);
            if (ds < 0 || ds >= 30) return false;
            const std::size_t d = kDistBase[ds] + in.Bits(kDistExtra[ds]);
            if (in.bad || d > out.size() || out.size() + len > maxOut) return false;
            const std::size_t from = out.size() - d;
            for (std::size_t k = 0; k < len; ++k) out.push_back(out[from + k]);   // may overlap: byte by byte
        }
    }
}

} // namespace

bool Inflate(const std::uint8_t* data, std::size_t n, std::vector<std::uint8_t>& out, std::size_t maxOut) {
    out.clear();
    if (n < 2) return false;
    if ((data[0] & 0x0F) != 8 || ((data[0] << 8) | data[1]) % 31 != 0 || (data[1] & 0x20)) return false;   // deflate, no preset dictionary
    BitIn in(data + 2, n - 2);
    for (;;) {
        const std::uint32_t last = in.Bits(1), type = in.Bits(2);
        if (in.bad) return false;
        if (type == 0) {
            in.buf = 0; in.cnt = 0;                       // drop the partial byte
            if (in.pos + 4 > in.n) return false;
            const std::size_t len = in.p[in.pos] | (in.p[in.pos + 1] << 8);
            const std::size_t nlen = in.p[in.pos + 2] | (in.p[in.pos + 3] << 8);
            in.pos += 4;
            if ((len ^ 0xFFFF) != nlen || in.pos + len > in.n || out.size() + len > maxOut) return false;
            out.insert(out.end(), in.p + in.pos, in.p + in.pos + len);
            in.pos += len;
        } else if (type == 1) {
            Huff lit, dist;
            std::uint8_t l[288];
            for (int i = 0; i < 144; ++i) l[i] = 8;
            for (int i = 144; i < 256; ++i) l[i] = 9;
            for (int i = 256; i < 280; ++i) l[i] = 7;
            for (int i = 280; i < 288; ++i) l[i] = 8;
            lit.Build(l, 288);
            std::uint8_t dl[30]; for (int i = 0; i < 30; ++i) dl[i] = 5;
            dist.Build(dl, 30);
            if (!InflateBlock(in, lit, dist, out, maxOut)) return false;
        } else if (type == 2) {
            const int nlen = (int)in.Bits(5) + 257, ndist = (int)in.Bits(5) + 1, ncode = (int)in.Bits(4) + 4;
            if (in.bad || nlen > 286 || ndist > 30) return false;
            static const std::uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            std::uint8_t cl[19] = {0};
            for (int i = 0; i < ncode; ++i) cl[order[i]] = (std::uint8_t)in.Bits(3);
            Huff ch;
            if (in.bad || !ch.Build(cl, 19)) return false;
            std::uint8_t lens[320] = {0};
            int idx = 0;
            while (idx < nlen + ndist) {
                const int s = ch.Decode(in);
                if (s < 0) return false;
                if (s < 16) { lens[idx++] = (std::uint8_t)s; continue; }
                int rep, val = 0;
                if (s == 16) { if (idx == 0) return false; val = lens[idx - 1]; rep = 3 + (int)in.Bits(2); }
                else if (s == 17) rep = 3 + (int)in.Bits(3);
                else rep = 11 + (int)in.Bits(7);
                if (in.bad || idx + rep > nlen + ndist) return false;
                while (rep--) lens[idx++] = (std::uint8_t)val;
            }
            if (lens[256] == 0) return false;               // no end-of-block code
            Huff lit, dist;
            if (!lit.Build(lens, nlen) || !dist.Build(lens + nlen, ndist)) return false;
            if (!InflateBlock(in, lit, dist, out, maxOut)) return false;
        } else {
            return false;
        }
        if (last) return true;
    }
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
                out.s.assign((const char*)p + pos, len); pos += len;
                return true;
            }
            case 'f': case 'd': case 'l': case 'i': case 'b': {
                std::uint32_t cnt, enc, clen;
                if (!need(12)) return Fail("short array header");
                std::memcpy(&cnt, p + pos, 4); std::memcpy(&enc, p + pos + 4, 4); std::memcpy(&clen, p + pos + 8, 4);
                pos += 12;
                const std::size_t esz = (t == 'd' || t == 'l') ? 8 : (t == 'b' ? 1 : 4);
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
                out.arr.resize(cnt);
                for (std::uint32_t k = 0; k < cnt; ++k) {
                    double v;
                    switch (t) {
                        case 'd': { double x; std::memcpy(&x, src + (std::size_t)k * 8, 8); v = x; break; }
                        case 'f': { float x; std::memcpy(&x, src + (std::size_t)k * 4, 4); v = x; break; }
                        case 'l': { std::int64_t x; std::memcpy(&x, src + (std::size_t)k * 8, 8); v = (double)x; break; }
                        case 'i': { std::int32_t x; std::memcpy(&x, src + (std::size_t)k * 4, 4); v = x; break; }
                        default:  v = src[k] != 0; break;
                    }
                    out.arr[k] = v;
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
        const double d = std::strtod(tok.c_str(), &end);
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
            const std::string tok(s + b, pos - b);
            char* end = nullptr;
            const double d = std::strtod(tok.c_str(), &end);
            if (!end || *end) return Fail("bad number in array block");
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
        int a = 0, b = 0;
        if (std::sscanf((const char*)data + 6, "%d.%d", &a, &b) == 2) out.version = (std::uint32_t)(a * 1000 + b * 100);
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
