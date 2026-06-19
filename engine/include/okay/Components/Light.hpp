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
        s.type  = (int)l->type;
        s.dir   = go->transform->Forward();
        s.pos   = go->transform->Position();
        s.color = Vec3{l->color.r, l->color.g, l->color.b} * l->intensity;
        s.range = l->range;
        s.cosOuter = std::cos(0.5f * l->spotAngle * kPi / 180.0f);
        SceneLights::Add(s);
        if (!setAmbient) { SceneLights::SetAmbient(l->ambient); SceneLight::SetAmbient(l->ambient); setAmbient = true; }
        if (!setLegacy && l->type == Light::Type::Directional) { SceneLight::SetDirection(s.dir); setLegacy = true; }
    }
    if (!any) {
        // No Light object: keep the script-controllable global light + the
        // scene's ambient so a built game still has the author's base lighting.
        SceneLights::SetAmbient(scene.renderSettings.ambient);
        SceneLight::SetAmbient(scene.renderSettings.ambient);
    }
}

} // namespace okay
