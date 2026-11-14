// Our own AC3D (.ac, .acc, .ac3d) reader. Written from the public AC3D file format description (the "AC3Db" text format: a
// header, MATERIAL lines, a tree of OBJECT blocks with vertices and SURF lists) and from reading the files the tool writes;
// no code from any other importer. One part per (object, material, shading) group; polygons are triangulated; triangle
// strips (surface type 4, written by the "acc" files of newer AC3D versions) are unrolled; per-vertex normals are read when
// a vertex line carries them, otherwise smooth normals are made from the surfaces flagged "shaded". Bounds-checked, capped,
// no recursion. See okay/Render/ModelData.hpp and docs/own_ac3d_reader.md.
#include "PolygonTriangulate.hpp"
#include "ReaderCommon.hpp"
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <array>
#include <map>
#include <unordered_map>
#include <string>
#include <vector>

namespace okay {

namespace {
namespace ac {

constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;
constexpr long kMaxVertsPerObject = 5000000, kMaxSurfsPerObject = 5000000, kMaxRefsPerSurf = 1000000;
constexpr std::size_t kMaxCorners = 40000000, kMaxObjects = 1000000, kMaxMaterials = 100000, kMaxDepth = 512;

bool Fail(std::string* err, const std::string& m) { if (err) *err = "AC3D: " + m; return false; }

struct Obj {
    std::string type, name, texture;
    double texrep[2] = {1, 1}, texoff[2] = {0, 0};
    double rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, loc[3] = {0, 0, 0};
    double crease = 45.0;
    bool hasRot = false;
    long subdiv = 0;
    std::vector<Vec3> pos;
    std::vector<Vec3> nrm;   // empty, or one per vertex
    struct Surf { unsigned flags = 0; int mat = 0; std::vector<std::uint32_t> idx; std::vector<Vec2> uv; };
    std::vector<Surf> surfs;
    int node = 0;
};

// Quaternion of the rotation part of a 3x3 (rows given as in the file); the scale is taken from the column lengths.
void MatrixToTrs(const double m[9], Quat& q, Vec3& s, bool& sheared) {
    // column j of the matrix is (m[j], m[3+j], m[6+j]); p' = M p
    double c[3][3], len[3];
    for (int j = 0; j < 3; ++j) {
        c[j][0] = m[j]; c[j][1] = m[3 + j]; c[j][2] = m[6 + j];
        len[j] = std::sqrt(c[j][0] * c[j][0] + c[j][1] * c[j][1] + c[j][2] * c[j][2]);
        if (!(len[j] > 1e-12)) len[j] = 1.0;
        for (int k = 0; k < 3; ++k) c[j][k] /= len[j];
    }
    // right-handed check: x cross y vs z
    double cx = c[0][1] * c[1][2] - c[0][2] * c[1][1], cy = c[0][2] * c[1][0] - c[0][0] * c[1][2], cz = c[0][0] * c[1][1] - c[0][1] * c[1][0];
    const double handed = cx * c[2][0] + cy * c[2][1] + cz * c[2][2];
    double sx = len[0], sy = len[1], sz = len[2];
    if (handed < 0) { sz = -sz; for (int k = 0; k < 3; ++k) c[2][k] = -c[2][k]; }
    const double d01 = c[0][0] * c[1][0] + c[0][1] * c[1][1] + c[0][2] * c[1][2];
    const double d02 = c[0][0] * c[2][0] + c[0][1] * c[2][1] + c[0][2] * c[2][2];
    sheared = std::fabs(d01) > 1e-3 || std::fabs(d02) > 1e-3;
    // rotation matrix R with columns c0, c1, c2: R(row,col) = c[col][row]
    const double r00 = c[0][0], r11 = c[1][1], r22 = c[2][2], r21 = c[1][2], r12 = c[2][1], r02 = c[2][0], r20 = c[0][2], r10 = c[0][1], r01 = c[1][0];
    double w, x, y, z;
    const double tr = r00 + r11 + r22;
    if (tr > 0) { const double S = std::sqrt(tr + 1.0) * 2; w = 0.25 * S; x = (r21 - r12) / S; y = (r02 - r20) / S; z = (r10 - r01) / S; }
    else if (r00 > r11 && r00 > r22) { const double S = std::sqrt(1.0 + r00 - r11 - r22) * 2; w = (r21 - r12) / S; x = 0.25 * S; y = (r01 + r10) / S; z = (r02 + r20) / S; }
    else if (r11 > r22) { const double S = std::sqrt(1.0 + r11 - r00 - r22) * 2; w = (r02 - r20) / S; x = (r01 + r10) / S; y = 0.25 * S; z = (r12 + r21) / S; }
    else { const double S = std::sqrt(1.0 + r22 - r00 - r11) * 2; w = (r10 - r01) / S; x = (r02 + r20) / S; y = (r12 + r21) / S; z = 0.25 * S; }
    q = Quat{(float)x, (float)y, (float)z, (float)w}.Normalized();
    s = Vec3{(float)sx, (float)sy, (float)sz};
}

bool Hex(const std::string& t, unsigned& v) {
    std::size_t i = 0;
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) i = 2;
    if (i >= t.size() || t.size() - i > 8) return false;
    unsigned r = 0;
    for (; i < t.size(); ++i) {
        const char c = t[i];
        const int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (d < 0) return false;
        r = (r << 4) | (unsigned)d;
    }
    v = r;
    return true;
}


struct OTri { std::uint32_t p[3] = {0, 0, 0}; Vec2 uv[3] = {{0, 0}, {0, 0}, {0, 0}}; int mat = 0; bool smooth = false; };
constexpr std::size_t kMaxSubdivQuads = 1000000;

// The polygons of one object: corner position indices and texture coordinates, flat, with the start of each face.
struct Faces {
    std::vector<std::uint32_t> p;     // corner position index
    std::vector<Vec2> uv;             // corner texture coordinate
    std::vector<std::uint32_t> start; // face i is corners [start[i], start[i+1])
    std::vector<int> mat;
    std::vector<char> smooth;
    std::size_t Count() const { return mat.size(); }
    void Add(const std::uint32_t* idx, const Vec2* tc, std::size_t k, int m, bool sm) {
        if (start.empty()) start.push_back(0);
        for (std::size_t i = 0; i < k; ++i) { p.push_back(idx[i]); uv.push_back(tc[i]); }
        start.push_back((std::uint32_t)p.size());
        mat.push_back(m); smooth.push_back(sm ? 1 : 0);
    }
};

// Catmull-Clark subdivision (E. Catmull and J. Clark, "Recursively generated B-spline surfaces on arbitrary topological
// meshes", 1978) applied `levels` times to a polygon mesh: every face becomes one quad per corner (corner, next edge point, face
// point, previous edge point); a face point is the mean of the face's corners, an edge point the mean of its two ends and the
// two face points beside it (the midpoint on a boundary edge), an old vertex (F + 2R + (n-3)P) / n with F the mean of its face
// points, R the mean of its edge midpoints and n its valence (boundary: 3/4 P and 1/8 of each of the two boundary neighbours;
// a corner of the boundary stays). Corner texture coordinates are subdivided the same way inside each face, so seams stay where
// they are. Stops before a level that would pass `maxQuads`; returns the levels done.
long CatmullClark(std::vector<Vec3>& pos, Faces& F, long levels, std::size_t maxQuads) {
    long done = 0;
    for (; done < levels; ++done) {
        const std::size_t nf = F.Count(), nc = F.p.size();
        if (nc > maxQuads) break;
        const std::size_t nv = pos.size();
        // edges
        struct Edge { std::uint32_t a, b; int faces = 0; std::uint32_t f[2] = {0, 0}; };
        std::unordered_map<std::uint64_t, std::uint32_t> emap;
        std::vector<Edge> edges;
        std::vector<std::uint32_t> cornerEdge(nc);   // edge starting at each corner
        for (std::size_t f = 0; f < nf; ++f) {
            const std::uint32_t s0 = F.start[f], s1 = F.start[f + 1];
            for (std::uint32_t c = s0; c < s1; ++c) {
                const std::uint32_t a = F.p[c], b = F.p[c + 1 < s1 ? c + 1 : s0];
                const std::uint32_t lo = std::min(a, b), hi = std::max(a, b);
                auto r = emap.emplace(((std::uint64_t)lo << 32) | hi, (std::uint32_t)edges.size());
                if (r.second) { Edge e; e.a = lo; e.b = hi; edges.push_back(e); }
                Edge& e = edges[r.first->second];
                if (e.faces < 2) e.f[e.faces] = (std::uint32_t)f;
                ++e.faces;
                cornerEdge[c] = r.first->second;
            }
        }
        const std::size_t ne = edges.size();
        // face points
        std::vector<Vec3> fp(nf);
        std::vector<Vec2> fuv(nf);
        for (std::size_t f = 0; f < nf; ++f) {
            double x = 0, y = 0, z = 0, u = 0, v = 0;
            const std::uint32_t s0 = F.start[f], s1 = F.start[f + 1];
            for (std::uint32_t c = s0; c < s1; ++c) { x += pos[F.p[c]].x; y += pos[F.p[c]].y; z += pos[F.p[c]].z; u += F.uv[c].x; v += F.uv[c].y; }
            const double k = (double)(s1 - s0);
            fp[f] = Vec3{(float)(x / k), (float)(y / k), (float)(z / k)};
            fuv[f] = Vec2{(float)(u / k), (float)(v / k)};
        }
        // edge points, and the per-vertex sums for the vertex rule
        std::vector<Vec3> ep(ne);
        std::vector<double> fsum(nv * 3, 0.0), rsum(nv * 3, 0.0), bsum(nv * 3, 0.0);
        std::vector<int> nfaces(nv, 0), nedges(nv, 0), nbound(nv, 0);
        for (std::size_t e = 0; e < ne; ++e) {
            const Edge& ed = edges[e];
            const Vec3 &A = pos[ed.a], &B = pos[ed.b];
            if (ed.faces == 2) ep[e] = Vec3{(A.x + B.x + fp[ed.f[0]].x + fp[ed.f[1]].x) / 4, (A.y + B.y + fp[ed.f[0]].y + fp[ed.f[1]].y) / 4, (A.z + B.z + fp[ed.f[0]].z + fp[ed.f[1]].z) / 4};
            else ep[e] = Vec3{(A.x + B.x) / 2, (A.y + B.y) / 2, (A.z + B.z) / 2};
            const Vec3 mid{(A.x + B.x) / 2, (A.y + B.y) / 2, (A.z + B.z) / 2};
            for (int s = 0; s < 2; ++s) {
                const std::uint32_t v = s ? ed.b : ed.a;
                const Vec3& o = s ? A : B;   // the other end
                ++nedges[v];
                rsum[v * 3] += mid.x; rsum[v * 3 + 1] += mid.y; rsum[v * 3 + 2] += mid.z;
                if (ed.faces != 2) { ++nbound[v]; bsum[v * 3] += o.x; bsum[v * 3 + 1] += o.y; bsum[v * 3 + 2] += o.z; }
                if (ed.a == ed.b) break;   // a collapsed edge counts once
            }
        }
        for (std::size_t f = 0; f < nf; ++f)
            for (std::uint32_t c = F.start[f]; c < F.start[f + 1]; ++c) {
                const std::uint32_t v = F.p[c];
                ++nfaces[v];
                fsum[v * 3] += fp[f].x; fsum[v * 3 + 1] += fp[f].y; fsum[v * 3 + 2] += fp[f].z;
            }
        std::vector<Vec3> np(nv + ne + nf);
        for (std::size_t v = 0; v < nv; ++v) {
            const Vec3 P = pos[v];
            if (nedges[v] == 0) { np[v] = P; continue; }
            if (nbound[v] > 0) {
                if (nbound[v] == 2) np[v] = Vec3{(float)(0.75 * P.x + 0.125 * bsum[v * 3]), (float)(0.75 * P.y + 0.125 * bsum[v * 3 + 1]), (float)(0.75 * P.z + 0.125 * bsum[v * 3 + 2])};
                else np[v] = P;
                continue;
            }
            const double n = nedges[v], nfc = nfaces[v] > 0 ? nfaces[v] : 1;
            np[v] = Vec3{(float)((fsum[v * 3] / nfc + 2 * rsum[v * 3] / n + (n - 3) * P.x) / n), (float)((fsum[v * 3 + 1] / nfc + 2 * rsum[v * 3 + 1] / n + (n - 3) * P.y) / n),
                         (float)((fsum[v * 3 + 2] / nfc + 2 * rsum[v * 3 + 2] / n + (n - 3) * P.z) / n)};
        }
        for (std::size_t e = 0; e < ne; ++e) np[nv + e] = ep[e];
        for (std::size_t f = 0; f < nf; ++f) np[nv + ne + f] = fp[f];
        // new faces: one quad per corner
        Faces G;
        G.p.reserve(nc * 4); G.uv.reserve(nc * 4); G.start.reserve(nc + 1); G.mat.reserve(nc); G.smooth.reserve(nc);
        G.start.push_back(0);
        for (std::size_t f = 0; f < nf; ++f) {
            const std::uint32_t s0 = F.start[f], s1 = F.start[f + 1];
            for (std::uint32_t c = s0; c < s1; ++c) {
                const std::uint32_t cn = c + 1 < s1 ? c + 1 : s0, cp = c > s0 ? c - 1 : s1 - 1;
                const std::uint32_t q[4] = {F.p[c], (std::uint32_t)(nv + cornerEdge[c]), (std::uint32_t)(nv + ne + f), (std::uint32_t)(nv + cornerEdge[cp])};
                auto mid2 = [](const Vec2& a, const Vec2& b) { return Vec2{(a.x + b.x) / 2, (a.y + b.y) / 2}; };
                const Vec2 t[4] = {F.uv[c], mid2(F.uv[c], F.uv[cn]), fuv[f], mid2(F.uv[cp], F.uv[c])};
                G.Add(q, t, 4, F.mat[f], F.smooth[f] != 0);
            }
        }
        pos.swap(np);
        F = std::move(G);
    }
    return done;
}

} // namespace ac
} // namespace

