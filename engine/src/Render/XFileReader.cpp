// Our own DirectX .x reader: text ("txt "), binary ("bin ") and MSZIP-compressed ("tzip", "bzip") files, 32 and 64 bit
// floats. Frames (the node tree), meshes (positions, polygons, normals, first texture coordinate set, per-face materials),
// materials, skin weights and animation sets. Written from the public description of the format (the DirectX SDK
// documentation of the .x file format and its standard templates). Bounds-checked, capped; the frame tree, unknown
// objects and animation sets are walked with loops and explicit stacks (no recursion). See okay/Render/ModelData.hpp and
// docs/own_x_reader.md.
//
// DirectX is left-handed and writes its matrices for row vectors. The model is turned into the engine's right-handed space
// by mirroring Z (positions, normals, matrices; the winding of every triangle is reversed). A rotation key (w, x, y, z)
// becomes (x, y, -z, w).
#include "BinReader.hpp"
#include "ReaderCommon.hpp"
#include "TextReaderUtil.hpp"
#include "okay/Core/ImageDecode.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

namespace {
namespace xf {

constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;
constexpr std::size_t kMaxBody = 256u * 1024u * 1024u;
constexpr std::size_t kMaxVerts = 20000000, kMaxFaces = 20000000, kMaxNodes = 100000, kMaxKeys = 20000000, kMaxMaterials = 100000, kMaxBones = 100000;
constexpr std::size_t kMaxDepth = 100000;
constexpr double kMaxCoord = 1e15;

// ---- the token stream: text and binary look the same to the parser --------------------------------------------------
struct Lex {
    const unsigned char* d = nullptr;
    std::size_t n = 0, pos = 0;
    bool bin = false;
    int fsz = 4;
    bool bad = false;
    std::string err;
    std::uint32_t pending = 0;   // binary: numbers left in the current INT_LIST / FLOAT_LIST
    int pendingKind = 0;         // 1 ints, 2 floats

    bool Fail(const std::string& m) { if (!bad) { bad = true; err = m; } return false; }
    std::size_t Left() const { return n - pos; }

