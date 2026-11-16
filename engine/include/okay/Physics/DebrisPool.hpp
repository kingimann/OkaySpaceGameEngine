#pragma once
// Cheap rigid debris: lots of small boxes that fly, land, settle, sit for a while and fade away, with a hard cap.
//
// This is the helper a digging or breaking game uses for the loose chunks (the voxel terrain's broken-off pieces, a smashed
// wall, rubble). It is built on the engine's ObjectPool, so spawning a chunk reuses a switched-off one instead of making
// objects, and everything about a chunk is bounded:
//   * at most `maxActive` chunks are alive; spawning one more recycles the oldest (the cap is never exceeded),
//   * a chunk lives `lifetime` seconds, or `settledLife` seconds after it first goes to sleep (a chunk that has landed
//     does not need to stay for ever), then shrinks away over `fadeTime` seconds,
//   * chunks sit on their own collision layer and (by default) do not collide with each other, so a pile of 500 chunks costs
//     500 contacts with the ground and not 125 000 pairs. Set `collideWithEachOther` for chunks that must stack.
// Chunks are ordinary Rigidbody3D + BoxCollider3D + MeshRenderer objects: they fall, bounce, slide and sleep under whichever
// physics quality the scene uses (Improved lets a settled chunk sleep at once, which is what keeps the cost low).
//
//   DebrisPool debris(scene);                      // once
//   debris.Spawn(pos, {0.2f, 0.2f, 0.2f}, vel, Color::FromBytes(120, 90, 60));   // as often as you like
//   debris.Update(dt);                             // once per frame
//
// Written for this engine from the ObjectPool and Rigidbody3D it uses; no outside code. Main thread only, like the scene.
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/ObjectPool.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

namespace okay {

struct DebrisSettings {
    int   maxActive = 200;        ///< most chunks alive at once (the oldest is recycled for a new one)
    float lifetime = 12.0f;       ///< seconds a chunk may live at the most
    float settledLife = 4.0f;     ///< seconds it may stay once it has gone to sleep (landed and still)
    float fadeTime = 0.6f;        ///< seconds it takes to shrink away at the end
    float density = 800.0f;       ///< kg per cubic metre (mass = density x volume, kept between minMass and maxMass)
    float minMass = 0.02f, maxMass = 200.0f;
    float friction = 0.6f;
    float bounciness = 0.15f;
    int   layer = 30;             ///< collision layer of every chunk
    bool  collideWithEachOther = false;   ///< off: chunks only hit the world, which is much cheaper
    float angularDrag = 0.3f;
};

class DebrisPool {
public:
    explicit DebrisPool(Scene& scene, const DebrisSettings& settings = DebrisSettings{})
        : m_scene(&scene), m_settings(settings), m_pool(scene) {
        m_pool.maxSize = std::max(8, settings.maxActive / 2);
        Apply();
        GameObject* t = scene.CreateGameObject("Debris template");
        t->transform->localScale = Vec3{0.2f, 0.2f, 0.2f};
        t->layer = m_settings.layer;
        t->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
        t->AddComponent<BoxCollider3D>();
        t->AddComponent<Rigidbody3D>();
        m_pool.SetTemplate(*t);
        scene.DestroyImmediate(t);
    }
    DebrisPool(const DebrisPool&) = delete;
    DebrisPool& operator=(const DebrisPool&) = delete;

    DebrisSettings& settings() { return m_settings; }
    /// Re-apply the settings that live in the scene (the layer collision rule). Call after changing `settings()`.
    void Apply() {
        const int l = m_settings.layer & 31;
        m_scene->physics3D().SetLayerCollision(l, l, m_settings.collideWithEachOther);
    }

