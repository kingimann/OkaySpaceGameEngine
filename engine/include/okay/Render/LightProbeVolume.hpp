#pragma once
// A box-shaped grid of baked light probes (L2 spherical harmonics per probe) and
// the CPU reference for sampling it: trilinear blend of the 8 nearest probes, then
// the cosine-convolved ambient lookup. The GPU shaders will mirror SampleShAt and
// ProbeAmbientAt. Also holds the binary serialization (magic, version, sizes,
// bounds, coefficients, CRC32) with checks for hostile input.
// See docs/foundations/global_illumination.md.
#include "okay/Render/GiSH.hpp"
#include "okay/Render/Lighting.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace okay {

struct LightProbeVolume {
    Vec3 boundsMin{-5, 0, -5}, boundsMax{5, 5, 5};
    int nx = 1, ny = 1, nz = 1;          // probes per axis (>= 1)
    float edgeFade = 0.0f;               // world units over which the effect fades out inside the box edge (0 = hard)
    std::vector<ShRGB> probes;           // nx*ny*nz, index = x + nx*(y + ny*z)

    static constexpr int kMaxProbes = 1 << 18;

    int Count() const { return nx * ny * nz; }
    bool Valid() const {
        return nx >= 1 && ny >= 1 && nz >= 1 && (int64_t)nx * ny * nz <= kMaxProbes &&
               (int)probes.size() == nx * ny * nz && Finite(boundsMin) && Finite(boundsMax) &&
               boundsMin.x <= boundsMax.x && boundsMin.y <= boundsMax.y && boundsMin.z <= boundsMax.z;
    }
    /// Size the grid and zero the probes. False (and untouched) when the size is bad.
    bool Resize(int x, int y, int z) {
        if (x < 1 || y < 1 || z < 1 || (int64_t)x * y * z > kMaxProbes) return false;
        nx = x; ny = y; nz = z;
        probes.assign((size_t)x * y * z, ShRGB{});
        return true;
    }
    int Index(int x, int y, int z) const { return x + nx * (y + ny * z); }
    /// World position of probe (x,y,z). A one-probe axis sits at the box centre.
    Vec3 ProbePosition(int x, int y, int z) const {
        return {AxisPos(boundsMin.x, boundsMax.x, x, nx), AxisPos(boundsMin.y, boundsMax.y, y, ny),
                AxisPos(boundsMin.z, boundsMax.z, z, nz)};
    }
    bool Contains(const Vec3& p) const {
        return p.x >= boundsMin.x && p.x <= boundsMax.x && p.y >= boundsMin.y && p.y <= boundsMax.y &&
               p.z >= boundsMin.z && p.z <= boundsMax.z;
    }

