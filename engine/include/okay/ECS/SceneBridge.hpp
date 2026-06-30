#pragma once
// ---------------------------------------------------------------------------
// okay::ecs::SceneBridge — make ECS entities visible by mirroring them to
// GameObjects, so they render/collide through the existing scene without the ECS
// having to reimplement rendering. Each entity that carries an EcsTransform gets a
// GameObject (created once via your factory, where you attach a MeshRenderer/etc.);
// every sync() copies the entity's transform onto it, and removes GameObjects whose
// entity is gone. Pairs with WorldReplicator: a client applies a network snapshot,
// then sync() draws it.
//
//   ecs::SceneBridge bridge(world, scene, [](Scene& s, ecs::Entity){
//       GameObject* go = s.CreateGameObject("Unit");
//       auto* mr = go->AddComponent<MeshRenderer>(); mr->mesh = Mesh::Cube();
//       return go;
//   });
//   bridge.sync();   // each frame, after systems / network apply
// ---------------------------------------------------------------------------
#include "okay/ECS/World.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include <functional>
#include <unordered_map>
#include <vector>

namespace okay {
namespace ecs {

/// Spatial state of an ECS entity. Trivially copyable, so it can also be a
/// replicated NetWorld component — the same struct drives both the network and
/// the on-screen GameObject.
struct EcsTransform {
    Vec3 position{0, 0, 0};
    Quat rotation = Quat::Identity;
    Vec3 scale{1, 1, 1};
};

class SceneBridge {
public:
    using Factory = std::function<GameObject*(Scene&, Entity)>;

    /// `factory` builds the GameObject (and attaches whatever renderers/colliders)
    /// for a newly-seen entity. If null, a bare GameObject is created.
    SceneBridge(World& world, Scene& scene, Factory factory = {})
        : m_world(world), m_scene(scene), m_factory(std::move(factory)) {}

    /// Reconcile the scene with the ECS: spawn GameObjects for new entities, push
    /// each entity's EcsTransform onto its GameObject, and destroy orphans.
    void sync() {
        // Push transforms; spawn GameObjects for entities seen for the first time.
        m_world.each<EcsTransform>([&](Entity e, EcsTransform& t) {
            GameObject* go = nullptr;
            auto it = m_map.find(e);
            if (it == m_map.end()) {
                go = m_factory ? m_factory(m_scene, e) : m_scene.CreateGameObject("Entity");
                if (!go) return;
                m_map[e] = go;
            } else {
                go = it->second;
            }
            if (go && go->transform) {
                go->transform->localPosition = t.position;
                go->transform->localRotation = t.rotation;
                go->transform->localScale    = t.scale;
            }
        });

        // Destroy GameObjects whose entity is gone (or lost its EcsTransform).
        std::vector<Entity> dead;
        for (auto& kv : m_map)
            if (!m_world.alive(kv.first) || !m_world.has<EcsTransform>(kv.first))
                dead.push_back(kv.first);
        for (Entity e : dead) {
            if (GameObject* go = m_map[e]) m_scene.Destroy(go);
            m_map.erase(e);
        }
    }

    /// The GameObject mirroring an entity, or nullptr.
    GameObject* gameObjectFor(Entity e) const {
        auto it = m_map.find(e);
        return it == m_map.end() ? nullptr : it->second;
    }
    std::size_t mappedCount() const { return m_map.size(); }

private:
    World&  m_world;
    Scene&  m_scene;
    Factory m_factory;
    std::unordered_map<Entity, GameObject*> m_map;
};

} // namespace ecs
} // namespace okay
