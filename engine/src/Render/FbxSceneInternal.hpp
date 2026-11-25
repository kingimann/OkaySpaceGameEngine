#pragma once
// Private to the engine: FbxScene.cpp's types and helpers (the FBX element tree -> Scene builder). FbxScene.cpp keeps the polygon
// import step and the public entry points; FbxSceneClips.cpp has the animation reader and the clip bake.
#include "okay/Core/SafeCast.hpp"
#include "okay/Core/Base64.hpp"
#include "okay/Render/FbxReader.hpp"
#include "okay/Core/ThirdParty.hpp"
#include <algorithm>
#include <array>
#include <iterator>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace okay {
namespace fbx {
namespace scene_build {

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
inline Mat Mul(const Mat& a, const Mat& b) {
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
inline Mat Translate(double x, double y, double z) { Mat r; r.m[3] = x; r.m[7] = y; r.m[11] = z; return r; }
inline Mat ScaleM(double x, double y, double z) { Mat r; r.m[0] = x; r.m[5] = y; r.m[10] = z; return r; }
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
inline Mat RotAxis(int axis, double deg) {
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
inline Mat RotateLeft(int axis, double deg, const Mat& r, bool known = false) {   // known: r is affine and finite
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
/// RotateLeft(axis, deg, r, true) written back into r: the same sums in the same order (each column of the result
/// reads only the same column of r), without building the rotation matrix and a second matrix. The rotation's entries
/// are RotAxis's: (p,p) = (q,q) = c; (p,q) = -s and (q,p) = s for X and Z, the other way round for Y.
inline void RotateLeftInPlace(int axis, double deg, Mat& r) {
    const double a = deg * kPi / 180.0, c = std::cos(a), s = std::sin(a);
    const int p = axis == 0 ? 1 : 0, q = axis == 2 ? 1 : 2;
    const double apq = axis == 1 ? s : -s, aqp = axis == 1 ? -s : s;
    double* m = r.m;
    for (int j = 0; j < 4; ++j) {
        const double rp = m[p * 4 + j], rq = m[q * 4 + j];
        double s0 = 0; s0 += m[axis * 4 + j]; m[axis * 4 + j] = s0;
        double sp = 0; sp += c * rp; sp += apq * rq; m[p * 4 + j] = sp;
        double sq = 0; sq += aqp * rp; sq += c * rq; m[q * 4 + j] = sq;
    }
}
inline Mat Euler(const double d[3], int order) {
    static const int seq[6][3] = {{0,1,2},{0,2,1},{1,2,0},{1,0,2},{2,0,1},{2,1,0}};
    if (order < 0 || order > 5) order = 0;
    // With small finite angles every step is a rotation of finite numbers (no check needed per step).
    Mat r;
    if (Small3(d)) {
        for (int k = 0; k < 3; ++k) RotateLeftInPlace(seq[order][k], d[seq[order][k]], r);
        return r;
    }
    for (int k = 0; k < 3; ++k) r = RotateLeft(seq[order][k], d[seq[order][k]], r, false);
    return r;
}
inline bool Invert(const Mat& a, Mat& out) {
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
inline Mat InvertOrIdentity(const Mat& a) { Mat r; if (!Invert(a, r)) r = Mat(); return r; }

/// T * R * S pieces from a matrix (no shear handling). Rotation as a quaternion x y z w.
inline void Decompose(const Mat& a, double t[3], double q[4], double s[3]) {
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
inline Mat FromFbx16(const Array& a) {
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
            if (p.props.size() < 5 || p.name != "P" || p.props[0].type != 'S' || p.props[0].s() != n) continue;
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
inline void LoadProps(const Elem& e, Props& out) { out.p70 = e.Find("Properties70"); }

inline std::string_view CleanName(std::string_view raw) {
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

// Base64 to bytes (RFC 4648 alphabet), replacing `out`: okay/Core/Base64.hpp's lenient decoder (characters outside the
// alphabet, '=' and line ends included, are skipped; four alphabet characters in a row go through its fast path).
inline void Base64Decode(std::string_view in, std::vector<std::uint8_t>& out) {
    out.clear();
    base64::DecodeLenient(in.data(), in.size(), out);
}

// ---------------------------------------------------------------------------
// geometry layer elements
// ---------------------------------------------------------------------------
enum Mapping { kByPolyVert, kByVertex, kByPoly, kAllSame, kByEdge, kNoMap };
struct Layer {
    Mapping map = kNoMap; bool indexed = false; int dim = 1;
    const Array* vals = nullptr; const Array* idx = nullptr;
    const Prop* valsProp = nullptr; const Prop* idxProp = nullptr;   // the properties `vals` and `idx` are in (reader only)
    bool Ok() const { return map != kNoMap && vals; }
};
inline const Prop* ArrayPropOf(const Elem& e, const char* name) {
    const Elem* c = e.Find(name);
    return (c && !c->props.empty() && c->props[0].type == 'a') ? &c->props[0] : nullptr;
}
inline const Array* ArrayOf(const Elem& e, const char* name) { const Prop* p = ArrayPropOf(e, name); return p ? &p->arr : nullptr; }
/// The layer an element describes (`best`: one LayerElementNormal / UV / Color / Material child of the geometry).
inline Layer LayerOfElem(const Elem* best, const char* valName, const char* idxName, int dim) {
    Layer L; L.dim = dim;
    if (!best) return L;
    std::string_view mp, ref;
    if (const Elem* m = best->Find("MappingInformationType")) if (!m->props.empty()) mp = m->props[0].s();
    if (const Elem* r = best->Find("ReferenceInformationType")) if (!r->props.empty()) ref = r->props[0].s();
    if (mp == "ByPolygonVertex") L.map = kByPolyVert;
    else if (mp == "ByVertice" || mp == "ByVertex" || mp == "ByControlPoint") L.map = kByVertex;
    else if (mp == "ByPolygon") L.map = kByPoly;
    else if (mp == "AllSame") L.map = kAllSame;
    else if (mp == "ByEdge") L.map = kByEdge;
    else return L;
    L.valsProp = ArrayPropOf(*best, valName); L.vals = L.valsProp ? &L.valsProp->arr : nullptr;
    L.indexed = (ref == "IndexToDirect" || ref == "Index");
    if (L.indexed) { L.idxProp = ArrayPropOf(*best, idxName); L.idx = L.idxProp ? &L.idxProp->arr : nullptr; if (!L.idx) L.indexed = false; }
    if (L.indexed && !L.vals && std::string(valName) != idxName) { L.vals = nullptr; L.valsProp = nullptr; }
    return L;
}
/// Every `layerName` child of the geometry, ordered by its index (the element's first property; a missing one counts
/// as 0), elements with equal indices in file order. A geometry has one such element per layer (FBX "layers": set 0,
/// set 1, ...), so for UVs this is the list of UV sets.
inline std::vector<const Elem*> LayerElems(const Elem& geo, const char* layerName) {
    std::vector<std::pair<double, const Elem*>> found;
    for (const Elem& c : geo.children)
        if (c.name == layerName) found.push_back({(!c.props.empty() && c.props[0].IsNumber()) ? c.props[0].Num() : 0, &c});
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<const Elem*> out; out.reserve(found.size());
    for (const auto& f : found) out.push_back(f.second);
    return out;
}
/// The first layer of a kind: the element with the lowest index (the first in the file among equal ones).
inline Layer LoadLayer(const Elem& geo, const char* layerName, const char* valName, const char* idxName, int dim) {
    const Elem* best = nullptr; double bestIdx = 1e30;
    for (const Elem& c : geo.children) {
        if (c.name != layerName) continue;
        const double k = (!c.props.empty() && c.props[0].IsNumber()) ? c.props[0].Num() : 0;
        if (!best || k < bestIdx) { best = &c; bestIdx = k; }
    }
    return LayerOfElem(best, valName, idxName, dim);
}
/// A layer element's "Name" (a UV set's name), "" when it has none.
inline std::string LayerName(const Elem* e) {
    if (!e) return std::string();
    const Elem* n = e->Find("Name");
    return (n && !n->props.empty()) ? std::string(n->props[0].s()) : std::string();
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
inline void Triangulate(const std::vector<std::array<double, 3>>& pts, std::vector<int>& tris, bool flip) {
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

/// Corner c of a PolygonVertexIndex array: the control point (or -1 when not below `ncp`), `end` set on a polygon's last
/// corner. A non-int32 array must have been range checked (BuildMesh / BuildParts do that first).
inline std::int32_t PolyCorner(const Array& pvi, std::size_t i, std::size_t ncp, bool& end) {
    std::int64_t v;
    if (pvi.type == 'i') { std::int32_t x; std::memcpy(&x, pvi.data + i * 4, 4); v = x; }
    else v = (std::int64_t)pvi[i];
    end = v < 0;
    const std::int64_t c = end ? ~v : v;
    return (c >= 0 && (std::size_t)c < ncp) ? (std::int32_t)c : -1;
}
inline bool PolygonIndicesInRange(const Array& pvi) {
    if (pvi.type == 'i') return true;
    for (std::size_t i = 0; i < pvi.size(); ++i) {
        const double d = pvi[i];
        if (!(d > -2147483649.0 && d < 2147483648.0)) return false;
    }
    return true;
}
/// The triangle corners the polygons give: (n - 2) * 3 for each polygon of n >= 3 corners that are all below `ncp`
/// (the polygons BuildParts triangulates; the others are skipped). A last polygon without its end marker counts.
inline std::uint64_t PolygonCorners(const Array& pvi, std::size_t ncp) {
    std::uint64_t total = 0;
    std::size_t start = 0; bool good = true;
    const std::size_t npv = pvi.size();
    auto close = [&](std::size_t count) { if (count >= 3 && good) total += (std::uint64_t)(count - 2) * 3; };
    if (pvi.type == 'i') {   // the usual type, read directly (the same as PolyCorner)
        for (std::size_t i = 0; i < npv; ++i) {
            std::int32_t x; std::memcpy(&x, pvi.data + i * 4, 4);
            const bool end = x < 0;
            const std::int64_t c = end ? ~(std::int64_t)x : (std::int64_t)x;
            if ((std::uint64_t)c >= (std::uint64_t)ncp) good = false;
            if (end) { close(i + 1 - start); start = i + 1; good = true; }
        }
    } else {
        for (std::size_t i = 0; i < npv; ++i) {
            bool end; if (PolyCorner(pvi, i, ncp, end) < 0) good = false;
            if (end) { close(i + 1 - start); start = i + 1; good = true; }
        }
    }
    if (start < npv) close(npv - start);
    return total;
}

// ---------------------------------------------------------------------------
// animation keys at the file's own key times (BuildOptions::fileKeys)
// ---------------------------------------------------------------------------
// Written for this engine from first principles (cubic interpolation, quaternion algebra); docs/content/model_readers.md (FBX part),
// "Animation keys".

/// The smallest and largest value on [0, 1] of the cubic through (0, y0), (1/3, y1), (2/3, y2), (1, y3). Newton's
/// forward differences in s = 3u: p(s) = y0 + d1 s + d2 s(s-1)/2 + d3 s(s-1)(s-2)/6; the extremes inside are where
/// p'(s) = (d3/2) s^2 + (d2 - d3) s + (d1 - d2/2 + d3/3) is zero. Between two neighbouring key times every curve of a
/// node is one cubic piece (or a line or a constant), so this is exact for the curves' own values.
inline void CubicMinMax(double y0, double y1, double y2, double y3, double& lo, double& hi) {
    if (y0 == y1 && y1 == y2 && y2 == y3) { lo = hi = y0; return; }   // a held value (most channels)
    const double d1 = y1 - y0, d2 = y2 - 2 * y1 + y0, d3 = y3 - 3 * y2 + 3 * y1 - y0;
    lo = std::min(std::min(y0, y3), std::min(y1, y2)); hi = std::max(std::max(y0, y3), std::max(y1, y2));
    const double A = d3 * 0.5, B = d2 - d3, C = d1 - d2 * 0.5 + d3 / 3.0;
    double r[2]; int nr = 0;
    if (A != 0) {
        const double disc = B * B - 4 * A * C;
        if (disc >= 0) { const double sq = std::sqrt(disc); r[nr++] = (-B - sq) / (2 * A); r[nr++] = (-B + sq) / (2 * A); }
    } else if (B != 0) r[nr++] = -C / B;
    for (int i = 0; i < nr; ++i) {
        const double sv = r[i];
        if (!(sv > 0 && sv < 3)) continue;
        const double v = y0 + d1 * sv + d2 * sv * (sv - 1) * 0.5 + d3 * sv * (sv - 1) * (sv - 2) / 6.0;
        lo = std::min(lo, v); hi = std::max(hi, v);
    }
}
/// The largest |e(u)| on [0, 1] of the cubic that is 0 at both ends and e1, e2 at 1/3 and 2/3: the distance between a
/// cubic piece and the straight line through its two ends.
inline double CubicDeviation(double e1, double e2) { if (e1 == 0 && e2 == 0) return 0; double lo, hi; CubicMinMax(0, e1, e2, 0, lo, hi); return std::max(-lo, hi); }

/// Quaternion (x y z w) product a * b (rotation b first, then a).
inline void QuatMul(const double a[4], const double b[4], double o[4]) {
    const double x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    const double y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    const double z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    const double w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    o[0] = x; o[1] = y; o[2] = z; o[3] = w;
}
/// The rotation Euler(d, order) as a unit quaternion: the three axis rotations in the same order (first letter first).
inline void EulerQuat(const double d[3], int order, double q[4]) {
    static const int seq[6][3] = {{0,1,2},{0,2,1},{1,2,0},{1,0,2},{2,0,1},{2,1,0}};
    if (order < 0 || order > 5) order = 0;
    q[0] = q[1] = q[2] = 0; q[3] = 1;
    for (int k = 0; k < 3; ++k) {
        const int ax = seq[order][k];
        const double h = d[ax] * kPi / 360.0;
        double a[4] = {0, 0, 0, std::cos(h)}; a[ax] = std::sin(h);
        QuatMul(a, q, q);
    }
}
/// Degrees between `truth` and what the engine plays at u between keys qa and qb: the components blended linearly
/// (qb's sign first matched to qa's, as the importer does) and normalised.
inline double NlerpErrorDeg(const double qa[4], const double qb[4], double u, const double truth[4]) {
    const double sg = (qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3]) < 0 ? -1.0 : 1.0;
    double l[4], n2 = 0, t2 = 0, d = 0;
    for (int k = 0; k < 4; ++k) { l[k] = qa[k] + (sg * qb[k] - qa[k]) * u; n2 += l[k] * l[k]; t2 += truth[k] * truth[k]; }
    for (int k = 0; k < 4; ++k) d += l[k] * truth[k];
    if (!(n2 > 0 && t2 > 0)) return 0;
    const double c = std::min(1.0, std::fabs(d) / std::sqrt(n2 * t2));
    return 2.0 * std::acos(c) * 180.0 / kPi;
}

// ---------------------------------------------------------------------------
// animation curves
// ---------------------------------------------------------------------------
/// A read-only run of values kept elsewhere: a Curve's arrays are runs in a few pooled arrays that the Animator owns
/// (Builder::Animation), so a file with thousands of curves makes a handful of allocations instead of several per curve.
template <class T> struct Run {
    const T* p = nullptr; std::size_t n = 0;
    std::size_t size() const { return n; }
    bool empty() const { return n == 0; }
    const T& operator[](std::size_t i) const { return p[i]; }
    const T* begin() const { return p; }
    const T* end() const { return p + n; }
    const T& front() const { return p[0]; }
    const T& back() const { return p[n - 1]; }
};

struct Curve {
    Run<double> t, v;                   // seconds, value
    // Flags and slopes belong to the file's attribute sets (KeyAttrFlags, KeyAttrDataFloat), which several keys share
    // (often all of them): they are kept per set, and each key names its set. The values are the same as one copy per
    // key would hold.
    Run<std::uint32_t> set;             // per key: its attribute set; empty when every key uses set 0
    Run<int> setFlags;                  // per set (at least one entry once the curve has keys)
    Run<double> setR, setL;             // per set: right slope, next-left slope (value/sec); empty when all are 0
    bool hasSlopes = false;             // the file carried KeyAttrDataFloat (slopes of auto keys are in it too)
    bool Empty() const { return t.empty(); }
    std::size_t SetOf(std::size_t i) const { return set.empty() ? 0 : set[i]; }
    int FlagsOf(std::size_t i) const { return setFlags[SetOf(i)]; }
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
        const std::size_t sl = SetOf(lo);
        const int f = setFlags[sl];
        if (f & 0x2) return v[lo];                                   // constant (step)
        const double u = (time - t[lo]) / dt;
        if (f & 0x4) return v[lo] + (v[hi] - v[lo]) * u;              // linear
        // cubic Hermite from the key tangents
        double m0 = setR.empty() ? 0.0 : setR[sl], m1 = setL.empty() ? 0.0 : setL[sl];   // (empty: no key had stored slopes, all 0)
        if ((f & 0x300) && !hasSlopes) {                              // auto / TCB without stored slopes: derive from the neighbours
            m0 = AutoSlope(lo, f); m1 = AutoSlope(hi, FlagsOf(hi));
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

/// Object id -> value, open addressing in flat arrays (no allocation per entry, unlike a node-based map; scenes have
/// tens of thousands of objects). Nothing iterates it, so the layout never shows in the output.
template <class V> struct IdMap {
    std::vector<Id> keys; std::vector<V> vals; std::vector<unsigned char> used; std::size_t n = 0;
    static std::size_t Hash(Id id) { std::uint64_t h = (std::uint64_t)id * 0x9E3779B97F4A7C15ull; return (std::size_t)(h ^ (h >> 29)); }
    std::size_t Slot(Id id) const {
        const std::size_t mask = keys.size() - 1;
        std::size_t at = Hash(id) & mask;
        while (used[at] && keys[at] != id) at = (at + 1) & mask;
        return at;
    }
    void Rehash(std::size_t cap) {
        std::vector<Id> k(cap); std::vector<V> v(cap); std::vector<unsigned char> u(cap, 0);
        k.swap(keys); v.swap(vals); u.swap(used);
        for (std::size_t i = 0; i < k.size(); ++i) if (u[i]) { const std::size_t s = Slot(k[i]); used[s] = 1; keys[s] = k[i]; vals[s] = std::move(v[i]); }
    }
    void reserve(std::size_t want) { std::size_t cap = 16; while (cap < want * 2) cap <<= 1; if (cap > keys.size()) Rehash(cap); }
    const V* find(Id id) const { if (keys.empty()) return nullptr; const std::size_t s = Slot(id); return used[s] ? &vals[s] : nullptr; }
    V* find(Id id) { if (keys.empty()) return nullptr; const std::size_t s = Slot(id); return used[s] ? &vals[s] : nullptr; }
    bool count(Id id) const { return find(id) != nullptr; }
    V& operator[](Id id) {
        if ((n + 1) * 2 > keys.size()) Rehash(keys.empty() ? 16 : keys.size() * 2);
        const std::size_t s = Slot(id);
        if (!used[s]) { used[s] = 1; keys[s] = id; vals[s] = V(); ++n; }
        return vals[s];
    }
};

/// The connections grouped by parent and by child, in file order within each group (what a map of vectors held),
/// as two flat arrays and a range per id.
struct ConnSpan {
    const Conn* const* b = nullptr; const Conn* const* e = nullptr;
    const Conn* const* begin() const { return b; }
    const Conn* const* end() const { return e; }
    bool empty() const { return b == e; }
};

struct Builder {
    const Document& doc; Scene& sc; const BuildOptions& opt; std::string err;
    std::vector<Obj> objs; IdMap<std::size_t> byId;
    std::vector<Conn> conns;
    struct Range { std::uint32_t begin = 0, count = 0; };
    std::vector<const Conn*> byParent, byChild;
    IdMap<Range> parentRange, childRange;
    ConnSpan ChildrenOf(Id id) const {   // connections whose parent is `id`
        const Range* r = parentRange.find(id);
        return r ? ConnSpan{byParent.data() + r->begin, byParent.data() + r->begin + r->count} : ConnSpan{};
    }
    ConnSpan ParentsOf(Id id) const {    // connections whose child is `id`
        const Range* r = childRange.find(id);
        return r ? ConnSpan{byChild.data() + r->begin, byChild.data() + r->begin + r->count} : ConnSpan{};
    }
    Conv conv;
    IdMap<int> nodeOfModel, texOfObj, matOfObj, meshOfGeom;
    std::vector<Id> nodeModelId;
    std::size_t cornersBuilt = 0;

    Builder(const Document& d, Scene& s, const BuildOptions& o) : doc(d), sc(s), opt(o) {}
    bool Fail(const std::string& m) { if (err.empty()) err = m; return false; }
    const Obj* Get(Id id) const { const std::size_t* i = byId.find(id); return i ? &objs[*i] : nullptr; }

    bool Index() {
        const Elem* o = doc.root.Find("Objects");
        if (!o) return Fail("no Objects section (the file holds no scene)");
        {
            std::size_t count = 0;
            for (const Elem& e : o->children) { (void)e; ++count; }
            byId.reserve(std::min<std::size_t>(count, opt.limits.maxElements + 1));
            objs.reserve(std::min<std::size_t>(count, opt.limits.maxElements + 1));
        }
        for (const Elem& e : o->children) {
            if (e.props.empty() || !e.props[0].IsNumber()) continue;
            Obj ob; ob.e = &e; ob.cls = e.name;
            const Id id = ToId(e.props[0].Num());
            if (e.props.size() > 1 && e.props[1].type == 'S') ob.name = CleanName(e.props[1].s());
            if (e.props.size() > 2 && e.props[2].type == 'S') ob.sub = e.props[2].s();
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
            cn.propLink = e.props[0].s() == "OP";
            if (e.props.size() > 3 && e.props[3].type == 'S') cn.prop = e.props[3].s();
            conns.push_back(std::move(cn));
        }
        // Group by parent and by child: count per id, give each id its range, then place the connections in order.
        auto group = [&](bool byParentId, std::vector<const Conn*>& out, IdMap<Range>& ranges) {
            ranges.reserve(conns.size());
            for (const Conn& cn : conns) ++ranges[byParentId ? cn.parent : cn.child].count;
            std::uint32_t at = 0;
            for (std::size_t i = 0; i < ranges.keys.size(); ++i) if (ranges.used[i]) { ranges.vals[i].begin = at; at += ranges.vals[i].count; ranges.vals[i].count = 0; }
            out.assign(conns.size(), nullptr);
            for (const Conn& cn : conns) { Range& r = *ranges.find(byParentId ? cn.parent : cn.child); out[r.begin + r.count++] = &cn; }
        };
        group(true, byParent, parentRange);
        group(false, byChild, childRange);
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
        if (const Elem* h = doc.root.Find("FBXHeaderExtension")) if (const Elem* cr = h->Find("Creator")) if (!cr->props.empty()) sc.creator = std::string(cr->props[0].s());
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
            if (const Elem* f = o.e->Find("FileName")) if (!f->props.empty()) t.fileName = std::string(f->props[0].s());
            if (const Elem* f = o.e->Find("RelativeFilename")) if (!f->props.empty()) t.relativeFileName = std::string(f->props[0].s());
            const Id id = ToId(o.e->props[0].Num());
            for (const Conn* c : ChildrenOf(id)) {   // children of the texture: Video
                const Obj* v = Get(c->child);
                if (!v || v->cls != "Video" || c->propLink) continue;
                if (t.fileName.empty()) if (const Elem* f = v->e->Find("FileName")) if (!f->props.empty()) t.fileName = std::string(f->props[0].s());
                if (t.relativeFileName.empty()) if (const Elem* f = v->e->Find("RelativeFilename")) if (!f->props.empty()) t.relativeFileName = std::string(f->props[0].s());
                if (const Elem* ct = v->e->Find("Content")) if (!ct->props.empty()) {
                    const Prop& p = ct->props[0];
                    if (p.type == 'R') t.content.assign(p.s().begin(), p.s().end());
                    else if (p.type == 'S' && !p.s().empty()) Base64Decode(p.s(), t.content);
                }
            }
            // A Video that carries the picture while the one connected to the texture does not (some exporters write the pair
            // apart): found by file name, as ufbx does. Only when the texture got no content from its own Video.
            if (t.content.empty() && (!t.fileName.empty() || !t.relativeFileName.empty())) {
                for (const Obj& vo : objs) {
                    if (vo.cls != "Video") continue;
                    const Elem* ct = vo.e->Find("Content");
                    if (!ct || ct->props.empty() || ct->props[0].s().empty()) continue;
                    const Elem* vf = vo.e->Find("FileName"); const Elem* vr = vo.e->Find("RelativeFilename");
                    const bool same = (vf && !vf->props.empty() && !t.fileName.empty() && vf->props[0].s() == t.fileName) ||
                                      (vr && !vr->props.empty() && !t.relativeFileName.empty() && vr->props[0].s() == t.relativeFileName);
                    if (!same) continue;
                    const Prop& p = ct->props[0];
                    if (p.type == 'R') t.content.assign(p.s().begin(), p.s().end());
                    else if (p.type == 'S') Base64Decode(p.s(), t.content);
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
            if (const Elem* sm = o.e->Find("ShadingModel")) if (!sm->props.empty()) shadingModel = std::string(sm->props[0].s());
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
            for (const Conn* c : ChildrenOf(id)) {   // textures plugged into this material
                const Obj* t = Get(c->child);
                if (!t || t->cls != "Texture" || !c->propLink) continue;
                const int* tex = texOfObj.find(c->child);
                if (!tex) continue;
                if (pbr) {
                    const std::string pre = pbr->prefix;
                    if (isOneOf(c->prop, pbr->baseColor, pre)) { if (m.diffuseTexture < 0) m.diffuseTexture = *tex; continue; }
                    if (isOneOf(c->prop, pbr->normal, pre)) { if (m.normalTexture < 0) m.normalTexture = *tex; continue; }
                    if (isOneOf(c->prop, pbr->ao, pre)) { if (m.aoTexture < 0) m.aoTexture = *tex; continue; }
                }
                if (c->prop == "DiffuseColor" || c->prop == "Diffuse") { if (m.diffuseTexture < 0) m.diffuseTexture = *tex; }
                else if (c->prop == "NormalMap") { if (m.normalTexture < 0) m.normalTexture = *tex; }
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
        // Everything below is per model, by its position in `models` (flat arrays instead of maps keyed by id).
        const std::size_t nm = models.size();
        std::vector<Id> idOf(nm);
        IdMap<std::uint32_t> modelOf; modelOf.reserve(nm);
        for (std::size_t k = 0; k < nm; ++k) { idOf[k] = ToId(objs[models[k]].e->props[0].Num()); modelOf[idOf[k]] = (std::uint32_t)k; }
        // parent of each model (first Model parent wins; id 0 = scene root)
        std::vector<Id> parentOf(nm, 0);
        for (std::size_t k = 0; k < nm; ++k) {
            const Id id = idOf[k];
            Id par = 0;
            for (const Conn* c : ParentsOf(id)) {
                if (c->propLink) continue;
                if (c->parent == 0) { par = 0; break; }
                const Obj* p = Get(c->parent);
                if (p && p->cls == "Model" && c->parent != id) { par = c->parent; break; }
            }
            parentOf[k] = par;
        }
        // children in file order (grouped by parent model); roots first, then anything caught in a parent cycle
        std::vector<std::uint32_t> kidBegin(nm + 1, 0), kids;
        for (std::size_t k = 0; k < nm; ++k) if (parentOf[k] != 0) ++kidBegin[*modelOf.find(parentOf[k]) + 1];
        for (std::size_t k = 0; k < nm; ++k) kidBegin[k + 1] += kidBegin[k];
        kids.resize(kidBegin[nm]);
        {
            std::vector<std::uint32_t> fill(kidBegin.begin(), kidBegin.end() - 1);
            for (std::size_t k = 0; k < nm; ++k) if (parentOf[k] != 0) kids[fill[*modelOf.find(parentOf[k])]++] = (std::uint32_t)k;
        }
        std::vector<std::uint32_t> rootIds, order;
        for (std::size_t k = 0; k < nm; ++k) if (parentOf[k] == 0) rootIds.push_back((std::uint32_t)k);
        std::vector<int> depth(nm, 0); std::vector<unsigned char> seen(nm, 0);
        std::vector<std::uint32_t> stack;
        auto walk = [&](std::uint32_t start) -> bool {
            stack.assign(1, start); depth[start] = 1;
            while (!stack.empty()) {
                const std::uint32_t cur = stack.back(); stack.pop_back();
                if (seen[cur]) continue;
                seen[cur] = 1;
                order.push_back(cur);
                if (depth[cur] > (int)opt.limits.maxNodeDepth) return false;
                for (std::uint32_t j = kidBegin[cur + 1]; j-- > kidBegin[cur];) { depth[kids[j]] = depth[cur] + 1; stack.push_back(kids[j]); }
            }
            return true;
        };
        for (std::uint32_t r : rootIds) if (!walk(r)) return Fail("node hierarchy too deep");
        for (std::size_t k = 0; k < nm; ++k) {   // cycle members: promote to roots
            if (!seen[k]) { parentOf[k] = 0; rootIds.push_back((std::uint32_t)k); if (!walk((std::uint32_t)k)) return Fail("node hierarchy too deep"); }
        }
        // `order` is depth-first pre-order per root already, but a promoted cycle root may have been walked
        // with stale parents: recompute final parents from visit order.
        sc.nodes.resize(order.size()); xp.resize(order.size()); nodeModelId.resize(order.size());
        nodeOfModel.reserve(order.size());
        std::vector<int> nodeOfIndex(nm, 0);
        for (std::size_t i = 0; i < order.size(); ++i) { nodeModelId[i] = idOf[order[i]]; nodeOfModel[idOf[order[i]]] = (int)i; nodeOfIndex[order[i]] = (int)i; }
        for (std::size_t i = 0; i < order.size(); ++i) {
            const std::uint32_t k = order[i];
            SceneNode& n = sc.nodes[i];
            const Obj& o = objs[models[k]];
            n.name = std::string(o.name);
            const Id par = parentOf[k];
            n.parent = par == 0 ? -1 : nodeOfIndex[*modelOf.find(par)];
            if (n.parent >= (int)i) n.parent = -1;           // (cycle promoted) keep parents before children
            if (n.parent < 0) sc.roots.push_back((int)i); else sc.nodes[(std::size_t)n.parent].children.push_back((int)i);
            XProps& x = xp[i];
            ReadXProps(o.props, x);
        }
        // a first pass for world matrices happens in Finish()
        return true;
    }

    /// The transform properties of a model in one pass over its P records. The same values as one Props lookup per
    /// name: for each name, the last record with at least one number wins.
    static void ReadXProps(const Props& p, XProps& x) {
        enum { kT, kR, kS, kPre, kPost, kRoff, kRp, kSoff, kSp, kGt, kGr, kGs, kOrder, kCount };
        static const char* const names[kCount] = {"Lcl Translation", "Lcl Rotation", "Lcl Scaling", "PreRotation", "PostRotation",
            "RotationOffset", "RotationPivot", "ScalingOffset", "ScalingPivot", "GeometricTranslation", "GeometricRotation",
            "GeometricScaling", "RotationOrder"};
        static const std::string_view views[kCount] = {names[0], names[1], names[2], names[3], names[4], names[5], names[6],
            names[7], names[8], names[9], names[10], names[11], names[12]};
        const Elem* hit[kCount] = {};
        if (p.p70) for (const Elem& e : p.p70->children) {
            if (e.props.size() < 5 || e.name != "P" || e.props[0].type != 'S') continue;
            const std::string_view n = e.props[0].s();
            int which = -1;
            for (int k = 0; k < kCount; ++k) if (n == views[k]) { which = k; break; }
            if (which < 0) continue;
            bool number = false;
            for (std::size_t i = 4; i < e.props.size() && !number; ++i) number = e.props[i].IsNumber();
            if (number) hit[which] = &e;
        }
        auto vec3 = [&](int k, double out[3], double def) {
            double v[3]; const int got = Props::Numbers(hit[k], v, 3);
            for (int i = 0; i < 3; ++i) out[i] = i < got ? v[i] : def;
        };
        vec3(kT, x.t, 0); vec3(kR, x.r, 0); vec3(kS, x.s, 1);
        vec3(kPre, x.pre, 0); vec3(kPost, x.post, 0);
        vec3(kRoff, x.roff, 0); vec3(kRp, x.rp, 0);
        vec3(kSoff, x.soff, 0); vec3(kSp, x.sp, 0);
        vec3(kGt, x.gt, 0); vec3(kGr, x.gr, 0); vec3(kGs, x.gs, 1);
        double ord; x.order = SafeInt(Props::Numbers(hit[kOrder], &ord, 1) == 1 ? ord : 0);
    }

    void SetLocal(std::size_t i, const Mat& rawLocal) {
        SceneNode& n = sc.nodes[i];
        n.local = conv.Local(rawLocal, n.parent < 0);
        Decompose(n.local, n.translation, n.rotation, n.scale);
    }

    // ---- meshes -------------------------------------------------------------
    // The reader keeps a mesh's polygons as the file stores them (positions, polygon index list, layers); the import
    // turns them into triangles and shared corners with BuildParts (below the Builder).
    Document* steal = nullptr;   // LoadMemory / LoadFile: the Document's decoded buffers may be handed to the scene
    /// The buffers one mesh keeps, in one allocation (shared by its KeptArrays; freed with the last of them).
    struct MeshStore {
        ByteBuffer bytes[12]; NumberBuffer nums[12]; int nb = 0, nn = 0;   // (a mesh usually keeps at most 11 arrays; extra UV sets can add more, and those are copied by the fallback below)
    };
    std::shared_ptr<MeshStore> store;   // the mesh being built
    struct KeptBuf { const void* from; const std::uint8_t* data; };
    std::vector<KeptBuf> keptHere;      // arrays already kept by the mesh being built (the material layer uses one twice)
    void BeginMeshStore() { store = std::make_shared<MeshStore>(); keptHere.clear(); }
    /// Keeps array property `p` (an 'a' property, or null) for the scene: the Document's own buffer is moved into the
    /// mesh's store when the build may take it (steal), otherwise the bytes are copied.
    KeptArray Keep(const Prop* p) {
        KeptArray k;
        if (!p) return k;
        const Array* a = &p->arr;
        k.present = true; k.a = *a;
        if (a->count == 0 || !a->data) { k.a.data = nullptr; return k; }
        for (const KeptBuf& kb : keptHere) if (kb.from == a->data) { k.a.data = kb.data; k.keep = store; return k; }
        MeshStore& st = *store;
        bool done = false;
        if (steal && p->own) {   // a decoded zlib array or a parsed ASCII array of the Document: moved, not copied
            const std::size_t i = p->own - 1;
            if (steal->binary) {
                if (i < steal->inflated.size() && steal->inflated[i].data() == a->data && st.nb < 12) { st.bytes[st.nb++] = std::move(steal->inflated[i]); done = true; }
            } else {
                if (i < steal->numbers.size() && (const void*)steal->numbers[i].data() == (const void*)a->data && st.nn < 12) { st.nums[st.nn++] = std::move(steal->numbers[i]); done = true; }
            }
            // (the moved buffer is the same memory: a->data stays valid)
        }
        if (!done) {   // a view into the caller's bytes (or a const Document): copied
            const std::size_t esz = (a->type == 'd' || a->type == 'l') ? 8 : ((a->type == 'b' || a->type == 'c') ? 1 : 4);
            if (st.nb < 12) {
                ByteBuffer& v = st.bytes[st.nb++];
                v.assign(a->data, a->data + (std::size_t)a->count * esz);
                k.a.data = v.data();
            } else {   // (more arrays than a mesh has: its own allocation)
                auto v = std::make_shared<std::vector<std::uint8_t>>(a->data, a->data + (std::size_t)a->count * esz);
                k.a.data = v->data(); k.keep = v;
                keptHere.push_back({a->data, k.a.data});
                return k;
            }
        }
        k.keep = store;
        keptHere.push_back({a->data, k.a.data});
        return k;
    }
    MeshLayer KeepLayer(const Layer& L) {
        MeshLayer m;
        m.dim = L.dim;
        switch (L.map) {
            case kByPolyVert: m.mapping = LayerMapping::ByPolygonVertex; break;
            case kByVertex: m.mapping = LayerMapping::ByControlPoint; break;
            case kByPoly: m.mapping = LayerMapping::ByPolygon; break;
            case kAllSame: m.mapping = LayerMapping::AllSame; break;
            case kByEdge: m.mapping = LayerMapping::ByEdge; break;
            default: return m;
        }
        m.values = Keep(L.valsProp);
        m.indexed = L.indexed;
        if (L.indexed) m.indices = Keep(L.idxProp);
        return m;
    }
    bool BuildMesh(const Obj& geo, std::size_t modelNode, Mesh& m) {
        const Elem& g = *geo.e;
        m.name = std::string(geo.name);
        const Array* verts = ArrayOf(g, "Vertices");
        const Array* pvi = ArrayOf(g, "PolygonVertexIndex");
        if (!verts || !pvi) return true;   // an empty mesh: nothing to emit
        const std::size_t ncp = verts->size() / 3;
        m.controlPoints = (std::uint32_t)std::min<std::size_t>(ncp, 0xFFFFFFFFu);
        const std::size_t npv = pvi->size();
        if (pvi->type != 'i')
            for (std::size_t i = 0; i < npv; ++i) {
                const double d = (*pvi)[i];
                if (!(d > -2147483649.0 && d < 2147483648.0)) return Fail("polygon index out of range");
            }
        // The triangle corners BuildParts will make: n - 2 triangles for a polygon of n >= 3 corners all in range (the
        // others are skipped), checked against the whole file's budget here, before the import allocates anything.
        const std::uint64_t total = PolygonCorners(*pvi, ncp);
        if (total > opt.limits.maxCorners - std::min(cornersBuilt, opt.limits.maxCorners)) return Fail("mesh too large");
        cornersBuilt += (std::size_t)total;
        const Layer ln = LoadLayer(g, "LayerElementNormal", "Normals", "NormalsIndex", 3);
        // UV sets: set 0 is the element with the lowest index (Corner::uv); the others, in index order, are extra sets
        // (Part::extraUVs). A set whose mapping or values cannot be read is skipped; at most Limits::maxUVSets.
        const std::vector<const Elem*> uvElems = LayerElems(g, "LayerElementUV");
        const Layer lu = LayerOfElem(uvElems.empty() ? nullptr : uvElems[0], "UV", "UVIndex", 2);
        std::vector<Layer> lx;
        if (lu.Ok()) {
            m.uvSetNames.push_back(LayerName(uvElems[0]));
            for (std::size_t k = 1; k < uvElems.size() && m.uvSetNames.size() < opt.limits.maxUVSets; ++k) {
                const Layer L = LayerOfElem(uvElems[k], "UV", "UVIndex", 2);
                if (!L.Ok()) continue;
                lx.push_back(L);
                m.uvSetNames.push_back(LayerName(uvElems[k]));
            }
        }
        bool anyByEdge = false;
        for (const Layer& L : lx) if (L.map == kByEdge) anyByEdge = true;
        const Layer lc = LoadLayer(g, "LayerElementColor", "Colors", "ColorIndex", 4);
        Layer lm = LoadLayer(g, "LayerElementMaterial", "Materials", "Materials", 1);
        if (lm.Ok()) lm.indexed = false;          // material ids are the values themselves
        m.hasUV = lu.Ok(); m.hasColors = lc.Ok(); m.hasNormals = true;   // missing normals are generated (flat, per polygon)
        Polygons& P = m.polygons;
        BeginMeshStore();
        P.corners = total;
        P.positionScale = conv.s;
        P.positions = Keep(ArrayPropOf(g, "Vertices"));
        P.polygonVertexIndex = Keep(ArrayPropOf(g, "PolygonVertexIndex"));
        if (ln.map == kByEdge || lu.map == kByEdge || lc.map == kByEdge || lm.map == kByEdge || anyByEdge) P.edges = Keep(ArrayPropOf(g, "Edges"));
        P.normals = KeepLayer(ln); P.uvs = KeepLayer(lu); P.colors = KeepLayer(lc); P.materials = KeepLayer(lm);
        for (const Layer& L : lx) P.extraUVs.push_back(KeepLayer(L));
        if (lm.Ok()) P.materials.indexed = false;
        // material slots: materials connected to the model, in connection order
        {
            const Id mid = nodeModelId[modelNode];
            for (const Conn* c : ChildrenOf(mid)) {   // materials are children of the model
                if (c->propLink) continue;
                if (const int* mt = matOfObj.find(c->child)) m.materials.push_back(*mt);
            }
        }

        // skin + blend shapes
        const Id gid = ToId(g.props[0].Num());
        bool haveSkin = false;
        for (const Conn* c : ChildrenOf(gid)) {
            const Obj* d = Get(c->child);
            if (!d || d->cls != "Deformer" || c->propLink) continue;
            if (d->sub == "Skin" && !haveSkin) {
                haveSkin = true;
                m.skinWeights.assign(ncp, {});
                const Mat g2n = Mul(Translate(xp[modelNode].gt[0], xp[modelNode].gt[1], xp[modelNode].gt[2]),
                                    Mul(Euler(xp[modelNode].gr, 0), ScaleM(xp[modelNode].gs[0], xp[modelNode].gs[1], xp[modelNode].gs[2])));
                std::uint32_t ci = 0;
                // Each cluster's Indexes / Weights, read after all clusters are known: the influences of every control
                // point are counted first, so each control point's list is allocated once at its final size (growing
                // them one push at a time was about a tenth of a skinned file's import). Same order as before.
                struct ClusterArrays { const Array* ix; const Array* wt; std::uint32_t ci; };
                std::vector<ClusterArrays> weightArrays;
                for (const Conn* cc : ChildrenOf(ToId(d->e->props[0].Num()))) {
                    const Obj* cl = Get(cc->child);
                    if (!cl || cl->cls != "Deformer" || cl->sub != "Cluster" || cc->propLink) continue;
                    Cluster cluster;
                    for (const Conn* bc : ChildrenOf(ToId(cl->e->props[0].Num()))) {
                        const int* nm = nodeOfModel.find(bc->child);
                        if (nm && !bc->propLink) { cluster.boneNode = *nm; break; }
                    }
                    // `Transform` is the mesh node's matrix relative to the bone at bind time (what exporters
                    // such as Blender write; TransformLink, the bone's world bind matrix, is not needed).
                    Mat transform;
                    if (const Array* a = ArrayOf(*cl->e, "Transform")) transform = FromFbx16(*a);
                    cluster.geometryToBone = conv.Scaled(Mul(transform, g2n));
                    const Array* ix = ArrayOf(*cl->e, "Indexes");
                    const Array* wt = ArrayOf(*cl->e, "Weights");
                    if (ix && wt) weightArrays.push_back({ix, wt, ci});
                    m.clusters.push_back(std::move(cluster));
                    ++ci;
                }
                {
                    std::vector<std::uint32_t> count(ncp, 0);
                    for (int pass = 0; pass < 2; ++pass) {
                        if (pass == 1) for (std::size_t v = 0; v < ncp; ++v) if (count[v]) m.skinWeights[v].reserve(count[v]);
                        for (const ClusterArrays& ca : weightArrays) {
                            const Array& ix = *ca.ix; const Array& wt = *ca.wt;
                            for (std::size_t k = 0; k < std::min(ix.size(), wt.size()); ++k) {
                                const double v = ix[k];
                                if (!(v >= 0 && v < (double)ncp && std::isfinite(wt[k]))) continue;
                                if (pass == 0) ++count[(std::size_t)v];
                                else m.skinWeights[(std::size_t)v].push_back({ca.ci, (float)wt[k]});
                            }
                        }
                    }
                }
                for (auto& w : m.skinWeights) std::stable_sort(w.begin(), w.end(), [](const Influence& a, const Influence& b) { return a.weight > b.weight; });
                if (m.clusters.empty()) m.skinWeights.clear();
            } else if (d->sub == "BlendShape") {
                for (const Conn* cc : ChildrenOf(ToId(d->e->props[0].Num()))) {
                    const Obj* ch = Get(cc->child);
                    if (!ch || ch->cls != "Deformer" || ch->sub != "BlendShapeChannel" || cc->propLink) continue;
                    BlendChannel bc; bc.name = std::string(ch->name); bc.weight = ch->props.Get("DeformPercent", 0.0) * 0.01;
                    const Obj* shape = nullptr;
                    for (const Conn* sc2 : ChildrenOf(ToId(ch->e->props[0].Num()))) { const Obj* s = Get(sc2->child); if (s && s->cls == "Geometry" && !sc2->propLink) shape = s; }   // last = full weight
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
    IdMap<std::pair<int, int>> chanOfObj;

    bool Meshes() {
        for (std::size_t ni = 0; ni < nodeModelId.size(); ++ni) {
            const Id mid = nodeModelId[ni];
            const Obj* geo = nullptr;
            for (const Conn* c : ChildrenOf(mid)) {
                if (c->propLink) continue;
                const Obj* o = Get(c->child);
                if (o && o->cls == "Geometry" && o->sub == "Mesh") { geo = o; break; }
            }
            if (!geo) continue;
            const Id gid = ToId(geo->e->props[0].Num());
            if (const int* it = meshOfGeom.find(gid)) { sc.nodes[ni].mesh = *it; continue; }
            Mesh m;
            if (!BuildMesh(*geo, ni, m)) return false;
            meshOfGeom[gid] = (int)sc.meshes.size();
            sc.nodes[ni].mesh = (int)sc.meshes.size();
            sc.meshes.push_back(std::move(m));
        }
        return true;
    }

    bool Animation();   // reads the curves and plans the clips (defined after Animator, below)


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

} // namespace scene_build
} // namespace fbx
} // namespace okay
