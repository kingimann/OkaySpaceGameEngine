// okay::fbx layer 2: element tree -> Scene (nodes, meshes, skins, blend shapes,
// materials, baked animation). See FbxReader.hpp and docs/own_fbx_reader.md.
// Written from the public description of the FBX object model; no third-party code.
#include "okay/Core/SafeCast.hpp"
#include "okay/Render/FbxReader.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace okay {
namespace fbx {

namespace {

const double kPi = 3.14159265358979323846;
const double kKTimePerSec = 46186158000.0;

// ---------------------------------------------------------------------------
// small matrix helpers (double, row-major, column vectors)
// ---------------------------------------------------------------------------
Mat Mul(const Mat& a, const Mat& b) {
    Mat r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            double s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[i * 4 + k] * b.m[k * 4 + j];
            r.m[i * 4 + j] = s;
        }
    return r;
}
Mat Translate(double x, double y, double z) { Mat r; r.m[3] = x; r.m[7] = y; r.m[11] = z; return r; }
Mat ScaleM(double x, double y, double z) { Mat r; r.m[0] = x; r.m[5] = y; r.m[10] = z; return r; }
Mat RotAxis(int axis, double deg) {
    const double a = deg * kPi / 180.0, c = std::cos(a), s = std::sin(a);
    Mat r;
    if (axis == 0)      { r.m[5] = c; r.m[6] = -s; r.m[9] = s; r.m[10] = c; }
    else if (axis == 1) { r.m[0] = c; r.m[2] = s; r.m[8] = -s; r.m[10] = c; }
    else                { r.m[0] = c; r.m[1] = -s; r.m[4] = s; r.m[5] = c; }
    return r;
}
/// FBX rotation orders: 0 XYZ 1 XZY 2 YZX 3 YXZ 4 ZXY 5 ZYX (the first letter is applied first).
Mat Euler(const double d[3], int order) {
    static const int seq[6][3] = {{0,1,2},{0,2,1},{1,2,0},{1,0,2},{2,0,1},{2,1,0}};
    if (order < 0 || order > 5) order = 0;
    Mat r;
    for (int k = 0; k < 3; ++k) r = Mul(RotAxis(seq[order][k], d[seq[order][k]]), r);
    return r;
}
bool Invert(const Mat& a, Mat& out) {
    // Gauss-Jordan with partial pivoting.
    double m[4][8];
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) { m[i][j] = a.m[i * 4 + j]; m[i][4 + j] = i == j; }
    for (int c = 0; c < 4; ++c) {
        int piv = c;
        for (int r = c + 1; r < 4; ++r) if (std::fabs(m[r][c]) > std::fabs(m[piv][c])) piv = r;
        if (std::fabs(m[piv][c]) < 1e-300) return false;
        if (piv != c) for (int j = 0; j < 8; ++j) std::swap(m[c][j], m[piv][j]);
        const double inv = 1.0 / m[c][c];
        for (int j = 0; j < 8; ++j) m[c][j] *= inv;
        for (int r = 0; r < 4; ++r) if (r != c) { const double f = m[r][c]; if (f != 0) for (int j = 0; j < 8; ++j) m[r][j] -= f * m[c][j]; }
    }
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) out.m[i * 4 + j] = m[i][4 + j];
    return true;
}
Mat InvertOrIdentity(const Mat& a) { Mat r; if (!Invert(a, r)) r = Mat(); return r; }

/// T * R * S pieces from a matrix (no shear handling). Rotation as a quaternion x y z w.
void Decompose(const Mat& a, double t[3], double q[4], double s[3]) {
    t[0] = a.m[3]; t[1] = a.m[7]; t[2] = a.m[11];
    double c[3][3];
    for (int j = 0; j < 3; ++j) {
        const double x = a.m[j], y = a.m[4 + j], z = a.m[8 + j];
        s[j] = std::sqrt(x * x + y * y + z * z);
        const double inv = s[j] > 1e-12 ? 1.0 / s[j] : 0.0;
        c[0][j] = x * inv; c[1][j] = y * inv; c[2][j] = z * inv;
    }
    const double det = c[0][0] * (c[1][1] * c[2][2] - c[1][2] * c[2][1]) - c[0][1] * (c[1][0] * c[2][2] - c[1][2] * c[2][0]) + c[0][2] * (c[1][0] * c[2][1] - c[1][1] * c[2][0]);
    if (det < 0) { s[0] = -s[0]; for (int i = 0; i < 3; ++i) c[i][0] = -c[i][0]; }
    for (int j = 0; j < 3; ++j) if (s[j] == 0) { /* collapsed axis: keep identity column */ c[0][j] = j == 0; c[1][j] = j == 1; c[2][j] = j == 2; }
    const double tr = c[0][0] + c[1][1] + c[2][2];
    if (tr > 0) {
        const double k = std::sqrt(tr + 1.0) * 2.0;
        q[3] = 0.25 * k; q[0] = (c[2][1] - c[1][2]) / k; q[1] = (c[0][2] - c[2][0]) / k; q[2] = (c[1][0] - c[0][1]) / k;
    } else if (c[0][0] > c[1][1] && c[0][0] > c[2][2]) {
        const double k = std::sqrt(1.0 + c[0][0] - c[1][1] - c[2][2]) * 2.0;
        q[3] = (c[2][1] - c[1][2]) / k; q[0] = 0.25 * k; q[1] = (c[0][1] + c[1][0]) / k; q[2] = (c[0][2] + c[2][0]) / k;
    } else if (c[1][1] > c[2][2]) {
        const double k = std::sqrt(1.0 + c[1][1] - c[0][0] - c[2][2]) * 2.0;
        q[3] = (c[0][2] - c[2][0]) / k; q[0] = (c[0][1] + c[1][0]) / k; q[1] = 0.25 * k; q[2] = (c[1][2] + c[2][1]) / k;
    } else {
        const double k = std::sqrt(1.0 + c[2][2] - c[0][0] - c[1][1]) * 2.0;
        q[3] = (c[1][0] - c[0][1]) / k; q[0] = (c[0][2] + c[2][0]) / k; q[1] = (c[1][2] + c[2][1]) / k; q[2] = 0.25 * k;
    }
    const double l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (l > 1e-12) for (int i = 0; i < 4; ++i) q[i] /= l; else { q[0] = q[1] = q[2] = 0; q[3] = 1; }
}

/// FBX stores a matrix as 16 doubles, column by column.
Mat FromFbx16(const std::vector<double>& a) {
    Mat r;
    if (a.size() < 16) return r;
    for (int c = 0; c < 4; ++c) for (int rr = 0; rr < 4; ++rr) r.m[rr * 4 + c] = a[(std::size_t)c * 4 + rr];
    return r;
}

// ---------------------------------------------------------------------------
// axis / unit conversion: p' = s * C * p, C a signed axis permutation
// ---------------------------------------------------------------------------
struct Conv {
    // Like ufbx's "modify geometry" conversion: only the ROOT nodes get the axis rotation (so every
    // node below keeps the local axes the file was authored in), and the unit scale multiplies
    // every translation, vertex and bind matrix.
    double C[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
    double s = 1.0;
    void Pt(const double in[3], double out[3]) const { for (int i = 0; i < 3; ++i) out[i] = in[i] * s; }
    /// Unit scale on an affine matrix: the translation is scaled, the linear part kept.
    Mat Scaled(const Mat& x) const { Mat r = x; r.m[3] *= s; r.m[7] *= s; r.m[11] *= s; return r; }
    Mat Axes() const {
        Mat r;
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i * 4 + j] = C[i][j];
        return r;
    }
    /// What a node's local matrix becomes: scaled, and rotated into Y-up if it hangs off the scene root.
    Mat Local(const Mat& raw, bool isRoot) const { return isRoot ? Mul(Axes(), Scaled(raw)) : Scaled(raw); }
};

