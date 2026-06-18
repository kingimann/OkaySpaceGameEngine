#pragma once
#include "okay/Scene/GameObject.hpp"
#include "okay/Core/Scheduler.hpp"
#include "okay/Physics/Physics2D.hpp"
#include <memory>
#include <string>
#include <vector>

namespace okay {

class Component;
class Camera;
class IRenderer;

/// Owns a graph of GameObjects and drives their lifecycle and rendering.
/// Analogous to a Unity Scene.
class Scene {
public:
    explicit Scene(std::string name = "Scene");
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    const std::string& Name() const { return m_name; }
    void SetName(const std::string& name) { m_name = name; }

    /// Create an empty GameObject (with a Transform) owned by this scene.
    GameObject* CreateGameObject(const std::string& name = "GameObject");

    /// Remove every GameObject immediately (used when loading a scene).
    void Clear();

    /// Clone a prefab (a GameObject and its descendants) into this scene.
    GameObject* Instantiate(const GameObject& prefab);
    GameObject* Instantiate(const GameObject& prefab, const Vec3& position);

    /// Mark a GameObject for destruction; removed at the end of the frame.
    void Destroy(GameObject* go);

    // ---- Queries -------------------------------------------------------
    GameObject* Find(const std::string& name) const;
    GameObject* FindWithTag(const std::string& tag) const;
    const std::vector<std::unique_ptr<GameObject>>& Objects() const { return m_objects; }

    /// Collect every component of type T across all GameObjects in the scene.
    template <typename T>
    std::vector<T*> FindObjectsOfType() const {
        std::vector<T*> out;
        for (const auto& go : m_objects)
            for (T* c : go->GetComponents<T>()) out.push_back(c);
        return out;
    }
    /// The first component of type T found in the scene, or nullptr.
    template <typename T>
    T* FindObjectOfType() const {
        for (const auto& go : m_objects)
            if (T* c = go->GetComponent<T>()) return c;
        return nullptr;
    }

    /// The camera used for rendering. Set automatically by Camera::Awake.
    Camera* mainCamera = nullptr;

    /// Time-based callbacks (Invoke / InvokeRepeating / Tween), ticked each
    /// frame during Update.
    Scheduler& scheduler() { return m_scheduler; }

    /// The 2D physics world, stepped each frame during Update.
    Physics2D& physics() { return m_physics; }
    /// Set false to skip the physics step (e.g. for pure-UI scenes).
    bool physicsEnabled = true;

    // ---- Lifecycle (driven by the Application) ------------------------
    /// Run Awake/Start on all components created so far.
    void Start();
    /// Advance one frame: flush pending components, Update, LateUpdate, cleanup.
    void Update(float deltaTime);
    /// Issue the render pass to the given backend.
    void Render(IRenderer& renderer);

private:
    friend class GameObject;
    void QueuePending(Component* component);
    void NotifyComponentRemoved(Component* component);
    void FlushPending();

    std::string m_name;
    Scheduler   m_scheduler;
    Physics2D   m_physics;
    std::vector<std::unique_ptr<GameObject>> m_objects;
    std::vector<Component*>  m_pending;   // awaiting Awake/Start
    std::vector<Component*>  m_active;    // receive Update each frame
    std::vector<GameObject*> m_destroyQueue;
};

} // namespace okay
