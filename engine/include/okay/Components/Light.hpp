#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Render/Lighting.hpp"

namespace okay {

/// A directional light. Its GameObject's forward (+Z) is the direction the light
/// shines; rotate the object to aim it (like Unity's Directional Light). The
/// 3D renderer reads the first active Light each frame into the global
/// SceneLight. `ambient` is the unlit floor brightness (0..1).
class Light : public Component {
public:
    Color color   = Color::White;   // reserved for tinted lighting
    float ambient = 0.30f;
    float intensity = 1.0f;         // reserved (flat shading uses N·L)
};

/// Push the first active Light in the scene into the global SceneLight. Called
/// by the player and editor each frame before rendering 3D. Falls back to the
/// engine default when there is no Light.
inline void ApplySceneLight(const Scene& scene) {
    for (const auto& go : scene.Objects()) {
        if (!go->active) continue;
        if (auto* l = go->GetComponent<Light>()) {
            SceneLight::SetDirection(go->transform->Forward());
            SceneLight::SetAmbient(l->ambient);
            return;
        }
    }
    // No Light object: fall back to the scene's ambient render setting so a
    // built game still has the base lighting the author chose.
    SceneLight::SetDirection(DefaultLightDir());
    SceneLight::SetAmbient(scene.renderSettings.ambient);
}

} // namespace okay