// ---------------------------------------------------------------------------
// object database
// ---------------------------------------------------------------------------
typedef std::int64_t Id;

// Numbers from a file are doubles; ids go through the saturating cast in okay/Core/SafeCast.hpp (as do the other integer fields).
inline Id ToId(double v) { return (Id)SafeInt64(v); }

struct Props {
    std::unordered_map<std::string, std::vector<double>> num;
    std::unordered_map<std::string, std::string> str;
    bool Has(const char* n) const { return num.count(n) != 0; }
    double Get(const char* n, double def) const { auto it = num.find(n); return it == num.end() || it->second.empty() ? def : it->second[0]; }
    void Vec3(const char* n, double out[3], double def) const {
        auto it = num.find(n);
        for (int i = 0; i < 3; ++i) out[i] = (it != num.end() && it->second.size() > (std::size_t)i) ? it->second[(std::size_t)i] : def;
    }
};
void LoadProps(const Elem& e, Props& out) {
    const Elem* t = e.Find("Properties70");
    if (!t) return;
    for (const Elem& p : t->children) {
        if (p.name != "P" || p.props.empty() || p.props[0].type != 'S') continue;
        std::vector<double> v;
        for (std::size_t i = 4; i < p.props.size(); ++i) {
            if (p.props[i].IsNumber()) v.push_back(p.props[i].Num());
            else if (i == 4 && p.props[i].type == 'S') out.str[p.props[0].s] = p.props[i].s;
        }
        if (!v.empty()) out.num[p.props[0].s] = std::move(v);
    }
}

std::string CleanName(const std::string& raw, const std::string& /*cls*/) {
    // Binary files: "Name\0\1Class". ASCII files: "Class::Name".
    const std::size_t z = raw.find('\0');
    if (z != std::string::npos) return raw.substr(0, z);
    const std::size_t c = raw.find("::");
    if (c != std::string::npos && c > 0 && c <= 24) {
        bool word = true;
        for (std::size_t i = 0; i < c; ++i) { const char ch = raw[i]; if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))) word = false; }
        if (word) return raw.substr(c + 2);
    }
    return raw;
}

struct Obj {
    const Elem* e = nullptr;
    std::string cls, name, sub;
    Props props;
};
struct Conn { Id child, parent; std::string prop; bool propLink; };

std::string Base64Decode(const std::string& in) {
    static signed char tab[256]; static bool init = false;
    if (!init) { std::memset(tab, -1, sizeof(tab)); const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; for (int i = 0; i < 64; ++i) tab[(unsigned char)a[i]] = (signed char)i; init = true; }
    std::string out; unsigned acc = 0; int bits = 0;
    for (unsigned char c : in) {
        if (tab[c] < 0) continue;
        acc = (acc << 6) | (unsigned)tab[c]; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((char)((acc >> bits) & 0xFF)); }
    }
    return out;
}

// ---------------------------------------------------------------------------
// geometry layer elements
// ---------------------------------------------------------------------------
enum Mapping { kByPolyVert, kByVertex, kByPoly, kAllSame, kByEdge, kNoMap };
struct Layer {
    Mapping map = kNoMap; bool indexed = false; int dim = 1;
    const std::vector<double>* vals = nullptr; const std::vector<double>* idx = nullptr;
    bool Ok() const { return map != kNoMap && vals; }
};
const std::vector<double>* ArrayOf(const Elem& e, const char* name) {
    const Elem* c = e.Find(name);
    return (c && !c->props.empty() && c->props[0].type == 'a') ? &c->props[0].arr : nullptr;
}
Layer LoadLayer(const Elem& geo, const char* layerName, const char* valName, const char* idxName, int dim) {
    Layer L; L.dim = dim;
    const Elem* best = nullptr; double bestIdx = 1e30;
    for (const Elem& c : geo.children) {
        if (c.name != layerName) continue;
        const double k = (!c.props.empty() && c.props[0].IsNumber()) ? c.props[0].Num() : 0;
        if (!best || k < bestIdx) { best = &c; bestIdx = k; }
    }
    if (!best) return L;
    std::string mp, ref;
    if (const Elem* m = best->Find("MappingInformationType")) if (!m->props.empty()) mp = m->props[0].s;
    if (const Elem* r = best->Find("ReferenceInformationType")) if (!r->props.empty()) ref = r->props[0].s;
    if (mp == "ByPolygonVertex") L.map = kByPolyVert;
    else if (mp == "ByVertice" || mp == "ByVertex" || mp == "ByControlPoint") L.map = kByVertex;
    else if (mp == "ByPolygon") L.map = kByPoly;
    else if (mp == "AllSame") L.map = kAllSame;
    else if (mp == "ByEdge") L.map = kByEdge;
    else return L;
    L.vals = ArrayOf(*best, valName);
    L.indexed = (ref == "IndexToDirect" || ref == "Index");
    if (L.indexed) { L.idx = ArrayOf(*best, idxName); if (!L.idx) L.indexed = false; }
    if (L.indexed && !L.vals && std::string(valName) != idxName) L.vals = nullptr;
    return L;
}
/// Fetches value `out[0..dim)` for a corner. Returns false when the data does not cover it.
bool Fetch(const Layer& L, std::size_t pv, std::size_t face, std::size_t cp, const std::vector<int>& edgeOfCorner, double* out) {
    if (!L.Ok()) return false;
    std::size_t k = 0;
    switch (L.map) {
        case kByPolyVert: k = pv; break;
        case kByVertex: k = cp; break;
        case kByPoly: k = face; break;
        case kAllSame: k = 0; break;
        case kByEdge: { if (pv >= edgeOfCorner.size() || edgeOfCorner[pv] < 0) return false; k = (std::size_t)edgeOfCorner[pv]; break; }
        default: return false;
    }
    if (L.indexed) {
        if (k >= L.idx->size()) return false;
        const double v = (*L.idx)[k];
        if (!(v >= 0) || v >= 4e9) return false;
        k = (std::size_t)v;
    }
    if ((k + 1) * (std::size_t)L.dim > L.vals->size()) return false;
    for (int i = 0; i < L.dim; ++i) out[i] = (*L.vals)[k * (std::size_t)L.dim + (std::size_t)i];
    return true;
}

