#pragma once
#include "okay/Math/Vec3.hpp"

namespace okay {

/// Flat-shading brightness for a triangle: Lambert (N·L) diffuse plus a constant
/// ambient floor, clamped to [ambient, 1]. `lightDir` points *from* the light
/// (so the surface is lit when its normal opposes it). Shared by the player's
/// software 3D renderer and the editor's shaded scene view so they match.
inline float LambertShade(const Vec3& normal, const Vec3& lightDir, float ambient = 0.25f) {
    float lambert = Vec3::Dot(normal.Normalized(), lightDir.Normalized() * -1.0f);
    if (lambert < 0.0f) lambert = 0.0f;
    return ambient + (1.0f - ambient) * lambert;
}

/// The engine's default key light direction (points down-forward-left).
inline Vec3 DefaultLightDir() { return Vec3{-0.4f, -1.0f, -0.6f}.Normalized(); }

} // namespace okay
