#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"

namespace okay {

GameObject::GameObject(Scene* scene, std::string name)
    : name(std::move(name)), m_scene(scene) {}

void GameObject::NotifyComponentAdded(Component* component) {
    if (m_scene) m_scene->QueuePending(component);
}

void GameObject::MoveComponent(Component* component, int delta) {
    if (!component || component == transform || delta == 0) return;
    int index = -1;
    for (int i = 0; i < static_cast<int>(m_components.size()); ++i) {
        if (m_components[i].get() == component) { index = i; break; }
    }
    if (index < 0) return;
    int target = index + delta;
    // Transform stays pinned at index 0; nothing else may occupy it.
    if (target < 1 || target >= static_cast<int>(m_components.size())) return;
    auto owned = std::move(m_components[index]);
    m_components.erase(m_components.begin() + index);
    m_components.insert(m_components.begin() + target, std::move(owned));
}

bool GameObject::RemoveComponent(Component* component) {
    if (!component || component == transform) return false; // Transform is permanent
    for (auto it = m_components.begin(); it != m_components.end(); ++it) {
        if (it->get() == component) {
            if (m_scene) m_scene->NotifyComponentRemoved(component);
            component->OnDestroy();
            m_components.erase(it);
            return true;
        }
    }
    return false;
}

} // namespace okay
