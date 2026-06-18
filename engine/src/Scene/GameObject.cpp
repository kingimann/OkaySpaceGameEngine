#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"

namespace okay {

GameObject::GameObject(Scene* scene, std::string name)
    : name(std::move(name)), m_scene(scene) {}

void GameObject::NotifyComponentAdded(Component* component) {
    if (m_scene) m_scene->QueuePending(component);
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
