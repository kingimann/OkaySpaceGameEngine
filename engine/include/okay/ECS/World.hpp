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
#include <algorithm>
#include <cstdint>
#include <vector>
#include <deque>
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <functional>
#include <tuple>

namespace okay {
namespace ecs {

using Entity = std::uint32_t;
constexpr Entity Null = 0;

/// Packed component storage with O(1) add/get/remove (sparse set). Dense arrays
/// mean iteration is cache-friendly; `remove` swaps with the last element.
template <class T>
class Pool {
public:
    std::vector<Entity> dense;     // entity id at each dense slot (Null = hole, only while iterating)
    std::deque<T>       comps;     // component at each dense slot (references survive growth)
    std::unordered_map<Entity, std::size_t> sparse;  // entity -> dense index
    std::unique_ptr<T>  sink;      // scratch target for add() on a dead entity

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
    /// Remove a component. Outside iteration this swaps the last entry into the slot;
    /// while an `each` loop is running it only leaves a hole (compacted when the loop
    /// ends), so nothing moves under the loop and every reference it handed out stays valid.
    void remove(Entity e) {
        auto it = sparse.find(e);
        if (it == sparse.end()) return;
        std::size_t i = it->second;
        sparse.erase(it);
        if (m_iter > 0) { dense[i] = Null; m_holes = true; return; }
        eraseSlot(i);
    }
    void clear() {
        if (m_iter > 0) {
            for (std::size_t i = 0; i < dense.size(); ++i) if (dense[i] != Null) { sparse.erase(dense[i]); dense[i] = Null; }
            m_holes = true;
            return;
        }
        dense.clear(); comps.clear(); sparse.clear();
    }
    std::size_t size() const { return sparse.size(); }

    void beginIter() { ++m_iter; }
    void endIter() {
        if (--m_iter > 0 || !m_holes) return;
        m_holes = false;
        for (std::size_t i = 0; i < dense.size();) {
            if (dense[i] == Null) eraseSlot(i); else ++i;
        }
    }
private:
    // Swap-remove slot i (its sparse entry is already gone); a hole swapped into i is
    // re-examined by the caller loop.
    void eraseSlot(std::size_t i) {
        std::size_t last = dense.size() - 1;
        if (i != last) {
            dense[i] = dense[last];
            comps[i] = std::move(comps[last]);
            if (dense[i] != Null) sparse[dense[i]] = i;
        }
        dense.pop_back();
        comps.pop_back();
    }
    int  m_iter = 0;
    bool m_holes = false;
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
    /// Make a fresh entity. Ids count up from 1 and are not reused, so a stale id never
    /// names a newer entity. After 2^32 - 1 creations the counter wraps; it then skips Null
    /// and every id that is still alive.
    Entity create() { Entity e = takeId(); m_alive[e] = true; return e; }

