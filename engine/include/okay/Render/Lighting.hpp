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

/// The active directional light for 3D shading — a single global the player and
/// editor both read, and scripts can change (set_light / set_ambient) for
/// day-night cycles, mood lighting, or flash effects. Direction points *from*
/// the light; `ambient` is the unlit floor brightness in [0, 1].
struct SceneLight {
    static Vec3& Direction() { static Vec3 d = DefaultLightDir(); return d; }
    static float& Ambient()  { static float a = 0.25f; return a; }
    static void SetDirection(const Vec3& d) {
        Direction() = (d.SqrMagnitude() > 1e-12f) ? d.Normalized() : DefaultLightDir();
    }
    static void SetAmbient(float a) { Ambient() = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a); }
    static void Reset() { Direction() = DefaultLightDir(); Ambient() = 0.25f; }
    /// Shade a normal with the current global light + ambient.
    static float Shade(const Vec3& normal) { return LambertShade(normal, Direction(), Ambient()); }
};

} // namespace okay
