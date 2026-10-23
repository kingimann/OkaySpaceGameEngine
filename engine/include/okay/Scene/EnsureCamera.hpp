#pragma once
// ---------------------------------------------------------------------------
// Every scene needs a camera: without one the Game view / built game renders
// nothing. EnsureSceneCamera returns the scene's main camera, promoting an
// existing camera if none is flagged main, and creates a "Main Camera" when the
// scene has none at all (perspective at (0, 2, 10), or orthographic for 2D).
// ---------------------------------------------------------------------------
#include "okay/Scene/Scene.hpp"
#include <string>
#include <vector>
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

/// True when the object and every parent above it are active.
inline bool ActiveInHierarchy(const GameObject* go) {
    for (int guard = 0; go && guard < 256; ++guard) {
        if (!go->active) return false;
        const Transform* p = go->transform ? go->transform->Parent() : nullptr;
        go = p ? p->gameObject : nullptr;
    }
    return true;
}
/// A camera that can render right now: enabled and active in the hierarchy.
inline bool CameraUsable(const Camera* c) {
    return c && c->enabled && c->gameObject && ActiveInHierarchy(c->gameObject);
}

/// Which camera the scene renders through, given the current one and every live
/// camera. Keeps `current` while it can render, unless another usable camera is
/// marked Main and outranks it (current isn't Main, or the other has a higher
/// Depth). When `current` can't render (its object or component was turned off),
/// the best usable camera takes over: Main first, then highest Depth. So enabling
/// one camera and disabling another switches the view (cutscenes, death cams).
inline Camera* PickRenderCamera(Camera* current, const std::vector<Camera*>& cams) {
    bool currentLive = false;
    for (Camera* c : cams) if (c == current) { currentLive = true; break; }
    if (!currentLive) current = nullptr;
    Camera* best = nullptr;
    for (Camera* c : cams) {
        if (!CameraUsable(c)) continue;
        if (!best || (c->main && !best->main) || (c->main == best->main && c->depth > best->depth)) best = c;
    }
    if (current && CameraUsable(current)) {
        if (best && best != current && best->main && (!current->main || best->depth > current->depth)) return best;
        return current;
    }
    return best ? best : current;   // nothing usable: keep what we had
}

inline Camera* EnsureSceneCamera(Scene& s, bool orthographic, bool* created = nullptr, bool* restored = nullptr) {
    if (created) *created = false;
    if (restored) *restored = false;
    // Prefer an active camera flagged main, then any active camera (promoted to
    // main so the Game view and the player both use it), then an inactive one.
    Camera* mainActive = nullptr; Camera* firstActive = nullptr; Camera* first = nullptr;
    for (const auto& up : s.Objects()) {
        if (!up) continue;
        if (auto* c = up->GetComponent<Camera>()) {
            if (!first) first = c;
            if (!up->active || !c->enabled) continue;
            if (c->main && !mainActive) mainActive = c;
            if (!firstActive) firstActive = c;
        }
    }
    Camera* pick = mainActive ? mainActive : (firstActive ? firstActive : first);
    if (pick) {
        pick->main = true;
        if (!s.mainCamera || s.mainCamera == pick || !s.mainCamera->gameObject || !s.mainCamera->gameObject->active)
            s.mainCamera = pick;
        return pick;
    }
    // No Camera component anywhere. If a camera OBJECT is still there (its Camera
    // component was removed), give it back its component instead of adding a second
    // "Main Camera" beside a camera that doesn't render. "Main Camera" first, then
    // any object named like a camera.
    GameObject* shell = nullptr;
    for (const auto& up : s.Objects()) {
        if (!up || up->GetComponent<Camera>()) continue;
        if (up->name == "Main Camera") { shell = up.get(); break; }
        if (!shell && up->name.find("Camera") != std::string::npos) shell = up.get();
    }
    GameObject* go = shell ? shell : s.CreateGameObject("Main Camera");
    auto* cam = go->AddComponent<Camera>();
    cam->projection = orthographic ? Camera::Projection::Orthographic : Camera::Projection::Perspective;
    cam->main = true;
    if (!shell) go->transform->localPosition = orthographic ? Vec3{0, 0, 10} : Vec3{0, 2, 10};
    s.mainCamera = cam;
    if (created) *created = true;
    if (restored) *restored = shell != nullptr;
    return cam;
}

} // namespace okay