    // ---- text helpers ----
    static bool Ws(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
    static bool Sep(char c) { return c == ';' || c == ','; }
    // white space, separators (they carry no meaning: the structure comes from the templates) and comments
    void SkipBlank() {
        while (pos < n) {
            const char c = (char)d[pos];
            if (Ws(c) || Sep(c)) { ++pos; continue; }
            if (c == '#' || (c == '/' && pos + 1 < n && d[pos + 1] == '/')) { while (pos < n && d[pos] != '\n' && d[pos] != '\r') ++pos; continue; }
            break;
        }
    }
    bool TextToken(std::string& w) {   // a word, a quoted string (quotes dropped) or a brace
        SkipBlank();
        if (pos >= n) return false;
        const char c = (char)d[pos];
        if (c == '{' || c == '}') { w.assign(1, c); ++pos; return true; }
        if (c == '"') {
            const std::size_t b = ++pos;
            while (pos < n && d[pos] != '"') ++pos;
            if (pos >= n) return Fail("a quoted string is not closed");
            if (pos - b > 65536) return Fail("a string is too long");
            w.assign((const char*)d + b, pos - b);
            ++pos;
            return true;
        }
        const std::size_t b = pos;
        while (pos < n && !Ws((char)d[pos]) && !Sep((char)d[pos]) && d[pos] != '{' && d[pos] != '}') ++pos;
        if (pos - b > 65536) return Fail("a name is too long");
        w.assign((const char*)d + b, pos - b);
        return true;
    }

    // ---- binary helpers ----
    std::uint16_t W16() { if (Left() < 2) { Fail("the file ends inside a token"); return 0; } const std::uint16_t v = (std::uint16_t)(d[pos] | (d[pos + 1] << 8)); pos += 2; return v; }
    std::uint32_t D32() { if (Left() < 4) { Fail("the file ends inside a token"); return 0; } const std::uint32_t v = (std::uint32_t)d[pos] | ((std::uint32_t)d[pos + 1] << 8) | ((std::uint32_t)d[pos + 2] << 16) | ((std::uint32_t)d[pos + 3] << 24); pos += 4; return v; }
    bool BinToken(std::string& w) {
        for (;;) {
            if (pos + 2 > n) return false;
            const std::uint16_t t = W16();
            switch (t) {
            case 1: case 2: {
                const std::uint32_t len = D32();
                if (bad) return false;
                if (len > 65536 || len > Left()) return Fail("a name or string runs past the end of the file or is too long");
                w.assign((const char*)d + pos, len);
                pos += len;
                return true;
            }
            case 5: if (Left() < 16) return Fail("the file ends inside a GUID"); pos += 16; w = "<guid>"; return true;
            case 10: w = "{"; return true;
            case 11: w = "}"; return true;
            case 31: w = "template"; return true;
            case 19: case 20: continue;   // a separator between tokens
            default: return Fail("unexpected binary token " + std::to_string(t) + " where a name was expected");
            }
        }
    }

    bool Word(std::string& w) { pending = 0; pendingKind = 0; return bin ? BinToken(w) : TextToken(w); }

    // Binary: start a new number list when the previous one is used up.
    bool BinNumbers(int kind) {
        if (pending > 0) return pendingKind == kind || Fail("a list of numbers of one kind is read as the other kind");
        for (;;) {
            if (pos + 2 > n) return Fail("the file ends where a number was expected");
            const std::uint16_t t = W16();
            if (t == 19 || t == 20) continue;
            if (kind == 1 && t == 6) { pending = D32(); if (bad) return false; if ((std::uint64_t)pending * 4 > Left()) return Fail("an integer list runs past the end of the file"); }
            else if (kind == 1 && t == 3) pending = 1;
            else if (kind == 2 && t == 7) { pending = D32(); if (bad) return false; if ((std::uint64_t)pending * (std::uint64_t)fsz > Left()) return Fail("a float list runs past the end of the file"); }
            else if (kind == 2 && (t == 42 || t == 43)) pending = 1;
            else return Fail("unexpected binary token " + std::to_string(t) + " where a number was expected");
            pendingKind = kind;
            if (pending == 0) continue;   // an empty list: the next token is the one wanted
            return true;
        }
    }
    bool Int(long long& v) {
        v = 0;
        if (bad) return false;
        if (bin) {
            if (!BinNumbers(1)) return false;
            --pending;
            if (Left() < 4) return Fail("the file ends inside an integer");
            v = (std::int32_t)D32();
            return true;
        }
        SkipBlank();
        const std::size_t b = pos;
        if (pos < n && (d[pos] == '-' || d[pos] == '+')) ++pos;
        std::size_t digits = 0;
        long long r = 0;
        while (pos < n && d[pos] >= '0' && d[pos] <= '9') { if (digits < 12) r = r * 10 + (d[pos] - '0'); ++digits; ++pos; }
        if (digits == 0 || digits > 10 || (pos < n && !Ws((char)d[pos]) && !Sep((char)d[pos]) && d[pos] != '{' && d[pos] != '}')) { pos = b; return Fail("an integer was expected"); }
        if (d[b] == '-') r = -r;
        if (r > 2147483647LL || r < -2147483648LL) return Fail("an integer is out of range");
        v = r;
        return true;
    }
    bool Float(double& v) {
        v = 0;
        if (bad) return false;
        if (bin) {
            if (!BinNumbers(2)) return false;
            --pending;
            if (Left() < (std::size_t)fsz) return Fail("the file ends inside a number");
            if (fsz == 4) { std::uint32_t u = D32(); float f; std::memcpy(&f, &u, 4); v = f; }
            else { std::uint64_t lo = D32(), hi = D32(); std::uint64_t u = lo | (hi << 32); std::memcpy(&v, &u, 8); }
            if (!std::isfinite(v) || std::fabs(v) > kMaxCoord) return Fail("a number is not finite or is out of range");
            return true;
        }
        SkipBlank();
        const std::size_t b = pos;
        while (pos < n && !Ws((char)d[pos]) && !Sep((char)d[pos]) && d[pos] != '{' && d[pos] != '}') ++pos;
        if (pos == b || !detail::rc::ParseNumber((const char*)d + b, (const char*)d + pos, v)) { pos = b; return Fail("a number was expected"); }
        return true;
    }

    // After the keyword (and the optional name): the opening brace. `name` gets the name, if any.
    bool Head(std::string* name) {
        std::string w;
        if (!Word(w)) return Fail("the file ends where an object's name or opening brace was expected");
        if (w == "{") { if (name) name->clear(); return true; }
        if (w == "}") return Fail("an object has no opening brace");
        if (name) *name = w;
        if (!Word(w) || w != "{") return Fail("an object's opening brace is missing");
        return true;
    }

    // Skips to the closing brace of the object whose opening brace was just read (nested objects included).
    bool SkipToClose() {
        pending = 0; pendingKind = 0;
        std::size_t depth = 1;
        if (!bin) {
            while (pos < n) {
                SkipBlank();
                if (pos >= n) break;
                const char c = (char)d[pos++];
                if (c == '"') { while (pos < n && d[pos] != '"') ++pos; if (pos >= n) return Fail("a quoted string is not closed"); ++pos; }
                else if (c == '{') { if (++depth > kMaxDepth) return Fail("objects nested too deeply"); }
                else if (c == '}') { if (--depth == 0) return true; }
            }
            return Fail("the file ends inside an object");
        }
        while (pos + 2 <= n) {
            const std::uint16_t t = W16();
            switch (t) {
            case 1: case 2: { const std::uint32_t len = D32(); if (bad) return false; if (len > Left()) return Fail("a name runs past the end of the file"); pos += len; break; }
            case 3: Skip(4); break;
            case 5: Skip(16); break;
            case 6: { const std::uint32_t c = D32(); if (bad) return false; if ((std::uint64_t)c * 4 > Left()) return Fail("an integer list runs past the end of the file"); pos += (std::size_t)c * 4; break; }
            case 7: { const std::uint32_t c = D32(); if (bad) return false; if ((std::uint64_t)c * (std::uint64_t)fsz > Left()) return Fail("a float list runs past the end of the file"); pos += (std::size_t)c * (std::size_t)fsz; break; }
            case 10: if (++depth > kMaxDepth) return Fail("objects nested too deeply"); break;
            case 11: if (--depth == 0) return true; break;
            case 12: case 13: case 14: case 15: case 16: case 17: case 18: case 19: case 20: case 31:
            case 40: case 41: case 42: case 43: case 44: case 45: case 46: case 47: case 48: case 49: case 50: case 51: case 52: break;
            default: return Fail("unknown binary token " + std::to_string(t));
            }
            if (bad) return false;
        }
        return Fail("the file ends inside an object");
    }
    void Skip(std::size_t k) { if (Left() < k) { Fail("the file ends inside a token"); return; } pos += k; }
};

// ---- parsed data --------------------------------------------------------------------------------------------------
struct Mat16 { double f[16]; };   // as written: 16 numbers in rows, for row vectors
struct XBone { std::string name; std::vector<std::uint32_t> v; std::vector<float> w; Mat16 off{}; };
struct XMesh {
    int frame = -1;
    std::vector<Vec3> pos;
    std::vector<std::uint32_t> fstart, fidx;          // polygons: fstart has faces + 1 entries
    std::vector<Vec3> nrm;
    std::vector<std::uint32_t> nstart, nidx;
    bool haveNrm = false;
    std::vector<Vec2> uv;
    std::vector<int> faceMat;                          // per polygon: index into matSlots, or -1
    struct Slot { std::string ref; int def = -1; };    // a material entry: an inline definition or a reference by name
    std::vector<Slot> slots;
    std::vector<XBone> bones;
};
struct MatDef { std::string name; float diff[4] = {1, 1, 1, 1}, power = 0, spec[3] = {0, 0, 0}, emis[3] = {0, 0, 0}; std::string tex, nrmTex; bool haveColor = false; };
struct XKey { double time; double v[16]; };
struct XAnimBone { std::string frame; std::vector<XKey> rot, scl, pos, mat; };
struct XAnimSet { std::string name; std::vector<XAnimBone> bones; };
struct XFrame { std::string name; int parent = -1; Mat16 m{}; bool hasM = false; };

bool IdentityMat(Mat16& m) { for (int i = 0; i < 16; ++i) m.f[i] = (i % 5 == 0) ? 1.0 : 0.0; return true; }

} // namespace xf
} // namespace

namespace {

using namespace xf;

struct Parser {
    Lex lx;
    std::vector<XFrame> frames;
    std::vector<XMesh> meshes;
    std::vector<MatDef> mats;
    std::vector<XAnimSet> anims;
    double ticks = 0;
    int topFrames = 0;
    int unknownObjects = 0, badFaces = 0, badNormals = 0, extraUv = 0, badUv = 0, colorSets = 0, meshRefs = 0, badBoneData = 0, badKeys = 0, extraTexCoords = 0;
    std::unordered_map<std::string, int> unknownNames;
    std::size_t totalKeys = 0;

    bool Fail(const std::string& m) { return lx.Fail(m); }
    bool Err() const { return lx.bad; }

    bool Vec3At(Vec3& v) {
        double a, b, c;
        if (!lx.Float(a) || !lx.Float(b) || !lx.Float(c)) return false;
        v = Vec3{(float)a, (float)b, (float)c};
        return true;
    }
    bool Mat(Mat16& m) { for (double& x : m.f) if (!lx.Float(x)) return false; return true; }

    // A count read from the file: within the cap, and no larger than the bytes that remain can hold (every number takes at
    // least 2 bytes in text and 4 in binary), so nothing is reserved on a hostile claim.
    bool Count(long long& c, std::size_t numbersPerItem, const char* what, std::size_t cap) {
        if (!lx.Int(c)) return false;
        if (c < 0 || (std::size_t)c > cap) return Fail(std::string("bad ") + what + " count " + std::to_string(c));
        const std::size_t minBytes = numbersPerItem * (lx.bin ? 4 : 2);
        if ((std::uint64_t)c * minBytes > (std::uint64_t)lx.Left() + 8) return Fail(std::string("the ") + what + " count " + std::to_string(c) + " is more than the rest of the file can hold");
        return true;
    }

