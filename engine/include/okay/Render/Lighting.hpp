#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Graphics/Image.hpp"
#include <string>
#include <vector>
#include <cmath>
#include <cstdlib>

namespace okay {

/// "Classic lighting": the look before the 2026-11 lighting upgrade (hard clamp of the summed light, a straight spot ramp, a
/// fixed-radius shadow filter with a constant bias, cascades snapped on the world axes). Off by default. Set the environment
/// variable OKAY_CLASSIC_LIGHTING=1 to get it back, to compare the two looks or to rule the upgrade out when something looks
/// wrong. The software renderer honours it; the GPU renderers are not changed by the upgrade yet.
inline bool& ClassicLighting() {
    static bool v = [] { const char* e = std::getenv("OKAY_CLASSIC_LIGHTING"); return e && std::atoi(e) > 0; }();
    return v;
}

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
    int   type = 0;             // 0 = directional, 1 = point, 2 = spot, 3 = area
    int   falloff = 0;          // 0 = linear (1-d/range)^2, 1 = inverse-square (physical)
    Vec3  dir{0, -1, 0};
    Vec3  pos{0, 0, 0};         // world position (point / spot / area)
    Vec3  color{1, 1, 1};       // rgb * intensity
    float range = 10.0f;        // point / spot / area falloff distance
    float cosOuter = 0.7071f;   // cos(half spot angle) — cone edge
    float cosInner = 0.7071f;   // cos(soft inner angle) — full brightness inside
    bool  castShadows = true;   // occluders block this light (sun: cascades; spot/point: shadow tiles)
    float shadowStrength = 1.0f;// 0 = shadows invisible .. 1 = fully dark (down to ambient)
    float shadowBias = 1.0f;    // bias multiplier: raise if surfaces self-shadow, lower if shadows float
    float shadowSoftness = 0.5f;// 0 = same soft edge everywhere .. 1 = shadows get soft quickly with distance from the caster
    // Light shapes and masks (software renderer; the GPU renderers draw an Area light as a soft point light and ignore cookies).
    Vec3  right{1, 0, 0};       // the light's own sideways axis (cookie and panel orientation)
    Vec3  up{0, 1, 0};          // the light's own up axis
    float halfW = 0.0f;         // Area light: half the panel width in metres (0 = classic soft glow, no panel)
    float halfH = 0.0f;         // Area light: half the panel height in metres
    int   cookie = -1;          // index into LightCookies (-1 = none)
    float cookieScale = 4.0f;   // Directional: metres per repeat of the mask
};

/// Distance attenuation for a positional light, shared by every shading path so
/// the software renderer, the GL shader (mirrored) and the lightmap baker agree.
/// `falloff` 0 = linear (1-d/range)^2; 1 = physical 1/d^2 windowed to reach 0 at
/// `range`. Returns [0,1]-ish (inverse-square peaks >1 very close to the source).
inline float LightAttenuation(float d, float range, int falloff) {
    if (range <= 0.0f) return 0.0f;
    if (falloff == 1) {                          // inverse-square, windowed at range
        float dr = d / range; if (dr >= 1.0f) return 0.0f;
        float win = 1.0f - dr * dr * dr * dr; win *= win;      // smooth cutoff
        return win / (1.0f + 8.0f * d * d / (range * range));  // ~1 near source, scaled to range
    }
    float a = 1.0f - d / range; if (a < 0.0f) a = 0.0f;
    return a * a;                                // smooth linear rolloff
}
/// Wrap-diffuse for Area lights: softens N·L so a broad panel fills shadows.
inline float AreaWrap(float ndl) { return (ndl + 0.5f) / 1.5f; }


/// Smooth spot cone: 1 inside the soft inner angle, 0 outside the outer angle, smoothstep between (no visible ring where
/// the soft edge starts, unlike a straight ramp). `cs` is the cosine of the angle between the spot axis and the surface point.
inline float SpotCone(float cs, float cosOuter, float cosInner) {
    const float denom = cosInner - cosOuter;
    if (denom <= 1e-4f) return cs >= cosOuter ? 1.0f : 0.0f;
    float t = (cs - cosOuter) / denom;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    if (ClassicLighting()) return t * t;      // the old straight ramp, squared
    return t * t * (3.0f - 2.0f * t);
}

