#pragma once
// ---------------------------------------------------------------------------
// okay::ecs — a small, data-oriented Entity-Component-System that lives alongside
// the GameObject/Component scene (it doesn't replace it). Entities are plain ids;
// components are plain structs stored in packed sparse-set pools; systems are just
// loops over queries. Designed to pair with NetWorld for built-in replication.
//
//   ecs::World w;
//   auto e = w.create();
//   w.add<Position>(e, {0,0,0});
//   w.add<Velocity>(e, {1,0,0});
//   w.each<Position, Velocity>([&](ecs::Entity, Position& p, Velocity& v){ p.x += v.x; });
// ---------------------------------------------------------------------------
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <typeindex>
#include <memory>

namespace okay {
namespace ecs {

using Entity = std::uint32_t;
constexpr Entity Null = 0;

/// Packed component storage with O(1) add/get/remove (sparse set). Dense arrays
/// mean iteration is cache-friendly; `remove` swaps with the last element.
template <class T>
class Pool {
public:
    std::vector<Entity> dense;     // entity id at each dense slot
    std::vector<T>      comps;     // component at each dense slot
    std::unordered_map<Entity, std::size_t> sparse;  // entity -> dense index

    T& add(Entity e, const T& v) {
        auto it = sparse.find(e);
        if (it != sparse.end()) { comps[it->second] = v; return comps[it->second]; }
        sparse[e] = dense.size();
        dense.push_back(e);
        comps.push_back(v);
        return comps.back();
    }
    bool has(Entity e) const { return sparse.find(e) != sparse.end(); }
    T* get(Entity e) {
        auto it = sparse.find(e);
        return it == sparse.end() ? nullptr : &comps[it->second];
    }
    void remove(Entity e) {
        auto it = sparse.find(e);
        if (it == sparse.end()) return;
        std::size_t i = it->second, last = dense.size() - 1;
        dense[i] = dense[last];
        comps[i] = comps[last];
        sparse[dense[i]] = i;
        dense.pop_back();
        comps.pop_back();
        sparse.erase(it);
    }
    void clear() { dense.clear(); comps.clear(); sparse.clear(); }
    std::size_t size() const { return dense.size(); }
};

// Type-erased pool handle so the World can destroy an entity's components without
// knowing every type.
struct IPool {
    virtual ~IPool() = default;
    virtual void remove(Entity e) = 0;
    virtual void clear() = 0;
};
template <class T>
struct TPool : IPool {
    Pool<T> pool;
    void remove(Entity e) override { pool.remove(e); }
    void clear() override { pool.clear(); }
};

class World {
public:
    /// Make a fresh entity.
    Entity create() { Entity e = m_next++; m_alive[e] = true; return e; }

    /// Make (or reuse) a specific entity id — used when mirroring a networked world
    /// so client and server share ids.
    Entity ensure(Entity e) {
        if (e >= m_next) m_next = e + 1;
        m_alive[e] = true;
        return e;
    }

    void destroy(Entity e) {
        auto it = m_alive.find(e);
        if (it == m_alive.end()) return;
        m_alive.erase(it);
        for (auto& kv : m_pools) kv.second->remove(e);
    }

    bool alive(Entity e) const {
        auto it = m_alive.find(e);
        return it != m_alive.end() && it->second;
    }
    std::size_t count() const { return m_alive.size(); }

    template <class T> T& add(Entity e, const T& v = T{}) { return pool<T>().add(e, v); }
    template <class T> T* get(Entity e) { return pool<T>().get(e); }
    template <class T> bool has(Entity e) { return pool<T>().has(e); }
    template <class T> void remove(Entity e) { pool<T>().remove(e); }
    template <class T> void clearPool() { pool<T>().clear(); }

    /// All live entity ids (order unspecified).
    std::vector<Entity> entities() const {
        std::vector<Entity> out; out.reserve(m_alive.size());
        for (auto& kv : m_alive) if (kv.second) out.push_back(kv.first);
        return out;
    }

    /// Run `fn(entity, T&)` for every entity that has a T.
    template <class T, class Fn> void each(Fn fn) {
        Pool<T>& p = pool<T>();
        for (std::size_t i = 0; i < p.dense.size(); ++i) fn(p.dense[i], p.comps[i]);
    }
    /// Run `fn(entity, A&, B&)` for every entity that has both A and B.
    template <class A, class B, class Fn> void each(Fn fn) {
        Pool<A>& pa = pool<A>();
        Pool<B>& pb = pool<B>();
        for (std::size_t i = 0; i < pa.dense.size(); ++i) {
            Entity e = pa.dense[i];
            if (B* b = pb.get(e)) fn(e, pa.comps[i], *b);
        }
    }

    /// Direct pool access (used by NetWorld for replication).
    template <class T> Pool<T>& pool() {
        std::type_index ti(typeid(T));
        auto it = m_pools.find(ti);
        if (it == m_pools.end()) {
            auto up = std::make_unique<TPool<T>>();
            TPool<T>* raw = up.get();
            m_pools.emplace(ti, std::move(up));
            return raw->pool;
        }
        return static_cast<TPool<T>*>(it->second.get())->pool;
    }

private:
    Entity m_next = 1;   // 0 is reserved (Null)
    std::unordered_map<Entity, bool> m_alive;
    std::unordered_map<std::type_index, std::unique_ptr<IPool>> m_pools;
};

} // namespace ecs
} // namespace okay
