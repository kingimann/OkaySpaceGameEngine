#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Render/Lighting.hpp"
#include <cmath>
#include <cstdint>

namespace okay {

/// A scene light. `Directional` shines along its GameObject's forward (+Z) from
/// infinitely far (like the sun); `Point` radiates from its position out to
/// `range`; `Spot` is a point light limited to a cone of `spotAngle` degrees
/// about its forward. `color` × `intensity` is the light's contribution, and the
/// first light's `ambient` sets the global unlit floor (0..1).
class Light : public Component {
public:
    // Area shines like a point but with a soft "wrap" diffuse (light seems to come
    // from a broad panel, not a pinpoint) — good for windows, screens, soft studio
    // fill. Keep values stable for serialization (append only).
    enum class Type { Directional, Point, Spot, Area };
    Type  type      = Type::Directional;
    // How a point/spot/area light dims with distance. Linear = the classic
    // (1 - d/range)^2 rolloff (predictable, gamey). InverseSquare = physically based
    // 1/d^2 with a smooth cutoff at `range` (realistic, punchier near the source).
    enum class Falloff { Linear, InverseSquare };
    Falloff falloff = Falloff::Linear;
    Color color     = Color::White;
    float intensity = 1.0f;
    float ambient   = 0.30f;        // unlit floor (taken from the first light)
    float range     = 10.0f;        // point / spot reach
    float spotAngle = 45.0f;        // spot cone (full angle, degrees)
    float spotSoftness = 0.5f;      // 0 = hard cone edge, 1 = very soft
    bool  useTemperature = false;   // drive `color` from a Kelvin temperature
    float temperature = 6500.0f;    // white point in Kelvin (1000..15000)
    Color ambientColor = Color::White;  // tint of the ambient floor (x ambient)
    // Shadows. A directional light's shadows are the cascaded sun shadows (only
    // the first directional light casts them); point/spot/area lights render
    // their own shadow maps (up to 16 tiles' worth, nearest the camera first).
    bool  castShadows = true;
    float shadowStrength = 1.0f;    // 0 = invisible .. 1 = fully dark
    float shadowBias = 1.0f;        // multiplier: raise on acne, lower if shadows detach
    // Flicker (torches, candles, failing bulbs): randomly dims the intensity by up
    // to `flicker` (0..1) at `flickerSpeed` changes per second, while playing.
    float flicker = 0.0f;
    float flickerSpeed = 8.0f;

    void Update(float dt) override { if (dt > 0.0f) m_flickerTime += dt; }
    /// Intensity multiplier from flicker at the current play time (1 when flicker
    /// is off or the game hasn't run yet, so edit mode shows the full light).
    float FlickerFactor() const {
        if (flicker <= 0.0f || m_flickerTime <= 0.0f) return 1.0f;
        float a = flicker > 1.0f ? 1.0f : flicker;
        float t = m_flickerTime * (flickerSpeed > 0.0f ? flickerSpeed : 0.0f);
        // Smooth value noise: random levels at integer steps, eased between them,
        // plus a faster small jitter so it never looks like a clean sine.
        // Seeded per light instance so several torches don't flicker in sync.
        const unsigned seed = m_seed ^ (unsigned)(reinterpret_cast<std::uintptr_t>(this) >> 4);
        auto h = [seed](float i) {
            unsigned x = (unsigned)(long long)i * 2654435761u ^ seed;
            x ^= x >> 15; x *= 2246822519u; x ^= x >> 13; x *= 3266489917u; x ^= x >> 16;
            return (float)(x & 0xFFFFu) / 65535.0f;
        };
        auto noise = [&](float u) {
            float i = std::floor(u), f = u - i;
            f = f * f * (3.0f - 2.0f * f);
            return h(i) + (h(i + 1.0f) - h(i)) * f;
        };
        float n = 0.75f * noise(t) + 0.25f * noise(t * 3.7f + 17.0f);
        return 1.0f - a * n;
    }

