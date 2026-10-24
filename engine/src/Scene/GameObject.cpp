#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <mutex>
#include <unordered_map>

namespace okay {

namespace {
// Scenes can be built on loader threads (scene thumbnails, async loads), so the
// id registry is guarded.
std::mutex g_registryMutex;
std::unordered_map<std::uint64_t, GameObject*> g_registry;
std::uint64_t g_nextInstanceId = 1;
} // namespace

GameObject::GameObject(Scene* scene, std::string name)
    : name(std::move(name)), m_scene(scene) {
    std::lock_guard<std::mutex> lock(g_registryMutex);
    m_instanceId = g_nextInstanceId++;
    g_registry[m_instanceId] = this;
}

GameObject::~GameObject() {
    std::lock_guard<std::mutex> lock(g_registryMutex);
    g_registry.erase(m_instanceId);
}

GameObject* GameObject::FromInstanceId(std::uint64_t id) {
    std::lock_guard<std::mutex> lock(g_registryMutex);
    auto it = g_registry.find(id);
    return (it == g_registry.end() || it->second->m_destroyed) ? nullptr : it->second;
}

bool GameObject::IsSelfOrDescendantOf(const GameObject* other) const {
    if (!other) return false;
    for (const Transform* t = transform; t; t = t->Parent())
        if (t->gameObject == other) return true;
    return false;
}

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
