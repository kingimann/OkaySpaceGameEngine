#pragma once
// Private to the engine: the Blender (.blend) reader's types (BlendReader.cpp reads the container, BlendReaderBuild.cpp builds the model).
#include "okay/Core/ImageDecode.hpp"
#include "BlendModifiers.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace okay {
namespace blnd {

using u8 = std::uint8_t;

constexpr std::size_t kMaxFileBytes    = 512u * 1024u * 1024u;   // as read from disk
constexpr std::size_t kMaxInflated     = 768u * 1024u * 1024u;   // after gunzip
constexpr std::size_t kMaxBlocks       = 4000000;
constexpr std::size_t kMaxStructs      = 20000;
constexpr std::size_t kMaxFields       = 4000;                   // per struct
constexpr std::size_t kMaxNames        = 200000;
constexpr std::size_t kMaxList         = 1000000;                // items of one ListBase
constexpr std::size_t kMaxObjects      = 100000;
constexpr std::size_t kMaxBones        = 100000;
constexpr std::size_t kMaxMaterials    = 100000;
constexpr std::size_t kMaxVerts        = 20000000;
constexpr std::size_t kMaxFaces        = 20000000;
constexpr std::size_t kMaxCorners      = 60000000;
constexpr std::size_t kMaxOutVerts     = 40000000;
constexpr std::size_t kMaxParts        = 200000;
constexpr std::size_t kMaxCurves       = 400000;
constexpr std::size_t kMaxKeysPerCurve = 4000000;
constexpr std::size_t kMaxOutKeys      = 20000000;
constexpr std::size_t kMaxDenseFrames  = 40000;                  // integer frames sampled per animated node
constexpr int         kMaxDepth        = 512;                    // bone / collection nesting
constexpr int         kMaxNgon         = 256;                    // ear clipping stops here (fan after)
constexpr float       kMaxCoord        = 1e15f;
constexpr int         kMaxModBytes     = 1 << 20;

inline bool Fin(double v) { return std::isfinite(v); }
inline int SafeInt(double v, int lo, int hi) {
    if (!(v == v)) return lo;
    if (v < (double)lo) return lo;
    if (v > (double)hi) return hi;
    return (int)v;
}
inline bool InRange(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::fabs(v.x) <= kMaxCoord && std::fabs(v.y) <= kMaxCoord && std::fabs(v.z) <= kMaxCoord;
}

// ------------------------------------------------------------------------------------------------
// Container: header, blocks, SDNA
// ------------------------------------------------------------------------------------------------

enum class Kind : u8 { Other, Char, UChar, Short, UShort, Int, UInt, Long64, ULong64, Float, Double, Void };

struct FieldInfo {
    std::string name;      // without '*', array brackets
    int type = -1;         // index into typeNames
    std::uint32_t offset = 0, size = 0, count = 1, elem = 0;   // size = elem * count
    bool ptr = false;
};
struct StructInfo {
    std::string name;
    std::uint32_t size = 0;
    bool layoutOk = true;
    std::vector<FieldInfo> fields;
    std::unordered_map<std::string, int> byName;
};
struct BlockInfo {
    char code[5] = {0, 0, 0, 0, 0};
    std::uint32_t size = 0;
    std::uint64_t old = 0;
    int sdna = 0;
    std::uint32_t nr = 0;
    std::size_t off = 0;   // data offset in the file buffer
};

struct Span { const u8* p = nullptr; std::size_t n = 0; int st = -1; std::uint64_t addr = 0; std::uint32_t nr = 0; };

struct File {
    std::vector<u8> buf;
    bool le = true;
    int ps = 8;
    int version = 0;
    std::vector<std::string> typeNames;
    std::vector<Kind> typeKind;
    std::vector<std::uint16_t> typeLen;
    std::vector<StructInfo> structs;
    std::unordered_map<std::string, int> structByName;
    std::vector<BlockInfo> blocks;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> ptrIndex;   // (old address, block index), sorted
    mutable std::size_t boundsMiss = 0;                              // reads that would have left their block
    std::string err;

    bool Fail(const std::string& m) { if (err.empty()) err = m; return false; }

    std::uint16_t U16(const u8* p) const { return le ? (std::uint16_t)(p[0] | (p[1] << 8)) : (std::uint16_t)((p[0] << 8) | p[1]); }
    std::uint32_t U32(const u8* p) const {
        return le ? (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24)
                  : (std::uint32_t)p[3] | ((std::uint32_t)p[2] << 8) | ((std::uint32_t)p[1] << 16) | ((std::uint32_t)p[0] << 24);
    }
    std::uint64_t U64(const u8* p) const {
        std::uint64_t r = 0;
        if (le) for (int i = 7; i >= 0; --i) r = (r << 8) | p[i];
        else for (int i = 0; i < 8; ++i) r = (r << 8) | p[i];
        return r;
    }
    float F32(const u8* p) const { const std::uint32_t u = U32(p); float f; std::memcpy(&f, &u, 4); return f; }
    double F64(const u8* p) const { const std::uint64_t u = U64(p); double f; std::memcpy(&f, &u, 8); return f; }
    std::uint64_t PtrAt(const u8* p) const { return ps == 4 ? (std::uint64_t)U32(p) : U64(p); }

    double ReadKind(Kind k, const u8* p) const {
        switch (k) {
            case Kind::Char:    return (double)(std::int8_t)p[0];
            case Kind::UChar:   return (double)p[0];
            case Kind::Short:   return (double)(std::int16_t)U16(p);
            case Kind::UShort:  return (double)U16(p);
            case Kind::Int:     return (double)(std::int32_t)U32(p);
            case Kind::UInt:    return (double)U32(p);
            case Kind::Long64:  return (double)(std::int64_t)U64(p);
            case Kind::ULong64: return (double)U64(p);
            case Kind::Float:   return (double)F32(p);
            case Kind::Double:  return F64(p);
            default: return 0.0;
        }
    }

    static Kind KindOf(const std::string& t) {
        if (t == "char") return Kind::Char;
        if (t == "uchar") return Kind::UChar;
        if (t == "short") return Kind::Short;
        if (t == "ushort") return Kind::UShort;
        if (t == "int") return Kind::Int;
        if (t == "uint") return Kind::UInt;
        if (t == "float") return Kind::Float;
        if (t == "double") return Kind::Double;
        if (t == "int64_t" || t == "long long") return Kind::Long64;
        if (t == "uint64_t" || t == "unsigned long long") return Kind::ULong64;
        if (t == "void") return Kind::Void;
        return Kind::Other;
    }

