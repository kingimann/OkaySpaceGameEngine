#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace okay {

/// A named container of Components living in a Scene — the engine's primary
/// unit of composition, modeled directly on Unity's GameObject.
class GameObject {
public:
    std::string name;
    /// Inactive GameObjects are skipped by Update and Render.
    bool active = true;
    /// Optional tag for lookups (see Scene::FindWithTag).
    std::string tag;
    /// Render/physics layer (0-31). A camera's cullingMask selects which layers it
    /// draws (bit 1<<layer); also used by the physics collision matrix.
    int layer = 0;
    /// Marked non-moving (Unity-style). A hint for tools/builds; objects flagged
    /// static are not expected to move at runtime.
    bool isStatic = false;

    /// Per-object UI layering override. 0 = use the widget's default type layer
    /// (the historic per-type pass order); any non-zero value places this widget in
    /// the single UI draw pass by this key instead (higher = drawn later / on top),
    /// letting non-nested widgets of different types be layered freely. Ignored by
    /// non-UI objects. Hierarchy pre-order breaks ties.
    int uiDrawOrder = 0;

    /// Every GameObject has a Transform from birth.
    Transform* transform = nullptr;

    /// Attach a new component of type T, forwarding constructor arguments.
    template <typename T, typename... Args>
    T* AddComponent(Args&&... args);

    /// Return the first attached component of type T, or nullptr.
    template <typename T>
    T* GetComponent() const;

    /// Return every attached component assignable to T.
    template <typename T>
    std::vector<T*> GetComponents() const;

    /// Append every component assignable to T to `out` (no per-object allocation;
    /// used by Scene::FindObjectsOfType to avoid a temporary vector per object).
    template <typename T>
    void CollectComponents(std::vector<T*>& out) const;

    /// Iterate the attached components in their stored order.
    const std::vector<std::unique_ptr<Component>>& Components() const { return m_components; }

    /// True if `other` is this object or any ancestor of it — used by cameras to skip
    /// a whole subtree (e.g. hiding the local player's body, rig parts included, from
    /// the first-person view without affecting other cameras).
    bool IsSelfOrDescendantOf(const GameObject* other) const;

    /// Move `component` by `delta` positions within the stored order. Transform
    /// is pinned to index 0: the valid target range is [1, size-1], so neither the
    /// Transform itself nor any other component can be moved to index 0. Out-of-range
    /// moves or an unknown component are no-ops.
    void MoveComponent(Component* component, int delta);

    /// Remove a specific component (Transform cannot be removed). Returns true
    /// if it was found and removed.
    bool RemoveComponent(Component* component);
    /// Remove the first component of type T. Returns true if one was removed.
    template <typename T>
    bool RemoveComponent() {
        for (const auto& c : m_components)
            if (T* casted = dynamic_cast<T*>(c.get())) return RemoveComponent(casted);
        return false;
    }

    Scene* scene() const { return m_scene; }

private:
    friend class Scene;
    explicit GameObject(Scene* scene, std::string name);

    /// Routes a freshly added component to the Scene's lifecycle queue.
    /// Defined in GameObject.cpp where the full Scene type is visible.
    void NotifyComponentAdded(Component* component);

    Scene* m_scene = nullptr;
    std::vector<std::unique_ptr<Component>> m_components;
};

// ---- Template implementations -----------------------------------------

template <typename T, typename... Args>
T* GameObject::AddComponent(Args&&... args) {
    static_assert(std::is_base_of<Component, T>::value,
                  "T must derive from okay::Component");
    auto owned = std::make_unique<T>(std::forward<Args>(args)...);
    T* ptr = owned.get();
    ptr->gameObject = this;
    ptr->transform  = transform ? transform : dynamic_cast<Transform*>(ptr);
    m_components.push_back(std::move(owned));
    NotifyComponentAdded(ptr); // declared in Scene.hpp, defined in Scene.cpp
    return ptr;
}

template <typename T>
T* GameObject::GetComponent() const {
    for (const auto& c : m_components)
        if (T* casted = dynamic_cast<T*>(c.get())) return casted;
    return nullptr;
}

template <typename T>
std::vector<T*> GameObject::GetComponents() const {
    std::vector<T*> result;
    for (const auto& c : m_components)
        if (T* casted = dynamic_cast<T*>(c.get())) result.push_back(casted);
    return result;
}

template <typename T>
void GameObject::CollectComponents(std::vector<T*>& out) const {
    for (const auto& c : m_components)
        if (T* casted = dynamic_cast<T*>(c.get())) out.push_back(casted);
}

} // namespace okay