    bool ParseMaterial(MatDef& m) {
        std::string name;
        if (!lx.Head(&name)) return false;
        m.name = name;
        double v[11];
        for (double& x : v) if (!lx.Float(x)) return false;
        for (int i = 0; i < 4; ++i) m.diff[i] = (float)v[i];
        m.power = (float)v[4];
        for (int i = 0; i < 3; ++i) { m.spec[i] = (float)v[5 + i]; m.emis[i] = (float)v[8 + i]; }
        std::string w;
        for (;;) {
            if (!lx.Word(w)) return Fail("the file ends inside a material");
            if (w == "}") return true;
            if (w == "TextureFilename" || w == "TextureFileName" || w == "NormalmapFilename" || w == "NormalmapFileName") {
                const bool nm = w[0] == 'N';
                if (!lx.Head(nullptr)) return false;
                std::string f, c;
                if (!lx.Word(f)) return Fail("the file ends inside a texture name");
                if (f != "}") {
                    if (!lx.Word(c) || c != "}") return Fail("a texture name object is not closed");
                    {   // text files write each backslash twice: a run of backslashes is one separator
                        std::string t;
                        for (char ch : f) { if (ch == '\\') { if (t.empty() || t.back() != '/') t += '/'; } else t += ch; }
                        f.swap(t);
                    }
                    (nm ? m.nrmTex : m.tex) = f;
                }
            } else if (w == "{") { if (!lx.SkipToClose()) return false; }   // a reference inside a material
            else {
                std::string nm;
                if (!lx.Head(&nm) || !lx.SkipToClose()) return false;
                ++unknownObjects;
            }
        }
    }

    bool ParseMesh(XMesh& m) {
        if (!lx.Head(nullptr)) return false;
        long long nv;
        if (!Count(nv, 3, "vertex", kMaxVerts)) return false;
        m.pos.reserve((std::size_t)nv);
        for (long long i = 0; i < nv; ++i) { Vec3 v; if (!Vec3At(v)) return false; m.pos.push_back(v); }
        long long nf;
        if (!Count(nf, 1, "face", kMaxFaces)) return false;
        m.fstart.reserve((std::size_t)nf + 1);
        m.fstart.push_back(0);
        for (long long f = 0; f < nf; ++f) {
            long long k;
            if (!lx.Int(k)) return false;
            if (k < 0 || (std::size_t)k > lx.Left() + 1 || k > 1000000) return Fail("a face has a bad index count " + std::to_string(k));
            const std::size_t before = m.fidx.size();
            bool good = true;
            for (long long i = 0; i < k; ++i) {
                long long idx;
                if (!lx.Int(idx)) return false;
                if (idx < 0 || idx >= nv) good = false; else m.fidx.push_back((std::uint32_t)idx);
            }
            if (!good) { m.fidx.resize(before); ++badFaces; }   // a face naming a missing vertex is an empty face
            m.fstart.push_back((std::uint32_t)m.fidx.size());
            if (m.fidx.size() > 4 * kMaxFaces) return Fail("too many face indices");
        }
        std::string w;
        for (;;) {
            if (!lx.Word(w)) return Fail("the file ends inside a mesh");
            if (w == "}") return true;
            if (w == "MeshNormals") {
                if (!lx.Head(nullptr)) return false;
                long long nn;
                if (!Count(nn, 3, "normal", kMaxVerts)) return false;
                std::vector<Vec3> nrm;
                nrm.reserve((std::size_t)nn);
                for (long long i = 0; i < nn; ++i) { Vec3 v; if (!Vec3At(v)) return false; nrm.push_back(v); }
                long long nfn;
                if (!Count(nfn, 1, "normal face", kMaxFaces)) return false;
                std::vector<std::uint32_t> ns{0}, ni;
                for (long long f = 0; f < nfn; ++f) {
                    long long k;
                    if (!lx.Int(k)) return false;
                    if (k < 0 || (std::size_t)k > lx.Left() + 1 || k > 1000000) return Fail("a normal face has a bad index count");
                    for (long long i = 0; i < k; ++i) { long long idx; if (!lx.Int(idx)) return false; ni.push_back(idx >= 0 && idx < nn ? (std::uint32_t)idx : 0xFFFFFFFFu); }
                    ns.push_back((std::uint32_t)ni.size());
                }
                if (!lx.Word(w) || w != "}") return Fail("a MeshNormals object is not closed");
                if (nfn == nf && nn > 0) { m.nrm.swap(nrm); m.nstart.swap(ns); m.nidx.swap(ni); m.haveNrm = true; }
                else ++badNormals;
            } else if (w == "MeshTextureCoords") {
                if (!lx.Head(nullptr)) return false;
                long long nt;
                if (!Count(nt, 2, "texture coordinate", kMaxVerts)) return false;
                std::vector<Vec2> uv;
                uv.reserve((std::size_t)nt);
                for (long long i = 0; i < nt; ++i) {
                    double a, b;
                    if (!lx.Float(a) || !lx.Float(b)) return false;
                    uv.push_back(Vec2{(float)a, (float)b});
                }
                if (!lx.Word(w) || w != "}") return Fail("a MeshTextureCoords object is not closed");
                if (m.uv.empty() && nt == nv) m.uv.swap(uv);
                else if (!m.uv.empty()) ++extraTexCoords;
                else ++badUv;
            } else if (w == "MeshMaterialList") {
                if (!lx.Head(nullptr)) return false;
                long long nm, ni;
                if (!lx.Int(nm) || !lx.Int(ni)) return false;
                if (nm < 0 || nm > (long long)kMaxMaterials || ni < 0 || (std::size_t)ni > kMaxFaces) return Fail("a MeshMaterialList has a bad count");
                if (ni > 0 && (std::size_t)ni > lx.Left() + 1) return Fail("a MeshMaterialList has more face indices than the file holds");
                std::vector<long long> ids;
                ids.reserve((std::size_t)ni);
                for (long long i = 0; i < ni; ++i) { long long id; if (!lx.Int(id)) return false; ids.push_back(id); }
                for (;;) {
                    if (!lx.Word(w)) return Fail("the file ends inside a material list");
                    if (w == "}") break;
                    if (w == "{") {
                        std::string ref;
                        if (!lx.Word(ref)) return false;
                        XMesh::Slot s; s.ref = ref;
                        m.slots.push_back(s);
                        if (!lx.Word(w) || w != "}") return Fail("a material reference is not closed");
                    } else if (w == "Material") {
                        MatDef md;
                        if (mats.size() >= kMaxMaterials) return Fail("too many materials");
                        if (!ParseMaterial(md)) return false;
                        XMesh::Slot s; s.def = (int)mats.size();
                        mats.push_back(std::move(md));
                        m.slots.push_back(s);
                    } else { if (!lx.Head(nullptr) || !lx.SkipToClose()) return false; ++unknownObjects; }
                }
                m.faceMat.assign((std::size_t)nf, -1);
                for (std::size_t f = 0; f < m.faceMat.size() && !ids.empty(); ++f) {
                    const long long id = f < ids.size() ? ids[f] : ids.front();   // a short list repeats its first entry (a one-entry list means all faces)
                    m.faceMat[f] = id >= 0 && id < (long long)m.slots.size() ? (int)id : -1;
                }
            } else if (w == "SkinWeights") {
                if (!lx.Head(nullptr)) return false;
                XBone b;
                if (!lx.Word(b.name)) return false;
                long long nw;
                if (!Count(nw, 2, "skin weight", kMaxVerts)) return false;
                b.v.reserve((std::size_t)nw);
                for (long long i = 0; i < nw; ++i) { long long v; if (!lx.Int(v)) return false; b.v.push_back(v >= 0 ? (std::uint32_t)v : 0xFFFFFFFFu); }
                b.w.reserve((std::size_t)nw);
                for (long long i = 0; i < nw; ++i) { double x; if (!lx.Float(x)) return false; b.w.push_back((float)x); }
                if (!Mat(b.off)) return false;
                if (!lx.Word(w) || w != "}") return Fail("a SkinWeights object is not closed");
                if (m.bones.size() >= kMaxBones) return Fail("too many bones");
                m.bones.push_back(std::move(b));
            } else if (w == "MeshVertexColors") {
                if (!lx.Head(nullptr) || !lx.SkipToClose()) return false;
                ++colorSets;
            } else if (w == "{") {
                if (!lx.SkipToClose()) return false;   // a reference
            } else {
                if (!lx.Head(nullptr) || !lx.SkipToClose()) return false;   // XSkinMeshHeader, VertexDuplicationIndices, FVFData, DeclData ...
                if (w != "XSkinMeshHeader" && w != "VertexDuplicationIndices") ++unknownObjects;
            }
        }
    }