/// Brightness roll-off for the summed light: exactly as before up to 1.0 (so ordinary lighting does not change), and above 1.0 a
/// smooth shoulder that keeps climbing towards 1.3 instead of stopping dead. The old hard clamp made every light brighter than 1
/// look the same flat white (a 3x lamp and a 1.1x lamp were identical, and a colour channel that hit 1 first shifted the hue).
/// Over-bright light now washes a surface out a little beyond its own colour, in the order of the light's strength.
inline float SoftClipLight(float x) {
    if (ClassicLighting()) return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    if (x <= 1.0f) return x < 0.0f ? 0.0f : x;
    return 1.0f + 0.3f * (1.0f - std::exp(-(x - 1.0f) / 0.3f));
}

/// Real-world light units, shown to the user as lux (sun, moon, sky: light arriving on a surface) and lumens (lamps:
/// light given off). Everything is turned into the engine's simple intensity with one reference: the illuminance that
/// shows as 1.0. Auto brightness picks that reference from the scene's own lights; otherwise `kDefaultReferenceLux`.
constexpr float kDefaultReferenceLux = 20000.0f;    // a bright overcast day looks "fully lit"
inline float LuxFromLumens(float lumens, float coneCosHalf = -1.0f) {
    // Lumens spread over the sphere (4*pi sr) or, for a spot, over its cone. Gives candela, which at 1 m is lux.
    const float pi = 3.14159265358979f;
    const float sr = coneCosHalf > -0.99f ? 2.0f * pi * (1.0f - coneCosHalf) : 4.0f * pi;
    return lumens / (sr > 1e-4f ? sr : 1e-4f);
}
inline float SimpleIntensityFromLux(float lux, float referenceLux) {
    return referenceLux > 1e-6f ? lux / referenceLux : 0.0f;
}
/// Auto brightness: the reference illuminance for a scene is that of its brightest light as seen from 2 m (a sun counts
/// as its lux). A sunny scene is then "1.0", an evening lit by one lamp is also "about 1.0": the eye-like adaptation.
inline float AutoReferenceLux(float brightestLuxAtTwoMetres) {
    return brightestLuxAtTwoMetres > 1.0f ? brightestLuxAtTwoMetres : 1.0f;
}

