#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include <cstdint>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace okay {

class Scene;
class GameObject;

/// Reuses spawned objects (bullets, hit sparks, pickups) instead of creating and
/// destroying them. Get() hands out an active copy of the template; Release() switches
/// it off and keeps it for the next Get(). Components reset themselves in OnEnable /
/// OnDisable, which the Scene delivers at the start of the next frame, so an object
/// released this frame is only reused from the next one (Get makes a new one
/// meanwhile). The pool tolerates objects destroyed behind its back and a scene that
/// was cleared.
///
/// A reused object is put back to what a fresh copy of the template has, as far as the pool can tell:
/// the local pose, `active`, layer and tag of the root and of every object below it (when the hierarchy
/// still has the template's shape), the `enabled` flag of each component (when the object's components are
/// still the template's), and each component's own runtime state through Component::OnPoolReuse
/// (Rigidbody2D / Rigidbody3D velocities and forces, Lifetime's elapsed time). A delayed
/// Scene::Destroy(go, seconds) that was waiting for the object is cancelled when it is released, so the
/// next use is not cut short by the old request.
///
/// Main thread only, like the scene it serves (a pool does not lock anything), and it must not outlive its Scene.
///
///   ObjectPool bullets(scene);
///   bullets.SetPrefabFile("Assets/Bullet.okayprefab");
///   bullets.Prewarm(32);
///   GameObject* b = bullets.Get(muzzle);   ...   bullets.Release(b);
class ObjectPool {
public:
    explicit ObjectPool(Scene& scene) : m_scene(&scene) {}
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    /// What the pool makes. The source is captured now (a prefab file is read once; a
    /// template object is serialized once and may be destroyed afterwards). False if the
    /// file cannot be read. A destroyed template object is ignored (the source stays as it was).
    bool SetPrefabFile(const std::string& path);
    void SetTemplate(const GameObject& templateObject);
    bool HasSource() const { return !m_text.empty(); }

    /// Most idle objects kept. A Release beyond that destroys the object.
    int maxSize = 64;

    /// Make `count` idle objects ahead of time (never more than maxSize idle).
    void Prewarm(int count);
    /// An active object at its template pose, reused when one is idle. nullptr without a source.
    GameObject* Get();
    GameObject* Get(const Vec3& position);
    /// Put an object from this pool back. False if it is not one of this pool's.
    bool Release(GameObject* go);
    /// Destroy every idle object.
    void Clear();

    int IdleCount() const;
    /// Objects handed out and not released.
    int ActiveCount() const;
    /// Objects made so far / reused from idle (for tuning the pre-warm count).
    int CreatedCount() const { return m_created; }
    int ReuseCount() const { return m_reused; }
    bool Owns(const GameObject* go) const;
    /// Diagnostic: how many objects the pool has looked up to answer Get, Release and the counts since it
    /// was made. Get and Release look at a few; IdleCount and ActiveCount look at every object the pool
    /// holds, so ask for those rarely (an overlay once a frame is fine).
    std::uint64_t LookupCount() const { return m_lookups; }

private:
    struct NodeState {   // what one object of the template looked like, in depth-first order
        Vec3 position, scale{1, 1, 1};
        Quat rotation = Quat::Identity;
        bool active = true;
        int layer = 0;
        std::string tag;
        std::vector<const std::type_info*> types;   // its components, in order, the Transform first
        std::vector<bool> enabled;                  // ... and whether each one is switched on
    };
    enum : unsigned char { kActive = 0, kIdle = 1, kCooling = 2 };   // where an object of the pool is
    GameObject* Make();
    void Prune() const;                  // forget every object that is gone (O(everything held): for the counts)
    void Settle() const;                 // objects released before the last Update become reusable
    void Capture(const GameObject& root);
    void ResetForReuse(GameObject& root);
    GameObject* Lookup(std::uint64_t id) const;   // the live object of this scene with that id, or nullptr

    Scene* m_scene;
    std::string m_text;
    std::string m_prefabPath;
    std::vector<NodeState> m_nodes;      // the template's objects, root first
    mutable std::vector<std::uint64_t> m_idle;      // reusable now, the most recently released last
    mutable std::vector<std::uint64_t> m_cooling;   // released since the scene's last Update: their OnDisable is still owed
    mutable std::uint64_t m_coolStamp = 0;          // Scene::UpdateCount() when m_cooling was started
    mutable std::unordered_map<std::uint64_t, unsigned char> m_owned;   // every object the pool made and still holds, and where it is
    mutable std::uint64_t m_pruneStructure = 0;     // Scene::StructureVersion() at the last full prune
    mutable std::uint64_t m_lookups = 0;
    int m_created = 0, m_reused = 0;
};

} // namespace okay
