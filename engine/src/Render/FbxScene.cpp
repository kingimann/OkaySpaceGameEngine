// okay::fbx layer 2: element tree -> Scene (nodes, meshes, skins, blend shapes,
// materials, baked animation). See FbxReader.hpp and docs/content/model_readers.md.
// Written from the public description of the FBX object model; no third-party code.
#include "okay/Core/SafeCast.hpp"
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
Mat Euler(const double d[3], int order) {
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

// Base64 to bytes (RFC 4648 alphabet), straight into `out`. Characters outside the alphabet ('=', line ends, anything)
// are skipped and the bits of the rest are taken in order, 8 at a time; a tail of fewer than 8 bits is dropped. Four
// alphabet characters in a row (the usual case) give their three bytes at once: the same bytes the bit-by-bit rule
// gives, since a group starts with no bits left over.
struct Base64Table {
    signed char t[256];
    Base64Table() : t{} {
        for (int c = 0; c < 256; ++c) t[c] = -1;
        const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) t[(unsigned char)a[i]] = (signed char)i;
    }
};
void Base64Decode(std::string_view in, std::vector<std::uint8_t>& out) {
    static const Base64Table kTab;
    const signed char* tab = kTab.t;
    const unsigned char* p = (const unsigned char*)in.data();
    const std::size_t n = in.size();
    out.resize(n / 4 * 3 + 3);   // the most it can produce (every character in the alphabet), trimmed at the end
    std::uint8_t* o = out.data();
    std::size_t w = 0;
    unsigned acc = 0; int bits = 0;
    std::size_t i = 0;
    while (i < n) {
        if (bits == 0) {
            while (n - i >= 4) {
                const int a = tab[p[i]], b = tab[p[i + 1]], c = tab[p[i + 2]], d = tab[p[i + 3]];
                if ((a | b | c | d) < 0) break;
                const std::uint32_t v = ((std::uint32_t)a << 18) | ((std::uint32_t)b << 12) | ((std::uint32_t)c << 6) | (std::uint32_t)d;
                o[w] = (std::uint8_t)(v >> 16); o[w + 1] = (std::uint8_t)(v >> 8); o[w + 2] = (std::uint8_t)v;
                w += 3; i += 4;
            }
            if (i >= n) break;
        }
        const int t = tab[p[i++]];
        if (t < 0) continue;
        acc = (acc << 6) | (unsigned)t; bits += 6;
        if (bits >= 8) { bits -= 8; o[w++] = (std::uint8_t)((acc >> bits) & 0xFF); }
    }
    out.resize(w);
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
const Prop* ArrayPropOf(const Elem& e, const char* name) {
    const Elem* c = e.Find(name);
    return (c && !c->props.empty() && c->props[0].type == 'a') ? &c->props[0] : nullptr;
}
const Array* ArrayOf(const Elem& e, const char* name) { const Prop* p = ArrayPropOf(e, name); return p ? &p->arr : nullptr; }
/// The layer an element describes (`best`: one LayerElementNormal / UV / Color / Material child of the geometry).
Layer LayerOfElem(const Elem* best, const char* valName, const char* idxName, int dim) {
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
std::vector<const Elem*> LayerElems(const Elem& geo, const char* layerName) {
    std::vector<std::pair<double, const Elem*>> found;
    for (const Elem& c : geo.children)
        if (c.name == layerName) found.push_back({(!c.props.empty() && c.props[0].IsNumber()) ? c.props[0].Num() : 0, &c});
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<const Elem*> out; out.reserve(found.size());
    for (const auto& f : found) out.push_back(f.second);
    return out;
}
/// The first layer of a kind: the element with the lowest index (the first in the file among equal ones).
Layer LoadLayer(const Elem& geo, const char* layerName, const char* valName, const char* idxName, int dim) {
    const Elem* best = nullptr; double bestIdx = 1e30;
    for (const Elem& c : geo.children) {
        if (c.name != layerName) continue;
        const double k = (!c.props.empty() && c.props[0].IsNumber()) ? c.props[0].Num() : 0;
        if (!best || k < bestIdx) { best = &c; bestIdx = k; }
    }
    return LayerOfElem(best, valName, idxName, dim);
}
/// A layer element's "Name" (a UV set's name), "" when it has none.
std::string LayerName(const Elem* e) {
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
bool PolygonIndicesInRange(const Array& pvi) {
    if (pvi.type == 'i') return true;
    for (std::size_t i = 0; i < pvi.size(); ++i) {
        const double d = pvi[i];
        if (!(d > -2147483649.0 && d < 2147483648.0)) return false;
    }
    return true;
}
/// The triangle corners the polygons give: (n - 2) * 3 for each polygon of n >= 3 corners that are all below `ncp`
/// (the polygons BuildParts triangulates; the others are skipped). A last polygon without its end marker counts.
std::uint64_t PolygonCorners(const Array& pvi, std::size_t ncp) {
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
void CubicMinMax(double y0, double y1, double y2, double y3, double& lo, double& hi) {
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
        if (!o) return Fail("no Objects section (FBX 6.x files are not supported)");
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

// ---------------------------------------------------------------------------
// clip keys (the import step: BuildClips). The reader (Builder::Animation) reads the curves and plans the clips; the
// keys are made here, from what the Animator kept, when the import asks for them.
// ---------------------------------------------------------------------------
struct Animator : ClipSource {
    using XProps = Builder::XProps; using FixedRot = Builder::FixedRot;
    static FixedRot FixedRotations(const XProps& x) { return Builder::FixedRotations(x); }
    static Mat LocalMatrix(const XProps& x, const FixedRot& f, const Mat& R) { return Builder::LocalMatrix(x, f, R); }
    static Mat LocalSimple(const XProps& x, const FixedRot& f, const Mat& R) { return Builder::LocalSimple(x, f, R); }
    BuildOptions opt; Conv conv; std::string err;
    std::vector<XProps> xp;          // per node: its transform properties
    std::vector<char> isRoot;        // per node: hangs off the scene root
    std::vector<CurveNode> cns;      // (curve pointers into curveStore: neither vector changes after the reader)
    bool Fail(const std::string& m) { if (err.empty()) err = m; return false; }
    struct Layr { Id id; std::string_view name; double weight = 100; int blendMode = 0; };
    std::vector<Curve> curveStore;
    // The curves' arrays (Curve's runs point into these; they do not change after the reader)
    std::vector<double> poolT, poolV, poolSlope; std::vector<std::uint32_t> poolSet; std::vector<int> poolFlags;
    /// A curve node driving one node's T, R or S in one layer; `hint` and `hintP` remember the key segments of the
    /// last evaluation (two, so the probes between keys do not move the one the keys use).
    struct NodeCN { int layer; int which; const CurveNode* cn; std::size_t hint[3]; std::size_t hintP[3]; };   // which: 0 T, 1 R, 2 S

    // ---- keys at the file's own key times ---------------------------------------------------------------------   // lint-allow: secret
    std::size_t fileKeyEvals = 0;   // evaluations so far (each counted once per curve node it reads), whole file

    /// One channel (0 T, 1 R, 2 S) at absolute file time `at`, the layers combined as the fixed-rate bake does.
    static void EvalChannel(std::vector<NodeCN>& list, const std::vector<Layr>& layers, int which, const double base[3], double at, bool probe, double out[3]) {
        for (int k = 0; k < 3; ++k) out[k] = base[k];
        bool first = true;
        for (NodeCN& n : list) {
            if (n.which != which) continue;
            double lv[3];
            for (int k = 0; k < 3; ++k) lv[k] = n.cn->curve[k] ? n.cn->curve[k]->Eval(at, probe ? n.hintP[k] : n.hint[k]) : n.cn->def[k];
            const Layr& ly = layers[(std::size_t)n.layer];
            const double w = std::max(0.0, ly.weight) * 0.01;
            if (first) { for (int k = 0; k < 3; ++k) out[k] = lv[k]; first = false; }
            else if (ly.blendMode == 0) for (int k = 0; k < 3; ++k) out[k] += lv[k] * w;
            else for (int k = 0; k < 3; ++k) out[k] += (lv[k] - out[k]) * std::min(1.0, w);
        }
    }
    /// A node's pose at one time: the curves' raw values (rt, rr, rs: file units and Euler degrees) and the local
    /// translation, rotation and scale they give (what the clip stores).
    struct KeyPose {
        double rt[3], rr[3], rs[3]; double t[3], q[4], s[3];
        double qr[4]; bool hasQr = false;   // EulerQuat(rr), made when first needed
        const double* RawQuat(int order) { if (!hasQr) { EulerQuat(rr, order, qr); hasQr = true; } return qr; }
    };
    struct NodeBake {
        std::vector<NodeCN>* list; const std::vector<Layr>* layers;
        XProps x; FixedRot fixed; bool isRoot;
        double t0, tolT, tolS, tolR;
        std::size_t cost;   // curve nodes read per evaluation
        // When no channel has more than one curve node (one layer, the usual case), the one per channel (or none):
        // EvalChannel then returns that curve node's values, or the base values without one, which is what the direct
        // reading in RawPose does without walking the list three times.
        bool direct = false; NodeCN* only[3] = {nullptr, nullptr, nullptr};
    };
    void RawPose(NodeBake& nb, double at, bool probe, KeyPose& p) {
        if (nb.direct) {
            const double* base[3] = {nb.x.t, nb.x.r, nb.x.s};
            double* out[3] = {p.rt, p.rr, p.rs};
            for (int w = 0; w < 3; ++w) {
                NodeCN* n = nb.only[w];
                if (!n) { for (int k = 0; k < 3; ++k) out[w][k] = base[w][k]; continue; }
                for (int k = 0; k < 3; ++k) out[w][k] = n->cn->curve[k] ? n->cn->curve[k]->Eval(at, probe ? n->hintP[k] : n->hint[k]) : n->cn->def[k];
            }
        } else {
            EvalChannel(*nb.list, *nb.layers, 0, nb.x.t, at, probe, p.rt);
            EvalChannel(*nb.list, *nb.layers, 1, nb.x.r, at, probe, p.rr);
            EvalChannel(*nb.list, *nb.layers, 2, nb.x.s, at, probe, p.rs);
        }
        fileKeyEvals += nb.cost;
    }
    /// The same matrix chain and decomposition as the fixed-rate bake, so equal curve values give equal keys.
    void LocalPose(NodeBake& nb, KeyPose& p) {
        XProps x = nb.x;
        std::memcpy(x.t, p.rt, sizeof(x.t)); std::memcpy(x.r, p.rr, sizeof(x.r)); std::memcpy(x.s, p.rs, sizeof(x.s));
        const Mat R = Euler(x.r, x.order);
        const bool simple = nb.fixed.simple && Small3(x.t) && Small3(x.r) && Small3(x.s);
        const Mat local = conv.Local(simple ? LocalSimple(x, nb.fixed, R) : LocalMatrix(x, nb.fixed, R), nb.isRoot);
        Decompose(local, p.t, p.q, p.s);
    }
    // The keys of the node being baked go here first (kept from node to node, so they do not grow again for each one),
    // then into the node's arrays at their final size.
    std::vector<Key3> emitT, emitS; std::vector<Key4> emitQ;
    void Emit(NodeBake& nb, double at, const KeyPose& p) {
        const double rel = at - nb.t0;
        Key3 kt; kt.time = rel; for (int k = 0; k < 3; ++k) kt.v[k] = p.t[k];
        Key4 kq; kq.time = rel; for (int k = 0; k < 4; ++k) kq.v[k] = p.q[k];
        Key3 ks; ks.time = rel; for (int k = 0; k < 3; ++k) ks.v[k] = p.s[k];
        emitT.push_back(kt); emitQ.push_back(kq); emitS.push_back(ks);
    }
    /// How far linear playback from a to b is from the tolerances, judged from the poses at 1/3 and 2/3: `euler` is the
    /// largest move of an Euler angle over maxEulerStep, `err` the largest error over what is allowed. The segment is
    /// good when both are at most 1 (NaN counts as good: a broken curve is not chased down to minGap).
    void SegmentNeed(NodeBake& nb, KeyPose& a, KeyPose& p1, KeyPose& p2, KeyPose& b, bool& haveLocal, double& euler, double& err) {
        const KeyTolerance& tol = opt.keyTolerance;
        euler = 0; err = 0;
        // Euler angles: no angle moves more than maxEulerStep inside the segment (exact: each is one cubic piece here).
        // This also keeps every step under half a turn, so the quaternion blend cannot take the other way round.
        bool rotMoves = false;
        for (int k = 0; k < 3; ++k) {
            double lo, hi; CubicMinMax(a.rr[k], p1.rr[k], p2.rr[k], b.rr[k], lo, hi);
            euler = std::max(euler, (hi - lo) / tol.maxEulerStep);
            rotMoves |= hi != lo;
        }
        // When an angle turns too far, Subdivide looks at nothing else (it splits by `euler` alone and never reads
        // `err`), so the error is not worked out. (The poses' quaternions and local poses it would have made are made
        // when they are needed, with the same values.)
        if (euler > 1.0) return;
        // Without offsets and pivots the local translation is the curves' translation times the unit scale (turned by
        // the axis matrix at a root) and the local scale is the curves' scale up to fixed signs, so the error of a
        // straight line is the exact deviation of a cubic. The scale needs the same sign at both ends and no zero in
        // between. Otherwise (and for the rotation) the poses at 1/3 and 2/3 are compared with half the tolerance.
        bool raw = nb.fixed.simple;
        for (int k = 0; k < 3 && raw; ++k) {
            double lo, hi; CubicMinMax(a.rs[k], p1.rs[k], p2.rs[k], b.rs[k], lo, hi);
            raw = lo > 1e-9 || hi < -1e-9;
        }
        if (raw) {
            double e2 = 0, es = 0;
            for (int k = 0; k < 3; ++k) {
                const double dt = CubicDeviation(p1.rt[k] - (a.rt[k] + (b.rt[k] - a.rt[k]) / 3.0), p2.rt[k] - (a.rt[k] + (b.rt[k] - a.rt[k]) * 2.0 / 3.0));
                e2 += dt * dt;
                es = std::max(es, CubicDeviation(p1.rs[k] - (a.rs[k] + (b.rs[k] - a.rs[k]) / 3.0), p2.rs[k] - (a.rs[k] + (b.rs[k] - a.rs[k]) * 2.0 / 3.0)));
            }
            err = std::max(std::sqrt(e2) * std::fabs(conv.s) / nb.tolT, es / nb.tolS);
            if (!rotMoves) return;
            const double* qa = a.RawQuat(nb.x.order); const double* qb = b.RawQuat(nb.x.order);
            const double* q1 = p1.RawQuat(nb.x.order); const double* q2 = p2.RawQuat(nb.x.order);
            // the fixed pre/post rotations and the axis matrix turn every quaternion the same way: angles do not change
            err = std::max(err, std::max(NlerpErrorDeg(qa, qb, 1.0 / 3.0, q1), NlerpErrorDeg(qa, qb, 2.0 / 3.0, q2)) / (0.5 * nb.tolR));
            return;
        }
        LocalPose(nb, p1); LocalPose(nb, p2); haveLocal = true;
        for (int i = 0; i < 2; ++i) {
            const KeyPose& p = i ? p2 : p1;
            const double u = i ? 2.0 / 3.0 : 1.0 / 3.0;
            double e2 = 0, es = 0;
            for (int k = 0; k < 3; ++k) {
                const double d = p.t[k] - (a.t[k] + (b.t[k] - a.t[k]) * u); e2 += d * d;
                es = std::max(es, std::fabs(p.s[k] - (a.s[k] + (b.s[k] - a.s[k]) * u)));
            }
            err = std::max(err, std::max(std::sqrt(e2) / (0.5 * nb.tolT), es / (0.5 * nb.tolS)));
            err = std::max(err, NlerpErrorDeg(a.q, b.q, u, p.q) / (0.5 * nb.tolR));
        }
    }
    /// Keys strictly between a (at ta) and b (at tb), emitted in time order. None if the segment is within the
    /// tolerances. Else it is cut into n equal parts, n from how far it is off (an Euler move shrinks in proportion to
    /// the distance between keys, an error at least with its square), never into parts of minGap or less, and each
    /// part is checked the same way. Three parts reuse the poses at 1/3 and 2/3.
    bool Subdivide(NodeBake& nb, double ta, KeyPose& a, double tb, KeyPose& b, int depth) {
        const double len = tb - ta, g = opt.keyTolerance.minGap * (1.0 + 1e-6);   // (the margin: keys a frame apart at
        if (!(len / 2.0 > g) || depth > 40) return true;                          //  30 or 60 Hz are never split by rounding)
        if (fileKeyEvals > 4 * opt.limits.maxTotalBakedSamples) return Fail("animation bake budget exceeded");
        const double t1 = ta + len / 3.0, t2 = ta + len * (2.0 / 3.0);
        KeyPose p1, p2;
        RawPose(nb, t1, true, p1); RawPose(nb, t2, true, p2);
        bool haveLocal = false;
        double euler, err;
        SegmentNeed(nb, a, p1, p2, b, haveLocal, euler, err);
        if (!(euler > 1.0 || err > 1.0)) return true;
        // (while an angle still turns too far, the error at 1/3 and 2/3 says little: the turn alone decides then)
        double want = std::max(2.0, euler > 1.0 ? std::ceil(euler * 1.05) : std::ceil(std::sqrt(err) * 1.1));
        want = std::min(want, std::min(64.0, std::floor(len / g)));   // parts longer than minGap (at least 2 here)
        const int n = (int)std::max(2.0, want);
        if (n == 3) {
            if (!haveLocal) { LocalPose(nb, p1); LocalPose(nb, p2); }
            if (!Subdivide(nb, ta, a, t1, p1, depth + 1)) return false;
            Emit(nb, t1, p1);
            if (!Subdivide(nb, t1, p1, t2, p2, depth + 1)) return false;
            Emit(nb, t2, p2);
            return Subdivide(nb, t2, p2, tb, b, depth + 1);
        }
        KeyPose prev = a; double tp = ta;
        for (int i = 1; i < n; ++i) {
            const double ti = ta + len * ((double)i / (double)n);
            KeyPose pi; RawPose(nb, ti, true, pi); LocalPose(nb, pi);
            if (!Subdivide(nb, tp, prev, ti, pi, depth + 1)) return false;
            Emit(nb, ti, pi);
            prev = pi; tp = ti;
        }
        return Subdivide(nb, tp, prev, tb, b, depth + 1);
    }
    /// The key times of `cs` in (t0, t1], merged without repeats, each marked when a curve jumps there (a step key
    /// before it, or two keys at the same time with different values); t0 first and t1 last.
    static void KeyTimes(const std::vector<const Curve*>& cs, double t0, double t1, std::vector<double>& times, std::vector<char>& jump) {
        times.assign(1, t0); jump.assign(1, 0);
        if (!(t1 > t0)) return;
        std::vector<std::size_t> pos(cs.size());
        for (std::size_t i = 0; i < cs.size(); ++i) pos[i] = (std::size_t)(std::upper_bound(cs[i]->t.begin(), cs[i]->t.end(), t0) - cs[i]->t.begin());
        for (;;) {
            double m = std::numeric_limits<double>::infinity();
            for (std::size_t i = 0; i < cs.size(); ++i) if (pos[i] < cs[i]->t.size() && cs[i]->t[pos[i]] < m) m = cs[i]->t[pos[i]];
            if (!(m <= t1)) break;
            char j = 0;
            for (std::size_t i = 0; i < cs.size(); ++i) {
                const Curve& c = *cs[i];
                while (pos[i] < c.t.size() && c.t[pos[i]] == m) {
                    const std::size_t k = pos[i]++;
                    if (k >= 1 && c.v[k - 1] != c.v[k] && (c.t[k - 1] == c.t[k] || (c.FlagsOf(k - 1) & 0x2))) j = 1;
                }
            }
            times.push_back(m); jump.push_back(j);
        }
        if (times.back() != t1) { times.push_back(t1); jump.push_back(0); }
    }
    /// The time of the key just before a jump at `at`: 2e-4 s earlier, or 2^-17 of the clip time when that is more (64
    /// float steps). The engine's clips merge keys less than 1e-4 s apart (AnimationClip::AddKeys), so the pair must be
    /// further apart than that, also after the times are rounded to float.
    static double BeforeJump(double at, double t0) { return at - JumpGap(at - t0); }
    static double JumpGap(double clipTime) { return std::max(2e-4, std::fabs(clipTime) * 7.62939453125e-6); }

    bool BakeNodeFileKeys(std::vector<NodeCN>& list, const std::vector<Layr>& layers, int node, double t0, double t1, NodeAnim& na) {
        std::vector<const Curve*> cs;
        for (const NodeCN& n : list) for (int k = 0; k < 3; ++k) {
            const Curve* c = n.cn->curve[k];
            if (c && std::find(cs.begin(), cs.end(), c) == cs.end()) cs.push_back(c);
        }
        std::vector<double> times; std::vector<char> jump;
        KeyTimes(cs, t0, t1, times, jump);
        NodeBake nb;
        nb.list = &list; nb.layers = &layers; nb.x = xp[(std::size_t)node]; nb.fixed = FixedRotations(nb.x);
        nb.isRoot = isRoot[(std::size_t)node] != 0; nb.t0 = t0; nb.cost = std::max<std::size_t>(1, list.size());
        nb.direct = true;
        for (NodeCN& n : list) {
            if (n.which < 0 || n.which > 2 || nb.only[n.which]) { nb.direct = false; break; }
            nb.only[n.which] = &n;
        }
        if ((double)times.size() * (double)nb.cost > (double)(4 * opt.limits.maxTotalBakedSamples - std::min(fileKeyEvals, 4 * opt.limits.maxTotalBakedSamples)))
            return Fail("animation bake budget exceeded");
        std::vector<KeyPose> poses(times.size());
        double maxT = 0, maxS = 1;
        for (std::size_t i = 0; i < times.size(); ++i) {
            RawPose(nb, times[i], false, poses[i]); LocalPose(nb, poses[i]);
            const KeyPose& p = poses[i];
            const double n = std::sqrt(p.t[0] * p.t[0] + p.t[1] * p.t[1] + p.t[2] * p.t[2]);
            if (n > maxT) maxT = n;   // (NaN never wins)
            for (int k = 0; k < 3; ++k) if (std::fabs(p.s[k]) > maxS) maxS = std::fabs(p.s[k]);
        }
        const KeyTolerance& tol = opt.keyTolerance;
        nb.tolT = tol.positionAbs + tol.positionRel * maxT; nb.tolS = tol.scaleRel * maxS; nb.tolR = tol.rotationDeg;
        emitT.clear(); emitQ.clear(); emitS.clear();
        Emit(nb, times[0], poses[0]);
        double ta = times[0]; KeyPose a = poses[0];
        for (std::size_t i = 1; i < times.size(); ++i) {
            if (jump[i]) {   // a key just before the jump with the value before it; nothing is added across the jump
                const double pre = BeforeJump(times[i], t0);
                if (pre > ta) {
                    KeyPose p; RawPose(nb, pre, true, p); LocalPose(nb, p);
                    if (!Subdivide(nb, ta, a, pre, p, 0)) return false;
                    Emit(nb, pre, p);
                }
            } else if (!Subdivide(nb, ta, a, times[i], poses[i], 0)) return false;
            Emit(nb, times[i], poses[i]);
            ta = times[i]; a = poses[i];
        }
        na.translation.assign(emitT.begin(), emitT.end()); na.rotation.assign(emitQ.begin(), emitQ.end()); na.scale.assign(emitS.begin(), emitS.end());
        return true;
    }
    /// A blend shape weight curve the same way (its own value is the cubic, so the check is exact).
    bool BakeBlendFileKeys(const CurveNode& cn, double t0, double t1, BlendAnim& ba) {
        const Curve* c = cn.curve[0];
        std::vector<const Curve*> cs; if (c) cs.push_back(c);
        std::vector<double> times; std::vector<char> jump;
        KeyTimes(cs, t0, t1, times, jump);
        std::size_t hint = (std::size_t)-1, hintP = (std::size_t)-1;
        auto val = [&](double at, std::size_t& h) { ++fileKeyEvals; return c ? c->Eval(at, h) : cn.def[0]; };
        auto emit = [&](double at, double v) { Key3 k; k.time = at - t0; k.v[0] = v; k.v[1] = k.v[2] = 0; ba.percent.push_back(k); };
        const KeyTolerance& tol = opt.keyTolerance;
        bool failed = false;
        std::function<void(double, double, double, double, int)> sub = [&](double ta, double va, double tb, double vb, int depth) {
            const double len = tb - ta, g = tol.minGap * (1.0 + 1e-6);
            if (failed || !(len / 2.0 > g) || depth > 40) return;
            if (fileKeyEvals > 4 * opt.limits.maxTotalBakedSamples) { failed = true; return; }
            const double t1_ = ta + len / 3.0, t2_ = ta + len * (2.0 / 3.0);
            const double v1 = val(t1_, hintP), v2 = val(t2_, hintP);
            const double err = CubicDeviation(v1 - (va + (vb - va) / 3.0), v2 - (va + (vb - va) * 2.0 / 3.0)) / tol.percent;
            if (!(err > 1.0)) return;
            const int n = (int)std::max(2.0, std::min(std::ceil(std::sqrt(err) * 1.1), std::min(64.0, std::floor(len / g))));
            if (n == 3) {
                sub(ta, va, t1_, v1, depth + 1); emit(t1_, v1);
                sub(t1_, v1, t2_, v2, depth + 1); emit(t2_, v2);
                sub(t2_, v2, tb, vb, depth + 1);
                return;
            }
            double tp = ta, vp = va;
            for (int i = 1; i < n; ++i) {
                const double ti = ta + len * ((double)i / (double)n), vi = val(ti, hintP);
                sub(tp, vp, ti, vi, depth + 1); emit(ti, vi);
                tp = ti; vp = vi;
            }
            sub(tp, vp, tb, vb, depth + 1);
        };
        double ta = times[0], va = val(ta, hint);
        emit(ta, va);
        for (std::size_t i = 1; i < times.size() && !failed; ++i) {
            const double vb = val(times[i], hint);
            if (jump[i]) {
                const double pre = BeforeJump(times[i], t0);
                if (pre > ta) { const double vp = val(pre, hintP); sub(ta, va, pre, vp, 0); emit(pre, vp); }
            } else sub(ta, va, times[i], vb, 0);
            emit(times[i], vb);
            ta = times[i]; va = vb;
        }
        if (failed) return Fail("animation bake budget exceeded");
        return true;
    }

    /// One take: what its keys are made from.
    struct Plan {
        std::size_t clip = 0;        // index in Scene::clips
        std::vector<Layr> layers;
        std::map<int, std::vector<NodeCN>> perNode;   // node index -> curve nodes (ordered)
        struct Blend { int mesh, channel; const CurveNode* cn; };
        std::vector<Blend> blends;
        double t0 = 0, t1 = 0, dur = 0; std::size_t samples = 0;
    };
    std::vector<Plan> plans;
    /// Makes the keys of every planned clip into `sc.clips` (the import step; BuildClips).
    bool Bake(Scene& sc) {
        fileKeyEvals = 0; err.clear();
        for (Plan& pl : plans) {
            if (pl.clip >= sc.clips.size()) return Fail("clip missing");
            Clip& clip = sc.clips[pl.clip];
            clip.nodes.clear(); clip.blends.clear();
            const double t0 = pl.t0, t1 = pl.t1, dur = pl.dur;
            const std::size_t samples = pl.samples;
            std::vector<Layr>& layers = pl.layers;
            std::map<int, std::vector<NodeCN>>& perNode = pl.perNode;
            if (opt.fileKeys) {
                for (auto& kv : perNode) {
                    NodeAnim na; na.node = kv.first;
                    if (!BakeNodeFileKeys(kv.second, layers, kv.first, t0, t1, na)) return false;
                    clip.nodes.push_back(std::move(na));
                }
                for (const Plan::Blend& b : pl.blends) {
                    BlendAnim ba; ba.mesh = b.mesh; ba.channel = b.channel;
                    if (!BakeBlendFileKeys(*b.cn, t0, t1, ba)) return false;
                    clip.blends.push_back(std::move(ba));
                }
                continue;
            }
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
                const bool isRootNode = isRoot[(std::size_t)kv.first] != 0;
                for (double t : times) {
                    evalVec(kv.second, 0, base.t, t, x.t); evalVec(kv.second, 1, base.r, t, x.r); evalVec(kv.second, 2, base.s, t, x.s);
                    const bool sameR = haveR && std::memcmp(prevR, x.r, sizeof(prevR)) == 0;
                    if (!(havePrev && sameR && std::memcmp(prevT, x.t, sizeof(prevT)) == 0 && std::memcmp(prevS, x.s, sizeof(prevS)) == 0)) {
                        if (!sameR) { R = Euler(x.r, x.order); std::memcpy(prevR, x.r, sizeof(prevR)); haveR = true; }
                        const bool simple = fixed.simple && Small3(x.t) && Small3(x.r) && Small3(x.s);
                        const Mat local = conv.Local(simple ? LocalSimple(x, fixed, R) : LocalMatrix(x, fixed, R), isRootNode);
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
            for (const Plan::Blend& b : pl.blends) {
                BlendAnim ba; ba.mesh = b.mesh; ba.channel = b.channel;
                std::size_t blendHint = (std::size_t)-1;
                for (double t : times) {
                    Key3 k; k.time = t; k.v[1] = k.v[2] = 0;
                    k.v[0] = b.cn->curve[0] ? b.cn->curve[0]->Eval(t + t0, blendHint) : b.cn->def[0];
                    ba.percent.push_back(k);
                }
                clip.blends.push_back(std::move(ba));
            }
        }
        return true;
    }
};

bool Builder::Animation() {
    using Layr = Animator::Layr; using NodeCN = Animator::NodeCN;
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
    // The clips' keys are made later, by BuildClips (the import step): what they need is kept in `job`.
    auto job = std::make_shared<Animator>();
    std::vector<Curve>& curveStore = job->curveStore;
    // curves: their arrays go into the Animator's pools; each curve records where its runs start, and the runs get
    // their pointers once every curve is in (the pools may move while they grow)
    IdMap<int> curveOf;
    std::vector<double>& poolT = job->poolT; std::vector<double>& poolV = job->poolV; std::vector<double>& poolSlope = job->poolSlope;
    std::vector<std::uint32_t>& poolSet = job->poolSet; std::vector<int>& poolFlags = job->poolFlags;
    struct At { std::size_t t, set, flags, slope; };
    std::vector<At> runAt;
    {
        std::size_t keys = 0;   // an upper bound of the keys, for one allocation of each pool
        for (const Obj& o : objs) {
            if (o.cls != "AnimationCurve") continue;
            const Array* kt = ArrayOf(*o.e, "KeyTime");
            const Array* kv = ArrayOf(*o.e, "KeyValueFloat");
            if (!kv) kv = ArrayOf(*o.e, "KeyValueDouble");
            if (kt && kv) keys += std::min(kt->size(), kv->size());
        }
        keys = std::min<std::size_t>(keys, opt.limits.maxAnimKeys);
        poolT.reserve(keys); poolV.reserve(keys);
    }
    for (const Obj& o : objs) {
        if (o.cls != "AnimationCurve") continue;
        Curve c; At at{poolT.size(), poolSet.size(), poolFlags.size(), poolSlope.size()};
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
        // Attribute set of each key: KeyAttrRefCount says how many keys in a row use each set (all use set 0
        // without it). Walked along with the keys below, the same rule as one index per key computed first.
        const std::size_t nsets = (rc && !rc->empty()) ? rc->size() : 1;
        std::size_t sCur = 0, left = (rc && !rc->empty()) ? (std::size_t)std::max(0.0, (*rc)[0]) : n;
        auto nextSet = [&]() -> std::size_t {
            if (!rc) return 0;
            while (left == 0 && sCur + 1 < rc->size()) { ++sCur; left = (std::size_t)std::max(0.0, (*rc)[sCur]); }
            const std::size_t sg = sCur;
            if (left) --left;
            return sg;
        };
        double prev = -1e300;
        bool otherSet = false;
        std::size_t maxSet = 0;   // the tables below cover the sets the kept keys use (never more than the keys)
        // The usual types (KeyTime int64, KeyValueFloat float32) read directly: the same (double)value as Array's [].
        const bool ktLong = kt->type == 'l', kvFloat = kv->type == 'f';
        auto timeAt = [&](std::size_t i) -> double { if (ktLong) { std::int64_t x; std::memcpy(&x, kt->data + i * 8, 8); return (double)x; } return (*kt)[i]; };
        auto valueAt = [&](std::size_t i) -> double { if (kvFloat) { float x; std::memcpy(&x, kv->data + i * 4, 4); return (double)x; } return (*kv)[i]; };
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t sg = nextSet();
            const double t = timeAt(i) / kKTimePerSec;
            const double val = valueAt(i);
            if (!std::isfinite(t) || !std::isfinite(val) || t < prev) continue;   // keep keys sorted and finite
            prev = t;
            poolT.push_back(t); poolV.push_back(val);
            if (nsets > 1) { poolSet.push_back((std::uint32_t)sg); otherSet |= sg != 0; maxSet = std::max(maxSet, sg); }
            if (da && (sg * 4 + 1) < da->size()) c.hasSlopes = true;   // without the array every slope is 0
        }
        c.t.n = c.v.n = poolT.size() - at.t;
        if (!otherSet) poolSet.resize(at.set);   // every key uses set 0
        c.set.n = poolSet.size() - at.set;
        // per set: the flags (0x8, cubic, without KeyAttrFlags) and the slopes (0 where the file has none)
        const std::size_t usedSets = c.set.empty() ? 1 : maxSet + 1;
        for (std::size_t sg = 0; sg < usedSets; ++sg)
            poolFlags.push_back((fl && !fl->empty()) ? SafeInt((*fl)[std::min(sg, fl->size() - 1)]) : 0x8);
        c.setFlags.n = usedSets;
        if (c.hasSlopes) {   // right slopes, then next-left slopes, one run each
            poolSlope.resize(at.slope + 2 * usedSets, 0.0);
            for (std::size_t sg = 0; sg < usedSets; ++sg)
                if ((sg * 4 + 1) < da->size()) { poolSlope[at.slope + sg] = (*da)[sg * 4]; poolSlope[at.slope + usedSets + sg] = (*da)[sg * 4 + 1]; }
            c.setR.n = c.setL.n = usedSets;
        }
        curveOf[ToId(o.e->props[0].Num())] = (int)curveStore.size();
        curveStore.push_back(c);
        runAt.push_back(at);
    }
    for (std::size_t i = 0; i < curveStore.size(); ++i) {   // the pools are complete: point the runs into them
        Curve& c = curveStore[i]; const At& at = runAt[i];
        c.t.p = poolT.data() + at.t; c.v.p = poolV.data() + at.t;
        c.set.p = poolSet.data() + at.set; c.setFlags.p = poolFlags.data() + at.flags;
        c.setR.p = poolSlope.data() + at.slope; c.setL.p = poolSlope.data() + at.slope + c.setR.n;
    }
    // curve nodes
    std::vector<CurveNode>& cns = job->cns; IdMap<std::size_t> cnOf;
    for (const Obj& o : objs) {
        if (o.cls != "AnimationCurveNode") continue;
        CurveNode cn;
        const Id id = ToId(o.e->props[0].Num());
        o.props.Vec3("d|X", cn.def, 0);
        if (o.props.Has("d|DeformPercent")) cn.def[0] = o.props.Get("d|DeformPercent", 0);
        for (const Conn* c : ChildrenOf(id)) {   // curves feeding it
            if (!c->propLink) continue;
            const Obj* cv = Get(c->child);
            if (!cv || cv->cls != "AnimationCurve") continue;
            const int* it = curveOf.find(c->child);
            if (!it || curveStore[(std::size_t)*it].Empty()) continue;
            const int comp = (c->prop == "d|X" || c->prop == "d|DeformPercent") ? 0 : c->prop == "d|Y" ? 1 : c->prop == "d|Z" ? 2 : -1;
            if (comp >= 0) cn.curve[comp] = &curveStore[(std::size_t)*it];
        }
        for (int k = 1; k < 3; ++k) { const char* nm = k == 1 ? "d|Y" : "d|Z"; cn.def[k] = o.props.Get(nm, 0); }
        for (const Conn* c : ParentsOf(id)) {    // what it drives
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
        for (const Conn* c : ChildrenOf(sid)) {
            const Obj* l = Get(c->child);
            if (!l || l->cls != "AnimationLayer" || c->propLink) continue;
            Layr ly; ly.id = c->child; ly.name = l->name; ly.weight = l->props.Get("Weight", 100.0); ly.blendMode = SafeInt(l->props.Get("BlendMode", 0.0));
            layers.push_back(ly);
        }
        std::map<int, std::vector<NodeCN>> perNode;                       // node index -> curve nodes (ordered)
        struct BlendCN { int layer; Id target; const CurveNode* cn; };
        std::vector<BlendCN> blendCns;
        double t0 = 1e300, t1 = -1e300;
        auto span = [&](const CurveNode& cn) { for (int k = 0; k < 3; ++k) if (cn.curve[k] && !cn.curve[k]->Empty()) { t0 = std::min(t0, cn.curve[k]->t.front()); t1 = std::max(t1, cn.curve[k]->t.back()); } };
        for (std::size_t li = 0; li < layers.size(); ++li)
            for (const Conn* c : ChildrenOf(layers[li].id)) {
                const std::size_t* it = cnOf.find(c->child);
                if (!it || c->propLink) continue;
                const CurveNode& cn = cns[*it];
                const int* nm = nodeOfModel.find(cn.target);
                if (nm) {
                    int which = cn.prop == "Lcl Translation" ? 0 : cn.prop == "Lcl Rotation" ? 1 : cn.prop == "Lcl Scaling" ? 2 : -1;
                    if (which < 0) continue;
                    perNode[*nm].push_back({(int)li, which, &cn, {(std::size_t)-1, (std::size_t)-1, (std::size_t)-1}, {(std::size_t)-1, (std::size_t)-1, (std::size_t)-1}}); span(cn);
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
        clip.start = t0;
        Animator::Plan plan;
        plan.clip = sc.clips.size(); plan.t0 = t0; plan.t1 = t1; plan.dur = dur; plan.samples = samples;
        for (Layr& ly : layers) ly.name = {};   // (a view into the file: not kept)
        plan.layers = std::move(layers); plan.perNode = std::move(perNode);
        for (const BlendCN& b : blendCns) plan.blends.push_back({chanOfObj[b.target].first, chanOfObj[b.target].second, b.cn});
        job->plans.push_back(std::move(plan));
        sc.clips.push_back(std::move(clip));
    }
    if (!job->plans.empty()) {
        for (CurveNode& cn : cns) cn.prop = {};   // (views into the file: not kept)
        job->opt = opt; job->conv = conv;
        job->isRoot.resize(sc.nodes.size());
        for (std::size_t i = 0; i < sc.nodes.size(); ++i) job->isRoot[i] = sc.nodes[i].parent < 0 ? 1 : 0;
        job->xp = std::move(xp);   // (the last step of the build: nothing reads xp after this)
        sc.clipSource = std::move(job);
    }
    return true;
}


// ---------------------------------------------------------------------------
// the import step: polygons -> parts (BuildParts)
// ---------------------------------------------------------------------------
Layer LayerOf(const MeshLayer& m) {
    Layer L; L.dim = m.dim;
    switch (m.mapping) {
        case LayerMapping::ByPolygonVertex: L.map = kByPolyVert; break;
        case LayerMapping::ByControlPoint: L.map = kByVertex; break;
        case LayerMapping::ByPolygon: L.map = kByPoly; break;
        case LayerMapping::AllSame: L.map = kAllSame; break;
        case LayerMapping::ByEdge: L.map = kByEdge; break;
        default: return L;
    }
    L.vals = m.values.present ? &m.values.a : nullptr;
    L.indexed = m.indexed && m.indices.present;
    L.idx = L.indexed ? &m.indices.a : nullptr;
    return L;
}

bool PartsOf(Mesh& m, const Limits& lim, std::string& err) {
    const Polygons& P = m.polygons;
    if (P.empty()) return true;   // an empty mesh: nothing to emit
    const Array* verts = &P.positions.a;
    const Array* pvi = &P.polygonVertexIndex.a;
    const double scale = P.positionScale;
    const std::size_t ncp = verts->size() / 3;
    // Positions (unit-scaled) and polygon corners are read straight from the kept arrays where they are used: no
    // per-mesh copies (on big meshes the fresh memory for copies cost more time than the copying itself).
    auto pos = [&](std::size_t cp, int k) -> double {
        const std::size_t at = cp * 3 + (std::size_t)k;
        if (verts->type == 'd') { double v; std::memcpy(&v, verts->data + at * 8, 8); return v * scale; }
        return (*verts)[at] * scale;
    };
    const std::size_t npv = pvi->size();
    if (!PolygonIndicesInRange(*pvi)) { err = "polygon index out of range"; return false; }
    auto cornerAt = [&](std::size_t i, bool& end) -> std::int32_t { return PolyCorner(*pvi, i, ncp, end); };
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
    const Layer ln = LayerOf(P.normals), lu = LayerOf(P.uvs), lc = LayerOf(P.colors), lm = LayerOf(P.materials);
    // Extra UV sets: `xw` floats per corner (2 per set), kept next to the corners; with none the code below is the
    // one-set code it was before.
    std::vector<Layer> lx;
    for (const MeshLayer& ml : P.extraUVs) lx.push_back(LayerOf(ml));
    const std::size_t xw = lx.size() * 2;
    bool anyByEdge = false;
    for (const Layer& L : lx) if (L.map == kByEdge) anyByEdge = true;
    std::vector<int> edgeOfCorner;   // only a ByEdge layer reads it
    if (ln.map == kByEdge || lu.map == kByEdge || lc.map == kByEdge || lm.map == kByEdge || anyByEdge)
        if (P.edges.present) {
            const Array* ed = &P.edges.a;
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
    std::size_t total = 0;
    for (const auto& kv : cornersOfSlot) total += kv.second;
    if (total > lim.maxCorners) { err = "mesh too large"; return false; }   // (the reader already refused such a file)
    // Each part keeps its distinct corners and an index list. Equal corners (byte for byte) are found with an
    // open-addressing table of vertex numbers (power-of-two size, at most two thirds full, linear probing; the size
    // changes only the work, never which vertex a corner finds).
    struct PartBuild { Part part; std::vector<std::uint32_t> table; std::uint32_t no = 0xFFFFFFFFu; std::vector<float> xs; };   // no: index in partList; xs: xw floats per vertex
    const std::uint32_t kEmpty = 0xFFFFFFFFu;
    std::map<int, PartBuild> parts;
    for (const auto& kv : cornersOfSlot) {
        PartBuild& pb = parts[kv.first];
        pb.part.slot = kv.first;
        pb.part.indices.reserve(kv.second);
        const std::size_t guess = std::min(kv.second, ncp + 16);   // distinct corners: usually about the vertex count
        pb.part.vertices.reserve(guess);
        if (xw) pb.xs.reserve(guess * xw);
        std::size_t cap = 64;
        while (cap * 2 < guess * 3) cap <<= 1;
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
    // A corner's hash with its extra UV values (x: xw floats) mixed in; without extra sets it is hashCorner alone.
    auto hashFull = [&](const Corner& c, const float* x) {
        std::uint64_t h = hashCorner(c);
        for (std::size_t k = 0; k < xw; ++k) {
            std::uint32_t b; std::memcpy(&b, x + k, 4);
            h = (h ^ b) * 0x9E3779B97F4A7C15ull; h ^= h >> 31;
        }
        return h;
    };
    auto addCorner = [&](PartBuild& pb, const Corner& c, const float* x) {
        std::vector<Corner>& vs = pb.part.vertices;
        if ((vs.size() + 1) * 3 > pb.table.size() * 2) {   // keep the table at most 2/3 full: double it and re-insert
            pb.table.assign(pb.table.size() * 2, kEmpty);
            const std::size_t mask = pb.table.size() - 1;
            for (std::size_t v = 0; v < vs.size(); ++v) {
                std::size_t at = (std::size_t)(xw ? hashFull(vs[v], pb.xs.data() + v * xw) : hashCorner(vs[v])) & mask;
                while (pb.table[at] != kEmpty) at = (at + 1) & mask;
                pb.table[at] = (std::uint32_t)v;
            }
        }
        const std::size_t mask = pb.table.size() - 1;
        std::size_t at = (std::size_t)(xw ? hashFull(c, x) : hashCorner(c)) & mask;
        for (;;) {
            const std::uint32_t u = pb.table[at];
            if (u == kEmpty) {
                pb.table[at] = (std::uint32_t)vs.size(); pb.part.indices.push_back((std::uint32_t)vs.size()); vs.push_back(c);
                if (xw) pb.xs.insert(pb.xs.end(), x, x + xw);
                return;
            }
            if (std::memcmp(&vs[u], &c, sizeof(Corner)) == 0 &&
                (!xw || std::memcmp(pb.xs.data() + (std::size_t)u * xw, x, xw * sizeof(float)) == 0)) { pb.part.indices.push_back(u); return; }
            at = (at + 1) & mask;
        }
    };
    std::vector<std::array<double, 3>> pts;
    std::vector<int> tris;
    // Corners are made in batches and then looked up in the tables: measured about a third faster on big meshes
    // than looking each one up as it is made (the lookups then do not compete with the reading of the layer arrays).
    const std::size_t kBatch = 2048;
    struct Pending { Corner c; std::uint32_t part; };   // 56 bytes (a pointer next to the corner would make it 64)
    std::vector<Pending> pending;
    std::vector<float> pendingX;        // xw floats per entry of `pending` (extra UV sets only)
    std::vector<PartBuild*> partList;   // `part` above indexes this
    pending.reserve(std::min(total, kBatch));   // flushed as soon as it reaches kBatch
    if (xw) pendingX.reserve(std::min(total, kBatch) * xw);
    auto flush = [&]() {
        for (std::size_t i = 0; i < pending.size(); ++i) addCorner(*partList[pending[i].part], pending[i].c, xw ? pendingX.data() + i * xw : nullptr);
        pending.clear(); pendingX.clear();
    };
    forEachPolygon([&](std::size_t fi, std::size_t start, std::size_t count, bool good) {
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
        PartBuild& part = parts[slot]; part.part.slot = slot;
        if (part.no == 0xFFFFFFFFu) { part.no = (std::uint32_t)partList.size(); partList.push_back(&part); }
        const std::uint32_t partNo = part.no;
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
            pending.push_back({c, partNo});
            for (const Layer& L : lx) {   // extra UV sets: (0, 0) where a set does not cover the corner, as for set 0
                double xu[2];
                if (Fetch(L, pv, fi, cp, edgeOfCorner, xu)) { pendingX.push_back((float)xu[0]); pendingX.push_back((float)xu[1]); }
                else { pendingX.push_back(0.0f); pendingX.push_back(0.0f); }
            }
            if (pending.size() >= kBatch) flush();
        }
        return true;
    });
    flush();   // (in the order they were made: the numbering is the same)
    for (auto& kv : parts) {
        PartBuild& pb = kv.second;
        if (pb.part.indices.empty()) continue;
        if (xw) {   // split the interleaved values into one array per set
            const std::size_t nv = pb.part.vertices.size();
            pb.part.extraUVs.assign(lx.size(), std::vector<float>());
            for (std::size_t k = 0; k < lx.size(); ++k) {
                std::vector<float>& a = pb.part.extraUVs[k];
                a.resize(nv * 2);
                for (std::size_t v = 0; v < nv; ++v) { a[v * 2] = pb.xs[v * xw + k * 2]; a[v * 2 + 1] = pb.xs[v * xw + k * 2 + 1]; }
            }
        }
        m.parts.push_back(std::move(pb.part));
    }
    return true;
}

} // namespace

namespace {
bool BuildFrom(const Document& doc, Document* steal, Scene& out, std::string* err, const BuildOptions& opt) {
    out = Scene();
    Builder b(doc, out, opt);
    b.steal = steal;
    const bool ok = b.Run();
    if (!ok) { out = Scene(); if (err) *err = b.err.empty() ? "FBX build failed" : b.err; }
    return ok;
}
} // namespace

bool Build(const Document& doc, Scene& out, std::string* err, const BuildOptions& opt) {
    return BuildFrom(doc, nullptr, out, err, opt);
}

bool BuildParts(Mesh& m, std::string* err, const Limits& lim, bool releasePolygons) {
    if (m.parts.empty()) {
        std::string e;
        if (!PartsOf(m, lim, e)) { m.parts.clear(); if (err) *err = e; return false; }
    }
    if (releasePolygons) m.polygons = Polygons();
    return true;
}

bool BuildClips(Scene& s, std::string* err) {
    if (!s.clipSource) return true;
    Animator& job = static_cast<Animator&>(*s.clipSource);
    const bool ok = job.Bake(s);
    if (!ok && err) *err = job.err.empty() ? "FBX clip build failed" : job.err;
    s.clipSource.reset();
    return ok;
}

bool BuildParts(Scene& s, std::string* err, const Limits& lim, bool releasePolygons) {
    for (Mesh& m : s.meshes) if (!BuildParts(m, err, lim, releasePolygons)) return false;
    return true;
}

bool LoadMemory(const std::uint8_t* data, std::size_t size, Scene& out, std::string* err, const BuildOptions& opt) {
    Document doc;
    if (!Parse(data, size, doc, err, opt.limits)) { out = Scene(); return false; }
    // The mesh arrays the Document decoded (zlib, ASCII) are handed to the scene instead of copied.
    return BuildFrom(doc, &doc, out, err, opt);
}

bool LoadFile(const std::string& path, Scene& out, std::string* err, const BuildOptions& opt) {
    {   // a folder opens as a stream on some systems and reports a huge size: the resize below threw std::bad_alloc
        std::error_code ec;
        if (!std::filesystem::is_regular_file(std::filesystem::u8path(path), ec)) { if (err) *err = "not a file: " + path; return false; }
    }
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) { if (err) *err = "cannot open " + path; return false; }
    // One read of the whole file at its size (a byte-by-byte stream iterator grew the buffer by doubling, slowly), into
    // a buffer that is not zero-filled first (the read writes every byte that is kept).
    ByteBuffer buf;
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
#if defined(OKAY_XBOX_UWP)
    return true;   // the Xbox UWP build has no ufbx: our reader is the only one (OKAY_OWN_FBX / OKAY_THIRD_PARTY cannot switch it off)
#endif
    if (g_own >= 0) return g_own != 0;
    if (okay::ThirdPartyForced()) { g_own = 0; return false; }   // OKAY_THIRD_PARTY=1: ufbx for every FBX file
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
bool FileKeysDefault() {
    static const bool v = [] { const char* e = std::getenv("OKAY_FBX_BAKE30"); return !(e && e[0] == '1'); }();
    return v;
}

} // namespace fbx
} // namespace okay
