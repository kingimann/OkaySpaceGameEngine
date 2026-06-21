#pragma once
#include "okay/Math/Vec3.hpp"
#include <vector>

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

/// One light gathered for a frame: directional, point, or spot. `dir` points
/// *from* the light (the direction it shines); `color` already folds in intensity.
struct LightSample {
    int   type = 0;             // 0 = directional, 1 = point, 2 = spot
    Vec3  dir{0, -1, 0};
    Vec3  pos{0, 0, 0};         // world position (point / spot)
    Vec3  color{1, 1, 1};       // rgb * intensity
    float range = 10.0f;        // point / spot falloff distance
    float cosOuter = 0.7071f;   // cos(half spot angle) — cone edge
    float cosInner = 0.7071f;   // cos(soft inner angle) — full brightness inside
};

/// Multi-light shading: the player/editor fill this list from the scene's Light
/// components each frame, then the software renderer accumulates colored diffuse
/// from every light (directional + point + spot) plus an ambient floor.
struct SceneLights {
    static std::vector<LightSample>& List() { static std::vector<LightSample> v; return v; }
    static float& Ambient() { static float a = 0.25f; return a; }
    /// Tinted ambient (RGB). Defaults to a neutral grey scaled by Ambient().
    static Vec3& AmbientColor() { static Vec3 c{0.25f, 0.25f, 0.25f}; return c; }
    static void Clear() { List().clear(); }
    static void Add(const LightSample& s) { List().push_back(s); }
    static void SetAmbient(float a) {
        a = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
        Ambient() = a; AmbientColor() = Vec3{a, a, a};
    }
    /// Set a colored ambient floor (e.g. a cool sky tint). `Ambient()` tracks its
    /// average so scalar consumers still see a sensible value.
    static void SetAmbientColor(const Vec3& c) {
        AmbientColor() = Vec3{c.x < 0 ? 0 : c.x, c.y < 0 ? 0 : c.y, c.z < 0 ? 0 : c.z};
        Ambient() = (AmbientColor().x + AmbientColor().y + AmbientColor().z) / 3.0f;
    }

    // ---- Hemisphere ambient ----
    // Instead of one flat ambient color, the indirect/ambient term can blend
    // between a "sky" color (for up-facing surfaces) and a "ground" color (for
    // down-facing ones). The renderer fills these from the scene's sky gradient;
    // the midpoint (horizontal surfaces) is kept equal to AmbientColor() so the
    // overall ambient level is unchanged — it just gains direction.
    static bool& Hemisphere()    { static bool b = false; return b; }
    static Vec3& SkyAmbient()    { static Vec3 c{0.25f, 0.25f, 0.25f}; return c; }
    static Vec3& GroundAmbient() { static Vec3 c{0.25f, 0.25f, 0.25f}; return c; }
    /// Ambient color for a surface with world normal `n` (hemisphere if enabled).
    static Vec3 AmbientAt(const Vec3& n) {
        if (!Hemisphere()) return AmbientColor();
        float t = n.y * 0.5f + 0.5f;            // 1 = up (sky), 0 = down (ground)
        const Vec3& g = GroundAmbient();
        const Vec3& s = SkyAmbient();
        return Vec3{g.x + (s.x - g.x) * t, g.y + (s.y - g.y) * t, g.z + (s.z - g.z) * t};
    }

    /// Accumulated light color (RGB multipliers, clamped to 1) at a world point
    /// with the given surface normal. When no lights were gathered, falls back to
    /// the single global SceneLight so scripts' set_light still works.
    static Vec3 ShadeColor(const Vec3& p, const Vec3& n) {
        Vec3 nn = n.Normalized();
        const auto& lights = List();
        if (lights.empty()) {
            float s = LambertShade(nn, SceneLight::Direction(), SceneLight::Ambient());
            return Vec3{s, s, s};
        }
        Vec3 acc = AmbientAt(nn);
        for (const auto& L : lights) {
            float ndl, atten = 1.0f;
            if (L.type == 0) {
                ndl = Vec3::Dot(nn, L.dir.Normalized() * -1.0f);
            } else {
                Vec3 toL = L.pos - p; float d = toL.Magnitude();
                Vec3 ld = d > 1e-5f ? toL * (1.0f / d) : Vec3{0, 1, 0};
                ndl = Vec3::Dot(nn, ld);
                atten = (L.range > 0.0f) ? (1.0f - d / L.range) : 0.0f;
                if (atten < 0.0f) atten = 0.0f;
                atten *= atten;                                  // smooth falloff
                if (L.type == 2) {                               // spot cone
                    float cs = Vec3::Dot(L.dir.Normalized(), ld * -1.0f);
                    // Smooth from the soft inner angle to the hard outer edge.
                    float denom = L.cosInner - L.cosOuter;
                    float spot = denom > 1e-4f ? (cs - L.cosOuter) / denom
                                               : (cs >= L.cosOuter ? 1.0f : 0.0f);
                    if (spot < 0.0f) spot = 0.0f; else if (spot > 1.0f) spot = 1.0f;
                    atten *= spot * spot;                        // softer rolloff
                }
            }
            if (ndl < 0.0f) ndl = 0.0f;
            float k = ndl * atten;
            acc.x += L.color.x * k; acc.y += L.color.y * k; acc.z += L.color.z * k;
        }
        if (acc.x > 1.0f) acc.x = 1.0f;
        if (acc.y > 1.0f) acc.y = 1.0f;
        if (acc.z > 1.0f) acc.z = 1.0f;
        return acc;
    }
};

} // namespace okay
