#pragma once
// Pieces shared by the own 3MF / AMF / X3D / DXF / LWO readers. Internal: not installed.
//   * number parsing that does not depend on the process locale (NumParse.hpp), finite and bounded
//   * a triangle "soup" (corner positions, optional UVs / normals) turned into an ImportedPart: smooth normals
//     with a crease angle, vertices welded per (position, normal cluster, uv), degenerate triangles dropped
//   * 3x4 / 4x4 transform helpers in double precision, and the Z-up -> Y-up root of the model
#include "NumParse.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {
namespace detail {
namespace rc {

constexpr double kMaxCoord = 1e15;   // larger coordinates would overflow the float arithmetic that follows

inline bool Ws(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

// One number from the whole of [b, e) (no trailing junk). Finite, |v| <= kMaxCoord.
inline bool ParseNumber(const char* b, const char* e, double& v) {
    while (b < e && Ws(*b)) ++b;
    while (e > b && Ws(e[-1])) --e;
    if (b >= e || e - b > 63) return false;
    char buf[64];
    std::memcpy(buf, b, (std::size_t)(e - b));
    buf[e - b] = 0;
    // strtod would also take "inf", "nan" and hex floats: only plain decimal numbers are file numbers
    for (const char* c = buf; *c; ++c) if (!((*c >= '0' && *c <= '9') || *c == '+' || *c == '-' || *c == '.' || *c == 'e' || *c == 'E')) return false;
    char* end = nullptr;
    v = StrToD(buf, &end);
    if (end != buf + (e - b)) return false;
    return std::isfinite(v) && std::fabs(v) <= kMaxCoord;
}
inline bool ParseNumber(const std::string& s, double& v) { return ParseNumber(s.data(), s.data() + s.size(), v); }

// A list of numbers separated by white space (and by commas when `commas`). False on any bad token or when
// more than `maxCount` numbers are present.
inline bool ParseList(const std::string& t, std::vector<double>& out, std::size_t maxCount, bool commas = false) {
    out.clear();
    const char* p = t.data();
    const char* end = p + t.size();
    for (;;) {
        while (p < end && (Ws(*p) || (commas && *p == ','))) ++p;
        if (p >= end) return true;
        const char* q = p;
        while (q < end && !Ws(*q) && !(commas && *q == ',')) ++q;
        double v;
        if (!ParseNumber(p, q, v)) return false;
        if (out.size() >= maxCount) return false;
        out.push_back(v);
        p = q;
    }
}

// Integers (index lists), same rules. Values must be within int range.
inline bool ParseIntList(const std::string& t, std::vector<long long>& out, std::size_t maxCount, bool commas = false) {
    out.clear();
    const char* p = t.data();
    const char* end = p + t.size();
    for (;;) {
        while (p < end && (Ws(*p) || (commas && *p == ','))) ++p;
        if (p >= end) return true;
        const char* q = p;
        while (q < end && !Ws(*q) && !(commas && *q == ',')) ++q;
        const char* d = p;
        bool neg = false;
        if (d < q && (*d == '-' || *d == '+')) { neg = *d == '-'; ++d; }
        if (d >= q || q - d > 10) return false;
        long long v = 0;
        for (; d < q; ++d) { if (*d < '0' || *d > '9') return false; v = v * 10 + (*d - '0'); }
        if (v > 2147483647LL) return false;
        if (out.size() >= maxCount) return false;
        out.push_back(neg ? -v : v);
        p = q;
    }
}

inline std::string Lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32); return s; }

// ---- 4x4 transforms, double precision, column-vector convention: p' = M p, m[col*4 + row] ----
struct M4 {
    double m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    double& at(int col, int row) { return m[col * 4 + row]; }
    double at(int col, int row) const { return m[col * 4 + row]; }
};
inline M4 Mul(const M4& a, const M4& b) {
    M4 r;
    for (int c = 0; c < 4; ++c) for (int row = 0; row < 4; ++row) {
        double s = 0;
        for (int k = 0; k < 4; ++k) s += a.at(k, row) * b.at(c, k);
        r.at(c, row) = s;
    }
    return r;
}
inline M4 Translate(double x, double y, double z) { M4 r; r.at(3, 0) = x; r.at(3, 1) = y; r.at(3, 2) = z; return r; }
inline M4 ScaleM(double x, double y, double z) { M4 r; r.at(0, 0) = x; r.at(1, 1) = y; r.at(2, 2) = z; return r; }
// Rotation by `angle` radians about the (not necessarily unit) axis; a zero axis gives the identity.
inline M4 AxisAngle(double ax, double ay, double az, double angle) {
    M4 r;
    const double l = std::sqrt(ax * ax + ay * ay + az * az);
    if (!(l > 1e-12) || !std::isfinite(angle)) return r;
    const double x = ax / l, y = ay / l, z = az / l, c = std::cos(angle), s = std::sin(angle), t = 1 - c;
    r.at(0, 0) = t * x * x + c;     r.at(1, 0) = t * x * y - s * z; r.at(2, 0) = t * x * z + s * y;
    r.at(0, 1) = t * x * y + s * z; r.at(1, 1) = t * y * y + c;     r.at(2, 1) = t * y * z - s * x;
    r.at(0, 2) = t * x * z - s * y; r.at(1, 2) = t * y * z + s * x; r.at(2, 2) = t * z * z + c;
    return r;
}
inline double Det3(const M4& a) {
    return a.at(0, 0) * (a.at(1, 1) * a.at(2, 2) - a.at(2, 1) * a.at(1, 2)) - a.at(1, 0) * (a.at(0, 1) * a.at(2, 2) - a.at(2, 1) * a.at(0, 2)) +
           a.at(2, 0) * (a.at(0, 1) * a.at(1, 2) - a.at(1, 1) * a.at(0, 2));
}
inline bool Finite(const M4& a) { for (double v : a.m) if (!std::isfinite(v) || std::fabs(v) > 1e30) return false; return true; }
inline void Apply(const M4& a, double x, double y, double z, double& ox, double& oy, double& oz) {
    ox = a.at(0, 0) * x + a.at(1, 0) * y + a.at(2, 0) * z + a.at(3, 0);
    oy = a.at(0, 1) * x + a.at(1, 1) * y + a.at(2, 1) * z + a.at(3, 1);
    oz = a.at(0, 2) * x + a.at(1, 2) * y + a.at(2, 2) * z + a.at(3, 2);
}

// ---- triangle soup -> ImportedPart ------------------------------------------------------------------------
struct Soup {
    std::vector<Vec3> pos;
    std::vector<std::uint32_t> ci;   // corner position indices, 3 per triangle
    std::vector<Vec2> cuv;           // empty, or one per corner
    std::vector<Vec3> cnrm;          // empty, or one explicit unit normal per corner (then no crease smoothing)
    std::size_t Tris() const { return ci.size() / 3; }
};

struct BuildStats { int degenerate = 0; };

// creaseDeg: faces meeting at a vertex share a smooth normal when their normals are within this angle. 0 = flat,
// >= 180 = fully smooth. Positions are never merged here: two corners share an output vertex only when they use the
// same position index, the same normal cluster and the same UV.
inline bool BuildMesh(const Soup& s, double creaseDeg, Mesh& out, BuildStats* st, std::size_t maxOutVerts = 40000000) {
    out = Mesh{};
    const std::size_t nt = s.Tris();
    const bool haveUv = s.cuv.size() == s.ci.size() && !s.cuv.empty();
    const bool haveN = s.cnrm.size() == s.ci.size() && !s.cnrm.empty();
    // face normals and corner angle weights
    std::vector<std::uint8_t> keep(nt, 1);
    std::vector<Vec3> fn(nt);
    std::vector<std::array<float, 3>> ang(nt);
    for (std::size_t t = 0; t < nt; ++t) {
        const Vec3 a = s.pos[s.ci[t * 3]], b = s.pos[s.ci[t * 3 + 1]], c = s.pos[s.ci[t * 3 + 2]];
        const double ux = (double)b.x - a.x, uy = (double)b.y - a.y, uz = (double)b.z - a.z;
        const double vx = (double)c.x - a.x, vy = (double)c.y - a.y, vz = (double)c.z - a.z;
        double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        const double l = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (!(l > 1e-30) || !std::isfinite(l)) { keep[t] = 0; if (st) ++st->degenerate; continue; }
        fn[t] = Vec3{(float)(nx / l), (float)(ny / l), (float)(nz / l)};
        auto angle = [&](const Vec3& p, const Vec3& q, const Vec3& r) {
            const double ax = (double)q.x - p.x, ay = (double)q.y - p.y, az = (double)q.z - p.z;
            const double bx = (double)r.x - p.x, by = (double)r.y - p.y, bz = (double)r.z - p.z;
            const double la = std::sqrt(ax * ax + ay * ay + az * az), lb = std::sqrt(bx * bx + by * by + bz * bz);
            if (!(la > 0) || !(lb > 0)) return 0.0f;
            double d = (ax * bx + ay * by + az * bz) / (la * lb);
            d = std::max(-1.0, std::min(1.0, d));
            return (float)std::acos(d);
        };
        ang[t] = {angle(a, b, c), angle(b, c, a), angle(c, a, b)};
    }
    // per-position incident corners (CSR)
    const std::size_t np = s.pos.size();
    std::vector<std::uint32_t> start(np + 1, 0), corner;
    for (std::size_t t = 0; t < nt; ++t) if (keep[t]) for (int k = 0; k < 3; ++k) ++start[s.ci[t * 3 + (std::size_t)k] + 1];
    for (std::size_t i = 0; i < np; ++i) start[i + 1] += start[i];
    corner.resize(start[np]);
    {
        std::vector<std::uint32_t> fill(start.begin(), start.end() - 1);
        for (std::size_t t = 0; t < nt; ++t) if (keep[t]) for (int k = 0; k < 3; ++k) corner[fill[s.ci[t * 3 + (std::size_t)k]]++] = (std::uint32_t)(t * 3 + (std::size_t)k);
    }
    const double cosCrease = creaseDeg >= 179.9 ? -2.0 : std::cos(creaseDeg * 3.14159265358979323846 / 180.0);
    constexpr int kMaxClusters = 8;
    std::vector<std::uint8_t> cluster(nt * 3, 0);
    // the clusters of one position are made one after the other, so they sit side by side in one array
    std::vector<Vec3> csum;                           // summed angle-weighted normals, one per cluster
    std::vector<std::uint32_t> cbase(np, 0);          // first cluster of each position
    if (!haveN) {
        csum.reserve(corner.size() / 2 + 8);
        Vec3 rep[kMaxClusters];
        for (std::size_t p = 0; p < np; ++p) {
            int used = 0;
            cbase[p] = (std::uint32_t)csum.size();
            for (std::uint32_t ci = start[p]; ci < start[p + 1]; ++ci) {
                const std::uint32_t c = corner[ci];
                const std::size_t t = c / 3, k = c % 3;
                const Vec3& f = fn[t];
                int best = -1;
                double bestDot = -3;
                for (int j = 0; j < used; ++j) {
                    const double d = (double)rep[j].x * f.x + (double)rep[j].y * f.y + (double)rep[j].z * f.z;
                    if (d >= cosCrease) { best = j; break; }
                }
                if (best < 0) {
                    if (used < kMaxClusters) { rep[used] = f; csum.push_back(Vec3{0, 0, 0}); best = used++; }
                    else {   // out of clusters (a hostile vertex): join the closest one
                        for (int j = 0; j < used; ++j) { const double d = (double)rep[j].x * f.x + (double)rep[j].y * f.y + (double)rep[j].z * f.z; if (d > bestDot) { bestDot = d; best = j; } }
                    }
                }
                cluster[c] = (std::uint8_t)best;
                Vec3& n = csum[cbase[p] + (std::uint32_t)best];
                const float w = ang[t][k];
                n = Vec3{n.x + f.x * w, n.y + f.y * w, n.z + f.z * w};
            }
        }
    }
    struct Key {
        std::uint32_t w[7];
        bool operator==(const Key& o) const { return std::memcmp(w, o.w, sizeof(w)) == 0; }
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const {
            std::uint64_t h = 0x9E3779B97F4A7C15ull;
            for (std::uint32_t v : k.w) { h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2); h *= 0xC2B2AE3D27D4EB4Full; }
            return (std::size_t)(h ^ (h >> 29));
        }
    };
    std::unordered_map<Key, int, KeyHash> map;
    map.reserve(np);
    auto bits = [](float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; };
    for (std::size_t t = 0; t < nt; ++t) {
        if (!keep[t]) continue;
        for (int k = 0; k < 3; ++k) {
            const std::size_t c = t * 3 + (std::size_t)k;
            Key key;
            key.w[0] = s.ci[c];
            key.w[1] = haveN ? 0u : (std::uint32_t)cluster[c];
            key.w[2] = haveUv ? bits(s.cuv[c].x) : 0u; key.w[3] = haveUv ? bits(s.cuv[c].y) : 0u;
            key.w[4] = haveN ? bits(s.cnrm[c].x) : 0u; key.w[5] = haveN ? bits(s.cnrm[c].y) : 0u; key.w[6] = haveN ? bits(s.cnrm[c].z) : 0u;
            auto it = map.find(key);
            if (it == map.end()) {
                if (out.vertices.size() >= maxOutVerts) return false;
                const int idx = (int)out.vertices.size();
                map.emplace(key, idx);
                out.vertices.push_back(s.pos[s.ci[c]]);
                Vec3 n = haveN ? s.cnrm[c] : csum[cbase[s.ci[c]] + cluster[c]];
                const double l = std::sqrt((double)n.x * n.x + (double)n.y * n.y + (double)n.z * n.z);
                n = l > 1e-20 ? Vec3{(float)(n.x / l), (float)(n.y / l), (float)(n.z / l)} : fn[t];
                out.normals.push_back(n);
                if (haveUv) out.uvs.push_back(s.cuv[c]);
                out.triangles.push_back(idx);
            } else out.triangles.push_back(it->second);
        }
    }
    return true;
}