/// Ear clipping for a (possibly concave) polygon, projected on its dominant plane.
/// Appends triangles as indices into `pts` (local 0..n-1).
void Triangulate(const std::vector<std::array<double, 3>>& pts, std::vector<int>& tris, bool flip) {
    const int n = (int)pts.size();
    auto emit = [&](int a, int b, int c) { tris.push_back(a); if (flip) { tris.push_back(c); tris.push_back(b); } else { tris.push_back(b); tris.push_back(c); } };
    if (n < 3) return;
    if (n == 3) { emit(0, 1, 2); return; }
    if (n > 256) { for (int i = 1; i + 1 < n; ++i) emit(0, i, i + 1); return; }   // keep hostile n-gons O(n)
    double nx = 0, ny = 0, nz = 0;   // Newell
    for (int i = 0; i < n; ++i) {
        const auto& a = pts[(std::size_t)i]; const auto& b = pts[(std::size_t)((i + 1) % n)];
        nx += (a[1] - b[1]) * (a[2] + b[2]); ny += (a[2] - b[2]) * (a[0] + b[0]); nz += (a[0] - b[0]) * (a[1] + b[1]);
    }
    const double ax = std::fabs(nx), ay = std::fabs(ny), az = std::fabs(nz);
    int u = 0, v = 1; double sgn = nz;
    if (ax >= ay && ax >= az) { u = 1; v = 2; sgn = nx; } else if (ay >= az) { u = 2; v = 0; sgn = ny; }
    if (ax + ay + az < 1e-30) { for (int i = 1; i + 1 < n; ++i) emit(0, i, i + 1); return; }
    const double orient = sgn >= 0 ? 1.0 : -1.0;
    auto X = [&](int i) { return pts[(std::size_t)i][(std::size_t)u]; };
    auto Y = [&](int i) { return pts[(std::size_t)i][(std::size_t)v]; };
    auto cross = [&](int a, int b, int c) { return ((X(b) - X(a)) * (Y(c) - Y(a)) - (Y(b) - Y(a)) * (X(c) - X(a))) * orient; };
    std::vector<int> idx((std::size_t)n);
    for (int i = 0; i < n; ++i) idx[(std::size_t)i] = i;
    int guard = 0;
    while (idx.size() > 3) {
        const int m = (int)idx.size();
        int ear = -1;
        for (int i = 0; i < m && ear < 0; ++i) {
            const int a = idx[(std::size_t)((i + m - 1) % m)], b = idx[(std::size_t)i], c = idx[(std::size_t)((i + 1) % m)];
            if (cross(a, b, c) <= 1e-18) continue;   // reflex or degenerate
            bool inside = false;
            for (int j = 0; j < m && !inside; ++j) {
                const int p = idx[(std::size_t)j];
                if (p == a || p == b || p == c) continue;
                auto same = [&](int o) { return std::fabs(X(p) - X(o)) < 1e-12 && std::fabs(Y(p) - Y(o)) < 1e-12; };
                if (same(a) || same(b) || same(c)) continue;   // a bridge into a hole repeats corners at the same spot: not inside
                if (cross(a, b, p) >= 0 && cross(b, c, p) >= 0 && cross(c, a, p) >= 0) inside = true;
            }
            if (!inside) ear = i;
        }
        if (ear < 0) {   // degenerate / self-touching input: clip the sharpest convex-ish vertex, never loop
            ear = 0; double best = -1e300;
            for (int i = 0; i < m; ++i) { const double c = cross(idx[(std::size_t)((i + m - 1) % m)], idx[(std::size_t)i], idx[(std::size_t)((i + 1) % m)]); if (c > best) { best = c; ear = i; } }
        }
        const int a = idx[(std::size_t)((ear + m - 1) % m)], b = idx[(std::size_t)ear], c = idx[(std::size_t)((ear + 1) % m)];
        emit(a, b, c);
        idx.erase(idx.begin() + ear);
        if (++guard > n + 4) break;
    }
    if (idx.size() == 3) emit(idx[0], idx[1], idx[2]);
}

// ---------------------------------------------------------------------------
// animation curves
// ---------------------------------------------------------------------------
struct Curve {
    std::vector<double> t, v;           // seconds, value
    std::vector<int> flags;             // per key
    std::vector<double> sr, sl;         // per key: right slope, next-left slope (value/sec) from the file
    bool hasSlopes = false;             // the file carried KeyAttrDataFloat (slopes of auto keys are in it too)
    bool Empty() const { return t.empty(); }
    double Eval(double time) const {
        const std::size_t n = t.size();
        if (n == 0) return 0;
        if (time <= t[0] || n == 1) return v[0];
        if (time >= t[n - 1]) return v[n - 1];
        std::size_t lo = 0, hi = n - 1;
        while (hi - lo > 1) { const std::size_t mid = (lo + hi) / 2; if (t[mid] <= time) lo = mid; else hi = mid; }
        const double dt = t[hi] - t[lo];
        if (dt <= 0) return v[lo];
        const int f = flags[lo];
        if (f & 0x2) return v[lo];                                   // constant (step)
        const double u = (time - t[lo]) / dt;
        if (f & 0x4) return v[lo] + (v[hi] - v[lo]) * u;              // linear
        // cubic Hermite from the key tangents
        double m0 = sr[lo], m1 = sl[lo];
        if ((f & 0x300) && !hasSlopes) {                              // auto / TCB without stored slopes: derive from the neighbours
            m0 = AutoSlope(lo, f); m1 = AutoSlope(hi, flags[hi]);
        }
        const double u2 = u * u, u3 = u2 * u;
        return (2 * u3 - 3 * u2 + 1) * v[lo] + (u3 - 2 * u2 + u) * dt * m0 + (-2 * u3 + 3 * u2) * v[hi] + (u3 - u2) * dt * m1;
    }
    double AutoSlope(std::size_t i, int f) const {
        const std::size_t n = t.size();
        if (i == 0 || i + 1 >= n) return 0;
        const double dt = t[i + 1] - t[i - 1];
        if (dt <= 0) return 0;
        if ((f & 0x1000) && ((v[i] >= v[i - 1] && v[i] >= v[i + 1]) || (v[i] <= v[i - 1] && v[i] <= v[i + 1]))) return 0;   // clamped extremum
        return (v[i + 1] - v[i - 1]) / dt;
    }
};

struct CurveNode {
    int layer = -1;
    Id target = 0;
    std::string prop;
    double def[3] = {0, 0, 0};
    const Curve* curve[3] = {nullptr, nullptr, nullptr};
};

struct Builder {
    const Document& doc; Scene& sc; const BuildOptions& opt; std::string err;
    std::vector<Obj> objs; std::unordered_map<Id, std::size_t> byId;
    std::vector<Conn> conns;
    std::unordered_map<Id, std::vector<const Conn*>> childrenOf, parentsOf;   // by parent / by child
    Conv conv;
    std::unordered_map<Id, int> nodeOfModel, texOfObj, matOfObj, meshOfGeom;
    std::vector<Id> nodeModelId;
    std::size_t cornersBuilt = 0;

    Builder(const Document& d, Scene& s, const BuildOptions& o) : doc(d), sc(s), opt(o) {}
    bool Fail(const std::string& m) { if (err.empty()) err = m; return false; }
    const Obj* Get(Id id) const { auto it = byId.find(id); return it == byId.end() ? nullptr : &objs[it->second]; }

    bool Index() {
        const Elem* o = doc.root.Find("Objects");
        if (!o) return Fail("no Objects section (FBX 6.x files are not supported)");
        for (const Elem& e : o->children) {
            if (e.props.empty() || !e.props[0].IsNumber()) continue;
            Obj ob; ob.e = &e; ob.cls = e.name;
            const Id id = ToId(e.props[0].Num());
            if (e.props.size() > 1 && e.props[1].type == 'S') ob.name = CleanName(e.props[1].s, e.name);
            if (e.props.size() > 2 && e.props[2].type == 'S') ob.sub = e.props[2].s;
            LoadProps(e, ob.props);
            if (byId.count(id)) continue;   // duplicate id: first wins
            byId[id] = objs.size();
            objs.push_back(std::move(ob));
            if (objs.size() > opt.limits.maxElements) return Fail("too many objects");
        }
        const Elem* c = doc.root.Find("Connections");
        if (c) for (const Elem& e : c->children) {
            if (e.name != "C" || e.props.size() < 3 || e.props[0].type != 'S') continue;
            if (!e.props[1].IsNumber() || !e.props[2].IsNumber()) continue;
            Conn cn; cn.child = ToId(e.props[1].Num()); cn.parent = ToId(e.props[2].Num());
            cn.propLink = e.props[0].s == "OP";
            if (e.props.size() > 3 && e.props[3].type == 'S') cn.prop = e.props[3].s;
            conns.push_back(std::move(cn));
        }
        for (const Conn& cn : conns) { childrenOf[cn.parent].push_back(&cn); parentsOf[cn.child].push_back(&cn); }
        return true;
    }