    /// Approximate RGB (0..1) of a black-body at `kelvin` (Tanner Helland fit),
    /// so a light can be tinted by color temperature like a real bulb / the sun.
    static Color KelvinToColor(float kelvin) {
        float t = (kelvin < 1000.f ? 1000.f : (kelvin > 40000.f ? 40000.f : kelvin)) / 100.0f;
        float r, g, b;
        if (t <= 66.f) { r = 255.f; g = 99.4708f * std::log(t) - 161.1196f; }
        else { r = 329.6987f * std::pow(t - 60.f, -0.1332f); g = 288.1222f * std::pow(t - 60.f, -0.0755f); }
        if (t >= 66.f) b = 255.f;
        else if (t <= 19.f) b = 0.f;
        else b = 138.5177f * std::log(t - 10.f) - 305.0448f;
        auto cl = [](float v) { return v < 0.f ? 0.f : (v > 255.f ? 255.f : v); };
        return Color{cl(r) / 255.f, cl(g) / 255.f, cl(b) / 255.f, 1.0f};
    }
    /// The light's effective RGB (temperature-driven when enabled).
    Color EffectiveColor() const { return useTemperature ? KelvinToColor(temperature) : color; }

private:
    float m_flickerTime = 0.0f;
    unsigned m_seed = 0x9E3779B9u;
};

/// Gather every active Light in the scene into SceneLights for multi-light
/// shading, and mirror the first directional one into the legacy global
/// SceneLight (so set_light/set_ambient scripts and old code still work). Called
/// by the player and editor each frame before rendering 3D.
inline void ApplySceneLight(const Scene& scene) {
    SceneLights::Clear();
    bool any = false, setAmbient = false, setLegacy = false;
    const float kPi = 3.14159265358979323846f;
    for (const auto& go : scene.Objects()) {
        if (!go->active) continue;
        auto* l = go->GetComponent<Light>();
        if (!l || !l->enabled) continue;   // a disabled Light component gives no light
        any = true;
        LightSample s;
        Color ec = l->EffectiveColor();
        s.type  = (int)l->type;
        s.falloff = (int)l->falloff;
        s.dir   = go->transform->Forward();
        s.pos   = go->transform->Position();
        s.color = Vec3{ec.r, ec.g, ec.b} * (l->intensity * l->FlickerFactor());
        s.range = l->range;
        float half = 0.5f * l->spotAngle * kPi / 180.0f;
        s.cosOuter = std::cos(half);
        // Soft inner cone: a fraction of the way in from the edge to the axis.
        float soft = l->spotSoftness < 0.f ? 0.f : (l->spotSoftness > 1.f ? 1.f : l->spotSoftness);
        s.cosInner = std::cos(half * (1.0f - soft));
        s.castShadows = l->castShadows;
        s.shadowStrength = l->shadowStrength < 0.f ? 0.f : (l->shadowStrength > 1.f ? 1.f : l->shadowStrength);
        s.shadowBias = l->shadowBias < 0.f ? 0.f : l->shadowBias;
        if (!setLegacy && l->type == Light::Type::Directional)
            SceneLights::SunIndex() = (int)SceneLights::List().size();   // the shadow-casting sun
        SceneLights::Add(s);
        if (!setAmbient) {
            Vec3 ac{l->ambientColor.r * l->ambient, l->ambientColor.g * l->ambient, l->ambientColor.b * l->ambient};
            SceneLights::SetAmbientColor(ac);
            SceneLight::SetAmbient(l->ambient);
            setAmbient = true;
        }
        if (!setLegacy && l->type == Light::Type::Directional) { SceneLight::SetDirection(s.dir); setLegacy = true; }
    }
    if (!any) {
        // No Light object: keep the script-controllable global light + the
        // scene's ambient so a built game still has the author's base lighting.
        // No Light object: fall back to the scene's ambient render setting so a
        // built game still has the base lighting the author chose.
        SceneLights::SetAmbient(scene.renderSettings.ambient);
        SceneLight::SetDirection(DefaultLightDir());
        SceneLight::SetAmbient(scene.renderSettings.ambient);
        // ...and a default sun, like default lighting: the GPU paths shade
        // only from the gathered list, so a scene without a Light object (a new
        // empty scene, an imported level) otherwise rendered almost black under
        // the 0.15 ambient. Adding any Light replaces it.
        LightSample sun;
        sun.type = (int)Light::Type::Directional;
        sun.dir = SceneLight::Direction();
        sun.pos = Vec3{0, 0, 0};
        sun.color = Vec3{1.0f, 0.97f, 0.92f};
        sun.range = 0.0f; sun.cosOuter = sun.cosInner = 1.0f;
        SceneLights::SunIndex() = (int)SceneLights::List().size();
        SceneLights::Add(sun);
    }
}

} // namespace okay