// The model's root node, Z-up to Y-up like the 3DS reader: (x, y, z) -> (x, z, -y). `flatten` bakes that into the
// vertices and keeps the root at the identity.
inline void ApplyUpAxis(ImportedModel& m, bool zUp, bool flatten) {
    if (!zUp || m.nodes.empty()) return;
    if (!flatten) {
        // rotation of -90 degrees about X: (x,y,z) -> (x, z, -y)
        const float h = 0.70710678118654752f;
        m.nodes[0].rotation = Quat{-h, 0, 0, h};
        return;
    }
    for (ImportedPart& p : m.parts) {
        for (Vec3& v : p.mesh.vertices) v = Vec3{v.x, v.z, -v.y};
        for (Vec3& v : p.mesh.normals) v = Vec3{v.x, v.z, -v.y};
    }
}

inline bool PartInRange(const ImportedPart& p) {
    for (const Vec3& v : p.mesh.vertices) if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return false;
    for (const Vec3& v : p.mesh.normals) if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return false;
    return true;
}

inline std::string ReadAllBounded(std::FILE* fp, std::size_t cap, bool* tooLarge) {
    std::string bytes;
    char buf[65536];
    if (tooLarge) *tooLarge = false;
    for (;;) {
        const std::size_t k = std::fread(buf, 1, sizeof(buf), fp);
        if (k == 0) break;
        if (bytes.size() + k > cap) { if (tooLarge) *tooLarge = true; return std::string(); }
        bytes.append(buf, k);
    }
    return bytes;
}

} // namespace rc
} // namespace detail
} // namespace okay