    bool ParseAnimationKey(XAnimBone& ab) {
        if (!lx.Head(nullptr)) return false;
        long long type, nk;
        if (!lx.Int(type) || !Count(nk, 2, "animation key", kMaxKeys)) return false;
        if (totalKeys + (std::size_t)nk > kMaxKeys) return Fail("too many animation keys");
        totalKeys += (std::size_t)nk;
        const int want = type == 0 ? 4 : (type == 1 || type == 2) ? 3 : (type == 3 || type == 4) ? 16 : -1;
        std::vector<XKey>* dst = type == 0 ? &ab.rot : type == 1 ? &ab.scl : type == 2 ? &ab.pos : (type == 3 || type == 4) ? &ab.mat : nullptr;
        for (long long i = 0; i < nk; ++i) {
            long long t, nvals;
            if (!lx.Int(t) || !lx.Int(nvals)) return false;
            if (nvals < 0 || nvals > 4096) return Fail("an animation key has a bad value count");
            XKey k{};
            k.time = (double)t;
            bool use = dst && nvals == want && t >= 0;
            for (long long j = 0; j < nvals; ++j) { double x; if (!lx.Float(x)) return false; if (j < 16) k.v[j] = x; }
            if (use) dst->push_back(k); else ++badKeys;
        }
        std::string w;
        if (!lx.Word(w) || w != "}") return Fail("an AnimationKey object is not closed");
        return true;
    }

    bool ParseAnimationSet(XAnimSet& set) {
        std::string nm;
        if (!lx.Head(&nm)) return false;
        set.name = nm;
        std::string w;
        for (;;) {
            if (!lx.Word(w)) return Fail("the file ends inside an animation set");
            if (w == "}") return true;
            if (w == "Animation") {
                if (!lx.Head(nullptr)) return false;
                XAnimBone ab;
                for (;;) {
                    if (!lx.Word(w)) return Fail("the file ends inside an animation");
                    if (w == "}") break;
                    if (w == "{") {
                        std::string ref;
                        if (!lx.Word(ref)) return false;
                        if (ab.frame.empty()) ab.frame = ref;
                        if (!lx.Word(w) || w != "}") return Fail("an animation's frame reference is not closed");
                    } else if (w == "AnimationKey") { if (!ParseAnimationKey(ab)) return false; }
                    else { if (!lx.Head(nullptr) || !lx.SkipToClose()) return false; }   // AnimationOptions
                }
                set.bones.push_back(std::move(ab));
            } else if (w == "{") { if (!lx.SkipToClose()) return false; }
            else { if (!lx.Head(nullptr) || !lx.SkipToClose()) return false; }
        }
    }

