#pragma once
// ---------------------------------------------------------------------------
// Sky, fog, post-processing and weather helpers shared by every renderer, the
// editor, the player, the scripting bindings and the tests. The GPU shaders
// (GLSL in GLRenderer.cpp, HLSL in D3D11Renderer.cpp) mirror the maths here, so
// a value computed on the CPU (the sun's sky colours, a fog factor, a tonemap
// curve) matches what the frame shows.
// ---------------------------------------------------------------------------
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Environment/Wind.hpp"
#include "okay/Scene/Scene.hpp"
#include <cmath>
#include <cstring>
#include <string>

namespace okay {

// ---- Global environment state ------------------------------------------------
// WindVector() and LightningFlash() live in Environment/Wind.hpp.
/// True when the GPU post chain already darkened the frame's edges this render,
/// so the host must not paint its own vignette overlay on top.
inline bool& PostVignetteHandled() { static bool v = false; return v; }
/// Whether the GPU renderers draw the Procedural sky this render. The host turns
/// it off where it paints no sky either (the Scene view's "Sky gradient" debug
/// toggle, a camera clearing to a solid colour).
inline bool& SkyPassEnabled() { static bool v = true; return v; }

/// Direction TOWARD the sun (unit), from the scene's directional light.
inline Vec3 SunDirection() { return (SceneLight::Direction() * -1.0f).Normalized(); }

// ---- Procedural sky ----------------------------------------------------------
/// The sky's colours for a sun height (`sunHeight` = the sun direction's Y,
/// -1 straight down .. 1 overhead): blue by day, orange at dusk, dark at night.
struct SkyPalette {
    Vec3  zenith{0, 0, 0}, horizon{0, 0, 0}, ground{0, 0, 0};
    Vec3  sun{1, 1, 1};        // disc / halo colour
    float night = 0.0f;        // 0 day .. 1 night: star visibility
    float sunDisc = 1.0f;      // 0..1 visibility of the disc (it sets below the horizon)
};
inline SkyPalette SkyPaletteFor(float sunHeight) {
    const float h = Mathf::Clamp(sunHeight, -1.0f, 1.0f);
    struct Set { Vec3 zenith, horizon, ground, sun; };
    static const Set kDay   {{0.22f, 0.45f, 0.86f}, {0.62f, 0.77f, 0.93f}, {0.44f, 0.45f, 0.49f}, {1.00f, 0.97f, 0.90f}};
    static const Set kDusk  {{0.16f, 0.17f, 0.42f}, {0.98f, 0.52f, 0.22f}, {0.27f, 0.18f, 0.19f}, {1.00f, 0.56f, 0.26f}};
    static const Set kNight {{0.010f, 0.014f, 0.050f}, {0.040f, 0.050f, 0.105f}, {0.018f, 0.018f, 0.030f}, {0.55f, 0.62f, 0.80f}};
    auto mix = [](const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; };
    SkyPalette p;
    if (h >= 0.0f) {
        const float t = Mathf::SmoothStep(0.0f, 0.3f, h);          // dusk -> day as the sun climbs
        p.zenith = mix(kDusk.zenith, kDay.zenith, t); p.horizon = mix(kDusk.horizon, kDay.horizon, t);
        p.ground = mix(kDusk.ground, kDay.ground, t); p.sun = mix(kDusk.sun, kDay.sun, t);
        p.night = 0.0f;
    } else {
        const float t = Mathf::SmoothStep(0.0f, 0.22f, -h);        // dusk -> night as it sets
        p.zenith = mix(kDusk.zenith, kNight.zenith, t); p.horizon = mix(kDusk.horizon, kNight.horizon, t);
        p.ground = mix(kDusk.ground, kNight.ground, t); p.sun = mix(kDusk.sun, kNight.sun, t);
        p.night = t;
    }
    p.sunDisc = 1.0f - Mathf::SmoothStep(0.0f, 0.08f, -h);          // slips under the horizon
    return p;
}

/// The three gradient colours a renderer should use for the sky, reflections
/// and sky-tinted ambient: the authored gradient in Colour mode, the sun-driven
/// palette in Procedural mode.
inline void EffectiveSkyColors(const Scene::RenderSettings& rs, Vec3& top, Vec3& horizon, Vec3& bottom) {
    if (rs.skyMode == Scene::RenderSettings::SkyProcedural) {
        const SkyPalette p = SkyPaletteFor(SunDirection().y);
        top = p.zenith; horizon = p.horizon; bottom = p.ground;
        return;
    }
    top = {rs.skyTop.r, rs.skyTop.g, rs.skyTop.b};
    horizon = {rs.skyHorizon.r, rs.skyHorizon.g, rs.skyHorizon.b};
    bottom = {rs.skyBottom.r, rs.skyBottom.g, rs.skyBottom.b};
}

// ---- Fog ---------------------------------------------------------------------
/// The fog colour: the authored one, or the sky's horizon when fogMatchSky is on.
inline Vec3 EffectiveFogColor(const Scene::RenderSettings& rs) {
    if (rs.fogMatchSky) { Vec3 t, h, b; EffectiveSkyColors(rs, t, h, b); return h; }
    return {rs.fogColor.r, rs.fogColor.g, rs.fogColor.b};
}
/// True when the fog settings produce any fog at all.
inline bool FogActive(const Scene::RenderSettings& rs) {
    if (!rs.fog) return false;
    if (rs.fogMode == Scene::RenderSettings::FogLinear) return rs.fogEnd > rs.fogStart;
    return rs.fogDensity > 0.0f;
}
/// How much fog (0 = none .. 1 = solid) covers a point `dist` from the eye, at
/// world height `worldY`, seen from an eye at `eyeY`. Mirrored in the shaders.
inline float FogFactor(const Scene::RenderSettings& rs, float dist, float eyeY, float worldY) {
    if (!FogActive(rs)) return 0.0f;
    if (dist < 0.0f) dist = 0.0f;
    switch (rs.fogMode) {
    case Scene::RenderSettings::FogExponential:
        return 1.0f - std::exp(-rs.fogDensity * dist);
    case Scene::RenderSettings::FogHeight: {
        // The layer thins above fogHeight: average the thickness at both ends of
        // the ray, so looking down into a valley fills with mist and looking up
        // out of it clears.
        auto thick = [&](float y) { float a = y - rs.fogHeight; return std::exp(-(a > 0.0f ? a : 0.0f) * rs.fogFalloff); };
        const float t = 0.5f * (thick(eyeY) + thick(worldY));
        return 1.0f - std::exp(-rs.fogDensity * dist * t);
    }
    default:
        return Mathf::Clamp01((dist - rs.fogStart) / (rs.fogEnd - rs.fogStart));
    }
}

// ---- Post-processing ---------------------------------------------------------
/// Filmic ACES curve (Narkowicz): linear light in, display value 0..1 out.
inline float TonemapACES(float x) {
    const float n = x * (2.51f * x + 0.03f), d = x * (2.43f * x + 0.59f) + 0.14f;
    return Mathf::Clamp01(d > 1e-6f ? n / d : 0.0f);
}
/// Filmic curve (Hable): a softer shoulder with more room for highlights.
inline float TonemapFilmic(float x) {
    auto hable = [](float v) {
        const float A = 0.15f, B = 0.50f, C = 0.10f, D = 0.20f, E = 0.02f, F = 0.30f;
        return ((v * (A * v + C * B) + D * E) / (v * (A * v + B) + D * F)) - E / F;
    };
    return Mathf::Clamp01(hable(x * 2.0f) / hable(11.2f));
}
/// Apply the scene's tonemap choice to a linear colour.
inline Vec3 TonemapColor(const Vec3& c, int mode) {
    switch (mode) {
    case Scene::RenderSettings::PostSettings::TonemapACES:   return {TonemapACES(c.x), TonemapACES(c.y), TonemapACES(c.z)};
    case Scene::RenderSettings::PostSettings::TonemapFilmic: return {TonemapFilmic(c.x), TonemapFilmic(c.y), TonemapFilmic(c.z)};
    default: return {Mathf::Clamp01(c.x), Mathf::Clamp01(c.y), Mathf::Clamp01(c.z)};
    }
}
/// The exposure the frame is rendered with: the setting, boosted by a lightning flash.
inline float ExposureScale(const Scene::RenderSettings& rs) {
    return rs.post.exposure * (1.0f + 3.0f * LightningFlash());
}
/// Whether the GPU post chain has anything to do. With everything off / neutral
/// the renderers take their plain path, so the frame is exactly what it was.
inline bool PostActive(const Scene::RenderSettings& rs) {
    const auto& p = rs.post;
    if (p.bloom || p.grading || p.fxaa || p.ssao || p.dof) return true;
    if (std::fabs(p.exposure - 1.0f) > 1e-4f) return true;
    if (rs.tonemap && p.tonemap != Scene::RenderSettings::PostSettings::TonemapACES) return true;
    return LightningFlash() > 1e-3f;
}

// ---- Names <-> values (scripting, presets, the editor) ---------------------------
inline int TonemapFromName(const std::string& s) {
    if (s == "none" || s == "None" || s == "0") return Scene::RenderSettings::PostSettings::TonemapNone;
    if (s == "filmic" || s == "Filmic" || s == "2") return Scene::RenderSettings::PostSettings::TonemapFilmic;
    return Scene::RenderSettings::PostSettings::TonemapACES;
}
inline int FogModeFromName(const std::string& s) {
    if (s == "exponential" || s == "exp" || s == "Exponential" || s == "1") return Scene::RenderSettings::FogExponential;
    if (s == "height" || s == "Height" || s == "2") return Scene::RenderSettings::FogHeight;
    return Scene::RenderSettings::FogLinear;
}
inline int WeatherPresetFromName(const std::string& s) {
    using W = Scene::RenderSettings::WeatherSettings;
    if (s == "rain" || s == "Rain" || s == "1") return W::Rain;
    if (s == "snow" || s == "Snow" || s == "2") return W::Snow;
    if (s == "storm" || s == "Storm" || s == "3") return W::Storm;
    return W::None;
}
inline const char* WeatherPresetName(int preset) {
    static const char* kNames[] = {"none", "rain", "snow", "storm"};
    return (preset >= 0 && preset < 4) ? kNames[preset] : "none";
}

/// Set one post-processing value by name (the post_set builtin and
/// PostProcess.Set). Booleans take 0 / 1. Returns false for an unknown name.
inline bool PostSet(Scene::RenderSettings& rs, const std::string& name, float value) {
    auto& p = rs.post;
    const bool on = value != 0.0f;
    if (name == "exposure")            p.exposure = Mathf::Clamp(value, 0.05f, 8.0f);
    else if (name == "tonemap")        p.tonemap = (int)Mathf::Clamp(std::round(value), 0.0f, 2.0f);
    else if (name == "bloom")          p.bloom = on;
    else if (name == "bloom_threshold") p.bloomThreshold = Mathf::Clamp(value, 0.0f, 4.0f);
    else if (name == "bloom_intensity") p.bloomIntensity = Mathf::Clamp(value, 0.0f, 4.0f);
    else if (name == "bloom_radius")   p.bloomRadius = Mathf::Clamp(value, 0.25f, 3.0f);
    else if (name == "grading")        p.grading = on;
    else if (name == "temperature")    p.temperature = Mathf::Clamp(value, -1.0f, 1.0f);
    else if (name == "tint")           p.tint = Mathf::Clamp(value, -1.0f, 1.0f);
    else if (name == "saturation")     p.saturation = Mathf::Clamp(value, 0.0f, 3.0f);
    else if (name == "contrast")       p.contrast = Mathf::Clamp(value, 0.2f, 3.0f);
    else if (name == "vignette")       rs.vignette = Mathf::Clamp01(value);
    else if (name == "vignette_smoothness") p.vignetteSmooth = Mathf::Clamp(value, 0.05f, 1.0f);
    else if (name == "fxaa")           p.fxaa = on;
    else if (name == "ssao")           p.ssao = on;
    else if (name == "ssao_radius")    p.ssaoRadius = Mathf::Clamp(value, 0.05f, 4.0f);
    else if (name == "ssao_intensity") p.ssaoIntensity = Mathf::Clamp(value, 0.0f, 2.0f);
    else if (name == "dof")            p.dof = on;
    else if (name == "dof_focus")      p.dofFocus = Mathf::Clamp(value, 0.1f, 1000.0f);
    else if (name == "dof_range")      p.dofRange = Mathf::Clamp(value, 0.1f, 1000.0f);
    else return false;
    return true;
}

/// Put the sky, fog, post and weather settings back to their defaults (the
/// authored gradient colours are kept).
inline void ResetLook(Scene::RenderSettings& rs) {
    const Scene::RenderSettings d;
    rs.post = d.post;
    rs.vignette = d.vignette;
    rs.skyMode = d.skyMode; rs.skySunDisc = d.skySunDisc; rs.skySunHalo = d.skySunHalo; rs.skyHaze = d.skyHaze;
    rs.cloudCover = d.cloudCover; rs.cloudDensity = d.cloudDensity; rs.cloudSpeed = d.cloudSpeed; rs.cloudScale = d.cloudScale;
    rs.fog = d.fog; rs.fogMode = d.fogMode; rs.fogDensity = d.fogDensity; rs.fogHeight = d.fogHeight;
    rs.fogFalloff = d.fogFalloff; rs.fogMatchSky = d.fogMatchSky;
    rs.weather = d.weather;
}

/// The look presets the editor's Lighting panel offers.
inline const char* const* LookPresetNames(int& count) {
    static const char* kNames[] = {"Default", "Cinematic", "Vivid", "Noir", "Sunset", "Night", "Foggy morning", "Storm"};
    count = 8;
    return kNames;
}
/// Fill the settings with a ready-made look. Presets that need the sun at a
/// certain height (Sunset, Night, Foggy morning, Storm) write the elevation in
/// degrees to `sunElevationDeg` when given, so the caller can aim the sun light;
/// the others leave it untouched. Returns false for an unknown name.
inline bool ApplyLookPreset(Scene::RenderSettings& rs, const std::string& name, float* sunElevationDeg = nullptr) {
    using RS = Scene::RenderSettings;
    using PS = RS::PostSettings;
    ResetLook(rs);
    auto& p = rs.post;
    auto sun = [&](float deg) { if (sunElevationDeg) *sunElevationDeg = deg; };
    if (name == "Default") {
        return true;
    } else if (name == "Cinematic") {
        p.tonemap = PS::TonemapFilmic; p.exposure = 1.05f;
        p.bloom = true; p.bloomThreshold = 1.0f; p.bloomIntensity = 0.35f; p.bloomRadius = 1.2f;
        p.grading = true; p.temperature = 0.08f; p.saturation = 0.9f; p.contrast = 1.12f;
        p.lift = {-0.02f, -0.02f, -0.02f}; p.gain = {0.02f, 0.02f, 0.02f};
        rs.vignette = 0.35f; p.vignetteSmooth = 0.6f; p.fxaa = true;
    } else if (name == "Vivid") {
        p.tonemap = PS::TonemapACES; p.exposure = 1.15f;
        p.bloom = true; p.bloomThreshold = 0.9f; p.bloomIntensity = 0.5f; p.bloomRadius = 1.0f;
        p.grading = true; p.temperature = 0.05f; p.saturation = 1.35f; p.contrast = 1.1f;
        rs.vignette = 0.1f; p.fxaa = true;
    } else if (name == "Noir") {
        p.tonemap = PS::TonemapACES; p.exposure = 0.95f;
        p.bloom = true; p.bloomThreshold = 0.85f; p.bloomIntensity = 0.25f;
        p.grading = true; p.saturation = 0.0f; p.contrast = 1.3f;
        p.lift = {-0.05f, -0.05f, -0.05f}; p.gain = {0.05f, 0.05f, 0.05f};
        rs.vignette = 0.55f; p.vignetteSmooth = 0.5f; p.fxaa = true;
    } else if (name == "Sunset") {
        rs.skyMode = RS::SkyProcedural; rs.skyHaze = 0.8f; rs.cloudCover = 0.45f; rs.cloudDensity = 0.55f;
        rs.fog = true; rs.fogMode = RS::FogExponential; rs.fogDensity = 0.01f; rs.fogMatchSky = true;
        p.bloom = true; p.bloomThreshold = 0.9f; p.bloomIntensity = 0.45f; p.bloomRadius = 1.3f;
        p.grading = true; p.temperature = 0.2f; p.saturation = 1.1f;
        rs.vignette = 0.2f; p.fxaa = true;
        sun(8.0f);
    } else if (name == "Night") {
        rs.skyMode = RS::SkyProcedural; rs.skyHaze = 0.3f; rs.cloudCover = 0.2f;
        rs.skyStarDensity = 0.8f; rs.skyStarBright = 0.9f;
        rs.fog = true; rs.fogMode = RS::FogExponential; rs.fogDensity = 0.012f; rs.fogMatchSky = true;
        p.exposure = 0.9f;
        p.bloom = true; p.bloomThreshold = 0.7f; p.bloomIntensity = 0.5f; p.bloomRadius = 1.2f;
        p.grading = true; p.temperature = -0.15f; p.saturation = 0.9f;
        rs.vignette = 0.3f; p.fxaa = true;
        sun(-25.0f);
    } else if (name == "Foggy morning") {
        rs.skyMode = RS::SkyProcedural; rs.skyHaze = 1.0f; rs.cloudCover = 0.7f; rs.cloudDensity = 0.5f;
        rs.fog = true; rs.fogMode = RS::FogHeight; rs.fogDensity = 0.05f; rs.fogHeight = 2.0f; rs.fogFalloff = 0.25f;
        rs.fogMatchSky = true;
        p.grading = true; p.temperature = -0.05f; p.saturation = 0.85f;
        p.bloom = true; p.bloomThreshold = 0.95f; p.bloomIntensity = 0.3f; p.bloomRadius = 1.5f;
        p.fxaa = true;
        sun(12.0f);
    } else if (name == "Storm") {
        rs.skyMode = RS::SkyProcedural; rs.skyHaze = 0.6f; rs.cloudCover = 0.95f; rs.cloudDensity = 0.9f; rs.cloudSpeed = 3.0f;
        rs.fog = true; rs.fogMode = RS::FogExponential; rs.fogDensity = 0.02f; rs.fogMatchSky = true;
        rs.weather.preset = RS::WeatherSettings::Storm; rs.weather.intensity = 0.8f; rs.weather.wind = {6.0f, 0.0f, 2.0f};
        p.exposure = 0.9f;
        p.bloom = true; p.bloomThreshold = 0.8f; p.bloomIntensity = 0.3f;
        p.grading = true; p.temperature = -0.1f; p.saturation = 0.7f; p.contrast = 1.1f;
        rs.vignette = 0.35f; p.fxaa = true;
        sun(35.0f);
    } else {
        return false;
    }
    return true;
}

} // namespace okay
