#pragma once
// ---------------------------------------------------------------------------
// Shading with only some of the scene's lights (the software renderer).
//
// A point, spot or area light farther than its range from every point of a mesh adds exactly zero to every vertex and pixel of
// that mesh: its attenuation is exactly 0, so its term is `colour * (ndl * 0)`. The software renderer therefore gives each mesh
// the list of lights whose range reaches its bounding box, and these two functions run the loops of SceneLights::ShadeNormalized
// and SceneLights::SpecularN over that list instead of over all lights. A scene with dozens of lamps otherwise ran every lamp for
// every vertex and every pixel of every mesh (83 percent of the instructions of tests/bench_frame.cpp's lights64 scene).
//
// The bodies are the loops of Lighting.hpp, operation for operation and in the same order, so a result is the very same float
// whenever the omitted lights really are out of reach (tests/test_software_perf_paths.cpp renders scenes both ways and compares
// every pixel). They live in their own header, not in Lighting.hpp, so that touching this does not rebuild everything that includes
// Lighting.hpp. If SceneLights::ShadeNormalized or SpecularN change, change these too: that test fails until they agree.
//
// Written from scratch for this repository. Header only.
// ---------------------------------------------------------------------------
#include "okay/Render/Lighting.hpp"
#include <cstddef>

namespace okay {

/// A subset of SceneLights::List(): ascending indices into it. n may be 0 (no light reaches).
struct LightSubset {
    const int* idx = nullptr;
    int n = 0;
};

/// SceneLights::ShadeNormalized over the lights in `subset` only. Needs SceneLights::List() to be non empty.
inline Vec3 ShadeNormalizedSubset(const Vec3& p, const Vec3& nn, const float* perLight, const LightSubset& subset) {
    const auto& lights = SceneLights::List();
    Vec3 acc = SceneLights::AmbientAtPoint(p, nn);
    for (int t = 0; t < subset.n; ++t) {
        const int li = subset.idx[t];                       // the light's index in List(): the shadow factors are indexed by it
        const auto& L = lights[(std::size_t)li];
        float k = LightDiffuseTerm(L, p, nn);               // shared with SceneLights::ShadeNormalized
        if (perLight && li < SceneLights::kMaxShadowedLights) k *= perLight[li];
        acc.x += L.color.x * k; acc.y += L.color.y * k; acc.z += L.color.z * k;
    }
    acc.x = SoftClipLight(acc.x); acc.y = SoftClipLight(acc.y); acc.z = SoftClipLight(acc.z);
    return acc;
}

/// SceneLights::SpecularN over the lights in `subset` only. Needs SceneLights::List() to be non empty.
inline Vec3 SpecularSubset(const Vec3& p, const Vec3& nn, const Vec3& toEye, float shininess, float dirShadow,
                           const float* perLight, const LightSubset& subset) {
    const auto& lights = SceneLights::List();
    Vec3 spec{0, 0, 0};
    for (int t = 0; t < subset.n; ++t) {
        const int li = subset.idx[t];
        const auto& L = lights[(std::size_t)li];
        Vec3 ld; float atten;
        if (!LightSpecularSetup(L, p, ld, atten)) continue;
        Vec3 h = ld + toEye; float hl = h.Magnitude();
        if (hl < 1e-6f) continue;
        h = h * (1.0f / hl);
        float nh = Vec3::Dot(nn, h);
        if (nh <= 0.0f) continue;
        float s = std::pow(nh, shininess) * atten;
        if (perLight) { if (li < SceneLights::kMaxShadowedLights) s *= perLight[li]; }
        else if (L.type == 0) s *= dirShadow;     // only the sun is shadow-mapped
        spec.x += L.color.x * s; spec.y += L.color.y * s; spec.z += L.color.z * s;
    }
    return spec;
}

} // namespace okay