    // The whole body: frames with an explicit stack, everything else as it comes.
    bool ParseBody() {
        std::vector<int> stack;
        std::string w;
        while (lx.Word(w)) {
            if (w == "template") { if (!lx.Head(nullptr) || !lx.SkipToClose()) return false; }
            else if (w == "Frame") {
                if (frames.size() >= kMaxNodes) return Fail("too many frames");
                if (stack.size() >= kMaxDepth) return Fail("frames nested too deeply");
                XFrame f;
                if (!lx.Head(&f.name)) return false;
                f.parent = stack.empty() ? -1 : stack.back();
                if (stack.empty()) ++topFrames;
                IdentityMat(f.m);
                stack.push_back((int)frames.size());
                frames.push_back(std::move(f));
            } else if (w == "}") {
                if (stack.empty()) return Fail("a closing brace without an object");
                stack.pop_back();
            } else if (w == "FrameTransformMatrix") {
                if (!lx.Head(nullptr)) return false;
                Mat16 m;
                if (!Mat(m)) return false;
                if (!lx.Word(w) || w != "}") return Fail("a FrameTransformMatrix object is not closed");
                if (!stack.empty()) { frames[(std::size_t)stack.back()].m = m; frames[(std::size_t)stack.back()].hasM = true; }
            } else if (w == "Mesh") {
                if (meshes.size() >= kMaxNodes) return Fail("too many meshes");
                XMesh m;
                m.frame = stack.empty() ? -1 : stack.back();
                if (!ParseMesh(m)) return false;
                meshes.push_back(std::move(m));
            } else if (w == "Material") {
                MatDef md;
                if (mats.size() >= kMaxMaterials) return Fail("too many materials");
                if (!ParseMaterial(md)) return false;
                mats.push_back(std::move(md));
            } else if (w == "AnimTicksPerSecond") {
                if (!lx.Head(nullptr)) return false;
                long long t;
                if (!lx.Int(t)) return false;
                if (t > 0) ticks = (double)t;
                if (!lx.Word(w) || w != "}") return Fail("an AnimTicksPerSecond object is not closed");
            } else if (w == "AnimationSet") {
                if (anims.size() >= 10000) return Fail("too many animation sets");
                XAnimSet s;
                if (!ParseAnimationSet(s)) return false;
                anims.push_back(std::move(s));
            } else if (w == "{") {
                if (!lx.SkipToClose()) return false;   // a reference
                if (!stack.empty()) ++meshRefs;
            } else {
                if (!lx.Head(nullptr) || !lx.SkipToClose()) return false;   // Header, an unknown object, ...
                ++unknownObjects;
                ++unknownNames[w];
            }
        }
        if (lx.bad) return false;
        if (!stack.empty()) return Fail("the file ends inside a frame");
        return true;
    }
};

// ---- MSZIP: independent-looking blocks that share one 32 KiB window ------------------------------------------------
bool Mszip(const unsigned char* d, std::size_t n, std::vector<unsigned char>& out, std::string& err) {
    out.clear();
    if (n < 4) { err = "the compressed data is too short"; return false; }
    std::uint32_t total = (std::uint32_t)d[0] | ((std::uint32_t)d[1] << 8) | ((std::uint32_t)d[2] << 16) | ((std::uint32_t)d[3] << 24);   // size of the whole file when unpacked
    if (total > kMaxBody) { err = "the unpacked size is too large"; return false; }
    std::size_t p = 4;
    while (p < n) {
        if (n - p < 6) { err = "a compressed block header is cut off"; return false; }
        const std::size_t usize = (std::size_t)d[p] | ((std::size_t)d[p + 1] << 8);
        const std::size_t csize = (std::size_t)d[p + 2] | ((std::size_t)d[p + 3] << 8);
        p += 4;
        if (usize == 0 || usize > 32768 || csize < 2 || csize > n - p) { err = "a compressed block has a bad size"; return false; }
        if (d[p] != 'C' || d[p + 1] != 'K') { err = "a compressed block does not start with CK (not MSZIP)"; return false; }
        if (out.size() + usize > kMaxBody) { err = "the unpacked data is too large"; return false; }
        // the block may refer back into the previous 32 KiB: put them in front as a stored deflate block, inflate, drop them again
        const std::size_t win = std::min<std::size_t>(out.size(), 32768);
        std::vector<std::uint8_t> in;
        in.reserve(5 + win + csize);
        if (win > 0) {
            in.push_back(0x00);   // not final, stored
            in.push_back((std::uint8_t)(win & 255)); in.push_back((std::uint8_t)(win >> 8));
            in.push_back((std::uint8_t)(~win & 255)); in.push_back((std::uint8_t)((~win >> 8) & 255));
            in.insert(in.end(), out.end() - (std::ptrdiff_t)win, out.end());
        }
        in.insert(in.end(), d + p + 2, d + p + csize);
        std::vector<std::uint8_t> block;
        if (!okay::Inflate(in.data(), in.size(), false, block, win + usize)) { err = "a compressed block is damaged"; return false; }
        if (block.size() != win + usize) { err = "a compressed block unpacks to a different size than it says"; return false; }
        out.insert(out.end(), block.begin() + (std::ptrdiff_t)win, block.end());
        p += csize;
    }
    return true;
}

// ---- matrices ------------------------------------------------------------------------------------------------------
struct Trs { Vec3 pos{0, 0, 0}; Quat rot{0, 0, 0, 1}; Vec3 scl{1, 1, 1}; bool sheared = false, degenerate = false; };

// The 16 numbers as written (rows, row vectors) to a column-vector matrix A[r][c] = f[c * 4 + r], then the Z mirror
// A' = M A M with M = diag(1, 1, -1): the entries (0,2), (1,2), (2,0), (2,1) and (2,3) change sign.
void MirroredColumnMatrix(const Mat16& m, double A[3][4]) {
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c) A[r][c] = m.f[c * 4 + r];
    A[0][2] = -A[0][2]; A[1][2] = -A[1][2]; A[2][0] = -A[2][0]; A[2][1] = -A[2][1]; A[2][3] = -A[2][3];
}

Trs Decompose(const double A[3][4]) {
    Trs t;
    t.pos = Vec3{(float)A[0][3], (float)A[1][3], (float)A[2][3]};
    double col[3][3], s[3];
    for (int c = 0; c < 3; ++c) { for (int r = 0; r < 3; ++r) col[c][r] = A[r][c]; s[c] = std::sqrt(col[c][0] * col[c][0] + col[c][1] * col[c][1] + col[c][2] * col[c][2]); }
    const double det = col[0][0] * (col[1][1] * col[2][2] - col[1][2] * col[2][1]) - col[1][0] * (col[0][1] * col[2][2] - col[0][2] * col[2][1]) + col[2][0] * (col[0][1] * col[1][2] - col[0][2] * col[1][1]);
    for (int c = 0; c < 3; ++c) if (!(s[c] > 1e-9)) { t.degenerate = true; s[c] = 1e-6; for (int r = 0; r < 3; ++r) col[c][r] = (r == c) ? 1.0 : 0.0; }
    if (det < 0) s[0] = -s[0];
    double R[3][3];   // R[row][col]
    for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r) R[r][c] = col[c][r] / (s[c] < 0 ? -std::fabs(s[c]) : s[c]);
    // columns of R should be orthogonal: a shear cannot be stored in a position, rotation and scale
    for (int i = 0; i < 3; ++i) for (int j = i + 1; j < 3; ++j) {
        double dot = 0; for (int r = 0; r < 3; ++r) dot += R[r][i] * R[r][j];
        if (std::fabs(dot) > 1e-3) t.sheared = true;
    }
    t.scl = Vec3{(float)s[0], (float)s[1], (float)s[2]};
    const double tr = R[0][0] + R[1][1] + R[2][2];
    double x, y, z, w;
    if (tr > 0) { const double q = std::sqrt(tr + 1.0) * 2; w = 0.25 * q; x = (R[2][1] - R[1][2]) / q; y = (R[0][2] - R[2][0]) / q; z = (R[1][0] - R[0][1]) / q; }
    else if (R[0][0] > R[1][1] && R[0][0] > R[2][2]) { const double q = std::sqrt(1.0 + R[0][0] - R[1][1] - R[2][2]) * 2; w = (R[2][1] - R[1][2]) / q; x = 0.25 * q; y = (R[0][1] + R[1][0]) / q; z = (R[0][2] + R[2][0]) / q; }
    else if (R[1][1] > R[2][2]) { const double q = std::sqrt(1.0 + R[1][1] - R[0][0] - R[2][2]) * 2; w = (R[0][2] - R[2][0]) / q; x = (R[0][1] + R[1][0]) / q; y = 0.25 * q; z = (R[1][2] + R[2][1]) / q; }
    else { const double q = std::sqrt(1.0 + R[2][2] - R[0][0] - R[1][1]) * 2; w = (R[1][0] - R[0][1]) / q; x = (R[0][2] + R[2][0]) / q; y = (R[1][2] + R[2][1]) / q; z = 0.25 * q; }
    t.rot = Quat{(float)x, (float)y, (float)z, (float)w}.Normalized();
    return t;
}

bool FiniteTrs(const Trs& t) {
    return std::isfinite(t.pos.x) && std::isfinite(t.pos.y) && std::isfinite(t.pos.z) && std::isfinite(t.scl.x) && std::isfinite(t.scl.y) && std::isfinite(t.scl.z) &&
           std::isfinite(t.rot.x) && std::isfinite(t.rot.y) && std::isfinite(t.rot.z) && std::isfinite(t.rot.w);
}

} // namespace