    /// Make (or reuse) a specific entity id — used when mirroring a networked world
    /// so client and server share ids.
    Entity ensure(Entity e) {
        // Null is never a live entity, and the top id is kept out (these ids come off the
        // network, so a bad one must not push the counter to the wrap point).
        if (e == Null || e == 0xFFFFFFFFu) return Null;
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

    /// Add (or overwrite) a component. A dead or null entity gets nothing stored:
    /// a stale handle must not leave a component that queries still visit. The
    /// returned reference then points at a scratch copy that is dropped.
    template <class T> T& add(Entity e, const T& v = T{}) {
        Pool<T>& p = pool<T>();
        if (!alive(e)) { p.sink = std::make_unique<T>(v); return *p.sink; }
        return p.add(e, v);
    }
    template <class T> T* get(Entity e) { return pool<T>().get(e); }
    template <class T> bool has(Entity e) { return pool<T>().has(e); }
    template <class T> void remove(Entity e) { pool<T>().remove(e); }
    template <class T> void clearPool() { pool<T>().clear(); }

    /// All live entity ids, in ascending order (the same on every platform and every run, so a
    /// loop over it, a full snapshot and a checksum of it do not depend on the hash table's layout).
    std::vector<Entity> entities() const {
        std::vector<Entity> out; out.reserve(m_alive.size());
        for (auto& kv : m_alive) if (kv.second) out.push_back(kv.first);
        std::sort(out.begin(), out.end());
        return out;
    }

    /// Run `fn(entity, T&)` for every entity that has a T. Defined behaviour under
    /// mutation: the entities to visit are fixed when the loop starts, so the
    /// callback may destroy / remove ANY entity (one removed before its turn is not
    /// visited; nothing is visited twice or skipped), and entities or components
    /// added during the loop are not visited until the next pass. While a loop runs,
    /// removals leave holes instead of moving entries, so the `T&` handed to the
    /// callback (and any other component reference) stays valid until the loop ends.
    /// `commands()` + `flush()` remain the way to batch structural edits.
    template <class T, class Fn> void each(Fn fn) {
        Pool<T>& p = pool<T>();
        std::vector<Entity> snap = p.dense;
        IterGuard g(m_iterDepth, p);
        for (Entity e : snap) {
            if (e == Null) continue;
            if (T* c = p.get(e)) fn(e, *c);
        }
    }
    /// Run `fn(entity, A&, B&)` for every entity that has both A and B (same
    /// mutation rules as the single-component `each`).
    template <class A, class B, class Fn> void each(Fn fn) {
        Pool<A>& pa = pool<A>();
        Pool<B>& pb = pool<B>();
        std::vector<Entity> snap = pa.dense;
        IterGuard g(m_iterDepth, pa, pb);
        for (Entity e : snap) {
            if (e == Null) continue;
            A* a = pa.get(e);
            B* b = a ? pb.get(e) : nullptr;
            if (a && b) fn(e, *a, *b);
        }
    }

    /// True while an `each` loop is running.
    bool iterating() const { return m_iterDepth > 0; }

    // ---- command buffer -------------------------------------------------
    /// Deferred structural edits, recorded during iteration and applied in order at
    /// `World::flush()`. `create()` reserves its id immediately (so later commands
    /// can target it) but the entity only becomes alive at flush.
    class CommandBuffer {
    public:
        CommandBuffer() = default;
        Entity create() { Entity e = m_w->reserveId(); m_cmds.push_back([e](World& w) { w.activate(e); }); return e; }
        void destroy(Entity e) { m_cmds.push_back([e](World& w) { w.destroy(e); }); }
        template <class T> void add(Entity e, const T& v = T{}) {
            m_cmds.push_back([e, v](World& w) { w.add<T>(e, v); });
        }
        /// Same as add (overwrites an existing component).
        template <class T> void set(Entity e, const T& v) { add<T>(e, v); }
        template <class T> void remove(Entity e) {
            m_cmds.push_back([e](World& w) { w.remove<T>(e); });
        }
        std::size_t size() const { return m_cmds.size(); }
        bool empty() const { return m_cmds.empty(); }
        void clear() { m_cmds.clear(); }
    private:
        friend class World;
        World* m_w = nullptr;
        std::vector<std::function<void(World&)>> m_cmds;
    };

    CommandBuffer& commands() { m_commands.m_w = this; return m_commands; }

    /// Apply every recorded command in order and clear the buffer; returns how many
    /// ran. Does nothing (returns 0, commands stay queued) while an `each` loop is
    /// running, so a flush from inside a callback can't pull the rug out.
    std::size_t flush() {
        if (m_iterDepth > 0) return 0;
        std::vector<std::function<void(World&)>> run;
        run.swap(m_commands.m_cmds);
        for (auto& c : run) c(*this);
        return run.size();
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
    // Holds the world's loop depth and pins the iterated pools (no slot moves).
    template <class... Ps> struct IterGuard {
        int& d; std::tuple<Ps&...> ps;
        explicit IterGuard(int& x, Ps&... p) : d(x), ps(p...) {
            ++d; std::apply([](Ps&... q) { (q.beginIter(), ...); }, ps);
        }
        ~IterGuard() { std::apply([](Ps&... q) { (q.endIter(), ...); }, ps); --d; }
    };
    // The next id that is not Null and not alive; the counter wraps past the top id back to 1.
    Entity takeId() {
        for (;;) {
            const Entity e = m_next;
            m_next = (m_next == 0xFFFFFFFFu) ? 1u : m_next + 1u;
            if (e != Null && m_alive.find(e) == m_alive.end()) return e;
        }
    }
    Entity reserveId() { return takeId(); }
    void activate(Entity e) { if (e != Null) m_alive[e] = true; }
    int m_iterDepth = 0;
    CommandBuffer m_commands;
    Entity m_next = 1;   // 0 is reserved (Null)
    std::unordered_map<Entity, bool> m_alive;
    std::unordered_map<std::type_index, std::unique_ptr<IPool>> m_pools;
};

} // namespace ecs
} // namespace okay