    // ---- settings -----------------------------------------------------------
    void Settings() {
        Props gp;
        if (const Elem* g = doc.root.Find("GlobalSettings")) LoadProps(*g, gp);
        sc.settings.upAxis = std::clamp(SafeInt(gp.Get("UpAxis", 1)), 0, 2);       sc.settings.upSign = gp.Get("UpAxisSign", 1) < 0 ? -1 : 1;
        sc.settings.frontAxis = std::clamp(SafeInt(gp.Get("FrontAxis", 2)), 0, 2); sc.settings.frontSign = gp.Get("FrontAxisSign", 1) < 0 ? -1 : 1;
        sc.settings.coordAxis = std::clamp(SafeInt(gp.Get("CoordAxis", 0)), 0, 2); sc.settings.coordSign = gp.Get("CoordAxisSign", 1) < 0 ? -1 : 1;
        sc.settings.unitScaleFactor = gp.Get("UnitScaleFactor", 1.0);
        if (const Elem* h = doc.root.Find("FBXHeaderExtension")) if (const Elem* cr = h->Find("Creator")) if (!cr->props.empty()) sc.creator = cr->props[0].s;
        if (!opt.convertToYUpMeters) { sc.settings.appliedScale = 1.0; return; }
        const int ua = sc.settings.upAxis, fa = sc.settings.frontAxis, ca = sc.settings.coordAxis;
        const bool okAxes = ua >= 0 && ua < 3 && fa >= 0 && fa < 3 && ca >= 0 && ca < 3 && ua != fa && ua != ca && fa != ca;
        if (okAxes) {
            // Rows of C: where the source "right", "up", "front" axes end up (X, Y, Z of the target).
            for (int j = 0; j < 3; ++j) { conv.C[0][j] = j == ca ? sc.settings.coordSign : 0; conv.C[1][j] = j == ua ? sc.settings.upSign : 0; conv.C[2][j] = j == fa ? sc.settings.frontSign : 0; }
        }
        double s = sc.settings.unitScaleFactor * 0.01;
        if (!(s > 0) || !std::isfinite(s)) s = 0.01;
        conv.s = s;
        sc.settings.appliedScale = s;
    }

    // ---- materials / textures ----------------------------------------------
    void Textures() {
        for (const Obj& o : objs) {
            if (o.cls != "Texture") continue;
            Texture t; t.name = o.name;
            if (const Elem* f = o.e->Find("FileName")) if (!f->props.empty()) t.fileName = f->props[0].s;
            if (const Elem* f = o.e->Find("RelativeFilename")) if (!f->props.empty()) t.relativeFileName = f->props[0].s;
            const Id id = ToId(o.e->props[0].Num());
            auto pit = parentsOf.find(id);   // Video -> Texture
            for (const Conn* c : childrenOf[id]) {   // children of the texture: Video
                const Obj* v = Get(c->child);
                if (!v || v->cls != "Video" || c->propLink) continue;
                if (t.fileName.empty()) if (const Elem* f = v->e->Find("FileName")) if (!f->props.empty()) t.fileName = f->props[0].s;
                if (t.relativeFileName.empty()) if (const Elem* f = v->e->Find("RelativeFilename")) if (!f->props.empty()) t.relativeFileName = f->props[0].s;
                if (const Elem* ct = v->e->Find("Content")) if (!ct->props.empty()) {
                    const Prop& p = ct->props[0];
                    if (p.type == 'R') t.content.assign(p.s.begin(), p.s.end());
                    else if (p.type == 'S' && !p.s.empty()) { const std::string b = Base64Decode(p.s); t.content.assign(b.begin(), b.end()); }
                }
            }
            (void)pit;
            texOfObj[id] = (int)sc.textures.size();
            sc.textures.push_back(std::move(t));
        }
    }
    void Materials() {
        for (const Obj& o : objs) {
            if (o.cls != "Material") continue;
            Material m; m.name = o.name;
            const Props& p = o.props;
            const char* dn = p.Has("DiffuseColor") ? "DiffuseColor" : (p.Has("Diffuse") ? "Diffuse" : nullptr);
            if (dn) { m.hasDiffuse = true; p.Vec3(dn, m.diffuse, 0.8); }
            const char* en = p.Has("EmissiveColor") ? "EmissiveColor" : (p.Has("Emissive") ? "Emissive" : nullptr);
            if (en) { m.hasEmissive = true; p.Vec3(en, m.emissive, 0.0); }
            if (p.Has("EmissiveFactor")) { m.hasEmissiveFactor = true; m.emissiveFactor = p.Get("EmissiveFactor", 1.0); }
            const char* sn = p.Has("ShininessExponent") ? "ShininessExponent" : (p.Has("Shininess") ? "Shininess" : nullptr);
            if (sn) {   // Phong exponent -> roughness (approximate; ufbx uses its own mapping)
                const double sh = std::max(0.0, p.Get(sn, 0.0));
                m.hasRoughness = true; m.roughness = std::min(1.0, std::max(0.0, 1.0 - std::sqrt(sh) * 0.1));
            }
            const Id id = ToId(o.e->props[0].Num());
            for (const Conn* c : childrenOf[id]) {   // textures plugged into this material
                const Obj* t = Get(c->child);
                if (!t || t->cls != "Texture" || !c->propLink) continue;
                auto it = texOfObj.find(c->child);
                if (it == texOfObj.end()) continue;
                if (c->prop == "DiffuseColor" || c->prop == "Diffuse") { if (m.diffuseTexture < 0) m.diffuseTexture = it->second; }
                else if (c->prop == "NormalMap") { if (m.normalTexture < 0) m.normalTexture = it->second; }
            }
            matOfObj[id] = (int)sc.materials.size();
            sc.materials.push_back(std::move(m));
        }
    }

    // ---- nodes ---------------------------------------------------------------
    struct XProps { double t[3], r[3], s[3], pre[3], post[3], roff[3], rp[3], soff[3], sp[3], gt[3], gr[3], gs[3]; int order; };
    std::vector<XProps> xp;

    static Mat LocalMatrix(const XProps& x) {
        const Mat T = Translate(x.t[0], x.t[1], x.t[2]), Roff = Translate(x.roff[0], x.roff[1], x.roff[2]);
        const Mat Rp = Translate(x.rp[0], x.rp[1], x.rp[2]), RpI = Translate(-x.rp[0], -x.rp[1], -x.rp[2]);
        const Mat Rpre = Euler(x.pre, 0), R = Euler(x.r, x.order), RpostI = InvertOrIdentity(Euler(x.post, 0));
        const Mat Soff = Translate(x.soff[0], x.soff[1], x.soff[2]);
        const Mat Sp = Translate(x.sp[0], x.sp[1], x.sp[2]), SpI = Translate(-x.sp[0], -x.sp[1], -x.sp[2]);
        const Mat S = ScaleM(x.s[0], x.s[1], x.s[2]);
                Mat m = T;
        m = Mul(m, Roff); m = Mul(m, Rp); m = Mul(m, Rpre); m = Mul(m, R); m = Mul(m, RpostI); m = Mul(m, RpI);
        m = Mul(m, Soff); m = Mul(m, Sp); m = Mul(m, S); m = Mul(m, SpI);
        return m;
    }

