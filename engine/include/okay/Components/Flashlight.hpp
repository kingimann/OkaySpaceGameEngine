#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Input/Input.hpp"
#include <vector>

namespace okay {

/// A toggleable player flashlight — a spot light that shines wherever the camera
/// looks, so you can see inside caves / tunnels / dark rooms. Drop it on the player
/// (or the camera) and press the toggle key (F by default) to switch it on/off.
///
/// It mounts the light as a child of the main camera, turned to face the camera's
/// view direction (cameras look down -Z, so the light is rotated 180°), so the beam
/// always follows where you're looking with no per-frame work.
class Flashlight : public Behaviour {
public:
    char  toggleKey = 'f';                 ///< press to toggle (0 = no key; still starts in `on`)
    bool  on        = true;                ///< current state
    float range     = 22.0f;               ///< how far the beam reaches (world units)
    float angle     = 55.0f;               ///< cone width (full angle, degrees)
    float intensity = 1.6f;                ///< brightness
    Color color     = Color::FromBytes(255, 244, 214);   ///< warm torch white

    void Start() override { EnsureLight(); Sync(); }

    void Update(float) override {
        if (!m_light) { EnsureLight(); }
        if (toggleKey && Input::GetKeyDown(toggleKey)) { on = !on; Sync(); }
        Sync();
    }

    void OnDestroy() override {
        if (m_obj) { if (Scene* s = GetScene()) s->Destroy(m_obj); m_obj = nullptr; m_light = nullptr; }
    }

    /// Turn the beam on/off from code (e.g. a UI button or a battery system).
    void SetOn(bool v) { on = v; Sync(); }

private:
    GameObject* m_obj   = nullptr;
    Light*      m_light = nullptr;

    /// The camera to mount the beam on: this object's own Camera, else the main one.
    Camera* FindCamera() const {
        if (gameObject)
            if (auto* c = gameObject->GetComponent<Camera>()) return c;
        Scene* s = GetScene();
        if (!s) return nullptr;
        if (s->mainCamera) return s->mainCamera;
        return s->FindObjectOfType<Camera>();
    }

    void EnsureLight() {
        if (m_light) return;
        Scene* s = GetScene();
        Camera* cam = FindCamera();
        if (!s || !cam || !cam->gameObject || !cam->gameObject->transform) return;
        // Adopt an existing "Flashlight" beam under the camera instead of spawning a
        // new one (Play→Stop→Play, a reloaded scene, or two Flashlight components would
        // otherwise stack up duplicates). Keep the first; clean up any extras.
        std::vector<GameObject*> existing;
        for (Transform* c : cam->gameObject->transform->Children())
            if (c && c->gameObject && c->gameObject->name == "Flashlight"
                && c->gameObject->GetComponent<Light>())
                existing.push_back(c->gameObject);
        for (std::size_t i = 0; i < existing.size(); ++i) {
            if (i == 0) { m_obj = existing[0]; m_light = existing[0]->GetComponent<Light>(); }
            else s->Destroy(existing[i]);     // stray duplicate from a previous run
        }
        if (m_light) return;
        m_obj = s->CreateGameObject("Flashlight");
        m_obj->transform->SetParent(cam->gameObject->transform, false);
        // Cameras look down -Z (their Forward is +Z, i.e. behind the view), so spin
        // the light 180° about Y to aim it down the camera's actual view direction.
        m_obj->transform->localRotation = Quat::Euler({0.0f, 180.0f, 0.0f});
        m_obj->transform->localPosition = {0.0f, 0.0f, 0.0f};
        m_light = m_obj->AddComponent<Light>();
        m_light->type = Light::Type::Spot;
    }

    void Sync() {
        if (!m_light) return;
        m_light->enabled = on;
        if (m_obj) m_obj->active = on;       // also hide it from the light collector when off
        m_light->color     = color;
        m_light->intensity = intensity;
        m_light->range     = range;
        m_light->spotAngle = angle;
    }
};

} // namespace okay
