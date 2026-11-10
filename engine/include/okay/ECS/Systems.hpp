#pragma once
// ---------------------------------------------------------------------------
// okay::ecs::SystemScheduler — register systems (functions over the World) and run
// them in order each tick. A system is just `void(World&, float dt)`; this keeps
// the ordering, naming and enable/disable in one place instead of a hand-written
// call list.
//
//   ecs::SystemScheduler sched;
//   sched.add("movement", [](ecs::World& w, float dt){
//       w.each<Position, Velocity>([&](ecs::Entity, Position& p, Velocity& v){
//           p.x += v.x * dt; p.y += v.y * dt; p.z += v.z * dt; });
//   });
//   sched.run(world, dt);   // each frame
//
// Order is deterministic: the order of add() calls, every tick. A system may add, remove,
// enable or disable systems (itself included) while a tick runs; see run().
// Not thread safe: use it from one thread.
// ---------------------------------------------------------------------------
#include "okay/ECS/World.hpp"
#include <functional>
#include <string>
#include <vector>

namespace okay {
namespace ecs {

class SystemScheduler {
public:
    using System = std::function<void(World&, float)>;

    /// Append a system. Systems run in the order they're added. Two systems may share a
    /// name (setEnabled and remove then act on all of them). Added from inside a running
    /// system, it joins the order at the end of that tick and first runs in the next one.
    void add(const std::string& name, System fn) {
        Entry e{name, std::move(fn), true, false};
        if (m_running > 0) m_pending.push_back(std::move(e));
        else m_systems.push_back(std::move(e));
    }

    /// Remove every system with this name; returns how many were removed. From inside a
    /// running system the removal is immediate for the rest of the tick (a removed system
    /// that has not run yet is skipped); the entry itself is dropped when the tick ends, so
    /// a system may remove itself.
    std::size_t remove(const std::string& name) {
        std::size_t n = 0;
        for (Entry& e : m_systems) if (!e.removed && e.name == name) { ++n; e.removed = true; }
        for (std::size_t i = m_pending.size(); i-- > 0;)
            if (m_pending[i].name == name) { ++n; m_pending.erase(m_pending.begin() + (std::ptrdiff_t)i); }
        if (m_running == 0) dropRemoved();
        return n;
    }

    /// Enable/disable a named system without removing it.
    void setEnabled(const std::string& name, bool on) {
        for (Entry& e : m_systems) if (e.name == name) e.enabled = on;
        for (Entry& e : m_pending) if (e.name == name) e.enabled = on;
    }
    bool enabled(const std::string& name) const {
        for (const Entry& e : m_systems) if (!e.removed && e.name == name) return e.enabled;
        for (const Entry& e : m_pending) if (e.name == name) return e.enabled;
        return false;
    }
    /// Whether a system with this name is registered (enabled or not).
    bool has(const std::string& name) const {
        for (const Entry& e : m_systems) if (!e.removed && e.name == name) return true;
        for (const Entry& e : m_pending) if (e.name == name) return true;
        return false;
    }
    /// The registered names, in run order.
    std::vector<std::string> names() const {
        std::vector<std::string> out;
        for (const Entry& e : m_systems) if (!e.removed) out.push_back(e.name);
        for (const Entry& e : m_pending) out.push_back(e.name);
        return out;
    }

    /// Run every enabled system once, in order. Changes a system makes to the list while
    /// the tick runs (add, remove, clear) never move or free a system that is executing.
    void run(World& world, float dt) {
        RunGuard guard(*this);
        for (std::size_t i = 0, n = m_systems.size(); i < n; ++i) {   // m_systems itself is not resized during a tick
            Entry& e = m_systems[i];
            if (e.enabled && !e.removed && e.fn) e.fn(world, dt);
        }
    }

    std::size_t count() const {
        std::size_t n = m_pending.size();
        for (const Entry& e : m_systems) if (!e.removed) ++n;
        return n;
    }
    void clear() {
        m_pending.clear();
        if (m_running > 0) { for (Entry& e : m_systems) e.removed = true; }
        else m_systems.clear();
    }

private:
    struct Entry { std::string name; System fn; bool enabled; bool removed; };
    // Counts nested run() calls; the outermost one applies what was added and removed meanwhile.
    struct RunGuard {
        SystemScheduler& s;
        explicit RunGuard(SystemScheduler& sched) : s(sched) { ++s.m_running; }
        ~RunGuard() {
            if (--s.m_running > 0) return;
            s.dropRemoved();
            for (Entry& e : s.m_pending) s.m_systems.push_back(std::move(e));
            s.m_pending.clear();
        }
        RunGuard(const RunGuard&) = delete;
        RunGuard& operator=(const RunGuard&) = delete;
    };
    void dropRemoved() {
        for (std::size_t i = m_systems.size(); i-- > 0;)
            if (m_systems[i].removed) m_systems.erase(m_systems.begin() + (std::ptrdiff_t)i);
    }

    std::vector<Entry> m_systems;
    std::vector<Entry> m_pending;   // added while a tick was running
    int m_running = 0;
};

} // namespace ecs
} // namespace okay
