#pragma once
// ---------------------------------------------------------------------------
// CachedComponents<T>(scene): the same list as scene.FindObjectsOfType<T>(), without walking the whole scene every time.
//
// FindObjectsOfType visits every object and runs a dynamic_cast on every component of it. The physics steps ask for the
// rigidbodies, colliders, terrains and joints several times each, every step: for a scene of 2,000 objects that was a fifth of
// the frame (docs/perf_log.md). The answer only changes when a component is added or removed or an object is created,
// destroyed, reordered or moved between scenes, and the scene counts exactly those events (Scene::ComponentVersion and
// Scene::StructureVersion), so the list is kept until one of the two counts or the scene changes. Component order within an
// object (GameObject::MoveComponent / ReorderComponents) bumps the structure count too, so the order is never stale.
//
// It returns a COPY, like FindObjectsOfType: a physics step holds its list while contact callbacks add or remove
// components, and that must not change the list under it. Copying a few thousand pointers is cheap next to the walk.
// One scene's list is kept per component type; asking for another scene's list just rebuilds it (what every call cost before).
//
// Main thread only, like the scene update and the physics step that use it. Header only; no <mutex> or <thread>.
// ---------------------------------------------------------------------------
#include "okay/Scene/Scene.hpp"
#include <cstdint>
#include <vector>

namespace okay {

/// Diagnostic switch (tests, the benchmark): true makes CachedComponents walk the scene on every call, as before.
inline bool& ComponentCacheDisabled() { static bool v = false; return v; }

template <typename T>
std::vector<T*> CachedComponents(const Scene& scene) {
    struct Entry {
        bool valid = false;
        std::uint64_t uid = 0, structure = 0, components = 0;
        std::vector<T*> list;
    };
    static Entry e;
    if (ComponentCacheDisabled()) return scene.FindObjectsOfType<T>();
    if (!e.valid || e.uid != scene.Uid() || e.structure != scene.StructureVersion() || e.components != scene.ComponentVersion()) {
        e.list = scene.FindObjectsOfType<T>();
        e.uid = scene.Uid(); e.structure = scene.StructureVersion(); e.components = scene.ComponentVersion();
        e.valid = true;
    }
    return e.list;
}

} // namespace okay
