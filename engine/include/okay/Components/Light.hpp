#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Render/Lighting.hpp"
#include <cmath>

namespace okay {

/// A scene light. `Directional` shines along its GameObject's forward (+Z) from
/// infinitely far (like the sun); `Point` radiates from its position out to
/// `range`; `Spot` is a point light limited to a cone of `spotAngle` degrees
/// about its forward. `color` × `intensity` is the light's contribution, and the
/// first light's `ambient` sets the global unlit floor (0..1).
class Light : public Component {
public:
    enum class Type { Directional, Point, Spot };
    Type  type      = Type::Directional;
    Color color     = Color::White;
    float intensity = 1.0f;
    float ambient   = 0.30f;        // unlit floor (taken from the first light)
    float range     = 10.0f;        // point / spot reach
    float spotAngle = 45.0f;        // spot cone (full angle, degrees)
    float spotSoftness = 0.5f;      // 0 = hard cone edge, 1 = very soft
    bool  useTemperature = false;   // drive `color` from a Kelvin temperature
    float temperature = 6500.0f;    // white point in Kelvin (1000..15000)
    Color ambientColor = Color::White;  // tint of the ambient floor (x ambient)

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
        if (!l) continue;
        any = true;
        LightSample s;
        Color ec = l->EffectiveColor();
        s.type  = (int)l->type;
        s.dir   = go->transform->Forward();
        s.pos   = go->transform->Position();
        s.color = Vec3{ec.r, ec.g, ec.b} * l->intensity;
        s.range = l->range;
        float half = 0.5f * l->spotAngle * kPi / 180.0f;
        s.cosOuter = std::cos(half);
        // Soft inner cone: a fraction of the way in from the edge to the axis.
        float soft = l->spotSoftness < 0.f ? 0.f : (l->spotSoftness > 1.f ? 1.f : l->spotSoftness);
        s.cosInner = std::cos(half * (1.0f - soft));
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
    }
}

} // namespace okay
