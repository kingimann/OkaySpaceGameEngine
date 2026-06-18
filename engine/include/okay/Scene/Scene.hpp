#pragma once
#include <memory>
#include <string>
#include <vector>

namespace okay {

class GameObject;
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

    /// Mark a GameObject for destruction; removed at the end of the frame.
    void Destroy(GameObject* go);

    // ---- Queries -------------------------------------------------------
    GameObject* Find(const std::string& name) const;
    GameObject* FindWithTag(const std::string& tag) const;
    const std::vector<std::unique_ptr<GameObject>>& Objects() const { return m_objects; }

    /// The camera used for rendering. Set automatically by Camera::Awake.
    Camera* mainCamera = nullptr;

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
    void FlushPending();

    std::string m_name;
    std::vector<std::unique_ptr<GameObject>> m_objects;
    std::vector<Component*>  m_pending;   // awaiting Awake/Start
    std::vector<Component*>  m_active;    // receive Update each frame
    std::vector<GameObject*> m_destroyQueue;
};

} // namespace okay
