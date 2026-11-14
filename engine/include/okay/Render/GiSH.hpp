#pragma once
// Order-2 (9 coefficient) real spherical harmonics for baked diffuse lighting.
// Projection of radiance samples, reconstruction, and the cosine-convolved
// "ambient" lookup (Ramamoorthi and Hanrahan 2001) that the shaders will mirror.
#include "okay/Math/Vec3.hpp"

namespace okay {

constexpr int kShCoeffs = 9;
constexpr float kGiPi = 3.14159265358979323846f;

/// Nine SH coefficients per colour channel (x=r, y=g, z=b).
struct ShRGB {
    Vec3 c[kShCoeffs];
};

/// The 9 real SH basis functions for a unit direction.
inline void ShBasis(const Vec3& d, float out[kShCoeffs]) {
    const float x = d.x, y = d.y, z = d.z;
    out[0] = 0.282095f;
    out[1] = 0.488603f * y;
    out[2] = 0.488603f * z;
    out[3] = 0.488603f * x;
    out[4] = 1.092548f * x * y;
    out[5] = 1.092548f * y * z;
    out[6] = 0.315392f * (3.0f * z * z - 1.0f);
    out[7] = 1.092548f * x * z;
    out[8] = 0.546274f * (x * x - y * y);
}

/// Add one radiance sample (arriving from unit direction `d`) with Monte Carlo
/// weight `w` (= 4*pi / sampleCount for uniform sphere sampling).
inline void ShAddSample(ShRGB& sh, const Vec3& d, const Vec3& radiance, float w) {
    float b[kShCoeffs]; ShBasis(d, b);
    for (int i = 0; i < kShCoeffs; ++i) sh.c[i] = sh.c[i] + radiance * (b[i] * w);
}

/// Radiance reconstructed in direction `d` (not clamped; ringing can go negative).
inline Vec3 ShEvalRadiance(const ShRGB& sh, const Vec3& d) {
    float b[kShCoeffs]; ShBasis(d, b);
    Vec3 r{0, 0, 0};
    for (int i = 0; i < kShCoeffs; ++i) r = r + sh.c[i] * b[i];
    return r;
}

/// Diffuse "ambient" for surface normal `n`: irradiance / pi, i.e. the colour a
/// white surface takes from this light, in the engine's units (a uniform field of
/// radiance L gives L). Clamped at zero to hide ringing.
inline Vec3 ShAmbient(const ShRGB& sh, const Vec3& n) {
    float b[kShCoeffs]; ShBasis(n, b);
    // Cosine lobe weights A_l / pi: 1, 2/3, 1/4.
    static const float kA[kShCoeffs] = {1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f,
                                        0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
    Vec3 r{0, 0, 0};
    for (int i = 0; i < kShCoeffs; ++i) r = r + sh.c[i] * (b[i] * kA[i]);
    if (r.x < 0.0f) r.x = 0.0f;
    if (r.y < 0.0f) r.y = 0.0f;
    if (r.z < 0.0f) r.z = 0.0f;
    return r;
}

inline ShRGB ShLerp(const ShRGB& a, const ShRGB& b, float t) {
    ShRGB o;
    for (int i = 0; i < kShCoeffs; ++i) o.c[i] = a.c[i] + (b.c[i] - a.c[i]) * t;
    return o;
}

} // namespace okay
