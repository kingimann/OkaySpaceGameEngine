#pragma once
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Vec3.hpp"
#include <functional>

namespace okay {

/// Offline "map baking" (Unity-style lightmapping): pre-compute the scene's
/// lighting — including cast SHADOWS and ambient occlusion — into a static mesh's
/// per-face colors, so at runtime it renders unlit yet fully lit for near-zero
/// cost. Call once (in the editor) after the lights are set; the result is stored
/// in `mesh.triColors` and persisted with the scene.
///
/// `model`    transforms the mesh's local vertices into world space.
/// `base`     the material's albedo (each face color is base x incoming light).
/// `occluded` returns true if a shadow ray from `p` along unit `dir` hits geometry
///            within `maxDist` (the point is in shadow / occluded). Supplied by the
///            caller so this kernel needs no Scene/Physics dependency; pass an
///            always-false lambda to bake lighting with no shadows.
/// `aoSamples`  hemisphere rays per face for ambient occlusion (0 = skip AO).
inline void BakeFaceLighting(Mesh& mesh, const Mat4& model, const Color& base,
                             const std::function<bool(const Vec3&, const Vec3&, float)>& occluded,
                             int aoSamples = 6, float aoRadius = 2.5f) {
    const int nTris = mesh.TriangleCount();
    if (nTris == 0) return;
    mesh.triColors.assign(nTris, base);
    const auto& lights = SceneLights::List();
    const float eps = 2e-3f;

    // A small fixed set of hemisphere directions (deterministic, no RNG), tilted
    // into each face's normal frame below for AO.
    static const Vec3 kHemi[6] = {
        {0.0f, 1.0f, 0.0f}, {0.6f, 0.8f, 0.0f}, {-0.6f, 0.8f, 0.0f},
        {0.0f, 0.8f, 0.6f}, {0.0f, 0.8f, -0.6f}, {0.4f, 0.9f, 0.4f}};

    for (int t = 0; t < nTris; ++t) {
        int ia = mesh.triangles[t * 3], ib = mesh.triangles[t * 3 + 1], ic = mesh.triangles[t * 3 + 2];
        Vec3 a = model.MultiplyPoint(mesh.vertices[ia]);
        Vec3 b = model.MultiplyPoint(mesh.vertices[ib]);
        Vec3 c = model.MultiplyPoint(mesh.vertices[ic]);
        Vec3 centroid = (a + b + c) * (1.0f / 3.0f);
        Vec3 nrm = Vec3::Cross(b - a, c - a);
        float nl = nrm.Magnitude();
        Vec3 n = nl > 1e-8f ? nrm * (1.0f / nl) : Vec3{0, 1, 0};
        Vec3 origin = centroid + n * eps;

        Vec3 acc = SceneLights::AmbientAt(n);              // hemisphere-aware ambient
        if (aoSamples > 0 && occluded) {
            int taken = aoSamples < 6 ? aoSamples : 6, hits = 0;
            Vec3 up = Mathf::Abs(n.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
            Vec3 tx = Vec3::Cross(up, n).Normalized();
            Vec3 tz = Vec3::Cross(n, tx);
            for (int i = 0; i < taken; ++i) {
                const Vec3& h = kHemi[i];
                Vec3 dir = (tx * h.x + n * h.y + tz * h.z).Normalized();
                if (occluded(origin, dir, aoRadius)) ++hits;
            }
            acc = acc * (1.0f - 0.7f * (float)hits / (float)taken);   // up to 70% darkening
        }

        for (const auto& L : lights) {
            Vec3 ld; float dist;
            if (L.type == 0) { ld = L.dir.Normalized() * -1.0f; dist = 1e6f; }
            else {
                Vec3 toL = L.pos - centroid; dist = toL.Magnitude();
                ld = dist > 1e-5f ? toL * (1.0f / dist) : Vec3{0, 1, 0};
            }
            float ndl = Vec3::Dot(n, ld);
            if (ndl <= 0.0f) continue;
            if (occluded && occluded(origin, ld, dist)) continue;      // in shadow

            float atten = 1.0f;
            if (L.type != 0) {
                atten = (L.range > 0.0f) ? (1.0f - dist / L.range) : 0.0f;
                if (atten <= 0.0f) continue;
                atten *= atten;
                if (L.type == 2) {
                    float cs = Vec3::Dot(L.dir.Normalized(), ld * -1.0f);
                    float denom = L.cosInner - L.cosOuter;
                    float spot = denom > 1e-4f ? (cs - L.cosOuter) / denom
                                               : (cs >= L.cosOuter ? 1.0f : 0.0f);
                    if (spot <= 0.0f) continue;
                    if (spot > 1.0f) spot = 1.0f;
                    atten *= spot * spot;
                }
            }
            float k = ndl * atten;
            acc = acc + Vec3{L.color.x * k, L.color.y * k, L.color.z * k};
        }

        auto sat = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
        mesh.triColors[t] = Color{sat(base.r * acc.x), sat(base.g * acc.y),
                                  sat(base.b * acc.z), base.a};
    }
}

/// Möller–Trumbore ray/triangle intersection — the occlusion primitive for the
/// baker's shadow rays. Returns the hit distance in `outT` (only when it lands in
/// (eps, maxDist)); back-face culling off so single-sided geometry still occludes.
inline bool RayTriangle(const Vec3& o, const Vec3& d, const Vec3& v0, const Vec3& v1,
                        const Vec3& v2, float maxDist, float& outT) {
    Vec3 e1 = v1 - v0, e2 = v2 - v0;
    Vec3 p = Vec3::Cross(d, e2);
    float det = Vec3::Dot(e1, p);
    if (det > -1e-7f && det < 1e-7f) return false;     // ray parallel to triangle
    float inv = 1.0f / det;
    Vec3 tv = o - v0;
    float u = Vec3::Dot(tv, p) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    Vec3 q = Vec3::Cross(tv, e1);
    float v = Vec3::Dot(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    float tt = Vec3::Dot(e2, q) * inv;
    if (tt <= 1e-4f || tt >= maxDist) return false;
    outT = tt;
    return true;
}

} // namespace okay
