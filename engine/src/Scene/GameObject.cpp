#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"

namespace okay {

GameObject::GameObject(Scene* scene, std::string name)
    : name(std::move(name)), m_scene(scene) {}

void GameObject::NotifyComponentAdded(Component* component) {
    if (m_scene) m_scene->QueuePending(component);
}

} // namespace okay