    bool Nodes() {
        std::vector<std::size_t> models;
        for (std::size_t i = 0; i < objs.size(); ++i) if (objs[i].cls == "Model") models.push_back(i);
        if (models.size() > opt.limits.maxNodes) return Fail("too many nodes");
        // parent of each model (first Model parent wins; id 0 = scene root)
        std::unordered_map<Id, Id> parentOf;
        for (std::size_t mi : models) {
            const Id id = ToId(objs[mi].e->props[0].Num());
            Id par = 0;
            auto it = parentsOf.find(id);
            if (it != parentsOf.end()) for (const Conn* c : it->second) {
                if (c->propLink) continue;
                if (c->parent == 0) { par = 0; break; }
                const Obj* p = Get(c->parent);
                if (p && p->cls == "Model" && c->parent != id) { par = c->parent; break; }
            }
            parentOf[id] = par;
        }
        // children in file order; roots first, then anything caught in a parent cycle
        std::unordered_map<Id, std::vector<Id>> kids;
        std::vector<Id> rootIds, order;
        for (std::size_t mi : models) { const Id id = ToId(objs[mi].e->props[0].Num()); if (parentOf[id] == 0) rootIds.push_back(id); else kids[parentOf[id]].push_back(id); }
        std::unordered_map<Id, int> depth; std::unordered_set<Id> seen;
        auto walk = [&](Id start) -> bool {
            std::vector<Id> stack{start}; depth[start] = 1;
            while (!stack.empty()) {
                const Id cur = stack.back(); stack.pop_back();
                if (!seen.insert(cur).second) continue;
                order.push_back(cur);
                if (depth[cur] > (int)opt.limits.maxNodeDepth) return false;
                auto& k = kids[cur];
                for (std::size_t j = k.size(); j-- > 0;) { depth[k[j]] = depth[cur] + 1; stack.push_back(k[j]); }
            }
            return true;
        };
        for (Id r : rootIds) if (!walk(r)) return Fail("node hierarchy too deep");
        for (std::size_t mi : models) {   // cycle members: promote to roots
            const Id id = ToId(objs[mi].e->props[0].Num());
            if (!seen.count(id)) { parentOf[id] = 0; rootIds.push_back(id); if (!walk(id)) return Fail("node hierarchy too deep"); }
        }
        // `order` is depth-first pre-order per root already, but a promoted cycle root may have been walked
        // with stale parents: recompute final parents from visit order.
        sc.nodes.resize(order.size()); xp.resize(order.size()); nodeModelId = order;
        for (std::size_t i = 0; i < order.size(); ++i) nodeOfModel[order[i]] = (int)i;
        for (std::size_t i = 0; i < order.size(); ++i) {
            const Id id = order[i];
            SceneNode& n = sc.nodes[i];
            const Obj& o = objs[byId[id]];
            n.name = o.name;
            Id par = parentOf[id];
            n.parent = par == 0 ? -1 : nodeOfModel[par];
            if (n.parent >= (int)i) n.parent = -1;           // (cycle promoted) keep parents before children
            if (n.parent < 0) sc.roots.push_back((int)i); else sc.nodes[(std::size_t)n.parent].children.push_back((int)i);
            XProps& x = xp[i];
            const Props& p = o.props;
            p.Vec3("Lcl Translation", x.t, 0); p.Vec3("Lcl Rotation", x.r, 0); p.Vec3("Lcl Scaling", x.s, 1);
            p.Vec3("PreRotation", x.pre, 0); p.Vec3("PostRotation", x.post, 0);
            p.Vec3("RotationOffset", x.roff, 0); p.Vec3("RotationPivot", x.rp, 0);
            p.Vec3("ScalingOffset", x.soff, 0); p.Vec3("ScalingPivot", x.sp, 0);
            p.Vec3("GeometricTranslation", x.gt, 0); p.Vec3("GeometricRotation", x.gr, 0); p.Vec3("GeometricScaling", x.gs, 1);
            x.order = SafeInt(p.Get("RotationOrder", 0));
        }
        // a first pass for world matrices happens in Finish()
        return true;
    }

    void SetLocal(std::size_t i, const Mat& rawLocal) {
        SceneNode& n = sc.nodes[i];
        n.local = conv.Local(rawLocal, n.parent < 0);
        Decompose(n.local, n.translation, n.rotation, n.scale);
    }