/// A mask a light shines through (a window, blinds, leaves). 0 = blocked, 1 = open. Small grayscale grids, built in code
/// ("builtin:" names, so nothing to ship or license) or read from an image file (brightness is the mask).
struct LightCookie {
    int w = 0, h = 0;
    std::vector<float> a;
    float At(int x, int y) const { return a[(std::size_t)y * w + x]; }
    /// Bilinear sample at (u, v) in 0..1, wrapping (repeat) when `wrap`, else 0 outside the picture.
    float Sample(float u, float v, bool wrap) const {
        if (w <= 0 || h <= 0) return 1.0f;
        if (wrap) { u -= std::floor(u); v -= std::floor(v); }
        else if (u < 0.0f || v < 0.0f || u > 1.0f || v > 1.0f) return 0.0f;
        const float fx = u * (float)w - 0.5f, fy = v * (float)h - 0.5f;
        int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
        const float tx = fx - (float)x0, ty = fy - (float)y0;
        auto px = [&](int x) { return wrap ? ((x % w) + w) % w : (x < 0 ? 0 : (x >= w ? w - 1 : x)); };
        auto py = [&](int y) { return wrap ? ((y % h) + h) % h : (y < 0 ? 0 : (y >= h ? h - 1 : y)); };
        const float c00 = At(px(x0), py(y0)), c10 = At(px(x0 + 1), py(y0));
        const float c01 = At(px(x0), py(y0 + 1)), c11 = At(px(x0 + 1), py(y0 + 1));
        return (c00 + (c10 - c00) * tx) * (1.0f - ty) + (c01 + (c11 - c01) * tx) * ty;
    }
};
struct LightCookies {
    static const char* const* BuiltinNames(int& count) {
        static const char* const k[] = {"builtin:window", "builtin:blinds", "builtin:dots", "builtin:leaves", "builtin:star", "builtin:soft-circle"};
        count = 6;
        return k;
    }
    static std::vector<LightCookie>& Store() { static std::vector<LightCookie> v; return v; }
    static std::vector<std::string>& Names() { static std::vector<std::string> v; return v; }
    /// Index of the mask called `name` (loading it the first time), or -1 when it is empty or cannot be read.
    /// Call this while gathering lights (one thread); rendering threads only read.
    static int Find(const std::string& name) {
        if (name.empty()) return -1;
        auto& names = Names();
        for (std::size_t i = 0; i < names.size(); ++i) if (names[i] == name) return Store()[i].w > 0 ? (int)i : -1;
        LightCookie c;
        if (name.compare(0, 8, "builtin:") == 0) c = Builtin(name.substr(8));
        else {
            Image im;
            if (im.Load(name) && im.Valid()) {
                c.w = im.Width(); c.h = im.Height(); c.a.resize((std::size_t)c.w * c.h);
                for (int y = 0; y < c.h; ++y) for (int x = 0; x < c.w; ++x) {
                    const Color p = im.GetPixel(x, y);
                    c.a[(std::size_t)y * c.w + x] = (0.299f * p.r + 0.587f * p.g + 0.114f * p.b) * p.a;
                }
            }
        }
        names.push_back(name); Store().push_back(c);
        return c.w > 0 ? (int)names.size() - 1 : -1;
    }
    static const LightCookie* Get(int i) { return (i >= 0 && i < (int)Store().size()) ? &Store()[(std::size_t)i] : nullptr; }
    static float Smooth(float e0, float e1, float x) {
        float t = (x - e0) / (e1 - e0); t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        return t * t * (3.0f - 2.0f * t);
    }
    static LightCookie Builtin(const std::string& what) {
        LightCookie c; c.w = c.h = 64; c.a.assign((std::size_t)64 * 64, 1.0f);
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
            const float u = (x + 0.5f) / 64.0f, v = (y + 0.5f) / 64.0f;
            float m = 1.0f;
            if (what == "window") {                       // four panes in a frame with a cross bar
                const float fx = std::fabs(u - 0.5f), fy = std::fabs(v - 0.5f);
                const bool frame = fx > 0.44f || fy > 0.44f, bar = fx < 0.035f || fy < 0.035f;
                m = (frame || bar) ? 0.0f : 1.0f;
            } else if (what == "blinds") {                // horizontal slats
                m = Smooth(0.30f, 0.55f, std::fabs(std::fmod(v * 8.0f, 1.0f) - 0.5f) * 2.0f);
                m = 1.0f - m;
            } else if (what == "dots") {                  // a grid of round holes
                const float dx = std::fmod(u * 6.0f, 1.0f) - 0.5f, dy = std::fmod(v * 6.0f, 1.0f) - 0.5f;
                m = 1.0f - Smooth(0.28f, 0.36f, std::sqrt(dx * dx + dy * dy));
            } else if (what == "leaves") {                // dappled light under a tree: a sum of a few waves, thresholded
                const float t = 6.2831853f;
                const float f = std::sin(u * t * 3.0f + std::sin(v * t * 2.0f) * 1.7f) + std::sin(v * t * 4.0f + std::sin(u * t * 3.0f) * 1.3f)
                              + std::sin((u + v) * t * 2.0f + 1.0f);
                m = Smooth(-0.7f, 0.5f, f);
            } else if (what == "star") {                  // a five-point star
                const float dx = u - 0.5f, dy = v - 0.5f;
                const float r = std::sqrt(dx * dx + dy * dy), ang = std::atan2(dy, dx);
                const float edge = 0.30f + 0.17f * std::cos(5.0f * ang);
                m = 1.0f - Smooth(edge - 0.02f, edge + 0.02f, r);
            } else {                                      // soft-circle (and any other name)
                const float dx = u - 0.5f, dy = v - 0.5f;
                m = 1.0f - Smooth(0.25f, 0.5f, std::sqrt(dx * dx + dy * dy));
            }
            c.a[(std::size_t)y * 64 + x] = m;
        }
        return c;
    }
};

/// How much of the light's mask is open at world point `p` (1 when the light has no mask). Spot: the mask fits the cone.
/// Directional: the mask is laid across the world and repeats every `cookieScale` metres, seen along the light.
inline float CookieAt(const LightSample& L, const Vec3& p) {
    if (L.cookie < 0 || L.type == 1 || L.type == 3) return 1.0f;
    const LightCookie* c = LightCookies::Get(L.cookie);
    if (!c) return 1.0f;
    if (L.type == 0) {
        const float sc = L.cookieScale > 0.05f ? L.cookieScale : 0.05f;
        return c->Sample(Vec3::Dot(p, L.right) / sc + 0.5f, 0.5f - Vec3::Dot(p, L.up) / sc, true);
    }
    const Vec3 d = p - L.pos;
    const float z = Vec3::Dot(d, L.dir.Normalized());
    if (z <= 1e-4f) return 0.0f;
    const float tanHalf = std::sqrt(std::fmax(0.0f, 1.0f - L.cosOuter * L.cosOuter)) / std::fmax(L.cosOuter, 1e-3f);
    const float k = 0.5f / (z * (tanHalf > 1e-3f ? tanHalf : 1e-3f));
    return c->Sample(0.5f + Vec3::Dot(d, L.right) * k, 0.5f - Vec3::Dot(d, L.up) * k, false);
}

