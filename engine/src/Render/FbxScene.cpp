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
#include <filesystem>
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
/// The matrix is affine (last row exactly 0 0 0 1) and its other twelve numbers are finite.
inline bool AffineFinite(const Mat& a) {
    if (a.m[12] != 0 || a.m[13] != 0 || a.m[14] != 0 || a.m[15] != 1) return false;
    // x - x is +0 for a finite x and NaN for an infinite or NaN one; the sum of the twelve is then 0 or NaN
    const double* m = a.m;
    const double z = (((m[0] - m[0]) + (m[1] - m[1])) + ((m[2] - m[2]) + (m[3] - m[3]))) +
                     (((m[4] - m[4]) + (m[5] - m[5])) + ((m[6] - m[6]) + (m[7] - m[7]))) +
                     (((m[8] - m[8]) + (m[9] - m[9])) + ((m[10] - m[10]) + (m[11] - m[11])));
    return z == 0;
}
/// The product of two affine matrices with finite entries, without the checks (see Mul).
inline Mat Mul34(const Mat& a, const Mat& b) {
    Mat r;
    for (int i = 0; i < 3; ++i) {
        const double* ai = a.m + i * 4;
        for (int j = 0; j < 3; ++j) {
            double s = 0;
            s += ai[0] * b.m[j]; s += ai[1] * b.m[4 + j]; s += ai[2] * b.m[8 + j];   // + ai[3] * 0
            r.m[i * 4 + j] = s;
        }
        double s = 0;
        s += ai[0] * b.m[3]; s += ai[1] * b.m[7]; s += ai[2] * b.m[11]; s += ai[3] * b.m[15];
        r.m[i * 4 + 3] = s;
    }
    return r;   // r.m[12..15] are already 0 0 0 1
}
Mat Mul(const Mat& a, const Mat& b) {
    Mat r;
    if (AffineFinite(a) && AffineFinite(b)) {
        // The same sums as the general loop below, in the same order, minus the terms that are exactly zero. Why that
        // is exact: a sum that starts at +0 and adds products in round-to-nearest is never -0, and adding a zero
        // (of either sign) to a number that is not -0 leaves it unchanged; with finite entries every product with a
        // zero factor is a zero. The last row of a product of two affine matrices is 0 0 0 1 exactly.
        return Mul34(a, b);
    }
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
inline bool Finite3(double x, double y, double z) {   // all three finite
    std::uint64_t u[3]; std::memcpy(u, &x, 8); std::memcpy(u + 1, &y, 8); std::memcpy(u + 2, &z, 8);
    const std::uint64_t e = 0x7FF0000000000000ull;
    return (u[0] & e) != e && (u[1] & e) != e && (u[2] & e) != e;
}
Mat Mul(const Mat& a, const Mat& b);
/// Mul(m, Translate(x, y, z)), computed with only the terms that are not exactly zero (exact for the reason given in
/// Mul; it needs m affine and finite and the offset finite, else it is the general product).
inline Mat MulTranslate(const Mat& m, double x, double y, double z);
/// Mul(m, ScaleM(x, y, z)) the same way.
inline Mat MulScale(const Mat& m, double x, double y, double z);
Mat RotAxis(int axis, double deg) {
    const double a = deg * kPi / 180.0, c = std::cos(a), s = std::sin(a);
    Mat r;
    if (axis == 0)      { r.m[5] = c; r.m[6] = -s; r.m[9] = s; r.m[10] = c; }
    else if (axis == 1) { r.m[0] = c; r.m[2] = s; r.m[8] = -s; r.m[10] = c; }
    else                { r.m[0] = c; r.m[1] = -s; r.m[4] = s; r.m[5] = c; }
    return r;
}
inline Mat MulTranslate(const Mat& m, double x, double y, double z) {
    if (!AffineFinite(m) || !Finite3(x, y, z)) return Mul(m, Translate(x, y, z));
    Mat r;
    for (int i = 0; i < 3; ++i) {
        const double* a = m.m + i * 4;
        for (int j = 0; j < 3; ++j) { double s = 0; s += a[j]; r.m[i * 4 + j] = s; }   // 0 + a * 1, the zero terms dropped
        double s = 0; s += a[0] * x; s += a[1] * y; s += a[2] * z; s += a[3];        // a[3] * 1 is a[3]
        r.m[i * 4 + 3] = s;
    }
    return r;
}
inline Mat MulScale(const Mat& m, double x, double y, double z) {
    if (!AffineFinite(m) || !Finite3(x, y, z)) return Mul(m, ScaleM(x, y, z));
    Mat r;
    const double sc[3] = {x, y, z};
    for (int i = 0; i < 3; ++i) {
        const double* a = m.m + i * 4;
        for (int j = 0; j < 3; ++j) { double s = 0; s += a[j] * sc[j]; r.m[i * 4 + j] = s; }
        double s = 0; s += a[3]; r.m[i * 4 + 3] = s;
    }
    return r;
}

/// FBX rotation orders: 0 XYZ 1 XZY 2 YZX 3 YXZ 4 ZXY 5 ZYX (the first letter is applied first).
/// Mul(RotAxis(axis, deg), r) with the structure of the rotation used: the row of the axis is r's row (0 + r), the two
/// other rows are c and s combinations of r's rows, in the order the general product adds them (exact for the reason
/// given in Mul: r and the rotation affine and finite, so the dropped products are all zeros). Otherwise the general
/// product.
Mat RotateLeft(int axis, double deg, const Mat& r, bool known = false) {   // known: r is affine and finite
    const Mat A = RotAxis(axis, deg);
    if (!known && (!AffineFinite(r) || !AffineFinite(A))) return Mul(A, r);   // (NaN results must come out bit for bit too)
    const double* a = A.m;
    // the two rows that change: (p, q) = (1, 2) for X, (0, 2) for Y, (0, 1) for Z
    const int p = axis == 0 ? 1 : 0, q = axis == 2 ? 1 : 2;
    Mat out;
    for (int j = 0; j < 4; ++j) {
        double s0 = 0; s0 += r.m[axis * 4 + j]; out.m[axis * 4 + j] = s0;
        double sp = 0; sp += a[p * 4 + p] * r.m[p * 4 + j]; sp += a[p * 4 + q] * r.m[q * 4 + j]; out.m[p * 4 + j] = sp;
        double sq = 0; sq += a[q * 4 + p] * r.m[p * 4 + j]; sq += a[q * 4 + q] * r.m[q * 4 + j]; out.m[q * 4 + j] = sq;
    }
    return out;
}
/// |v| < 1e100 (false for NaN and infinities). Products and sums of a few such numbers and of sines and cosines stay
/// far below the largest double, so a chain of the fast products above never meets an infinity on the way.
inline bool Small(double v) { return std::fabs(v) < 1e100; }
inline bool Small3(const double v[3]) { return Small(v[0]) && Small(v[1]) && Small(v[2]); }
Mat Euler(const double d[3], int order) {
    static const int seq[6][3] = {{0,1,2},{0,2,1},{1,2,0},{1,0,2},{2,0,1},{2,1,0}};
    if (order < 0 || order > 5) order = 0;
    // With small finite angles every step is a rotation of finite numbers (no check needed per step).
    const bool known = Small3(d);
    Mat r;
    for (int k = 0; k < 3; ++k) r = RotateLeft(seq[order][k], d[seq[order][k]], r, known);
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
Mat FromFbx16(const Array& a) {
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

// An object's Properties70, read on demand: each lookup scans the P records (no map is built per object). The
// rules are the ones the old per-object map followed: the LAST record with a name and at least one number wins, and
// the numbers are the numeric properties from index 4 on, in order.
struct Props {
    const Elem* p70 = nullptr;
    const Elem* FindP(std::string_view n) const {
        if (!p70) return nullptr;
        const Elem* hit = nullptr;
        for (const Elem& p : p70->children) {
            if (p.props.size() < 5 || p.name != "P" || p.props[0].type != 'S' || p.props[0].s != n) continue;
            for (std::size_t i = 4; i < p.props.size(); ++i) if (p.props[i].IsNumber()) { hit = &p; break; }
        }
        return hit;
    }
    /// The first `k` numbers of the record (fewer if it has fewer); returns how many were found.
    static int Numbers(const Elem* p, double* out, int k) {
        int got = 0;
        if (p) for (std::size_t i = 4; i < p->props.size() && got < k; ++i) if (p->props[i].IsNumber()) out[got++] = p->props[i].Num();
        return got;
    }
    bool Has(std::string_view n) const { return FindP(n) != nullptr; }
    double Get(std::string_view n, double def) const { double v; return Numbers(FindP(n), &v, 1) == 1 ? v : def; }
    void Vec3(std::string_view n, double out[3], double def) const {
        double v[3]; const int got = Numbers(FindP(n), v, 3);
        for (int i = 0; i < 3; ++i) out[i] = i < got ? v[i] : def;
    }
};
void LoadProps(const Elem& e, Props& out) { out.p70 = e.Find("Properties70"); }

std::string_view CleanName(std::string_view raw) {
    // Binary files: "Name\0\1Class". ASCII files: "Class::Name".
    const std::size_t z = raw.find('\0');
    if (z != std::string_view::npos) return raw.substr(0, z);
    const std::size_t c = raw.find("::");
    if (c != std::string_view::npos && c > 0 && c <= 24) {
        bool word = true;
        for (std::size_t i = 0; i < c; ++i) { const char ch = raw[i]; if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))) word = false; }
        if (word) return raw.substr(c + 2);
    }
    return raw;
}

