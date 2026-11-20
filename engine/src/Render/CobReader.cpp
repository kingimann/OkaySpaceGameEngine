// Our own COB (Caligari trueSpace) reader, binary ("B") and ASCII ("A") files. No document was used: the chunk names and the ASCII keywords
// (Grou, PolH, Mat1, ShBx, Name, center, Transform, World Vertices, Faces ...) are from memory of the format; every byte layout (the 20 byte chunk
// header, the object chunk with its 48 byte centre-and-axes block and 3x4 matrix, the face record, Mat1, the ShBx parameter type codes 3 / 6 / 7 / 10
// and the "file name" parameter) was settled by a hex dump and a text view of 8 real sample files. No importer source was read. Bounds-checked,
// every count is checked against the data that is really there and a per-file budget before anything is allocated, no recursion.
// See okay/Render/ModelData.hpp and docs/content/model_readers/own_cob_reader.md.
#include "BinReader.hpp"
#include "PolygonTriangulate.hpp"
#include "ReaderCommon.hpp"
#include "SceneRefPath.hpp"
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

namespace {
namespace cob {

using detail::bin::Br;

constexpr std::size_t kMaxFileBytes = 128u * 1024u * 1024u;
// Per-file budgets (counted over all objects). The triangle budget also bounds the work of the welding step.
constexpr std::size_t kMaxObjects = 100000, kMaxVerts = 2000000, kMaxUvs = 2000000, kMaxFaces = 500000, kMaxCorners = 2000000, kMaxTris = 500000;
constexpr std::size_t kMaxFaceCorners = 4096, kMaxMats = 100000, kMaxKinds = 256, kMaxEarCorners = 48, kMaxChunks = 1000000;

bool Fail(std::string* err, const std::string& m) { if (err) *err = "COB: " + m; return false; }

struct Corner { std::int32_t v, uv; };
struct Face { std::uint32_t first; std::uint16_t n, mat; std::uint8_t flags; };
struct Obj {
    bool poly = false;
    long long id = 0, parent = 0;
    std::string name;
    float m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};   // the stored world matrix, three rows of four (the last row 0 0 0 1 is implied)
    std::vector<Vec3> v;
    std::vector<Vec2> uv;
    std::vector<Face> f;
    std::vector<Corner> c;
};
struct CMat {
    long long id = 0, objId = 0;
    int index = 0;
    char facet = 'a';
    float angle = 40.0f;
    float rgb[3] = {1, 1, 1}, alpha = 1.0f;
    std::string texture;
};

struct Parsed {
    std::vector<Obj> objs;
    std::vector<CMat> mats;
    std::unordered_map<long long, std::size_t> matById;   // chunk id -> index in mats (the first Mat1 with that id)
    std::size_t chunks = 0;
    void AddMat(const CMat& m) { matById.emplace(m.id, mats.size()); mats.push_back(m); }
    void SetTexture(long long matId, const std::string& tex) { auto it = matById.find(matId); if (it != matById.end() && mats[it->second].texture.empty()) mats[it->second].texture = tex; }
    std::map<std::string, int> skipped;   // chunk kinds that are not read, with how often each came
    int unitChunks = 0;
    std::size_t verts = 0, uvs = 0, faces = 0, corners = 0;
    int badShaders = 0;
    void Skip(const std::string& k) { if (skipped.size() < kMaxKinds || skipped.count(k)) ++skipped[k]; }
};

bool FiniteM(const float* m) { for (int i = 0; i < 12; ++i) if (!std::isfinite(m[i]) || std::fabs(m[i]) > 1e15f) return false; return true; }

// ---------------------------------------------------------------------------------------------------------------
// binary
// ---------------------------------------------------------------------------------------------------------------
// The data of a ShBx chunk: shader classes, each with a name and typed parameters. Only the "file name" string matters;
// parsing stops quietly at a type it does not know.
std::string BinShaderTexture(const unsigned char* d, std::size_t n, bool& unknown) {
    Br b(d, n);
    std::string tex;
    for (int cls = 0; cls < 16 && b.Left() >= 3; ++cls) {
        b.U8();                                   // the class (colour, transparency, reflectance, displacement)
        const std::size_t nl = b.U16();
        b.Skip(nl);                               // the shader's short name
        const std::int32_t np = b.I32();
        if (b.bad || np < 0 || np > 1000) { unknown = true; return tex; }
        for (std::int32_t i = 0; i < np; ++i) {
            const std::size_t pl = b.U16();
            if (!b.Has(pl)) { unknown = true; return tex; }
            const std::string pn((const char*)d + b.pos, pl);
            b.Skip(pl);
            const std::int32_t ty = b.I32();
            if (b.bad) { unknown = true; return tex; }
            if (ty == 3 || ty == 6 || ty == 4) b.Skip(4);          // float, bool, int
            else if (ty == 7) b.Skip(12);                           // colour: three floats
            else if (ty == 10) {                                    // string
                const std::size_t sl = b.U16();
                if (!b.Has(sl)) { unknown = true; return tex; }
                if (pn == "file name" && tex.empty()) tex.assign((const char*)d + b.pos, sl);
                b.Skip(sl);
            } else { unknown = true; return tex; }
            if (b.bad) { unknown = true; return tex; }
        }
        b.U16();                                  // flags
    }
    return tex;
}

bool ParseBinary(const std::string& bytes, Parsed& P, std::string* err) {
    Br b(bytes);
    b.Skip(32);
    std::size_t chunks = 0;
    while (b.Left() >= 20) {
        char type[5] = {0, 0, 0, 0, 0};
        for (int i = 0; i < 4; ++i) type[i] = (char)b.U8();
        b.U16(); b.U16();                         // major, minor version
        const std::int32_t id = b.I32(), parent = b.I32(), size = b.I32();
        if (size < 0 || (std::uint64_t)size > (std::uint64_t)b.Left()) return Fail(err, "a chunk is larger than the rest of the file (the file is truncated or damaged)");
        if (++chunks > kMaxChunks) return Fail(err, "too many chunks (more than " + std::to_string(kMaxChunks) + ")");
        const unsigned char* d = (const unsigned char*)bytes.data() + b.pos;
        const std::size_t sz = (std::size_t)size;
        std::string t(type, 4);
        while (!t.empty() && t.back() == ' ') t.pop_back();
        b.pos += sz;
        if (t == "END") break;
        if (t == "Grou" || t == "PolH") {
            if (P.objs.size() >= kMaxObjects) return Fail(err, "too many objects");
            Obj o;
            o.poly = t == "PolH"; o.id = id; o.parent = parent;
            Br c(d, sz);
            const std::uint16_t dupe = c.U16();
            const std::size_t nl = c.U16();
            if (!c.Has(nl)) return Fail(err, "an object's name runs past its chunk");
            o.name.assign((const char*)d + c.pos, nl);
            c.Skip(nl);
            o.name = std::string(o.name.c_str());          // up to a NUL
            if (dupe) o.name += "," + std::to_string(dupe);
            c.Skip(48);                                    // centre and the three axes (the matrix below holds the same information)
            for (float& f : o.m) f = c.F32();
            if (c.bad) return Fail(err, "an object chunk is too short for its name and matrix");
            if (!FiniteM(o.m)) return Fail(err, "an object's matrix is not finite or is out of range");
            if (o.poly) {
                const std::int32_t nv = c.I32();
                if (c.bad || nv < 0 || !c.Fits((std::uint64_t)nv, 12)) return Fail(err, "an object's vertex list runs past its chunk");
                if ((std::size_t)nv > kMaxVerts - P.verts) return Fail(err, "too many vertices (more than " + std::to_string(kMaxVerts) + " in the file)");
                P.verts += (std::size_t)nv;
                o.v.reserve((std::size_t)nv);
                for (std::int32_t i = 0; i < nv; ++i) {
                    const float x = c.F32(), y = c.F32(), z = c.F32();
                    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || std::fabs(x) > 1e15f || std::fabs(y) > 1e15f || std::fabs(z) > 1e15f) return Fail(err, "a vertex is not finite or is out of range");
                    o.v.push_back(Vec3{x, y, z});
                }
                const std::int32_t nu = c.I32();
                if (c.bad || nu < 0 || !c.Fits((std::uint64_t)nu, 8)) return Fail(err, "an object's texture vertex list runs past its chunk");
                if ((std::size_t)nu > kMaxUvs - P.uvs) return Fail(err, "too many texture vertices");
                P.uvs += (std::size_t)nu;
                o.uv.reserve((std::size_t)nu);
                for (std::int32_t i = 0; i < nu; ++i) {
                    const float u = c.F32(), v = c.F32();
                    if (!std::isfinite(u) || !std::isfinite(v) || std::fabs(u) > 1e15f || std::fabs(v) > 1e15f) return Fail(err, "a texture coordinate is not finite or is out of range");
                    o.uv.push_back(Vec2{u, v});
                }
                const std::int32_t nf = c.I32();
                if (c.bad || nf < 0 || !c.Fits((std::uint64_t)nf, 5)) return Fail(err, "an object's face list runs past its chunk");
                if ((std::size_t)nf > kMaxFaces - P.faces) return Fail(err, "too many faces");
                P.faces += (std::size_t)nf;
                o.f.reserve((std::size_t)nf);
                for (std::int32_t i = 0; i < nf; ++i) {
                    Face f;
                    f.flags = c.U8(); f.n = c.U16(); f.mat = c.U16();
                    if (c.bad) return Fail(err, "the face list ends inside a face");
                    if (f.n > kMaxFaceCorners) return Fail(err, "a face has more than " + std::to_string(kMaxFaceCorners) + " corners");
                    if (!c.Fits(f.n, 8)) return Fail(err, "the face list ends inside a face");
                    if ((std::size_t)f.n > kMaxCorners - P.corners) return Fail(err, "too many face corners");
                    P.corners += f.n;
                    f.first = (std::uint32_t)o.c.size();
                    for (std::uint16_t k = 0; k < f.n; ++k) { Corner q; q.v = c.I32(); q.uv = c.I32(); o.c.push_back(q); }
                    o.f.push_back(f);
                }
            }
            P.objs.push_back(std::move(o));
        } else if (t == "Mat1") {
            if (P.mats.size() >= kMaxMats) return Fail(err, "too many materials");
            if (sz < 37) { ++P.badShaders; continue; }
            Br c(d, sz);
            CMat m;
            m.id = id; m.objId = parent;
            m.index = c.U16();
            c.U8();                                   // shader kind: flat / phong / metal ...
            const char fc = (char)c.U8();
            m.facet = fc == 'f' ? 'f' : fc == 's' ? 's' : 'a';
            m.angle = (float)c.U8();
            for (float& f : m.rgb) f = c.F32();
            m.alpha = c.F32();
            for (float& f : m.rgb) if (!std::isfinite(f)) f = 1.0f;
            if (!std::isfinite(m.alpha)) m.alpha = 1.0f;
            P.AddMat(m);
        } else if (t == "ShBx") {
            bool unk = false;
            const std::string tex = BinShaderTexture(d, sz, unk);
            if (unk) ++P.badShaders;
            if (!tex.empty()) P.SetTexture(parent, tex);
        } else if (t == "Unit") ++P.unitChunks;
        else if (t != "OLay" && t != "ObRQ" && t != "BitM" && t != "PhAn") P.Skip(t);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// ASCII
// ---------------------------------------------------------------------------------------------------------------
// "PolH V0.08 Id 497163284 Parent 497130340 Size 00013948"
bool IsHeader(const std::string& l, std::string& type, long long& id, long long& parent) {
    if (l.size() < 20 || l.size() > 200) return false;
    const auto tk = detail::txt::Split(l);
    if (tk.size() != 8 || tk[1].size() < 2 || tk[1][0] != 'V' || tk[2] != "Id" || tk[4] != "Parent" || tk[6] != "Size") return false;
    long long a, p, s;
    if (!detail::txt::ToInt(tk[3], a) || !detail::txt::ToInt(tk[5], p) || !detail::txt::ToInt(tk[7], s)) return false;
    type = tk[0]; id = a; parent = p;
    return true;
}

bool Nums(const std::vector<std::string>& tk, std::size_t from, std::size_t count, double* out) {
    if (tk.size() < from + count) return false;
    for (std::size_t i = 0; i < count; ++i) if (!detail::txt::ToDouble(tk[from + i], out[i])) return false;
    return true;
}

struct AsciiReader {
    detail::txt::Lines lines;
    std::string pending, pendType;
    long long pendId = 0, pendParent = 0;
    bool havePending = false, atEnd = false;
    explicit AsciiReader(const std::string& t) : lines(t) {}

    // The next line of the current chunk body; false at the next chunk header (kept in `pending`) or at the end of the file.
    bool Body(std::string& l) {
        if (havePending) return false;
        std::string x;
        while (lines.Next(x)) {
            std::string t; long long i, p;
            if (IsHeader(x, t, i, p)) { havePending = true; pendType = t; pendId = i; pendParent = p; return false; }
            l = x;
            return true;
        }
        atEnd = true;
        return false;
    }
    bool Header(std::string& t, long long& id, long long& parent) {
        if (havePending) { havePending = false; t = pendType; id = pendId; parent = pendParent; return true; }
        std::string x;
        while (lines.Next(x)) {
            if (IsHeader(x, t, id, parent)) return true;
        }
        return false;
    }
};

bool ParseAscii(const std::string& text, Parsed& P, std::string* err) {
    AsciiReader R(text);
    std::string l;
    R.Body(l);   // the file header line "Caligari V00.01ALH": not a chunk header, so Body hands it back as a line and we drop it
    std::string type;
    long long id, parent;
    while (R.Header(type, id, parent)) {
        if (type == "END") break;
        if (++P.chunks > kMaxChunks) return Fail(err, "too many chunks (more than " + std::to_string(kMaxChunks) + ")");
        if (type == "Grou" || type == "PolH") {
            if (P.objs.size() >= kMaxObjects) return Fail(err, "too many objects");
            Obj o;
            o.poly = type == "PolH"; o.id = id; o.parent = parent;
            bool haveTransform = false;
            std::vector<std::string> rowTk, ft, ct;   // token vectors refilled row after row (SplitInto reuses their room)
            while (R.Body(l)) {
                const auto tk = detail::txt::Split(l);
                if (tk.empty()) continue;
                double v[4];
                if (tk[0] == "Name" && tk.size() >= 2) { o.name = l.substr(l.find("Name") + 4); const std::size_t a = o.name.find_first_not_of(" \t"); o.name = a == std::string::npos ? std::string() : o.name.substr(a); }
                else if (tk[0] == "Transform" && tk.size() == 1) {
                    for (int r = 0; r < 3; ++r) {
                        std::string row;
                        if (!R.Body(row)) return Fail(err, "the Transform of an object is cut short");
                        const auto rt = detail::txt::Split(row);
                        if (!Nums(rt, 0, 4, v)) return Fail(err, "a Transform row is not four numbers");
                        for (int c = 0; c < 4; ++c) o.m[r * 4 + c] = (float)v[c];
                    }
                    std::string last;
                    if (!R.Body(last)) return Fail(err, "the Transform of an object is cut short");   // the row 0 0 0 1
                    haveTransform = true;
                    if (!FiniteM(o.m)) return Fail(err, "an object's matrix is not finite or is out of range");
                } else if (o.poly && tk[0] == "World" && tk.size() == 3 && tk[1] == "Vertices") {
                    long long n;
                    if (!detail::txt::ToInt(tk[2], n) || n < 0) return Fail(err, "bad vertex count");
                    if ((std::size_t)n > kMaxVerts - P.verts) return Fail(err, "too many vertices (more than " + std::to_string(kMaxVerts) + " in the file)");
                    P.verts += (std::size_t)n;
                    for (long long i = 0; i < n; ++i) {
                        std::string row;
                        if (!R.Body(row)) return Fail(err, "the vertex list ends early");
                        detail::txt::SplitInto(row, rowTk);
                        if (!Nums(rowTk, 0, 3, v)) return Fail(err, "a vertex line is not three numbers");
                        o.v.push_back(Vec3{(float)v[0], (float)v[1], (float)v[2]});
                    }
                } else if (o.poly && tk[0] == "Texture" && tk.size() == 3 && tk[1] == "Vertices") {
                    long long n;
                    if (!detail::txt::ToInt(tk[2], n) || n < 0) return Fail(err, "bad texture vertex count");
                    if ((std::size_t)n > kMaxUvs - P.uvs) return Fail(err, "too many texture vertices");
                    P.uvs += (std::size_t)n;
                    for (long long i = 0; i < n; ++i) {
                        std::string row;
                        if (!R.Body(row)) return Fail(err, "the texture vertex list ends early");
                        detail::txt::SplitInto(row, rowTk);
                        if (!Nums(rowTk, 0, 2, v)) return Fail(err, "a texture vertex line is not two numbers");
                        o.uv.push_back(Vec2{(float)v[0], (float)v[1]});
                    }
                } else if (o.poly && tk[0] == "Faces" && tk.size() == 2) {
                    long long n;
                    if (!detail::txt::ToInt(tk[1], n) || n < 0) return Fail(err, "bad face count");
                    if ((std::size_t)n > kMaxFaces - P.faces) return Fail(err, "too many faces");
                    P.faces += (std::size_t)n;
                    for (long long i = 0; i < n; ++i) {
                        std::string fl, cl;
                        if (!R.Body(fl) || !R.Body(cl)) return Fail(err, "the face list ends early");
                        detail::txt::SplitInto(fl, ft);
                        long long fn, ff, fm;
                        if (ft.size() < 7 || ft[0] != "Face" || !detail::txt::ToInt(ft[2], fn) || !detail::txt::ToInt(ft[4], ff) || !detail::txt::ToInt(ft[6], fm) || fn < 0 || fn > (long long)kMaxFaceCorners || fm < 0 || fm > 65535 || ff < 0 || ff > 255)
                            return Fail(err, "a Face line is malformed");
                        if ((std::size_t)fn > kMaxCorners - P.corners) return Fail(err, "too many face corners");
                        P.corners += (std::size_t)fn;
                        Face f;
                        f.first = (std::uint32_t)o.c.size(); f.n = (std::uint16_t)fn; f.mat = (std::uint16_t)fm; f.flags = (std::uint8_t)ff;
                        detail::txt::SplitInto(cl, ct);
                        if ((long long)ct.size() < fn) return Fail(err, "a face has fewer corners than it says");
                        for (long long k = 0; k < fn; ++k) {
                            const std::string& s = ct[(std::size_t)k];   // <vertex,texture vertex>
                            const std::size_t cm = s.find(',');
                            long long a, bq;
                            if (s.size() < 5 || s[0] != '<' || s.back() != '>' || cm == std::string::npos || !detail::txt::ToInt(s.substr(1, cm - 1), a) || !detail::txt::ToInt(s.substr(cm + 1, s.size() - cm - 2), bq))
                                return Fail(err, "a face corner is not <vertex,texture vertex>");
                            o.c.push_back(Corner{(std::int32_t)a, (std::int32_t)bq});
                        }
                        o.f.push_back(f);
                    }
                }
            }
            if (!haveTransform) return Fail(err, "an object has no Transform");
            P.objs.push_back(std::move(o));
        } else if (type == "Mat1") {
            if (P.mats.size() >= kMaxMats) return Fail(err, "too many materials");
            CMat m;
            m.id = id; m.objId = parent;
            bool any = false;
            while (R.Body(l)) {
                const auto tk = detail::txt::Split(l);
                if (tk.empty()) continue;
                double v[3];
                if (tk[0] == "mat#" && tk.size() >= 2) { long long q; if (detail::txt::ToInt(tk[1], q) && q >= 0 && q <= 65535) { m.index = (int)q; any = true; } }
                else if (tk[0] == "shader:") {
                    for (std::size_t i = 1; i + 1 < tk.size(); ++i) if (tk[i] == "facet:") {
                        const std::string& f = tk[i + 1];
                        if (f.compare(0, 4, "auto") == 0) { m.facet = 'a'; double a; if (f.size() > 4 && detail::txt::ToDouble(f.substr(4), a) && a >= 0 && a <= 180) m.angle = (float)a; }
                        else if (!f.empty() && f[0] == 'f') m.facet = 'f';
                        else if (!f.empty() && f[0] == 's') m.facet = 's';
                    }
                } else if (tk[0] == "rgb") {
                    std::string j;
                    for (std::size_t i = 1; i < tk.size(); ++i) j += tk[i] + " ";
                    for (char& c : j) if (c == ',') c = ' ';
                    if (Nums(detail::txt::Split(j), 0, 3, v)) for (int i = 0; i < 3; ++i) m.rgb[i] = (float)v[i];
                } else if (tk[0] == "alpha") {
                    double a;
                    if (tk.size() >= 2 && detail::txt::ToDouble(tk[1], a)) m.alpha = (float)a;
                }
            }
            if (any) P.AddMat(m); else ++P.badShaders;
        } else if (type == "ShBx") {
            std::string tex;
            while (R.Body(l)) {
                const std::size_t a = l.find("file name: string");
                if (a != std::string::npos && tex.empty()) {
                    const std::size_t q1 = l.find('"', a), q2 = q1 == std::string::npos ? q1 : l.find('"', q1 + 1);
                    if (q2 != std::string::npos) tex = l.substr(q1 + 1, q2 - q1 - 1);
                }
            }
            if (!tex.empty()) P.SetTexture(parent, tex);
        } else {
            if (type == "Unit") ++P.unitChunks;
            else if (type != "OLay" && type != "ObRQ" && type != "BitM" && type != "PhAn") P.Skip(type);
            while (R.Body(l)) {}
        }
    }
    return true;
}

// P swaps y and z (a reflection); conjugating a matrix with it keeps scales positive and rotations rotations.
Mat4 Conj(const float* m) {
    Mat4 r;
    const int p[4] = {0, 2, 1, 3};
    for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col) {
        const int pr = p[row], pc = p[col];
        float v = pr < 3 ? m[pr * 4 + pc] : (pc == 3 ? 1.0f : 0.0f);
        r.m[col * 4 + row] = v;
    }
    return r;
}

} // namespace cob
} // namespace

