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

    /// Append a system. Systems run in the order they're added.
    void add(const std::string& name, System fn) {
        m_systems.push_back({name, std::move(fn), true});
    }

    /// Enable/disable a named system without removing it.
    void setEnabled(const std::string& name, bool on) {
        for (Entry& e : m_systems) if (e.name == name) e.enabled = on;
    }
    bool enabled(const std::string& name) const {
        for (const Entry& e : m_systems) if (e.name == name) return e.enabled;
        return false;
    }

    /// Run every enabled system once, in order.
    void run(World& world, float dt) {
        for (Entry& e : m_systems) if (e.enabled && e.fn) e.fn(world, dt);
    }

    std::size_t count() const { return m_systems.size(); }
    void clear() { m_systems.clear(); }

private:
    struct Entry { std::string name; System fn; bool enabled; };
    std::vector<Entry> m_systems;
};

} // namespace ecs
} // namespace okay
