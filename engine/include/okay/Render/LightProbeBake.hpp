#pragma once
// ---------------------------------------------------------------------------
// Bake a LightProbeGrid for a scene (editor: Lighting window > Light Probes > Bake probes; also used by tests).
//
// For every probe, 64 directions (a Fibonacci spiral over the sphere) are traced against the scene's triangles. A ray that
// escapes sees the sky (the scene's sky colours, scaled so that a fully open sky gives the scene's ambient level). A ray that
// hits a surface sees that surface's colour lit once by the scene's lights (the sun with a shadow ray, the other lights without
// shadows), plus the ambient level: one bounce, which is what makes a red wall tint the floor beside it and a roof darken what
// is under it. The radiance samples are projected onto first-order spherical harmonics (LightProbes.hpp).
//
// Written from the definition of SH projection (L_lm = 4*pi/N * sum radiance * Y_lm) and ray-triangle intersection; no code
// from another project. Brute force over the triangles (capped), which is fine for a one-off bake in the editor.
// ---------------------------------------------------------------------------
#include "okay/Components/Light.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Render/Atmosphere.hpp"
#include "okay/Render/Lightmap.hpp"      // RayTriangle
#include "okay/Render/LightProbes.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace okay {

struct LightProbeBakeResult { bool ok = false; int probes = 0; int triangles = 0; std::string message; };

