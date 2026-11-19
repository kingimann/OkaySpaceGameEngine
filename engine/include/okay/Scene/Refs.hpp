#pragma once
// ---------------------------------------------------------------------------
// ObjectRef and ComponentRef<T>: a handle to a GameObject / Component that turns into "nothing" when the thing is
// destroyed, instead of dangling.
//
// A raw `GameObject*` or `Enemy*` kept in a variable is a hazard: the object can be destroyed (the scene frees its
// components at the end of the frame) and the pointer then points at a frozen shell or freed memory. These handles
// store only numbers that are never reused:
//   - ObjectRef       the object's InstanceId() (unique for the whole run);
//   - ComponentRef<T> the object's InstanceId() and the component's ComponentId().
// Get() looks the numbers up each time and answers nullptr once the object or component is gone, even if a new
// object was later made at the same address. Copy them, store them in containers, compare them, use them as map keys.
//
// Semantics (see docs/ecs_guide.md):
//   - Get() is null from the moment the scene has really destroyed the object (the end of the frame in which
//     Destroy was called). Between Destroy and that point the object is still alive and Get() still returns it;
//     IsDestroyPending() tells you it is on its way out, and Alive() is false for it.
//   - A component that was removed (RemoveComponent) reads as null at once.
//   - Cost: one hash lookup and a scan of the object's components (a handful); do not call it in an inner loop
//     over thousands of items, keep the pointer for the length of one function instead.
//
// Written from the plain idea of a generation-checked handle; header only, no <mutex>/<thread>.
// ---------------------------------------------------------------------------
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>

namespace okay {

class ObjectRef {
public:
    ObjectRef() = default;
    ObjectRef(const GameObject* go) : m_id(go ? go->InstanceId() : 0) {}   // NOLINT: implicit on purpose, like a pointer
    /// The object, or nullptr when it was destroyed (or the ref is empty).
    GameObject* Get() const { return m_id ? GameObject::FromInstanceId(m_id) : nullptr; }
    /// True when the object exists and is not waiting for the end-of-frame destroy.
    bool Alive() const {
        GameObject* g = Get();
        return g && !IsPending(g);
    }
    /// True when Destroy was called on it and the scene has not freed it yet.
    bool IsDestroyPending() const {
        GameObject* g = Get();
        return g && IsPending(g);
    }
    explicit operator bool() const { return Get() != nullptr; }
    /// The number this ref holds (0 = empty). Stable for the life of the object.
    std::uint64_t Id() const { return m_id; }
    void Reset() { m_id = 0; }
    friend bool operator==(const ObjectRef& a, const ObjectRef& b) { return a.m_id == b.m_id; }
    friend bool operator!=(const ObjectRef& a, const ObjectRef& b) { return a.m_id != b.m_id; }
    friend bool operator<(const ObjectRef& a, const ObjectRef& b) { return a.m_id < b.m_id; }
private:
    static bool IsPending(const GameObject* g) { return g->scene() && g->scene()->IsDestroyPending(g); }
    std::uint64_t m_id = 0;
};

template <typename T>
class ComponentRef {
    static_assert(std::is_base_of<Component, T>::value, "T must derive from okay::Component");
public:
    ComponentRef() = default;
    ComponentRef(const T* c)   // NOLINT: implicit on purpose, like a pointer
        : m_object(c ? c->gameObject : nullptr), m_id(c ? c->ComponentId() : 0) {}
    /// The component, or nullptr when it was removed or its object was destroyed.
    T* Get() const {
        if (!m_id) return nullptr;
        GameObject* g = m_object.Get();
        if (!g) return nullptr;
        for (const auto& c : g->Components())
            if (c->ComponentId() == m_id) return dynamic_cast<T*>(c.get());
        return nullptr;
    }
    /// The object the component sits on (nullptr once destroyed).
    GameObject* Object() const { return Get() ? m_object.Get() : nullptr; }
    bool Alive() const { return Get() != nullptr && m_object.Alive(); }
    explicit operator bool() const { return Get() != nullptr; }
    T* operator->() const { return Get(); }   // only after checking `if (ref)`; null when gone, as a raw pointer would be
    std::uint64_t Id() const { return m_id; }
    void Reset() { m_object.Reset(); m_id = 0; }
    friend bool operator==(const ComponentRef& a, const ComponentRef& b) { return a.m_id == b.m_id; }
    friend bool operator!=(const ComponentRef& a, const ComponentRef& b) { return a.m_id != b.m_id; }
private:
    ObjectRef m_object;
    std::uint64_t m_id = 0;
};

} // namespace okay

namespace std {
template <> struct hash<okay::ObjectRef> {
    size_t operator()(const okay::ObjectRef& r) const noexcept { return std::hash<std::uint64_t>()(r.Id()); }
};
}
