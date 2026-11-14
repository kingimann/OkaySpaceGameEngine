#pragma once
// CPU path-tracing baker for light probe volumes. Describes the static scene
// (triangles with a diffuse albedo and an emissive colour), the lights and the
// sky; traces diffuse bounces with next-event estimation for sun, point and spot
// lights; projects the radiance seen from each probe onto L2 spherical harmonics.
//
// Units match the engine shading (surface = albedo * (ambient + sum of lights)):
// a uniform sky of colour L gives probe ambient L; a light's `color*N.L*atten` is
// reflected as albedo * that, with no 1/pi. Direct light seen by a probe itself
// is not baked (the renderer already adds lamps); light that bounced is.
// See docs/foundations/global_illumination.md.
#include "okay/Render/GiBvh.hpp"
#include "okay/Render/LightProbeVolume.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Math/Mat4.hpp"
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace okay {

struct GiMaterial {
    Vec3 albedo{0.7f, 0.7f, 0.7f};   // diffuse reflectance, clamped to [0,1] when baking
    Vec3 emissive{0, 0, 0};          // radiance it gives off (engine units)
};

/// Sky as a three-colour gradient (same idea as the renderers' sky): horizon to
/// top for directions above the horizon, horizon to bottom below.
struct GiSky {
    Vec3 top{0.25f, 0.25f, 0.25f}, horizon{0.25f, 0.25f, 0.25f}, bottom{0.25f, 0.25f, 0.25f};
    Vec3 Radiance(const Vec3& d) const {
        if (d.y >= 0.0f) { const float t = d.y > 1.0f ? 1.0f : d.y; return horizon + (top - horizon) * t; }
        const float t = -d.y > 1.0f ? 1.0f : -d.y;
        return horizon + (bottom - horizon) * t;
    }
};

struct GiScene {
    std::vector<GiTri> tris;
    std::vector<GiMaterial> materials;
    std::vector<LightSample> lights;     // types 0 sun, 1 point, 2 spot (3 area is treated as a point)
    GiSky sky;

    int AddMaterial(const GiMaterial& m) { materials.push_back(m); return (int)materials.size() - 1; }
    void AddTriangle(const Vec3& a, const Vec3& b, const Vec3& c, int material) { tris.push_back({a, b, c, material}); }
    /// Add a mesh's triangles, transformed by `model`, with one material.
    void AddMesh(const Mesh& mesh, const Mat4& model, int material) {
        const int n = mesh.TriangleCount();
        for (int t = 0; t < n; ++t) {
            const int ia = mesh.triangles[t * 3], ib = mesh.triangles[t * 3 + 1], ic = mesh.triangles[t * 3 + 2];
            const int nv = (int)mesh.vertices.size();
            if (ia < 0 || ib < 0 || ic < 0 || ia >= nv || ib >= nv || ic >= nv) continue;
            AddTriangle(model.MultiplyPoint(mesh.vertices[ia]), model.MultiplyPoint(mesh.vertices[ib]),
                        model.MultiplyPoint(mesh.vertices[ic]), material);
        }
    }
    /// Axis-aligned box with all six faces, triangles wound outward.
    void AddBox(const Vec3& lo, const Vec3& hi, int material) {
        const Vec3 p[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                           {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
        static const int f[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
                                     {3, 6, 2}, {3, 7, 6}, {0, 4, 7}, {0, 7, 3}, {1, 2, 6}, {1, 6, 5}};
        for (auto& t : f) AddTriangle(p[t[0]], p[t[1]], p[t[2]], material);
    }
};

/// Small deterministic generator (PCG32). Same seed, same stream, on any platform.
struct GiRng {
    uint64_t state = 0x853c49e6748fea9bULL, inc = 0xda3e39cb94b95bdbULL;
    explicit GiRng(uint64_t seed = 1, uint64_t seq = 1) {
        state = 0; inc = (seq << 1u) | 1u; Next(); state += seed; Next();
    }
    uint32_t Next() {
        const uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        const uint32_t xs = (uint32_t)(((old >> 18u) ^ old) >> 27u);
        const uint32_t rot = (uint32_t)(old >> 59u);
        return (xs >> rot) | (xs << ((0u - rot) & 31u));
    }
    /// Uniform in [0,1).
    float Float() { return (float)(Next() >> 8) * (1.0f / 16777216.0f); }
};

/// Mix the user seed with a probe index into an independent stream seed.
inline uint64_t GiMixSeed(uint64_t seed, uint64_t index) {
    uint64_t z = seed + 0x9E3779B97F4A7C15ULL * (index + 1);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

struct GiBakeSettings {
    int samplesPerProbe = 256;   // radiance rays per probe, over the full sphere
    int maxBounces = 4;          // diffuse bounces after the first hit
    uint64_t seed = 1;
    float maxRadiance = 50.0f;   // per-sample clamp that tames fireflies (bias, but tiny)
};

/// Tracer: radiance arriving along a ray, in engine units.
class GiTracer {
public:
    GiTracer(const GiScene& scene, const GiBvh& bvh, const GiBakeSettings& s) : sc_(scene), bvh_(bvh), set_(s) {}

    Vec3 Radiance(Vec3 o, Vec3 d, GiRng& rng) const {
        Vec3 L{0, 0, 0}, thr{1, 1, 1};
        for (int bounce = 0;; ++bounce) {
            GiRay ray; ray.o = o; ray.d = d;
            GiHit h;
            if (!bvh_.Closest(ray, h)) { L = L + thr * sc_.sky.Radiance(d); break; }
            const GiMaterial mat = Mat(h);
            L = L + thr * mat.emissive;
            Vec3 n = h.n; if (Vec3::Dot(n, d) > 0.0f) n = -n;       // double sided: face the ray
            const Vec3 p = o + d * h.t;
            const Vec3 org = p + n * Eps(p);
            const Vec3 alb = Clamp01(mat.albedo);
            L = L + thr * (alb * Direct(org, n));
            if (bounce >= set_.maxBounces) break;
            thr = thr * alb;
            float pr = std::fmax(thr.x, std::fmax(thr.y, thr.z));
            if (!(pr > 1e-4f)) break;
            if (bounce >= 2) {                                      // Russian roulette, unbiased
                const float q = pr > 0.95f ? 0.95f : pr;
                if (rng.Float() >= q) break;
                thr = thr * (1.0f / q);
            }
            o = org; d = CosineHemisphere(n, rng.Float(), rng.Float());
        }
        return L;
    }

    /// Light from the lamps arriving at `p` with normal `n` (color*N.L*atten, shadowed).
    Vec3 Direct(const Vec3& p, const Vec3& n) const {
        Vec3 acc{0, 0, 0};
        for (const LightSample& L : sc_.lights) {
            Vec3 ld; float dist;
            if (L.type == 0) { ld = L.dir.Normalized() * -1.0f; dist = 1e6f; }
            else {
                Vec3 to = L.pos - p; dist = to.Magnitude();
                if (!(dist > 1e-5f)) continue;
                ld = to * (1.0f / dist);
            }
            const float ndl = Vec3::Dot(n, ld);
            if (ndl <= 0.0f) continue;
            float atten = 1.0f;
            if (L.type != 0) {
                atten = LightAttenuation(dist, L.range, L.falloff);
                if (atten <= 0.0f) continue;
                if (L.type == 2) {
                    const float cs = Vec3::Dot(L.dir.Normalized(), ld * -1.0f);
                    const float denom = L.cosInner - L.cosOuter;
                    float spot = denom > 1e-4f ? (cs - L.cosOuter) / denom : (cs >= L.cosOuter ? 1.0f : 0.0f);
                    if (spot <= 0.0f) continue;
                    if (spot > 1.0f) spot = 1.0f;
                    atten *= spot * spot;
                }
            }
            GiRay sr; sr.o = p; sr.d = ld; sr.tMax = dist - (L.type == 0 ? 0.0f : 2e-3f);
            if (bvh_.Any(sr)) continue;
            acc = acc + L.color * (ndl * atten);
        }
        return acc;
    }

    static Vec3 CosineHemisphere(const Vec3& n, float u1, float u2) {
        const float r = std::sqrt(u1), phi = 2.0f * kGiPi * u2;
        const float x = r * std::cos(phi), y = r * std::sin(phi), z = std::sqrt(std::fmax(0.0f, 1.0f - u1));
        const Vec3 up = std::fabs(n.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        const Vec3 tx = Vec3::Cross(up, n).Normalized();
        const Vec3 ty = Vec3::Cross(n, tx);
        return (tx * x + ty * y + n * z).Normalized();
    }
    static Vec3 UniformSphere(float u1, float u2) {
        const float z = 1.0f - 2.0f * u1, r = std::sqrt(std::fmax(0.0f, 1.0f - z * z)), phi = 2.0f * kGiPi * u2;
        return {r * std::cos(phi), r * std::sin(phi), z};
    }

private:
    const GiScene& sc_; const GiBvh& bvh_; const GiBakeSettings& set_;
    static float Eps(const Vec3& p) {
        return 1e-3f * (1.0f + std::fmax(std::fabs(p.x), std::fmax(std::fabs(p.y), std::fabs(p.z))) * 0.01f);
    }
    static Vec3 Clamp01(const Vec3& v) {
        auto c = [](float x) { return !(x > 0.0f) ? 0.0f : (x > 1.0f ? 1.0f : x); };
        return {c(v.x), c(v.y), c(v.z)};
    }
    GiMaterial Mat(const GiHit& h) const {
        // h.tri indexes the original triangle list, which is sc_.tris.
        const int m = sc_.tris[h.tri].material;
        if (m >= 0 && m < (int)sc_.materials.size()) return sc_.materials[m];
        return GiMaterial{};
    }
};

enum class GiBakeResult { Done, Cancelled, BadInput };

/// Progress callback: fraction 0..1 after each probe; return false to cancel.
using GiProgressFn = std::function<bool(float)>;

/// Radiance projected onto SH as seen from `pos`. Deterministic for a given rng.
inline ShRGB GiBakeProbe(const GiTracer& tr, const GiBakeSettings& set, const Vec3& pos, GiRng& rng) {
    ShRGB sh;
    const int n = set.samplesPerProbe < 1 ? 1 : set.samplesPerProbe;
    const float w = 4.0f * kGiPi / (float)n;
    // Stratify over a sqrt(n) x sqrt(n) jittered grid on the sphere's (z, phi) square.
    int side = (int)std::sqrt((double)n); if (side < 1) side = 1;
    for (int i = 0; i < n; ++i) {
        float u1, u2;
        if (i < side * side) {
            u1 = ((float)(i % side) + rng.Float()) / (float)side;
            u2 = ((float)(i / side) + rng.Float()) / (float)side;
        } else { u1 = rng.Float(); u2 = rng.Float(); }
        const Vec3 d = GiTracer::UniformSphere(u1, u2);
        Vec3 r = tr.Radiance(pos, d, rng);
        const float m = std::fmax(r.x, std::fmax(r.y, r.z));
        if (m > set.maxRadiance) r = r * (set.maxRadiance / m);
        ShAddSample(sh, d, r, w);
    }
    return sh;
}

/// Bake every probe of `vol` (its grid and bounds must already be set; see
/// LightProbeVolume::Resize). Results do not depend on probe order.
inline GiBakeResult GiBake(const GiScene& scene, LightProbeVolume& vol, const GiBakeSettings& set,
                           const GiProgressFn& progress = nullptr) {
    if (!vol.Valid() || set.maxBounces < 0) return GiBakeResult::BadInput;
    GiBvh bvh; bvh.Build(scene.tris);
    // The BVH drops degenerate triangles, but hit.tri indexes the original list, so materials stay valid.
    GiTracer tracer(scene, bvh, set);
    const int total = vol.Count();
    for (int z = 0; z < vol.nz; ++z)
        for (int y = 0; y < vol.ny; ++y)
            for (int x = 0; x < vol.nx; ++x) {
                const int idx = vol.Index(x, y, z);
                GiRng rng(GiMixSeed(set.seed, (uint64_t)idx), (uint64_t)idx);
                vol.probes[idx] = GiBakeProbe(tracer, set, vol.ProbePosition(x, y, z), rng);
                if (progress && !progress((float)(idx + 1) / (float)total)) return GiBakeResult::Cancelled;
            }
    return GiBakeResult::Done;
}

} // namespace okay
