#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <chrono>
#include <mutex>
#include <random>
#include <unordered_map>

namespace okay {

namespace {
// Scenes can be built on loader threads (scene thumbnails, async loads), so the
// id registry is guarded.
std::mutex g_registryMutex;
std::unordered_map<std::uint64_t, GameObject*> g_registry;
std::uint64_t g_nextInstanceId = 1;

std::string NewGuid() {   // caller holds g_registryMutex
    static std::mt19937_64 rng{std::random_device{}() ^ (std::uint64_t)std::chrono::steady_clock::now().time_since_epoch().count()};
    static const char* hex = "0123456789abcdef";
    std::string g(32, '0');
    for (int w = 0; w < 2; ++w) {
        std::uint64_t v = rng();
        for (int i = 0; i < 16; ++i) { g[(std::size_t)(w * 16 + i)] = hex[v & 15u]; v >>= 4; }
    }
    return g;
}
} // namespace

bool GameObject::SetGuid(const std::string& guid) {
    if (guid.empty() || guid.size() > 64) return false;
    for (char c : guid)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '-')) return false;
    m_guid = guid;
    return true;
}

GameObject::GameObject(Scene* scene, std::string name)
    : name(std::move(name)), m_scene(scene) {
    std::lock_guard<std::mutex> lock(g_registryMutex);
    m_instanceId = g_nextInstanceId++;
    m_guid = NewGuid();
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
    std::vector<Component*> parts;
    component->CollectOwnedParts(parts);
    for (Component* part : parts) RemoveComponent(part);
    for (auto it = m_components.begin(); it != m_components.end(); ++it) {
        if (it->get() == component) {
            component->OnDestroy();
            // Notify after OnDestroy: a query it runs can't cache a component about to be freed.
            if (m_scene) m_scene->NotifyComponentRemoved(component);
            m_components.erase(it);
            return true;
        }
    }
    return false;
}

} // namespace okay