// PBR property sets of modelling tools, named as ufbx names them (ufbx.c: ufbxi_3ds_max_physical_material_pbr_mapping and
// its siblings). The property name in the file is `prefix + name`. A set is recognised by the class id the tool writes into
// the material (3dsMax|ClassIDa / 3dsMax|ClassIDb). Only what this engine can show is read: base colour, roughness,
// metalness, emission colour and factor, normal map, ambient occlusion map. Each list ends with nullptr.
struct PbrSet {
    const char* id; std::uint32_t classA, classB; const char* prefix;
    const char* const* baseColor; const char* const* roughness; const char* const* metalness;
    const char* const* emitColor; const char* const* emitFactor; const char* const* normal; const char* const* ao;
    const char* useGlossinessProp;   ///< property that says the roughness slot holds glossiness (about 1 = it does; nullptr = never)
};
const char* const kNone[] = {nullptr};
const char* const kBase[] = {"base_color", "baseColor", "basecolor", nullptr};   // "basecolor" is what 3ds Max really writes for its PBR material
const char* const kRough[] = {"roughness", "Roughness_Map", nullptr};
const char* const kMetal[] = {"metalness", nullptr};
const char* const kEmitC[] = {"emit_color", nullptr};
const char* const kPhysEmitF[] = {"emission", nullptr};
const char* const kBump[] = {"bump", "bump_map_amt", nullptr};
const char* const kNorm[] = {"norm", nullptr};
const char* const kAo[] = {"ao", nullptr};
const char* const kGloss[] = {"glossiness", nullptr};
const char* const kGltfBase[] = {"main|baseColor", nullptr};
const char* const kGltfRough[] = {"main|roughness", nullptr};
const char* const kGltfMetal[] = {"main|metalness", nullptr};
const char* const kGltfEmit[] = {"main|emission", "main|emissionColor", nullptr};
const char* const kGltfNormal[] = {"main|normal", nullptr};
const char* const kGltfAo[] = {"main|ambientOcclusion", nullptr};
const char* const kOpenRough[] = {"specular_roughness", nullptr};
const char* const kOpenMetal[] = {"base_metalness", nullptr};
const char* const kOpenBase[] = {"base_color", nullptr};
const char* const kOpenEmitC[] = {"emission_color", nullptr};
const char* const kOpenEmitF[] = {"emission_weight", nullptr};
const PbrSet kPbrSets[] = {
    {"3dsmax-physical",       0x3d6b1cecu, 0xdeadc001u, "3dsMax|Parameters|", kBase, kRough, kMetal, kEmitC, kPhysEmitF, kBump, kNone, nullptr},
    {"openpbr",               0xf1551e33u, 0x37fb1337u, "3dsMax|Parameters|", kOpenBase, kOpenRough, kOpenMetal, kOpenEmitC, kOpenEmitF, kBump, kNone, nullptr},
    {"gltf",                  0x38420192u, 0x45fe4e1bu, "3dsMax|",            kGltfBase, kGltfRough, kGltfMetal, kGltfEmit, kNone, kGltfNormal, kGltfAo, nullptr},
    {"3dsmax-pbr-metal-rough", 0xd00f1e00u, 0xbe77e500u, "3dsMax|main|",      kBase, kRough, kMetal, kEmitC, kNone, kNorm, kAo, "useGlossiness"},
    {"3dsmax-pbr-spec-gloss", 0xd00f1e00u, 0x01dbad33u, "3dsMax|main|",       kBase, kGloss, kNone, kEmitC, kNone, kNorm, kAo, "useGlossiness"},
};

