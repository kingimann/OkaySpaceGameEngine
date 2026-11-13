#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <random>
#include <typeindex>
#include <unordered_set>
#include <unordered_map>

namespace okay {

namespace {
// Scenes can be built on loader threads (scene thumbnails, async loads), so the
// id registry is guarded.
std::mutex g_registryMutex;
std::unordered_map<std::uint64_t, GameObject*> g_registry;
std::uint64_t g_nextInstanceId = 1;

std::mutex g_rulesMutex;
std::atomic<bool> g_anyRules{false};
std::atomic<std::uint64_t> g_rulesGeneration{0};   // moves whenever a rule is added or the rules are cleared
std::unordered_map<std::type_index, std::vector<ComponentRules::Requirement>> g_requirements;
std::unordered_set<std::type_index> g_unique;
thread_local std::vector<std::type_index> t_applying;   // owners whose requirements are being added
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

void ComponentRules::AddRequirement(std::type_index owner, Requirement req) {
    std::lock_guard<std::mutex> lock(g_rulesMutex);
    auto& v = g_requirements[owner];
    for (const auto& r : v) if (r.type == req.type) return;   // already declared
    v.push_back(req);
    g_anyRules = true;
    g_rulesGeneration.fetch_add(1, std::memory_order_relaxed);
}
void ComponentRules::SetUnique(std::type_index owner) {
    std::lock_guard<std::mutex> lock(g_rulesMutex);
    g_unique.insert(owner);
    g_anyRules = true;
    g_rulesGeneration.fetch_add(1, std::memory_order_relaxed);
}
bool ComponentRules::Any() { return g_anyRules.load(std::memory_order_relaxed); }
bool ComponentRules::IsUnique(std::type_index owner) {
    std::lock_guard<std::mutex> lock(g_rulesMutex);
    return g_unique.count(owner) != 0;
}
std::vector<ComponentRules::Requirement> ComponentRules::RequirementsOf(std::type_index owner) {
    std::lock_guard<std::mutex> lock(g_rulesMutex);
    auto it = g_requirements.find(owner);
    return it == g_requirements.end() ? std::vector<Requirement>{} : it->second;
}
void ComponentRules::Clear() {
    std::lock_guard<std::mutex> lock(g_rulesMutex);
    g_requirements.clear(); g_unique.clear(); g_anyRules = false;
    g_rulesGeneration.fetch_add(1, std::memory_order_relaxed);
}

namespace detail {
/// Moves whenever the RequireComponent / DisallowMultipleComponent rules change: the order a loader gives an
/// object's components depends on them (SceneSerializer keeps what it worked out per rule state).
std::uint64_t ComponentRulesGeneration() { return g_rulesGeneration.load(std::memory_order_relaxed); }
} // namespace detail

void GameObject::ApplyRequirements(std::type_index owner) {
    for (const auto& t : t_applying) if (t == owner) return;   // A needs B needs A: stop the loop
    const auto reqs = ComponentRules::RequirementsOf(owner);
    if (reqs.empty()) return;
    t_applying.push_back(owner);
    for (const auto& r : reqs) {
        bool inProgress = false;   // that type is itself being added further up: it is coming
        for (const auto& t : t_applying) if (t == r.type) inProgress = true;
        if (!inProgress && !r.has(*this)) r.add(*this);
    }
    t_applying.pop_back();
}

Component* GameObject::RequiredBy(const Component* component) const {
    if (!component || !ComponentRules::Any()) return nullptr;
    for (const auto& c : m_components) {
        if (c.get() == component) continue;
        for (const auto& r : ComponentRules::RequirementsOf(typeid(*c)))
            if (r.type == std::type_index(typeid(*component))) {
                // Only blocks if no other component could still satisfy it.
                int providers = 0;
                for (const auto& o : m_components)
                    if (o.get() != component && std::type_index(typeid(*o)) == r.type) ++providers;
                if (providers == 0) return c.get();
            }
    }
    return nullptr;
}

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

bool GameObject::HasLiveComponentInHierarchy() const {
    std::vector<const GameObject*> stack{this};   // explicit stack: a deep hierarchy cannot overflow
    while (!stack.empty()) {
        const GameObject* g = stack.back(); stack.pop_back();
        if (g->HasLiveComponent()) return true;
        if (g->transform)
            for (const Transform* c : g->transform->Children())
                if (c && c->gameObject) stack.push_back(c->gameObject);
    }
    return false;
}

bool GameObject::IsSelfOrDescendantOf(const GameObject* other) const {
    if (!other) return false;
    for (const Transform* t = transform; t; t = t->Parent())
        if (t->gameObject == other) return true;
    return false;
}

// The answer for this object, worked out for `epoch`. The walk climbs only until it meets an object whose
// answer is already known for this epoch (or an inactive one, or the top), then writes the answer into
// every object it passed on the way down. So after a change the first question costs the length of the
// chain, and every other object below that point finds its parent answered: the whole hierarchy is
// re-answered in linear time, however deep it is.
bool GameObject::ComputeActiveInHierarchy(std::uint64_t epoch) const {
    const GameObject* small[32];
    std::vector<const GameObject*> big;
    std::size_t n = 0;
    auto push = [&](const GameObject* g) {
        if (big.empty() && n < 32) { small[n++] = g; return; }
        if (big.empty()) big.assign(small, small + n);
        big.push_back(g);
        n = big.size();
    };
    auto at = [&](std::size_t i) { return big.empty() ? small[i] : big[i]; };
    auto store = [epoch](const GameObject* g, bool answer) {
        g->m_activeCache.store((epoch << 1) | (answer ? 1u : 0u), std::memory_order_relaxed);
    };
    bool value = true;   // the answer for what is above the last object pushed (true past the top)
    std::uint64_t steps = 0;
    const GameObject* g = this;
    // The bound only keeps a corrupted pointer from spinning for ever; a hierarchy cannot loop (SetParent refuses it).
    for (long guard = 0; g && guard < (1L << 26); ++guard) {
        ++steps;
        const std::uint64_t seen = g->m_activeCache.load(std::memory_order_relaxed);
        if ((seen >> 1) == epoch) { value = (seen & 1u) != 0; break; }
        if (!g->active) { value = false; store(g, false); break; }
        push(g);
        const Transform* p = g->transform ? g->transform->Parent() : nullptr;
        g = p ? p->gameObject : nullptr;
    }
    for (std::size_t i = n; i-- > 0;) store(at(i), value);   // every object pushed is itself active, so it takes its parent's answer
    detail::g_activeWalkSteps.fetch_add(steps, std::memory_order_relaxed);
    return value;
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
    m_customOrder = true;   // arranged on purpose: the scene file keeps it
}

bool GameObject::ReorderComponents(const std::vector<Component*>& first) {
    std::vector<Component*> want;   // this object's own components named in `first`, once each, never the Transform
    for (Component* c : first) {
        if (!c || c == static_cast<Component*>(transform)) continue;
        if (std::find(want.begin(), want.end(), c) != want.end()) continue;
        bool mine = false;
        for (const auto& own : m_components) if (own.get() == c) { mine = true; break; }
        if (mine) want.push_back(c);
    }
    if (want.empty()) return false;
    m_customOrder = true;
    Component* const pinned = transform;   // stays first
    std::vector<Component*> order;
    order.reserve(m_components.size());
    if (pinned) order.push_back(pinned);
    for (Component* w : want) order.push_back(w);
    for (const auto& c : m_components)
        if (c.get() != pinned && std::find(want.begin(), want.end(), c.get()) == want.end()) order.push_back(c.get());
    bool changed = false;
    for (std::size_t i = 0; i < order.size(); ++i) if (order[i] != m_components[i].get()) { changed = true; break; }
    if (!changed) return false;
    std::vector<std::unique_ptr<Component>> next;
    next.reserve(m_components.size());
    for (Component* o : order)
        for (auto& c : m_components)
            if (c.get() == o) { next.push_back(std::move(c)); break; }
    m_components.swap(next);
    return true;
}

bool GameObject::RemoveComponent(Component* component) {
    if (!component || component == transform) return false; // Transform is permanent
    // Only a component of this object (found by address: nothing is read through the pointer yet).
    auto find = [&]() {
        for (auto it = m_components.begin(); it != m_components.end(); ++it)
            if (it->get() == component) return it;
        return m_components.end();
    };
    if (find() == m_components.end()) return false;
    if (component->m_removing) return false;   // already on its way out: its own OnDisable / OnDestroy asked again
    component->m_removing = true;
    std::vector<Component*> parts;
    component->CollectOwnedParts(parts);
    for (Component* part : parts) RemoveComponent(part);
    component->DeliverDisableBeforeDestroy();
    component->OnDestroy();
    // Those callbacks may add or remove components of this object (the list can have moved or
    // shifted), so the position is looked up again, never kept from before.
    auto it = find();
    if (it == m_components.end()) return false;
    // Notify after OnDestroy: a query it runs can't cache a component about to be freed.
    if (m_scene) m_scene->NotifyComponentRemoved(component);
    m_components.erase(it);
    return true;
}

bool GameObject::SetLayer(int newLayer, bool includeChildren) {
    if (newLayer < 0 || newLayer > 31) return false;
    if (!includeChildren) { layer = newLayer; return true; }
    std::vector<GameObject*> stack{this};   // explicit stack: a deep hierarchy cannot overflow
    while (!stack.empty()) {
        GameObject* g = stack.back(); stack.pop_back();
        g->layer = newLayer;
        if (g->transform)
            for (Transform* c : g->transform->Children())
                if (c && c->gameObject) stack.push_back(c->gameObject);
    }
    return true;
}

std::string GameObject::Path() const {
    std::vector<const GameObject*> chain;
    for (const GameObject* g = this; g;) {
        chain.push_back(g);
        const Transform* p = g->transform ? g->transform->Parent() : nullptr;
        g = p ? p->gameObject : nullptr;
    }
    std::string out;
    for (std::size_t i = chain.size(); i-- > 0;) {
        out += chain[i]->name;
        if (i > 0) out += '/';
    }
    return out;
}

void GameObject::StripToTransform() {
    for (std::size_t i = m_components.size(); i-- > 0;)
        if (Component* c = m_components[i].get(); c != static_cast<Component*>(transform)) {
            // The scene records the freed address in its removal watches before the
            // free, so a StartComponent watching while an OnDestroy destroyed the
            // whole object skips the freed component instead of touching it (#735).
            if (m_scene) m_scene->NotifyComponentRemoved(c);
            m_components.erase(m_components.begin() + (long)i);
        }
    active = false;
    m_destroyed = true;
}

} // namespace okay