static bool ReadXImpl(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = "X: " + m; return false; };
    if (bytes.size() < 16) return fail("file too short");
    if (std::memcmp(bytes.data(), "xof ", 4) != 0) return fail("not a DirectX .x file (the magic text 'xof ' is missing)");
    const char* h = bytes.data();
    for (int i : {4, 5, 6, 7, 12, 13, 14, 15}) if (h[i] < '0' || h[i] > '9') return fail("the file header is damaged");
    const int fbits = (h[12] - '0') * 1000 + (h[13] - '0') * 100 + (h[14] - '0') * 10 + (h[15] - '0');
    if (fbits != 32 && fbits != 64) return fail("unknown float size " + std::to_string(fbits) + " in the header");
    const std::string fmt(h + 8, 4);
    bool bin, comp = false;
    if (fmt == "txt ") bin = false;
    else if (fmt == "bin ") bin = true;
    else if (fmt == "tzip") { bin = false; comp = true; }
    else if (fmt == "bzip") { bin = true; comp = true; }
    else return fail("unsupported format '" + fmt + "'");

    std::vector<unsigned char> unpacked;
    Parser ps;
    ps.lx.bin = bin;
    ps.lx.fsz = fbits / 8;
    if (comp) {
        std::string e;
        if (!Mszip((const unsigned char*)bytes.data() + 16, bytes.size() - 16, unpacked, e)) return fail(e);
        ps.lx.d = unpacked.data();
        ps.lx.n = unpacked.size();
    } else {
        ps.lx.d = (const unsigned char*)bytes.data() + 16;
        ps.lx.n = bytes.size() - 16;
        // text files may be UTF-16 only in theory (the format is ASCII): not handled
    }
    if (!ps.ParseBody()) return fail(ps.lx.err.empty() ? "could not read the file" : ps.lx.err);
    if (ps.frames.empty() && ps.meshes.empty()) return fail("the file has no frames and no meshes");
    if (ps.meshes.empty()) return fail("the file has no mesh");

    int sheared = 0, degenerate = 0;
    // ---- nodes ----
    const bool synthRoot = ps.topFrames != 1 || ps.frames.empty();
    const int shift = synthRoot ? 1 : 0;
    std::vector<std::string> nodeName(ps.frames.size());
    {
        std::unordered_map<std::string, int> used;
        used["X"] = synthRoot ? 1 : 0;
        for (std::size_t i = 0; i < ps.frames.size(); ++i) {
            std::string nm = ps.frames[i].name.empty() ? "frame" + std::to_string(i) : ps.frames[i].name;
            int& u = used[nm];
            if (u++) { nm += "_" + std::to_string(u - 1); used[nm] = 1; }
            nodeName[i] = nm;
        }
    }
    std::unordered_map<std::string, int> frameByName;   // the file's own name -> first frame with it
    for (std::size_t i = 0; i < ps.frames.size(); ++i) { const std::string nm = ps.frames[i].name.empty() ? "frame" + std::to_string(i) : ps.frames[i].name; frameByName.emplace(nm, (int)i); }
    if (synthRoot) { ImportedNode r; r.name = "X"; out.nodes.push_back(r); }
    std::vector<Mat4> world(ps.frames.size());
    for (std::size_t i = 0; i < ps.frames.size(); ++i) {
        double A[3][4];
        MirroredColumnMatrix(ps.frames[i].m, A);
        const Trs t = Decompose(A);
        if (!FiniteTrs(t)) return fail("a frame transform is not finite");
        if (t.sheared) ++sheared;
        if (t.degenerate) ++degenerate;
        ImportedNode nd;
        nd.name = nodeName[i];
        nd.parent = ps.frames[i].parent >= 0 ? ps.frames[i].parent + shift : (synthRoot ? 0 : -1);
        nd.position = t.pos; nd.rotation = t.rot; nd.scale = t.scl;
        const Mat4 l = Mat4::TRS(nd.position, nd.rotation, nd.scale);
        world[i] = ps.frames[i].parent >= 0 ? world[(std::size_t)ps.frames[i].parent] * l : l;
        for (int k = 0; k < 16; ++k) if (!std::isfinite(world[i].m[k]) || std::fabs(world[i].m[k]) > 1e30f) return fail("frame transforms overflow");
        out.nodes.push_back(std::move(nd));
    }
    if (out.nodes.empty()) { ImportedNode r; r.name = "X"; out.nodes.push_back(r); }

    // ---- materials ----
    std::unordered_map<std::string, int> matByName;
    for (std::size_t i = 0; i < ps.mats.size(); ++i) {
        const MatDef& m = ps.mats[i];
        ImportedMaterial im;
        im.name = m.name;
        im.hasDiffuse = true;
        im.diffuse = Color{m.diff[0], m.diff[1], m.diff[2], std::min(1.0f, std::max(0.0f, m.diff[3]))};
        if (m.emis[0] > 0 || m.emis[1] > 0 || m.emis[2] > 0) { im.hasEmissive = true; im.emissive = Color{m.emis[0], m.emis[1], m.emis[2], 1.0f}; }
        if (m.power > 0) im.shininess = m.power;
        auto fix = [&](std::string t) {
            if (t.compare(0, 2, "./") == 0) t.erase(0, 2);
            return (!baseDir.empty() && !t.empty() && t[0] != '/' && t.find(':') == std::string::npos) ? baseDir + t : t;
        };
        im.texture = fix(m.tex);
        im.normalMap = fix(m.nrmTex);
        if (!m.name.empty()) matByName[m.name] = (int)i;
        out.materials.push_back(std::move(im));
    }

    // ---- meshes ----
    int unresolvedMat = 0, skinnedParts = 0, unweighted = 0, badBoneNames = 0, badWeights = 0, emptyMeshes = 0, degenFaces = 0, shortFaces = 0, noNormalFaces = 0, droppedWeights = 0;
    std::size_t outVerts = 0;
    for (const XMesh& m : ps.meshes) {
        const std::size_t nf = m.fstart.empty() ? 0 : m.fstart.size() - 1;
        // bone table of this mesh: names resolved to frames
        std::vector<int> boneFrame(m.bones.size(), -1);
        for (std::size_t b = 0; b < m.bones.size(); ++b) { auto it = frameByName.find(m.bones[b].name); if (it != frameByName.end()) boneFrame[b] = it->second; else ++badBoneNames; }
        // per vertex weights (up to four strongest)
        struct W { int bone[4]; float w[4]; int cnt = 0; };
        std::vector<W> vw;
        bool skinned = false;
        for (std::size_t b = 0; b < m.bones.size(); ++b) if (boneFrame[b] >= 0 && !m.bones[b].v.empty()) skinned = true;
        if (skinned) {
            vw.assign(m.pos.size(), W{{0, 0, 0, 0}, {0, 0, 0, 0}, 0});
            for (std::size_t b = 0; b < m.bones.size(); ++b) {
                if (boneFrame[b] < 0) continue;
                for (std::size_t i = 0; i < m.bones[b].v.size() && i < m.bones[b].w.size(); ++i) {
                    const std::uint32_t v = m.bones[b].v[i];
                    const float w = m.bones[b].w[i];
                    if (v >= m.pos.size() || !std::isfinite(w) || !(w > 0.0f)) { ++badWeights; continue; }
                    W& x = vw[v];
                    int slot = -1;
                    for (int k = 0; k < x.cnt; ++k) if (x.bone[k] == (int)b) { slot = k; break; }
                    if (slot >= 0) { x.w[slot] += w; continue; }
                    if (x.cnt < 4) { x.bone[x.cnt] = (int)b; x.w[x.cnt] = w; ++x.cnt; continue; }
                    int weakest = 0;
                    for (int k = 1; k < 4; ++k) if (x.w[k] < x.w[weakest]) weakest = k;
                    ++droppedWeights;
                    if (w > x.w[weakest]) { x.bone[weakest] = (int)b; x.w[weakest] = w; }
                }
            }
        }
        // The mesh's frame and its world matrix. The vertices stay in the frame's own coordinates; the engine skins a vertex as
        // jointWorld * inverseBind * v, and the file's offset matrix maps the mesh's coordinates to the bone's, so it is the
        // inverse bind matrix as it stands (after the Z mirror).
        const int meshNode = m.frame >= 0 ? m.frame + shift : 0;
        const Mat4 wm = m.frame >= 0 ? world[(std::size_t)m.frame] : Mat4::Identity();
        std::vector<ImportedBone> partBones;
        std::vector<int> partBoneOf(m.bones.size(), -1);
        if (skinned) {
            for (std::size_t b = 0; b < m.bones.size(); ++b) {
                if (boneFrame[b] < 0) continue;
                double A[3][4];
                MirroredColumnMatrix(m.bones[b].off, A);
                Mat4 off = Mat4::Identity();
                for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c) off.at(c, r) = (float)A[r][c];
                ImportedBone ib;
                ib.name = nodeName[(std::size_t)boneFrame[b]];
                ib.inverseBind = off;
                partBoneOf[b] = (int)partBones.size();
                partBones.push_back(ib);
            }
        }
        // group faces by material slot (-1 = none)
        std::vector<int> slotOrder;   // distinct slots in first-use order
        std::unordered_map<int, std::size_t> slotIndex;
        std::vector<std::vector<std::uint32_t>> facesOfSlot;
        for (std::size_t f = 0; f < nf; ++f) {
            const int s = m.faceMat.empty() ? -1 : m.faceMat[f];
            auto it = slotIndex.find(s);
            if (it == slotIndex.end()) { it = slotIndex.emplace(s, slotOrder.size()).first; slotOrder.push_back(s); facesOfSlot.emplace_back(); }
            facesOfSlot[it->second].push_back((std::uint32_t)f);
        }
        bool any = false;
        for (std::size_t g = 0; g < slotOrder.size(); ++g) {
            ImportedPart part;
            bool partNeedsRoot = false;
            std::unordered_map<std::uint64_t, int> weld;
            for (std::uint32_t f : facesOfSlot[g]) {
                const std::uint32_t b0 = m.fstart[f], e0 = m.fstart[f + 1];
                if (e0 - b0 < 3) { if (e0 > b0) ++shortFaces; continue; }
                for (std::uint32_t k = 1; k + 1 < e0 - b0; ++k) {   // fan; winding reversed (the Z mirror turns it)
                    const std::uint32_t corner[3] = {0, k + 1, k};
                    int ids[3];
                    bool degen = false;
                    if (m.fidx[b0 + corner[0]] == m.fidx[b0 + corner[1]] || m.fidx[b0 + corner[1]] == m.fidx[b0 + corner[2]] || m.fidx[b0 + corner[0]] == m.fidx[b0 + corner[2]]) degen = true;
                    if (degen) { ++degenFaces; continue; }
                    for (int q = 0; q < 3; ++q) {
                        const std::uint32_t pi = m.fidx[b0 + corner[q]];
                        std::uint32_t ni = 0xFFFFFFFEu;
                        if (m.haveNrm) {
                            const std::uint32_t nb = m.nstart[f], ne = m.nstart[f + 1];
                            ni = (corner[q] < ne - nb) ? m.nidx[nb + corner[q]] : 0xFFFFFFFFu;
                            if (ni == 0xFFFFFFFFu) { ++noNormalFaces; ni = 0xFFFFFFFEu; }
                        }
                        const std::uint64_t key = ((std::uint64_t)pi << 32) | ni;
                        auto it = weld.find(key);
                        if (it == weld.end()) {
                            if (++outVerts > kMaxVerts) return fail("too many output vertices");
                            const int vi = (int)part.mesh.vertices.size();
                            weld.emplace(key, vi);
                            Vec3 p = m.pos[pi];
                            p = Vec3{p.x, p.y, -p.z};
                            Vec3 nn{0, 0, 0};
                            if (ni < m.nrm.size()) nn = Vec3{m.nrm[ni].x, m.nrm[ni].y, -m.nrm[ni].z};
                            part.mesh.vertices.push_back(p);
                            if (m.haveNrm) part.mesh.normals.push_back(nn);
                            if (!m.uv.empty()) part.mesh.uvs.push_back(m.uv[pi]);
                            if (skinned) {
                                std::array<int, 4> ji{0, 0, 0, 0};
                                std::array<float, 4> jw{0, 0, 0, 0};
                                const W& x = vw[pi];
                                float sum = 0; int cnt = 0;
                                for (int k2 = 0; k2 < x.cnt; ++k2) if (partBoneOf[(std::size_t)x.bone[k2]] >= 0) { ji[(std::size_t)cnt] = partBoneOf[(std::size_t)x.bone[k2]]; jw[(std::size_t)cnt] = x.w[k2]; sum += x.w[k2]; ++cnt; }
                                if (cnt == 0 || !(sum > 0)) { ++unweighted; ji = {-2, 0, 0, 0}; jw = {1, 0, 0, 0}; partNeedsRoot = true; }
                                else for (float& w : jw) w /= sum;
                                part.jointIdx.push_back(ji);
                                part.jointWt.push_back(jw);
                            }
                            ids[q] = vi;
                        } else ids[q] = it->second;
                    }
                    part.mesh.triangles.push_back(ids[0]); part.mesh.triangles.push_back(ids[1]); part.mesh.triangles.push_back(ids[2]);
                }
            }
            if (part.mesh.triangles.empty()) continue;
            if (m.haveNrm) {
                bool bad = false;
                for (Vec3& n : part.mesh.normals) { const float l = n.Magnitude(); if (!(l > 1e-9f) || !std::isfinite(l)) { bad = true; break; } n = n * (1.0f / l); }
                if (bad) part.mesh.ComputeSmoothNormals();
            } else part.mesh.ComputeSmoothNormals();
            if (!detail::rc::PartInRange(part)) return fail("a vertex is out of range");
            part.mesh.name = m.frame >= 0 ? nodeName[(std::size_t)m.frame] : "mesh";
            part.node = meshNode;
            const int slot = slotOrder[g];
            if (slot >= 0) {
                const XMesh::Slot& s = m.slots[(std::size_t)slot];
                int mi = s.def;
                if (mi < 0) { auto it = matByName.find(s.ref); if (it != matByName.end()) mi = it->second; else ++unresolvedMat; }
                part.material = mi;
            }
            if (skinned) {
                part.bones = partBones;
                ++skinnedParts;
                if (partNeedsRoot) {   // vertices without a weight follow the root node
                    const int rootBone = (int)part.bones.size();
                    ImportedBone rb; rb.name = out.nodes[0].name; rb.inverseBind = (synthRoot || ps.frames.empty() ? Mat4::Identity() : world[0].Inverse()) * wm;
                    part.bones.push_back(rb);
                    for (auto& ji : part.jointIdx) for (int& x : ji) if (x == -2) x = rootBone;
                }
            }
            out.parts.push_back(std::move(part));
            any = true;
        }
        if (!any) ++emptyMeshes;
    }
    if (out.parts.empty()) return fail("the file has no triangles");
    // ---- animation: ticks per second as the file says. Without AnimTicksPerSecond (the format's own default is 4800, but files
    // written by game tools key frame numbers) 25 is used, which is what the engine's Assimp path assumes for a file without one ----
    const double tps = ps.ticks > 0 ? ps.ticks : 25.0;
    int unresolvedAnim = 0;
    for (const XAnimSet& set : ps.anims) {
        ImportedClip clip;
        clip.name = set.name.empty() ? "animation" : set.name;
        for (const XAnimBone& ab : set.bones) {
            auto it = frameByName.find(ab.frame);
            if (it == frameByName.end()) { ++unresolvedAnim; continue; }
            ImportedChannel ch;
            ch.node = nodeName[(std::size_t)it->second];
            auto sortT = [&](auto& v) {
                std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
            };
            std::vector<ImportedKeyQ> rk; std::vector<ImportedKeyV3> pk, sk;
            for (const XKey& k : ab.rot) {
                const double l = std::sqrt(k.v[0] * k.v[0] + k.v[1] * k.v[1] + k.v[2] * k.v[2] + k.v[3] * k.v[3]);
                Quat q{0, 0, 0, 1};
                if (l > 1e-12) q = Quat{(float)(k.v[1] / l), (float)(k.v[2] / l), (float)(-k.v[3] / l), (float)(k.v[0] / l)};
                rk.push_back({(float)(k.time / tps), q});
            }
            for (const XKey& k : ab.pos) pk.push_back({(float)(k.time / tps), Vec3{(float)k.v[0], (float)k.v[1], (float)-k.v[2]}});
            for (const XKey& k : ab.scl) sk.push_back({(float)(k.time / tps), Vec3{(float)k.v[0], (float)k.v[1], (float)k.v[2]}});
            for (const XKey& k : ab.mat) {
                Mat16 m16;
                for (int i = 0; i < 16; ++i) m16.f[i] = k.v[i];
                double A[3][4];
                MirroredColumnMatrix(m16, A);
                const Trs t = Decompose(A);
                if (!FiniteTrs(t)) continue;
                const float tm = (float)(k.time / tps);
                pk.push_back({tm, t.pos}); sk.push_back({tm, t.scl}); rk.push_back({tm, t.rot});
            }
            sortT(rk); sortT(pk); sortT(sk);
            ch.rotation = std::move(rk); ch.position = std::move(pk); ch.scale = std::move(sk);
            if (!ch.rotation.empty() || !ch.position.empty() || !ch.scale.empty()) clip.channels.push_back(std::move(ch));
        }
        if (!clip.channels.empty()) out.clips.push_back(std::move(clip));
    }

    out.backend = "own";
    auto note = [&](const std::string& s) { out.notes.push_back("X: " + s); };
    note("DirectX is left-handed: Z was mirrored and the winding reversed to make the model right-handed; matrices are read for row vectors");
    if (synthRoot) note("several top-level frames, or meshes outside any frame: they were put under a new root node");
    if (!ps.anims.empty() && ps.ticks <= 0) note("no AnimTicksPerSecond in the file: 25 ticks per second assumed (what the engine's Assimp path assumes; the format's own default would be 4800)");
    if (ps.colorSets) note(std::to_string(ps.colorSets) + " vertex colour set(s) not imported");
    if (ps.extraTexCoords) note(std::to_string(ps.extraTexCoords) + " further texture coordinate set(s) not imported");
    if (ps.badUv) note(std::to_string(ps.badUv) + " texture coordinate set(s) whose count differs from the vertex count ignored");
    if (ps.badNormals) note(std::to_string(ps.badNormals) + " normal set(s) whose face count differs from the face count ignored: smooth normals were generated");
    if (noNormalFaces) note(std::to_string(noNormalFaces) + " face corner(s) without a valid normal index");
    if (ps.badFaces) note(std::to_string(ps.badFaces) + " face(s) naming a missing vertex skipped");
    if (shortFaces) note(std::to_string(shortFaces) + " face(s) with fewer than 3 vertices skipped");
    if (degenFaces) note(std::to_string(degenFaces) + " triangle(s) that repeat a vertex skipped");
    if (unresolvedMat) note(std::to_string(unresolvedMat) + " material reference(s) to a material that is not defined: left without one");
    if (unweighted) note(std::to_string(unweighted) + " vertex(es) of skinned meshes have no weight and follow the root node");
    if (droppedWeights) note(std::to_string(droppedWeights) + " bone weight(s) beyond four per vertex: the four strongest were kept");
    if (badBoneNames) note(std::to_string(badBoneNames) + " skin weight set(s) name a frame that does not exist and were ignored");
    if (badWeights) note(std::to_string(badWeights) + " bone weight(s) with a bad vertex or weight skipped");
    if (unresolvedAnim) note(std::to_string(unresolvedAnim) + " animation(s) name a frame that does not exist and were skipped");
    if (ps.badKeys) note(std::to_string(ps.badKeys) + " animation key(s) of an unknown type, with the wrong value count or a negative time skipped");
    if (sheared) note(std::to_string(sheared) + " frame transform(s) contain a shear, which a position, rotation and scale cannot hold: the shear was dropped");
    if (degenerate) note(std::to_string(degenerate) + " frame transform(s) with a zero scale axis were given a tiny scale");
    if (emptyMeshes) note(std::to_string(emptyMeshes) + " mesh(es) without usable triangles skipped");
    if (ps.meshRefs) note(std::to_string(ps.meshRefs) + " reference(s) inside frames (shared meshes) are not followed");
    if (ps.unknownObjects) {
        std::string s;
        for (const auto& kv : ps.unknownNames) { if (!s.empty()) s += ", "; s += kv.first; }
        note(std::to_string(ps.unknownObjects) + " object(s) not read" + (s.empty() ? std::string() : " (" + s + ")") + ": vertex colours, shared-vertex tables, mesh options and the like");
    }
    note("vertex colours, specular colour, further texture coordinate sets and animation options are not imported");
    return true;
}

bool ReadXBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    const bool ok = ReadXImpl(bytes, baseDir, out, err);
    if (!ok) out = ImportedModel{};
    return ok;
}

bool ReadXFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, xf::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    const std::size_t sl = path.find_last_of("/\\");
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    return ReadXBuffer(bytes, dir, out, err);
}

} // namespace okay