    // ---- meshes -------------------------------------------------------------
    bool BuildMesh(const Obj& geo, std::size_t modelNode, Mesh& m) {
        const Elem& g = *geo.e;
        m.name = geo.name;
        const std::vector<double>* verts = ArrayOf(g, "Vertices");
        const std::vector<double>* pvi = ArrayOf(g, "PolygonVertexIndex");
        if (!verts || !pvi) return true;   // an empty mesh: nothing to emit
        const std::size_t ncp = verts->size() / 3;
        m.controlPoints = (std::uint32_t)std::min<std::size_t>(ncp, 0xFFFFFFFFu);
        std::vector<double> P(ncp * 3);
        for (std::size_t i = 0; i < ncp; ++i) for (int k = 0; k < 3; ++k) P[i * 3 + (std::size_t)k] = (*verts)[i * 3 + (std::size_t)k] * conv.s;

        // polygons
        struct Face { std::size_t start, count; };
        std::vector<Face> faces; std::vector<std::int32_t> cpOf(pvi->size());
        {
            std::size_t start = 0;
            for (std::size_t i = 0; i < pvi->size(); ++i) {
                const double d = (*pvi)[i];
                if (!(d > -2147483649.0 && d < 2147483648.0)) return Fail("polygon index out of range");
                const std::int64_t v = (std::int64_t)d;
                const bool end = v < 0;
                const std::int64_t c = end ? ~v : v;
                cpOf[i] = (c >= 0 && (std::size_t)c < ncp) ? (std::int32_t)c : -1;
                if (end) { faces.push_back({start, i + 1 - start}); start = i + 1; }
            }
            if (start < pvi->size()) faces.push_back({start, pvi->size() - start});   // unterminated last polygon
        }
        std::vector<int> edgeOfCorner;
        if (const std::vector<double>* ed = ArrayOf(g, "Edges")) {
            edgeOfCorner.assign(pvi->size(), -1);
            for (std::size_t e = 0; e < ed->size(); ++e) { const double v = (*ed)[e]; if (v >= 0 && v < (double)pvi->size()) edgeOfCorner[(std::size_t)v] = (int)e; }
        }
        const Layer ln = LoadLayer(g, "LayerElementNormal", "Normals", "NormalsIndex", 3);
        const Layer lu = LoadLayer(g, "LayerElementUV", "UV", "UVIndex", 2);
        const Layer lc = LoadLayer(g, "LayerElementColor", "Colors", "ColorIndex", 4);
        Layer lm = LoadLayer(g, "LayerElementMaterial", "Materials", "Materials", 1);
        if (lm.Ok()) lm.indexed = false;          // material ids are the values themselves
        m.hasUV = lu.Ok(); m.hasColors = lc.Ok(); m.hasNormals = true;   // missing normals are generated (flat, per polygon)

        std::map<int, Part> parts;
        std::vector<std::array<double, 3>> pts;
        std::vector<int> tris;
        for (std::size_t fi = 0; fi < faces.size(); ++fi) {
            const Face& f = faces[fi];
            if (f.count < 3) continue;
            bool bad = false;
            pts.clear();
            for (std::size_t k = 0; k < f.count; ++k) {
                const std::int32_t c = cpOf[f.start + k];
                if (c < 0) { bad = true; break; }
                pts.push_back({P[(std::size_t)c * 3], P[(std::size_t)c * 3 + 1], P[(std::size_t)c * 3 + 2]});
            }
            if (bad) continue;
            int slot = 0;
            double mv;
            if (lm.Ok() && Fetch(lm, f.start, fi, (std::size_t)cpOf[f.start], edgeOfCorner, &mv) && mv >= 0 && mv < 1e6) slot = (int)mv;
            double fn[3] = {0, 0, 0};   // polygon (Newell) normal, used when the file has none
            if (!ln.Ok()) for (std::size_t k = 0; k < pts.size(); ++k) {
                const auto& pa = pts[k]; const auto& pb = pts[(k + 1) % pts.size()];
                fn[0] += (pa[1] - pb[1]) * (pa[2] + pb[2]); fn[1] += (pa[2] - pb[2]) * (pa[0] + pb[0]); fn[2] += (pa[0] - pb[0]) * (pa[1] + pb[1]);
            }
            tris.clear();
            Triangulate(pts, tris, false);
            if (cornersBuilt + tris.size() > opt.limits.maxCorners) return Fail("mesh too large");
            cornersBuilt += tris.size();
            Part& part = parts[slot]; part.slot = slot;
            for (int t : tris) {
                const std::size_t k = (std::size_t)t, pv = f.start + k;
                const std::size_t cp = (std::size_t)cpOf[pv];
                Corner c;
                for (int a = 0; a < 3; ++a) c.p[a] = (float)P[cp * 3 + (std::size_t)a];
                c.cp = (std::uint32_t)cp;
                double nn[3] = {0, 0, 0};
                if (ln.Ok()) { if (!Fetch(ln, pv, fi, cp, edgeOfCorner, nn)) { nn[0] = nn[1] = nn[2] = 0; } }
                else { nn[0] = fn[0]; nn[1] = fn[1]; nn[2] = fn[2]; }
                const double l = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
                if (l > 1e-12) for (int a = 0; a < 3; ++a) c.n[a] = (float)(nn[a] / l);
                double uv[2];
                if (lu.Ok() && Fetch(lu, pv, fi, cp, edgeOfCorner, uv)) { c.uv[0] = (float)uv[0]; c.uv[1] = (float)uv[1]; }
                double col[4];
                if (lc.Ok() && Fetch(lc, pv, fi, cp, edgeOfCorner, col)) for (int a = 0; a < 4; ++a) c.color[a] = (float)col[a];
                part.corners.push_back(c);
            }
        }
        for (auto& kv : parts) if (!kv.second.corners.empty()) m.parts.push_back(std::move(kv.second));

        // material slots: materials connected to the model, in connection order
        {
            const Id mid = nodeModelId[modelNode];
            auto it = parentsOf.find(mid);   // (not used) materials are children of the model
            (void)it;
            for (const Conn* c : childrenOf[mid]) {
                if (c->propLink) continue;
                auto mt = matOfObj.find(c->child);
                if (mt != matOfObj.end()) m.materials.push_back(mt->second);
            }
        }

        // skin + blend shapes
        const Id gid = ToId(g.props[0].Num());
        bool haveSkin = false;
        for (const Conn* c : childrenOf[gid]) {
            const Obj* d = Get(c->child);
            if (!d || d->cls != "Deformer" || c->propLink) continue;
            if (d->sub == "Skin" && !haveSkin) {
                haveSkin = true;
                m.skinWeights.assign(ncp, {});
                const Mat g2n = Mul(Translate(xp[modelNode].gt[0], xp[modelNode].gt[1], xp[modelNode].gt[2]),
                                    Mul(Euler(xp[modelNode].gr, 0), ScaleM(xp[modelNode].gs[0], xp[modelNode].gs[1], xp[modelNode].gs[2])));
                std::uint32_t ci = 0;
                for (const Conn* cc : childrenOf[ToId(d->e->props[0].Num())]) {
                    const Obj* cl = Get(cc->child);
                    if (!cl || cl->cls != "Deformer" || cl->sub != "Cluster" || cc->propLink) continue;
                    Cluster cluster;
                    for (const Conn* bc : childrenOf[ToId(cl->e->props[0].Num())]) {
                        auto nm = nodeOfModel.find(bc->child);
                        if (nm != nodeOfModel.end() && !bc->propLink) { cluster.boneNode = nm->second; break; }
                    }
                    // `Transform` is the mesh node's matrix relative to the bone at bind time (what exporters
                    // such as Blender write; TransformLink, the bone's world bind matrix, is not needed).
                    Mat transform;
                    if (const std::vector<double>* a = ArrayOf(*cl->e, "Transform")) transform = FromFbx16(*a);
                    cluster.geometryToBone = conv.Scaled(Mul(transform, g2n));
                    const std::vector<double>* ix = ArrayOf(*cl->e, "Indexes");
                    const std::vector<double>* wt = ArrayOf(*cl->e, "Weights");
                    if (ix && wt) for (std::size_t k = 0; k < std::min(ix->size(), wt->size()); ++k) {
                        const double v = (*ix)[k];
                        if (v >= 0 && v < (double)ncp && std::isfinite((*wt)[k])) m.skinWeights[(std::size_t)v].push_back({ci, (float)(*wt)[k]});
                    }
                    m.clusters.push_back(std::move(cluster));
                    ++ci;
                }
                for (auto& w : m.skinWeights) std::stable_sort(w.begin(), w.end(), [](const Influence& a, const Influence& b) { return a.weight > b.weight; });
                if (m.clusters.empty()) m.skinWeights.clear();
            } else if (d->sub == "BlendShape") {
                for (const Conn* cc : childrenOf[ToId(d->e->props[0].Num())]) {
                    const Obj* ch = Get(cc->child);
                    if (!ch || ch->cls != "Deformer" || ch->sub != "BlendShapeChannel" || cc->propLink) continue;
                    BlendChannel bc; bc.name = ch->name; bc.weight = ch->props.Get("DeformPercent", 0.0) * 0.01;
                    const Obj* shape = nullptr;
                    for (const Conn* sc2 : childrenOf[ToId(ch->e->props[0].Num())]) { const Obj* s = Get(sc2->child); if (s && s->cls == "Geometry" && !sc2->propLink) shape = s; }   // last = full weight
                    if (shape) {
                        const std::vector<double>* ix = ArrayOf(*shape->e, "Indexes");
                        const std::vector<double>* dv = ArrayOf(*shape->e, "Vertices");
                        const std::vector<double>* dnm = ArrayOf(*shape->e, "Normals");
                        if (ix && dv) {
                            const std::size_t cnt = std::min(ix->size(), dv->size() / 3);
                            const bool hasN = dnm && dnm->size() >= cnt * 3 && cnt > 0;
                            for (std::size_t k = 0; k < cnt; ++k) {
                                const double v = (*ix)[k];
                                if (!(v >= 0 && v < (double)ncp)) continue;
                                bc.indices.push_back((std::uint32_t)v);
                                const double in[3] = {(*dv)[k * 3], (*dv)[k * 3 + 1], (*dv)[k * 3 + 2]}; double o[3]; conv.Pt(in, o);
                                for (int a = 0; a < 3; ++a) bc.dp.push_back((float)o[a]);
                                if (hasN) for (int a = 0; a < 3; ++a) bc.dn.push_back((float)(*dnm)[k * 3 + (std::size_t)a]);
                            }
                        }
                    }
                    chanOfObj[ToId(ch->e->props[0].Num())] = {(int)sc.meshes.size(), (int)m.channels.size()};
                    m.channels.push_back(std::move(bc));
                }
            }
        }
        return true;
    }
    std::unordered_map<Id, std::pair<int, int>> chanOfObj;

