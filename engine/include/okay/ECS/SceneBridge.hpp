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
//
// The bridge keeps instance ids, never pointers, so a GameObject the scene destroyed or freed
// (a Destroy call, a scene reload) is noticed on the next sync instead of dereferenced. By default
// the entity still exists, so the object is made again through the factory (respawnLost). A
// non-finite EcsTransform (a damaged snapshot) is skipped: the object keeps its last good pose.
// Not thread safe: call it from the thread that owns the scene.
// ---------------------------------------------------------------------------
#include "okay/ECS/World.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mathf.hpp"
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>
#include <cmath>

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
    /// Smoothly ease GameObjects toward their entity's transform instead of
    /// snapping — for networked worlds where snapshots arrive at a low rate, this
    /// hides the steps. Pass a real dt to sync(); newly-spawned objects always snap.
    bool  interpolate = false;
    float interpRate  = 12.0f;   // higher = snappier (0: the object does not move; negative or NaN: it snaps)

    /// When the GameObject of a live entity is gone (destroyed by other code, or the scene was
    /// cleared), create it again with the factory on the next sync; the new object snaps to the
    /// entity's pose. Off: the entity stays without an object until it dies.
    bool  respawnLost = true;

    SceneBridge(World& world, Scene& scene, Factory factory = {})
        : m_world(world), m_scene(scene), m_factory(std::move(factory)) {}

    /// Reconcile the scene with the ECS: spawn GameObjects for new entities, push
    /// each entity's EcsTransform onto its GameObject (snapping or easing per
    /// `interpolate`), and destroy orphans. Pass the frame dt for interpolation.
    void sync(float dt = 0.0f) {
        float a = (interpolate && dt > 0.0f) ? -std::expm1(-interpRate * dt) : 1.0f;   // 1 - e^(-rate*dt), exact for tiny steps
        if (!(a >= 0.0f && a <= 1.0f)) a = 1.0f;   // a bad rate or dt must not throw the object around: snap instead
        // Push transforms; spawn GameObjects for entities seen for the first time.
        m_world.each<EcsTransform>([&](Entity e, EcsTransform& t) {
            GameObject* go = nullptr;
            bool fresh = false;
            auto it = m_map.find(e);
            if (it == m_map.end()) {
                go = m_factory ? m_factory(m_scene, e) : m_scene.CreateGameObject("Entity");
                if (!go) return;
                m_map[e] = go->InstanceId();
                fresh = true;   // a new object snaps to its spawn pose
            } else {
                go = GameObject::FromInstanceId(it->second);   // null if the scene freed or destroyed it
                if (!go && respawnLost) {
                    go = m_factory ? m_factory(m_scene, e) : m_scene.CreateGameObject("Entity");
                    if (!go) return;
                    it->second = go->InstanceId();
                    fresh = true;
                }
            }
            if (go && go->transform && Finite(t)) {
                Transform* tr = go->transform;
                float w = fresh ? 1.0f : a;
                tr->localPosition = Vec3::Lerp(tr->localPosition, t.position, w);
                tr->localRotation = Quat::Slerp(tr->localRotation, t.rotation, w);
                tr->localScale    = Vec3::Lerp(tr->localScale, t.scale, w);
            }
        });

        // Destroy GameObjects whose entity is gone (or lost its EcsTransform).
        std::vector<Entity> dead;
        for (auto& kv : m_map)
            if (!m_world.alive(kv.first) || !m_world.has<EcsTransform>(kv.first))
                dead.push_back(kv.first);
        for (Entity e : dead) {
            if (GameObject* go = GameObject::FromInstanceId(m_map[e])) m_scene.Destroy(go);
            m_map.erase(e);
        }
    }

    /// The GameObject mirroring an entity, or nullptr.
    GameObject* gameObjectFor(Entity e) const {
        auto it = m_map.find(e);
        return it == m_map.end() ? nullptr : GameObject::FromInstanceId(it->second);
    }
    std::size_t mappedCount() const { return m_map.size(); }

private:
    static bool Finite(const EcsTransform& t) {
        return std::isfinite(t.position.x) && std::isfinite(t.position.y) && std::isfinite(t.position.z) &&
               std::isfinite(t.rotation.x) && std::isfinite(t.rotation.y) && std::isfinite(t.rotation.z) && std::isfinite(t.rotation.w) &&
               std::isfinite(t.scale.x) && std::isfinite(t.scale.y) && std::isfinite(t.scale.z);
    }
    World&  m_world;
    Scene&  m_scene;
    Factory m_factory;
    std::unordered_map<Entity, std::uint64_t> m_map;   // instance ids: a GameObject the scene freed must not dangle here
};

} // namespace ecs
} // namespace okay