    /// Throw one chunk. `size` is its width, height and depth in metres. Returns the object (nullptr when the pool has
    /// nothing to make from). At the cap the oldest chunk is recycled.
    GameObject* Spawn(const Vec3& position, const Vec3& size, const Vec3& velocity, const Color& color,
                      const Vec3& spin = Vec3::Zero) {
        Prune();
        while ((int)m_live.size() >= std::max(1, m_settings.maxActive)) {   // over the cap: the oldest goes
            GameObject* old = GameObject::FromInstanceId(m_live.front().id);
            if (old) m_pool.Release(old);
            m_live.erase(m_live.begin());
            ++m_recycled;
        }
        GameObject* go = m_pool.Get();
        if (!go) return nullptr;
        const Vec3 s{std::max(0.01f, Mathf::Abs(size.x)), std::max(0.01f, Mathf::Abs(size.y)), std::max(0.01f, Mathf::Abs(size.z))};
        go->transform->localPosition = position;
        go->transform->localRotation = Quat::Identity;
        go->transform->localScale = s;
        go->layer = m_settings.layer;
        if (auto* mr = go->GetComponent<MeshRenderer>()) mr->color = color;
        if (auto* rb = go->GetComponent<Rigidbody3D>()) {
            rb->mass = Mathf::Clamp(m_settings.density * s.x * s.y * s.z, m_settings.minMass, m_settings.maxMass);
            rb->friction = m_settings.friction;
            rb->bounciness = m_settings.bounciness;
            rb->angularDrag = m_settings.angularDrag;
            rb->freezeRotation = false;
            rb->allowSleep = true;
            rb->velocity = velocity;
            rb->angularVelocity = spin;
            rb->WakeUp();
        }
        if (auto* col = go->GetComponent<Collider3D>()) col->layer = m_settings.layer;
        m_live.push_back({go->InstanceId(), 0.0f, 0.0f, s, false, 0.0f});
        ++m_spawned;
        return go;
    }

    /// Age the chunks: shrink and release the ones whose time is up. Call once per frame with the frame time.
    void Update(float dt) {
        if (!(dt > 0.0f)) return;
        Prune();
        for (std::size_t i = 0; i < m_live.size();) {
            Entry& e = m_live[i];
            GameObject* go = GameObject::FromInstanceId(e.id);
            if (!go) { m_live.erase(m_live.begin() + (std::ptrdiff_t)i); continue; }
            e.age += dt;
            Rigidbody3D* rb = go->GetComponent<Rigidbody3D>();
            const float fade = std::max(1e-3f, m_settings.fadeTime);
            if (!e.fading) {
                if (rb && rb->sleeping) e.asleep += dt; else e.asleep = 0.0f;
                float remaining = m_settings.lifetime - e.age;
                if (e.asleep > 0.0f) remaining = std::min(remaining, m_settings.settledLife - e.asleep);
                if (remaining > fade) { ++i; continue; }
                e.fading = true;            // from here the fade only runs forward (a chunk that wakes as it shrinks still goes)
                e.fadeLeft = std::max(0.0f, remaining);
            } else {
                e.fadeLeft -= dt;
            }
            if (e.fadeLeft <= 0.0f) {
                m_pool.Release(go);
                m_live.erase(m_live.begin() + (std::ptrdiff_t)i);
                ++m_expired;
                continue;
            }
            go->transform->localScale = e.size * (e.fadeLeft / fade);
            ++i;
        }
    }

    /// Switch every chunk off now (a scene change, a "clean up" key).
    void Clear() {
        for (const Entry& e : m_live)
            if (GameObject* go = GameObject::FromInstanceId(e.id)) m_pool.Release(go);
        m_live.clear();
    }

    int ActiveCount() const { return (int)m_live.size(); }
    int SpawnedTotal() const { return m_spawned; }
    int RecycledTotal() const { return m_recycled; }     ///< chunks cut short because the cap was reached
    int ExpiredTotal() const { return m_expired; }       ///< chunks that lived out their time
    /// How many chunks are asleep right now (the ones that cost almost nothing).
    int SleepingCount() const {
        int n = 0;
        for (const Entry& e : m_live)
            if (GameObject* go = GameObject::FromInstanceId(e.id))
                if (Rigidbody3D* rb = go->GetComponent<Rigidbody3D>()) n += rb->sleeping ? 1 : 0;
        return n;
    }
    ObjectPool& pool() { return m_pool; }

private:
    struct Entry { std::uint64_t id; float age; float asleep; Vec3 size; bool fading = false; float fadeLeft = 0.0f; };
    void Prune() {   // chunks destroyed behind the pool's back (a scene cleared, an editor delete) are forgotten
        const std::uint64_t v = m_scene->StructureVersion();
        if (v == m_pruneVersion) return;
        m_pruneVersion = v;
        m_live.erase(std::remove_if(m_live.begin(), m_live.end(), [this](const Entry& e) {
            GameObject* g = GameObject::FromInstanceId(e.id);
            return !g || g->scene() != m_scene || g->IsDestroyed();
        }), m_live.end());
    }

    Scene* m_scene;
    DebrisSettings m_settings;
    ObjectPool m_pool;
    std::vector<Entry> m_live;            // oldest first
    std::uint64_t m_pruneVersion = 0;
    int m_spawned = 0, m_recycled = 0, m_expired = 0;
};

} // namespace okay