    bool Meshes() {
        for (std::size_t ni = 0; ni < nodeModelId.size(); ++ni) {
            const Id mid = nodeModelId[ni];
            const Obj* geo = nullptr;
            for (const Conn* c : childrenOf[mid]) {
                if (c->propLink) continue;
                const Obj* o = Get(c->child);
                if (o && o->cls == "Geometry" && o->sub == "Mesh") { geo = o; break; }
            }
            if (!geo) continue;
            const Id gid = ToId(geo->e->props[0].Num());
            auto it = meshOfGeom.find(gid);
            if (it != meshOfGeom.end()) { sc.nodes[ni].mesh = it->second; continue; }
            Mesh m;
            if (!BuildMesh(*geo, ni, m)) return false;
            meshOfGeom[gid] = (int)sc.meshes.size();
            sc.nodes[ni].mesh = (int)sc.meshes.size();
            sc.meshes.push_back(std::move(m));
        }
        return true;
    }

    // ---- animation ----------------------------------------------------------
    struct Layr { Id id; std::string name; double weight = 100; int blendMode = 0; };
    std::vector<Curve> curveStore;

    bool Animation() {
        // Budgets first: every count below is checked before anything proportional to it is allocated.
        {
            std::size_t stacks = 0, layers = 0, curves = 0, curveNodes = 0;
            for (const Obj& o : objs) {
                if (o.cls == "AnimationStack") ++stacks;
                else if (o.cls == "AnimationLayer") ++layers;
                else if (o.cls == "AnimationCurve") ++curves;
                else if (o.cls == "AnimationCurveNode") ++curveNodes;
            }
            if (stacks > opt.limits.maxAnimStacks) return Fail("too many animation stacks");
            if (layers > opt.limits.maxAnimLayers) return Fail("too many animation layers");
            if (curves > opt.limits.maxAnimCurves || curveNodes > opt.limits.maxAnimCurves) return Fail("too many animation curves");
        }
        std::size_t totalKeys = 0, totalBaked = 0;
        // curves
        std::unordered_map<Id, int> curveOf;
        for (const Obj& o : objs) {
            if (o.cls != "AnimationCurve") continue;
            Curve c;
            const std::vector<double>* kt = ArrayOf(*o.e, "KeyTime");
            const std::vector<double>* kv = ArrayOf(*o.e, "KeyValueFloat");
            if (!kv) kv = ArrayOf(*o.e, "KeyValueDouble");
            if (!kt || !kv) continue;
            const std::size_t n = std::min(kt->size(), kv->size());
            if (n > opt.limits.maxAnimKeys - totalKeys) return Fail("too many animation keys");
            totalKeys += n;
            const std::vector<double>* fl = ArrayOf(*o.e, "KeyAttrFlags");
            const std::vector<double>* da = ArrayOf(*o.e, "KeyAttrDataFloat");
            std::vector<double> daBits;
            if (da && !doc.binary && doc.version >= 7200) {
                // ASCII files from 7.2 on print this array as integers: the 32-bit patterns of the floats (slopes,
                // and packed weights/velocities). Binary files carry real floats; see the same rule in ufbx.
                daBits.reserve(da->size());
                for (double d : *da) { const std::uint32_t u = (d > -4.3e9 && d < 4.3e9) ? (std::uint32_t)(std::int64_t)d : 0u; float f; std::memcpy(&f, &u, 4); daBits.push_back((double)f); }
                da = &daBits;
            }
            const std::vector<double>* rc = ArrayOf(*o.e, "KeyAttrRefCount");
            std::vector<std::size_t> seg(n, 0);
            if (rc) { std::size_t s = 0, left = rc->empty() ? n : (std::size_t)std::max(0.0, (*rc)[0]); for (std::size_t i = 0; i < n; ++i) { while (left == 0 && s + 1 < rc->size()) { ++s; left = (std::size_t)std::max(0.0, (*rc)[s]); } seg[i] = s; if (left) --left; } }
            double prev = -1e300;
            for (std::size_t i = 0; i < n; ++i) {
                const double t = (*kt)[i] / kKTimePerSec;
                if (!std::isfinite(t) || !std::isfinite((*kv)[i]) || t < prev) continue;   // keep keys sorted and finite
                prev = t;
                c.t.push_back(t); c.v.push_back((*kv)[i]);
                int flags = 0x8;
                if (fl && !fl->empty()) flags = SafeInt((*fl)[std::min(seg[i], fl->size() - 1)]);
                c.flags.push_back(flags);
                double r = 0, l = 0;
                if (da && (seg[i] * 4 + 1) < da->size()) { r = (*da)[seg[i] * 4]; l = (*da)[seg[i] * 4 + 1]; c.hasSlopes = true; }
                c.sr.push_back(r); c.sl.push_back(l);
            }
            curveOf[ToId(o.e->props[0].Num())] = (int)curveStore.size();
            curveStore.push_back(std::move(c));
        }
        // curve nodes
        std::vector<CurveNode> cns; std::unordered_map<Id, std::size_t> cnOf;
        for (const Obj& o : objs) {
            if (o.cls != "AnimationCurveNode") continue;
            CurveNode cn;
            const Id id = ToId(o.e->props[0].Num());
            o.props.Vec3("d|X", cn.def, 0);
            if (o.props.Has("d|DeformPercent")) cn.def[0] = o.props.Get("d|DeformPercent", 0);
            for (const Conn* c : childrenOf[id]) {   // curves feeding it
                if (!c->propLink) continue;
                const Obj* cv = Get(c->child);
                if (!cv || cv->cls != "AnimationCurve") continue;
                auto it = curveOf.find(c->child);
                if (it == curveOf.end() || curveStore[(std::size_t)it->second].Empty()) continue;
                const int comp = (c->prop == "d|X" || c->prop == "d|DeformPercent") ? 0 : c->prop == "d|Y" ? 1 : c->prop == "d|Z" ? 2 : -1;
                if (comp >= 0) cn.curve[comp] = &curveStore[(std::size_t)it->second];
            }
            for (int k = 1; k < 3; ++k) { const char* nm = k == 1 ? "d|Y" : "d|Z"; cn.def[k] = o.props.Get(nm, 0); }
            for (const Conn* c : parentsOf[id]) {    // what it drives
                if (!c->propLink) continue;
                cn.target = c->parent; cn.prop = c->prop; break;
            }
            cnOf[id] = cns.size();
            cns.push_back(cn);
        }
        // stacks -> layers -> curve nodes
        for (const Obj& st : objs) {
            if (st.cls != "AnimationStack") continue;
            const Id sid = ToId(st.e->props[0].Num());
            std::vector<Layr> layers;
            for (const Conn* c : childrenOf[sid]) {
                const Obj* l = Get(c->child);
                if (!l || l->cls != "AnimationLayer" || c->propLink) continue;
                Layr ly; ly.id = c->child; ly.name = l->name; ly.weight = l->props.Get("Weight", 100.0); ly.blendMode = SafeInt(l->props.Get("BlendMode", 0.0));
                layers.push_back(ly);
            }
            struct NodeCN { int layer; int which; const CurveNode* cn; };      // which: 0 T, 1 R, 2 S
            std::map<int, std::vector<NodeCN>> perNode;                       // node index -> curve nodes (ordered)
            struct BlendCN { int layer; Id target; const CurveNode* cn; };
            std::vector<BlendCN> blendCns;
            double t0 = 1e300, t1 = -1e300;
            auto span = [&](const CurveNode& cn) { for (int k = 0; k < 3; ++k) if (cn.curve[k] && !cn.curve[k]->Empty()) { t0 = std::min(t0, cn.curve[k]->t.front()); t1 = std::max(t1, cn.curve[k]->t.back()); } };
            for (std::size_t li = 0; li < layers.size(); ++li)
                for (const Conn* c : childrenOf[layers[li].id]) {
                    auto it = cnOf.find(c->child);
                    if (it == cnOf.end() || c->propLink) continue;
                    const CurveNode& cn = cns[it->second];
                    auto nm = nodeOfModel.find(cn.target);
                    if (nm != nodeOfModel.end()) {
                        int which = cn.prop == "Lcl Translation" ? 0 : cn.prop == "Lcl Rotation" ? 1 : cn.prop == "Lcl Scaling" ? 2 : -1;
                        if (which < 0) continue;
                        perNode[nm->second].push_back({(int)li, which, &cn}); span(cn);
                    } else if (cn.prop == "DeformPercent" && chanOfObj.count(cn.target)) { blendCns.push_back({(int)li, cn.target, &cn}); span(cn); }
                }
            if (perNode.empty() && blendCns.empty()) continue;
            if (t1 < t0) continue;
            // A take has its own time span (LocalStart / LocalStop): several takes can share one timeline
            // and their curves, so the span, not the keys, says which part of the curves a take plays.
            if (st.props.Has("LocalStop")) {
                const double a = st.props.Get("LocalStart", 0.0) / kKTimePerSec, b = st.props.Get("LocalStop", 0.0) / kKTimePerSec;
                if (std::isfinite(a) && std::isfinite(b) && b > a) { t0 = a; t1 = b; }
            }
            Clip clip; clip.name = st.name;
            const double dur = t1 - t0;
            std::size_t samples = (std::size_t)std::max(1.0, std::ceil(dur * opt.bakeRate));
            if (samples + 1 > opt.limits.maxBakedSamples) samples = opt.limits.maxBakedSamples - 1;
            {   // whole-file budget: this clip costs (sample count) x (curve nodes it evaluates)
                std::size_t entries = blendCns.size();
                for (const auto& kv : perNode) entries += kv.second.size();
                const std::size_t ts = dur > 0 ? samples + 1 : 1;
                if (entries && ts > (opt.limits.maxTotalBakedSamples - totalBaked) / entries) return Fail("animation bake budget exceeded");
                totalBaked += ts * entries;
            }
            clip.duration = dur;
            std::vector<double> times;
            for (std::size_t k = 0; k <= samples; ++k) times.push_back(dur > 0 ? dur * (double)k / (double)samples : 0.0);
            if (dur <= 0) times.assign(1, 0.0);
            auto evalVec = [&](const std::vector<NodeCN>& list, int which, const double base[3], double time, double out[3]) {
                for (int k = 0; k < 3; ++k) out[k] = base[k];
                bool first = true;
                for (const NodeCN& n : list) {
                    if (n.which != which) continue;
                    double lv[3];
                    for (int k = 0; k < 3; ++k) lv[k] = n.cn->curve[k] ? n.cn->curve[k]->Eval(time + t0) : n.cn->def[k];
                    const Layr& ly = layers[(std::size_t)n.layer];
                    const double w = std::max(0.0, ly.weight) * 0.01;
                    if (first) { for (int k = 0; k < 3; ++k) out[k] = lv[k]; first = false; }
                    else if (ly.blendMode == 0) for (int k = 0; k < 3; ++k) out[k] += lv[k] * w;
                    else for (int k = 0; k < 3; ++k) out[k] += (lv[k] - out[k]) * std::min(1.0, w);
                }
            };
            for (auto& kv : perNode) {
                NodeAnim na; na.node = kv.first;
                const XProps base = xp[(std::size_t)kv.first];
                for (double t : times) {
                    XProps x = base;
                    evalVec(kv.second, 0, base.t, t, x.t); evalVec(kv.second, 1, base.r, t, x.r); evalVec(kv.second, 2, base.s, t, x.s);
                    const Mat local = conv.Local(LocalMatrix(x), sc.nodes[(std::size_t)kv.first].parent < 0);
                    double tr[3], q[4], s[3]; Decompose(local, tr, q, s);
                    Key3 kt; kt.time = t; for (int k = 0; k < 3; ++k) kt.v[k] = tr[k];
                    Key4 kq; kq.time = t; for (int k = 0; k < 4; ++k) kq.v[k] = q[k];
                    Key3 ks; ks.time = t; for (int k = 0; k < 3; ++k) ks.v[k] = s[k];
                    na.translation.push_back(kt); na.rotation.push_back(kq); na.scale.push_back(ks);
                }
                clip.nodes.push_back(std::move(na));
            }
            for (const BlendCN& b : blendCns) {
                BlendAnim ba; ba.mesh = chanOfObj[b.target].first; ba.channel = chanOfObj[b.target].second;
                for (double t : times) {
                    Key3 k; k.time = t; k.v[1] = k.v[2] = 0;
                    k.v[0] = b.cn->curve[0] ? b.cn->curve[0]->Eval(t + t0) : b.cn->def[0];
                    ba.percent.push_back(k);
                }
                clip.blends.push_back(std::move(ba));
            }
            sc.clips.push_back(std::move(clip));
        }
        return true;
    }