struct Obj {
    const Elem* e = nullptr;
    std::string_view cls, name, sub;
    Props props;
};
struct Conn { Id child, parent; std::string_view prop; bool propLink; };

std::string Base64Decode(std::string_view in) {
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
    const Array* vals = nullptr; const Array* idx = nullptr;
    bool Ok() const { return map != kNoMap && vals; }
};
const Array* ArrayOf(const Elem& e, const char* name) {
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
    std::string_view mp, ref;
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
inline bool Fetch(const Layer& L, std::size_t pv, std::size_t face, std::size_t cp, const std::vector<int>& edgeOfCorner, double* out) {
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
        if (L.idx->type == 'i') {   // the usual index type, read directly: the same test as below for a 32-bit integer
            std::int32_t v; std::memcpy(&v, L.idx->data + k * 4, 4);
            if (v < 0) return false;
            k = (std::size_t)v;
        } else {
            const double v = (*L.idx)[k];
            if (!(v >= 0) || v >= 4e9) return false;
            k = (std::size_t)v;
        }
    }
    if ((k + 1) * (std::size_t)L.dim > L.vals->size()) return false;
    if (L.vals->type == 'd') {   // doubles, copied as they are (one fixed-size copy per number, no call)
        const std::uint8_t* src = L.vals->data + k * (std::size_t)L.dim * 8;
        for (int i = 0; i < L.dim; ++i) std::memcpy(out + i, src + (std::size_t)i * 8, 8);
        return true;
    }
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
        const auto& a = pts[(std::size_t)i]; const auto& b = pts[(std::size_t)(i + 1 == n ? 0 : i + 1)];
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
    int idx[256];   // n <= 256 here: the corners still to clip, on the stack (no allocation per polygon)
    int cnt = n;
    for (int i = 0; i < n; ++i) idx[i] = i;
    int guard = 0;
    while (cnt > 3) {
        const int m = cnt;
        int ear = -1;
        for (int i = 0; i < m && ear < 0; ++i) {
            const int a = idx[i == 0 ? m - 1 : i - 1], b = idx[i], c = idx[i + 1 == m ? 0 : i + 1];   // (no division: same as % m)
            if (cross(a, b, c) <= 1e-18) continue;   // reflex or degenerate
            bool inside = false;
            for (int j = 0; j < m && !inside; ++j) {
                const int p = idx[j];
                if (p == a || p == b || p == c) continue;
                auto same = [&](int o) { return std::fabs(X(p) - X(o)) < 1e-12 && std::fabs(Y(p) - Y(o)) < 1e-12; };
                if (same(a) || same(b) || same(c)) continue;   // a bridge into a hole repeats corners at the same spot: not inside
                if (cross(a, b, p) >= 0 && cross(b, c, p) >= 0 && cross(c, a, p) >= 0) inside = true;
            }
            if (!inside) ear = i;
        }
        if (ear < 0) {   // degenerate / self-touching input: clip the sharpest convex-ish vertex, never loop
            ear = 0; double best = -1e300;
            for (int i = 0; i < m; ++i) { const double c = cross(idx[i == 0 ? m - 1 : i - 1], idx[i], idx[i + 1 == m ? 0 : i + 1]); if (c > best) { best = c; ear = i; } }
        }
        const int a = idx[ear == 0 ? m - 1 : ear - 1], b = idx[ear], c = idx[ear + 1 == m ? 0 : ear + 1];
        emit(a, b, c);
        std::memmove(idx + ear, idx + ear + 1, sizeof(int) * (std::size_t)(cnt - ear - 1)); --cnt;
        if (++guard > n + 4) break;
    }
    if (cnt == 3) emit(idx[0], idx[1], idx[2]);
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
    double Eval(double time) const { std::size_t h = (std::size_t)-1; return Eval(time, h); }
    /// `hint` remembers the key segment of the last call: samples taken in increasing time move forward from it
    /// instead of searching all keys. The segment is the same either way (the last key at or before `time`).
    double Eval(double time, std::size_t& hint) const {
        const std::size_t n = t.size();
        if (n == 0) return 0;
        if (time <= t[0] || n == 1) return v[0];
        if (time >= t[n - 1]) return v[n - 1];
        std::size_t lo, hi;
        if (hint < n - 1 && t[hint] <= time) {   // t[n - 1] > time, so this stops before the last key
            lo = hint;
            while (t[lo + 1] <= time) ++lo;
        } else {
            lo = 0; hi = n - 1;
            while (hi - lo > 1) { const std::size_t mid = (lo + hi) / 2; if (t[mid] <= time) lo = mid; else hi = mid; }
        }
        hint = lo; hi = lo + 1;
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
    std::string_view prop;
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
            if (e.props.size() > 1 && e.props[1].type == 'S') ob.name = CleanName(e.props[1].s);
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
        if (const Elem* h = doc.root.Find("FBXHeaderExtension")) if (const Elem* cr = h->Find("Creator")) if (!cr->props.empty()) sc.creator = std::string(cr->props[0].s);
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
            Texture t; t.name = std::string(o.name);
            if (const Elem* f = o.e->Find("FileName")) if (!f->props.empty()) t.fileName = std::string(f->props[0].s);
            if (const Elem* f = o.e->Find("RelativeFilename")) if (!f->props.empty()) t.relativeFileName = std::string(f->props[0].s);
            const Id id = ToId(o.e->props[0].Num());
            auto pit = parentsOf.find(id);   // Video -> Texture
            for (const Conn* c : childrenOf[id]) {   // children of the texture: Video
                const Obj* v = Get(c->child);
                if (!v || v->cls != "Video" || c->propLink) continue;
                if (t.fileName.empty()) if (const Elem* f = v->e->Find("FileName")) if (!f->props.empty()) t.fileName = std::string(f->props[0].s);
                if (t.relativeFileName.empty()) if (const Elem* f = v->e->Find("RelativeFilename")) if (!f->props.empty()) t.relativeFileName = std::string(f->props[0].s);
                if (const Elem* ct = v->e->Find("Content")) if (!ct->props.empty()) {
                    const Prop& p = ct->props[0];
                    if (p.type == 'R') t.content.assign(p.s.begin(), p.s.end());
                    else if (p.type == 'S' && !p.s.empty()) { const std::string b = Base64Decode(p.s); t.content.assign(b.begin(), b.end()); }
                }
            }
            (void)pit;
            // A Video that carries the picture while the one connected to the texture does not (some exporters write the pair
            // apart): found by file name, as ufbx does. Only when the texture got no content from its own Video.
            if (t.content.empty() && (!t.fileName.empty() || !t.relativeFileName.empty())) {
                for (const Obj& vo : objs) {
                    if (vo.cls != "Video") continue;
                    const Elem* ct = vo.e->Find("Content");
                    if (!ct || ct->props.empty() || ct->props[0].s.empty()) continue;
                    const Elem* vf = vo.e->Find("FileName"); const Elem* vr = vo.e->Find("RelativeFilename");
                    const bool same = (vf && !vf->props.empty() && !t.fileName.empty() && vf->props[0].s == t.fileName) ||
                                      (vr && !vr->props.empty() && !t.relativeFileName.empty() && vr->props[0].s == t.relativeFileName);
                    if (!same) continue;
                    const Prop& p = ct->props[0];
                    if (p.type == 'R') t.content.assign(p.s.begin(), p.s.end());
                    else if (p.type == 'S') { const std::string b = Base64Decode(p.s); t.content.assign(b.begin(), b.end()); }
                    if (!t.content.empty()) break;
                }
            }
            texOfObj[id] = (int)sc.textures.size();
            sc.textures.push_back(std::move(t));
        }
    }
    void Materials() {
        for (const Obj& o : objs) {
            if (o.cls != "Material") continue;
            Material m; m.name = std::string(o.name);
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
            std::string shadingModel;
            if (const Elem* sm = o.e->Find("ShadingModel")) if (!sm->props.empty()) shadingModel = std::string(sm->props[0].s);
            for (char& ch : shadingModel) ch = (char)std::tolower((unsigned char)ch);
            if (shadingModel == "lambert" || shadingModel == "phong") m.shaderSet = shadingModel;
            // PBR property sets written by modelling tools (ufbx's "shader types"): found by the tool's class id, and
            // only when the material does not say lambert / phong, the same rule ufbx uses.
            const PbrSet* pbr = nullptr;
            if (m.shaderSet.empty()) {
                const std::uint32_t ca = (std::uint32_t)SafeInt64(p.Get("3dsMax|ClassIDa", 0)), cb = (std::uint32_t)SafeInt64(p.Get("3dsMax|ClassIDb", 0));
                for (const PbrSet& s : kPbrSets) if (ca == s.classA && cb == s.classB) { pbr = &s; break; }
            }
            if (pbr) {
                m.shaderSet = pbr->id;
                const std::string pre = pbr->prefix;
                auto find = [&](const char* const* names) -> const char* {   // first name the file has, as the full property name
                    for (; *names; ++names) if (p.Has((pre + *names).c_str())) return *names;
                    return nullptr;
                };
                double v3[3];
                if (const char* n = find(pbr->baseColor)) { m.hasDiffuse = true; p.Vec3((pre + n).c_str(), v3, 1.0); for (int i = 0; i < 3; ++i) m.diffuse[i] = v3[i]; }
                if (const char* n = find(pbr->emitColor)) { m.hasEmissive = true; p.Vec3((pre + n).c_str(), v3, 0.0); for (int i = 0; i < 3; ++i) m.emissive[i] = v3[i]; }
                if (const char* n = find(pbr->emitFactor)) { m.hasEmissiveFactor = true; m.emissiveFactor = p.Get((pre + n).c_str(), 1.0); }
                if (const char* n = find(pbr->metalness)) { m.hasMetalness = true; m.metalness = std::min(1.0, std::max(0.0, p.Get((pre + n).c_str(), 0.0))); }
                if (const char* n = find(pbr->roughness)) {
                    double r = p.Get((pre + n).c_str(), 0.5);
                    const bool gloss = pbr->useGlossinessProp && std::fabs(p.Get((pre + pbr->useGlossinessProp).c_str(), 0.0) - 1.0) < 0.5;
                    if (gloss) r = 1.0 - r;
                    m.hasRoughness = true; m.roughness = std::min(1.0, std::max(0.0, r));
                }
            }
            const Id id = ToId(o.e->props[0].Num());
            auto squash = [](std::string_view x) { std::string o; for (char ch : x) if (ch != '_') o += (char)std::tolower((unsigned char)ch); return o; };
            // the slot's own name, or "<name>_map" (what 3ds Max connects textures to: "base_color_map" for the value "basecolor")
            auto isOneOf = [&](std::string_view prop, const char* const* names, const std::string& pre) {
                if (!names) return false;
                const std::string want = squash(prop);
                for (; *names; ++names) { const std::string n = squash(pre + *names); if (want == n || want == n + "map") return true; }
                return false;
            };
            for (const Conn* c : childrenOf[id]) {   // textures plugged into this material
                const Obj* t = Get(c->child);
                if (!t || t->cls != "Texture" || !c->propLink) continue;
                auto it = texOfObj.find(c->child);
                if (it == texOfObj.end()) continue;
                if (pbr) {
                    const std::string pre = pbr->prefix;
                    if (isOneOf(c->prop, pbr->baseColor, pre)) { if (m.diffuseTexture < 0) m.diffuseTexture = it->second; continue; }
                    if (isOneOf(c->prop, pbr->normal, pre)) { if (m.normalTexture < 0) m.normalTexture = it->second; continue; }
                    if (isOneOf(c->prop, pbr->ao, pre)) { if (m.aoTexture < 0) m.aoTexture = it->second; continue; }
                }
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

    /// Pre-rotation and inverse post-rotation, which animation never changes: computed once per node when a clip is
    /// baked (the same values LocalMatrix would compute every sample).
    struct FixedRot { Mat pre, postInv; bool preIsIdentity, postIsIdentity; bool simple = false; };
    static bool IsIdentity(const Mat& a) {   // by value: -0 counts as 0 (only the value takes part in a product)
        static const double id[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        for (int i = 0; i < 16; ++i) if (!(a.m[i] == id[i])) return false;
        return true;
    }
    static FixedRot FixedRotations(const XProps& x) {
        FixedRot f{Euler(x.pre, 0), InvertOrIdentity(Euler(x.post, 0)), false, false};
        f.preIsIdentity = IsIdentity(f.pre); f.postIsIdentity = IsIdentity(f.postInv);
        // LocalSimple applies: no offsets or pivots (by value), and fixed rotations that are affine with small entries
        auto zero3 = [](const double v[3]) { return v[0] == 0 && v[1] == 0 && v[2] == 0; };
        auto smallAffine = [](const Mat& a) {
            if (a.m[12] != 0 || a.m[13] != 0 || a.m[14] != 0 || a.m[15] != 1) return false;
            for (int i = 0; i < 12; ++i) if (!Small(a.m[i])) return false;
            return true;
        };
        f.simple = zero3(x.roff) && zero3(x.rp) && zero3(x.soff) && zero3(x.sp) && smallAffine(f.pre) && smallAffine(f.postInv);
        return f;
    }
    /// Mul(m, identity): every entry is 0 + m (the other products are zeros when m is finite), else the general product.
    static Mat MulIdentity(const Mat& m, const Mat& id) {
        if (!AffineFinite(m)) return Mul(m, id);
        Mat r;
        for (int i = 0; i < 12; ++i) { double s = 0; s += m.m[i]; r.m[i] = s; }
        return r;
    }
    static Mat LocalMatrix(const XProps& x) { const FixedRot f = FixedRotations(x); return LocalMatrix(x, f); }
    static Mat LocalMatrix(const XProps& x, const FixedRot& fixed) { return LocalMatrix(x, fixed, Euler(x.r, x.order)); }
    /// LocalMatrix for a node without offsets and pivots, with small finite t, r and s: the same numbers. Dropped are
    /// the products by the zero offsets (each is 0 + m, which changes nothing here: every entry that follows is again
    /// a sum that starts at +0 and only meets m through products and sums, so a -0 in m cannot show), the products by
    /// identity fixed rotations (the same), and all the per-step checks (nothing can overflow, see Small).
    static Mat LocalSimple(const XProps& x, const FixedRot& f, const Mat& R) {
        Mat m = Translate(x.t[0], x.t[1], x.t[2]);
        if (!f.preIsIdentity) m = Mul34(m, f.pre);
        m = Mul34(m, R);
        if (!f.postIsIdentity) m = Mul34(m, f.postInv);
        Mat r;
        const double sc[3] = {x.s[0], x.s[1], x.s[2]};
        for (int i = 0; i < 3; ++i) {   // MulScale
            const double* a = m.m + i * 4;
            for (int j = 0; j < 3; ++j) { double s = 0; s += a[j] * sc[j]; r.m[i * 4 + j] = s; }
            double s = 0; s += a[3]; r.m[i * 4 + 3] = s;
        }
        return r;
    }
    /// T * Roff * Rp * Rpre * R * Rpost^-1 * Rp^-1 * Soff * Sp * S * Sp^-1, multiplied left to right (R given).
    static Mat LocalMatrix(const XProps& x, const FixedRot& fixed, const Mat& R) {
        Mat m = Translate(x.t[0], x.t[1], x.t[2]);
        m = MulTranslate(m, x.roff[0], x.roff[1], x.roff[2]); m = MulTranslate(m, x.rp[0], x.rp[1], x.rp[2]);
        m = fixed.preIsIdentity ? MulIdentity(m, fixed.pre) : Mul(m, fixed.pre);
        m = Mul(m, R);
        m = fixed.postIsIdentity ? MulIdentity(m, fixed.postInv) : Mul(m, fixed.postInv);
        m = MulTranslate(m, -x.rp[0], -x.rp[1], -x.rp[2]);
        m = MulTranslate(m, x.soff[0], x.soff[1], x.soff[2]); m = MulTranslate(m, x.sp[0], x.sp[1], x.sp[2]);
        m = MulScale(m, x.s[0], x.s[1], x.s[2]);
        m = MulTranslate(m, -x.sp[0], -x.sp[1], -x.sp[2]);
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
            n.name = std::string(o.name);
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
        m.name = std::string(geo.name);
        const Array* verts = ArrayOf(g, "Vertices");
        const Array* pvi = ArrayOf(g, "PolygonVertexIndex");
        if (!verts || !pvi) return true;   // an empty mesh: nothing to emit
        const std::size_t ncp = verts->size() / 3;
        m.controlPoints = (std::uint32_t)std::min<std::size_t>(ncp, 0xFFFFFFFFu);
        // Positions (unit-scaled) and polygon corners are read straight from the file's arrays where they are used:
        // no per-mesh copies (on big meshes the fresh memory for copies cost more time than the copying itself).
        // Each value is the same double the copies held: verts[k] * s, and the corner decoding below.
        auto pos = [&](std::size_t cp, int k) -> double {
            const std::size_t at = cp * 3 + (std::size_t)k;
            if (verts->type == 'd') { double v; std::memcpy(&v, verts->data + at * 8, 8); return v * conv.s; }
            return (*verts)[at] * conv.s;
        };
        const std::size_t npv = pvi->size();
        const bool pviInt = pvi->type == 'i';
        // Corner i: its control point, or -1 when out of range; `end` is set on the last corner of a polygon.
        auto cornerAt = [&](std::size_t i, bool& end) -> std::int32_t {
            std::int64_t v;
            if (pviInt) { std::int32_t x; std::memcpy(&x, pvi->data + i * 4, 4); v = x; }
            else v = (std::int64_t)(*pvi)[i];   // range checked once, below, before any polygon is read
            end = v < 0;
            const std::int64_t c = end ? ~v : v;
            return (c >= 0 && (std::size_t)c < ncp) ? (std::int32_t)c : -1;
        };
        if (!pviInt)
            for (std::size_t i = 0; i < npv; ++i) {
                const double d = (*pvi)[i];
                if (!(d > -2147483649.0 && d < 2147483648.0)) return Fail("polygon index out of range");
            }
        // Walks the polygons in order: fn(polygon number, first corner, corner count, all corners in range).
        auto forEachPolygon = [&](auto&& fn) -> bool {
            std::size_t start = 0, fi = 0; bool good = true;
            for (std::size_t i = 0; i < npv; ++i) {
                bool end; const std::int32_t c = cornerAt(i, end);
                if (c < 0) good = false;
                if (end) { if (!fn(fi++, start, i + 1 - start, good)) return false; start = i + 1; good = true; }
            }
            if (start < npv) return fn(fi, start, npv - start, good);   // unterminated last polygon
            return true;
        };
        auto cpOf = [&](std::size_t i) { bool e; return cornerAt(i, e); };
        const Layer ln = LoadLayer(g, "LayerElementNormal", "Normals", "NormalsIndex", 3);
        const Layer lu = LoadLayer(g, "LayerElementUV", "UV", "UVIndex", 2);
        const Layer lc = LoadLayer(g, "LayerElementColor", "Colors", "ColorIndex", 4);
        Layer lm = LoadLayer(g, "LayerElementMaterial", "Materials", "Materials", 1);
        if (lm.Ok()) lm.indexed = false;          // material ids are the values themselves
        m.hasUV = lu.Ok(); m.hasColors = lc.Ok(); m.hasNormals = true;   // missing normals are generated (flat, per polygon)
        std::vector<int> edgeOfCorner;   // only a ByEdge layer reads it
        if (ln.map == kByEdge || lu.map == kByEdge || lc.map == kByEdge || lm.map == kByEdge)
            if (const Array* ed = ArrayOf(g, "Edges")) {
                edgeOfCorner.assign(npv, -1);
                for (std::size_t e = 0; e < ed->size(); ++e) { const double v = (*ed)[e]; if (v >= 0 && v < (double)npv) edgeOfCorner[(std::size_t)v] = (int)e; }
            }
        auto slotOfPolygon = [&](std::size_t fi, std::size_t start) {
            int slot = 0;
            double mv;
            if (lm.Ok() && Fetch(lm, start, fi, (std::size_t)cpOf(start), edgeOfCorner, &mv) && mv >= 0 && mv < 1e6) slot = (int)mv;
            return slot;
        };

        // First pass: how many corners each material slot will get, so each part's index list is allocated once at its
        // final size. A polygon of n corners gives n - 2 triangles (Triangulate); polygons with fewer than 3 corners
        // or a corner out of range are skipped (in both passes).
        std::map<int, std::size_t> cornersOfSlot;
        forEachPolygon([&](std::size_t fi, std::size_t start, std::size_t count, bool good) {
            if (count >= 3 && good) cornersOfSlot[slotOfPolygon(fi, start)] += (count - 2) * 3;
            return true;
        });
        {   // the same budget the loop below enforces polygon by polygon, checked before anything is allocated
            std::size_t total = 0;
            for (const auto& kv : cornersOfSlot) total += kv.second;
            if (total > opt.limits.maxCorners - std::min(cornersBuilt, opt.limits.maxCorners)) return Fail("mesh too large");
        }
        // Each part keeps its distinct corners and an index list. Equal corners (byte for byte) are found with an
        // open-addressing table of vertex numbers (power-of-two size, at most half full, linear probing).
        struct PartBuild { Part part; std::vector<std::uint32_t> table; };
        const std::uint32_t kEmpty = 0xFFFFFFFFu;
        std::map<int, PartBuild> parts;
        for (const auto& kv : cornersOfSlot) {
            PartBuild& pb = parts[kv.first];
            pb.part.slot = kv.first;
            pb.part.indices.reserve(kv.second);
            const std::size_t guess = std::min(kv.second, ncp + 16);   // distinct corners: usually about the vertex count
            pb.part.vertices.reserve(guess);
            std::size_t cap = 64;
            while (cap < guess * 2) cap <<= 1;
            pb.table.assign(cap, kEmpty);
        }
        auto hashCorner = [](const Corner& c) {   // six 64-bit words and the last 32 bits, each times its own odd constant
            std::uint64_t w[6]; std::uint32_t last;
            std::memcpy(w, &c, sizeof(w)); std::memcpy(&last, (const unsigned char*)&c + sizeof(w), 4);
            std::uint64_t h = w[0] * 0x9E3779B97F4A7C15ull + w[1] * 0xC2B2AE3D27D4EB4Full + w[2] * 0x165667B19E3779F9ull +
                              w[3] * 0xD6E8FEB86659FD93ull + w[4] * 0xFF51AFD7ED558CCDull + w[5] * 0xC4CEB9FE1A85EC53ull +
                              (std::uint64_t)last * 0x9FB21C651E98DF25ull;
            h ^= h >> 32; h *= 0xD6E8FEB86659FD93ull; h ^= h >> 29;
            return h;
        };
        auto addCorner = [&](PartBuild& pb, const Corner& c) {
            std::vector<Corner>& vs = pb.part.vertices;
            if ((vs.size() + 1) * 2 > pb.table.size()) {   // keep the table at most half full: double it and re-insert
                pb.table.assign(pb.table.size() * 2, kEmpty);
                const std::size_t mask = pb.table.size() - 1;
                for (std::size_t v = 0; v < vs.size(); ++v) {
                    std::size_t at = (std::size_t)hashCorner(vs[v]) & mask;
                    while (pb.table[at] != kEmpty) at = (at + 1) & mask;
                    pb.table[at] = (std::uint32_t)v;
                }
            }
            const std::size_t mask = pb.table.size() - 1;
            std::size_t at = (std::size_t)hashCorner(c) & mask;
            for (;;) {
                const std::uint32_t u = pb.table[at];
                if (u == kEmpty) { pb.table[at] = (std::uint32_t)vs.size(); pb.part.indices.push_back((std::uint32_t)vs.size()); vs.push_back(c); return; }
                if (std::memcmp(&vs[u], &c, sizeof(Corner)) == 0) { pb.part.indices.push_back(u); return; }
                at = (at + 1) & mask;
            }
        };
        std::vector<std::array<double, 3>> pts;
        std::vector<int> tris;
        // Corners are made in batches and then looked up in the tables: measured about a third faster on big meshes
        // than looking each one up as it is made (the lookups then do not compete with the reading of the layer arrays).
        const std::size_t kBatch = 2048;
        std::vector<std::pair<PartBuild*, Corner>> pending;
        {
            std::size_t total = 0;
            for (const auto& kv : cornersOfSlot) total += kv.second;
            pending.reserve(std::min(total, kBatch + 512));
        }
        const bool ok = forEachPolygon([&](std::size_t fi, std::size_t start, std::size_t count, bool good) {
            if (count < 3 || !good) return true;
            const int slot = slotOfPolygon(fi, start);
            tris.clear();
            double fn[3] = {0, 0, 0};   // polygon (Newell) normal, used when the file has none
            if (count == 3 && ln.Ok()) { tris.push_back(0); tris.push_back(1); tris.push_back(2); }   // what Triangulate gives
            else {
                pts.clear();
                for (std::size_t k = 0; k < count; ++k) {
                    const std::size_t c = (std::size_t)cpOf(start + k);
                    pts.push_back({pos(c, 0), pos(c, 1), pos(c, 2)});
                }
                if (!ln.Ok()) for (std::size_t k = 0; k < pts.size(); ++k) {
                    const auto& pa = pts[k]; const auto& pb = pts[k + 1 == pts.size() ? 0 : k + 1];
                    fn[0] += (pa[1] - pb[1]) * (pa[2] + pb[2]); fn[1] += (pa[2] - pb[2]) * (pa[0] + pb[0]); fn[2] += (pa[0] - pb[0]) * (pa[1] + pb[1]);
                }
                Triangulate(pts, tris, false);
            }
            if (cornersBuilt + tris.size() > opt.limits.maxCorners) return Fail("mesh too large");
            cornersBuilt += tris.size();
            PartBuild& part = parts[slot]; part.part.slot = slot;
            for (int t : tris) {
                const std::size_t k = (std::size_t)t, pv = start + k;
                const std::size_t cp = (std::size_t)cpOf(pv);
                Corner c;
                for (int a = 0; a < 3; ++a) c.p[a] = (float)pos(cp, a);
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
                pending.push_back({&part, c});
                if (pending.size() >= kBatch) { for (auto& pc : pending) addCorner(*pc.first, pc.second); pending.clear(); }
            }
            return true;
        });
        for (auto& pc : pending) addCorner(*pc.first, pc.second);   // (in the order they were made: the numbering is the same)
        if (!ok) return false;
        for (auto& kv : parts) if (!kv.second.part.indices.empty()) m.parts.push_back(std::move(kv.second.part));

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
                    if (const Array* a = ArrayOf(*cl->e, "Transform")) transform = FromFbx16(*a);
                    cluster.geometryToBone = conv.Scaled(Mul(transform, g2n));
                    const Array* ix = ArrayOf(*cl->e, "Indexes");
                    const Array* wt = ArrayOf(*cl->e, "Weights");
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
                    BlendChannel bc; bc.name = std::string(ch->name); bc.weight = ch->props.Get("DeformPercent", 0.0) * 0.01;
                    const Obj* shape = nullptr;
                    for (const Conn* sc2 : childrenOf[ToId(ch->e->props[0].Num())]) { const Obj* s = Get(sc2->child); if (s && s->cls == "Geometry" && !sc2->propLink) shape = s; }   // last = full weight
                    if (shape) {
                        const Array* ix = ArrayOf(*shape->e, "Indexes");
                        const Array* dv = ArrayOf(*shape->e, "Vertices");
                        const Array* dnm = ArrayOf(*shape->e, "Normals");
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
    struct Layr { Id id; std::string_view name; double weight = 100; int blendMode = 0; };
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
            const Array* kt = ArrayOf(*o.e, "KeyTime");
            const Array* kv = ArrayOf(*o.e, "KeyValueFloat");
            if (!kv) kv = ArrayOf(*o.e, "KeyValueDouble");
            if (!kt || !kv) continue;
            const std::size_t n = std::min(kt->size(), kv->size());
            if (n > opt.limits.maxAnimKeys - totalKeys) return Fail("too many animation keys");
            totalKeys += n;
            const Array* fl = ArrayOf(*o.e, "KeyAttrFlags");
            const Array* da = ArrayOf(*o.e, "KeyAttrDataFloat");
            std::vector<double> daBits; Array daBitsArr;
            if (da && !doc.binary && doc.version >= 7200) {
                // ASCII files from 7.2 on print this array as integers: the 32-bit patterns of the floats (slopes,
                // and packed weights/velocities). Binary files carry real floats; see the same rule in ufbx.
                daBits.reserve(da->size());
                for (std::size_t k = 0; k < da->size(); ++k) { const double d = (*da)[k]; const std::uint32_t u = (d > -4.3e9 && d < 4.3e9) ? (std::uint32_t)(std::int64_t)d : 0u; float f; std::memcpy(&f, &u, 4); daBits.push_back((double)f); }
                daBitsArr.type = 'd'; daBitsArr.count = (std::uint32_t)daBits.size(); daBitsArr.data = (const std::uint8_t*)daBits.data();
                da = &daBitsArr;
            }
            const Array* rc = ArrayOf(*o.e, "KeyAttrRefCount");
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
            struct NodeCN { int layer; int which; const CurveNode* cn; std::size_t hint[3]; };   // which: 0 T, 1 R, 2 S
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
                        perNode[nm->second].push_back({(int)li, which, &cn, {(std::size_t)-1, (std::size_t)-1, (std::size_t)-1}}); span(cn);
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
            Clip clip; clip.name = std::string(st.name);
            const double dur = t1 - t0;
            std::size_t samples = SafeSize(std::max(1.0, std::ceil(dur * opt.bakeRate)));   // a hostile LocalStop makes dur huge: saturate, then the limits below apply
            if (samples >= opt.limits.maxBakedSamples) samples = opt.limits.maxBakedSamples - 1;   // (not samples + 1 > max: that wraps for a saturated count)
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
            auto evalVec = [&](std::vector<NodeCN>& list, int which, const double base[3], double time, double out[3]) {
                for (int k = 0; k < 3; ++k) out[k] = base[k];
                bool first = true;
                for (NodeCN& n : list) {
                    if (n.which != which) continue;
                    double lv[3];
                    for (int k = 0; k < 3; ++k) lv[k] = n.cn->curve[k] ? n.cn->curve[k]->Eval(time + t0, n.hint[k]) : n.cn->def[k];
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
                const FixedRot fixed = FixedRotations(base);   // samples change only t, r and s
                na.translation.reserve(times.size()); na.rotation.reserve(times.size()); na.scale.reserve(times.size());
                // A sample whose translation, rotation and scaling are the same (bit for bit) as the previous sample's has
                // the same result: it is copied. The rotation matrix alone is reused the same way.
                XProps x = base;
                double prevT[3], prevR[3], prevS[3], tr[3], q[4], s[3];
                bool havePrev = false, haveR = false;
                Mat R;
                const bool isRoot = sc.nodes[(std::size_t)kv.first].parent < 0;
                for (double t : times) {
                    evalVec(kv.second, 0, base.t, t, x.t); evalVec(kv.second, 1, base.r, t, x.r); evalVec(kv.second, 2, base.s, t, x.s);
                    const bool sameR = haveR && std::memcmp(prevR, x.r, sizeof(prevR)) == 0;
                    if (!(havePrev && sameR && std::memcmp(prevT, x.t, sizeof(prevT)) == 0 && std::memcmp(prevS, x.s, sizeof(prevS)) == 0)) {
                        if (!sameR) { R = Euler(x.r, x.order); std::memcpy(prevR, x.r, sizeof(prevR)); haveR = true; }
                        const bool simple = fixed.simple && Small3(x.t) && Small3(x.r) && Small3(x.s);
                        const Mat local = conv.Local(simple ? LocalSimple(x, fixed, R) : LocalMatrix(x, fixed, R), isRoot);
                        Decompose(local, tr, q, s);
                        std::memcpy(prevT, x.t, sizeof(prevT)); std::memcpy(prevS, x.s, sizeof(prevS)); havePrev = true;
                    }
                    Key3 kt; kt.time = t; for (int k = 0; k < 3; ++k) kt.v[k] = tr[k];
                    Key4 kq; kq.time = t; for (int k = 0; k < 4; ++k) kq.v[k] = q[k];
                    Key3 ks; ks.time = t; for (int k = 0; k < 3; ++k) ks.v[k] = s[k];
                    na.translation.push_back(kt); na.rotation.push_back(kq); na.scale.push_back(ks);
                }
                clip.nodes.push_back(std::move(na));
            }
            for (const BlendCN& b : blendCns) {
                BlendAnim ba; ba.mesh = chanOfObj[b.target].first; ba.channel = chanOfObj[b.target].second;
                std::size_t blendHint = (std::size_t)-1;
                for (double t : times) {
                    Key3 k; k.time = t; k.v[1] = k.v[2] = 0;
                    k.v[0] = b.cn->curve[0] ? b.cn->curve[0]->Eval(t + t0, blendHint) : b.cn->def[0];
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
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) { if (err) *err = "cannot open " + path; return false; }
    // One read of the whole file at its size (a byte-by-byte stream iterator grew the buffer by doubling, slowly).
    std::vector<std::uint8_t> buf;
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    f.seekg(0, std::ios::beg);
    if (size > 0) {
        buf.resize((std::size_t)size);
        f.read((char*)buf.data(), (std::streamsize)size);
        buf.resize((std::size_t)f.gcount());
    } else {   // size unknown (not a regular file): fall back to reading until the end
        f.clear(); f.seekg(0, std::ios::beg);
        buf.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
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