/// Area light as a real rectangle: the panel is sampled on a 3x3 grid; each sample lights the point like a small
/// one-sided lamp (cosine on the receiving side, cosine on the emitting side, distance falloff). Broad panels give wide soft
/// light that wraps around objects and fades smoothly with distance and angle. Returns the average, 0..~1.
inline float RectLightTerm(const LightSample& L, const Vec3& p, const Vec3& nn) {
    const Vec3 axis = L.dir.Normalized();
    float sum = 0.0f;
    for (int j = 0; j < 3; ++j) for (int i = 0; i < 3; ++i) {
        const float fu = (i - 1) * (2.0f / 3.0f), fv = (j - 1) * (2.0f / 3.0f);   // sample centres of a 3x3 grid
        const Vec3 q = L.pos + L.right * (fu * L.halfW) + L.up * (fv * L.halfH);
        const Vec3 v = q - p; const float d = v.Magnitude();
        if (d < 1e-4f) { sum += 1.0f; continue; }
        const Vec3 l = v * (1.0f / d);
        const float recv = Vec3::Dot(nn, l), emit = Vec3::Dot(axis, l * -1.0f);
        if (recv <= 0.0f || emit <= 0.0f) continue;
        sum += recv * emit * LightAttenuation(d, L.range, L.falloff);
    }
    return sum * (1.0f / 9.0f);
}


/// One light's diffuse factor at point `p` with unit normal `nn`: N.L (or the panel's average), distance falloff, spot cone and
/// mask, NOT shadows. Shared by SceneLights::ShadeNormalized and the light-subset path (LightSubset.hpp) so they cannot differ.
inline float LightDiffuseTerm(const LightSample& L, const Vec3& p, const Vec3& nn) {
    float ndl, atten = 1.0f;
    if (L.type == 0) {
        ndl = Vec3::Dot(nn, L.dir.Normalized() * -1.0f);
    } else if (L.type == 3 && L.halfW > 0.0f && L.halfH > 0.0f) {
        ndl = RectLightTerm(L, p, nn);                    // a real panel (all of its own falloff is inside)
    } else {
        Vec3 toL = L.pos - p; float d = toL.Magnitude();
        Vec3 ld = d > 1e-5f ? toL * (1.0f / d) : Vec3{0, 1, 0};
        ndl = Vec3::Dot(nn, ld);
        if (L.type == 3) ndl = AreaWrap(ndl);            // area: soft wrap fill
        atten = LightAttenuation(d, L.range, L.falloff);
        if (L.type == 2) {                               // spot cone
            float cs = Vec3::Dot(L.dir.Normalized(), ld * -1.0f);
            atten *= SpotCone(cs, L.cosOuter, L.cosInner);
        }
    }
    if (ndl < 0.0f) ndl = 0.0f;
    float k = ndl * atten;
    if (L.cookie >= 0) k *= CookieAt(L, p);
    return k;
}
/// Direction to the light (`ld`) and its strength at `p` for the Blinn-Phong highlight; false when no light arrives.
inline bool LightSpecularSetup(const LightSample& L, const Vec3& p, Vec3& ld, float& atten) {
    atten = 1.0f;
    if (L.type == 0) {
        ld = L.dir.Normalized() * -1.0f;
    } else {
        Vec3 toL = L.pos - p; float d = toL.Magnitude();
        ld = d > 1e-5f ? toL * (1.0f / d) : Vec3{0, 1, 0};
        atten = LightAttenuation(d, L.range, L.falloff);
        if (L.type == 2) {
            float cs = Vec3::Dot(L.dir.Normalized(), ld * -1.0f);
            atten *= SpotCone(cs, L.cosOuter, L.cosInner);
        }
    }
    if (L.cookie >= 0) atten *= CookieAt(L, p);
    return atten > 0.0f;
}

