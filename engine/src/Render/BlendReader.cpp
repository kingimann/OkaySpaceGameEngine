// Our own Blender (.blend) reader. A .blend file is a stream of blocks (a 4 byte code, a size, the
// writer's memory address of the block, the index of the struct it holds and an element count) plus
// one "DNA1" block that describes every struct the writer knew (names, types, field order). The
// reader is driven by that description: every field is found by name and offset in the file's own
// layout, so one reader covers Blender 2.4x to 4.x, 32 and 64 bit pointers, both endians. Pointers in
// the file are the writer's old addresses; they are resolved through a table of block start addresses.
// Written from the public description of the file layout (the "DNA" self description), with no code
// from any other importer.
//
// Reads: objects (transforms, parenting incl. parent inverse, bone parents), meshes in both the old
// (MVert / MFace / MPoly + MLoop) and the new attribute-based storage, UVs, materials (legacy colours
// and image textures, node based Principled colours and image textures), armatures and vertex group
// skin weights, and action F-Curves (location, rotation, scale of objects and pose bones).
// Everything it did not read is listed in ImportedModel::notes. See docs/own_blend_reader.md.
#include "okay/Render/ModelData.hpp"
#include "okay/Core/ImageDecode.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace okay {

namespace {
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

Mat4 RotAxisMat(int axis, double a) {
    const float c = (float)std::cos(a), s = (float)std::sin(a);
    Mat4 m;
    if (axis == 0) { m.at(1, 1) = c; m.at(2, 1) = -s; m.at(1, 2) = s; m.at(2, 2) = c; }
    else if (axis == 1) { m.at(0, 0) = c; m.at(2, 0) = s; m.at(0, 2) = -s; m.at(2, 2) = c; }
    else { m.at(0, 0) = c; m.at(1, 0) = -s; m.at(0, 1) = s; m.at(1, 1) = c; }
    return m;
}
// Euler rotation in Blender's convention: order code 1..6 = XYZ XZY YXZ YZX ZXY ZYX, the first named axis is applied first.
Mat4 EulerMat(const double e[3], int order) {
    static const int ax[7][3] = {{0, 1, 2}, {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    if (order < 1 || order > 6) order = 1;
    const int* a = ax[order];
    return RotAxisMat(a[2], e[a[2]]) * RotAxisMat(a[1], e[a[1]]) * RotAxisMat(a[0], e[a[0]]);
}
Mat4 QuatMat(double w, double x, double y, double z) {
    const double n = std::sqrt(w * w + x * x + y * y + z * z);
    if (!(n > 1e-12)) return Mat4();
    w /= n; x /= n; y /= n; z /= n;
    Mat4 m;
    m.at(0, 0) = (float)(1 - 2 * (y * y + z * z)); m.at(1, 0) = (float)(2 * (x * y - w * z));     m.at(2, 0) = (float)(2 * (x * z + w * y));
    m.at(0, 1) = (float)(2 * (x * y + w * z));     m.at(1, 1) = (float)(1 - 2 * (x * x + z * z)); m.at(2, 1) = (float)(2 * (y * z - w * x));
    m.at(0, 2) = (float)(2 * (x * z - w * y));     m.at(1, 2) = (float)(2 * (y * z + w * x));     m.at(2, 2) = (float)(1 - 2 * (x * x + y * y));
    return m;
}
Mat4 AxisAngleMat(double ax, double ay, double az, double ang) {
    const double l = std::sqrt(ax * ax + ay * ay + az * az);
    if (!(l > 1e-12)) return Mat4();
    const double s = std::sin(ang / 2);
    return QuatMat(std::cos(ang / 2), ax / l * s, ay / l * s, az / l * s);
}
Mat4 TransMat(double x, double y, double z) { Mat4 m; m.m[12] = (float)x; m.m[13] = (float)y; m.m[14] = (float)z; return m; }
Mat4 ScaleMat(double x, double y, double z) { Mat4 m; m.at(0, 0) = (float)x; m.at(1, 1) = (float)y; m.at(2, 2) = (float)z; return m; }
bool MatFinite(const Mat4& m) { for (int i = 0; i < 16; ++i) if (!std::isfinite(m.m[i]) || std::fabs(m.m[i]) > 1e15f) return false; return true; }
bool IsIdentity(const Mat4& m, float eps = 1e-5f) { for (int i = 0; i < 16; ++i) if (std::fabs(m.m[i] - ((i % 5 == 0) ? 1.0f : 0.0f)) > eps) return false; return true; }
Mat4 UpMatrix() {   // Blender is Z up, the engine Y up: (x, y, z) -> (x, z, -y)
    Mat4 m;
    m.at(1, 1) = 0; m.at(2, 2) = 0;
    m.at(1, 2) = -1;
    m.at(2, 1) = 1;
    return m;
}
Vec3 Xform(const Mat4& m, const Vec3& v) { const Vec4 r = m * Vec4{v.x, v.y, v.z, 1.0f}; return Vec3{r.x, r.y, r.z}; }
Vec3 XformDir(const Mat4& m, const Vec3& v) { const Vec4 r = m * Vec4{v.x, v.y, v.z, 0.0f}; return Vec3{r.x, r.y, r.z}; }

// ------------------------------------------------------------------------------------------------
// Reader
// ------------------------------------------------------------------------------------------------

constexpr int OB_MESH = 1, OB_LAMP = 10, OB_CAMERA = 11, OB_ARMATURE = 25;

struct MeshData {
    std::vector<Vec3> pos;
    std::vector<int> faceStart, faceCount, faceMat;
    std::vector<char> faceSmooth;
    std::vector<int> loopVert;
    std::vector<Vec2> loopUv;                 // empty or one per loop
    std::vector<std::uint32_t> dvStart;       // empty or pos.size()+1
    std::vector<int> dvGroup;
    std::vector<float> dvWeight;
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
        return (std::size_t)h;
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
                if (b < a + 3 || b > nl) { ++md->badFaces; continue; }
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
                if (b < a + 3 || b > nl) continue;
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
    void Notes();
};

// ---- helpers for parts -----------------------------------------------------------------------
static Vec3 Cross3(const Vec3& a, const Vec3& b) { return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// Triangles (local corner indices, 3 per triangle) of one polygon.
static void TriangulatePoly(const std::vector<Vec3>& pos, const int* lv, int n, std::vector<int>& tri, int& fanned, long long& budget) {
    tri.clear();
    if (n == 3) { tri = {0, 1, 2}; return; }
    // Newell normal
    double nx = 0, ny = 0, nz = 0;
    for (int i = 0; i < n; ++i) {
        const Vec3& a = pos[(std::size_t)lv[i]]; const Vec3& b = pos[(std::size_t)lv[(i + 1) % n]];
        nx += ((double)a.y - b.y) * ((double)a.z + b.z); ny += ((double)a.z - b.z) * ((double)a.x + b.x); nz += ((double)a.x - b.x) * ((double)a.y + b.y);
    }
    if (n == 4) {
        // split along the diagonal that keeps both triangles facing the polygon's normal
        auto side = [&](int i, int j, int k) {
            const Vec3 e1 = pos[(std::size_t)lv[j]] - pos[(std::size_t)lv[i]], e2 = pos[(std::size_t)lv[k]] - pos[(std::size_t)lv[i]];
            const Vec3 c = Cross3(e1, e2);
            return (double)c.x * nx + (double)c.y * ny + (double)c.z * nz;
        };
        const bool ok02 = side(0, 1, 2) > 0 && side(0, 2, 3) > 0;
        const bool ok13 = side(1, 2, 3) > 0 && side(1, 3, 0) > 0;
        if (!ok02 && ok13) tri = {1, 2, 3, 1, 3, 0}; else tri = {0, 1, 2, 0, 2, 3};
        return;
    }
    if (n > kMaxNgon || budget <= 0) { for (int i = 1; i + 1 < n; ++i) { tri.push_back(0); tri.push_back(i); tri.push_back(i + 1); } ++fanned; return; }
    const double ax = std::fabs(nx), ay = std::fabs(ny), az = std::fabs(nz);
    const int drop = (ax >= ay && ax >= az) ? 0 : (ay >= az ? 1 : 2);
    auto P = [&](int i, double& u, double& v) {
        const Vec3& p = pos[(std::size_t)lv[i]];
        if (drop == 0) { u = p.y; v = p.z; } else if (drop == 1) { u = p.z; v = p.x; } else { u = p.x; v = p.y; }
    };
    const double nd = drop == 0 ? nx : (drop == 1 ? ny : nz);
    const double orient = nd >= 0 ? 1.0 : -1.0;
    std::vector<int> idx((std::size_t)n);
    for (int i = 0; i < n; ++i) idx[(std::size_t)i] = i;
    int guard = 0;
    while (idx.size() > 3 && guard++ < n * n + 8) {
        bool clipped = false;
        const int m = (int)idx.size();
        for (int i = 0; i < m && !clipped; ++i) {
            const int a = idx[(std::size_t)((i + m - 1) % m)], b = idx[(std::size_t)i], c = idx[(std::size_t)((i + 1) % m)];
            double au, av, bu, bv, cu, cv;
            P(a, au, av); P(b, bu, bv); P(c, cu, cv);
            const double cr = ((bu - au) * (cv - bv) - (bv - av) * (cu - bu)) * orient;
            if (!(cr > 1e-18)) continue;   // reflex or degenerate corner
            bool inside = false;
            budget -= m;
            for (int j = 0; j < m && !inside; ++j) {
                const int q = idx[(std::size_t)j];
                if (q == a || q == b || q == c) continue;
                double qu, qv; P(q, qu, qv);
                const double d1 = ((bu - au) * (qv - av) - (bv - av) * (qu - au)) * orient;
                const double d2 = ((cu - bu) * (qv - bv) - (cv - bv) * (qu - bu)) * orient;
                const double d3 = ((au - cu) * (qv - cv) - (av - cv) * (qu - cu)) * orient;
                if (d1 >= 0 && d2 >= 0 && d3 >= 0) inside = true;
            }
            if (inside) continue;
            tri.push_back(a); tri.push_back(b); tri.push_back(c);
            idx.erase(idx.begin() + i);
            clipped = true;
        }
        if (!clipped) break;
    }
    if (idx.size() == 3) { tri.push_back(idx[0]); tri.push_back(idx[1]); tri.push_back(idx[2]); }
    else if (idx.size() > 3) { for (std::size_t i = 1; i + 1 < idx.size(); ++i) { tri.push_back(idx[0]); tri.push_back(idx[i]); tri.push_back(idx[i + 1]); } ++fanned; }
}

static void ComputeCornerNormals(MeshData& md) {
    if (md.nDone) return;
    md.nDone = true;
    const std::size_t nl = md.loopVert.size(), nf = md.faceStart.size();
    md.cornerN.assign(nl, Vec3{0, 0, 1});
    std::vector<Vec3> fn(nf), vn(md.pos.size(), Vec3{0, 0, 0});
    for (std::size_t f = 0; f < nf; ++f) {
        const int n = md.faceCount[f], s = md.faceStart[f];
        double nx = 0, ny = 0, nz = 0;
        for (int i = 0; i < n; ++i) {
            const Vec3& a = md.pos[(std::size_t)md.loopVert[(std::size_t)(s + i)]]; const Vec3& b = md.pos[(std::size_t)md.loopVert[(std::size_t)(s + (i + 1) % n)]];
            nx += ((double)a.y - b.y) * ((double)a.z + b.z); ny += ((double)a.z - b.z) * ((double)a.x + b.x); nz += ((double)a.x - b.x) * ((double)a.y + b.y);
        }
        const double l = std::sqrt(nx * nx + ny * ny + nz * nz);
        fn[f] = l > 1e-30 ? Vec3{(float)(nx / l), (float)(ny / l), (float)(nz / l)} : Vec3{0, 0, 1};
        if (!md.faceSmooth[f]) continue;
        for (int i = 0; i < n; ++i) {
            const Vec3& c = md.pos[(std::size_t)md.loopVert[(std::size_t)(s + i)]];
            const Vec3 e1 = md.pos[(std::size_t)md.loopVert[(std::size_t)(s + (i + n - 1) % n)]] - c, e2 = md.pos[(std::size_t)md.loopVert[(std::size_t)(s + (i + 1) % n)]] - c;
            const float l1 = e1.Magnitude(), l2 = e2.Magnitude();
            float ang = 0.0f;
            if (l1 > 1e-20f && l2 > 1e-20f) ang = std::acos(std::max(-1.0f, std::min(1.0f, Vec3::Dot(e1, e2) / (l1 * l2))));
            vn[(std::size_t)md.loopVert[(std::size_t)(s + i)]] += fn[f] * ang;
        }
    }
    for (std::size_t f = 0; f < nf; ++f)
        for (int i = 0; i < md.faceCount[f]; ++i) {
            const std::size_t li = (std::size_t)(md.faceStart[f] + i);
            Vec3 n = fn[f];
            if (md.faceSmooth[f]) { const Vec3& v = vn[(std::size_t)md.loopVert[li]]; const float l = v.Magnitude(); if (l > 1e-12f) n = v * (1.0f / l); }
            md.cornerN[li] = n;
        }
}

static std::uint32_t FBits(float v) { std::uint32_t u; std::memcpy(&u, &v, 4); return u; }

// ---- Run -------------------------------------------------------------------------------------
bool Reader::Run(ImportedModel& model) {
    out = &model;
    if (!f.ParseBlocks()) return Fail(f.err);
    if (f.structs.empty()) return Fail("Blend: empty SDNA");

    // objects
    for (const BlockInfo& b : f.blocks) {
        if (std::memcmp(b.code, "OB", 3) != 0) continue;
        if (objs.size() >= kMaxObjects) { ++cntTruncList; break; }
        Ref ob = At(b.old, "Object");
        if (!ob) { ++cntBadObj; continue; }
        ObjRec o; o.ob = ob; o.addr = b.old; o.name = IdName(ob); o.type = ob.Int("type");
        objByAddr[b.old] = (int)objs.size();
        objs.push_back(std::move(o));
    }
    // the scene: FileGlobal.curscene, else the first SC block
    {
        for (const BlockInfo& b : f.blocks) {
            if (std::memcmp(b.code, "GLOB", 4) != 0) continue;
            Ref g = At(b.old, "FileGlobal");
            if (g) scene = g.Deref("curscene", "Scene");
            break;
        }
        if (!scene) for (const BlockInfo& b : f.blocks) if (std::memcmp(b.code, "SC", 3) == 0) { scene = At(b.old, "Scene"); if (scene) break; }
        if (scene) {
            Ref r = scene.Sub("r");
            const double fs = r ? r.Num("frs_sec", 0, 24.0) : 24.0, fb = r ? r.Num("frs_sec_base", 0, 1.0) : 1.0;
            if (fs > 0 && fb > 0 && fs / fb <= 1000.0 && fs / fb >= 0.001) fps = fs / fb;
            Ref unit = scene.Sub("unit");
            if (unit && unit.Has("scale_length") && std::fabs(unit.Num("scale_length", 0, 1.0) - 1.0) > 1e-6) ++cntUnitScale;
        }
    }
    {
        std::unordered_set<std::uint64_t> set;
        if (scene) {
            std::vector<Ref> bases;
            if (scene.List("base", "Base", bases, kMaxList)) for (const Ref& b : bases) set.insert(b.Ptr("object"));
            std::unordered_set<std::uint64_t> seen;
            CollectCollection(scene.Deref("master_collection", "Collection"), seen, set, 0);
        }
        set.erase(0);
        bool any = false;
        for (ObjRec& o : objs) if (set.count(o.addr)) { o.include = true; any = true; }
        if (!any) { for (ObjRec& o : objs) o.include = true; ++cntNoScene; }
        for (std::size_t i = 0; i < objs.size(); ++i) {   // parents come along even when they are not in the scene
            const std::uint64_t pa = objs[i].ob.Ptr("parent");
            auto it = objByAddr.find(pa);
            if (it != objByAddr.end()) objs[i].parent = it->second;
        }
        for (std::size_t i = 0; i < objs.size(); ++i) {
            if (!objs[i].include) continue;
            int p = objs[i].parent, d = 0;
            while (p >= 0 && d++ < kMaxDepth) { objs[(std::size_t)p].include = true; p = objs[(std::size_t)p].parent; }
        }
    }
    int included = 0;
    for (const ObjRec& o : objs) included += o.include ? 1 : 0;
    if (included == 0) return Fail("Blend: the file has no objects");
    BuildNodes();
    BuildParts();
    if (!flatten) BuildAnimation();
    for (const ImportedPart& p : model.parts) {
        for (const Vec3& v : p.mesh.vertices) if (!InRange(v)) return Fail("Blend: coordinates overflow");
        for (const Vec3& v : p.mesh.normals) if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return Fail("Blend: coordinates overflow");
    }
    for (const ImportedNode& nd : model.nodes)
        if (!InRange(nd.position) || !InRange(nd.scale) || !std::isfinite(nd.rotation.x + nd.rotation.y + nd.rotation.z + nd.rotation.w)) return Fail("Blend: transform overflow");
    Notes();
    return true;
}

void Reader::BuildNodes() {
    ImportedModel& m = *out;
    const Mat4 up = UpMatrix();
    ImportedNode root;
    root.name = UniqueNodeName("RootNode");
    up.Decompose(root.position, root.rotation, root.scale);
    m.nodes.assign(1, root);

    auto addNode = [&](const std::string& name, int parent, const Mat4& local) {
        ImportedNode nd;
        nd.name = UniqueNodeName(name);
        nd.parent = parent;
        if (!MatFinite(local)) { ++cntBadObj; }
        else {
            local.Decompose(nd.position, nd.rotation, nd.scale);
            const Mat4 re = Mat4::TRS(nd.position, nd.rotation, nd.scale);
            float worst = 0.0f, mag = 1.0f;
            for (int i = 0; i < 16; ++i) { worst = std::max(worst, std::fabs(re.m[i] - local.m[i])); mag = std::max(mag, std::fabs(local.m[i])); }
            if (worst > 1e-3f * mag) ++cntSheared;
        }
        m.nodes.push_back(nd);
        return (int)m.nodes.size() - 1;
    };

    // parent first order
    std::vector<std::vector<int>> kids(objs.size());
    std::vector<int> rootsList;
    for (std::size_t i = 0; i < objs.size(); ++i) {
        if (!objs[i].include) continue;
        int p = objs[i].parent, d = 0; bool cyc = false;
        while (p >= 0 && d++ <= kMaxDepth + 1) { if (p == (int)i) { cyc = true; break; } p = objs[(std::size_t)p].parent; }
        if (cyc || d > kMaxDepth) { ++cntParentCycle; objs[i].parent = -1; }
        if (objs[i].parent >= 0) kids[(std::size_t)objs[i].parent].push_back((int)i); else rootsList.push_back((int)i);
    }
    std::vector<int> stack;
    for (std::size_t i = rootsList.size(); i-- > 0;) stack.push_back(rootsList[i]);
    std::size_t visited = 0;
    while (!stack.empty()) {
        const int oi = stack.back(); stack.pop_back();
        ObjRec& o = objs[(std::size_t)oi];
        if (++visited > objs.size() + 1) break;
        const Ref& ob = o.ob;
        double loc[3], dloc[3] = {0, 0, 0}, sz[3] = {1, 1, 1}, dsc[3] = {1, 1, 1}, eul[3] = {0, 0, 0}, deul[3] = {0, 0, 0}, quat[4] = {1, 0, 0, 0}, ax[3] = {0, 1, 0};
        for (int k = 0; k < 3; ++k) { loc[k] = ob.Num("loc", (std::size_t)k); dloc[k] = ob.Num("dloc", (std::size_t)k); sz[k] = ob.Num("size", (std::size_t)k, 1.0); eul[k] = ob.Num("rot", (std::size_t)k); deul[k] = ob.Num("drot", (std::size_t)k); ax[k] = ob.Num("rotAxis", (std::size_t)k, k == 1 ? 1.0 : 0.0); }
        if (ob.Has("dscale")) { for (int k = 0; k < 3; ++k) dsc[k] = ob.Num("dscale", (std::size_t)k, 1.0); }
        else if (ob.Has("dsize")) { for (int k = 0; k < 3; ++k) sz[k] += ob.Num("dsize", (std::size_t)k); }
        for (int k = 0; k < 4; ++k) quat[k] = ob.Num("quat", (std::size_t)k, k == 0 ? 1.0 : 0.0);
        const int rotmode = ob.Has("rotmode") ? ob.Int("rotmode") : 1;
        for (int k = 0; k < 3; ++k) eul[k] += deul[k];
        if (rotmode <= 0) { bool dd = false; for (int k = 0; k < 3; ++k) if (std::fabs(deul[k]) > 1e-9) dd = true; if (dd) ++cntDeltaRot; }
        const Mat4 basis = TransMat(loc[0] + dloc[0], loc[1] + dloc[1], loc[2] + dloc[2]) * RotationOf(rotmode, eul, quat, ax, ob.Num("rotAngle")) * ScaleMat(sz[0] * dsc[0], sz[1] * dsc[1], sz[2] * dsc[2]);

        Mat4 pinv;
        { float t[16]; if (ob.ReadFloats("parentinv", t, 16)) for (int k = 0; k < 16; ++k) pinv.m[k] = t[k]; }
        if (!MatFinite(pinv)) pinv = Mat4();
        int parentNode = 0;
        Mat4 parentWorld, pre;
        bool haveParent = false;
        if (o.parent >= 0 && objs[(std::size_t)o.parent].placed) {
            const ObjRec& po = objs[(std::size_t)o.parent];
            const int ptype = ob.Int("partype") & 15;
            parentNode = po.node; parentWorld = po.world; haveParent = true;
            pre = pinv;
            if (ptype == 7 && po.armIdx >= 0) {   // parented to a bone: the parent matrix sits at the bone's tail
                const ArmRec& ar = arms[(std::size_t)po.armIdx];
                auto bi = ar.byName.find(ob.Str("parsubstr", 64));
                if (bi != ar.byName.end()) {
                    const BoneRec& br = ar.bones[(std::size_t)bi->second];
                    parentNode = br.node;
                    parentWorld = po.world * br.pose * TransMat(0, br.length, 0);
                    pre = TransMat(0, br.length, 0) * pinv;
                    ++cntBoneParent;
                } else ++cntNoBone;
            } else if (ptype == 1 || ptype == 2 || ptype == 5 || ptype == 6) ++cntVertexParent;
        }
        o.pre = pre;
        const Mat4 local = pre * basis;
        o.world = haveParent ? parentWorld * pinv * basis : basis;   // (the tail offset of a bone parent is part of parentWorld)
        o.node = addNode(o.name, parentNode, local);
        o.placed = true;

        // does the saved world matrix agree with what we computed?
        {
            float t[16];
            if (ob.ReadFloats("obmat", t, 16)) {
                bool nonzero = false; for (int k = 0; k < 16; ++k) if (t[k] != 0.0f) nonzero = true;
                if (nonzero && MatFinite(o.world)) {
                    float worst = 0, mag = 1;
                    for (int k = 0; k < 16; ++k) { if (!std::isfinite(t[k])) { worst = 1e30f; break; } worst = std::max(worst, std::fabs(t[k] - o.world.m[k])); mag = std::max(mag, std::fabs(t[k])); }
                    if (worst > 2e-3f * mag) ++cntMatMismatch;
                }
            }
        }
        if ((ob.Has("constraints") && ob.Sub("constraints").Ptr("first")) || ob.Ptr("track")) ++cntConstraints;
        if (ob.Has("modifiers")) {
            const Ref mb = ob.Sub("modifiers");
            std::uint64_t a = mb ? mb.Ptr("first") : 0;
            for (int guard = 0; a && guard < 1000; ++guard) {
                Span s = f.At(a);
                if (!s.p || s.st < 0) break;
                if (f.structs[(std::size_t)s.st].name != "ArmatureModifierData") ++cntModifiers;
                if (s.n < (std::size_t)f.ps) break;
                a = f.PtrAt(s.p);
            }
        }
        if (ob.Ptr("dup_group") || ob.Ptr("instance_collection")) ++cntDupli;
        switch (o.type) {
            case 0: case 1: case 25: break;
            case 10: ++cntLamps; break;
            case 11: ++cntCameras; break;
            default: ++cntOtherTypes; break;
        }
        if (ob.Has("adt")) {
            Ref ad = ob.Deref("adt", "AnimData");
            if (ad) { if (ad.Has("nla_tracks") && ad.Sub("nla_tracks").Ptr("first")) ++cntNla; if (ad.Has("drivers") && ad.Sub("drivers").Ptr("first")) ++cntDrivers; }
        }
        if (ob.Ptr("ipo")) ++cntIpo;

        // armature: the bones become nodes under the object
        if (o.type == OB_ARMATURE && ob.Ptr("data")) {
            const int ai = ReadArmature(ob.Ptr("data"));
            ArmRec& ar = arms[(std::size_t)ai];
            if (ar.ok) {
                o.armIdx = ai;
                if (ar.obj < 0) ar.obj = oi;
                Ref pose = ob.Deref("pose", "bPose");   // the stored pose of the saved frame
                std::vector<Ref> chans;
                if (pose) pose.List("chanbase", "bPoseChannel", chans, kMaxBones);
                for (const Ref& pc : chans) {
                    auto bi = ar.byName.find(pc.Str("name", 64));
                    if (bi == ar.byName.end()) continue;
                    BoneRec& br = ar.bones[(std::size_t)bi->second];
                    for (int k = 0; k < 3; ++k) { br.loc[k] = pc.Num("loc", (std::size_t)k); br.size[k] = pc.Num("size", (std::size_t)k, 1.0); br.eul[k] = pc.Num("eul", (std::size_t)k); br.axis[k] = pc.Num("rotAxis", (std::size_t)k, k == 1 ? 1.0 : 0.0); }
                    for (int k = 0; k < 4; ++k) br.quat[k] = pc.Num("quat", (std::size_t)k, k == 0 ? 1.0 : 0.0);
                    br.angle = pc.Num("rotAngle");
                    if (pc.Has("constraints") && pc.Sub("constraints").Ptr("first")) ++cntBoneConstraints;
                    br.rotmode = pc.Has("rotmode") ? pc.Int("rotmode") : 1;
                }
                for (std::size_t bi = 0; bi < ar.bones.size(); ++bi) {
                    BoneRec& br = ar.bones[bi];
                    const Mat4 basis2 = TransMat(br.loc[0], br.loc[1], br.loc[2]) * RotationOf(br.rotmode, br.eul, br.quat, br.axis, br.angle) * ScaleMat(br.size[0], br.size[1], br.size[2]);
                    const Mat4 loc2 = br.restLocal * basis2;
                    br.pose = br.parent >= 0 ? ar.bones[(std::size_t)br.parent].pose * loc2 : loc2;
                    br.node = addNode(br.name, br.parent >= 0 ? ar.bones[(std::size_t)br.parent].node : o.node, loc2);
                    br.name = m.nodes[(std::size_t)br.node].name;   // the node's (unique) name is the joint's name
                }
            }
        }
        for (std::size_t i = kids[(std::size_t)oi].size(); i-- > 0;) stack.push_back(kids[(std::size_t)oi][i]);
    }
}

void Reader::BuildParts() {
    for (std::size_t oi = 0; oi < objs.size(); ++oi) {
        ObjRec& o = objs[oi];
        if (!o.include || !o.placed || o.type != OB_MESH) continue;
        BuildPartsOf(o, (int)oi);
        if (out->parts.size() > kMaxParts) { ++cntTruncList; break; }
    }
    if (!flatten) return;
    // flattened: one node; bake the Z-up -> Y-up rotation and each object's world matrix into the vertices
    const Mat4 up = UpMatrix();
    std::vector<int> nodeObj(out->nodes.size(), -1);
    for (std::size_t i = 0; i < objs.size(); ++i) if (objs[i].node >= 0) nodeObj[(std::size_t)objs[i].node] = (int)i;
    for (ImportedPart& p : out->parts) {
        const int oi = p.node >= 0 && (std::size_t)p.node < nodeObj.size() ? nodeObj[(std::size_t)p.node] : -1;
        const Mat4 w = oi >= 0 ? up * objs[(std::size_t)oi].world : up;
        Mat4 wi = w.Inverse();
        if (!MatFinite(wi)) wi = Mat4();   // a zero scale: normals stay as they are
        Mat4 nm;   // inverse transpose
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) nm.at(c, r) = wi.at(r, c);
        for (Vec3& v : p.mesh.vertices) v = Xform(w, v);
        for (Vec3& v : p.mesh.normals) v = XformDir(nm, v).Normalized();
        const float det = w.at(0, 0) * (w.at(1, 1) * w.at(2, 2) - w.at(2, 1) * w.at(1, 2)) - w.at(1, 0) * (w.at(0, 1) * w.at(2, 2) - w.at(2, 1) * w.at(0, 2)) + w.at(2, 0) * (w.at(0, 1) * w.at(1, 2) - w.at(1, 1) * w.at(0, 2));
        if (det < 0) for (std::size_t i = 0; i + 2 < p.mesh.triangles.size(); i += 3) std::swap(p.mesh.triangles[i + 1], p.mesh.triangles[i + 2]);
        p.node = 0; p.bones.clear(); p.jointIdx.clear(); p.jointWt.clear();
    }
    const ImportedNode rootNode = out->nodes[0];
    out->nodes.assign(1, rootNode);
    out->clips.clear();
}

void Reader::BuildPartsOf(ObjRec& o, int oi) {
    (void)oi;
    ImportedModel& m = *out;
    const std::uint64_t meshAddr = o.ob.Ptr("data");
    if (!meshAddr) return;
    std::shared_ptr<MeshData> mdp = ReadMesh(meshAddr);
    MeshData& md = *mdp;
    if (!md.ok) return;
    if (md.faceStart.empty()) { ++cntNoFaces; return; }
    Ref me = At(meshAddr, "Mesh");
    ComputeCornerNormals(md);
    int totcol = me.Int("totcol");
    if (totcol <= 0) totcol = o.ob.Int("totcol");
    totcol = std::max(0, std::min(totcol, 4096));
    std::vector<int> slotMat((std::size_t)totcol, -1);
    {
        Span meshMats = f.At(me.Ptr("mat")), objMats = f.At(o.ob.Ptr("mat")), bits = f.At(o.ob.Ptr("matbits"));
        for (int i = 0; i < totcol; ++i) {
            const std::size_t po = (std::size_t)i * (std::size_t)f.ps;
            const bool objLinked = bits.p && (std::size_t)i < bits.n && bits.p[i];
            std::uint64_t a = 0;
            if (objLinked && objMats.p && po + (std::size_t)f.ps <= objMats.n) a = f.PtrAt(objMats.p + po);
            else if (meshMats.p && po + (std::size_t)f.ps <= meshMats.n) a = f.PtrAt(meshMats.p + po);
            slotMat[(std::size_t)i] = MaterialIndex(a);
        }
    }
    // skin: vertex group -> bone of the armature this object follows
    int armObj = -1, armIdx = -1;
    {
        if (o.parent >= 0 && objs[(std::size_t)o.parent].type == OB_ARMATURE && objs[(std::size_t)o.parent].armIdx >= 0) armObj = o.parent;
        if (armObj < 0 && o.ob.Has("modifiers")) {
            std::uint64_t a = o.ob.Sub("modifiers").Ptr("first");
            for (int guard = 0; a && guard < 1000; ++guard) {
                Span s = f.At(a);
                if (!s.p || s.st < 0 || s.n < (std::size_t)f.ps) break;
                if (f.structs[(std::size_t)s.st].name == "ArmatureModifierData" && s.n >= f.structs[(std::size_t)s.st].size) {
                    Ref amod; amod.f = &f; amod.p = s.p; amod.avail = s.n; amod.st = s.st; amod.addr = a;
                    auto it = objByAddr.find(amod.Ptr("object"));
                    if (it != objByAddr.end() && objs[(std::size_t)it->second].armIdx >= 0) { armObj = it->second; break; }
                }
                a = f.PtrAt(s.p);
            }
        }
        if (armObj >= 0) armIdx = objs[(std::size_t)armObj].armIdx;
    }
    std::vector<int> groupBone;
    bool skin = false;
    if (armIdx < 0 && !md.dvStart.empty()) ++cntGroupsNoArm;
    if (armIdx >= 0 && !md.dvStart.empty()) {
        std::vector<Ref> groups;
        if (o.ob.Has("defbase")) o.ob.List("defbase", "bDeformGroup", groups, 100000);
        if (groups.empty() && me.Has("vertex_group_names")) me.List("vertex_group_names", "bDeformGroup", groups, 100000);
        const ArmRec& ar = arms[(std::size_t)armIdx];
        groupBone.assign(groups.size(), -1);
        for (std::size_t g = 0; g < groups.size(); ++g) { auto it = ar.byName.find(groups[g].Str("name", 64)); if (it != ar.byName.end()) { groupBone[g] = it->second; skin = true; } }
    }

    std::map<int, std::vector<int>> byMat;   // material slot -> faces
    for (std::size_t fi = 0; fi < md.faceStart.size(); ++fi) {
        int mi = md.faceMat[fi];
        if (totcol > 0) mi = std::max(0, std::min(mi, totcol - 1)); else mi = -1;
        byMat[mi].push_back((int)fi);
    }
    std::vector<int> tri;
    for (auto& kv : byMat) {
        if (m.parts.size() >= kMaxParts) { ++cntTruncList; return; }
        ImportedPart part;
        part.mesh.name = o.name;
        part.material = kv.first >= 0 ? slotMat[(std::size_t)kv.first] : -1;
        part.node = o.node;
        const bool hasUv = !md.loopUv.empty();
        std::unordered_map<std::array<std::uint32_t, 6>, int, Hash3> weld;
        std::vector<int> srcVert;
        for (int fi : kv.second) {
            const int n = md.faceCount[(std::size_t)fi], s = md.faceStart[(std::size_t)fi];
            TriangulatePoly(md.pos, &md.loopVert[(std::size_t)s], n, tri, cntNgonFan, clipBudget);
            const std::size_t before = part.mesh.triangles.size();
            for (std::size_t ti = 0; ti + 2 < tri.size(); ti += 3) {
                int ids[3];
                for (int c = 0; c < 3; ++c) {
                    const std::size_t lp = (std::size_t)(s + tri[ti + (std::size_t)c]);
                    const int vi = md.loopVert[lp];
                    const Vec3& nn = md.cornerN[lp];
                    const Vec2 uv = hasUv ? Vec2{md.loopUv[lp].x, 1.0f - md.loopUv[lp].y} : Vec2{0, 0};
                    const std::array<std::uint32_t, 6> key{(std::uint32_t)vi, FBits(nn.x), FBits(nn.y), FBits(nn.z), hasUv ? FBits(uv.x) : 0u, hasUv ? FBits(uv.y) : 0u};
                    auto it = weld.find(key);
                    if (it == weld.end()) {
                        if (++outVerts > kMaxOutVerts) { ++cntTruncList; return; }
                        ids[c] = (int)part.mesh.vertices.size();
                        part.mesh.vertices.push_back(md.pos[(std::size_t)vi]);
                        part.mesh.normals.push_back(nn);
                        if (hasUv) part.mesh.uvs.push_back(uv);
                        srcVert.push_back(vi);
                        weld.emplace(key, ids[c]);
                    } else ids[c] = it->second;
                }
                if (ids[0] == ids[1] || ids[1] == ids[2] || ids[0] == ids[2]) continue;   // collapsed triangle
                part.mesh.triangles.push_back(ids[0]); part.mesh.triangles.push_back(ids[1]); part.mesh.triangles.push_back(ids[2]);
            }
            (void)before;
        }
        if (part.mesh.triangles.empty()) continue;
        if (skin) {
            const ArmRec& ar = arms[(std::size_t)armIdx];
            const Mat4 meshWorld = o.world, armWorld = objs[(std::size_t)armObj].world;
            for (const BoneRec& br : ar.bones) {
                ImportedBone ib; ib.name = br.name;
                ib.inverseBind = (armWorld * br.arm).Inverse() * meshWorld;
                if (!MatFinite(ib.inverseBind)) { ib.inverseBind = Mat4(); ++cntBadArm; }
                part.bones.push_back(std::move(ib));
            }
            bool any = false;
            part.jointIdx.assign(srcVert.size(), std::array<int, 4>{0, 0, 0, 0});
            part.jointWt.assign(srcVert.size(), std::array<float, 4>{0, 0, 0, 0});
            for (std::size_t v = 0; v < srcVert.size(); ++v) {
                const std::size_t sv = (std::size_t)srcVert[v];
                std::vector<std::pair<int, float>> w;
                for (std::uint32_t k = md.dvStart[sv]; k < md.dvStart[sv + 1]; ++k) {
                    const int g = md.dvGroup[k];
                    if (g < 0 || (std::size_t)g >= groupBone.size() || groupBone[(std::size_t)g] < 0) continue;
                    const int b = groupBone[(std::size_t)g];
                    bool merged = false;
                    for (auto& e : w) if (e.first == b) { e.second += md.dvWeight[k]; merged = true; }
                    if (!merged) w.emplace_back(b, md.dvWeight[k]);
                }
                std::sort(w.begin(), w.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
                if (w.size() > 4) w.resize(4);
                float sum = 0; for (auto& e : w) sum += e.second;
                if (!(sum > 0)) continue;
                any = true;
                for (std::size_t k = 0; k < w.size(); ++k) { part.jointIdx[v][k] = w[k].first; part.jointWt[v][k] = w[k].second / sum; }
            }
            if (!any) { part.bones.clear(); part.jointIdx.clear(); part.jointWt.clear(); ++cntSkinSkipped; }
        }
        m.parts.push_back(std::move(part));
    }
}

void Reader::BuildAnimation() {
    ImportedModel& m = *out;
    struct ClipAcc { std::string name; std::vector<std::pair<int, Target>> targets; };
    std::map<std::uint64_t, ClipAcc> actions;   // by action address (ordered: deterministic)
    for (std::size_t oi = 0; oi < objs.size(); ++oi) {
        ObjRec& o = objs[oi];
        if (!o.include || !o.placed || !o.ob.Has("adt")) continue;
        Ref ad = o.ob.Deref("adt", "AnimData");
        if (!ad) continue;
        const std::uint64_t aa = ad.Ptr("action");
        Ref act = At(aa, "bAction");
        if (!act) continue;
        if (!act.Has("curves")) { ++cntIpo; continue; }
        std::vector<Ref> fcs;
        act.List("curves", "FCurve", fcs, kMaxCurves);
        std::map<int, Target> tmap;   // bone index (-1 = the object itself) -> target
        for (const Ref& fc : fcs) {
            if (fc.Ptr("driver") || (fc.Has("modifiers") && fc.Sub("modifiers").Ptr("first"))) ++cntFcurveMods;
            std::string path;
            { Span s = f.At(fc.Ptr("rna_path")); if (s.p) for (std::size_t i = 0; i < s.n && i < 512 && s.p[i]; ++i) path += (char)s.p[i]; }
            const int idx = fc.Int("array_index");
            int bone = -1;
            std::string prop = path;
            if (path.compare(0, 12, "pose.bones[\"") == 0) {
                const std::size_t e = path.find("\"]", 12);
                if (e == std::string::npos || e + 3 > path.size() || path[e + 2] != '.' || o.armIdx < 0) { ++cntOtherPath; if (firstOtherPath.empty()) firstOtherPath = path; continue; }
                auto bi = arms[(std::size_t)o.armIdx].byName.find(path.substr(12, e - 12));
                if (bi == arms[(std::size_t)o.armIdx].byName.end()) { ++cntOtherPath; continue; }
                bone = bi->second;
                prop = path.substr(e + 3);
            }
            int kind = -1;
            if (prop == "location") kind = 0; else if (prop == "scale") kind = 1; else if (prop == "rotation_euler") kind = 2; else if (prop == "rotation_quaternion") kind = 3; else if (prop == "rotation_axis_angle") kind = 4;
            const int lim = kind == 3 || kind == 4 ? 4 : 3;
            if (kind < 0 || idx < 0 || idx >= lim) { ++cntOtherPath; if (firstOtherPath.empty()) firstOtherPath = path; continue; }
            Curve c;
            if (!LoadCurve(fc, c)) continue;
            Target& t = tmap[bone];
            t.obj = (int)oi; t.bone = bone; t.isBone = bone >= 0;
            t.node = bone >= 0 ? arms[(std::size_t)o.armIdx].bones[(std::size_t)bone].node : o.node;
            Curve* dst = kind == 0 ? &t.loc[idx] : kind == 1 ? &t.scl[idx] : kind == 2 ? &t.eul[idx] : kind == 3 ? &t.quat[idx] : &t.axis[idx];
            *dst = std::move(c);
            t.has[kind][idx] = true;
            ++cntCurves;
        }
        if (tmap.empty()) continue;
        ClipAcc& ca = actions[aa];
        if (ca.name.empty()) ca.name = IdName(act);
        for (auto& kv : tmap) ca.targets.emplace_back((int)oi, std::move(kv.second));
    }
    for (auto& akv : actions) {
        ClipAcc& ca = akv.second;
        double fmin = 1e300;
        for (auto& tp : ca.targets) {
            const Target& t = tp.second;
            auto scan = [&](const Curve* cs, int n, int kind) { for (int i = 0; i < n; ++i) if (t.has[kind][i] && !cs[i].keys.empty()) fmin = std::min(fmin, cs[i].keys.front().x); };
            scan(t.loc, 3, 0); scan(t.scl, 3, 1); scan(t.eul, 3, 2); scan(t.quat, 4, 3); scan(t.axis, 4, 4);
        }
        if (!(fmin < 1e299)) continue;
        ImportedClip clip;
        clip.name = ca.name.empty() ? std::string("action") : ca.name;
        for (auto& tp : ca.targets) {
            Target& t = tp.second;
            ObjRec& o = objs[(std::size_t)tp.first];
            std::vector<double> times;   // every key, plus every whole frame in between when a segment is a bezier curve
            double lo = 1e300, hi = -1e300; bool dense = false;
            auto gather = [&](const Curve* cs, int n, int kind) {
                for (int i = 0; i < n; ++i) {
                    if (!t.has[kind][i]) continue;
                    const Curve& c = cs[i];
                    if (c.extend != 0) ++cntExtrapolate;
                    for (std::size_t k = 0; k < c.keys.size(); ++k) {
                        times.push_back(c.keys[k].x);
                        if (k + 1 < c.keys.size() && c.keys[k].ipo == 0) times.push_back(c.keys[k + 1].x - 0.001);   // a held key: step just before the next
                        if (k + 1 < c.keys.size() && c.keys[k].ipo == 2) dense = true;
                    }
                    lo = std::min(lo, c.keys.front().x); hi = std::max(hi, c.keys.back().x);
                }
            };
            gather(t.loc, 3, 0); gather(t.scl, 3, 1); gather(t.eul, 3, 2); gather(t.quat, 4, 3); gather(t.axis, 4, 4);
            if (times.empty()) continue;
            if (dense) {
                if (hi - lo <= (double)kMaxDenseFrames) { for (double fr = std::ceil(lo); fr <= hi; fr += 1.0) times.push_back(fr); }
                else ++cntBigAnim;
            }
            std::sort(times.begin(), times.end());
            std::vector<double> ts;
            for (double v : times) if (ts.empty() || v - ts.back() > 1e-6) ts.push_back(v);
            if (ts.size() < 2) continue;   // one key is a placement, which the node's transform already is
            if (outKeys + ts.size() * 3 > kMaxOutKeys) { ++cntBigAnim; continue; }
            double loc[3] = {0, 0, 0}, sz[3] = {1, 1, 1}, eul[3] = {0, 0, 0}, quat[4] = {1, 0, 0, 0}, ax[4] = {0, 1, 0, 0};
            int rotmode = 1;
            Mat4 pre;
            double dl[3] = {0, 0, 0}, ds[3] = {1, 1, 1}, de[3] = {0, 0, 0};
            if (t.isBone) {
                const BoneRec& br = arms[(std::size_t)o.armIdx].bones[(std::size_t)t.bone];
                for (int k = 0; k < 3; ++k) { loc[k] = br.loc[k]; sz[k] = br.size[k]; eul[k] = br.eul[k]; ax[k] = br.axis[k]; }
                for (int k = 0; k < 4; ++k) quat[k] = br.quat[k];
                ax[3] = br.angle; rotmode = br.rotmode; pre = br.restLocal;
            } else {
                const Ref& ob = o.ob;
                for (int k = 0; k < 3; ++k) { loc[k] = ob.Num("loc", (std::size_t)k); dl[k] = ob.Num("dloc", (std::size_t)k); sz[k] = ob.Num("size", (std::size_t)k, 1.0); eul[k] = ob.Num("rot", (std::size_t)k); de[k] = ob.Num("drot", (std::size_t)k); ax[k] = ob.Num("rotAxis", (std::size_t)k, k == 1 ? 1.0 : 0.0); }
                if (ob.Has("dscale")) for (int k = 0; k < 3; ++k) ds[k] = ob.Num("dscale", (std::size_t)k, 1.0);
                for (int k = 0; k < 4; ++k) quat[k] = ob.Num("quat", (std::size_t)k, k == 0 ? 1.0 : 0.0);
                ax[3] = ob.Num("rotAngle");
                rotmode = ob.Has("rotmode") ? ob.Int("rotmode") : 1;
                pre = o.pre;
            }
            ImportedChannel ch;
            ch.node = m.nodes[(std::size_t)t.node].name;
            Quat prev; bool havePrev = false;
            for (double tt : ts) {
                double L[3], S[3], E[3], Q[4], A[4];
                for (int k = 0; k < 3; ++k) { L[k] = t.has[0][k] ? EvalCurve(t.loc[k], tt) : loc[k]; S[k] = t.has[1][k] ? EvalCurve(t.scl[k], tt) : sz[k]; E[k] = t.has[2][k] ? EvalCurve(t.eul[k], tt) : eul[k]; }
                for (int k = 0; k < 4; ++k) { Q[k] = t.has[3][k] ? EvalCurve(t.quat[k], tt) : quat[k]; A[k] = t.has[4][k] ? EvalCurve(t.axis[k], tt) : ax[k]; }
                for (int k = 0; k < 3; ++k) { L[k] += dl[k]; S[k] *= ds[k]; E[k] += de[k]; }
                const Mat4 basis = TransMat(L[0], L[1], L[2]) * RotationOf(rotmode, E, Q, A, A[3]) * ScaleMat(S[0], S[1], S[2]);
                const Mat4 local = pre * basis;
                if (!MatFinite(local)) continue;
                Vec3 p, s; Quat r;
                local.Decompose(p, r, s);
                if (havePrev && Quat::Dot(prev, r) < 0) r = Quat{-r.x, -r.y, -r.z, -r.w};
                prev = r; havePrev = true;
                const float time = (float)((tt - fmin) / fps);
                if (!std::isfinite(time) || !InRange(p) || !InRange(s)) continue;
                ch.position.push_back({time, p}); ch.rotation.push_back({time, r}); ch.scale.push_back({time, s});
            }
            outKeys += ch.position.size() * 3;
            if (!ch.position.empty()) clip.channels.push_back(std::move(ch));
        }
        if (!clip.channels.empty()) m.clips.push_back(std::move(clip));
    }
    // animation of data blocks other than objects (materials, meshes, cameras, lights, shape keys, worlds)
    for (const BlockInfo& b : f.blocks) {
        if (std::memcmp(b.code, "ME", 3) && std::memcmp(b.code, "MA", 3) && std::memcmp(b.code, "CA", 3) && std::memcmp(b.code, "LA", 3) && std::memcmp(b.code, "KE", 3) && std::memcmp(b.code, "WO", 3)) continue;
        if (b.sdna < 0 || (std::size_t)b.sdna >= f.structs.size()) continue;
        Ref idb = Root().DerefAddr(b.old, f.structs[(std::size_t)b.sdna].name.c_str());
        if (idb && idb.Has("adt")) { Ref ad = idb.Deref("adt", "AnimData"); if (ad && ad.Ptr("action")) ++cntOtherAnim; }
    }
}

void Reader::Notes() {
    auto note = [&](const std::string& s) { out->notes.push_back("Blend: " + s); };
    auto n = [](int v) { return std::to_string(v); };
    if (f.version < 250) note("file from Blender " + std::to_string(f.version / 100) + "." + std::to_string(f.version % 100) + " (before 2.5): objects, meshes, materials and armature rest poses are read; Ipo animation is not");
    if (cntNoScene) note("no scene objects found (no base list or collection): every object in the file was imported");
    if (cntLamps) note(n(cntLamps) + " light object(s) not imported (kept as empty nodes)");
    if (cntCameras) note(n(cntCameras) + " camera object(s) not imported (kept as empty nodes)");
    if (cntOtherTypes) note(n(cntOtherTypes) + " object(s) of an unsupported type (curve, surface, text, metaball, lattice, grease pencil, volume, ...) kept as empty nodes");
    if (cntModifiers) note(n(cntModifiers) + " modifier(s) not applied (subdivision, mirror, array, boolean, ...): meshes are shown as stored");
    if (cntConstraints) note(n(cntConstraints) + " object(s) with constraints or a track-to target: not evaluated");
    if (cntBoneConstraints) note(n(cntBoneConstraints) + " pose bone(s) with constraints (IK, copy rotation, ...): not evaluated, the stored pose values are used");
    if (cntDupli) note(n(cntDupli) + " object(s) instance a group / collection: the instance is not expanded");
    if (cntMatMismatch) note(n(cntMatMismatch) + " object(s) whose computed world matrix differs from the matrix saved in the file (constraints, drivers, animation or an unsupported parent type)");
    if (cntVertexParent) note(n(cntVertexParent) + " object(s) parented to a vertex, curve or lattice: attached to the parent object's origin instead");
    if (cntBoneParent) note(n(cntBoneParent) + " object(s) parented to a bone (attached at the bone's tail like Blender does)");
    if (cntNoBone) note(n(cntNoBone) + " object(s) parented to a bone that does not exist in the armature");
    if (cntParentCycle) note(n(cntParentCycle) + " object(s) with a parent cycle were moved to the root");
    if (cntSheared) note(n(cntSheared) + " node transform(s) contain shear that a position / rotation / scale triple cannot hold: the shear is dropped");
    if (cntDeltaRot) note(n(cntDeltaRot) + " object(s) with a delta rotation in quaternion / axis-angle mode: the delta is ignored");
    if (cntUnitScale) note("scene unit scale is not 1: sizes are in Blender units");
    if (cntNoFaces) note(n(cntNoFaces) + " mesh object(s) without faces (vertices or edges only) not imported");
    if (cntGroupsNoArm) note(n(cntGroupsNoArm) + " mesh object(s) with vertex groups but no armature to follow: weights not imported");
    if (cntBadMesh) note(n(cntBadMesh) + " mesh(es) with missing or truncated vertex / face arrays: read as far as they go");
    int badFaces = 0, vcol = 0, autoS = 0, custN = 0, shape = 0;
    for (auto& kv : meshCache) { badFaces += kv.second->badFaces; vcol += kv.second->colourLayers > 0; autoS += kv.second->autoSmooth; custN += kv.second->customNormals; shape += kv.second->shapeKeys; }
    if (badFaces) note(n(badFaces) + " face(s) or vertices skipped: fewer than three corners, an index out of range or a non-finite coordinate");
    if (vcol) note(n(vcol) + " mesh(es) have vertex colours: the engine mesh has no vertex colour channel, they are not imported");
    if (autoS) note(n(autoS) + " mesh(es) use auto smooth with an angle: faces are smooth or flat as flagged, sharp edges and the angle are not applied");
    if (custN) note(n(custN) + " mesh(es) have custom split normals: not read");
    if (shape) note(n(shape) + " mesh(es) have shape keys (morph targets): not imported");
    if (cntNgonFan) note(n(cntNgonFan) + " polygon(s) could not be ear clipped and were fanned");
    if (cntNodeMat) note(n(cntNodeMat) + " node-based material(s) without a Principled BSDF: colours come from the material's viewport settings");
    if (cntPacked) note(n(cntPacked) + " image(s) are packed inside the .blend: only the original file name is given, the pixels are not extracted");
    if (cntSkinSkipped) note(n(cntSkinSkipped) + " mesh(es) with vertex groups but no matching armature bones: imported without a skin");
    if (cntIpo) note(n(cntIpo) + " object(s) with pre-2.5 Ipo animation: not read");
    if (cntNla) note(n(cntNla) + " object(s) with NLA tracks: only the active action is read");
    if (cntDrivers) note(n(cntDrivers) + " object(s) with drivers: not evaluated");
    if (cntFcurveMods) note(n(cntFcurveMods) + " F-Curve(s) with modifiers or drivers (cycles, noise, ...): the keys are read, the modifiers are not applied");
    if (cntEasing) note(n(cntEasing) + " keyframe(s) use an easing interpolation: treated as linear");
    if (cntExtrapolate) note(n(cntExtrapolate) + " F-Curve(s) extrapolate: the value is held after the last key");
    if (cntOtherPath) note(n(cntOtherPath) + " F-Curve(s) on properties other than location, rotation and scale of an object or pose bone were skipped (e.g. '" + firstOtherPath + "')");
    if (cntOtherAnim) note(n(cntOtherAnim) + " non-object data block(s) (material, mesh, shape key, camera, light, world) have animation: not read");
    if (cntBigAnim) note(n(cntBigAnim) + " animated node(s) skipped or sampled at keys only (too many frames or keys)");
    if (cntTruncList) note("a list in the file was longer than the reader's limit and was cut");
    if (cntBadArm) note(n(cntBadArm) + " armature(s) or bone(s) with a singular or non-finite rest matrix: the armature was dropped or the inverse bind left as identity");
    if (cntBadObj) note(n(cntBadObj) + " object(s) unreadable or with a non-finite transform");
    if (f.boundsMiss) note(n((int)std::min<std::size_t>(f.boundsMiss, 1000000)) + " field read(s) fell outside their block (damaged or unusual file); the defaults were used");
    for (const StructInfo& s : f.structs) if (!s.layoutOk && (s.name == "Object" || s.name == "Mesh" || s.name == "Bone" || s.name == "bPoseChannel" || s.name == "FCurve")) { note("struct " + s.name + " has a size in the file that does not match its fields: values may be wrong"); break; }
}

// ---- public entry ----------------------------------------------------------------------------

static bool Gunzip(const std::string& in, std::vector<u8>& out, std::string& err) {
    const u8* d = (const u8*)in.data();
    const std::size_t n = in.size();
    if (n < 18 || d[2] != 8) { err = "Blend: bad gzip header"; return false; }
    const u8 flg = d[3];
    std::size_t p = 10;
    if (flg & 4) { if (p + 2 > n) { err = "Blend: truncated gzip header"; return false; } p += 2 + (std::size_t)(d[p] | (d[p + 1] << 8)); }
    if (flg & 8) { while (p < n && d[p]) ++p; ++p; }
    if (flg & 16) { while (p < n && d[p]) ++p; ++p; }
    if (flg & 2) p += 2;
    if (p + 8 > n) { err = "Blend: truncated gzip stream"; return false; }
    if (!okay::Inflate(d + p, n - p - 8, false, out, kMaxInflated)) { err = "Blend: the gzip data is damaged or larger than " + std::to_string(kMaxInflated >> 20) + " MiB"; return false; }
    const std::size_t t = n - 4;
    const std::uint32_t isize = (std::uint32_t)d[t] | ((std::uint32_t)d[t + 1] << 8) | ((std::uint32_t)d[t + 2] << 16) | ((std::uint32_t)d[t + 3] << 24);
    if (isize != (std::uint32_t)(out.size() & 0xffffffffu)) { err = "Blend: the gzip data does not match its stored size"; return false; }
    return true;
}

} // namespace blnd
} // namespace

bool ReadBlendBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err, bool flatten) {
    out = ImportedModel{};
    auto fail = [&](const std::string& m) { out = ImportedModel{}; if (err) *err = m; return false; };
    if (bytes.size() > blnd::kMaxFileBytes) return fail("Blend: file too large");
    blnd::Reader r;
    r.baseDir = baseDir;
    r.flatten = flatten;
    if (bytes.size() >= 2 && (unsigned char)bytes[0] == 0x1f && (unsigned char)bytes[1] == 0x8b) {
        std::string e;
        if (!blnd::Gunzip(bytes, r.f.buf, e)) return fail(e);
    } else if (bytes.size() >= 4 && (unsigned char)bytes[0] == 0x28 && (unsigned char)bytes[1] == 0xb5 && (unsigned char)bytes[2] == 0x2f && (unsigned char)bytes[3] == 0xfd) {
        return fail("Blend: this file is zstd compressed (Blender 3.0+ with 'Compress File' on), which is not supported yet; save it uncompressed or as gzip");
    } else {
        r.f.buf.assign(bytes.begin(), bytes.end());
    }
    if (!r.Run(out)) {
        const std::string m = !r.err.empty() ? r.err : (!r.f.err.empty() ? r.f.err : std::string("Blend: could not read file"));
        return fail(m);
    }
    out.backend = "own";
    return true;
}

bool ReadBlendFile(const std::string& path, ImportedModel& out, std::string* err, bool flatten) {
    out = ImportedModel{};
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) { if (err) *err = "cannot open file"; return false; }
    std::string bytes;
    char buf[65536];
    for (;;) {
        const std::size_t k = std::fread(buf, 1, sizeof(buf), fp);
        if (k == 0) break;
        if (bytes.size() + k > blnd::kMaxFileBytes) { std::fclose(fp); if (err) *err = "Blend: file too large"; return false; }
        bytes.append(buf, k);
    }
    std::fclose(fp);
    const std::size_t sl = path.find_last_of("/\\");
    return ReadBlendBuffer(bytes, sl == std::string::npos ? std::string() : path.substr(0, sl + 1), out, err, flatten);
}

} // namespace okay