    /// SH at world point `p`: trilinear blend of the 8 surrounding probes (the point
    /// is clamped into the box). Blending coefficients equals blending results.
    ShRGB SampleShAt(const Vec3& p) const {
        if (probes.empty()) return ShRGB{};
        int i0[3], i1[3]; float f[3];
        Cell(p.x, boundsMin.x, boundsMax.x, nx, i0[0], i1[0], f[0]);
        Cell(p.y, boundsMin.y, boundsMax.y, ny, i0[1], i1[1], f[1]);
        Cell(p.z, boundsMin.z, boundsMax.z, nz, i0[2], i1[2], f[2]);
        ShRGB out;
        for (int dz = 0; dz < 2; ++dz)
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const float w = (dx ? f[0] : 1.0f - f[0]) * (dy ? f[1] : 1.0f - f[1]) *
                                    (dz ? f[2] : 1.0f - f[2]);
                    if (w <= 0.0f) continue;
                    const ShRGB& s = probes[Index(i0[0] + dx * (i1[0] - i0[0]), i0[1] + dy * (i1[1] - i0[1]),
                                                  i0[2] + dz * (i1[2] - i0[2]))];
                    for (int k = 0; k < kShCoeffs; ++k) out.c[k] = out.c[k] + s.c[k] * w;
                }
        return out;
    }

    /// Weight 0..1 of the probes at `p`: 0 outside the box, 1 deeper than edgeFade.
    float Weight(const Vec3& p) const {
        if (!Contains(p)) return 0.0f;
        if (edgeFade <= 0.0f) return 1.0f;
        float d = std::fmin(std::fmin(p.x - boundsMin.x, boundsMax.x - p.x),
                  std::fmin(std::fmin(p.y - boundsMin.y, boundsMax.y - p.y),
                            std::fmin(p.z - boundsMin.z, boundsMax.z - p.z)));
        float w = d / edgeFade;
        return w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w);
    }

    // ---- serialization --------------------------------------------------------
    static constexpr uint32_t kMagic = 0x49474B4Fu;   // "OKGI" little endian
    static constexpr uint32_t kVersion = 1;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> o;
        PutU32(o, kMagic); PutU32(o, kVersion);
        PutU32(o, (uint32_t)nx); PutU32(o, (uint32_t)ny); PutU32(o, (uint32_t)nz);
        PutF(o, boundsMin.x); PutF(o, boundsMin.y); PutF(o, boundsMin.z);
        PutF(o, boundsMax.x); PutF(o, boundsMax.y); PutF(o, boundsMax.z);
        for (const ShRGB& s : probes)
            for (int k = 0; k < kShCoeffs; ++k) { PutF(o, s.c[k].x); PutF(o, s.c[k].y); PutF(o, s.c[k].z); }
        PutU32(o, Crc32(o.data(), o.size()));
        return o;
    }

    /// Parse. Checks sizes before allocating, rejects NaN/inf, bad bounds and a bad
    /// checksum. On failure returns false, sets `err` and leaves `out` unchanged.
    static bool Deserialize(const uint8_t* data, size_t size, LightProbeVolume& out, std::string* err = nullptr) {
        auto fail = [&](const char* m) { if (err) *err = m; return false; };
        constexpr size_t kHeader = 4 + 4 + 12 + 24;
        if (!data || size < kHeader + 4) return fail("too short");
        size_t pos = 0;
        if (GetU32(data, pos) != kMagic) return fail("bad magic");
        if (GetU32(data, pos) != kVersion) return fail("unsupported version");
        const uint32_t ux = GetU32(data, pos), uy = GetU32(data, pos), uz = GetU32(data, pos);
        if (ux < 1 || uy < 1 || uz < 1 || ux > kMaxProbes || uy > kMaxProbes || uz > kMaxProbes)
            return fail("bad grid size");
        const uint64_t n = (uint64_t)ux * uy * uz;
        if (n > (uint64_t)kMaxProbes) return fail("too many probes");
        const uint64_t expect = kHeader + n * kShCoeffs * 12 + 4;
        if ((uint64_t)size != expect) return fail("size does not match grid");
        const uint32_t stored = ReadU32(data + size - 4);
        if (Crc32(data, size - 4) != stored) return fail("checksum mismatch");
        LightProbeVolume v;
        v.nx = (int)ux; v.ny = (int)uy; v.nz = (int)uz;
        v.boundsMin = {GetF(data, pos), GetF(data, pos), GetF(data, pos)};
        v.boundsMax = {GetF(data, pos), GetF(data, pos), GetF(data, pos)};
        if (!Finite(v.boundsMin) || !Finite(v.boundsMax) || v.boundsMin.x > v.boundsMax.x ||
            v.boundsMin.y > v.boundsMax.y || v.boundsMin.z > v.boundsMax.z) return fail("bad bounds");
        v.probes.resize((size_t)n);
        for (ShRGB& s : v.probes)
            for (int k = 0; k < kShCoeffs; ++k) {
                s.c[k] = {GetF(data, pos), GetF(data, pos), GetF(data, pos)};
                if (!Finite(s.c[k])) return fail("non-finite coefficient");
            }
        out = std::move(v);
        return true;
    }
    static bool Deserialize(const std::vector<uint8_t>& b, LightProbeVolume& out, std::string* err = nullptr) {
        return Deserialize(b.data(), b.size(), out, err);
    }

    static uint32_t Crc32(const uint8_t* d, size_t n) {
        uint32_t c = 0xFFFFFFFFu;
        for (size_t i = 0; i < n; ++i) {
            c ^= d[i];
            for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
        return ~c;
    }

private:
    static bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
    static float AxisPos(float lo, float hi, int i, int n) {
        return n <= 1 ? 0.5f * (lo + hi) : lo + (hi - lo) * ((float)i / (float)(n - 1));
    }
    static void Cell(float p, float lo, float hi, int n, int& i0, int& i1, float& f) {
        if (n <= 1 || !(hi > lo) || !std::isfinite(p)) { i0 = i1 = 0; f = 0.0f; return; }
        float g = (p - lo) / (hi - lo) * (float)(n - 1);
        if (g < 0.0f) g = 0.0f;
        if (g > (float)(n - 1)) g = (float)(n - 1);
        i0 = (int)g; if (i0 > n - 2) i0 = n - 2;
        i1 = i0 + 1; f = g - (float)i0;
    }
    static void PutU32(std::vector<uint8_t>& o, uint32_t v) {
        for (int i = 0; i < 4; ++i) o.push_back((uint8_t)(v >> (8 * i)));
    }
    static void PutF(std::vector<uint8_t>& o, float f) { uint32_t u; std::memcpy(&u, &f, 4); PutU32(o, u); }
    static uint32_t ReadU32(const uint8_t* p) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    static uint32_t GetU32(const uint8_t* d, size_t& pos) { uint32_t v = ReadU32(d + pos); pos += 4; return v; }
    static float GetF(const uint8_t* d, size_t& pos) { uint32_t u = GetU32(d, pos); float f; std::memcpy(&f, &u, 4); return f; }
};

/// Ambient light for a world point and surface normal from a baked volume: the
/// colour a white surface takes from the indirect light (engine units, see the
/// design page). Outside the volume (or with none), and across the fade band,
/// it falls back to `fallback` (normally SceneLights::AmbientAt(n)).
inline Vec3 ProbeAmbientAt(const LightProbeVolume* vol, const Vec3& p, const Vec3& n, const Vec3& fallback) {
    if (!vol || vol->probes.empty()) return fallback;
    const float w = vol->Weight(p);
    if (w <= 0.0f) return fallback;
    const Vec3 a = ShAmbient(vol->SampleShAt(p), n.SqrMagnitude() > 1e-12f ? n.Normalized() : Vec3{0, 1, 0});
    return w >= 1.0f ? a : fallback + (a - fallback) * w;
}

/// Same, with the scene's current ambient as the fallback. For the software renderer.
inline Vec3 ProbeAmbientAt(const LightProbeVolume* vol, const Vec3& p, const Vec3& n) {
    return ProbeAmbientAt(vol, p, n, SceneLights::AmbientAt(n));
}

} // namespace okay
