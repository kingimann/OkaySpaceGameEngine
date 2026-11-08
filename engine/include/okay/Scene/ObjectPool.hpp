#pragma once
#include "okay/Math/Vec3.hpp"
#include <cstdint>
#include <string>
#include <unordered_set>
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
    /// file cannot be read.
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

private:
    GameObject* Make();
    void Prune() const;

    Scene* m_scene;
    std::string m_text;
    std::string m_prefabPath;
    Vec3 m_pos, m_scale{1, 1, 1};
    float m_rotX = 0, m_rotY = 0, m_rotZ = 0, m_rotW = 1;
    mutable std::vector<std::uint64_t> m_idle;
    mutable std::unordered_set<std::uint64_t> m_owned;
    int m_created = 0, m_reused = 0;
};

} // namespace okay