    // ---- SDNA --------------------------------------------------------------------------------
    bool ParseSdna(const u8* d, std::size_t n) {
        std::size_t o = 0;
        auto tag = [&](const char* t) { return o + 4 <= n && std::memcmp(d + o, t, 4) == 0; };
        auto align4 = [&] { o = (o + 3) & ~(std::size_t)3; };
        if (!tag("SDNA")) return Fail("Blend: DNA1 block does not start with SDNA");
        o += 4;
        if (!tag("NAME")) return Fail("Blend: SDNA has no NAME table");
        o += 4;
        if (o + 4 > n) return Fail("Blend: truncated SDNA");
        std::uint32_t cnt = U32(d + o); o += 4;
        if (cnt > kMaxNames) return Fail("Blend: SDNA name table too large");
        std::vector<std::string> names;
        names.reserve(cnt);
        for (std::uint32_t i = 0; i < cnt; ++i) {
            std::size_t s = o;
            while (o < n && d[o]) ++o;
            if (o >= n) return Fail("Blend: truncated SDNA names");
            names.emplace_back((const char*)d + s, o - s);
            ++o;
        }
        align4();
        if (!tag("TYPE")) return Fail("Blend: SDNA has no TYPE table");
        o += 4;
        if (o + 4 > n) return Fail("Blend: truncated SDNA");
        cnt = U32(d + o); o += 4;
        if (cnt > kMaxNames) return Fail("Blend: SDNA type table too large");
        typeNames.clear();
        for (std::uint32_t i = 0; i < cnt; ++i) {
            std::size_t s = o;
            while (o < n && d[o]) ++o;
            if (o >= n) return Fail("Blend: truncated SDNA types");
            typeNames.emplace_back((const char*)d + s, o - s);
            ++o;
        }
        align4();
        if (!tag("TLEN")) return Fail("Blend: SDNA has no TLEN table");
        o += 4;
        if (o + (std::size_t)cnt * 2 > n) return Fail("Blend: truncated SDNA lengths");
        typeLen.resize(cnt);
        for (std::uint32_t i = 0; i < cnt; ++i) { typeLen[i] = U16(d + o); o += 2; }
        align4();
        if (!tag("STRC")) return Fail("Blend: SDNA has no STRC table");
        o += 4;
        if (o + 4 > n) return Fail("Blend: truncated SDNA");
        const std::uint32_t ns = U32(d + o); o += 4;
        if (ns > kMaxStructs) return Fail("Blend: too many SDNA structs");
        typeKind.resize(typeNames.size());
        for (std::size_t i = 0; i < typeNames.size(); ++i) typeKind[i] = KindOf(typeNames[i]);
        structs.clear(); structByName.clear();
        structs.resize(ns);
        for (std::uint32_t s = 0; s < ns; ++s) {
            if (o + 4 > n) return Fail("Blend: truncated SDNA structs");
            const std::uint32_t ti = U16(d + o), nf = U16(d + o + 2); o += 4;
            if (ti >= typeNames.size()) return Fail("Blend: SDNA struct type index out of range");
            if (nf > kMaxFields) return Fail("Blend: SDNA struct with too many fields");
            if (o + (std::size_t)nf * 4 > n) return Fail("Blend: truncated SDNA fields");
            StructInfo& si = structs[s];
            si.name = typeNames[ti];
            si.size = typeLen[ti];
            std::uint64_t off = 0;
            for (std::uint32_t f = 0; f < nf; ++f) {
                const std::uint32_t ft = U16(d + o), fn = U16(d + o + 2); o += 4;
                if (ft >= typeNames.size() || fn >= names.size()) return Fail("Blend: SDNA field index out of range");
                FieldInfo fi;
                std::string raw = names[fn];
                bool func = false;
                if (raw.size() > 2 && raw[0] == '(' && raw[1] == '*') func = true;
                std::size_t a = 0;
                int stars = 0;
                while (a < raw.size() && raw[a] == '*') { ++stars; ++a; }
                if (func) { a = 2; stars = 1; }
                std::size_t b = a;
                while (b < raw.size() && raw[b] != '[' && raw[b] != ')') ++b;
                fi.name = raw.substr(a, b - a);
                std::uint64_t cntArr = 1;
                for (std::size_t q = raw.find('['); q != std::string::npos && q < raw.size();) {
                    std::size_t e2 = raw.find(']', q);
                    if (e2 == std::string::npos) break;
                    std::uint64_t dim = 0;
                    for (std::size_t k = q + 1; k < e2; ++k) { if (raw[k] < '0' || raw[k] > '9') { dim = 0; break; } dim = dim * 10 + (std::uint64_t)(raw[k] - '0'); if (dim > 1000000) break; }
                    if (dim == 0 || dim > 1000000) { cntArr = 0; break; }
                    cntArr *= dim;
                    if (cntArr > 100000000) { cntArr = 0; break; }
                    q = raw.find('[', e2);
                }
                if (cntArr == 0) { si.layoutOk = false; cntArr = 1; }
                fi.type = (int)ft;
                fi.ptr = stars > 0;
                fi.elem = fi.ptr ? (std::uint32_t)ps : (std::uint32_t)typeLen[ft];
                fi.count = (std::uint32_t)cntArr;
                const std::uint64_t sz = (std::uint64_t)fi.elem * cntArr;
                if (sz > 0x7fffffffu || off + sz > 0x7fffffffu) { si.layoutOk = false; sz > 0x7fffffffu ? (void)0 : (void)0; return Fail("Blend: SDNA struct too large"); }
                fi.size = (std::uint32_t)sz;
                fi.offset = (std::uint32_t)off;
                off += sz;
                si.byName.emplace(fi.name, (int)si.fields.size());   // the first of a name wins
                si.fields.push_back(std::move(fi));
            }
            if (off != si.size) si.layoutOk = false;
            structByName.emplace(si.name, (int)s);   // the first of a name wins
        }
        return true;
    }

    // ---- Container ---------------------------------------------------------------------------
    bool ParseBlocks() {
        const std::size_t n = buf.size();
        if (n < 12 || std::memcmp(buf.data(), "BLENDER", 7) != 0) return Fail("Blend: not a .blend file (no BLENDER header)");
        const u8 c7 = buf[7], c8 = buf[8];
        if (c7 >= '0' && c7 <= '9') return Fail("Blend: this file uses the Blender 5.0 container (large block headers), which is not supported yet");
        if (c7 == '_') ps = 4; else if (c7 == '-') ps = 8; else return Fail("Blend: bad pointer size in the header");
        if (c8 == 'v') le = true; else if (c8 == 'V') le = false; else return Fail("Blend: bad endianness in the header");
        for (int i = 9; i < 12; ++i) if (buf[i] < '0' || buf[i] > '9') return Fail("Blend: bad version in the header");
        version = (buf[9] - '0') * 100 + (buf[10] - '0') * 10 + (buf[11] - '0');
        const std::size_t hs = 16 + (std::size_t)ps;
        std::size_t pos = 12;
        bool sawDna = false, sawEnd = false;
        while (pos + hs <= n) {
            if (blocks.size() >= kMaxBlocks) return Fail("Blend: too many blocks");
            const u8* h = buf.data() + pos;
            BlockInfo b;
            u8 c[4] = {h[0], h[1], h[2], h[3]};
            if (!le && c[0] == 0 && c[1] == 0) { u8 t0 = c[2], t1 = c[3]; c[0] = t0; c[1] = t1; c[2] = 0; c[3] = 0; }   // big-endian two letter ids
            std::memcpy(b.code, c, 4); b.code[4] = 0;
            const std::int32_t sz = (std::int32_t)U32(h + 4);
            b.old = PtrAt(h + 8);
            b.sdna = (std::int32_t)U32(h + 8 + ps);
            b.nr = U32(h + 12 + ps);
            if (sz < 0) return Fail("Blend: negative block size");
            b.size = (std::uint32_t)sz;
            b.off = pos + hs;
            if ((std::uint64_t)b.off + b.size > n) {
                if (!sawDna) return Fail("Blend: file is truncated before the DNA block");
                break;   // truncated tail: keep what we have
            }
            pos = b.off + b.size;
            const bool isEnd = std::memcmp(b.code, "ENDB", 4) == 0;
            if (std::memcmp(b.code, "DNA1", 4) == 0 && !sawDna) {
                if (!ParseSdna(buf.data() + b.off, b.size)) return false;
                sawDna = true;
            }
            blocks.push_back(b);
            if (isEnd) { sawEnd = true; break; }
        }
        (void)sawEnd;
        if (!sawDna) return Fail("Blend: no DNA1 block (not a complete .blend)");
        // address table
        ptrIndex.reserve(blocks.size());
        for (std::size_t i = 0; i < blocks.size(); ++i)
            if (blocks[i].old != 0 && blocks[i].size > 0 && std::memcmp(blocks[i].code, "DNA1", 4) != 0 && std::memcmp(blocks[i].code, "ENDB", 4) != 0)
                ptrIndex.emplace_back(blocks[i].old, (std::uint32_t)i);
        std::stable_sort(ptrIndex.begin(), ptrIndex.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        return true;
    }

    // The memory from `ptr` to the end of the block that contains it.
    Span At(std::uint64_t ptr) const {
        Span s;
        if (!ptr || ptrIndex.empty()) return s;
        auto it = std::upper_bound(ptrIndex.begin(), ptrIndex.end(), ptr, [](std::uint64_t v, const auto& e) { return v < e.first; });
        while (it != ptrIndex.begin()) {
            --it;
            const BlockInfo& b = blocks[it->second];
            if (ptr - b.old < b.size) {
                const std::size_t inner = (std::size_t)(ptr - b.old);
                s.p = buf.data() + b.off + inner;
                s.n = b.size - inner;
                s.st = (b.sdna >= 0 && (std::size_t)b.sdna < structs.size()) ? b.sdna : -1;
                s.addr = ptr;
                s.nr = b.nr;
                return s;
            }
            // an earlier block with the same address (zero-sized duplicates were dropped); anything else does not contain ptr
            if (ptr - b.old >= b.size) break;
        }
        return s;
    }
    int StructIndex(const char* name) const {
        auto it = structByName.find(name);
        return it == structByName.end() ? -1 : it->second;
    }
};

// A typed view of one struct in the file.
struct Ref {
    const File* f = nullptr;
    const u8* p = nullptr;
    std::size_t avail = 0;
    int st = -1;
    std::uint64_t addr = 0;