static bool ReadAc3dImpl(const std::string& input, const std::string& baseDir, ImportedModel& out, std::string* err) {
    using namespace ac;
    std::string text = input;
    bool converted = false;
    std::string why;
    if (!detail::txt::NormalizeEncoding(text, converted, why, kMaxFileBytes)) return Fail(err, why);
    detail::txt::Lines lines(text);
    std::string line;
    if (!lines.Next(line) || line.size() < 5 || line.compare(0, 4, "AC3D") != 0) return Fail(err, "not an AC3D file (the first line must start with AC3D)");
    {
        const char v = line[4];
        if (!((v >= '0' && v <= '9') || (v >= 'a' && v <= 'f') || (v >= 'A' && v <= 'F'))) return Fail(err, "not an AC3D file (no version digit after AC3D)");
    }

    struct Mat { std::string name; Color diffuse{1, 1, 1, 1}, emissive{0, 0, 0, 1}; float shininess = -1; bool hasEmissive = false; };
    std::vector<Mat> mats;
    std::vector<Obj> objs;
    std::vector<int> parent;   // per object: index of the parent object or -1
    struct Frame { int obj; long remaining; };
    std::vector<Frame> stack;
    int lineSurfs = 0, badRefs = 0, shortSurfs = 0, unknownKeys = 0, extraLayers = 0, subdivs = 0, lights = 0, shearedNodes = 0, rotNodes = 0;
    std::string firstUnknown;
    int cur = -1;   // object whose properties are being read

    auto nextLine = [&](std::vector<std::string>& tok) -> bool {
        while (lines.Next(line)) {
            tok = detail::txt::Split(line);
            if (!tok.empty()) return true;
        }
        return false;
    };
    auto num = [&](const std::string& s, double& v) { return detail::txt::ToDouble(s, v); };
    auto count = [&](const std::vector<std::string>& t, long cap, long& v, const char* what) -> bool {
        long long x;
        if (t.size() < 2) { Fail(err, std::string("no ") + what + " count on line " + std::to_string(lines.number)); return false; }
        if (!detail::txt::ToInt(t[1], x) || x < 0 || x > cap) { Fail(err, std::string("bad ") + what + " count '" + t[1] + "' on line " + std::to_string(lines.number)); return false; }
        v = (long)x;
        return true;
    };

    std::vector<std::string> tok;
    while (nextLine(tok)) {
        const std::string& k = tok[0];
        if (k == "MATERIAL") {
            if (mats.size() >= kMaxMaterials) return Fail(err, "too many materials");
            Mat m;
            if (tok.size() >= 2) m.name = tok[1];
            for (std::size_t i = 2; i < tok.size(); ++i) {
                double a, b, c;
                auto three = [&](double& x, double& y, double& z) { return i + 3 < tok.size() + 0 && num(tok[i + 1], x) && num(tok[i + 2], y) && num(tok[i + 3], z); };
                if (tok[i] == "rgb") { if (!three(a, b, c)) return Fail(err, "bad rgb on line " + std::to_string(lines.number)); m.diffuse.r = (float)a; m.diffuse.g = (float)b; m.diffuse.b = (float)c; i += 3; }
                else if (tok[i] == "emis") { if (!three(a, b, c)) return Fail(err, "bad emis on line " + std::to_string(lines.number)); m.emissive = Color{(float)a, (float)b, (float)c, 1}; m.hasEmissive = a + b + c > 0; i += 3; }
                else if (tok[i] == "amb" || tok[i] == "spec") { if (!three(a, b, c)) return Fail(err, "bad " + tok[i] + " on line " + std::to_string(lines.number)); i += 3; }
                else if (tok[i] == "shi") { if (i + 1 >= tok.size() || !num(tok[i + 1], a)) return Fail(err, "bad shi on line " + std::to_string(lines.number)); m.shininess = (float)std::max(0.0, a); ++i; }
                else if (tok[i] == "trans") { if (i + 1 >= tok.size() || !num(tok[i + 1], a)) return Fail(err, "bad trans on line " + std::to_string(lines.number)); m.diffuse.a = (float)std::max(0.0, std::min(1.0, 1.0 - a)); ++i; }
            }
            m.diffuse.r = std::max(0.0f, std::min(1.0f, m.diffuse.r)); m.diffuse.g = std::max(0.0f, std::min(1.0f, m.diffuse.g)); m.diffuse.b = std::max(0.0f, std::min(1.0f, m.diffuse.b));
            mats.push_back(m);
        } else if (k == "OBJECT") {
            if (objs.size() >= kMaxObjects) return Fail(err, "too many objects");
            while (!stack.empty() && stack.back().remaining == 0) stack.pop_back();
            if (stack.size() >= kMaxDepth) return Fail(err, "objects nested deeper than " + std::to_string(kMaxDepth));
            Obj o;
            o.type = tok.size() >= 2 ? detail::txt::Lower(tok[1]) : "poly";
            objs.push_back(std::move(o));
            cur = (int)objs.size() - 1;
            if (!stack.empty()) { parent.push_back(stack.back().obj); --stack.back().remaining; }
            else parent.push_back(-1);
        } else if (cur < 0) {
            continue;   // anything before the first OBJECT other than MATERIAL is ignored
        } else {
            Obj& o = objs[(std::size_t)cur];
            if (k == "name") { if (tok.size() >= 2) o.name = tok[1]; }
            else if (k == "texture") {
                if (tok.size() >= 2) {
                    const std::string layer = tok.size() >= 3 ? tok.back() : std::string();
                    const bool other = layer == "tiled" || layer == "skids" || layer == "shad";
                    if (other) { if (tok[1] != "empty_texture_no_mapping" && !tok[1].empty()) ++extraLayers; }
                    else o.texture = tok[1];
                }
            }
            else if (k == "texrep" || k == "texoff") {
                double a, b;
                if (tok.size() < 3 || !num(tok[1], a) || !num(tok[2], b)) return Fail(err, "bad " + k + " on line " + std::to_string(lines.number));
                if (k == "texrep") { o.texrep[0] = a; o.texrep[1] = b; } else { o.texoff[0] = a; o.texoff[1] = b; }
            }
            else if (k == "rot") {
                if (tok.size() < 10) return Fail(err, "bad rot on line " + std::to_string(lines.number));
                for (int i = 0; i < 9; ++i) if (!num(tok[(std::size_t)i + 1], o.rot[i])) return Fail(err, "bad rot on line " + std::to_string(lines.number));
                o.hasRot = true;
            }
            else if (k == "loc") {
                if (tok.size() < 4) return Fail(err, "bad loc on line " + std::to_string(lines.number));
                for (int i = 0; i < 3; ++i) if (!num(tok[(std::size_t)i + 1], o.loc[i])) return Fail(err, "bad loc on line " + std::to_string(lines.number));
            }
            else if (k == "crease") { double a; if (tok.size() >= 2 && num(tok[1], a)) o.crease = std::max(0.0, std::min(180.0, a)); }
            else if (k == "subdiv") { long long a; if (tok.size() >= 2 && detail::txt::ToInt(tok[1], a) && a > 0) { o.subdiv = (long)a; } }
            else if (k == "data") {
                long n;
                if (!count(tok, 1000000000L, n, "data")) return false;
                // `n` characters follow on the next line(s); skip them
                long left = n;
                std::string d;
                while (left > 0 && lines.Next(d)) left -= (long)d.size() + 1;
            }
            else if (k == "numvert") {
                long n;
                if (!count(tok, kMaxVertsPerObject, n, "numvert")) return false;
                o.pos.reserve((std::size_t)std::min<long>(n, (long)(text.size() / 6 + 1)));
                std::vector<std::string> t;
                bool anyN = false;
                for (long i = 0; i < n; ++i) {
                    if (!nextLine(t)) return Fail(err, "the file ends after " + std::to_string(i) + " of " + std::to_string(n) + " vertices");
                    double f[6];
                    if (t.size() < 3) return Fail(err, "vertex " + std::to_string(i) + " has fewer than 3 numbers");
                    for (int j = 0; j < 3; ++j) if (!num(t[(std::size_t)j], f[j])) return Fail(err, "vertex " + std::to_string(i) + " has a bad number");
                    o.pos.push_back(Vec3{(float)f[0], (float)f[1], (float)f[2]});
                    if (t.size() >= 6) {
                        bool ok = true;
                        for (int j = 3; j < 6; ++j) ok = ok && num(t[(std::size_t)j], f[j]);
                        if (ok) { o.nrm.resize(o.pos.size() - 1, Vec3{0, 0, 0}); o.nrm.push_back(Vec3{(float)f[3], (float)f[4], (float)f[5]}); anyN = true; continue; }
                    }
                    if (anyN) o.nrm.push_back(Vec3{0, 0, 0});
                }
                if (!anyN) o.nrm.clear();
            }
            else if (k == "numsurf") {
                long n;
                if (!count(tok, kMaxSurfsPerObject, n, "numsurf")) return false;
                std::vector<std::string> t;
                std::size_t corners = 0;
                for (long i = 0; i < n; ++i) {
                    if (!nextLine(t) || t[0] != "SURF") return Fail(err, "expected SURF " + std::to_string(i + 1) + " of " + std::to_string(n) + " near line " + std::to_string(lines.number));
                    Obj::Surf s;
                    if (t.size() < 2 || !Hex(t[1], s.flags)) return Fail(err, "bad SURF flags on line " + std::to_string(lines.number));
                    long refs = -1;
                    for (;;) {   // mat and refs (in any order, other lines skipped) up to refs
                        if (!nextLine(t)) return Fail(err, "the file ends inside a SURF");
                        if (t[0] == "mat") { long long m; if (t.size() < 2 || !detail::txt::ToInt(t[1], m)) return Fail(err, "bad mat on line " + std::to_string(lines.number)); s.mat = (int)std::max<long long>(-1, std::min<long long>(m, 2147483647)); }
                        else if (t[0] == "refs") { if (!count(t, kMaxRefsPerSurf, refs, "refs")) return false; break; }
                        else if (t[0] == "SURF" || t[0] == "kids" || t[0] == "OBJECT") return Fail(err, "a SURF has no refs line (line " + std::to_string(lines.number) + ")");
                    }
                    corners += (std::size_t)refs;
                    if (corners > kMaxCorners) return Fail(err, "too many surface corners");
                    s.idx.reserve((std::size_t)std::min<long>(refs, (long)(text.size() / 4 + 1)));
                    for (long r = 0; r < refs; ++r) {
                        if (!nextLine(t)) return Fail(err, "the file ends after " + std::to_string(r) + " of " + std::to_string(refs) + " refs");
                        long long ix;
                        if (!detail::txt::ToInt(t[0], ix) || ix < 0) return Fail(err, "bad vertex index '" + t[0] + "' on line " + std::to_string(lines.number));
                        double u = 0, v = 0;
                        if (t.size() >= 3 && (!num(t[1], u) || !num(t[2], v))) return Fail(err, "bad texture coordinate on line " + std::to_string(lines.number));
                        s.idx.push_back((std::uint32_t)ix);
                        s.uv.push_back(Vec2{(float)u, (float)v});
                    }
                    o.surfs.push_back(std::move(s));
                }
            }
            else if (k == "kids") {
                long n;
                if (!count(tok, 100000000L, n, "kids")) return false;
                stack.push_back(Frame{cur, n});
                cur = -1;   // the properties of this object are done
            }
            else if (k == "url" || k == "folded" || k == "hidden" || k == "locked") {}
            else { ++unknownKeys; if (firstUnknown.empty()) firstUnknown = k; }
        }
    }
    if (objs.empty()) return Fail(err, "the file has no OBJECT");
    for (const Frame& f : stack) if (f.remaining > 0) { /* fewer kids than announced: tolerated */ (void)f; }

    // ---- nodes ----
    ImportedNode root;
    root.name = "AC3D";
    out.nodes.push_back(root);
    for (std::size_t i = 0; i < objs.size(); ++i) {
        Obj& o = objs[i];
        ImportedNode n;
        n.name = o.name.empty() ? o.type : o.name;
        n.parent = parent[i] >= 0 ? objs[(std::size_t)parent[i]].node : 0;
        n.position = Vec3{(float)o.loc[0], (float)o.loc[1], (float)o.loc[2]};
        if (o.hasRot) {
            bool sh = false;
            MatrixToTrs(o.rot, n.rotation, n.scale, sh);
            ++rotNodes;
            if (sh) ++shearedNodes;
        }
        if (o.type == "light") ++lights;
        o.node = (int)out.nodes.size();
        out.nodes.push_back(n);
    }

    // ---- parts ----
    std::map<std::pair<int, std::string>, int> matIndex;
    auto materialFor = [&](int m, const std::string& tex) -> int {
        const auto key = std::make_pair(m, tex);
        auto it = matIndex.find(key);
        if (it != matIndex.end()) return it->second;
        ImportedMaterial im;
        if (m >= 0 && (std::size_t)m < mats.size()) {
            const Mat& s = mats[(std::size_t)m];
            im.name = s.name;
            im.hasDiffuse = true; im.diffuse = s.diffuse;
            if (s.hasEmissive) { im.hasEmissive = true; im.emissive = s.emissive; }
            im.shininess = s.shininess;
        } else im.name = "default";
        if (!tex.empty()) {
            std::string t = tex;
            for (char& c : t) if (c == '\\') c = '/';
            if (t.compare(0, 2, "./") == 0) t.erase(0, 2);
            if (!baseDir.empty() && t[0] != '/' && t.find(':') == std::string::npos) t = baseDir + t;
            im.texture = t;
        }
        const int idx = (int)out.materials.size();
        out.materials.push_back(im);
        matIndex[key] = idx;
        return idx;
    };

    std::size_t outVerts = 0;
    int degenerate = 0, subdivCut = 0;
    std::vector<std::array<int, 3>> tri;
    std::vector<Vec3> poly;
    for (Obj& o : objs) {
        if (o.pos.empty() || o.surfs.empty()) continue;
        const std::uint32_t nv = (std::uint32_t)o.pos.size();
        // ---- every polygon of the object ----
        Faces faces;
        std::vector<std::uint32_t> pi;
        std::vector<Vec2> pu;
        auto addFace = [&](const Obj::Surf& s, const std::size_t* ord, std::size_t k, bool smooth) {
            pi.clear(); pu.clear();
            for (std::size_t j = 0; j < k; ++j) {
                pi.push_back(s.idx[ord[j]]);
                Vec2 uv{0, 0};
                if (!o.texture.empty()) {   // an untextured object's coordinates mean nothing (the files write zeros)
                    const Vec2 t = s.uv[ord[j]];
                    uv = Vec2{(float)(t.x * o.texrep[0] + o.texoff[0]), (float)(t.y * o.texrep[1] + o.texoff[1])};
                }
                pu.push_back(uv);
            }
            faces.Add(pi.data(), pu.data(), k, s.mat, smooth);
        };
        for (const Obj::Surf& s : o.surfs) {
            const unsigned type = s.flags & 0xF;
            if (type == 1 || type == 2) { ++lineSurfs; continue; }
            if (s.idx.size() < 3) { ++shortSurfs; continue; }
            bool bad = false;
            for (std::uint32_t ix : s.idx) if (ix >= nv) bad = true;
            if (bad) { ++badRefs; continue; }
            const bool smooth = (s.flags & 0x10) != 0 && o.nrm.empty();
            if (type == 4) {   // triangle strip: every second triangle is turned over to keep the winding
                for (std::size_t i = 0; i + 2 < s.idx.size(); ++i) {
                    const std::size_t ord[3] = {(i & 1) ? i + 1 : i, (i & 1) ? i : i + 1, i + 2};
                    addFace(s, ord, 3, smooth);
                }
            } else {
                std::vector<std::size_t> ord(s.idx.size());
                for (std::size_t j = 0; j < ord.size(); ++j) ord[j] = j;
                addFace(s, ord.data(), ord.size(), smooth);
            }
        }
        if (faces.Count() == 0) continue;
        std::vector<Vec3> pos = o.pos;
        bool useNrm = !o.nrm.empty();
        if (o.subdiv > 0) {
            ++subdivs;
            useNrm = false;   // the stored normals belong to the control mesh
            const long done = CatmullClark(pos, faces, std::min<long>(o.subdiv, 8), kMaxSubdivQuads);
            if (done < o.subdiv) ++subdivCut;
        }
        // ---- triangles ----
        std::vector<OTri> tris;
        for (std::size_t f = 0; f < faces.Count(); ++f) {
            const std::uint32_t s0 = faces.start[f], k = faces.start[f + 1] - s0;
            tri.clear();
            if (k == 3) tri.push_back({0, 1, 2});
            else if (k == 4) { tri.push_back({0, 1, 2}); tri.push_back({0, 2, 3}); }
            else {
                poly.clear();
                for (std::uint32_t j = 0; j < k; ++j) poly.push_back(pos[faces.p[s0 + j]]);
                detail::TriangulatePolygon(poly, tri);
            }
            for (const auto& t : tri) {
                OTri ot;
                ot.mat = faces.mat[f]; ot.smooth = faces.smooth[f] != 0;
                for (int c = 0; c < 3; ++c) { ot.p[c] = faces.p[s0 + (std::uint32_t)t[(std::size_t)c]]; ot.uv[c] = faces.uv[s0 + (std::uint32_t)t[(std::size_t)c]]; }
                tris.push_back(ot);
            }
        }
        // ---- group by (material, shading); only the vertices a group uses go into its position list ----
        struct Group { int mat; bool smooth; detail::rc::Soup soup; std::unordered_map<std::uint32_t, std::uint32_t> remap; };
        std::map<std::pair<int, int>, std::size_t> gi;
        std::vector<Group> groups;
        for (const OTri& t : tris) {
            const auto key = std::make_pair(t.mat, t.smooth ? 1 : 0);
            auto it = gi.find(key);
            if (it == gi.end()) { it = gi.emplace(key, groups.size()).first; groups.push_back(Group{t.mat, t.smooth, {}, {}}); }
            Group& g = groups[it->second];
            for (int c = 0; c < 3; ++c) {
                auto r = g.remap.emplace(t.p[c], (std::uint32_t)g.soup.pos.size());
                if (r.second) g.soup.pos.push_back(pos[t.p[c]]);
                g.soup.ci.push_back(r.first->second);
                if (!o.texture.empty()) g.soup.cuv.push_back(t.uv[c]);
                if (useNrm) g.soup.cnrm.push_back(o.nrm[t.p[c]]);
            }
        }
        for (Group& g : groups) {
            if (g.soup.ci.empty()) continue;
            ImportedPart part;
            detail::rc::BuildStats st;
            if (!detail::rc::BuildMesh(g.soup, g.smooth ? o.crease : 0.0, part.mesh, &st, 40000000 - std::min<std::size_t>(outVerts, 40000000))) return Fail(err, "too many output vertices");
            degenerate += st.degenerate;
            outVerts += part.mesh.vertices.size();
            if (part.mesh.triangles.empty()) continue;
            if (!detail::rc::PartInRange(part)) return Fail(err, "a vertex is out of range");
            part.mesh.name = o.name;
            part.node = o.node;
            part.material = materialFor(g.mat, o.texture);
            out.parts.push_back(std::move(part));
        }
    }
    if (out.parts.empty()) return Fail(err, "no polygons in the file (only lines, points or empty objects)");
    out.backend = "own";

    auto note = [&](const std::string& s) { out.notes.push_back("AC3D: " + s); };
    if (subdivs) note(std::to_string(subdivs) + " object(s) with subdivision (subdiv): Catmull-Clark subdivision applied to the polygons" + (subdivCut ? std::string("; ") + std::to_string(subdivCut) + " object(s) stopped early (more than 1,000,000 corners or 8 levels)" : std::string()) + "; stored per-vertex normals are replaced by smooth ones");
    if (lineSurfs) note(std::to_string(lineSurfs) + " line surface(s) (open or closed lines) skipped: the engine's meshes hold triangles only");
    if (lights) note(std::to_string(lights) + " light object(s) kept as empty nodes: lights are not imported");
    if (extraLayers) note(std::to_string(extraLayers) + " extra texture layer(s) (tiled, skids, shad) not imported");
    if (rotNodes) note("rot matrices are read as rows, p' = M p + loc; a shear in them is dropped" + (shearedNodes ? std::string(" (") + std::to_string(shearedNodes) + " object(s) had one)" : std::string()));
    if (unknownKeys) note(std::to_string(unknownKeys) + " unknown keyword line(s) skipped (first: " + firstUnknown + ")");
    if (shortSurfs) note(std::to_string(shortSurfs) + " surface(s) with fewer than 3 corners skipped");
    if (badRefs) note(std::to_string(badRefs) + " surface(s) with a vertex index past the vertex list skipped");
    if (degenerate) note(std::to_string(degenerate) + " degenerate triangle(s) skipped");
    note("texrep and texoff are applied to the texture coordinates as u * rep + off; the AC3D light, camera and url data are not read");
    return true;
}

bool ReadAc3dBuffer(const std::string& text, const std::string& baseDir, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    const bool ok = ReadAc3dImpl(text, baseDir, out, err);
    if (!ok) out = ImportedModel{};
    return ok;
}

bool ReadAc3dFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, ac::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    const std::size_t sl = path.find_last_of("/\\");
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    return ReadAc3dBuffer(bytes, dir, out, err);
}

} // namespace okay