inline LightProbeBakeResult BakeLightProbes(Scene& scene, LightProbeGrid& grid, float spacing = 2.0f, int maxTriangles = 40000) {
    LightProbeBakeResult res;
    struct Tri { Vec3 a, b, c, n; Vec3 albedo; };
    std::vector<Tri> tris;
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const auto& go : scene.Objects()) {
        if (!go->active) continue;
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || !mr->enabled) continue;
        const Mesh& m = mr->DisplayMesh();
        const Mat4 model = go->transform->LocalToWorldMatrix();
        for (std::size_t t = 0; t + 2 < m.triangles.size(); t += 3) {
            Tri tr;
            tr.a = model.MultiplyPoint(m.vertices[m.triangles[t]]); tr.b = model.MultiplyPoint(m.vertices[m.triangles[t + 1]]);
            tr.c = model.MultiplyPoint(m.vertices[m.triangles[t + 2]]);
            tr.n = Vec3::Cross(tr.b - tr.a, tr.c - tr.a);
            const float l = tr.n.Magnitude(); if (l < 1e-9f) continue;
            tr.n = tr.n * (1.0f / l);
            tr.albedo = Vec3{mr->color.r, mr->color.g, mr->color.b};
            tris.push_back(tr);
            for (const Vec3* v : {&tr.a, &tr.b, &tr.c}) {
                lo.x = std::fmin(lo.x, v->x); lo.y = std::fmin(lo.y, v->y); lo.z = std::fmin(lo.z, v->z);
                hi.x = std::fmax(hi.x, v->x); hi.y = std::fmax(hi.y, v->y); hi.z = std::fmax(hi.z, v->z);
            }
        }
    }
    if (tris.empty()) { res.message = "Nothing to bake: the scene has no meshes."; return res; }
    if ((int)tris.size() > maxTriangles) { res.message = "The scene has " + std::to_string(tris.size()) + " triangles; probes are baked for up to " + std::to_string(maxTriangles) + ". Hide the heavy meshes and bake again."; return res; }
    // The grid covers the scene's box; a coarser spacing is used if it would need more than 12 x 6 x 12 probes.
    // (A big ground slab is clipped to 60 m around its centre so the grid stays near the action.)
    for (int a = 0; a < 3; ++a) { float* l = &lo.x + a; float* h = &hi.x + a; if (*h - *l > 60.0f) { const float c = 0.5f * (*l + *h); *l = c - 30.0f; *h = c + 30.0f; } }
    spacing = spacing < 0.5f ? 0.5f : spacing;
    int nx, ny, nz;
    for (;;) {
        nx = (int)std::floor((hi.x - lo.x) / spacing) + 1; ny = (int)std::floor((hi.y - lo.y) / spacing) + 1; nz = (int)std::floor((hi.z - lo.z) / spacing) + 1;
        if (nx <= 12 && ny <= 6 && nz <= 12) break;
        spacing *= 1.25f;
    }
    ActiveLightProbes() = nullptr;        // bake without the old probes
    ApplySceneLight(scene);
    const auto& lights = SceneLights::List();
    Vec3 top, hor, bot; EffectiveSkyColors(scene.renderSettings, top, hor, bot);
    const Vec3 amb = SceneLights::AmbientColor();
    const float ambAvg = (amb.x + amb.y + amb.z) / 3.0f;
    const float skyAvg = ((top.x + top.y + top.z) + (hor.x + hor.y + hor.z) + (bot.x + bot.y + bot.z)) / 9.0f;
    const float skyScale = skyAvg > 1e-3f ? ambAvg / skyAvg : 1.0f;
    auto sky = [&](const Vec3& d) {            // the sky colours blended by height, scaled to the ambient level
        const Vec3 c = d.y >= 0.0f ? hor + (top - hor) * std::fmin(1.0f, d.y * 1.5f) : hor + (bot - hor) * std::fmin(1.0f, -d.y * 1.5f);
        return c * skyScale;
    };
    auto occluded = [&](const Vec3& o, const Vec3& d, float maxT) {
        for (const Tri& t : tris) { float tt; if (RayTriangle(o, d, t.a, t.b, t.c, maxT, tt)) return true; }
        return false;
    };
    auto nearest = [&](const Vec3& o, const Vec3& d, float& tBest) {
        const Tri* best = nullptr; tBest = 1e30f;
        for (const Tri& t : tris) { float tt; if (RayTriangle(o, d, t.a, t.b, t.c, tBest, tt)) { tBest = tt; best = &t; } }
        return best;
    };
    constexpr int kDirs = 64;
    Vec3 dirs[kDirs];
    for (int i = 0; i < kDirs; ++i) {           // Fibonacci spiral over the sphere
        const float y = 1.0f - 2.0f * ((float)i + 0.5f) / (float)kDirs, r = std::sqrt(std::fmax(0.0f, 1.0f - y * y)), a = (float)i * 2.39996323f;
        dirs[i] = Vec3{r * std::cos(a), y, r * std::sin(a)};
    }
    grid.Clear();
    grid.origin = lo; grid.spacing = spacing; grid.nx = nx; grid.ny = ny; grid.nz = nz;
    grid.sh.assign((std::size_t)nx * ny * nz * 12, 0.0f);
    const float w = 4.0f * 3.14159265f / (float)kDirs;
    for (int z = 0; z < nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
        const Vec3 pos = lo + Vec3{(float)x, (float)y, (float)z} * spacing;
        float* c = &grid.sh[((std::size_t)(z * ny + y) * nx + x) * 12];
        for (int k = 0; k < kDirs; ++k) {
            float tHit; const Tri* h = nearest(pos, dirs[k], tHit);
            Vec3 rad;
            if (!h) rad = sky(dirs[k]);
            else {
                const Vec3 hp = pos + dirs[k] * tHit; const Vec3 n = Vec3::Dot(h->n, dirs[k]) < 0.0f ? h->n : h->n * -1.0f;
                Vec3 direct{0, 0, 0};
                for (const LightSample& L : lights) {
                    float k2 = LightDiffuseTerm(L, hp, n);
                    if (k2 <= 0.0f) continue;
                    if (L.type == 0 && occluded(hp + n * 0.02f, L.dir.Normalized() * -1.0f, 1e9f)) continue;   // the sun's shadow
                    direct = direct + L.color * k2;
                }
                rad = Vec3{h->albedo.x * (direct.x + amb.x), h->albedo.y * (direct.y + amb.y), h->albedo.z * (direct.z + amb.z)};
            }
            const float rgb[3] = {rad.x, rad.y, rad.z};
            LightProbeGrid::Accumulate(c, dirs[k], rgb, w);
        }
    }
    res.ok = true; res.probes = nx * ny * nz; res.triangles = (int)tris.size();
    res.message = "Baked " + std::to_string(res.probes) + " probes (" + std::to_string(nx) + " x " + std::to_string(ny) + " x " + std::to_string(nz) + ", every " + std::to_string(spacing).substr(0, 4) + " m) from " + std::to_string(res.triangles) + " triangles.";
    ApplySceneLight(scene);               // the new probes are used from now on
    return res;
}

} // namespace okay