    explicit operator bool() const { return f && p && st >= 0; }
    const StructInfo& S() const { return f->structs[(std::size_t)st]; }
    const char* StructName() const { return st >= 0 ? S().name.c_str() : ""; }
    const FieldInfo* Find(const char* name) const {
        if (st < 0) return nullptr;
        auto it = S().byName.find(name);
        return it == S().byName.end() ? nullptr : &S().fields[(std::size_t)it->second];
    }
    bool Has(const char* name) const { return Find(name) != nullptr; }
    bool Fits(const FieldInfo* fi, std::size_t from, std::size_t len) const {
        if (!fi || (std::size_t)fi->offset + from + len > avail) { if (fi) ++f->boundsMiss; return false; }
        return true;
    }
    double Num(const char* name, std::size_t i = 0, double def = 0.0) const {
        const FieldInfo* fi = Find(name);
        if (!fi || fi->ptr || i >= fi->count) return def;
        const Kind k = f->typeKind[(std::size_t)fi->type];
        if (k == Kind::Other || k == Kind::Void) return def;
        if (!Fits(fi, i * fi->elem, fi->elem)) return def;
        const double v = f->ReadKind(k, p + fi->offset + i * fi->elem);
        return Fin(v) ? v : def;
    }
    int Int(const char* name, std::size_t i = 0, int def = 0) const { return SafeInt(Num(name, i, (double)def), -2147483647 - 1, 2147483647); }
    std::uint64_t Ptr(const char* name, std::size_t i = 0) const {
        const FieldInfo* fi = Find(name);
        if (!fi || !fi->ptr || i >= fi->count) return 0;
        if (!Fits(fi, i * fi->elem, fi->elem)) return 0;
        return f->PtrAt(p + fi->offset + i * fi->elem);
    }
    std::string Str(const char* name, std::size_t maxLen = 4096) const {
        const FieldInfo* fi = Find(name);
        if (!fi || fi->ptr || fi->elem != 1) return std::string();
        if (!Fits(fi, 0, fi->size)) return std::string();
        const char* s = (const char*)p + fi->offset;
        std::size_t k = 0;
        while (k < fi->size && k < maxLen && s[k]) ++k;
        return std::string(s, k);
    }
    // An embedded struct (ListBase, CustomData, ...), as a view.
    Ref Sub(const char* name) const {
        Ref r;
        const FieldInfo* fi = Find(name);
        if (!fi || fi->ptr) return r;
        auto it = f->structByName.find(f->typeNames[(std::size_t)fi->type]);
        if (it == f->structByName.end()) return r;
        if (!Fits(fi, 0, fi->size)) return r;
        r.f = f; r.p = p + fi->offset; r.avail = avail - fi->offset; r.st = it->second;
        return r;
    }
    bool ReadFloats(const char* name, float* out, std::size_t n) const {
        const FieldInfo* fi = Find(name);
        if (!fi || fi->ptr || fi->count < n || f->typeKind[(std::size_t)fi->type] != Kind::Float) return false;
        if (!Fits(fi, 0, n * 4)) return false;
        for (std::size_t i = 0; i < n; ++i) out[i] = f->F32(p + fi->offset + i * 4);
        return true;
    }
    // Follow a pointer field to a struct of the named type.
    Ref Deref(const char* field, const char* structName, std::size_t idx = 0) const { return DerefAddr(Ptr(field, idx), structName); }
    Ref DerefAddr(std::uint64_t a, const char* structName) const {
        Ref r;
        if (!a || !f) return r;
        const int si = f->StructIndex(structName);
        if (si < 0) return r;
        Span s = f->At(a);
        if (!s.p || s.n < f->structs[(std::size_t)si].size || f->structs[(std::size_t)si].size == 0) return r;
        r.f = f; r.p = s.p; r.avail = s.n; r.st = si; r.addr = a;
        return r;
    }
    // Follow a pointer field to a struct whatever its type (the struct index the writer recorded for the block).
    Ref DerefAny(const char* field) const {
        Ref r;
        const std::uint64_t a = Ptr(field);
        if (!a) return r;
        Span s = f->At(a);
        if (!s.p || s.st < 0 || s.n < f->structs[(std::size_t)s.st].size) return r;
        r.f = f; r.p = s.p; r.avail = s.n; r.st = s.st; r.addr = a;
        return r;
    }
    // ListBase `field`: its items as views of struct `structName` (each item's first field is `next`).
    bool List(const char* field, const char* structName, std::vector<Ref>& out, std::size_t cap = kMaxList) const {
        out.clear();
        const FieldInfo* fi = Find(field);
        if (!fi || fi->ptr || !Fits(fi, 0, (std::size_t)f->ps)) return false;
        std::uint64_t a = f->PtrAt(p + fi->offset);   // ListBase.first
        const int si = f->StructIndex(structName);
        if (si < 0) return false;
        bool clean = true;
        while (a) {
            if (out.size() >= cap) { clean = false; break; }
            Ref r = DerefAddr(a, structName);
            if (!r) { clean = false; break; }
            out.push_back(r);
            if (r.avail < (std::size_t)f->ps) { clean = false; break; }
            a = f->PtrAt(r.p);
        }
        return clean;
    }
    // Element i of an array of this struct.
    Ref Elem(std::size_t i) const {
        Ref r;
        const std::size_t sz = st >= 0 ? S().size : 0;
        if (!sz || (i + 1) * sz > avail) return r;
        r.f = f; r.p = p + i * sz; r.avail = avail - i * sz; r.st = st; r.addr = addr ? addr + i * sz : 0;
        return r;
    }
};

// ------------------------------------------------------------------------------------------------
// Math helpers (Blender: right handed, Z up, column major 4x4 with the translation in m[12..14])
// ------------------------------------------------------------------------------------------------

inline Mat4 RotAxisMat(int axis, double a) {
    const float c = (float)std::cos(a), s = (float)std::sin(a);
    Mat4 m;
    if (axis == 0) { m.at(1, 1) = c; m.at(2, 1) = -s; m.at(1, 2) = s; m.at(2, 2) = c; }
    else if (axis == 1) { m.at(0, 0) = c; m.at(2, 0) = s; m.at(0, 2) = -s; m.at(2, 2) = c; }
    else { m.at(0, 0) = c; m.at(1, 0) = -s; m.at(0, 1) = s; m.at(1, 1) = c; }
    return m;
}
// Euler rotation in Blender's convention: order code 1..6 = XYZ XZY YXZ YZX ZXY ZYX, the first named axis is applied first.
inline Mat4 EulerMat(const double e[3], int order) {
    static const int ax[7][3] = {{0, 1, 2}, {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    if (order < 1 || order > 6) order = 1;
    const int* a = ax[order];
    return RotAxisMat(a[2], e[a[2]]) * RotAxisMat(a[1], e[a[1]]) * RotAxisMat(a[0], e[a[0]]);
}
inline Mat4 QuatMat(double w, double x, double y, double z) {
    const double n = std::sqrt(w * w + x * x + y * y + z * z);
    if (!(n > 1e-12)) return Mat4();
    w /= n; x /= n; y /= n; z /= n;
    Mat4 m;
    m.at(0, 0) = (float)(1 - 2 * (y * y + z * z)); m.at(1, 0) = (float)(2 * (x * y - w * z));     m.at(2, 0) = (float)(2 * (x * z + w * y));
    m.at(0, 1) = (float)(2 * (x * y + w * z));     m.at(1, 1) = (float)(1 - 2 * (x * x + z * z)); m.at(2, 1) = (float)(2 * (y * z - w * x));
    m.at(0, 2) = (float)(2 * (x * z - w * y));     m.at(1, 2) = (float)(2 * (y * z + w * x));     m.at(2, 2) = (float)(1 - 2 * (x * x + y * y));
    return m;
}
inline Mat4 AxisAngleMat(double ax, double ay, double az, double ang) {
    const double l = std::sqrt(ax * ax + ay * ay + az * az);
    if (!(l > 1e-12)) return Mat4();
    const double s = std::sin(ang / 2);
    return QuatMat(std::cos(ang / 2), ax / l * s, ay / l * s, az / l * s);
}
inline Mat4 TransMat(double x, double y, double z) { Mat4 m; m.m[12] = (float)x; m.m[13] = (float)y; m.m[14] = (float)z; return m; }
inline Mat4 ScaleMat(double x, double y, double z) { Mat4 m; m.at(0, 0) = (float)x; m.at(1, 1) = (float)y; m.at(2, 2) = (float)z; return m; }
inline bool MatFinite(const Mat4& m) { for (int i = 0; i < 16; ++i) if (!std::isfinite(m.m[i]) || std::fabs(m.m[i]) > 1e15f) return false; return true; }
inline bool IsIdentity(const Mat4& m, float eps = 1e-5f) { for (int i = 0; i < 16; ++i) if (std::fabs(m.m[i] - ((i % 5 == 0) ? 1.0f : 0.0f)) > eps) return false; return true; }
inline Mat4 UpMatrix() {   // Blender is Z up, the engine Y up: (x, y, z) -> (x, z, -y)
    Mat4 m;
    m.at(1, 1) = 0; m.at(2, 2) = 0;
    m.at(1, 2) = -1;
    m.at(2, 1) = 1;
    return m;
}
inline Vec3 Xform(const Mat4& m, const Vec3& v) { const Vec4 r = m * Vec4{v.x, v.y, v.z, 1.0f}; return Vec3{r.x, r.y, r.z}; }
inline Vec3 XformDir(const Mat4& m, const Vec3& v) { const Vec4 r = m * Vec4{v.x, v.y, v.z, 0.0f}; return Vec3{r.x, r.y, r.z}; }

// ------------------------------------------------------------------------------------------------
// Reader
// ------------------------------------------------------------------------------------------------

constexpr int OB_MESH = 1, OB_LAMP = 10, OB_CAMERA = 11, OB_ARMATURE = 25;

// The polygon mesh (positions, faces, loops, UVs, weights, creases) is blendmod::PolyMesh, so the modifiers
// (BlendModifiers.cpp) work on it directly.
struct MeshData : blendmod::PolyMesh {
    int colourLayers = 0;
    bool autoSmooth = false, customNormals = false, shapeKeys = false;
    int badFaces = 0;
    bool ok = false;
    std::vector<Vec3> cornerN; bool nDone = false;
};

struct BoneRec {
    std::string name;
    int parent = -1;
    Mat4 arm;              // rest, armature space
    Mat4 restLocal;        // rest, relative to the parent bone (or the armature object)
    Mat4 pose;             // current pose, armature space
    float length = 0.0f;
    int node = -1;
    double loc[3] = {0, 0, 0}, size[3] = {1, 1, 1}, eul[3] = {0, 0, 0}, quat[4] = {1, 0, 0, 0}, axis[3] = {0, 1, 0}, angle = 0;
    int rotmode = 0;
};
struct ArmRec {
    int obj = -1;          // first object that uses it
    std::vector<BoneRec> bones;
    std::unordered_map<std::string, int> byName;
    bool ok = false;
};

struct ObjRec {
    Ref ob;
    std::uint64_t addr = 0;
    std::string name;
    int type = 0;
    bool include = false;
    int parent = -1;           // index in objs, or -1
    int node = -1;
    int armIdx = -1;
    Mat4 pre;                  // node local = pre * (T R S): parent inverse (and the bone tail offset)
    Mat4 world;                // Blender space
    bool placed = false;
};

struct Hash3 {
    std::size_t operator()(const std::array<std::uint32_t, 6>& k) const {
        std::size_t h = 1469598103934665603ull;
        for (std::uint32_t v : k) { h ^= v; h *= 1099511628211ull; }
        return (std::size_t)(h ^ (h >> 31));
    }
};

struct Reader {
    File f;
    std::string baseDir;
    bool flatten = false;
    std::string err;
    ImportedModel* out = nullptr;

    std::vector<ObjRec> objs;
    std::unordered_map<std::uint64_t, int> objByAddr;
    std::unordered_map<std::uint64_t, int> matByAddr;
    std::unordered_map<std::uint64_t, int> armByAddr;
    std::vector<ArmRec> arms;
    std::unordered_map<std::uint64_t, std::shared_ptr<MeshData>> meshCache;
    std::unordered_set<std::string> usedNodeNames;
    std::size_t outVerts = 0, outKeys = 0;
    long long clipBudget = 300000000;   // inner steps of ear clipping over the whole file
    double fps = 24.0;
    Ref scene;

    std::map<std::string, int> skippedMods;   // "'Name' (Type)" -> objects
    int cntModDisabled = 0, cntModCapped = 0, cntMirrorBisect = 0, cntMirrorNoObj = 0, cntSubsurfUnknownUv = 0;
    std::size_t subdivFaceBudget = 4000000;    // faces made by subdivision over the whole file
    int cntCurves = 0, cntLamps = 0, cntCameras = 0, cntOtherTypes = 0, cntModifiers = 0, cntConstraints = 0, cntDupli = 0;
    int cntShapeKeys = 0, cntVertexCol = 0, cntAutoSmooth = 0, cntBadFaces = 0, cntVertexParent = 0, cntBoneParent = 0;
    int cntPacked = 0, cntBadMesh = 0, cntNoBone = 0, cntMatMismatch = 0, cntIpo = 0, cntNla = 0, cntDrivers = 0;
    int cntFcurveMods = 0, cntEasing = 0, cntNodeMat = 0, cntTruncList = 0, cntNgonFan = 0, cntSheared = 0, cntOtherPath = 0;
    int cntDeltaRot = 0, cntBadLayout = 0, cntSkinSkipped = 0, cntCustomNormals = 0, cntParentCycle = 0, cntNoScene = 0;
    int cntUnitScale = 0, cntExtrapolate = 0, cntBigAnim = 0, cntFpt = 0, cntLinked = 0, cntHiddenSkipped = 0, cntBadObj = 0, cntOtherAnim = 0, cntBoneConstraints = 0, cntBadArm = 0, cntNoFaces = 0, cntGroupsNoArm = 0;
    std::string firstOtherPath;

    bool Fail(const std::string& m) { if (err.empty()) err = m; return false; }
    Ref Root() { Ref r; r.f = &f; return r; }
    Ref At(std::uint64_t a, const char* st) { return Root().DerefAddr(a, st); }

    static std::string IdName(const Ref& r) {
        const Ref id = r.Sub("id");
        std::string n = id ? id.Str("name", 130) : std::string();
        if (n.size() >= 2) n.erase(0, 2);
        return n;
    }
    std::string UniqueNodeName(const std::string& base) {
        std::string n = base.empty() ? std::string("node") : base;
        if (usedNodeNames.insert(n).second) return n;
        for (int i = 1; i < 1000000; ++i) {
            const std::string t = n + "." + std::to_string(i);
            if (usedNodeNames.insert(t).second) return t;
        }
        return n;
    }
    int StructOffset(const char* st, const char* field) {
        const int si = f.StructIndex(st);
        if (si < 0) return -1;
        auto it = f.structs[(std::size_t)si].byName.find(field);
        return it == f.structs[(std::size_t)si].byName.end() ? -1 : (int)f.structs[(std::size_t)si].fields[(std::size_t)it->second].offset;
    }
    std::uint32_t StructSize(const char* st) { const int si = f.StructIndex(st); return si < 0 ? 0 : f.structs[(std::size_t)si].size; }

    // ---- Texture file names ------------------------------------------------------------------
    std::string ResolveImage(const Ref& img) {
        if (!img) return std::string();
        if (img.Ptr("packedfile")) ++cntPacked;
        std::string n = img.Str("name", 1100);
        if (n.empty()) return n;
        for (char& c : n) if (c == '\\') c = '/';
        if (n.size() >= 2 && n[0] == '/' && n[1] == '/') n = baseDir + n.substr(2);   // "//" = relative to the .blend file
        return n;
    }

    // ---- Materials ---------------------------------------------------------------------------
    static Ref SockByName(const std::vector<Ref>& socks, const char* name) {
        for (const Ref& s : socks) if (s.Str("name", 64) == name) return s;
        return Ref();
    }
    static bool SockColor(const Ref& sock, float c[4]) {
        if (!sock) return false;
        Ref dv = sock.DerefAny("default_value");
        return dv && std::string(dv.StructName()) == "bNodeSocketValueRGBA" && dv.ReadFloats("value", c, 4);
    }
    static bool SockFloat(const Ref& sock, float& v) {
        if (!sock) return false;
        Ref dv = sock.DerefAny("default_value");
        float t[1];
        if (dv && std::string(dv.StructName()) == "bNodeSocketValueFloat" && dv.ReadFloats("value", t, 1)) { v = t[0]; return true; }
        return false;
    }
    static Ref LinkedFrom(const std::vector<Ref>& links, std::uint64_t sockAddr) {
        if (!sockAddr) return Ref();
        for (const Ref& l : links) if (l.Ptr("tosock") == sockAddr) return l.Deref("fromnode", "bNode");
        return Ref();
    }
    std::string TexOfNode(const Ref& node) {
        if (!node || node.Str("idname", 64) != "ShaderNodeTexImage") return std::string();
        return ResolveImage(node.Deref("id", "Image"));
    }

    int MaterialIndex(std::uint64_t addr) {
        if (!addr) return -1;
        auto it = matByAddr.find(addr);
        if (it != matByAddr.end()) return it->second;
        Ref ma = At(addr, "Material");
        if (!ma || out->materials.size() >= kMaxMaterials) { matByAddr[addr] = -1; return -1; }
        ImportedMaterial im;
        im.name = IdName(ma);
        const float r = (float)ma.Num("r", 0, 0.8), g = (float)ma.Num("g", 0, 0.8), b = (float)ma.Num("b", 0, 0.8);
        im.hasDiffuse = true;
        // Material alpha counts only when the material is set up to be transparent (legacy: Z / ray transparency bits of `mode`;
        // 2.8+: a blend method other than opaque).
        float alpha = 1.0f;
        {
            const float a0 = (float)(ma.Has("a") ? ma.Num("a", 0, 1.0) : ma.Num("alpha", 0, 1.0));
            const bool legacyT = ma.Has("mode") && (ma.Int("mode") & (0x40 | 0x10000 | 0x20000)) != 0;
            const bool newT = ma.Has("blend_method") && ma.Int("blend_method") != 0;
            if (legacyT || newT) alpha = a0;
        }
        im.diffuse = Color{r, g, b, alpha};
        if (ma.Has("emit") && ma.Num("emit") > 0.0) {
            const float e = (float)ma.Num("emit");
            im.hasEmissive = true; im.emissive = Color{r * e, g * e, b * e, 1.0f};
        }
        if (ma.Has("har")) im.shininess = std::min(1000.0f, (float)ma.Num("har"));   // 2.4x - 2.7x hardness (1..511)
        if (ma.Has("metallic")) im.metallic = (float)ma.Num("metallic", 0, -1.0);
        if (ma.Has("roughness") && !ma.Has("har")) im.roughness = (float)ma.Num("roughness", 0, -1.0);
        if (const FieldInfo* mt = ma.Find("mtex")) {   // legacy texture slots
            for (std::size_t i = 0; i < mt->count && i < 32; ++i) {
                Ref slot = ma.Deref("mtex", "MTex", i);
                if (!slot) continue;
                Ref tex = slot.Deref("tex", "Tex");
                if (!tex || tex.Int("type") != 8) continue;   // TEX_IMAGE
                const std::string path = ResolveImage(tex.Deref("ima", "Image"));
                if (path.empty()) continue;
                const int mapto = slot.Int("mapto");
                if ((mapto & 1) && im.texture.empty()) im.texture = path;               // MAP_COL
                else if ((mapto & 2) && im.normalMap.empty()) im.normalMap = path;      // MAP_NORM
                else if ((mapto & 4) && im.specularMap.empty()) im.specularMap = path;  // MAP_COLSPEC
            }
        }
        if (ma.Int("use_nodes") != 0) {   // node based: the Principled BSDF's inputs
            Ref tree = ma.Deref("nodetree", "bNodeTree");
            std::vector<Ref> nodes, links;
            bool found = false;
            if (tree && tree.List("nodes", "bNode", nodes, 100000)) {
                tree.List("links", "bNodeLink", links, 400000);
                for (const Ref& nd : nodes) {
                    if (nd.Str("idname", 64) != "ShaderNodeBsdfPrincipled") continue;
                    std::vector<Ref> ins;
                    nd.List("inputs", "bNodeSocket", ins, 200);
                    float c[4];
                    Ref sBase = SockByName(ins, "Base Color");
                    if (SockColor(sBase, c)) { im.hasDiffuse = true; im.diffuse = Color{c[0], c[1], c[2], c[3]}; }
                    std::string tp = TexOfNode(LinkedFrom(links, sBase.addr));
                    if (!tp.empty()) im.texture = tp;
                    float v;
                    if (SockFloat(SockByName(ins, "Metallic"), v)) im.metallic = v;
                    if (SockFloat(SockByName(ins, "Roughness"), v)) im.roughness = v;
                    if (SockFloat(SockByName(ins, "Alpha"), v)) im.diffuse.a = v;
                    float strength = 1.0f;
                    if (!SockFloat(SockByName(ins, "Emission Strength"), strength)) strength = 1.0f;
                    if (SockColor(SockByName(ins, "Emission"), c) && (c[0] > 0 || c[1] > 0 || c[2] > 0) && strength > 0) { im.hasEmissive = true; im.emissive = Color{c[0] * strength, c[1] * strength, c[2] * strength, 1.0f}; }
                    Ref nn = LinkedFrom(links, SockByName(ins, "Normal").addr);
                    if (nn && nn.Str("idname", 64) == "ShaderNodeNormalMap") {
                        std::vector<Ref> nins;
                        nn.List("inputs", "bNodeSocket", nins, 50);
                        tp = TexOfNode(LinkedFrom(links, SockByName(nins, "Color").addr));
                        if (!tp.empty()) im.normalMap = tp;
                    }
                    found = true;
                    break;
                }
            }
            if (!found) ++cntNodeMat;
        }
        const int idx = (int)out->materials.size();
        out->materials.push_back(std::move(im));
        matByAddr[addr] = idx;
        return idx;
    }

    // ---- Meshes ------------------------------------------------------------------------------
    // CustomData layers of `cd` (a CustomData struct view).
    std::vector<Ref> Layers(const Ref& cd) {
        std::vector<Ref> v;
        if (!cd) return v;
        const std::uint64_t a = cd.Ptr("layers");
        const int n = cd.Int("totlayer");
        Ref first = At(a, "CustomDataLayer");
        if (!first || n <= 0) return v;
        for (int i = 0; i < n && i < 512; ++i) { Ref l = first.Elem((std::size_t)i); if (!l) break; v.push_back(l); }
        return v;
    }
    static const Ref* FindLayerByName(const std::vector<Ref>& ls, const char* name) {
        for (const Ref& l : ls) if (l.Str("name", 64) == name) return &l;
        return nullptr;
    }
    static const Ref* FindLayerByType(const std::vector<Ref>& ls, int type) {
        for (const Ref& l : ls) if (l.Int("type") == type) return &l;
        return nullptr;
    }
    Span LayerData(const Ref& l) { return f.At(l.Ptr("data")); }
    Ref Cd(const Ref& me, const char* a, const char* b) { Ref r = me.Sub(a); return r ? r : me.Sub(b); }

    std::shared_ptr<MeshData> ReadMesh(std::uint64_t meshAddr) {
        auto it = meshCache.find(meshAddr);
        if (it != meshCache.end()) return it->second;
        auto md = std::make_shared<MeshData>();
        meshCache[meshAddr] = md;
        Ref me = At(meshAddr, "Mesh");
        if (!me) return md;
        const Ref vd = Cd(me, "vdata", "vert_data"), pd = Cd(me, "pdata", "face_data"), ld = Cd(me, "ldata", "corner_data");
        const std::vector<Ref> vl = Layers(vd), pl = Layers(pd), ll = Layers(ld);
        const int nv = me.Has("totvert") ? me.Int("totvert") : me.Int("verts_num");
        int npoly = me.Has("totpoly") ? me.Int("totpoly") : me.Int("faces_num");
        int nloop = me.Has("totloop") ? me.Int("totloop") : me.Int("corners_num");
        const int nface = me.Int("totface");
        if (nv < 0 || npoly < 0 || nloop < 0 || (std::size_t)nv > kMaxVerts || (std::size_t)npoly > kMaxFaces || (std::size_t)nloop > kMaxCorners) { ++cntBadMesh; return md; }

        // positions
        {
            Span sp; int stride = 0, off = 0;
            if (me.Ptr("mvert")) {
                sp = f.At(me.Ptr("mvert")); stride = (int)StructSize("MVert"); off = StructOffset("MVert", "co");
            } else if (const Ref* l = FindLayerByName(vl, "position")) {
                sp = LayerData(*l); stride = 12; off = 0;
            } else if (const Ref* l = FindLayerByType(vl, 0)) {   // CD_MVERT
                sp = LayerData(*l); stride = (int)StructSize("MVert"); off = StructOffset("MVert", "co");
            }
            if (!sp.p || stride < 12 || off < 0 || off + 12 > stride) { if (nv > 0) ++cntBadMesh; return md; }
            std::size_t cnt = (std::size_t)nv;
            if (cnt * (std::size_t)stride > sp.n) { cnt = sp.n / (std::size_t)stride; ++cntBadMesh; }
            md->pos.resize(cnt);
            for (std::size_t i = 0; i < cnt; ++i) {
                const u8* q = sp.p + i * (std::size_t)stride + (std::size_t)off;
                Vec3 v{f.F32(q), f.F32(q + 4), f.F32(q + 8)};
                if (!InRange(v)) { v = Vec3{0, 0, 0}; ++md->badFaces; }
                md->pos[i] = v;
            }
        }
        const int nvert = (int)md->pos.size();

        // faces: 1) MPoly + MLoop (2.63 - 3.5), 2) face offsets + .corner_vert (3.6+), 3) MFace (up to 2.6x)
        std::vector<int> faceMatIdx; bool matFromPoly = false;
        Span polyS, loopS;
        int pStride = (int)StructSize("MPoly"), lStride = (int)StructSize("MLoop");
        if (me.Ptr("mpoly") && me.Ptr("mloop")) { polyS = f.At(me.Ptr("mpoly")); loopS = f.At(me.Ptr("mloop")); }
        else if (const Ref* a = FindLayerByType(pl, 25)) { if (const Ref* b = FindLayerByType(ll, 26)) { polyS = LayerData(*a); loopS = LayerData(*b); } }
        if (polyS.p && loopS.p && npoly > 0 && pStride >= 12 && lStride >= 8) {
            const int oLs = StructOffset("MPoly", "loopstart"), oTl = StructOffset("MPoly", "totloop"), oMat = StructOffset("MPoly", "mat_nr"), oFl = StructOffset("MPoly", "flag");
            const int oV = StructOffset("MLoop", "v");
            if (oLs < 0 || oTl < 0 || oV < 0) { ++cntBadMesh; return md; }
            std::size_t np = (std::size_t)npoly, nl = (std::size_t)nloop;
            if (np * (std::size_t)pStride > polyS.n) { np = polyS.n / (std::size_t)pStride; ++cntBadMesh; }
            if (nl * (std::size_t)lStride > loopS.n) { nl = loopS.n / (std::size_t)lStride; ++cntBadMesh; }
            md->loopVert.resize(nl);
            for (std::size_t i = 0; i < nl; ++i) { const std::uint32_t u = f.U32(loopS.p + i * (std::size_t)lStride + (std::size_t)oV); md->loopVert[i] = u > 0x7fffffffu ? -1 : (int)u; }
            for (std::size_t i = 0; i < np; ++i) {
                const u8* q = polyS.p + i * (std::size_t)pStride;
                const std::uint32_t ls = f.U32(q + oLs); const std::int32_t tl = (std::int32_t)f.U32(q + oTl);
                if (tl < 3 || (std::uint64_t)ls + (std::uint64_t)tl > nl) { ++md->badFaces; continue; }
                md->faceStart.push_back((int)ls); md->faceCount.push_back(tl);
                md->faceMat.push_back(oMat >= 0 ? (int)(std::int16_t)f.U16(q + oMat) : 0);
                md->faceSmooth.push_back(oFl >= 0 ? (char)(q[oFl] & 1) : 1);
            }
            matFromPoly = true;
        } else if ((me.Ptr("poly_offset_indices") || me.Ptr("face_offset_indices")) && npoly > 0) {
            Span offS = f.At(me.Ptr("poly_offset_indices") ? me.Ptr("poly_offset_indices") : me.Ptr("face_offset_indices"));
            const Ref* cv = FindLayerByName(ll, ".corner_vert");
            if (!offS.p || !cv) { ++cntBadMesh; return md; }
            Span cvS = LayerData(*cv);
            std::size_t np = (std::size_t)npoly, nl = (std::size_t)nloop;
            if ((np + 1) * 4 > offS.n) { np = offS.n / 4 ? offS.n / 4 - 1 : 0; ++cntBadMesh; }
            if (nl * 4 > cvS.n) { nl = cvS.n / 4; ++cntBadMesh; }
            md->loopVert.resize(nl);
            for (std::size_t i = 0; i < nl; ++i) { const std::uint32_t u = f.U32(cvS.p + i * 4); md->loopVert[i] = u > 0x7fffffffu ? -1 : (int)u; }
            for (std::size_t i = 0; i < np; ++i) {
                const std::uint32_t a = f.U32(offS.p + i * 4), b = f.U32(offS.p + (i + 1) * 4);
                if ((std::uint64_t)b < (std::uint64_t)a + 3 || b > nl) { ++md->badFaces; continue; }
                md->faceStart.push_back((int)a); md->faceCount.push_back((int)(b - a)); md->faceMat.push_back(0); md->faceSmooth.push_back(1);
            }
            // face attributes by name: material_index (int), sharp_face (bool)
            const Ref* mi = FindLayerByName(pl, "material_index");
            const Ref* sf = FindLayerByName(pl, "sharp_face");
            Span ms = mi ? LayerData(*mi) : Span(), ss = sf ? LayerData(*sf) : Span();
            // faces dropped as bad keep their attribute slot, so index by the original face number
            md->faceStart.clear(); md->faceCount.clear(); md->faceMat.clear(); md->faceSmooth.clear();
            for (std::size_t i = 0; i < np; ++i) {
                const std::uint32_t a = f.U32(offS.p + i * 4), b = f.U32(offS.p + (i + 1) * 4);
                if ((std::uint64_t)b < (std::uint64_t)a + 3 || b > nl) continue;
                md->faceStart.push_back((int)a); md->faceCount.push_back((int)(b - a));
                md->faceMat.push_back(ms.p && (i + 1) * 4 <= ms.n ? (int)(std::int32_t)f.U32(ms.p + i * 4) : 0);
                md->faceSmooth.push_back(ss.p && i < ss.n ? (char)(ss.p[i] ? 0 : 1) : 1);
            }
            matFromPoly = true;
        } else if (nface > 0 && (me.Ptr("mface") || FindLayerByType(Layers(Cd(me, "fdata", "fdata_legacy")), 4))) {
            Span fs; 
            if (me.Ptr("mface")) fs = f.At(me.Ptr("mface"));
            else fs = LayerData(*FindLayerByType(Layers(Cd(me, "fdata", "fdata_legacy")), 4));
            const int fStride = (int)StructSize("MFace");
            const int o1 = StructOffset("MFace", "v1"), o2 = StructOffset("MFace", "v2"), o3 = StructOffset("MFace", "v3"), o4 = StructOffset("MFace", "v4");
            const int oMat = StructOffset("MFace", "mat_nr"), oFl = StructOffset("MFace", "flag");
            if (!fs.p || fStride < 16 || o1 < 0 || o2 < 0 || o3 < 0 || o4 < 0) { ++cntBadMesh; return md; }
            std::size_t nfc = (std::size_t)nface;
            if (nfc * (std::size_t)fStride > fs.n) { nfc = fs.n / (std::size_t)fStride; ++cntBadMesh; }
            // UVs: MTFace (2.47+) or TFace (older); colours are only counted
            Span uvS; int uvStride = 0, uvOff = 0;
            if (me.Ptr("mtface")) { uvS = f.At(me.Ptr("mtface")); uvStride = (int)StructSize("MTFace"); uvOff = StructOffset("MTFace", "uv"); }
            else if (me.Ptr("tface")) { uvS = f.At(me.Ptr("tface")); uvStride = (int)StructSize("TFace"); uvOff = StructOffset("TFace", "uv"); }
            std::vector<Vec2> uvs;
            bool haveUv = uvS.p && uvStride > 0 && uvOff >= 0 && uvOff + 32 <= uvStride;
            for (std::size_t i = 0; i < nfc; ++i) {
                const u8* q = fs.p + i * (std::size_t)fStride;
                std::uint32_t v[4] = {f.U32(q + o1), f.U32(q + o2), f.U32(q + o3), f.U32(q + o4)};
                // a legacy face is a quad when the fourth index is not zero; a zero third index marks an unused face
                int n = v[3] != 0 ? 4 : 3;
                if (v[2] == 0 && v[3] != 0) { n = 4; }
                if (v[2] == 0 && v[3] == 0) { ++md->badFaces; continue; }
                bool bad = false;
                for (int k = 0; k < n; ++k) if (v[k] >= (std::uint32_t)nvert) bad = true;
                if (bad) { ++md->badFaces; continue; }
                md->faceStart.push_back((int)md->loopVert.size()); md->faceCount.push_back(n);
                md->faceMat.push_back(oMat >= 0 ? (int)(std::int8_t)q[oMat] : 0);
                md->faceSmooth.push_back(oFl >= 0 ? (char)(q[oFl] & 1) : 0);
                for (int k = 0; k < n; ++k) md->loopVert.push_back((int)v[k]);
                if (haveUv) {
                    if ((i + 1) * (std::size_t)uvStride <= uvS.n) {
                        const u8* uq = uvS.p + i * (std::size_t)uvStride + (std::size_t)uvOff;
                        for (int k = 0; k < n; ++k) uvs.push_back(Vec2{f.F32(uq + k * 8), f.F32(uq + k * 8 + 4)});
                    } else { haveUv = false; uvs.clear(); }
                }
            }
            if (haveUv && uvs.size() == md->loopVert.size()) md->loopUv = uvs;
            matFromPoly = true;
        } else if (nvert > 0 && npoly == 0 && nface == 0) {
            // vertices only (or edges only): nothing to draw
        } else if (npoly > 0 || nface > 0) { ++cntBadMesh; }

        // drop faces that reference vertices outside the vertex list
        {
            std::size_t w = 0;
            for (std::size_t i = 0; i < md->faceStart.size(); ++i) {
                bool bad = false;
                for (int k = 0; k < md->faceCount[i]; ++k) { const int v = md->loopVert[(std::size_t)md->faceStart[i] + (std::size_t)k]; if (v < 0 || v >= nvert) { bad = true; break; } }
                if (bad) { ++md->badFaces; continue; }
                md->faceStart[w] = md->faceStart[i]; md->faceCount[w] = md->faceCount[i]; md->faceMat[w] = md->faceMat[i]; md->faceSmooth[w] = md->faceSmooth[i];
                ++w;
            }
            md->faceStart.resize(w); md->faceCount.resize(w); md->faceMat.resize(w); md->faceSmooth.resize(w);
        }
        (void)matFromPoly; (void)faceMatIdx;

        // UVs per loop (the polygon / attribute storage; MFace UVs were read above)
        if (md->loopUv.empty() && !md->loopVert.empty()) {
            Span uvS; int stride = 0, off = 0;
            if (me.Ptr("mloopuv")) { uvS = f.At(me.Ptr("mloopuv")); stride = (int)StructSize("MLoopUV"); off = StructOffset("MLoopUV", "uv"); }
            else {
                std::vector<const Ref*> cand;
                for (const Ref& l : ll) {
                    const int t = l.Int("type");
                    const std::string nm = l.Str("name", 64);
                    if ((t == 16 || t == 49) && (nm.empty() || nm[0] != '.')) cand.push_back(&l);
                }
                if (!cand.empty()) {
                    std::size_t pick = (std::size_t)SafeInt(cand[0]->Num("active_rnd"), 0, 1000);
                    if (pick >= cand.size()) pick = 0;
                    const Ref& l = *cand[pick];
                    uvS = LayerData(l);
                    if (l.Int("type") == 16) { stride = (int)StructSize("MLoopUV"); off = StructOffset("MLoopUV", "uv"); } else { stride = 8; off = 0; }
                }
            }
            if (uvS.p && stride >= 8 && off >= 0 && off + 8 <= stride && uvS.n >= md->loopVert.size() * (std::size_t)stride) {
                md->loopUv.resize(md->loopVert.size());
                for (std::size_t i = 0; i < md->loopVert.size(); ++i) { const u8* q = uvS.p + i * (std::size_t)stride + (std::size_t)off; md->loopUv[i] = Vec2{f.F32(q), f.F32(q + 4)}; }
            }
        }
        for (Vec2& u : md->loopUv) if (!std::isfinite(u.x) || !std::isfinite(u.y) || std::fabs(u.x) > 1e9f || std::fabs(u.y) > 1e9f) u = Vec2{0, 0};

        // vertex colours: counted (the engine mesh has no per-vertex colour channel)
        for (const Ref& l : ll) { const int t = l.Int("type"); const std::string nm = l.Str("name", 64); if ((t == 17 || t == 47) && (nm.empty() || nm[0] != '.')) ++md->colourLayers; if (t == 41) md->customNormals = true; }
        for (const Ref& l : vl) { const int t = l.Int("type"); if (t == 47) ++md->colourLayers; }
        if (me.Ptr("mloopcol") || me.Ptr("mcol")) ++md->colourLayers;
        if (me.Has("flag") && f.version < 410 && (me.Int("flag") & 32)) md->autoSmooth = true;

        // vertex weights
        {
            Span dv; 
            if (me.Ptr("dvert")) dv = f.At(me.Ptr("dvert"));
            else if (const Ref* l = FindLayerByType(vl, 2)) dv = LayerData(*l);
            const int dvStride = (int)StructSize("MDeformVert"), oDw = StructOffset("MDeformVert", "dw"), oTot = StructOffset("MDeformVert", "totweight");
            const int wStride = (int)StructSize("MDeformWeight"), oNr = StructOffset("MDeformWeight", "def_nr"), oW = StructOffset("MDeformWeight", "weight");
            if (dv.p && dvStride > 0 && oDw >= 0 && oTot >= 0 && wStride >= 8 && oNr >= 0 && oW >= 0 && dv.n >= (std::size_t)nvert * (std::size_t)dvStride && nvert > 0) {
                md->dvStart.assign((std::size_t)nvert + 1, 0);
                for (int v = 0; v < nvert; ++v) {
                    const u8* q = dv.p + (std::size_t)v * (std::size_t)dvStride;
                    const std::int32_t tot = (std::int32_t)f.U32(q + oTot);
                    Span ws = tot > 0 ? f.At(f.PtrAt(q + oDw)) : Span();
                    std::size_t k = 0;
                    if (ws.p && tot > 0) {
                        k = (std::size_t)tot;
                        if (k > 4096) k = 4096;
                        if (k * (std::size_t)wStride > ws.n) k = ws.n / (std::size_t)wStride;
                        for (std::size_t j = 0; j < k; ++j) {
                            const u8* w = ws.p + j * (std::size_t)wStride;
                            const std::int32_t g = (std::int32_t)f.U32(w + oNr);
                            const float wt = f.F32(w + oW);
                            if (g < 0 || !(wt > 0.0f) || !std::isfinite(wt)) continue;
                            md->dvGroup.push_back(g); md->dvWeight.push_back(wt);
                        }
                    }
                    md->dvStart[(std::size_t)v + 1] = (std::uint32_t)md->dvGroup.size();
                }
            }
        }
        if (me.Ptr("key")) {
            Ref key = me.Deref("key", "Key");
            std::vector<Ref> blocks;
            if (key && key.List("block", "KeyBlock", blocks, 10000) && blocks.size() > 1) md->shapeKeys = true;
        }
        md->ok = true;
        return md;
    }

    // ---- Armatures ---------------------------------------------------------------------------
    int ReadArmature(std::uint64_t addr) {
        auto it = armByAddr.find(addr);
        if (it != armByAddr.end()) return it->second;
        const int idx = (int)arms.size();
        armByAddr[addr] = idx;
        arms.emplace_back();
        ArmRec& ar = arms.back();
        Ref a = At(addr, "bArmature");
        if (!a) return idx;
        // depth first over the bone tree
        struct Item { Ref bone; int parent; int depth; };
        std::vector<Ref> roots;
        a.List("bonebase", "Bone", roots, kMaxBones);
        std::vector<Item> stack;
        for (std::size_t i = roots.size(); i-- > 0;) stack.push_back({roots[i], -1, 0});
        std::unordered_set<std::uint64_t> seen;
        while (!stack.empty()) {
            Item it2 = stack.back(); stack.pop_back();
            if (ar.bones.size() >= kMaxBones || it2.depth > kMaxDepth) { ++cntTruncList; break; }
            if (!seen.insert(it2.bone.addr).second) continue;
            BoneRec br;
            br.name = it2.bone.Str("name", 64);
            br.parent = it2.parent;
            float m[16];
            if (!it2.bone.ReadFloats("arm_mat", m, 16)) { ar.bones.clear(); ++cntBadArm; return idx; }
            for (int i = 0; i < 16; ++i) br.arm.m[i] = m[i];
            if (!MatFinite(br.arm)) { ar.bones.clear(); ++cntBadArm; return idx; }
            br.length = (float)it2.bone.Num("length");
            br.restLocal = it2.parent >= 0 ? ar.bones[(std::size_t)it2.parent].arm.Inverse() * br.arm : br.arm;
            if (!MatFinite(br.restLocal)) { ar.bones.clear(); ++cntBadArm; return idx; }   // a singular rest matrix
            br.pose = br.arm;
            const int me = (int)ar.bones.size();
            ar.byName.emplace(br.name, me);
            ar.bones.push_back(std::move(br));
            std::vector<Ref> kids;
            it2.bone.List("childbase", "Bone", kids, kMaxBones);
            for (std::size_t i = kids.size(); i-- > 0;) stack.push_back({kids[i], me, it2.depth + 1});
        }
        ar.ok = !ar.bones.empty();
        return idx;
    }

    // Rotation of an object / pose bone from its stored components, as a matrix.
    static Mat4 RotationOf(int rotmode, const double eul[3], const double quat[4], const double axis[3], double angle) {
        if (rotmode > 0) return EulerMat(eul, rotmode);
        if (rotmode == 0) return QuatMat(quat[0], quat[1], quat[2], quat[3]);
        return AxisAngleMat(axis[0], axis[1], axis[2], angle);
    }

    // ---- Scene selection ---------------------------------------------------------------------
    void CollectCollection(const Ref& coll, std::unordered_set<std::uint64_t>& seen, std::unordered_set<std::uint64_t>& objs_, int depth) {
        if (!coll || depth > 64 || !seen.insert(coll.addr).second) return;
        std::vector<Ref> items;
        coll.List("gobject", "CollectionObject", items, kMaxList);
        for (const Ref& it : items) objs_.insert(it.Ptr("ob"));
        std::vector<Ref> kids;
        coll.List("children", "CollectionChild", kids, kMaxList);
        for (const Ref& k : kids) CollectCollection(k.Deref("collection", "Collection"), seen, objs_, depth + 1);
    }

    // ---- Animation ---------------------------------------------------------------------------
    struct Key { double x, y, h1x, h1y, h2x, h2y; int ipo; };
    struct Curve { std::vector<Key> keys; int extend = 0; };
    static double EvalCurve(const Curve& c, double frame) {
        const std::vector<Key>& k = c.keys;
        if (k.empty()) return 0.0;
        if (frame <= k.front().x) return k.front().y;
        if (frame >= k.back().x) return k.back().y;
        std::size_t lo = 0, hi = k.size() - 1;
        while (hi - lo > 1) { const std::size_t mid = (lo + hi) / 2; if (k[mid].x <= frame) lo = mid; else hi = mid; }
        const Key& a = k[lo]; const Key& b = k[hi];
        const double dx = b.x - a.x;
        if (!(dx > 0)) return b.y;
        if (a.ipo == 0) return a.y;                                          // constant
        if (a.ipo != 2) return a.y + (b.y - a.y) * (frame - a.x) / dx;       // linear (and easing types, approximated)
        double h2x = a.h2x, h2y = a.h2y, h1x = b.h1x, h1y = b.h1y;
        const double l1 = h2x - a.x, l2 = b.x - h1x;
        if (l1 + l2 > dx && l1 + l2 > 0) {                                   // handles may not cross: scale them back
            const double fac = dx / (l1 + l2);
            h2x = a.x + (h2x - a.x) * fac; h2y = a.y + (h2y - a.y) * fac;
            h1x = b.x - (b.x - h1x) * fac; h1y = b.y - (b.y - h1y) * fac;
        }
        double t0 = 0, t1 = 1;
        for (int i = 0; i < 40; ++i) {
            const double t = 0.5 * (t0 + t1), u = 1 - t;
            const double x = u * u * u * a.x + 3 * u * u * t * h2x + 3 * u * t * t * h1x + t * t * t * b.x;
            if (x < frame) t0 = t; else t1 = t;
        }
        const double t = 0.5 * (t0 + t1), u = 1 - t;
        return u * u * u * a.y + 3 * u * u * t * h2y + 3 * u * t * t * h1y + t * t * t * b.y;
    }

    bool LoadCurve(const Ref& fc, Curve& c) {
        c = Curve();
        c.extend = fc.Int("extend");
        const int n = fc.Int("totvert");
        if (n <= 0 || (std::size_t)n > kMaxKeysPerCurve) return false;
        if (fc.Ptr("bezt")) {
            Span s = f.At(fc.Ptr("bezt"));
            const std::uint32_t sz = StructSize("BezTriple");
            if (!s.p || sz < 36) return false;
            std::size_t cnt = (std::size_t)n;
            if (cnt * sz > s.n) cnt = s.n / sz;
            const int oIpo = StructOffset("BezTriple", "ipo"), oVec = StructOffset("BezTriple", "vec");
            if (oVec < 0) return false;
            for (std::size_t i = 0; i < cnt; ++i) {
                const u8* q = s.p + i * sz + (std::size_t)oVec;
                Key k;
                k.h1x = f.F32(q); k.h1y = f.F32(q + 4); k.x = f.F32(q + 12); k.y = f.F32(q + 16); k.h2x = f.F32(q + 24); k.h2y = f.F32(q + 28);
                k.ipo = oIpo >= 0 ? (int)s.p[i * sz + (std::size_t)oIpo] : 1;
                if (!Fin(k.x) || !Fin(k.y) || !Fin(k.h1x) || !Fin(k.h1y) || !Fin(k.h2x) || !Fin(k.h2y)) continue;
                if (!c.keys.empty() && !(k.x > c.keys.back().x)) continue;   // keys must go forward in time
                if (k.ipo > 2) ++cntEasing;
                c.keys.push_back(k);
            }
        } else if (fc.Ptr("fpt")) {
            Span s = f.At(fc.Ptr("fpt"));
            const std::uint32_t sz = StructSize("FPoint");
            if (!s.p || sz < 8) return false;
            ++cntFpt;
            std::size_t cnt = (std::size_t)n;
            if (cnt * sz > s.n) cnt = s.n / sz;
            for (std::size_t i = 0; i < cnt; ++i) {
                const u8* q = s.p + i * sz;
                Key k; k.x = f.F32(q); k.y = f.F32(q + 4); k.h1x = k.h2x = k.x; k.h1y = k.h2y = k.y; k.ipo = 1;
                if (!Fin(k.x) || !Fin(k.y)) continue;
                if (!c.keys.empty() && !(k.x > c.keys.back().x)) continue;
                c.keys.push_back(k);
            }
        }
        return !c.keys.empty();
    }

    struct Target {
        int node = -1;
        bool isBone = false;
        int obj = -1, bone = -1;
        Curve loc[3], scl[3], eul[3], quat[4], axis[4];   // axis: x y z angle
        bool has[5][4] = {};                               // loc, scl, eul, quat, axisangle
    };

    // ---- Run ---------------------------------------------------------------------------------
    bool Run(ImportedModel& model);
    void BuildNodes();
    void BuildParts();
    void BuildAnimation();
    void BuildPartsOf(ObjRec& o, int oi);
    std::shared_ptr<MeshData> EvaluateModifiers(ObjRec& o, const std::shared_ptr<MeshData>& base, std::uint64_t meshAddr);
    void ReadCreases(std::uint64_t meshAddr, MeshData& md);
    // Up to 2.8x every Subdivision Surface uses creases; from 2.90 (the DNA with the "quality" field) only with flag 16
    // ("Use Creases"; the bit value is from memory of the DNA headers, unverified).
    bool SubsurfUsesCreases(const Ref& md) const { return (md.Has("quality") || f.version >= 290) ? (md.Int("flags") & 16) != 0 : true; }
    std::vector<std::string> GroupNames(const ObjRec& o, const Ref& me);
    void Notes();
};

} // namespace blnd
} // namespace okay