    bool Run() {
        if (!Index()) return false;
        Settings();
        Textures(); Materials();
        if (!Nodes()) return false;
        for (std::size_t i = 0; i < sc.nodes.size(); ++i) {
            SetLocal(i, LocalMatrix(xp[i]));
            SceneNode& n = sc.nodes[i];
            n.world = n.parent >= 0 ? Mul(sc.nodes[(std::size_t)n.parent].world, n.local) : n.local;
            const XProps& x = xp[i];
            const Mat g = Mul(Translate(x.gt[0], x.gt[1], x.gt[2]), Mul(Euler(x.gr, 0), ScaleM(x.gs[0], x.gs[1], x.gs[2])));
            n.geometryToNode = conv.Scaled(g);
            n.geometryToWorld = Mul(n.world, n.geometryToNode);
        }
        if (!Meshes()) return false;
        // Skin clusters refer to bones by node index; blend channel ids were recorded during Meshes().
        return Animation();
    }
};

} // namespace

bool Build(const Document& doc, Scene& out, std::string* err, const BuildOptions& opt) {
    out = Scene();
    Builder b(doc, out, opt);
    const bool ok = b.Run();
    if (!ok) { out = Scene(); if (err) *err = b.err.empty() ? "FBX build failed" : b.err; }
    return ok;
}

bool LoadMemory(const std::uint8_t* data, std::size_t size, Scene& out, std::string* err, const BuildOptions& opt) {
    Document doc;
    if (!Parse(data, size, doc, err, opt.limits)) { out = Scene(); return false; }
    return Build(doc, out, err, opt);
}

bool LoadFile(const std::string& path, Scene& out, std::string* err, const BuildOptions& opt) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "cannot open " + path; return false; }
    std::vector<std::uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return LoadMemory(buf.data(), buf.size(), out, err, opt);
}

namespace {
int g_own = -1;   // -1: not decided yet
}
bool OwnReaderEnabled() {
    if (g_own >= 0) return g_own != 0;
#ifdef OKAY_OWN_FBX_DEFAULT
    bool v = true;
#else
    bool v = false;
#endif
    if (const char* e = std::getenv("OKAY_OWN_FBX")) { if (e[0] == '1') v = true; else if (e[0] == '0') v = false; }
    g_own = v ? 1 : 0;
    return v;
}
void SetOwnReaderEnabled(bool on) { g_own = on ? 1 : 0; }

} // namespace fbx
} // namespace okay