/// Multi-light shading: the player/editor fill this list from the scene's Light
/// components each frame, then the software renderer accumulates colored diffuse
/// from every light (directional + point + spot) plus an ambient floor.
struct SceneLights {
    static std::vector<LightSample>& List() { static std::vector<LightSample> v; return v; }
    static float& Ambient() { static float a = 0.25f; return a; }
    /// Tinted ambient (RGB). Defaults to a neutral grey scaled by Ambient().
    static Vec3& AmbientColor() { static Vec3 c{0.25f, 0.25f, 0.25f}; return c; }
    /// Index in List() of the main directional light (the one whose shadows the
    /// renderers draw), or -1 when there is none. Set by ApplySceneLight.
    static int& SunIndex() { static int i = -1; return i; }
    static void Clear() { List().clear(); SunIndex() = -1; }
    /// The main directional light's sample, or nullptr.
    static const LightSample* Sun() {
        int i = SunIndex();
        return (i >= 0 && i < (int)List().size()) ? &List()[i] : nullptr;
    }
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
    static Vec3 ShadeColor(const Vec3& p, const Vec3& n) { return ShadeNormalized(p, n.Normalized()); }
    /// Same as ShadeColor but assumes `nn` is already unit length (skips the
    /// per-pixel sqrt) — the per-pixel renderer passes a normalized normal.
    ///
    /// `perLight` (optional): one 0..1 shadow factor per light, indexed like
    /// List() (only the first kMaxShadowedLights are read). A light's whole
    /// contribution is multiplied by its factor. The software renderer passes it
    /// when spot / point shadows are on, so each light is shadowed by its own map
    /// and not by the sun's. With nullptr the result is exactly as before.
    static constexpr int kMaxShadowedLights = 16;
    static Vec3 ShadeNormalized(const Vec3& p, const Vec3& nn, const float* perLight = nullptr) {
        const auto& lights = List();
        if (lights.empty()) {
            float s = LambertShade(nn, SceneLight::Direction(), SceneLight::Ambient());
            return Vec3{s, s, s};
        }
        Vec3 acc = AmbientAt(nn);
        int li = -1;
        for (const auto& L : lights) {
            ++li;
            float k = LightDiffuseTerm(L, p, nn);               // diffuse x distance x cone x panel x mask (no shadow)
            if (perLight && li < kMaxShadowedLights) k *= perLight[li];
            acc.x += L.color.x * k; acc.y += L.color.y * k; acc.z += L.color.z * k;
        }
        acc.x = SoftClipLight(acc.x); acc.y = SoftClipLight(acc.y); acc.z = SoftClipLight(acc.z);
        return acc;
    }

    /// Colored Blinn-Phong specular accumulated over every light (directional +
    /// point + spot), with each light's color and distance/cone attenuation. The
    /// directional contribution is scaled by `dirShadow` (the shadow factor) so
    /// highlights vanish in shadow. Returned value is NOT multiplied by the
    /// material's specular strength — the caller applies that (and any gloss map).
    static Vec3 Specular(const Vec3& p, const Vec3& n, const Vec3& toEye,
                         float shininess, float dirShadow = 1.0f) {
        return SpecularN(p, n.Normalized(), toEye, shininess, dirShadow);
    }
    /// Specular variant that assumes `nn` is already unit length (skips the sqrt).
    /// `perLight` (optional) works as in ShadeNormalized and replaces `dirShadow`
    /// for every light, the sun included.
    static Vec3 SpecularN(const Vec3& p, const Vec3& nn, const Vec3& toEye,
                          float shininess, float dirShadow = 1.0f,
                          const float* perLight = nullptr) {
        const auto& lights = List();
        if (lights.empty()) {
            Vec3 ld = SceneLight::Direction().Normalized() * -1.0f;
            Vec3 h = ld + toEye; float hl = h.Magnitude();
            if (hl < 1e-6f) return Vec3{0, 0, 0};
            h = h * (1.0f / hl);
            float nh = Vec3::Dot(nn, h);
            if (nh <= 0.0f) return Vec3{0, 0, 0};
            float s = std::pow(nh, shininess) * dirShadow;
            return Vec3{s, s, s};
        }
        Vec3 spec{0, 0, 0};
        int li = -1;
        for (const auto& L : lights) {
            ++li;
            Vec3 ld; float atten;
            if (!LightSpecularSetup(L, p, ld, atten)) continue;
            Vec3 h = ld + toEye; float hl = h.Magnitude();
            if (hl < 1e-6f) continue;
            h = h * (1.0f / hl);
            float nh = Vec3::Dot(nn, h);
            if (nh <= 0.0f) continue;
            float s = std::pow(nh, shininess) * atten;
            if (perLight) { if (li < kMaxShadowedLights) s *= perLight[li]; }
            else if (L.type == 0) s *= dirShadow;     // only the sun is shadow-mapped
            spec.x += L.color.x * s; spec.y += L.color.y * s; spec.z += L.color.z * s;
        }
        return spec;
    }
};

} // namespace okay
