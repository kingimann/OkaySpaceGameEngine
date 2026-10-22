#pragma once
// ---------------------------------------------------------------------------
// Every scene needs a camera: without one the Game view / built game renders
// nothing. EnsureSceneCamera returns the scene's main camera, promoting an
// existing camera if none is flagged main, and creates a "Main Camera" when the
// scene has none at all (perspective at (0, 2, 10), or orthographic for 2D).
// ---------------------------------------------------------------------------
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/MeshRenderer.hpp"

namespace okay {

/// True when the scene looks 2D: sprites and no 3D meshes.
inline bool SceneLooks2D(const Scene& s) {
    bool sprites = false;
    for (const auto& up : s.Objects()) {
        if (!up) continue;
        if (up->GetComponent<MeshRenderer>()) return false;
        if (up->GetComponent<SpriteRenderer>()) sprites = true;
    }
    return sprites;
}

inline Camera* EnsureSceneCamera(Scene& s, bool orthographic, bool* created = nullptr) {
    if (created) *created = false;
    Camera* first = nullptr;
    for (const auto& up : s.Objects()) {
        if (!up) continue;
        if (auto* c = up->GetComponent<Camera>()) {
            if (c->main) { if (!s.mainCamera) s.mainCamera = c; return c; }
            if (!first) first = c;
        }
    }
    if (first) {
        if (!s.mainCamera) s.mainCamera = first;
        return first;
    }
    GameObject* go = s.CreateGameObject("Main Camera");
    auto* cam = go->AddComponent<Camera>();
    cam->projection = orthographic ? Camera::Projection::Orthographic : Camera::Projection::Perspective;
    cam->main = true;
    go->transform->localPosition = orthographic ? Vec3{0, 0, 10} : Vec3{0, 2, 10};
    s.mainCamera = cam;
    if (created) *created = true;
    return cam;
}

} // namespace okay