// `release`: the caller's buffer behind `bytes`, which may be freed once it is parsed (ReadCobFile owns its bytes: the
// file is then not held while the meshes are built). Nothing parsed refers back into it.
static bool ReadCobImpl(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err, std::string* release = nullptr) {
    using namespace cob;
    if (bytes.size() < 32 || bytes.compare(0, 8, "Caligari") != 0 || bytes[9] != 'V') return Fail(err, "not a COB file (the text \"Caligari V..\" is missing)");
    const char kind = bytes[15];
    if (kind != 'A' && kind != 'B') return Fail(err, "the file header says neither ASCII (A) nor binary (B)");
    if (bytes[16] != 'L') return Fail(err, "only little endian COB files (header ...LH) are read; this one is marked differently and none was available to check");
    Parsed P;
    if (kind == 'B') { if (!ParseBinary(bytes, P, err)) return false; }
    else { if (!ParseAscii(bytes, P, err)) return false; }
    if (release) std::string().swap(*release);   // `bytes` is not used below

    int objectsWithoutMaterial = 0;
    // ---- hierarchy: groups and polygon objects become nodes, parents first ----
    ImportedNode root;
    root.name = "COB";
    out.nodes.push_back(root);
    const std::size_t no = P.objs.size();
    std::vector<int> par(no, -1), order, nodeOf(no, 0);
    {
        std::unordered_map<long long, int> byId;
        for (std::size_t i = 0; i < no; ++i) byId.emplace(P.objs[i].id, (int)i);
        for (std::size_t i = 0; i < no; ++i) {
            auto it = byId.find(P.objs[i].parent);
            if (it != byId.end() && it->second != (int)i) par[i] = it->second;
        }
    }
    const int loops = detail::txt::OrderParentsFirst(par, order);
    std::vector<Mat4> world(no);
    std::unordered_map<std::string, int> used;
    used["COB"] = 1;
    int mirrored = 0, nonAffine = 0, dupNames = 0, singularParents = 0;
    for (int oi : order) {
        const Obj& o = P.objs[(std::size_t)oi];
        world[(std::size_t)oi] = Conj(o.m);
        std::string nm = o.name.empty() ? (o.poly ? "object" : "group") + std::to_string(oi) : o.name;
        int& u = used[nm];
        if (u++) { ++dupNames; std::string cand; do { cand = nm + "_" + std::to_string(u++); } while (used.count(cand)); nm = cand; used[nm] = 1; }
        ImportedNode nd;
        nd.name = nm;
        const int p = par[(std::size_t)oi];
        nd.parent = p < 0 ? 0 : nodeOf[(std::size_t)p];
        Mat4 local = world[(std::size_t)oi];
        if (p >= 0) { if (detail::rc::Singular3(world[(std::size_t)p])) ++singularParents; else local = world[(std::size_t)p].Inverse() * world[(std::size_t)oi]; }   // a zero-scale parent cannot be undone: the child keeps its world matrix
        if (!local.Decompose(nd.position, nd.rotation, nd.scale)) ++nonAffine;
        if (nd.scale.x < 0.0f) ++mirrored;
        nodeOf[(std::size_t)oi] = (int)out.nodes.size();
        out.nodes.push_back(std::move(nd));
    }

    // ---- materials: (object id, material number) -> index ----
    std::map<std::pair<long long, int>, int> matOf;
    std::vector<double> matDeg;
    int texRefused = 0, texReduced = 0;
    std::unordered_map<std::string, std::pair<std::string, int>> texCache;
    for (const CMat& m : P.mats) {
        ImportedMaterial im;
        im.name = "mat" + std::to_string(m.index);
        im.hasDiffuse = true;
        im.diffuse = Color{std::max(0.0f, std::min(1.0f, m.rgb[0])), std::max(0.0f, std::min(1.0f, m.rgb[1])), std::max(0.0f, std::min(1.0f, m.rgb[2])), std::max(0.0f, std::min(1.0f, m.alpha))};
        if (!m.texture.empty()) {
            // many materials name the same few files: each distinct name is resolved (a disk lookup) once
            auto it = texCache.find(m.texture);
            if (it == texCache.end()) {
                std::string joined, why, res;
                int kind = 0;   // 0 refused, 1 resolved, 2 resolved after dropping the folder part
                if (texCache.size() >= 1000) {}   // more than 1,000 distinct texture names: the rest are not looked up (each lookup is a disk access)
                else if (detail::ResolveInsideDir(baseDir, m.texture, joined, why)) { res = joined; kind = 1; }
                else {   // a full path from the artist's machine: only the file name is tried, inside the model's folder
                    std::string f = m.texture;
                    const std::size_t sl = f.find_last_of("/\\");
                    if (sl != std::string::npos) f = f.substr(sl + 1);
                    if (!f.empty() && f != m.texture && detail::ResolveInsideDir(baseDir, f, joined, why)) { res = joined; kind = 2; }
                }
                it = texCache.emplace(m.texture, std::make_pair(res, kind)).first;
            }
            im.texture = it->second.first;
            if (it->second.second == 2) ++texReduced; else if (it->second.second == 0) ++texRefused;
        }
        matOf[{m.objId, m.index}] = (int)out.materials.size();
        matDeg.push_back(m.facet == 'f' ? 0.0 : m.facet == 's' ? 180.0 : (double)m.angle);   // the smoothing: faceted = flat, smooth = fully smooth, auto N = N degrees
        out.materials.push_back(std::move(im));
    }
    // ---- geometry ----
    int badCorners = 0, badUv = 0, badMat = 0, fewCorners = 0, flagged = 0, degenerate = 0, noFaceObjects = 0;
    std::size_t tris = 0;
    std::vector<std::int32_t> remap;
    for (std::size_t oi = 0; oi < no; ++oi) {
        const Obj& o = P.objs[oi];
        if (!o.poly) continue;
        if (o.f.empty() || o.v.empty()) { ++noFaceObjects; continue; }
        std::vector<Vec3> lv(o.v.size());
        for (std::size_t i = 0; i < o.v.size(); ++i) lv[i] = Vec3{o.v[i].x, o.v[i].z, o.v[i].y};   // y and z swapped
        const bool haveUv = !o.uv.empty();
        std::map<int, std::vector<std::uint32_t>> byMat;
        std::vector<std::vector<std::array<int, 3>>> faceTris(o.f.size());   // kept per face only while building this object
        std::vector<Vec3> poly;
        std::vector<std::array<int, 3>> t3;
        for (std::size_t fi = 0; fi < o.f.size(); ++fi) {
            const Face& f = o.f[fi];
            if (f.n < 3) { ++fewCorners; continue; }
            bool ok = true;
            poly.clear();
            for (std::uint16_t k = 0; k < f.n; ++k) {
                const Corner& c = o.c[f.first + k];
                if (c.v < 0 || (std::size_t)c.v >= lv.size()) { ok = false; break; }
                poly.push_back(lv[(std::size_t)c.v]);
            }
            if (!ok) { ++badCorners; continue; }
            if (f.flags) ++flagged;
            detail::TriangulatePolygon(poly, t3, kMaxEarCorners);   // ear clipping is cubic: only small polygons get it, the rest are fanned
            if (t3.size() > kMaxTris - tris) return Fail(err, "too many triangles (more than " + std::to_string(kMaxTris) + " in the file)");
            tris += t3.size();
            faceTris[fi] = t3;
            int key = -1;
            auto it = matOf.find({o.id, (int)f.mat});
            if (it != matOf.end()) key = it->second; else ++badMat;
            byMat[key].push_back((std::uint32_t)fi);
        }
        remap.assign(lv.size(), -1);
        for (auto& kv : byMat) {
            detail::rc::Soup soup;
            std::vector<std::int32_t> usedPos;
            for (std::uint32_t fi : kv.second) {
                const Face& f = o.f[fi];
                for (const std::array<int, 3>& t : faceTris[fi]) {
                    for (int k = 0; k < 3; ++k) {
                        const Corner& c = o.c[f.first + (std::uint32_t)t[(std::size_t)k]];
                        std::int32_t& r = remap[(std::size_t)c.v];
                        if (r < 0) { r = (std::int32_t)soup.pos.size(); soup.pos.push_back(lv[(std::size_t)c.v]); usedPos.push_back(c.v); }
                        soup.ci.push_back((std::uint32_t)r);
                        if (haveUv) {
                            Vec2 uv{0, 0};
                            if (c.uv >= 0 && (std::size_t)c.uv < o.uv.size()) uv = Vec2{o.uv[(std::size_t)c.uv].x, 1.0f - o.uv[(std::size_t)c.uv].y};   // trueSpace's v runs up: flipped to the engine's top-left origin
                            else ++badUv;
                            soup.cuv.push_back(uv);
                        }
                    }
                }
            }
            for (std::int32_t v : usedPos) remap[(std::size_t)v] = -1;
            ImportedPart part;
            detail::rc::BuildStats st;
            const double deg = kv.first >= 0 ? matDeg[(std::size_t)kv.first] : 40.0;
            if (!detail::rc::BuildMesh(soup, deg, part.mesh, &st, kMaxTris * 3)) return Fail(err, "too many vertices after welding");
            degenerate += st.degenerate;
            if (part.mesh.triangles.empty()) continue;
            part.mesh.name = o.name;
            part.node = nodeOf[oi];
            part.material = kv.first;
            out.parts.push_back(std::move(part));
        }
        if (matOf.empty()) ++objectsWithoutMaterial;
    }
    if (out.parts.empty()) return Fail(err, "the file has no polygon object with faces");

    auto note = [&](const std::string& s) { out.notes.push_back("COB: " + s); };
    note(std::string(kind == 'B' ? "binary" : "ASCII") + " file; trueSpace is left-handed and Z-up: y and z were swapped (a reflection, so the winding is kept) and each object's stored world matrix was made relative to its parent; units are kept as written");
    note("texture coordinates: v was flipped (1 - v), assuming trueSpace's v runs up; unverified against trueSpace");
    if (P.unitChunks) note("the unit setting is not applied");
    for (const auto& kv : P.skipped) note(std::to_string(kv.second) + " '" + kv.first + "' chunk(s) not imported (animation, bones, characters, cameras, lights, splines and the like are not read)");
    if (P.badShaders) note(std::to_string(P.badShaders) + " material or shader block(s) could not be understood and were left out or read only in part");
    if (!P.mats.empty()) note("material colour and alpha and the texture file of a 'texture map' shader are read; ambient, specular, exponent, index of refraction, texture repeat and offset, transparency, reflectance and displacement shaders are not");
    if (loops) note(std::to_string(loops) + " object parent loop(s) were cut");
    if (dupNames) note(std::to_string(dupNames) + " object name(s) are repeated: the later ones got a numeric suffix");
    if (nonAffine) note(std::to_string(nonAffine) + " object matrix(es) could not be split into position, rotation and scale cleanly");
    if (singularParents) note(std::to_string(singularParents) + " object(s) under a parent whose matrix has a zero scale: the stored world matrix was used as the local one (the position is wrong if the parent really moves it)");
    if (mirrored) note(std::to_string(mirrored) + " object(s) with a mirroring matrix (negative scale): the winding of their faces is not corrected");
    if (noFaceObjects) note(std::to_string(noFaceObjects) + " polygon object(s) without faces or vertices skipped");
    if (flagged) note(std::to_string(flagged) + " face(s) with non-zero flags were read as ordinary polygons (hole flags are not interpreted)");
    if (fewCorners) note(std::to_string(fewCorners) + " face(s) with fewer than 3 corners skipped");
    if (badCorners) note(std::to_string(badCorners) + " face(s) naming a vertex that does not exist skipped");
    if (badUv) note(std::to_string(badUv) + " face corner(s) naming a texture vertex that does not exist got (0, 1)");
    if (badMat) note(std::to_string(badMat) + " face(s) use a material number the object does not define and have no material");
    if (degenerate) note(std::to_string(degenerate) + " degenerate triangle(s) skipped");
    if (texReduced) note(std::to_string(texReduced) + " texture path(s) with a folder part were reduced to the file name inside the model's folder");
    if (texRefused) note(std::to_string(texRefused) + " texture path(s) refused (a model may only name files inside its own folder)");
    if (objectsWithoutMaterial) note(std::to_string(objectsWithoutMaterial) + " object(s) have no material: smoothing angle 40 degrees");
    return true;
}

bool ReadCobBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    const bool ok = ReadCobImpl(bytes, baseDir, out, err);
    if (!ok) { out = ImportedModel{}; return false; }
    out.backend = "own";
    return true;
}

bool ReadCobFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, cob::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    const std::size_t sl = path.find_last_of("/\\");
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    const bool ok = ReadCobImpl(bytes, dir, out, err, &bytes);   // as ReadCobBuffer, freeing the file once it is parsed
    if (!ok) { out = ImportedModel{}; return false; }
    out.backend = "own";
    return true;
}

} // namespace okay
